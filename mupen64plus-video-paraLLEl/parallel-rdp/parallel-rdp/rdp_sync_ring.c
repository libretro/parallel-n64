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


#include <string.h>
#include "rdp_sync_ring.h"

static void set_fence(rdp_sync_ring_t *r, unsigned slot, granite_fence_t *fence)
{
	if (fence)
		granite_fence_ref(fence);
	if (r->fences[slot])
		granite_fence_unref(r->fences[slot]);
	r->fences[slot] = fence;
}

void rdp_sync_ring_init(rdp_sync_ring_t *r)
{
	memset(r, 0, sizeof(*r));
}

void rdp_sync_ring_free(rdp_sync_ring_t *r)
{
	unsigned i;
	for (i = 0; i < RDP_SYNC_RING_SIZE; i++)
		set_fence(r, i, NULL);
	r->unsubmitted = 0;
}

void rdp_sync_ring_use(rdp_sync_ring_t *r)
{
	r->unsubmitted |= 1u << r->current;
}

unsigned rdp_sync_ring_advance(rdp_sync_ring_t *r)
{
	r->current = (r->current + 1) % RDP_SYNC_RING_SIZE;
	return r->current;
}

bool rdp_sync_ring_unsubmitted(const rdp_sync_ring_t *r, unsigned slot)
{
	return (r->unsubmitted & (1u << slot)) != 0;
}

void rdp_sync_ring_submitted(rdp_sync_ring_t *r, granite_fence_t *fence)
{
	unsigned slot;
	for (slot = 0; slot < RDP_SYNC_RING_SIZE; slot++)
		if (r->unsubmitted & (1u << slot))
			set_fence(r, slot, fence);
	r->unsubmitted = 0;
}

void rdp_sync_ring_wait(rdp_sync_ring_t *r, unsigned slot)
{
	if (!r->fences[slot])
		return;
	granite_fence_wait_for(r->fences[slot]);
	set_fence(r, slot, NULL);
}
