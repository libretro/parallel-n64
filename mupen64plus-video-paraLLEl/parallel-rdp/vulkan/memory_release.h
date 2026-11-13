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

#ifndef GRANITE_MEMORY_RELEASE_H
#define GRANITE_MEMORY_RELEASE_H

#include <stddef.h>
#include <boolean.h>
#include <retro_common_api.h>
#include <retro_atomic.h>
#include <queues/mpsc_stack.h>
#include "memory_allocator.h"
#include "release_queue.h"
#include "pool.h"

RETRO_BEGIN_DECLS

/* The device memory a frame context frees once the GPU is done with
 * it. The list keeps its room from frame to frame: it grows to the most
 * a frame ever freed and no further. Owner thread only. */
typedef struct granite_memory_garbage
{
	granite_allocation_t *items;
	size_t count;
	size_t capacity;
} granite_memory_garbage_t;

/* Out of memory ends the program: the GPU may still be using what
 * cannot be listed. */
void granite_memory_garbage_push(granite_memory_garbage_t *garbage, const granite_allocation_t *alloc);
/* Gives it all back to @allocator; the list is empty and keeps its room */
void granite_memory_garbage_collect(granite_memory_garbage_t *garbage, granite_device_allocator_t *allocator);
/* Teardown: collect first */
void granite_memory_garbage_free(granite_memory_garbage_t *garbage);

/* Memory released from any thread, without a lock, for the frame
 * context current when it is released (release_queue.h has the same for
 * Vulkan objects). */
typedef struct granite_memory_release_node
{
	mpsc_stack_node_t link;
	granite_allocation_t alloc;
} granite_memory_release_node_t;

typedef struct granite_memory_release_queue
{
	granite_lf_pool_t nodes;
	mpsc_stack_t frames[GRANITE_RELEASE_FRAMES];
} granite_memory_release_queue_t;

void granite_memory_release_queue_init(granite_memory_release_queue_t *queue);
/* Teardown: drain every frame first */
void granite_memory_release_queue_free(granite_memory_release_queue_t *queue);
/* Any thread: @alloc is freed when frame context @frame comes round */
void granite_memory_release_queue_push(granite_memory_release_queue_t *queue, unsigned frame,
		const granite_allocation_t *alloc);
/* The owning thread, after the frame context's GPU work is done: moves
 * what was released to it into its list */
void granite_memory_release_queue_drain(granite_memory_release_queue_t *queue, unsigned frame,
		granite_memory_garbage_t *garbage);

RETRO_END_DECLS

#endif
