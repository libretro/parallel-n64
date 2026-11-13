/* parallel-rdp's submissions (vulkan/submission.c), with the real frame
 * context, queue waits, composer, semaphores and fences, through a
 * stand-in device table.
 *
 * A finished command buffer waits in its frame for the queue's next
 * submission, which takes everything the frame has for the queue in
 * one vkQueueSubmit. Asking for a fence or semaphores submits at once.
 * The transfer queue always goes first. With timeline semaphores every
 * submission signals the next value of its queue's timeline, and the
 * fence and semaphores handed out are that value; without them they
 * are a VkFence and binary semaphores of their own, and ending the
 * frame leaves it a fence to wait for. A queue's next submission waits
 * for the semaphores added to it; a full list goes out in an empty
 * submission. A buffer filled on one queue is handed to the others by
 * semaphores, or by a barrier when it is the same queue. The frontend's
 * queue lock is held around every use of a queue. */
#include <stdio.h>
#include <string.h>
#include <boolean.h>

#include "submission.h"
#include "context.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_vk_submission: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

#define DEV ((VkDevice)(size_t)1)
#define Q(n) GRANITE_VK_FROM_BITS(VkQueue, 0x100 + (n))
#define G GRANITE_QUEUE_GRAPHICS
#define C GRANITE_QUEUE_COMPUTE
#define T GRANITE_QUEUE_TRANSFER

/* ---- the stand-in device ---- */
#define MAX_CALLS 64
#define MAX_ITEMS 40

typedef struct submit_call
{
	VkQueue queue;
	VkFence fence;
	unsigned num_waits, num_cmds, num_signals;
	VkSemaphore waits[MAX_ITEMS];
	VkPipelineStageFlags wait_stages[MAX_ITEMS];
	uint64_t wait_values[MAX_ITEMS];
	VkCommandBuffer cmds[MAX_ITEMS];
	VkSemaphore signals[MAX_ITEMS];
	uint64_t signal_values[MAX_ITEMS];
} submit_call_t;

static submit_call_t calls[MAX_CALLS];
static unsigned num_calls;
static bool locked;
static unsigned locks, unlocks, unlocked_uses;
static bool use_lock;

static VkResult VKAPI_CALL fake_queue_submit(VkQueue queue, uint32_t count, const VkSubmitInfo *submits, VkFence fence)
{
	submit_call_t *call;
	uint32_t i, j;

	if (use_lock && !locked)
		unlocked_uses++;
	if (num_calls == MAX_CALLS)
		return VK_ERROR_OUT_OF_HOST_MEMORY;
	call = &calls[num_calls++];
	memset(call, 0, sizeof(*call));
	call->queue = queue;
	call->fence = fence;
	for (i = 0; i < count; i++)
	{
		const VkTimelineSemaphoreSubmitInfo *timeline = (const VkTimelineSemaphoreSubmitInfo *)submits[i].pNext;
		CHECK(submits[i].sType == VK_STRUCTURE_TYPE_SUBMIT_INFO, "a well-formed submit");
		CHECK(!timeline || timeline->sType == VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO, "with timeline values");
		for (j = 0; j < submits[i].waitSemaphoreCount && call->num_waits < MAX_ITEMS; j++)
		{
			call->waits[call->num_waits] = submits[i].pWaitSemaphores[j];
			call->wait_stages[call->num_waits] = submits[i].pWaitDstStageMask[j];
			call->wait_values[call->num_waits] = timeline && j < timeline->waitSemaphoreValueCount ?
				timeline->pWaitSemaphoreValues[j] : 0;
			call->num_waits++;
		}
		for (j = 0; j < submits[i].commandBufferCount && call->num_cmds < MAX_ITEMS; j++)
			call->cmds[call->num_cmds++] = submits[i].pCommandBuffers[j];
		for (j = 0; j < submits[i].signalSemaphoreCount && call->num_signals < MAX_ITEMS; j++)
		{
			call->signals[call->num_signals] = submits[i].pSignalSemaphores[j];
			call->signal_values[call->num_signals] = timeline && j < timeline->signalSemaphoreValueCount ?
				timeline->pSignalSemaphoreValues[j] : 0;
			call->num_signals++;
		}
	}
	return VK_SUCCESS;
}

