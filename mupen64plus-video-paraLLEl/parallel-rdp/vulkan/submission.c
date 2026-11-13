/* Copyright (c) 2017-2022 Hans-Kristian Arntzen
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <string.h>
#include "submission.h"
#include "buffer_create.h"
#include "context.h"
#include "logging.h"

#define QUEUE_GRAPHICS GRANITE_QUEUE_GRAPHICS
#define QUEUE_COMPUTE  GRANITE_QUEUE_COMPUTE
#define QUEUE_TRANSFER GRANITE_QUEUE_TRANSFER

/* the transfer queue first: the others use what it filled */
static const unsigned queue_flush_order[GRANITE_FRAME_QUEUES] = { QUEUE_TRANSFER, QUEUE_GRAPHICS, QUEUE_COMPUTE };

/* what a submission can be waited for on, before it has its object */
typedef struct pending_fence
{
	VkFence fence;
	VkSemaphore timeline;
	uint64_t value;
} pending_fence_t;

void granite_submission_init(granite_submission_t *s)
{
	memset(s, 0, sizeof(*s));
	granite_submit_composer_init(&s->composer);
}

void granite_submission_free(granite_submission_t *s)
{
	granite_submit_composer_free(&s->composer);
}

void granite_submission_init_timelines(granite_submission_t *s)
{
	VkSemaphoreTypeCreateInfoKHR type_info;
	VkSemaphoreCreateInfo info;
	unsigned i;

	if (!s->timeline)
		return;

	memset(&type_info, 0, sizeof(type_info));
	type_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO_KHR;
	type_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE_KHR;
	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	info.pNext = &type_info;

	for (i = 0; i < GRANITE_FRAME_QUEUES; i++)
	{
		if (s->table->vkCreateSemaphore(s->device, &info, NULL, &s->queues[i].timeline_semaphore) != VK_SUCCESS)
		{
			LOGE("Failed to create timeline semaphore.\n");
			s->queues[i].timeline_semaphore = VK_NULL_HANDLE;
		}
	}
}

void granite_submission_free_timelines(granite_submission_t *s)
{
	unsigned i;
	for (i = 0; i < GRANITE_FRAME_QUEUES; i++)
	{
		if (s->queues[i].timeline_semaphore != VK_NULL_HANDLE)
			s->table->vkDestroySemaphore(s->device, s->queues[i].timeline_semaphore, NULL);
		s->queues[i].timeline_semaphore = VK_NULL_HANDLE;
	}
}

/* What the submission being composed signals: the queue's timeline, or
 * a fence and binary semaphores of its own */
static void emit_signals(granite_submission_t *s, granite_frame_context_t *frame, VkSemaphore sem,
		uint64_t timeline, pending_fence_t *fence, unsigned semaphore_count, granite_semaphore_t **semaphores)
{
	unsigned i;

	if (s->timeline)
	{
		/* Signal once and distribute the timeline value to all. */
		granite_submit_composer_add_signal(&s->composer, sem, timeline);

		if (fence)
		{
			fence->timeline = sem;
			fence->value = timeline;
			fence->fence = VK_NULL_HANDLE;
		}

		for (i = 0; i < semaphore_count; i++)
			semaphores[i] = granite_semaphore_new_timeline(s->semaphore_objects, s->owner, sem, timeline);
	}
	else
	{
		if (fence)
		{
			fence->timeline = VK_NULL_HANDLE;
			fence->value = 0;
		}

		for (i = 0; i < semaphore_count; i++)
		{
			VkSemaphore cleared = granite_semaphore_pool_request(s->semaphores);
			granite_submit_composer_add_signal(&s->composer, cleared, 0);
			semaphores[i] = granite_semaphore_new_binary(s->semaphore_objects, s->owner, cleared);
			/* Without an object for it nothing will wait for it: it is
			 * signalled, and destroyed when this frame comes round. */
			if (!semaphores[i])
				granite_frame_garbage_push(&frame->garbage, GRANITE_GARBAGE_SEMAPHORE, GRANITE_VK_TO_BITS(cleared));
		}
	}
}

/* Everything the frame has for the queue goes in one submission, which
 * waits for the queue's semaphores and signals the fence and semaphores
 * asked for. With nothing to submit and nothing to signal there is no
 * submission, unless @force asks for an empty one. */
