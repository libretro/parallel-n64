/* Copyright (c) 2020 Themaister
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
#include <string.h>
#include "rdp_timeline.h"
#include "masked_copy.h"
#include "memory_allocator.h"
#include "aligned_alloc.h"

/* A queued op, its copies after it */
typedef struct timeline_node
{
	mpsc_stack_node_t node;
	rdp_timeline_op_t op;
	/* copies it has room for */
	unsigned capacity;
} timeline_node_t;

static rdp_coherency_copy_t *node_copies(timeline_node_t *n)
{
	return (rdp_coherency_copy_t *)(n + 1);
}

static void perform(rdp_timeline_t *t, const rdp_timeline_op_t *op)
{
	unsigned c, i;

	if (op->fence)
		granite_fence_wait_for(op->fence);
	if (op->unlock_cookie)
		retro_atomic_fetch_sub_int(op->unlock_cookie, 1);
	if (!op->src)
		return;

	for (c = 0; c < op->num_copies; c++)
	{
		const rdp_coherency_copy_t *copy = &op->copies[c];
		const uint8_t *data = (const uint8_t *)granite_device_map_host_buffer(t->device, op->src,
				GRANITE_MEMORY_ACCESS_READ_BIT, copy->src_offset, copy->size);
		const uint8_t *mask = (const uint8_t *)granite_device_map_host_buffer(t->device, op->src,
				GRANITE_MEMORY_ACCESS_READ_BIT, copy->mask_offset, copy->size);
#ifdef PARALLEL_RDP_VERIFY_UPLOADS
		rdp_masked_copy_verify(op->dst + copy->dst_offset, data, mask, copy->size);
#endif
		rdp_masked_copy(op->dst + copy->dst_offset, data, mask, copy->size);
		for (i = 0; i < copy->counters; i++)
			retro_atomic_fetch_sub_int(&copy->counter_base[i], 1);
	}
	rdp_masked_copy_end();
}

static void work(void *user, mpsc_stack_node_t *item)
{
	rdp_timeline_t *t = (rdp_timeline_t *)user;
	timeline_node_t *n = (timeline_node_t *)item;

	perform(t, &n->op);
	if (n->op.timeline_value)
		retro_atomic_store_release_size(&t->done, (size_t)n->op.timeline_value);
	if (n->op.fence)
		granite_fence_unref(n->op.fence);
	n->op.fence = NULL;
	mpsc_stack_push(&t->returned, &n->node);
}

bool rdp_timeline_init(rdp_timeline_t *t, granite_device_t *device)
{
	memset(t, 0, sizeof(*t));
	t->device = device;
	retro_atomic_size_init(&t->done, 0);
	mpsc_stack_init(&t->returned);
	t->worker = rdp_worker_new(NULL, work, t);
	return t->worker != NULL;
}

static void free_list(mpsc_stack_node_t *list)
{
	while (list)
	{
		mpsc_stack_node_t *next = list->next;
		free(list);
		list = next;
	}
}

void rdp_timeline_free(rdp_timeline_t *t)
{
	rdp_worker_free(t->worker);
	t->worker = NULL;
	free_list(t->spare);
	free_list(mpsc_stack_drain(&t->returned));
	t->spare = NULL;
}

/* A node with room for @n copies: one handed back, or failing that a new
 * one */
static timeline_node_t *take_node(rdp_timeline_t *t, unsigned n)
{
	unsigned pass, capacity;
	timeline_node_t *node;

	for (pass = 0; pass < 2; pass++)
	{
		mpsc_stack_node_t **link, *back, *tail;
		for (link = &t->spare; *link; link = &(*link)->next)
		{
			node = (timeline_node_t *)*link;
			if (node->capacity >= n)
			{
				*link = node->node.next;
				return node;
			}
		}
		/* what came back since, in front of the rest */
		if (!(back = mpsc_stack_drain(&t->returned)))
			break;
		for (tail = back; tail->next; tail = tail->next)
			;
		tail->next = t->spare;
		t->spare = back;
	}

	for (capacity = 8; capacity < n; capacity <<= 1)
		;
	node = (timeline_node_t *)granite_heap_new(sizeof(*node) + capacity * sizeof(rdp_coherency_copy_t));
	memset(node, 0, sizeof(*node));
	node->capacity = capacity;
	return node;
}

void rdp_timeline_enqueue(rdp_timeline_t *t, const rdp_timeline_op_t *op)
{
	timeline_node_t *node = take_node(t, op->num_copies);
	if (op->num_copies)
		memcpy(node_copies(node), op->copies, op->num_copies * sizeof(*op->copies));
	node->op = *op;
	node->op.copies = node_copies(node);
	node->node.next = NULL;
	rdp_worker_push(t->worker, &node->node);
}

bool rdp_timeline_reached(rdp_timeline_t *t, uint64_t value)
{
	const size_t done = retro_atomic_load_acquire_size(&t->done);
	/* done >= value, modulo wrap */
	return (size_t)(done - (size_t)value) <= ((size_t)-1 >> 1);
}

typedef struct timeline_wait
{
	rdp_timeline_t *t;
	uint64_t value;
} timeline_wait_t;

static bool reached(void *ctx)
{
	timeline_wait_t *w = (timeline_wait_t *)ctx;
	return rdp_timeline_reached(w->t, w->value);
}

void rdp_timeline_wait(rdp_timeline_t *t, uint64_t value)
{
	timeline_wait_t w;
	w.t = t;
	w.value = value;
	rdp_worker_wait(t->worker, reached, &w);
}
