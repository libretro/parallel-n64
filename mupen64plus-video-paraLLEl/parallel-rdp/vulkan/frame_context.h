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

#ifndef GRANITE_FRAME_CONTEXT_H
#define GRANITE_FRAME_CONTEXT_H

#include <stddef.h>
#include <boolean.h>
#include <retro_common_api.h>
#include "volk.h"
#include "command_pool.h"
#include "command_buffer.h"
#include "frame_sync.h"
#include "frame_garbage.h"
#include "release_queue.h"
#include "memory_release.h"
#include "memory_allocator.h"
#include "buffer_pool.h"
#include "sync_pools.h"

RETRO_BEGIN_DECLS

#define GRANITE_FRAME_QUEUES GRANITE_FRAME_SYNC_QUEUES

/* What the frame contexts of a device share: where a frame's things go
 * back to once the GPU is done with them, and the queues other threads
 * release into. Filled by the device; outlives its frame contexts. */
typedef struct granite_frame_env
{
	VkDevice device;
	const struct VolkDeviceTable *table;
	/* the device has timeline semaphores */
	bool timeline;
	granite_device_allocator_t *memory;
	granite_fence_pool_t *fences;
	granite_semaphore_pool_t *semaphores;
	granite_buffer_pool_t *vbo;
	granite_buffer_pool_t *ubo;
	granite_release_queue_t *releases;
	granite_memory_release_queue_t *memory_releases;
} granite_frame_env_t;

/* One of the frame contexts a device goes round: the command buffers
 * recorded in the frame, what was submitted and not yet flushed to its
 * queue, and everything that may only be freed or reused once the
 * frame's GPU work is done. That happens in begin, when the frame's
 * turn comes again. The lists keep their room from turn to turn.
 * Owner thread only. */
typedef struct granite_frame_context
{
	const granite_frame_env_t *env;
	unsigned index;

	/* [queue][thread] */
	granite_command_pool_t *cmd_pools[GRANITE_FRAME_QUEUES];
	unsigned threads;

	/* what the frame waits for before it is used again */
	granite_frame_sync_t sync;

	/* used this frame; back to their pools by begin */
	granite_buffer_block_list_t vbo_blocks;
	granite_buffer_block_list_t ubo_blocks;

	/* freed by begin */
	granite_memory_garbage_t memory;
	/* destroyed, or recycled, by begin */
	granite_frame_garbage_t garbage;

	/* ended command buffers waiting for their queue's next submit;
	 * each holds a reference */
	granite_command_buffer_t **submissions[GRANITE_FRAME_QUEUES];
	unsigned num_submissions[GRANITE_FRAME_QUEUES];
	unsigned submission_capacity[GRANITE_FRAME_QUEUES];
} granite_frame_context_t;

/* @families: each queue's family, VK_QUEUE_FAMILY_IGNORED for none.
 * @timelines: each queue's timeline semaphore, or VK_NULL_HANDLE.
 * @threads: how many threads record. False, with nothing to free, when
 * out of memory. */
bool granite_frame_context_init(granite_frame_context_t *frame, const granite_frame_env_t *env, unsigned index,
		const uint32_t *families, const VkSemaphore *timelines, unsigned threads);
/* Waits for the frame's work, frees what it holds, then itself. Nothing
 * may be waiting for a submit. */
void granite_frame_context_free(granite_frame_context_t *frame);

/* The frame's turn has come again: waits for its GPU work, takes what
 * other threads released to it, and gives everything back. */
void granite_frame_context_begin(granite_frame_context_t *frame);
/* Gives the command buffers' memory back to the driver */
void granite_frame_context_trim(granite_frame_context_t *frame);

/* A command buffer to record into; VK_NULL_HANDLE if none can be made */
VkCommandBuffer granite_frame_context_request(granite_frame_context_t *frame, unsigned queue, unsigned thread);

/* @cb has ended and goes out with @queue's next submit; takes a
 * reference of its own. Out of memory ends the program. */
void granite_frame_context_submit(granite_frame_context_t *frame, unsigned queue, granite_command_buffer_t *cb);
/* @queue's command buffers went to the queue: drops them */
void granite_frame_context_submitted(granite_frame_context_t *frame, unsigned queue);

RETRO_END_DECLS

#endif
