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

#ifndef GRANITE_RELEASE_QUEUE_H
#define GRANITE_RELEASE_QUEUE_H

#include <stdint.h>
#include <retro_common_api.h>
#include <retro_atomic.h>
#include <queues/mpsc_stack.h>
#include "pool.h"
#include "frame_garbage.h"

RETRO_BEGIN_DECLS

/* Vulkan objects released from any thread, without a lock, to be
 * destroyed once the GPU is done with the frame context they were last
 * used in. A release goes onto the stack of the frame context current
 * when it is made; that frame context's owner drains it into its garbage
 * lists after waiting for its GPU work, when it comes round again. */
#define GRANITE_RELEASE_FRAMES 16

typedef struct granite_release_node
{
	mpsc_stack_node_t link;
	uint64_t handle;
	unsigned kind; /* enum granite_garbage_kind */
} granite_release_node_t;

typedef struct granite_release_queue
{
	granite_lf_pool_t nodes;
	mpsc_stack_t frames[GRANITE_RELEASE_FRAMES];
	/* the frame context releases go to: published only once it has begun,
	 * so nothing released for it is drained by its own begin */
	retro_atomic_int_t frame;
} granite_release_queue_t;

void granite_release_queue_init(granite_release_queue_t *queue);
/* Teardown: drain every frame first */
void granite_release_queue_free(granite_release_queue_t *queue);

/* Any thread. Out of memory ends the program: the GPU may still be
 * using what cannot be queued. */
void granite_release_queue_push(granite_release_queue_t *queue,
		enum granite_garbage_kind kind, uint64_t handle);

/* The owning thread: releases from now on go to this frame context */
void granite_release_queue_set_frame(granite_release_queue_t *queue, unsigned frame);
/* The frame context releases go to now; any thread */
unsigned granite_release_queue_frame(granite_release_queue_t *queue);

/* The owning thread, after the frame context's GPU work is done: moves
 * what was released to it into its garbage lists */
void granite_release_queue_drain(granite_release_queue_t *queue, unsigned frame,
		granite_frame_garbage_t *garbage);

RETRO_END_DECLS

#endif
