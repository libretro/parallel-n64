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
#include "memory_release.h"

void granite_memory_garbage_push(granite_memory_garbage_t *garbage, const granite_allocation_t *alloc)
{
	if (garbage->count == garbage->capacity)
	{
		size_t cap = garbage->capacity ? garbage->capacity * 2 : 16;
		granite_allocation_t *grown = (granite_allocation_t *)realloc(garbage->items, cap * sizeof(*grown));
		if (!grown)
			abort();
		garbage->items = grown;
		garbage->capacity = cap;
	}
	garbage->items[garbage->count++] = *alloc;
}

void granite_memory_garbage_collect(granite_memory_garbage_t *garbage, granite_device_allocator_t *allocator)
{
	size_t i;
	for (i = 0; i < garbage->count; i++)
		granite_allocation_free(&garbage->items[i], allocator);
	garbage->count = 0;
}

void granite_memory_garbage_free(granite_memory_garbage_t *garbage)
{
	free(garbage->items);
	garbage->items = NULL;
	garbage->count = 0;
	garbage->capacity = 0;
}

void granite_memory_release_queue_init(granite_memory_release_queue_t *queue)
{
	unsigned i;
	granite_lf_pool_init(&queue->nodes, sizeof(granite_memory_release_node_t), sizeof(void*));
	for (i = 0; i < GRANITE_RELEASE_FRAMES; i++)
		mpsc_stack_init(&queue->frames[i]);
}

void granite_memory_release_queue_free(granite_memory_release_queue_t *queue)
{
	granite_lf_pool_clear(&queue->nodes);
}

void granite_memory_release_queue_push(granite_memory_release_queue_t *queue, unsigned frame,
		const granite_allocation_t *alloc)
{
	granite_memory_release_node_t *node = (granite_memory_release_node_t *)granite_lf_pool_alloc(&queue->nodes);
	if (!node)
		abort();
	node->alloc = *alloc;
	mpsc_stack_push(&queue->frames[frame % GRANITE_RELEASE_FRAMES], &node->link);
}

void granite_memory_release_queue_drain(granite_memory_release_queue_t *queue, unsigned frame,
		granite_memory_garbage_t *garbage)
{
	mpsc_stack_node_t *item = mpsc_stack_drain(&queue->frames[frame % GRANITE_RELEASE_FRAMES]);
	while (item)
	{
		granite_memory_release_node_t *node = (granite_memory_release_node_t *)item;
		item = item->next;
		granite_memory_garbage_push(garbage, &node->alloc);
		granite_lf_pool_release(&queue->nodes, node);
	}
}
