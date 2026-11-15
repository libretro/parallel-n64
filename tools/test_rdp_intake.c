/* test_rdp_intake -- the RDP's command list as the frontend reads it
 * (mupen64plus-video-paraLLEl/rdp_intake.c).
 *
 * - Commands come out whole and in order however the lists split them:
 *   random command streams cut at random places into lists, read from
 *   RDRAM, from DMEM (XBUS, wrapping at 4 KiB) or from the HLE emitter's
 *   buffer, give the commands of the stream, each sync full followed by
 *   its callback; the processor's own opcodes (1 to 7) are not handed on.
 * - A list with nothing kept from the last is handed on where it lies,
 *   not copied.
 * - An empty list, one too long to keep, and an HLE list past its buffer
 *   are not read past.
 * - The state commands kept, and replayed in an order that sets each
 *   before what depends on it: tiles by tile, not the loads, rectangles
 *   or syncs; forgotten when asked.
 *
 * Built by `make check-core` (top-level Makefile). C89. Exit status 0 on
 * pass.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rdp_intake.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_rdp_intake: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

#define RDRAM_BYTES (16u << 20)
#define MAX_OUT 200000u

static uint8_t *rdram;
static uint8_t dmem[0x1000];
static uint8_t hle[0x10000];
static rdp_intake_t in;

/* what was handed on: the words of each command, and where each began */
static uint32_t out[MAX_OUT];
static unsigned num_out, num_cmds, num_syncs, syncs_at_cmd[4096], num_sync_marks;
static const uint32_t *last_ptr;

static void enqueue(void *user, unsigned num_words, const uint32_t *words)
{
	(void)user;
	last_ptr = words;
	if (num_out + num_words <= MAX_OUT)
		memcpy(out + num_out, words, num_words * 4);
	num_out += num_words;
	num_cmds++;
}

static void sync_full(void *user)
{
	(void)user;
	if (num_sync_marks < 4096)
		syncs_at_cmd[num_sync_marks++] = num_cmds;
	num_syncs++;
}

static void reset(void)
{
	rdp_intake_callbacks_t cb;
	cb.enqueue = enqueue;
	cb.sync_full = sync_full;
	cb.user = NULL;
	rdp_intake_init(&in, &cb);
	num_out = num_cmds = num_syncs = num_sync_marks = 0;
}

static uint32_t rnd_state = 12345;

static uint32_t rnd(void)
{
	rnd_state = rnd_state * 1103515245u + 12345u;
	return rnd_state >> 8;
}

/* A stream of @n_words 64-bit words of whole commands: the reference */
static uint32_t stream[2 * 20000];
static uint32_t expect[2 * 20000];
static unsigned num_expect, num_expect_cmds, expect_syncs_at[4096], num_expect_syncs;

static unsigned make_stream(unsigned max_words)
{
	unsigned n = 0, i;
	num_expect = num_expect_cmds = num_expect_syncs = 0;
	while (n < max_words)
	{
		unsigned op = rnd() & 63, len;
		/* (sync full now and then) */
		if ((rnd() & 15) == 0)
			op = 0x29;
		len = rdp_intake_command_words(op);
		if (n + len > max_words)
			break;
		for (i = 0; i < 2 * len; i++)
			stream[2 * n + i] = rnd();
		stream[2 * n] = (stream[2 * n] & 0x00ffffffu) | (op << 24) | ((rnd() & 3u) << 30);
		if (op >= 8)
		{
			memcpy(expect + num_expect, stream + 2 * n, 2 * len * 4);
			num_expect += 2 * len;
			num_expect_cmds++;
		}
		if (op == 0x29 && num_expect_syncs < 4096)
			expect_syncs_at[num_expect_syncs++] = num_expect_cmds;
		n += len;
	}
	return n;
}

enum source { FROM_RDRAM, FROM_DMEM, FROM_HLE };