static unsigned idle_waits;
static VkResult VKAPI_CALL fake_wait_idle(VkDevice d)
{
	(void)d;
	if (use_lock && !locked)
		unlocked_uses++;
	idle_waits++;
	return VK_SUCCESS;
}

static void lock_queue(void *user)
{
	CHECK(user == (void *)&locks && !locked, "the lock is taken once, with its userdata");
	locked = true;
	locks++;
}

static void unlock_queue(void *user)
{
	CHECK(user == (void *)&locks && locked, "and given back");
	locked = false;
	unlocks++;
}

static unsigned semaphores_created, timelines_created, semaphores_destroyed;
static VkResult VKAPI_CALL fake_create_semaphore(VkDevice d, const VkSemaphoreCreateInfo *info,
		const VkAllocationCallbacks *a, VkSemaphore *semaphore)
{
	const VkSemaphoreTypeCreateInfo *type = (const VkSemaphoreTypeCreateInfo *)info->pNext;
	(void)d; (void)a;
	semaphores_created++;
	if (type)
	{
		CHECK(type->sType == VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO
				&& type->semaphoreType == VK_SEMAPHORE_TYPE_TIMELINE && type->initialValue == 0,
				"a timeline semaphore starts at 0");
		timelines_created++;
	}
	*semaphore = GRANITE_VK_FROM_BITS(VkSemaphore, (type ? 0x700 : 0x300) + semaphores_created);
	return VK_SUCCESS;
}

static bool is_timeline(VkSemaphore s)
{
	return (GRANITE_VK_TO_BITS(s) & 0xf00) == 0x700;
}

static void VKAPI_CALL fake_destroy_semaphore(VkDevice d, VkSemaphore semaphore, const VkAllocationCallbacks *a)
{
	(void)d; (void)semaphore; (void)a;
	semaphores_destroyed++;
}

static unsigned fences_created, fences_destroyed, fence_resets, fence_waits;
static VkFence fence_waited;
static VkResult VKAPI_CALL fake_create_fence(VkDevice d, const VkFenceCreateInfo *info,
		const VkAllocationCallbacks *a, VkFence *fence)
{
	(void)d; (void)info; (void)a;
	fences_created++;
	*fence = GRANITE_VK_FROM_BITS(VkFence, 0x400 + fences_created);
	return VK_SUCCESS;
}

static void VKAPI_CALL fake_destroy_fence(VkDevice d, VkFence fence, const VkAllocationCallbacks *a)
{
	(void)d; (void)fence; (void)a;
	fences_destroyed++;
}

static VkResult VKAPI_CALL fake_reset_fences(VkDevice d, uint32_t count, const VkFence *fences)
{
	(void)d; (void)fences;
	fence_resets += count;
	return VK_SUCCESS;
}

static VkResult VKAPI_CALL fake_wait_fences(VkDevice d, uint32_t count, const VkFence *fences, VkBool32 all,
		uint64_t timeout)
{
	(void)d; (void)count; (void)all; (void)timeout;
	fence_waits++;
	fence_waited = fences[0];
	return VK_SUCCESS;
}

static VkResult VKAPI_CALL fake_wait_semaphores(VkDevice d, const VkSemaphoreWaitInfo *info, uint64_t timeout)
{
	(void)d; (void)info; (void)timeout;
	return VK_SUCCESS;
}

static VkResult VKAPI_CALL fake_create_pool(VkDevice d, const VkCommandPoolCreateInfo *info,
		const VkAllocationCallbacks *a, VkCommandPool *pool)
{
	(void)d; (void)info; (void)a;
	*pool = GRANITE_VK_FROM_BITS(VkCommandPool, 1);
	return VK_SUCCESS;
}

static void VKAPI_CALL fake_destroy_pool(VkDevice d, VkCommandPool pool, const VkAllocationCallbacks *a)
{
	(void)d; (void)pool; (void)a;
}

static unsigned ended;
static VkResult VKAPI_CALL fake_end_command_buffer(VkCommandBuffer cmd)
{
	(void)cmd;
	ended++;
	return VK_SUCCESS;
}

