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

#ifndef GRANITE_SUBMISSION_H
#define GRANITE_SUBMISSION_H

#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>
#include <rthreads/retro_eventcount.h>
#include "volk.h"
#include "submit_batch.h"
#include "queue_submit.h"
#include "frame_context.h"
#include "sync_pools.h"
#include "semaphore.h"
#include "fence.h"
#include "handle.h"
#include "pool.h"

RETRO_BEGIN_DECLS

/* One of the device's queues, as submissions see it */
typedef struct granite_submit_queue
{
	/* VK_NULL_HANDLE: the device has no such queue */
	VkQueue queue;
	/* what its next submission waits for */
	granite_queue_waits_t waits;
	/* something was submitted the frame has no fence for yet */
	bool need_fence;
	/* counts its submissions; VK_NULL_HANDLE without timeline semaphores */
	VkSemaphore timeline_semaphore;
	uint64_t current_timeline;
} granite_submit_queue_t;

/* How the device's command buffers get to its queues. Everything a
 * frame has for a queue goes out in one vkQueueSubmit, the transfer
 * queue's always before the others'. Owner thread only. */
typedef struct granite_submission
{
	VkDevice device;
	const struct VolkDeviceTable *table;
	/* the device has timeline semaphores */
	bool timeline;
	/* binary and timeline semaphores go in submissions of their own */
	bool split;
	granite_submit_queue_t queues[GRANITE_FRAME_QUEUES];
	granite_submit_composer_t composer;

	/* VkFences and VkSemaphores to signal */
	granite_fence_pool_t *fences;
	granite_semaphore_pool_t *semaphores;
	/* the counted objects handed out for them */
	granite_lf_pool_t *fence_objects;
	granite_lf_pool_t *semaphore_objects;
	const granite_handle_owner_t *owner;
	/* where threads waiting on one fence park; or NULL */
	retro_eventcount_t *fence_wait_ec;

	/* held around every use of a queue, which the frontend shares; or NULL */
	void (*queue_lock)(void *user);
	void (*queue_unlock)(void *user);
	void *queue_user;
} granite_submission_t;

/* Zeroes it and readies its composer; the caller fills in the rest */
void granite_submission_init(granite_submission_t *s);
void granite_submission_free(granite_submission_t *s);

/* A timeline semaphore per queue, when the device has them */
void granite_submission_init_timelines(granite_submission_t *s);
/* Destroys them. The frame contexts must forget them too. */
void granite_submission_free_timelines(granite_submission_t *s);

/* @queue's next submission waits for @semaphore at @stages (not 0);
 * @flush: what the frame has for the queue so far goes out first.
 * NULL is no semaphore. */
void granite_submission_add_wait(granite_submission_t *s, granite_frame_context_t *frame, unsigned queue,
		granite_semaphore_t *semaphore, VkPipelineStageFlags stages, bool flush);

/* @cb is done and goes out with its queue's next submission. That is
 * now if a fence or semaphores are asked for: @fence (NULL for none)
 * gets an object to wait for the submission on, and @semaphores
 * @semaphore_count objects it signals, each with one reference, or NULL
 * where there is no room for one. The caller keeps its reference to
 * @cb. */
void granite_submission_submit(granite_submission_t *s, granite_frame_context_t *frame,
		granite_command_buffer_t *cb, granite_fence_t **fence,
		unsigned semaphore_count, granite_semaphore_t **semaphores);
/* What the frame has for @queue goes out now, if only to signal @fence */
void granite_submission_submit_empty(granite_submission_t *s, granite_frame_context_t *frame, unsigned queue,
		granite_fence_t **fence);
/* @cb filled buffers that will be used as @usage: submits it so that
 * the graphics and compute queues wait for it where they have to.
 * @flush: as for granite_submission_add_wait. */
void granite_submission_submit_staging(granite_submission_t *s, granite_frame_context_t *frame,
		granite_command_buffer_t *cb, VkBufferUsageFlags usage, bool flush);

/* What the frame has for @queue goes out, if there is anything */
void granite_submission_flush_queue(granite_submission_t *s, granite_frame_context_t *frame, unsigned queue);
/* The same for every queue */
void granite_submission_flush(granite_submission_t *s, granite_frame_context_t *frame);
/* The same, and the frame has what it must wait for before it is used
 * again */
void granite_submission_end_frame(granite_submission_t *s, granite_frame_context_t *frame);

/* Blocks until the device has nothing left to do, then forgets what
 * the queues were to wait for */
void granite_submission_wait_idle(granite_submission_t *s);

RETRO_END_DECLS

#endif
