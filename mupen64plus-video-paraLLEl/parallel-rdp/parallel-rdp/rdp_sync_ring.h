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


#ifndef RDP_SYNC_RING_H
#define RDP_SYNC_RING_H

/* The renderer's per-flush stream buffers, used in turn: which slot the
 * flush being queued writes, which slots recorded work reads that is
 * not submitted yet, and the fence of each slot's last submission, so a
 * slot is written again only once the GPU is done with it. C89. */

#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>
#include "fence.h"

RETRO_BEGIN_DECLS

#define RDP_SYNC_RING_SIZE 32u

typedef struct rdp_sync_ring
{
	/* the last submission reading each slot; NULL when none is pending */
	granite_fence_t *fences[RDP_SYNC_RING_SIZE];
	/* a bit a slot: recorded work reads it, not submitted yet */
	uint32_t unsubmitted;
	/* the slot the flush being queued writes */
	unsigned current;
} rdp_sync_ring_t;

/* Slot 0, nothing pending */
void rdp_sync_ring_init(rdp_sync_ring_t *r);

/* The fences let go */
void rdp_sync_ring_free(rdp_sync_ring_t *r);

/* The current slot is read by recorded work */
void rdp_sync_ring_use(rdp_sync_ring_t *r);

/* The next slot becomes current; returns it */
unsigned rdp_sync_ring_advance(rdp_sync_ring_t *r);

/* Whether recorded work reading @slot is still to be submitted: submit
 * it before waiting for the slot */
bool rdp_sync_ring_unsubmitted(const rdp_sync_ring_t *r, unsigned slot);

/* The recorded work went to the GPU with @fence (NULL: none to wait
 * for): every slot it reads is done once @fence is signalled */
void rdp_sync_ring_submitted(rdp_sync_ring_t *r, granite_fence_t *fence);

/* Returns once the GPU is done with @slot */
void rdp_sync_ring_wait(rdp_sync_ring_t *r, unsigned slot);

RETRO_END_DECLS

#endif