static unsigned barriers;
static VkPipelineStageFlags barrier_src, barrier_dst;
static VkAccessFlags barrier_src_access, barrier_dst_access;
static void VKAPI_CALL fake_pipeline_barrier(VkCommandBuffer cmd, VkPipelineStageFlags src, VkPipelineStageFlags dst,
		VkDependencyFlags flags, uint32_t memory_count, const VkMemoryBarrier *memory, uint32_t buffer_count,
		const VkBufferMemoryBarrier *buffers, uint32_t image_count, const VkImageMemoryBarrier *images)
{
	(void)cmd; (void)flags; (void)buffers; (void)images;
	CHECK(memory_count == 1 && buffer_count == 0 && image_count == 0 && ended == 0,
			"one global barrier, before the command buffer ends");
	barriers++;
	barrier_src = src;
	barrier_dst = dst;
	barrier_src_access = memory[0].srcAccessMask;
	barrier_dst_access = memory[0].dstAccessMask;
}

static void VKAPI_CALL fake_memory_properties2(VkPhysicalDevice gpu, VkPhysicalDeviceMemoryProperties2 *props)
{
	(void)gpu; (void)props;
}
PFN_vkGetPhysicalDeviceMemoryProperties2 vkGetPhysicalDeviceMemoryProperties2 = fake_memory_properties2;

/* ---- what owns the counted objects ---- */
static unsigned owner_destroys, owner_fences;
static VkFence owner_fence;

static void owner_destroy(void *user, unsigned kind, uint64_t handle, bool owner_thread)
{
	(void)user; (void)kind; (void)handle; (void)owner_thread;
	owner_destroys++;
}

static void owner_free_memory(void *user, const granite_allocation_t *alloc, bool owner_thread)
{
	(void)user; (void)alloc; (void)owner_thread;
}

static void owner_recycle_fence(void *user, VkFence fence, bool seen, bool owner_thread)
{
	(void)user; (void)seen; (void)owner_thread;
	owner_fences++;
	owner_fence = fence;
}

/* ---- the fixture ---- */
static struct VolkDeviceTable table;
static granite_handle_owner_t owner;
static granite_fence_pool_t fence_pool;
static granite_semaphore_pool_t semaphore_pool;
static granite_release_queue_t releases;
static granite_memory_release_queue_t memory_releases;
static granite_lf_pool_t fence_objects, semaphore_objects, cb_objects;
static granite_frame_env_t env;
static granite_frame_context_t frame;
static granite_submission_t sub;
static granite_cmd_device_t cmd_device;
static granite_workarounds_t workarounds;
static char cb_storage[64];
static unsigned cb_next;

static const uint32_t families[GRANITE_FRAME_QUEUES] = { 0, 1, 2 };

static void setup(bool timeline, bool one_queue)
{
	VkSemaphore timelines[GRANITE_FRAME_QUEUES];
	unsigned i;

	num_calls = 0;
	ended = 0;
	barriers = 0;
	cb_next = 0;

	granite_submission_init(&sub);
	sub.device = DEV;
	sub.table = &table;
	sub.timeline = timeline;
	for (i = 0; i < GRANITE_FRAME_QUEUES; i++)
		sub.queues[i].queue = one_queue ? Q(0) : Q(i);
	sub.fences = &fence_pool;
	sub.semaphores = &semaphore_pool;
	sub.fence_objects = &fence_objects;
	sub.semaphore_objects = &semaphore_objects;
	sub.owner = &owner;
	granite_submission_init_timelines(&sub);

	env.timeline = timeline;
	for (i = 0; i < GRANITE_FRAME_QUEUES; i++)
		timelines[i] = sub.queues[i].timeline_semaphore;
	CHECK(granite_frame_context_init(&frame, &env, 0, families, timelines, 1), "a frame context comes up");
}

static void teardown(void)
{
	granite_submission_wait_idle(&sub);
	granite_frame_sync_forget_fences(&frame.sync);
	granite_frame_context_free(&frame);
	granite_submission_free_timelines(&sub);
	granite_submission_free(&sub);
}

