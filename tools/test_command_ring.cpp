/* parallel-rdp's command ring (parallel-rdp/command_ring.cpp) through a
 * small ring, so commands wrap all the time: every command reaches the
 * command thread whole, in order and unchanged, whether it is run where
 * it lies in the ring or copied out because it wraps, and its words stay
 * as they were for as long as it runs (the space it takes is not handed
 * back to the producer before then). The processor is a stand-in that
 * checks each command twice, before and after a stretch of work.
 * Written C89-shaped; C++ only for the class under test. */
#include <stdio.h>
#include <stdlib.h>

#include <retro_atomic.h>

#include "command_ring.hpp"
#include "rdp_device.hpp"

#define RING_WORDS 64u
#define COMMANDS   200000u
#define MAX_WORDS  44u /* the RDP's longest command */

static unsigned next_seq;
static unsigned mismatches;
static volatile unsigned sink;

static unsigned size_of(unsigned seq)
{
	return 1u + (seq * 7u) % MAX_WORDS;
}

static uint32_t word_of(unsigned seq, unsigned i)
{
	/* the top byte is never one of the processor's meta ops */
	return (0x3fu << 24) | ((seq * 131u + i * 17u) & 0xffffffu);
}

static unsigned check(unsigned seq, unsigned num_words, const uint32_t *words)
{
	unsigned i, bad = num_words != size_of(seq);
	for (i = 0; !bad && i < num_words; i++)
		bad = words[i] != word_of(seq, i);
	return bad;
}

/* the command thread's side: runs every command, MetaIdle included */
void RDP::CommandProcessor::enqueue_command_direct(unsigned num_words, const uint32_t *words)
{
	unsigned k, seq;
	if (num_words == 1 && words[0] == (uint32_t(Op::MetaIdle) << 24))
		return;
	seq = next_seq++;
	mismatches += check(seq, num_words, words);
	/* a while later, the words must still be the same */
	for (k = 0; k < 200; k++)
		sink += k * words[k % num_words];
	mismatches += check(seq, num_words, words);
}

int main(void)
{
	RDP::CommandRing ring;
	uint32_t words[MAX_WORDS];
	unsigned seq, i;

	if (!ring.init(reinterpret_cast<RDP::CommandProcessor *>(&ring), RING_WORDS))
	{
		fprintf(stderr, "test_command_ring: no command thread\n");
		return 1;
	}
	for (seq = 0; seq < COMMANDS; seq++)
	{
		for (i = 0; i < size_of(seq); i++)
			words[i] = word_of(seq, i);
		ring.enqueue_command(size_of(seq), words);
	}
	ring.drain();
	ring.teardown_thread();
	if (next_seq != COMMANDS || mismatches)
	{
		fprintf(stderr, "test_command_ring: FAIL: %u of %u commands run, %u changed\n",
		        next_seq, COMMANDS, mismatches);
		return 1;
	}
	printf("test_command_ring: ok\n");
	return 0;
}
