/* parallel-rdp's frame waits (vulkan/frame_sync.c), through a stand-in
 * device table.
 *
 * Before a frame context is used again its work has to be done. With
 * timeline semaphores that is one wait for the last value the frame
 * submitted to each queue, skipping queues it submitted nothing to, and
 * no wait at all when it submitted nothing. Without them it is one wait
 * for all the fences the frame's submissions signalled, which are then
 * forgotten. The fence list is fixed in size and says when it is full.
 * An idle device forgets the fences unwaited; a device whose timeline
 * semaphores are gone waits for none of them. */
#include <stdio.h>
#include <string.h>
#include <boolean.h>

#include "frame_sync.h"
#include "frame_garbage.h" /* GRANITE_VK_FROM_BITS */

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_vk_frame_sync: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

#define DEV ((VkDevice)(size_t)1)
#define SEM(n) GRANITE_VK_FROM_BITS(VkSemaphore, 0x300 + (n))
#define FENCE(n) GRANITE_VK_FROM_BITS(VkFence, 0x400 + (n))

static unsigned semaphore_waits, fence_waits;
static uint32_t waited_count;
static VkSemaphore waited_semaphores[8];
static uint64_t waited_values[8];
static VkSemaphoreWaitFlags waited_flags;
static uint64_t waited_timeout;
static uint32_t fences_count;
static VkFence fences_waited[16];
static VkBool32 fences_all;
static uint64_t fences_timeout;

static VkResult VKAPI_CALL fake_wait_semaphores(VkDevice d, const VkSemaphoreWaitInfo *info, uint64_t timeout)
{
	uint32_t i;
	CHECK(d == DEV && info->sType == VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO && !info->pNext, "a well-formed wait");
	semaphore_waits++;
	waited_count = info->semaphoreCount;
	waited_flags = info->flags;
	waited_timeout = timeout;
	for (i = 0; i < info->semaphoreCount && i < 8; i++)
	{
		waited_semaphores[i] = info->pSemaphores[i];
		waited_values[i] = info->pValues[i];
	}
	return VK_SUCCESS;
}

static VkResult VKAPI_CALL fake_wait_fences(VkDevice d, uint32_t count, const VkFence *fences, VkBool32 all,
		uint64_t timeout)
{
	uint32_t i;
	CHECK(d == DEV, "on the device given");
	fence_waits++;
	fences_count = count;
	fences_all = all;
	fences_timeout = timeout;
	for (i = 0; i < count && i < 16; i++)
		fences_waited[i] = fences[i];
	return VK_SUCCESS;
}

