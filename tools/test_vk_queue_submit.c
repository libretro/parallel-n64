/* parallel-rdp's queue waits and staging plan (vulkan/queue_submit.c),
 * with the real semaphores, submission composer and frame garbage.
 *
 * A queue keeps the semaphores its next submission waits for in a list
 * of fixed size, each with a reference: adding one allocates nothing,
 * and a full list says so and stays as it is. Collecting puts them all
 * in the submission, binary ones before timeline values, each at its
 * own stages, drops the references and leaves the list empty. Clearing
 * an idle device's list destroys the binary semaphores nothing will now
 * wait for, once, and leaves timeline values alone.
 *
 * A buffer filled on one queue reaches the others by a barrier where
 * the queue is the one that uses it, and by a semaphore for each other
 * queue that can: the compute queue only for what compute can do. */
#include <stdio.h>
#include <string.h>
#include <boolean.h>

#include "queue_submit.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_vk_queue_submit: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

#define SEM(n) GRANITE_VK_FROM_BITS(VkSemaphore, 0x200 + (n))
#define Q(n) GRANITE_VK_FROM_BITS(VkQueue, n)
#define DEV ((VkDevice)(size_t)1)

static unsigned owner_destroys, vk_destroys;
static VkSemaphore vk_destroyed[8];

static void fake_destroy(void *user, unsigned kind, uint64_t handle, bool owner_thread)
{
	(void)user; (void)kind; (void)handle; (void)owner_thread;
	owner_destroys++;
}

static void fake_free_memory(void *user, const granite_allocation_t *alloc, bool owner_thread)
{
	(void)user; (void)alloc; (void)owner_thread;
}

static void fake_recycle_fence(void *user, VkFence fence, bool seen, bool owner_thread)
{
	(void)user; (void)fence; (void)seen; (void)owner_thread;
}

static void VKAPI_CALL fake_vk_destroy_semaphore(VkDevice d, VkSemaphore semaphore, const VkAllocationCallbacks *a)
{
	(void)a;
	CHECK(d == DEV, "on the device given");
	if (vk_destroys < 8)
		vk_destroyed[vk_destroys] = semaphore;
	vk_destroys++;
}

static granite_handle_owner_t owner;
static granite_lf_pool_t pool;

