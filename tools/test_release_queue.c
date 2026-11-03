/* parallel-rdp's release queue (vulkan/release_queue.c): several threads
 * release Vulkan objects while the owning thread moves from frame
 * context to frame context, draining each into its garbage lists as it
 * comes round. Every release must reach a garbage list exactly once,
 * under the kind it was released as, with no lock. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <boolean.h>
#include <retro_atomic.h>
#include <rthreads/rthreads.h>

#include "release_queue.h"

#define THREADS  4
#define RELEASES 20000
#define FRAMES   3

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_release_queue: FAIL: %s\n", what); \
			failures++; \
		} \
	} while (0)

static granite_release_queue_t queue;
static granite_frame_garbage_t garbage[FRAMES];
static retro_atomic_int_t producers_done;
static unsigned char seen[THREADS][RELEASES];
static unsigned wrong_kind;

/* the thread and sequence in the handle; never 0 */
static uint64_t handle_of(unsigned thread, unsigned seq)
{
	return ((uint64_t)(thread + 1) << 32) | (uint64_t)(seq + 1);
}

static enum granite_garbage_kind kind_of(unsigned seq)
{
	return (enum granite_garbage_kind)(seq % GRANITE_GARBAGE_KINDS);
}

static void producer(void *data)
{
	unsigned thread = (unsigned)(uintptr_t)data;
	unsigned seq;
	for (seq = 0; seq < RELEASES; seq++)
		granite_release_queue_push(&queue, kind_of(seq), handle_of(thread, seq));
	retro_atomic_fetch_add_int(&producers_done, 1);
}

/* what a frame context does with its lists once collected: here, tick
 * every handle off and empty the lists */
static void account(granite_frame_garbage_t *g)
{
	unsigned kind;
	size_t i;
	for (kind = 0; kind < GRANITE_GARBAGE_KINDS; kind++)
	{
		granite_garbage_list_t *list = &g->lists[kind];
		for (i = 0; i < list->count; i++)
		{
			uint64_t h = list->items[i];
			unsigned thread = (unsigned)(h >> 32) - 1;
			unsigned seq = (unsigned)(h & 0xffffffffu) - 1;
			if (thread >= THREADS || seq >= RELEASES)
			{
				CHECK(0, "a handle that was never released");
				continue;
			}
			if (kind_of(seq) != (enum granite_garbage_kind)kind)
				wrong_kind++;
			seen[thread][seq]++;
		}
		list->count = 0;
	}
}

int main(void)
{
	sthread_t *threads[THREADS];
	unsigned i, t, s, frame = 0, missing = 0, twice = 0;

	granite_release_queue_init(&queue);
	retro_atomic_int_init(&producers_done, 0);
	for (t = 0; t < THREADS; t++)
		threads[t] = sthread_create(producer, (void *)(uintptr_t)t);

	/* the owner: next frame context, take what was released to it, then
	 * point releases at it, as Device::next_frame_context() does */
	while (retro_atomic_load_acquire_int(&producers_done) < THREADS)
	{
		frame = (frame + 1) % FRAMES;
		granite_release_queue_drain(&queue, frame, &garbage[frame]);
		account(&garbage[frame]);
		granite_release_queue_set_frame(&queue, frame);
	}
	for (t = 0; t < THREADS; t++)
		if (threads[t])
			sthread_join(threads[t]);
		else
			producer((void *)(uintptr_t)t);
	for (i = 0; i < FRAMES; i++)
	{
		granite_release_queue_drain(&queue, i, &garbage[i]);
		account(&garbage[i]);
		granite_frame_garbage_free(&garbage[i]);
	}

	for (t = 0; t < THREADS; t++)
		for (s = 0; s < RELEASES; s++)
		{
			if (!seen[t][s])
				missing++;
			else if (seen[t][s] > 1)
				twice++;
		}
	CHECK(missing == 0, "a release was lost");
	CHECK(twice == 0, "a release was drained twice");
	CHECK(wrong_kind == 0, "a release changed kind");
	granite_release_queue_free(&queue);

	if (failures)
		return 1;
	printf("test_release_queue: ok\n");
	return 0;
}
