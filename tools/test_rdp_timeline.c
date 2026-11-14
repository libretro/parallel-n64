/* parallel-rdp's timeline worker (parallel-rdp/rdp_timeline.c), with a
 * stand-in device.
 *
 * In order, each op waits for its fence, drops the submissions-in-flight
 * count, copies what the GPU wrote back to the host's RDRAM under its
 * mask (dropping each page's pending write counts) and publishes its
 * timeline value; the fence is let go after. A timeline value is reached
 * once passed, modulo wrap. The copies are the worker's own once queued:
 * the producer may reuse its storage at once. Nodes come back and are
 * used again; one too small for an op's copies is not. */
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

	/* nodes come back and are used again: the worker hands a node back
	 * just after publishing its value, so the next op may find it not
	 * back yet, but never more than one besides */
	for (i = 0; i < 10; i++)
	{
		memset(&op, 0, sizeof(op));
		op.timeline_value = 8 + i;
		rdp_timeline_enqueue(&t, &op);
		rdp_timeline_wait(&t, 8 + i);
	}
	CHECK(list_length(t.spare) + list_length((mpsc_stack_node_t *)retro_atomic_load_acquire_ptr(&t.returned.head)) <= 2,
	      "nodes used again");
	/* 20 copies: a new node, the others too small */
	memset(copies, 0, sizeof(copies));
	memset(&op, 0, sizeof(op));
	op.copies = copies;
	op.num_copies = 20;
	op.timeline_value = 100;
	rdp_timeline_enqueue(&t, &op);
	rdp_timeline_wait(&t, 100);

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
