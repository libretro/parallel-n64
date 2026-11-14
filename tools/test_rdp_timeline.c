/* parallel-rdp's timeline worker (parallel-rdp/rdp_timeline.c), with a
 * stand-in device.
 *
 * In order, each op waits for its fence, drops the submissions-in-flight
 * count, copies what the GPU wrote back to the host's RDRAM under its
 * mask (dropping each page's pending write counts) and publishes its
 * timeline value; the fence is let go after. A timeline value is reached
 * once passed, modulo wrap. The copies are the worker's own once queued:
 * the producer may reuse its storage at once. The nodes reserved at load
 * are used again, the smallest that fits first, and none is made while
 * they last; past them one is, never a wait. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <boolean.h>

#include "rdp_timeline.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_rdp_timeline: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

#include "vk_test_device.h"

static granite_fence_t *submit(void)
{
	granite_fence_t *fence = NULL;
	granite_command_buffer_t *cb = granite_device_request_command_buffer(&dev, GRANITE_QUEUE_COMPUTE);
	granite_device_submit(&dev, cb, &fence, 0, NULL);
	return fence;
}

static unsigned list_length(mpsc_stack_node_t *n)
{
	unsigned count = 0;
	for (; n; n = n->next)
		count++;
	return count;
}

/* The worker hands a node back just after publishing its value: until
 * @total are back, what came back is taken into the spare list (as an
 * enqueue would) */
static void all_back(rdp_timeline_t *t, unsigned total)
{
	unsigned tries;
	for (tries = 0; tries < 10000000; tries++)
	{
		mpsc_stack_node_t *back = mpsc_stack_drain(&t->returned), *tail;
		if (back)
		{
			for (tail = back; tail->next; tail = tail->next)
				;
			tail->next = t->spare;
			t->spare = back;
		}
		if (list_length(t->spare) == total)
			return;
	}
	CHECK(0, "the nodes came back");
}