static void test_waits(void)
{
	static granite_queue_waits_t waits;
	granite_submit_composer_t composer;
	granite_frame_garbage_t garbage;
	granite_semaphore_t *b1, *t1, *b2, *t2, *extra;
	granite_semaphore_t *many[GRANITE_QUEUE_MAX_WAITS];
	unsigned i;

	granite_submit_composer_init(&composer);
	memset(&garbage, 0, sizeof(garbage));

	/* ---- collected: binary ones first, each at its stages ---- */
	t1 = granite_semaphore_new_timeline(&pool, &owner, SEM(90), 5);
	b1 = granite_semaphore_new_binary(&pool, &owner, SEM(1));
	t2 = granite_semaphore_new_timeline(&pool, &owner, SEM(91), 7);
	b2 = granite_semaphore_new_binary(&pool, &owner, SEM(2));
	CHECK(t1 && b1 && t2 && b2, "four semaphores");
	CHECK(granite_queue_waits_add(&waits, t1, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
			&& granite_queue_waits_add(&waits, b1, VK_PIPELINE_STAGE_TRANSFER_BIT)
			&& granite_queue_waits_add(&waits, t2, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
			&& granite_queue_waits_add(&waits, b2, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT) && waits.count == 4,
			"they are added");
	CHECK(!granite_refs_only(&t1->refs) && !granite_refs_only(&b2->refs), "each with a reference of the list's");
	/* the callers let go: the list keeps them alive */
	granite_semaphore_unref(t1);
	granite_semaphore_unref(b1);
	granite_semaphore_unref(t2);
	granite_semaphore_unref(b2);
	CHECK(owner_destroys == 0 && granite_refs_only(&b1->refs) && b1->semaphore == SEM(1), "which keeps them alive");

	granite_submit_composer_begin(&composer, false);
	granite_queue_waits_collect(&waits, &composer, &garbage);
	CHECK(composer.num_waits == 4, "all four are waited for");
	CHECK(composer.waits[0] == SEM(1) && composer.wait_stages[0] == VK_PIPELINE_STAGE_TRANSFER_BIT
			&& composer.wait_values[0] == 0
			&& composer.waits[1] == SEM(2) && composer.wait_stages[1] == VK_PIPELINE_STAGE_VERTEX_SHADER_BIT
			&& composer.wait_values[1] == 0, "the binary ones first, in order, at their own stages");
	CHECK(composer.waits[2] == SEM(90) && composer.wait_stages[2] == VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
			&& composer.wait_values[2] == 5
			&& composer.waits[3] == SEM(91) && composer.wait_stages[3] == VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
			&& composer.wait_values[3] == 7, "then the timeline values");
	CHECK(waits.count == 0, "and the list is empty");
	CHECK(garbage.lists[GRANITE_GARBAGE_RECYCLE_SEMAPHORE].count == 2 && owner_destroys == 0,
			"the binary ones go to the frame to be used again; nothing is destroyed");
	/* the slots are free again: the references were dropped */
	extra = granite_semaphore_new_binary(&pool, &owner, SEM(3));
	CHECK(extra == b2 || extra == b1 || extra == t1 || extra == t2, "the list's references are gone");
	granite_semaphore_unref(extra);
	CHECK(owner_destroys == 1, "(a semaphore nothing waited for goes to its owner)");

	granite_submit_composer_begin(&composer, false);
	granite_queue_waits_collect(&waits, &composer, &garbage);
	CHECK(composer.num_waits == 0, "an empty list adds no waits");

	/* ---- full ---- */
	for (i = 0; i < GRANITE_QUEUE_MAX_WAITS; i++)
	{
		many[i] = granite_semaphore_new_timeline(&pool, &owner, SEM(90), 100 + i);
		CHECK(many[i] && granite_queue_waits_add(&waits, many[i], VK_PIPELINE_STAGE_TRANSFER_BIT), "the list fills");
		granite_semaphore_unref(many[i]);
	}
	extra = granite_semaphore_new_timeline(&pool, &owner, SEM(90), 999);
	CHECK(!granite_queue_waits_add(&waits, extra, VK_PIPELINE_STAGE_TRANSFER_BIT)
			&& waits.count == GRANITE_QUEUE_MAX_WAITS && granite_refs_only(&extra->refs),
			"a full list refuses, and takes no reference");
	granite_submit_composer_begin(&composer, false);
	granite_queue_waits_collect(&waits, &composer, &garbage);
	CHECK(composer.num_waits == GRANITE_QUEUE_MAX_WAITS && composer.wait_values[0] == 100
			&& composer.wait_values[GRANITE_QUEUE_MAX_WAITS - 1] == 100 + GRANITE_QUEUE_MAX_WAITS - 1,
			"collected, all of them are waited for");
	CHECK(granite_queue_waits_add(&waits, extra, VK_PIPELINE_STAGE_TRANSFER_BIT) && waits.count == 1,
			"and there is room again");
	granite_semaphore_unref(extra);
	granite_submit_composer_begin(&composer, false);
	granite_queue_waits_collect(&waits, &composer, &garbage);

	granite_submit_composer_free(&composer);
	granite_frame_garbage_free(&garbage);
}

static void test_clear(void)
{
	static granite_queue_waits_t waits;
	struct VolkDeviceTable table;
	granite_semaphore_t *binary, *timeline, *kept;
	unsigned before = owner_destroys;

	memset(&table, 0, sizeof(table));
	table.vkDestroySemaphore = fake_vk_destroy_semaphore;

	binary = granite_semaphore_new_binary(&pool, &owner, SEM(11));
	timeline = granite_semaphore_new_timeline(&pool, &owner, SEM(90), 3);
	kept = granite_semaphore_new_binary(&pool, &owner, SEM(12));
	CHECK(granite_queue_waits_add(&waits, binary, VK_PIPELINE_STAGE_TRANSFER_BIT)
			&& granite_queue_waits_add(&waits, timeline, VK_PIPELINE_STAGE_TRANSFER_BIT)
			&& granite_queue_waits_add(&waits, kept, VK_PIPELINE_STAGE_TRANSFER_BIT), "three wait");
	granite_semaphore_unref(binary);
	granite_semaphore_unref(timeline);
	/* (someone else still holds this one) */

	granite_queue_waits_clear(&waits, DEV, &table);
	CHECK(vk_destroys == 2 && vk_destroyed[0] == SEM(11) && vk_destroyed[1] == SEM(12),
			"the binary semaphores are destroyed at once, the queue's timeline one is not");
	CHECK(waits.count == 0, "and the list is empty");
	CHECK(kept->semaphore == VK_NULL_HANDLE && !kept->owned && !kept->signalled && granite_refs_only(&kept->refs),
			"one still held elsewhere is spent");
	granite_semaphore_unref(kept);
	CHECK(owner_destroys == before, "none of them is destroyed a second time through its owner");

	granite_queue_waits_clear(&waits, DEV, &table);
	CHECK(vk_destroys == 2, "clearing an empty list does nothing");
}

#define ALL_STAGES (VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT \
		| VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT)
#define COMPUTE_STAGES (VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT \
		| VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT)
