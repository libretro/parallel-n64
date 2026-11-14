/* parallel-rdp's ring of per-flush stream buffers
 * (parallel-rdp/rdp_sync_ring.c), with fences from a stand-in device.
 *
 * A slot read by recorded work is unsubmitted until a submission takes
 * it, and then holds that submission's fence, the latest one when it
 * was read again; waiting for a slot waits for its fence once and lets
 * it go. The ring comes round after 32 slots, and lets every fence go
 * when freed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <boolean.h>

#include "rdp_sync_ring.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_rdp_sync_ring: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

#include "vk_test_device.h"

/* A submission's fence */
static granite_fence_t *submit(void)
{
	granite_fence_t *fence = NULL;
	granite_command_buffer_t *cb = granite_device_request_command_buffer(&dev, GRANITE_QUEUE_COMPUTE);
	granite_device_submit(&dev, cb, &fence, 0, NULL);
	return fence;
}

static int refs(granite_fence_t *fence)
{
	return retro_atomic_load_acquire_int(&fence->refs);
}

int main(void)
{
	rdp_sync_ring_t ring;
	granite_fence_t *a, *b;
	unsigned i, waits;

	fill_context();
	granite_device_init(&dev);
	granite_device_set_context(&dev, &context);

	rdp_sync_ring_init(&ring);
	CHECK(ring.current == 0 && !rdp_sync_ring_unsubmitted(&ring, 0), "slot 0, nothing pending");
	waits = num_semaphore_waits;
	rdp_sync_ring_wait(&ring, 0);
	CHECK(num_semaphore_waits == waits, "nothing to wait for");

	rdp_sync_ring_use(&ring);
	CHECK(rdp_sync_ring_unsubmitted(&ring, 0) && !rdp_sync_ring_unsubmitted(&ring, 1), "slot 0 read");
	a = submit();
	CHECK(a != NULL, "a fence");
	rdp_sync_ring_submitted(&ring, a);
	CHECK(!rdp_sync_ring_unsubmitted(&ring, 0) && ring.fences[0] == a && refs(a) == 2, "slot 0 holds it");

	/* slot 0 read again, and slot 1: both on the next submission */
	rdp_sync_ring_use(&ring);
	CHECK(rdp_sync_ring_advance(&ring) == 1, "slot 1");
	rdp_sync_ring_use(&ring);
	b = submit();
	rdp_sync_ring_submitted(&ring, b);
	CHECK(ring.fences[0] == b && ring.fences[1] == b && refs(b) == 3 && refs(a) == 1, "the latest submission");
	CHECK(ring.unsubmitted == 0, "nothing unsubmitted");

	waits = num_semaphore_waits;
	rdp_sync_ring_wait(&ring, 0);
	CHECK(num_semaphore_waits == waits + 1 && ring.fences[0] == NULL && refs(b) == 2, "waited once, let go");
	rdp_sync_ring_wait(&ring, 0);
	CHECK(num_semaphore_waits == waits + 1, "and not again");

	/* a submission without a fence leaves none to wait for */
	rdp_sync_ring_use(&ring);
	rdp_sync_ring_submitted(&ring, NULL);
	CHECK(ring.fences[1] == NULL && refs(b) == 1, "no fence");

	for (i = 2; i < RDP_SYNC_RING_SIZE; i++)
		rdp_sync_ring_advance(&ring);
	CHECK(rdp_sync_ring_advance(&ring) == 0, "round after 32");

	rdp_sync_ring_use(&ring);
	rdp_sync_ring_submitted(&ring, a);
	CHECK(refs(a) == 2, "slot 0 holds the first again");
	rdp_sync_ring_free(&ring);
	CHECK(refs(a) == 1 && ring.fences[0] == NULL, "freed");

	granite_fence_unref(a);
	granite_fence_unref(b);
	granite_device_free(&dev);
	CHECK(unmade == made, "everything made went back");

	if (failures)
	{
		fprintf(stderr, "test_rdp_sync_ring: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_rdp_sync_ring: all checks passed\n");
	return 0;
}
