/* parallel-rdp's counted semaphores (vulkan/semaphore.c), with the real
 * submission composer and frame garbage and a stand-in owner.
 *
 * A semaphore object is a binary semaphore of its own or a value of a
 * queue's timeline semaphore, signalled and with one reference. Having
 * a submission wait for it puts it in the submission's waits, with its
 * stages and value, once: the object is then spent. A binary semaphore
 * waited for goes to the frame's garbage to be used again; a timeline
 * value leaves nothing behind. With the last reference a binary
 * semaphore nothing waited for goes to the owner to be destroyed; a
 * spent one and a timeline value give nothing back. */
#include <stdio.h>
#include <string.h>
#include <boolean.h>

#include "semaphore.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_vk_semaphore: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

#define SEM(n) GRANITE_VK_FROM_BITS(VkSemaphore, 0x200 + (n))

static unsigned destroys, other_calls;
static unsigned destroyed_kind;
static uint64_t destroyed_handle;
static bool destroyed_owner_thread;
static int user_token;

static void fake_destroy(void *user, unsigned kind, uint64_t handle, bool owner_thread)
{
	CHECK(user == &user_token, "the owner's pointer comes back");
	destroys++;
	destroyed_kind = kind;
	destroyed_handle = handle;
	destroyed_owner_thread = owner_thread;
}

static void fake_free_memory(void *user, const granite_allocation_t *alloc, bool owner_thread)
{
	(void)user; (void)alloc; (void)owner_thread;
	other_calls++;
}

static void fake_recycle_fence(void *user, VkFence fence, bool seen, bool owner_thread)
{
	(void)user; (void)fence; (void)seen; (void)owner_thread;
	other_calls++;
}

static size_t garbage_count(const granite_frame_garbage_t *garbage, unsigned kind)
{
	return garbage->lists[kind].count;
}

int main(void)
{
	granite_handle_owner_t owner;
	granite_lf_pool_t pool;
	granite_submit_composer_t composer;
	granite_frame_garbage_t garbage;
	granite_semaphore_t *binary, *timeline, *again;

	owner.user = &user_token;
	owner.destroy = fake_destroy;
	owner.free_memory = fake_free_memory;
	owner.recycle_fence = fake_recycle_fence;
	granite_semaphore_pool_slots_init(&pool);
	granite_submit_composer_init(&composer);
	memset(&garbage, 0, sizeof(garbage));

	/* ---- a binary semaphore, waited for ---- */
	binary = granite_semaphore_new_binary(&pool, &owner, SEM(1));
	CHECK(binary && binary->semaphore == SEM(1) && binary->value == 0 && binary->signalled && binary->owned &&
			granite_refs_only(&binary->refs) && !binary->internal_sync && !granite_semaphore_is_timeline(binary),
			"a binary semaphore object: signalled, its own, one reference");

	granite_submit_composer_begin(&composer, false);
	granite_semaphore_wait(binary, VK_PIPELINE_STAGE_TRANSFER_BIT, &composer, &garbage);
	CHECK(composer.num_waits == 1 && composer.waits[0] == SEM(1) &&
			composer.wait_stages[0] == VK_PIPELINE_STAGE_TRANSFER_BIT && composer.wait_values[0] == 0,
			"the submission waits for it at the stages given, as a binary semaphore");
	CHECK(garbage_count(&garbage, GRANITE_GARBAGE_RECYCLE_SEMAPHORE) == 1 &&
			garbage.lists[GRANITE_GARBAGE_RECYCLE_SEMAPHORE].items[0] == 0x201 &&
			garbage_count(&garbage, GRANITE_GARBAGE_SEMAPHORE) == 0, "and it goes to the frame to be used again");
	CHECK(binary->semaphore == VK_NULL_HANDLE && !binary->signalled && !binary->owned, "the object is spent");

	granite_semaphore_wait(binary, VK_PIPELINE_STAGE_TRANSFER_BIT, &composer, &garbage);
	CHECK(composer.num_waits == 1 && garbage_count(&garbage, GRANITE_GARBAGE_RECYCLE_SEMAPHORE) == 1,
			"a spent semaphore is not waited for again");

	granite_semaphore_ref(binary);
	granite_semaphore_unref(binary);
	granite_semaphore_unref(binary);
	CHECK(destroys == 0, "its last reference gives nothing back");

	/* ---- a binary semaphore nothing waited for ---- */
	again = granite_semaphore_new_binary(&pool, &owner, SEM(2));
	CHECK(again == binary && again->signalled && again->owned && again->semaphore == SEM(2),
			"the slot serves the next semaphore, which starts over");
	granite_semaphore_ref(again);
	granite_semaphore_unref(again);
	CHECK(destroys == 0, "a reference added and dropped leaves it alone");
	granite_semaphore_unref(again);
	CHECK(destroys == 1 && destroyed_kind == GRANITE_GARBAGE_SEMAPHORE && destroyed_handle == 0x202 &&
			!destroyed_owner_thread && other_calls == 0,
			"one nothing waited for goes to the owner to be destroyed, not reused: its signal may be pending");
	again = granite_semaphore_new_binary(&pool, &owner, SEM(3));
	again->internal_sync = true;
	granite_semaphore_unref(again);
	CHECK(destroys == 2 && destroyed_handle == 0x203 && destroyed_owner_thread, "from the owner's thread it says so");

	/* ---- a timeline value ---- */
	timeline = granite_semaphore_new_timeline(&pool, &owner, SEM(9), 77);
	CHECK(timeline && timeline->semaphore == SEM(9) && timeline->value == 77 && timeline->signalled &&
			!timeline->owned && granite_semaphore_is_timeline(timeline),
			"a timeline value: signalled, the semaphore not its own");
	CHECK(granite_semaphore_new_timeline(&pool, &owner, SEM(9), 0) == NULL, "there is no timeline value 0");

	granite_submit_composer_begin(&composer, false);
	granite_semaphore_wait(timeline, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, &composer, &garbage);
	CHECK(composer.num_waits == 1 && composer.waits[0] == SEM(9) &&
			composer.wait_stages[0] == VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT && composer.wait_values[0] == 77,
			"the submission waits for the value");
	CHECK(garbage_count(&garbage, GRANITE_GARBAGE_RECYCLE_SEMAPHORE) == 1 &&
			garbage_count(&garbage, GRANITE_GARBAGE_SEMAPHORE) == 0, "the queue's semaphore is not garbage");
	CHECK(timeline->semaphore == VK_NULL_HANDLE && !timeline->signalled, "the object is spent");
	granite_semaphore_wait(timeline, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, &composer, &garbage);
	CHECK(composer.num_waits == 1, "and not waited for again");
	granite_semaphore_unref(timeline);

	timeline = granite_semaphore_new_timeline(&pool, &owner, SEM(9), 78);
	timeline->internal_sync = true;
	granite_semaphore_unref(timeline);
	CHECK(destroys == 2 && other_calls == 0, "a timeline value gives nothing back, waited for or not");

	granite_frame_garbage_free(&garbage);
	granite_submit_composer_free(&composer);
	granite_lf_pool_clear(&pool);

	if (failures)
	{
		fprintf(stderr, "test_vk_semaphore: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_vk_semaphore: OK\n");
	return 0;
}