/* the stream's words in @src, list by list, cut at random */
static void feed(enum source src, unsigned n)
{
	unsigned done = 0;
	uint32_t base = src == FROM_HLE ? 0x200000u : 0x100000u + 8u * (rnd() & 0xfff);
	if (src == FROM_HLE)
	{
		memcpy(hle, stream, n * 8 > sizeof(hle) ? sizeof(hle) : n * 8);
		rdp_intake_set_hle_buffer(&in, hle, base, sizeof(hle));
		/* (RDRAM at those addresses holds something else) */
		memset(rdram + base, 0xff, sizeof(hle));
	}
	else if (src == FROM_RDRAM)
		memcpy(rdram + base, stream, n * 8);

	while (done < n)
	{
		unsigned take = 1 + rnd() % 40;
		uint32_t current, end;
		if (take > n - done)
			take = n - done;
		if (src == FROM_DMEM)
		{
			/* each list where the RSP put it in DMEM: anywhere, wrapping */
			uint32_t at = 8u * (rnd() & 0x1ff), i;
			for (i = 0; i < take; i++)
				memcpy(dmem + ((at + 8 * i) & 0xff8), stream + 2 * (done + i), 8);
			current = at;
			end = at + 8 * take;
			CHECK(rdp_intake_process(&in, current, end, 0x01, rdram, dmem), "an XBUS list is read");
		}
		else
		{
			current = base + 8 * done;
			end = current + 8 * take;
			CHECK(rdp_intake_process(&in, current, end, 0, rdram, dmem), "a list is read");
		}
		done += take;
	}
	rdp_intake_set_hle_buffer(&in, NULL, 0, 0);
}

static void test_streams(void)
{
	static const char *names[] = { "from RDRAM", "from DMEM", "from the HLE buffer" };
	unsigned round, src;
	for (src = 0; src < 3; src++)
		for (round = 0; round < 60; round++)
		{
			unsigned n = make_stream(src == FROM_HLE ? 0x10000 / 8 : 1 + rnd() % 8000);
			bool same;
			reset();
			feed((enum source)src, n);
			same = num_out == num_expect && !memcmp(out, expect, num_expect * 4) && num_cmds == num_expect_cmds
					&& num_syncs == num_expect_syncs
					&& !memcmp(syncs_at_cmd, expect_syncs_at, num_expect_syncs * sizeof(unsigned))
					&& in.ptr == 0 && in.cur == 0;
			CHECK(same, names[src]);
			if (!same)
				return;
		}
}

static void test_in_place(void)
{
	uint32_t w[2 * 4];
	reset();
	/* set fill color, then the first half of a 4-word triangle */
	w[0] = 0x37000000u; w[1] = 0x11223344u;
	w[2] = 0x08000000u; w[3] = 1; w[4] = 2; w[5] = 3; w[6] = 4; w[7] = 5;
	memcpy(rdram + 0x300000, w, sizeof(w));
	CHECK(rdp_intake_process(&in, 0x300000, 0x300000 + 24, 0, rdram, dmem), "read");
	CHECK(num_cmds == 1 && last_ptr == (const uint32_t *)(const void *)(rdram + 0x300000),
			"a list with nothing kept is handed on where it lies");
	CHECK(in.ptr == 2 && in.cur == 0 && in.data[0] == 0x08000000u && in.data[3] == 3,
			"the command it cuts short is kept");
	w[0] = 6; w[1] = 7; w[2] = 8; w[3] = 9;
	memcpy(rdram + 0x300018, w, 16);
	CHECK(rdp_intake_process(&in, 0x300018, 0x300028, 0, rdram, dmem), "read");
	CHECK(num_cmds == 2 && num_out == 2 + 8 && out[2] == 0x08000000u && out[9] == 9 && in.ptr == 0,
			"and runs whole when the rest comes");
}