/* a command buffer being recorded for @queue */
static granite_command_buffer_t *new_cb(unsigned queue)
{
	granite_command_buffer_t *cb = (granite_command_buffer_t *)granite_lf_pool_alloc(&cb_objects);
	memset(cb, 0, sizeof(*cb));
	granite_refs_init(&cb->refs);
	cb->pool = &cb_objects;
	cb->dev = &cmd_device;
	cb->table = &table;
	cb->cmd = (VkCommandBuffer)&cb_storage[cb_next++];
	cb->type = queue;
	return cb;
}

/* submitted, and the recorder lets go */
static VkCommandBuffer submit_plain(unsigned queue)
{
	granite_command_buffer_t *cb = new_cb(queue);
	VkCommandBuffer cmd = cb->cmd;
	granite_submission_submit(&sub, &frame, cb, NULL, 0, NULL);
	granite_cb_unref(cb);
	return cmd;
}

static void test_timeline(void)
{
	granite_command_buffer_t *cb;
	granite_fence_t *fence = NULL;
	granite_semaphore_t *sems[2];
	granite_semaphore_t *many[GRANITE_QUEUE_MAX_WAITS + 1];
	VkCommandBuffer a, b, c;
	VkSemaphore tg, tc, tt;
	unsigned before, i;
	uint64_t value;

	before = timelines_created;
	setup(true, false);
	tg = sub.queues[G].timeline_semaphore;
	tc = sub.queues[C].timeline_semaphore;
	tt = sub.queues[T].timeline_semaphore;
	CHECK(timelines_created == before + 3 && tg && tc && tt && tg != tc && tc != tt, "a timeline semaphore per queue");

	/* ---- a command buffer waits for the queue's next submission ---- */
	cb = new_cb(G);
	a = cb->cmd;
	granite_submission_submit(&sub, &frame, cb, NULL, 0, NULL);
	CHECK(num_calls == 0 && ended == 1 && frame.num_submissions[G] == 1 && frame.submissions[G][0] == cb,
			"submitted without a fence: ended, and waiting in the frame");
	CHECK(!granite_refs_only(&cb->refs), "kept alive by the frame");
	granite_cb_unref(cb);
	b = submit_plain(G);
	CHECK(num_calls == 0 && frame.num_submissions[G] == 2, "a second one waits with it");

	granite_submission_flush(&sub, &frame);
	CHECK(num_calls == 1 && calls[0].queue == Q(G) && calls[0].fence == VK_NULL_HANDLE
			&& calls[0].num_cmds == 2 && calls[0].cmds[0] == a && calls[0].cmds[1] == b,
			"flushed: both go to their queue in one submission, in order");
	CHECK(calls[0].num_waits == 0 && calls[0].num_signals == 1 && calls[0].signals[0] == tg
			&& calls[0].signal_values[0] == 1, "which signals the queue's timeline's next value");
	CHECK(frame.num_submissions[G] == 0 && frame.sync.timeline_values[G] == 1 && frame.sync.timeline_values[C] == 0
			&& sub.queues[G].current_timeline == 1, "the frame waits for that value");
	granite_submission_flush(&sub, &frame);
	CHECK(num_calls == 1, "with nothing waiting a flush submits nothing");

	/* ---- transfer first ---- */
	a = submit_plain(C);
	b = submit_plain(T);
	c = submit_plain(G);
	granite_submission_flush_queue(&sub, &frame, C);
	CHECK(num_calls == 3 && calls[1].queue == Q(T) && calls[1].cmds[0] == b && calls[1].signals[0] == tt
			&& calls[2].queue == Q(C) && calls[2].cmds[0] == a && calls[2].signals[0] == tc
			&& frame.num_submissions[G] == 1, "the transfer queue's go out before another queue's; the rest wait");
	granite_submission_flush(&sub, &frame);
	CHECK(num_calls == 4 && calls[3].queue == Q(G) && calls[3].cmds[0] == c && calls[3].signal_values[0] == 2,
			"then the graphics queue's, with its timeline's next value");

	/* ---- a fence: at once, and it is the timeline value ---- */
	before = fences_created;
	cb = new_cb(C);
	a = cb->cmd;
	granite_submission_submit(&sub, &frame, cb, &fence, 0, NULL);
	granite_cb_unref(cb);
	CHECK(num_calls == 5 && calls[4].queue == Q(C) && calls[4].cmds[0] == a && calls[4].fence == VK_NULL_HANDLE,
			"asking for a fence submits at once");
	CHECK(fence && fence->fence == VK_NULL_HANDLE && fence->timeline == tc && fence->value == 2
			&& calls[4].signal_values[0] == 2 && fences_created == before, "the fence is the timeline value, not a VkFence");
	CHECK(fence->env.device == DEV && fence->env.table == &table && granite_refs_only(&fence->refs),
			"and is the caller's alone, able to wait");
	granite_fence_unref(fence);
	CHECK(owner_fences == 0, "(there is no VkFence to give back)");

	/* ---- semaphores: the timeline value too ---- */
	before = semaphores_created;
	cb = new_cb(C);
	sems[0] = sems[1] = NULL;
	granite_submission_submit(&sub, &frame, cb, NULL, 2, sems);
	granite_cb_unref(cb);
	CHECK(num_calls == 6 && calls[5].num_signals == 1 && calls[5].signal_values[0] == 3,
			"asking for semaphores submits at once, signalling the timeline once");
	CHECK(sems[0] && sems[1] && sems[0] != sems[1] && sems[0]->semaphore == tc && sems[0]->value == 3
			&& sems[1]->semaphore == tc && sems[1]->value == 3 && semaphores_created == before,
			"each semaphore is that value");

	/* ---- the next submission of a queue waits for it ---- */
	a = submit_plain(G);
	granite_submission_add_wait(&sub, &frame, G, sems[0], VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, true);
	CHECK(num_calls == 7 && calls[6].queue == Q(G) && calls[6].cmds[0] == a && calls[6].num_waits == 0,
			"a flushing wait first sends what the queue already has, which does not wait");
	CHECK(sub.queues[G].waits.count == 1 && sub.queues[G].need_fence && !granite_refs_only(&sems[0]->refs),
			"then the queue has the wait");
	granite_semaphore_unref(sems[0]);
	b = submit_plain(G);
	granite_submission_add_wait(&sub, &frame, G, sems[1], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, false);
	granite_semaphore_unref(sems[1]);
	CHECK(num_calls == 7 && sub.queues[G].waits.count == 2, "a wait that does not flush submits nothing");
	granite_submission_add_wait(&sub, &frame, G, NULL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, false);
	CHECK(sub.queues[G].waits.count == 2, "no semaphore is no wait");
	granite_submission_flush(&sub, &frame);
	CHECK(num_calls == 8 && calls[7].cmds[0] == b && calls[7].num_waits == 2
			&& calls[7].waits[0] == tc && calls[7].wait_values[0] == 3
			&& calls[7].wait_stages[0] == VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
			&& calls[7].wait_stages[1] == VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			"the next submission waits for both, each at its stages");
	CHECK(sub.queues[G].waits.count == 0, "once");

	/* ---- ending the frame ---- */
	CHECK(sub.queues[G].need_fence, "(a queue that was given a wait still owes the frame)");
	a = submit_plain(T);
	granite_submission_end_frame(&sub, &frame);
	CHECK(num_calls == 10 && calls[8].queue == Q(T) && calls[8].cmds[0] == a
			&& calls[9].queue == Q(G) && calls[9].num_cmds == 0,
			"ending the frame sends what is left, transfer first, and settles the queue that owed");
	CHECK(!sub.queues[G].need_fence && frame.sync.num_fences == 0, "with timelines the frame needs no fence");
	granite_submission_end_frame(&sub, &frame);
	CHECK(num_calls == 10, "ending it again submits nothing");

	/* ---- an empty submission ---- */
	granite_submission_submit_empty(&sub, &frame, C, NULL);
	CHECK(num_calls == 10, "nothing to send and nothing to signal: no submission");
	value = sub.queues[C].current_timeline;
	fence = NULL;
	granite_submission_submit_empty(&sub, &frame, C, &fence);
	CHECK(num_calls == 11 && calls[10].queue == Q(C) && calls[10].num_cmds == 0 && calls[10].signals[0] == tc
			&& fence && fence->value == value + 1 && fence->timeline == tc,
			"for a fence an empty submission is made");
	granite_fence_unref(fence);

	/* ---- a full list of waits ---- */
	for (i = 0; i < GRANITE_QUEUE_MAX_WAITS + 1; i++)
	{
		many[i] = granite_semaphore_new_timeline(&semaphore_objects, &owner, tc, 100 + i);
		granite_submission_add_wait(&sub, &frame, T, many[i], VK_PIPELINE_STAGE_TRANSFER_BIT, false);
		granite_semaphore_unref(many[i]);
		CHECK(num_calls == (i < GRANITE_QUEUE_MAX_WAITS ? 11u : 12u), "nothing is submitted until the list is full");
	}
	CHECK(calls[11].queue == Q(T) && calls[11].num_cmds == 0 && calls[11].num_waits == GRANITE_QUEUE_MAX_WAITS
			&& calls[11].wait_values[0] == 100 && sub.queues[T].waits.count == 1,
			"a full list goes out in an empty submission, and the new wait is kept");

	/* ---- idle ---- */
	before = idle_waits;
	granite_submission_wait_idle(&sub);
	CHECK(idle_waits == before + 1 && sub.queues[T].waits.count == 0, "idle: the device is waited for, the waits forgotten");

	before = semaphores_destroyed;
	teardown();
	CHECK(semaphores_destroyed == before + 3 && sub.queues[G].timeline_semaphore == VK_NULL_HANDLE,
			"the timeline semaphores are destroyed");
}