int main(void)
{
	struct VolkDeviceTable table;
	granite_frame_sync_t sync;
	VkSemaphore timelines[GRANITE_FRAME_SYNC_QUEUES];
	unsigned i;

	memset(&table, 0, sizeof(table));
	table.vkWaitSemaphoresKHR = fake_wait_semaphores;
	table.vkWaitForFences = fake_wait_fences;

	for (i = 0; i < GRANITE_FRAME_SYNC_QUEUES; i++)
		timelines[i] = SEM(i);
	memset(&sync, 0xff, sizeof(sync));
	granite_frame_sync_init(&sync, timelines);
	CHECK(sync.num_fences == 0 && sync.timeline_semaphores[0] == SEM(0)
			&& sync.timeline_semaphores[GRANITE_FRAME_SYNC_QUEUES - 1] == SEM(GRANITE_FRAME_SYNC_QUEUES - 1)
			&& sync.timeline_values[0] == 0 && sync.timeline_values[GRANITE_FRAME_SYNC_QUEUES - 1] == 0,
			"a new frame has its queues' semaphores and nothing to wait for");

	/* ---- timeline semaphores ---- */
	granite_frame_sync_wait(&sync, DEV, &table, true);
	CHECK(semaphore_waits == 0 && fence_waits == 0, "a frame that submitted nothing does not wait");

	sync.timeline_values[0] = 12;
	sync.timeline_values[2] = 7;
	granite_frame_sync_wait(&sync, DEV, &table, true);
	CHECK(semaphore_waits == 1 && waited_count == 2 && waited_semaphores[0] == SEM(0) && waited_values[0] == 12
			&& waited_semaphores[1] == SEM(2) && waited_values[1] == 7,
			"one wait for the queues it submitted to, each for its last value");
	CHECK(waited_flags == 0 && waited_timeout == UINT64_MAX, "for all of them, however long it takes");
	CHECK(fence_waits == 0, "and no fences");

	/* a device without them, or a queue without one */
	granite_frame_sync_wait(&sync, DEV, &table, false);
	CHECK(semaphore_waits == 1, "without timeline semaphores none is waited for");
	sync.timeline_semaphores[3] = VK_NULL_HANDLE;
	granite_frame_sync_wait(&sync, DEV, &table, true);
	CHECK(semaphore_waits == 1, "nor when a queue has none");
	sync.timeline_semaphores[3] = SEM(3);

	/* ---- fences ---- */
	CHECK(granite_frame_sync_add_fence(&sync, FENCE(1)) && granite_frame_sync_add_fence(&sync, FENCE(2))
			&& sync.num_fences == 2, "fences are added");
	granite_frame_sync_wait(&sync, DEV, &table, false);
	CHECK(fence_waits == 1 && fences_count == 2 && fences_waited[0] == FENCE(1) && fences_waited[1] == FENCE(2)
			&& fences_all == VK_TRUE && fences_timeout == UINT64_MAX, "one wait for all of them");
	CHECK(sync.num_fences == 0, "which are then forgotten");
	granite_frame_sync_wait(&sync, DEV, &table, false);
	CHECK(fence_waits == 1, "and not waited for again");

	/* both at once */
	granite_frame_sync_add_fence(&sync, FENCE(3));
	granite_frame_sync_wait(&sync, DEV, &table, true);
	CHECK(semaphore_waits == 2 && fence_waits == 2 && fences_count == 1 && fences_waited[0] == FENCE(3),
			"a fence is waited for with timeline semaphores too");

	/* full */
	for (i = 0; i < GRANITE_FRAME_SYNC_FENCES; i++)
		CHECK(granite_frame_sync_add_fence(&sync, FENCE(10 + i)), "the list fills");
	CHECK(!granite_frame_sync_add_fence(&sync, FENCE(99)) && sync.num_fences == GRANITE_FRAME_SYNC_FENCES,
			"a full list refuses");
	granite_frame_sync_wait(&sync, DEV, &table, false);
	CHECK(fence_waits == 3 && fences_count == GRANITE_FRAME_SYNC_FENCES && fences_waited[0] == FENCE(10)
			&& fences_waited[GRANITE_FRAME_SYNC_FENCES - 1] == FENCE(10 + GRANITE_FRAME_SYNC_FENCES - 1),
			"and what it holds is waited for");

	/* ---- an idle device ---- */
	granite_frame_sync_add_fence(&sync, FENCE(5));
	granite_frame_sync_forget_fences(&sync);
	granite_frame_sync_wait(&sync, DEV, &table, false);
	CHECK(fence_waits == 3 && sync.num_fences == 0, "fences forgotten are not waited for");
	CHECK(sync.timeline_values[0] == 12 && sync.timeline_semaphores[0] == SEM(0), "the timeline values stay");

	/* ---- the semaphores gone ---- */
	granite_frame_sync_forget_timelines(&sync);
	CHECK(sync.timeline_values[0] == 0 && sync.timeline_values[2] == 0
			&& sync.timeline_semaphores[0] == VK_NULL_HANDLE
			&& sync.timeline_semaphores[GRANITE_FRAME_SYNC_QUEUES - 1] == VK_NULL_HANDLE, "nothing of them is left");
	granite_frame_sync_wait(&sync, DEV, &table, true);
	CHECK(semaphore_waits == 2, "and nothing is waited for");

	if (failures)
	{
		fprintf(stderr, "test_vk_frame_sync: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_vk_frame_sync: ok\n");
	return 0;
}