static void test_bounds(void)
{
	uint32_t w[2];
	reset();
	CHECK(!rdp_intake_process(&in, 0x1000, 0x1000, 0, rdram, dmem), "an empty list is not read");
	CHECK(!rdp_intake_process(&in, 0x1008, 0x1000, 0, rdram, dmem), "nor one that ends before it starts");
	CHECK(!rdp_intake_process(&in, 0, 0x40000, 0, rdram, dmem), "nor one too long to keep");
	CHECK(rdp_intake_process(&in, 0, 0x40000 - 8, 0, rdram, dmem) && in.ptr == 0, "one word short of it is");
	CHECK(!rdp_intake_process(&in, 0x1007, 0x1004, 0, rdram, dmem), "the registers' low bits are not the list's");

	/* an HLE list past its buffer: read no further than the buffer */
	reset();
	memset(hle, 0, sizeof(hle));
	w[0] = 0x37000000u;
	w[1] = 0xabcdef01u;
	memcpy(hle + sizeof(hle) - 8, w, 8);
	rdp_intake_set_hle_buffer(&in, hle, 0x400000, sizeof(hle));
	memset(rdram + 0x400000 + sizeof(hle), 0x37, 64);
	CHECK(rdp_intake_process(&in, 0x400000 + sizeof(hle) - 8, 0x400000 + sizeof(hle) + 64, 0, rdram, dmem),
			"an HLE list running past its buffer is read");
	CHECK(num_cmds == 1 && out[1] == 0xabcdef01u, "as far as its buffer goes");
	rdp_intake_set_hle_buffer(&in, NULL, 0, 0);

	/* outside the HLE buffer, RDRAM */
	reset();
	rdp_intake_set_hle_buffer(&in, hle, 0x400000, 0x100);
	w[1] = 0x55667788u;
	memcpy(rdram + 0x400100, w, 8);
	CHECK(rdp_intake_process(&in, 0x400100, 0x400108, 0, rdram, dmem) && num_cmds == 1 && out[1] == 0x55667788u,
			"a list past the HLE buffer's start is read from RDRAM");
	rdp_intake_set_hle_buffer(&in, NULL, 0, 0);
}

static void put(uint32_t addr, uint32_t w0, uint32_t w1)
{
	memcpy(rdram + addr, &w0, 4);
	memcpy(rdram + addr + 4, &w1, 4);
}

static void test_state(void)
{
	/* the order replay gives */
	static const unsigned order[] = {
		0x3f, 0x3e, 0x3d, 0x2f, 0x3c, 0x2d, 0x2c, 0x2a, 0x2b, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x2e
	};
	unsigned i, at = 0x500000;

	reset();
	/* every state command, twice (the last kept), in reverse order */
	for (i = 0; i < sizeof(order) / sizeof(order[0]); i++)
	{
		put(at, order[14 - i] << 24, 1);
		put(at + 8, order[14 - i] << 24, 0x100 + order[14 - i]);
		at += 16;
	}
	/* tiles 3 and 6, their sizes; a load tile, a load block, a TLUT, a fill
	 * rectangle, the syncs */
	put(at, 0x35000000u, (3u << 24) | 0x35003u); at += 8;
	put(at, 0x35000000u, (6u << 24) | 0x35006u); at += 8;
	put(at, 0x32000000u, (3u << 24) | 0x32003u); at += 8;
	put(at, 0x34000000u, 0); at += 8;
	put(at, 0x33000000u, 0); at += 8;
	put(at, 0x30000000u, 0); at += 8;
	put(at, 0x36000000u, 0); at += 8;
	put(at, 0x26000000u, 0); at += 8;
	put(at, 0x27000000u, 0); at += 8;
	put(at, 0x28000000u, 0); at += 8;
	CHECK(rdp_intake_process(&in, 0x500000, at, 0, rdram, dmem), "read");

	num_out = num_cmds = 0;
	rdp_intake_replay_state(&in);
	CHECK(num_cmds == 15 + 3, "replayed: each state command once, and the tiles");
	for (i = 0; i < 15 && num_cmds == 18; i++)
		CHECK(out[2 * i] == order[i] << 24 && out[2 * i + 1] == 0x100 + order[i],
				"the last of each, images first, the primitive depth last");
	CHECK(num_cmds == 18 && out[30] == 0x35000000u && out[31] == ((3u << 24) | 0x35003u)
			&& out[33] == ((6u << 24) | 0x35006u) && out[34] == 0x32000000u, "set tile by tile, then the tile sizes");

	rdp_intake_forget_state(&in);
	num_out = num_cmds = 0;
	rdp_intake_replay_state(&in);
	CHECK(num_cmds == 0, "forgotten when asked");
}

int main(void)
{
	rdram = (uint8_t *)calloc(1, RDRAM_BYTES);
	if (!rdram)
		return 1;
	test_streams();
	test_in_place();
	test_bounds();
	test_state();
	free(rdram);

	if (failures)
	{
		fprintf(stderr, "test_rdp_intake: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_rdp_intake: ok\n");
	return 0;
}