static void submit_queue(granite_submission_t *s, granite_frame_context_t *frame, unsigned queue,
		pending_fence_t *fence, unsigned semaphore_count, granite_semaphore_t **semaphores, bool force)
{
	granite_submit_queue_t *data = &s->queues[queue];
	VkFence cleared_fence = VK_NULL_HANDLE;
	VkResult result;
	unsigned i;

	/* Always check if we need to flush pending transfers. */
	if (queue != QUEUE_TRANSFER)
		granite_submission_flush_queue(s, frame, QUEUE_TRANSFER);

	if (!frame->num_submissions[queue] && !fence && !semaphore_count && !force)
		return;

	data->current_timeline++;
	frame->sync.timeline_values[queue] = data->current_timeline;

	granite_submit_composer_begin(&s->composer, s->split);
	granite_queue_waits_collect(&data->waits, &s->composer, &frame->garbage);

	for (i = 0; i < frame->num_submissions[queue]; i++)
		granite_submit_composer_add_command_buffer(&s->composer, frame->submissions[queue][i]->cmd);

	if (fence && !s->timeline)
		cleared_fence = granite_fence_pool_request(s->fences);
	if (fence)
		fence->fence = cleared_fence;

	emit_signals(s, frame, data->timeline_semaphore, data->current_timeline, fence, semaphore_count, semaphores);

	if (s->queue_lock)
		s->queue_lock(s->queue_user);
	result = granite_submit_composer_submit(&s->composer, s->table, data->queue, cleared_fence, s->split);
	if (s->queue_unlock)
		s->queue_unlock(s->queue_user);

	if (result != VK_SUCCESS)
		LOGE("vkQueueSubmit failed (code: %d).\n", (int)result);
	granite_frame_context_submitted(frame, queue);

	if (!s->timeline)
		data->need_fence = true;
}

/* The counted object for what a submission is waited on by */
static granite_fence_t *wrap_fence(granite_submission_t *s, granite_frame_context_t *frame,
		const pending_fence_t *fence)
{
	granite_fence_env_t env;
	granite_fence_t *object;

	env.device = s->device;
	env.table = s->table;
	env.wait_ec = s->fence_wait_ec;
	object = granite_fence_new(s->fence_objects, s->owner, &env, fence->fence, fence->timeline, fence->value);
	/* Without one the fence still goes back when its frame comes round. */
	if (!object && !fence->value && fence->fence != VK_NULL_HANDLE)
		granite_frame_garbage_push(&frame->garbage, GRANITE_GARBAGE_RECYCLE_FENCE, GRANITE_VK_TO_BITS(fence->fence));
	return object;
}

void granite_submission_add_wait(granite_submission_t *s, granite_frame_context_t *frame, unsigned queue,
		granite_semaphore_t *semaphore, VkPipelineStageFlags stages, bool flush)
{
	granite_submit_queue_t *data = &s->queues[queue];

	if (flush)
		granite_submission_flush_queue(s, frame, queue);

	/* (there was no room for the semaphore's object: nothing to wait for) */
	if (!semaphore)
		return;

	if (!granite_queue_waits_add(&data->waits, semaphore, stages))
	{
		/* No room: an empty submission takes the ones waiting with it. */
		submit_queue(s, frame, queue, NULL, 0, NULL, true);
		granite_queue_waits_add(&data->waits, semaphore, stages);
	}
	data->need_fence = true;
}

void granite_submission_submit(granite_submission_t *s, granite_frame_context_t *frame,
		granite_command_buffer_t *cb, granite_fence_t **fence,
		unsigned semaphore_count, granite_semaphore_t **semaphores)
{
	unsigned queue = cb->type;
	pending_fence_t signalled;

	granite_cb_end(cb);
	granite_frame_context_submit(frame, queue, cb);

	if (fence || semaphore_count)
		submit_queue(s, frame, queue, fence ? &signalled : NULL, semaphore_count, semaphores, false);

	if (fence)
		*fence = wrap_fence(s, frame, &signalled);
}

void granite_submission_submit_empty(granite_submission_t *s, granite_frame_context_t *frame, unsigned queue,
		granite_fence_t **fence)
{
	pending_fence_t signalled;

	submit_queue(s, frame, queue, fence ? &signalled : NULL, 0, NULL, false);

	if (fence)
		*fence = wrap_fence(s, frame, &signalled);
}