static void test_fences(void)
{
	granite_command_buffer_t *cb;
	granite_fence_t *fence = NULL;
	granite_semaphore_t *sem = NULL;
	VkCommandBuffer a;
	VkFence vk_fence;
	unsigned before, before_sems, i;

	before = semaphores_created;
	setup(false, false);
	CHECK(semaphores_created == before && sub.queues[G].timeline_semaphore == VK_NULL_HANDLE,
			"without timeline semaphores none is made");

	/* ---- a fence of its own ---- */
	before = fences_created;
	cb = new_cb(G);
	a = cb->cmd;
	granite_submission_submit(&sub, &frame, cb, &fence, 0, NULL);
	granite_cb_unref(cb);
	CHECK(num_calls == 1 && calls[0].cmds[0] == a && calls[0].num_signals == 0 && fences_created == before + 1
			&& calls[0].fence != VK_NULL_HANDLE, "the submission signals a VkFence");
	CHECK(fence && fence->fence == calls[0].fence && fence->value == 0 && fence->timeline == VK_NULL_HANDLE,
			"which the fence handed out is");
	CHECK(sub.queues[G].need_fence, "and the queue owes the frame a fence");
	vk_fence = fence->fence;
	before = owner_fences;
	granite_fence_unref(fence);
	CHECK(owner_fences == before + 1 && owner_fence == vk_fence, "let go, the VkFence goes back to the owner");

	/* ---- a binary semaphore of its own ---- */
	before_sems = semaphores_created;
	cb = new_cb(G);
	granite_submission_submit(&sub, &frame, cb, NULL, 1, &sem);
	granite_cb_unref(cb);
	CHECK(num_calls == 2 && calls[1].fence == VK_NULL_HANDLE && calls[1].num_signals == 1
			&& !is_timeline(calls[1].signals[0]) && calls[1].signal_values[0] == 0 && semaphores_created == before_sems + 1,
			"the submission signals a binary semaphore");
	CHECK(sem && sem->semaphore == calls[1].signals[0] && sem->value == 0 && sem->signalled && sem->owned,
			"which the semaphore handed out is");
	granite_submission_add_wait(&sub, &frame, C, sem, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, true);
	granite_semaphore_unref(sem);
	a = submit_plain(C);
	granite_submission_flush(&sub, &frame);
	CHECK(num_calls == 3 && calls[2].queue == Q(C) && calls[2].cmds[0] == a && calls[2].num_waits == 1
			&& calls[2].waits[0] == calls[1].signals[0] && calls[2].wait_values[0] == 0,
			"the other queue's next submission waits for it");

	/* ---- ending the frame leaves it fences ---- */
	a = submit_plain(T);
	granite_submission_end_frame(&sub, &frame);
	CHECK(num_calls == 6 && calls[3].queue == Q(T) && calls[3].cmds[0] == a && calls[3].fence != VK_NULL_HANDLE
			&& calls[4].queue == Q(G) && calls[4].num_cmds == 0 && calls[4].fence != VK_NULL_HANDLE
			&& calls[5].queue == Q(C) && calls[5].fence != VK_NULL_HANDLE,
			"ending the frame submits to every queue that was used, each with a fence");
	CHECK(frame.sync.num_fences == 3 && frame.sync.fences[0] == calls[3].fence && frame.sync.fences[2] == calls[5].fence,
			"which the frame waits for");
	CHECK(frame.garbage.lists[GRANITE_GARBAGE_RECYCLE_FENCE].count == 3
			&& frame.garbage.lists[GRANITE_GARBAGE_RECYCLE_FENCE].items[1] == GRANITE_VK_TO_BITS(calls[4].fence),
			"and gives back afterwards");
	CHECK(!sub.queues[G].need_fence && !sub.queues[C].need_fence && !sub.queues[T].need_fence, "nothing is owed");
	granite_submission_end_frame(&sub, &frame);
	CHECK(num_calls == 6, "ending it again submits nothing");

	/* ---- no room among the frame's fences: waited for at once ---- */
	for (i = frame.sync.num_fences; i < GRANITE_FRAME_SYNC_FENCES; i++)
		granite_frame_sync_add_fence(&frame.sync, GRANITE_VK_FROM_BITS(VkFence, 0x900 + i));
	before = fence_waits;
	submit_plain(G);
	granite_submission_end_frame(&sub, &frame);
	CHECK(num_calls == 7 && fence_waits == before + 1 && fence_waited == calls[6].fence
			&& frame.sync.num_fences == GRANITE_FRAME_SYNC_FENCES
			&& frame.garbage.lists[GRANITE_GARBAGE_RECYCLE_FENCE].count == 4,
			"a fence the frame has no room for is waited for there and then, and still given back");

	teardown();
}