#define ALL_ACCESS (VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT \
		| VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT \
		| VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT)
#define COMPUTE_ACCESS (VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT \
		| VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT)

static void test_staging(void)
{
	granite_staging_plan_t p;

	/* one queue */
	memset(&p, 0xff, sizeof(p));
	granite_staging_plan(Q(1), Q(1), Q(1), ALL_STAGES, ALL_ACCESS, &p);
	CHECK(p.barrier_stages == ALL_STAGES && p.barrier_access == ALL_ACCESS && !p.wait_graphics && !p.wait_compute,
			"one queue: a barrier to everything, and nothing waits");

	/* filled on graphics, compute apart */
	granite_staging_plan(Q(1), Q(1), Q(2), ALL_STAGES, ALL_ACCESS, &p);
	CHECK(p.barrier_stages == ALL_STAGES && p.barrier_access == ALL_ACCESS && !p.wait_graphics && p.wait_compute
			&& p.compute_stages == COMPUTE_STAGES, "graphics fills: compute waits for what compute can do");
	granite_staging_plan(Q(1), Q(1), Q(2), VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT, &p);
	CHECK(p.barrier_stages == VK_PIPELINE_STAGE_VERTEX_INPUT_BIT && !p.wait_graphics && !p.wait_compute,
			"a vertex buffer is nothing to the compute queue");

	/* filled on compute */
	granite_staging_plan(Q(2), Q(1), Q(2), ALL_STAGES, ALL_ACCESS, &p);
	CHECK(p.barrier_stages == COMPUTE_STAGES && p.barrier_access == COMPUTE_ACCESS && p.wait_graphics && !p.wait_compute
			&& p.graphics_stages == ALL_STAGES, "compute fills: its barrier is its own, and graphics waits");

	/* filled on a transfer queue of its own */
	granite_staging_plan(Q(3), Q(1), Q(2), ALL_STAGES, ALL_ACCESS, &p);
	CHECK(p.barrier_stages == 0 && p.barrier_access == 0 && p.wait_graphics && p.wait_compute
			&& p.graphics_stages == ALL_STAGES && p.compute_stages == COMPUTE_STAGES,
			"a transfer queue fills: both wait, with no barrier");
	granite_staging_plan(Q(3), Q(1), Q(2), VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT, &p);
	CHECK(p.wait_graphics && !p.wait_compute && p.graphics_stages == VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
			"only graphics for a vertex buffer");
	granite_staging_plan(Q(3), Q(1), Q(1), ALL_STAGES, ALL_ACCESS, &p);
	CHECK(p.barrier_stages == 0 && p.wait_graphics && p.wait_compute,
			"a transfer queue apart from one queue for the rest: both still wait");
	granite_staging_plan(Q(3), Q(1), Q(2), 0, 0, &p);
	CHECK(!p.wait_graphics && !p.wait_compute && p.barrier_stages == 0, "nothing to use it: nothing waits");
}

int main(void)
{
	owner.user = NULL;
	owner.destroy = fake_destroy;
	owner.free_memory = fake_free_memory;
	owner.recycle_fence = fake_recycle_fence;
	granite_semaphore_pool_slots_init(&pool);

	test_waits();
	test_clear();
	test_staging();

	if (failures)
	{
		fprintf(stderr, "test_vk_queue_submit: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_vk_queue_submit: ok\n");
	return 0;
}