void granite_submission_submit_staging(granite_submission_t *s, granite_frame_context_t *frame,
		granite_command_buffer_t *cb, VkBufferUsageFlags usage, bool flush)
{
	granite_staging_plan_t plan;
	/* (graphics first, then compute) */
	granite_semaphore_t *semaphores[2];
	unsigned count, i;

	semaphores[0] = NULL;
	semaphores[1] = NULL;

	granite_staging_plan(s->queues[cb->type].queue, s->queues[QUEUE_GRAPHICS].queue, s->queues[QUEUE_COMPUTE].queue,
			granite_buffer_usage_stages(usage), granite_buffer_usage_access(usage), &plan);

	if (plan.barrier_stages)
	{
		VkMemoryBarrier global;
		memset(&global, 0, sizeof(global));
		global.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		global.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		global.dstAccessMask = plan.barrier_access;
		granite_cb_barrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, plan.barrier_stages, 1, &global, 0, NULL, 0, NULL);
	}

	count = (plan.wait_graphics ? 1u : 0u) + (plan.wait_compute ? 1u : 0u);
	granite_submission_submit(s, frame, cb, NULL, count, semaphores);
	if (plan.wait_graphics)
		granite_submission_add_wait(s, frame, QUEUE_GRAPHICS, semaphores[0], plan.graphics_stages, flush);
	if (plan.wait_compute)
		granite_submission_add_wait(s, frame, QUEUE_COMPUTE, semaphores[plan.wait_graphics ? 1 : 0],
				plan.compute_stages, flush);

	/* (the queues' lists have their own references) */
	for (i = 0; i < count; i++)
		if (semaphores[i])
			granite_semaphore_unref(semaphores[i]);
}

void granite_submission_flush_queue(granite_submission_t *s, granite_frame_context_t *frame, unsigned queue)
{
	if (s->queues[queue].queue == VK_NULL_HANDLE)
		return;
	submit_queue(s, frame, queue, NULL, 0, NULL, false);
}

void granite_submission_flush(granite_submission_t *s, granite_frame_context_t *frame)
{
	unsigned i;
	for (i = 0; i < GRANITE_FRAME_QUEUES; i++)
		granite_submission_flush_queue(s, frame, queue_flush_order[i]);
}

void granite_submission_end_frame(granite_submission_t *s, granite_frame_context_t *frame)
{
	unsigned n;

	/* Make sure we have a fence which covers all submissions in the frame. */
	for (n = 0; n < GRANITE_FRAME_QUEUES; n++)
	{
		unsigned i = queue_flush_order[n];
		pending_fence_t fence;

		if (!s->queues[i].need_fence && !frame->num_submissions[i])
			continue;

		fence.fence = VK_NULL_HANDLE;
		submit_queue(s, frame, i, &fence, 0, NULL, false);
		if (fence.fence != VK_NULL_HANDLE)
		{
			/* (no room: wait for it here and now) */
			if (!granite_frame_sync_add_fence(&frame->sync, fence.fence))
				s->table->vkWaitForFences(s->device, 1, &fence.fence, VK_TRUE, UINT64_MAX);
			granite_frame_garbage_push(&frame->garbage, GRANITE_GARBAGE_RECYCLE_FENCE, GRANITE_VK_TO_BITS(fence.fence));
		}
		s->queues[i].need_fence = false;
	}
}

void granite_submission_wait_idle(granite_submission_t *s)
{
	unsigned i;

	if (s->device != VK_NULL_HANDLE)
	{
		VkResult result;
		if (s->queue_lock)
			s->queue_lock(s->queue_user);
		result = s->table->vkDeviceWaitIdle(s->device);
		if (result != VK_SUCCESS)
			LOGE("vkDeviceWaitIdle failed with code: %d\n", (int)result);
		if (s->queue_unlock)
			s->queue_unlock(s->queue_user);
	}

	/* (the device is idle: nothing will wait for them) */
	for (i = 0; i < GRANITE_FRAME_QUEUES; i++)
		granite_queue_waits_clear(&s->queues[i].waits, s->device, s->table);
}
