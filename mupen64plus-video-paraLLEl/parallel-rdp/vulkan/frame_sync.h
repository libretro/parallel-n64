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

#ifndef GRANITE_FRAME_SYNC_H
#define GRANITE_FRAME_SYNC_H

#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>
#include "volk.h"

RETRO_BEGIN_DECLS

#define GRANITE_FRAME_SYNC_QUEUES 3
#define GRANITE_FRAME_SYNC_FENCES 8

/* What a frame context waits for before it is used again: the last
 * value submitted to each queue's timeline semaphore in the frame, or,
 * on a device without timeline semaphores, the fences its submissions
 * signalled. Fixed in size. Owner thread only. */
typedef struct granite_frame_sync
{
	VkSemaphore timeline_semaphores[GRANITE_FRAME_SYNC_QUEUES];
	/* 0: nothing was submitted to the queue */
	uint64_t timeline_values[GRANITE_FRAME_SYNC_QUEUES];
	VkFence fences[GRANITE_FRAME_SYNC_FENCES];
	unsigned num_fences;
} granite_frame_sync_t;

/* @timelines: each queue's timeline semaphore, or VK_NULL_HANDLE */
void granite_frame_sync_init(granite_frame_sync_t *sync, const VkSemaphore *timelines);
/* The frame also waits for @fence. False when there is no room. */
bool granite_frame_sync_add_fence(granite_frame_sync_t *sync, VkFence fence);
/* Blocks until the frame's work is done, and forgets the fences.
 * @timeline: the device has timeline semaphores. They are only waited
 * for when every queue has one. */
void granite_frame_sync_wait(granite_frame_sync_t *sync, VkDevice device, const struct VolkDeviceTable *table,
		bool timeline);
/* The device is idle: there is nothing to wait for (and waiting for a
 * fence that was never submitted would not end) */
void granite_frame_sync_forget_fences(granite_frame_sync_t *sync);
/* The timeline semaphores are gone */
void granite_frame_sync_forget_timelines(granite_frame_sync_t *sync);

RETRO_END_DECLS

#endif