int main(void)
{
	static uint8_t rdram[4096];
	rdp_timeline_t t;
	rdp_timeline_op_t op;
	retro_atomic_int_t in_flight, counters[4];
	rdp_coherency_copy_t copies[20];
	granite_buffer_info_t info;
	granite_buffer_t *src;
	granite_fence_t *fence;
	uint8_t *mapped;
	unsigned i, waits;

	fill_context();
	granite_device_init(&dev);
	granite_device_set_context(&dev, &context);
	CHECK(rdp_timeline_init(&t, &dev), "started");
	CHECK(rdp_timeline_reserve(&t, 2, 1, 32), "reserved");

	/* a value */
	memset(&op, 0, sizeof(op));
	op.timeline_value = 5;
	rdp_timeline_enqueue(&t, &op);
	rdp_timeline_wait(&t, 5);
	CHECK(rdp_timeline_reached(&t, 5) && rdp_timeline_reached(&t, 4) && !rdp_timeline_reached(&t, 6), "5 passed");

	/* a fence, and the submissions in flight */
	fence = submit();
	retro_atomic_int_init(&in_flight, 1);
	waits = num_semaphore_waits;
	memset(&op, 0, sizeof(op));
	granite_fence_ref(fence);
	op.fence = fence;
	op.unlock_cookie = &in_flight;
	op.timeline_value = 6;
	rdp_timeline_enqueue(&t, &op);
	rdp_timeline_wait(&t, 6);
	CHECK(retro_atomic_load_acquire_int(&in_flight) == 0, "one submission less in flight");
	CHECK(num_semaphore_waits == waits + 1, "the fence waited for");
	CHECK(retro_atomic_load_acquire_int(&fence->refs) == 1, "and let go");
	granite_fence_unref(fence);

	/* copies back to RDRAM under the mask: 64 bytes of data, then mask */
	info.domain = GRANITE_BUFFER_DOMAIN_HOST;
	info.size = 4096;
	info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	info.misc = 0;
	src = granite_device_create_buffer(&dev, &info, NULL);
	mapped = (uint8_t *)granite_device_map_host_buffer(&dev, src, GRANITE_MEMORY_ACCESS_WRITE_BIT, 0, 4096);
	for (i = 0; i < 64; i++)
	{
		mapped[i] = (uint8_t)(0x80 + i);
		mapped[2048 + i] = (i & 1) ? 0xff : 0;
	}
	granite_device_unmap_host_buffer(&dev, src, GRANITE_MEMORY_ACCESS_WRITE_BIT, 0, 4096);
	memset(rdram, 0x11, sizeof(rdram));
	for (i = 0; i < 4; i++)
		retro_atomic_int_init(&counters[i], 2);

	memset(copies, 0, sizeof(copies));
	copies[0].src_offset = 0;
	copies[0].mask_offset = 2048;
	copies[0].dst_offset = 1024;
	copies[0].size = 32;
	copies[0].counter_base = &counters[0];
	copies[0].counters = 2;
	copies[1].src_offset = 32;
	copies[1].mask_offset = 2048 + 32;
	copies[1].dst_offset = 3000 & ~15u;
	copies[1].size = 32;
	copies[1].counter_base = &counters[2];
	copies[1].counters = 1;
	memset(&op, 0, sizeof(op));
	op.dst = rdram;
	op.src = src;
	op.copies = copies;
	op.num_copies = 2;
	op.timeline_value = 7;
	rdp_timeline_enqueue(&t, &op);
	/* the producer's storage is its own again at once */
	memset(copies, 0xee, sizeof(copies));
	rdp_timeline_wait(&t, 7);
	CHECK(rdram[1024] == 0x11 && rdram[1025] == 0x81 && rdram[1055] == 0x80 + 31, "the GPU's bytes under the mask");
	CHECK(rdram[1023] == 0x11 && rdram[1056] == 0x11, "nothing outside");
	CHECK(rdram[(3000 & ~15u) + 1] == 0x80 + 33 && rdram[3000 & ~15u] == 0x11, "the second copy");
	CHECK(retro_atomic_load_acquire_int(&counters[0]) == 1 && retro_atomic_load_acquire_int(&counters[1]) == 1 &&
	      retro_atomic_load_acquire_int(&counters[2]) == 1 && retro_atomic_load_acquire_int(&counters[3]) == 2,
	      "each page's pending writes dropped");

	/* the waits on small nodes, the readback of 20 copies on the big one:
	 * none made */
	all_back(&t, 3);
	CHECK(t.grown == 0, "the reserved so far");
	for (i = 0; i < 10; i++)
	{
		memset(&op, 0, sizeof(op));
		op.timeline_value = 8 + i;
		rdp_timeline_enqueue(&t, &op);
		rdp_timeline_wait(&t, 8 + i);
		all_back(&t, 3);
	}
	/* two waits in flight at once: both small nodes, the big one left */
	memset(&op, 0, sizeof(op));
	fence = submit();
	op.fence = fence;
	op.timeline_value = 50;
	rdp_timeline_enqueue(&t, &op);
	memset(&op, 0, sizeof(op));
	op.timeline_value = 51;
	rdp_timeline_enqueue(&t, &op);
	rdp_timeline_wait(&t, 51);
	all_back(&t, 3);
	memset(copies, 0, sizeof(copies));
	memset(&op, 0, sizeof(op));
	op.copies = copies;
	op.num_copies = 20;
	op.timeline_value = 100;
	rdp_timeline_enqueue(&t, &op);
	rdp_timeline_wait(&t, 100);
	CHECK(t.grown == 0, "none made while the reserved last");
	all_back(&t, 3);
	/* With the worker held in a wait: a wait, a timeline value and a
	 * readback queued behind it take the two small nodes and the big one,
	 * the smallest that fits each. And what came back is taken again:
	 * the worker hands the three back, and three more need them. */
	retro_atomic_store_release_int(&hold_semaphore_waits, 1);
	memset(&op, 0, sizeof(op));
	op.fence = submit();
	op.timeline_value = 200;
	rdp_timeline_enqueue(&t, &op);
	memset(&op, 0, sizeof(op));
	op.timeline_value = 201;
	rdp_timeline_enqueue(&t, &op);
	memset(&op, 0, sizeof(op));
	op.copies = copies;
	op.num_copies = 20;
	op.timeline_value = 202;
	rdp_timeline_enqueue(&t, &op);
	retro_atomic_store_release_int(&hold_semaphore_waits, 0);
	rdp_timeline_wait(&t, 202);
	CHECK(t.grown == 0, "the smallest node that fits");
	while (list_length(t.spare) + list_length((mpsc_stack_node_t *)retro_atomic_load_acquire_ptr(&t.returned.head)) < 3)
		retro_sleep(1);
	CHECK(t.spare == NULL, "all three handed back, none spare");
	retro_atomic_store_release_int(&hold_semaphore_waits, 1);
	memset(&op, 0, sizeof(op));
	op.fence = submit();
	op.timeline_value = 203;
	rdp_timeline_enqueue(&t, &op);
	memset(&op, 0, sizeof(op));
	op.timeline_value = 204;
	rdp_timeline_enqueue(&t, &op);
	memset(&op, 0, sizeof(op));
	op.copies = copies;
	op.num_copies = 20;
	op.timeline_value = 205;
	rdp_timeline_enqueue(&t, &op);
	retro_atomic_store_release_int(&hold_semaphore_waits, 0);
	rdp_timeline_wait(&t, 205);
	CHECK(t.grown == 0, "what came back taken again");
	all_back(&t, 3);

	/* 40 copies: more than any reserved holds */
	memset(&op, 0, sizeof(op));
	op.copies = copies;
	op.num_copies = 20;
	op.timeline_value = 101;
	{
		static rdp_coherency_copy_t many[40];
		op.copies = many;
		op.num_copies = 40;
		rdp_timeline_enqueue(&t, &op);
	}
	rdp_timeline_wait(&t, 101);
	CHECK(t.grown == 1, "one made, past them");

	/* wrap */
	retro_atomic_store_release_size(&t.done, (size_t)-2);
	CHECK(rdp_timeline_reached(&t, (uint64_t)((size_t)-3)) && !rdp_timeline_reached(&t, 1), "modulo wrap");
	retro_atomic_store_release_size(&t.done, 1);
	CHECK(rdp_timeline_reached(&t, (uint64_t)((size_t)-3)) && rdp_timeline_reached(&t, 1), "past the wrap");

	rdp_timeline_free(&t);
	granite_buffer_unref(src);
	granite_device_free(&dev);
	CHECK(unmade == made, "everything made went back");

	if (failures)
	{
		fprintf(stderr, "test_rdp_timeline: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_rdp_timeline: all checks passed\n");
	return 0;
}