static void test_staging(void)
{
	granite_command_buffer_t *cb;
	VkCommandBuffer a;

	/* ---- queues of their own: semaphores ---- */
	setup(true, false);
	cb = new_cb(T);
	a = cb->cmd;
	granite_submission_submit_staging(&sub, &frame, cb, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, false);
	granite_cb_unref(cb);
	CHECK(barriers == 0 && num_calls == 1 && calls[0].queue == Q(T) && calls[0].cmds[0] == a,
			"filled on its own queue: submitted at once, with no barrier");
	CHECK(sub.queues[G].waits.count == 1 && sub.queues[C].waits.count == 1
			&& sub.queues[G].waits.semaphores[0] != sub.queues[C].waits.semaphores[0]
			&& sub.queues[G].waits.semaphores[0]->value == 1
			&& granite_refs_only(&sub.queues[G].waits.semaphores[0]->refs)
			&& granite_refs_only(&sub.queues[C].waits.semaphores[0]->refs),
			"the graphics and compute queues each wait for it, with the only reference");
	CHECK((sub.queues[C].waits.stages[0] & VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
			&& !(sub.queues[C].waits.stages[0] & VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
			&& (sub.queues[G].waits.stages[0] & VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT),
			"each at the stages it can use the buffer in");

	/* graphics only: one semaphore, and a flush of what graphics has */
	submit_plain(G);
	cb = new_cb(T);
	granite_submission_submit_staging(&sub, &frame, cb, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true);
	granite_cb_unref(cb);
	CHECK(num_calls == 3 && calls[1].queue == Q(T) && calls[2].queue == Q(G) && calls[2].num_waits == 1
			&& sub.queues[G].waits.count == 1 && sub.queues[C].waits.count == 1,
			"a vertex buffer: only the graphics queue waits, after sending what it had");
	teardown();

	/* ---- one queue for everything: a barrier ---- */
	setup(true, true);
	cb = new_cb(G);
	granite_submission_submit_staging(&sub, &frame, cb, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
	granite_cb_unref(cb);
	CHECK(barriers == 1 && barrier_src == VK_PIPELINE_STAGE_TRANSFER_BIT && barrier_src_access == VK_ACCESS_TRANSFER_WRITE_BIT
			&& (barrier_dst & VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT) && (barrier_dst_access & VK_ACCESS_SHADER_READ_BIT),
			"filled on the queue that uses it: a barrier from the transfer write");
	CHECK(num_calls == 0 && ended == 1 && frame.num_submissions[G] == 1 && sub.queues[G].waits.count == 0
			&& sub.queues[C].waits.count == 0, "and it waits for the next submission like any other");
	teardown();
}

static void test_queue_lock(void)
{
	granite_fence_t *fence = NULL;

	setup(true, false);
	sub.queue_lock = lock_queue;
	sub.queue_unlock = unlock_queue;
	sub.queue_user = &locks;
	use_lock = true;

	submit_plain(T);
	submit_plain(G);
	granite_submission_flush(&sub, &frame);
	granite_submission_submit_empty(&sub, &frame, C, &fence);
	granite_fence_unref(fence);
	CHECK(num_calls == 3 && locks == 3 && unlocks == 3 && unlocked_uses == 0 && !locked,
			"the queue lock is held around each submission");
	granite_submission_wait_idle(&sub);
	CHECK(locks == 4 && unlocks == 4 && unlocked_uses == 0, "and around waiting for the device");
	use_lock = false;

	/* a queue the device does not have */
	sub.queues[C].queue = VK_NULL_HANDLE;
	submit_plain(C);
	granite_submission_flush(&sub, &frame);
	CHECK(num_calls == 3, "nothing goes to a queue the device lacks");
	granite_frame_context_submitted(&frame, C);
	teardown();
}

int main(void)
{
	memset(&table, 0, sizeof(table));
	table.vkQueueSubmit = fake_queue_submit;
	table.vkDeviceWaitIdle = fake_wait_idle;
	table.vkCreateSemaphore = fake_create_semaphore;
	table.vkDestroySemaphore = fake_destroy_semaphore;
	table.vkCreateFence = fake_create_fence;
	table.vkDestroyFence = fake_destroy_fence;
	table.vkResetFences = fake_reset_fences;
	table.vkWaitForFences = fake_wait_fences;
	table.vkWaitSemaphoresKHR = fake_wait_semaphores;
	table.vkCreateCommandPool = fake_create_pool;
	table.vkDestroyCommandPool = fake_destroy_pool;
	table.vkEndCommandBuffer = fake_end_command_buffer;
	table.vkCmdPipelineBarrier = fake_pipeline_barrier;

	owner.destroy = owner_destroy;
	owner.free_memory = owner_free_memory;
	owner.recycle_fence = owner_recycle_fence;

	granite_fence_pool_init(&fence_pool, DEV, &table);
	granite_semaphore_pool_init(&semaphore_pool, DEV, &table);
	granite_release_queue_init(&releases);
	granite_memory_release_queue_init(&memory_releases);
	granite_fence_pool_slots_init(&fence_objects);
	granite_semaphore_pool_slots_init(&semaphore_objects);
	granite_lf_pool_init(&cb_objects, sizeof(granite_command_buffer_t), 16);

	memset(&workarounds, 0, sizeof(workarounds));
	memset(&cmd_device, 0, sizeof(cmd_device));
	cmd_device.device = DEV;
	cmd_device.table = &table;
	cmd_device.workarounds = &workarounds;

	memset(&env, 0, sizeof(env));
	env.device = DEV;
	env.table = &table;
	env.fences = &fence_pool;
	env.semaphores = &semaphore_pool;
	env.releases = &releases;
	env.memory_releases = &memory_releases;

	test_timeline();
	test_fences();
	test_staging();
	test_queue_lock();

	granite_lf_pool_clear(&cb_objects);
	granite_lf_pool_clear(&semaphore_objects);
	granite_lf_pool_clear(&fence_objects);
	granite_memory_release_queue_free(&memory_releases);
	granite_release_queue_free(&releases);
	granite_semaphore_pool_free(&semaphore_pool);
	granite_fence_pool_free(&fence_pool);

	if (failures)
	{
		fprintf(stderr, "test_vk_submission: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_vk_submission: ok\n");
	return 0;
}
