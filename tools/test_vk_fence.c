/* parallel-rdp's counted fences (vulkan/fence.c), through a stand-in
 * device table and owner.
 *
 * A fence object is a VkFence, or a value of a timeline semaphore, with
 * one reference. Waiting on it waits for the fence, or for the value,
 * once, however often it is asked; a wait that fails says so and can be
 * tried again. With the last reference a VkFence goes back to the
 * owner, with whether a wait saw it signalled and whether this is the
 * owner's thread; a timeline value has nothing to give back. */
#include <stdio.h>
#include <string.h>
#include <boolean.h>

#include "fence.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_vk_fence: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

#define DEV ((VkDevice)(size_t)1)
#define FENCE(n) GRANITE_VK_FROM_BITS(VkFence, 0x100 + (n))
#define SEM(n) GRANITE_VK_FROM_BITS(VkSemaphore, 0x200 + (n))

static unsigned fence_waits, timeline_waits;
static VkFence waited_fence;
static VkSemaphore waited_semaphore;
static uint64_t waited_value, waited_timeout;
static VkBool32 waited_all;
static bool fail_wait;

static VkResult VKAPI_CALL fake_wait_fences(VkDevice d, uint32_t count, const VkFence *fences, VkBool32 all,
		uint64_t timeout)
{
	CHECK(d == DEV && count == 1, "one fence, on the device given");
	fence_waits++;
	waited_fence = fences[0];
	waited_all = all;
	waited_timeout = timeout;
	return fail_wait ? VK_ERROR_DEVICE_LOST : VK_SUCCESS;
}

static VkResult VKAPI_CALL fake_wait_semaphores(VkDevice d, const VkSemaphoreWaitInfoKHR *info, uint64_t timeout)
{
	CHECK(d == DEV && info->sType == VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO_KHR && info->semaphoreCount == 1 &&
			info->flags == 0 && info->pNext == NULL, "one semaphore, on the device given");
	timeline_waits++;
	waited_semaphore = info->pSemaphores[0];
	waited_value = info->pValues[0];
	waited_timeout = timeout;
	return fail_wait ? VK_ERROR_DEVICE_LOST : VK_SUCCESS;
}

static unsigned recycles, other_calls;
static VkFence recycled;
static bool recycled_seen, recycled_owner_thread;
static int user_token;

static void fake_recycle(void *user, VkFence fence, bool seen, bool owner_thread)
{
	CHECK(user == &user_token, "the owner's pointer comes back");
	recycles++;
	recycled = fence;
	recycled_seen = seen;
	recycled_owner_thread = owner_thread;
}

static void fake_destroy(void *user, unsigned kind, uint64_t handle, bool owner_thread)
{
	(void)user; (void)kind; (void)handle; (void)owner_thread;
	other_calls++;
}

static void fake_free_memory(void *user, const granite_allocation_t *alloc, bool owner_thread)
{
	(void)user; (void)alloc; (void)owner_thread;
	other_calls++;
}

int main(void)
{
	struct VolkDeviceTable table;
	granite_handle_owner_t owner;
	granite_fence_env_t env;
	granite_lf_pool_t pool;
	granite_fence_t *fence, *timeline, *again;

	memset(&table, 0, sizeof(table));
	table.vkWaitForFences = fake_wait_fences;
	table.vkWaitSemaphoresKHR = fake_wait_semaphores;
	owner.user = &user_token;
	owner.destroy = fake_destroy;
	owner.free_memory = fake_free_memory;
	owner.recycle_fence = fake_recycle;
	env.device = DEV;
	env.table = &table;
	env.wait_ec = NULL;
	granite_fence_pool_slots_init(&pool);

	/* ---- a VkFence ---- */
	fence = granite_fence_new(&pool, &owner, &env, FENCE(1), SEM(9), 0);
	CHECK(fence && fence->fence == FENCE(1) && fence->value == 0 && fence->timeline == VK_NULL_HANDLE &&
			granite_refs_only(&fence->refs) && !fence->internal_sync, "a fence object for a VkFence, with one reference");
	CHECK(!granite_fence_seen(&fence->wait_state), "not waited on yet");

	CHECK(granite_fence_wait_for(fence) && fence_waits == 1 && timeline_waits == 0 && waited_fence == FENCE(1) &&
			waited_all == VK_TRUE && waited_timeout == UINT64_MAX, "waiting waits for the fence, without bound");
	CHECK(granite_fence_seen(&fence->wait_state), "and it has been seen signalled");
	CHECK(granite_fence_wait_for(fence) && fence_waits == 1, "a second wait has nothing to wait for");

	granite_fence_ref(fence);
	granite_fence_unref(fence);
	CHECK(recycles == 0, "a reference added and dropped leaves it alone");
	granite_fence_unref(fence);
	CHECK(recycles == 1 && recycled == FENCE(1) && recycled_seen && !recycled_owner_thread && other_calls == 0,
			"the last reference gives the fence back, as seen, from any thread");

	/* never waited on, from the owner's thread */
	again = granite_fence_new(&pool, &owner, &env, FENCE(2), VK_NULL_HANDLE, 0);
	CHECK(again == fence && !granite_fence_seen(&again->wait_state) && granite_refs_only(&again->refs) &&
			!again->internal_sync, "the slot serves the next fence, which starts over");
	again->internal_sync = true;
	granite_fence_unref(again);
	CHECK(recycles == 2 && recycled == FENCE(2) && !recycled_seen && recycled_owner_thread,
			"one never waited on goes back as not seen, from the owner's thread");

	/* a wait that fails */
	fence = granite_fence_new(&pool, &owner, &env, FENCE(3), VK_NULL_HANDLE, 0);
	fail_wait = true;
	fence_waits = 0;
	CHECK(!granite_fence_wait_for(fence) && fence_waits == 1 && !granite_fence_seen(&fence->wait_state),
			"a wait that fails says so, and the fence is not seen");
	fail_wait = false;
	CHECK(granite_fence_wait_for(fence) && fence_waits == 2 && granite_fence_seen(&fence->wait_state),
			"and can be tried again");
	granite_fence_unref(fence);
	CHECK(recycles == 3 && recycled_seen, "then it goes back as seen");

	/* ---- a timeline value ---- */
	fence_waits = 0;
	timeline = granite_fence_new(&pool, &owner, &env, FENCE(7), SEM(1), 42);
	CHECK(timeline && timeline->fence == VK_NULL_HANDLE && timeline->timeline == SEM(1) && timeline->value == 42,
			"a fence object for a timeline value has no VkFence");
	CHECK(granite_fence_wait_for(timeline) && timeline_waits == 1 && fence_waits == 0 && waited_semaphore == SEM(1) &&
			waited_value == 42 && waited_timeout == UINT64_MAX, "waiting waits for the value, without bound");
	CHECK(granite_fence_wait_for(timeline) && timeline_waits == 1, "once");
	fail_wait = true;
	again = granite_fence_new(&pool, &owner, &env, VK_NULL_HANDLE, SEM(2), 5);
	CHECK(!granite_fence_wait_for(again) && timeline_waits == 2 && waited_semaphore == SEM(2) && waited_value == 5,
			"a failed wait for a value says so");
	fail_wait = false;
	granite_fence_unref(again);
	granite_fence_unref(timeline);
	CHECK(recycles == 3 && other_calls == 0, "a timeline value has nothing to give back");

	granite_lf_pool_clear(&pool);

	if (failures)
	{
		fprintf(stderr, "test_vk_fence: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_vk_fence: OK\n");
	return 0;
}
