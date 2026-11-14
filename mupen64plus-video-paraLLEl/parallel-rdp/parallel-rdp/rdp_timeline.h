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


#ifndef RDP_TIMELINE_H
#define RDP_TIMELINE_H

/* The timeline worker: in submission order, it waits for each
 * submission's fence, copies what the GPU wrote back to the host's
 * RDRAM, and publishes the timeline values the emulation waits for.
 * C89. */

#include <stdint.h>
#include <stddef.h>
#include <boolean.h>
#include <retro_common_api.h>
#include <retro_atomic.h>
#include <queues/mpsc_stack.h>
#include "device.h"
#include "rdram_coherency.h"
#include "rdp_worker.h"

RETRO_BEGIN_DECLS

/* One piece of the worker's work. Each part is optional. */
typedef struct rdp_timeline_op
{
	/* waited for first; the op's own reference */
	granite_fence_t *fence;
	/* dropped by one after the wait: submissions in flight */
	retro_atomic_int_t *unlock_cookie;
	/* @num_copies of @copies (the producer's storage, copied when queued)
	 * from @src to the host's RDRAM at @dst */
	uint8_t *dst;
	const granite_buffer_t *src;
	const rdp_coherency_copy_t *copies;
	unsigned num_copies;
	/* published once the rest is done; 0: none */
	uint64_t timeline_value;
} rdp_timeline_op_t;

typedef struct rdp_timeline
{
	granite_device_t *device;
	rdp_worker_t *worker;
	/* the last timeline value passed: free running, compared by
	 * difference, so it may wrap on 32-bit hosts */
	retro_atomic_size_t done;
	/* the worker's nodes handed back, and those the queuing thread took
	 * from them */
	mpsc_stack_t returned;
	mpsc_stack_node_t *spare;
} rdp_timeline_t;

/* Starts the worker. False when it does not start. */
bool rdp_timeline_init(rdp_timeline_t *t, granite_device_t *device);

/* Finishes what is queued, stops the worker and frees its nodes */
void rdp_timeline_free(rdp_timeline_t *t);

/* Queues @op; the worker takes over its fence reference. A node with room
 * for its copies is one handed back, or a new one the first time that
 * many copies are queued at once. One thread only. */
void rdp_timeline_enqueue(rdp_timeline_t *t, const rdp_timeline_op_t *op);

/* Whether the worker has passed timeline value @value */
bool rdp_timeline_reached(rdp_timeline_t *t, uint64_t value);

/* Returns once it has */
void rdp_timeline_wait(rdp_timeline_t *t, uint64_t value);

RETRO_END_DECLS

#endif
