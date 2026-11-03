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

#include <stdlib.h>
#include "release_queue.h"

void granite_release_queue_init(granite_release_queue_t *queue)
{
	unsigned i;
	granite_lf_pool_init(&queue->nodes, sizeof(granite_release_node_t), sizeof(uint64_t));
	for (i = 0; i < GRANITE_RELEASE_FRAMES; i++)
		mpsc_stack_init(&queue->frames[i]);
	retro_atomic_int_init(&queue->frame, 0);
}

void granite_release_queue_free(granite_release_queue_t *queue)
{
	granite_lf_pool_clear(&queue->nodes);
}

void granite_release_queue_push(granite_release_queue_t *queue,
		enum granite_garbage_kind kind, uint64_t handle)
{
	granite_release_node_t *node = (granite_release_node_t *)granite_lf_pool_alloc(&queue->nodes);
	unsigned frame;
	if (!node)
		abort();
	node->handle = handle;
	node->kind = (unsigned)kind;
	frame = (unsigned)retro_atomic_load_acquire_int(&queue->frame);
	mpsc_stack_push(&queue->frames[frame % GRANITE_RELEASE_FRAMES], &node->link);
}

void granite_release_queue_set_frame(granite_release_queue_t *queue, unsigned frame)
{
	retro_atomic_store_release_int(&queue->frame, (int)frame);
}

unsigned granite_release_queue_frame(granite_release_queue_t *queue)
{
	return (unsigned)retro_atomic_load_acquire_int(&queue->frame);
}

void granite_release_queue_drain(granite_release_queue_t *queue, unsigned frame,
		granite_frame_garbage_t *garbage)
{
	mpsc_stack_node_t *item = mpsc_stack_drain(&queue->frames[frame % GRANITE_RELEASE_FRAMES]);
	while (item)
	{
		granite_release_node_t *node = (granite_release_node_t *)item;
		item = item->next;
		granite_frame_garbage_push(garbage, (enum granite_garbage_kind)node->kind, node->handle);
		granite_lf_pool_release(&queue->nodes, node);
	}
}
