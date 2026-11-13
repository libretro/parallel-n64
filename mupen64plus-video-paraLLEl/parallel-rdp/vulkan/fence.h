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

#ifndef GRANITE_FENCE_H
#define GRANITE_FENCE_H

#include "handle.h"
#include "fence_wait.h"
#include "pool.h"

RETRO_BEGIN_DECLS

/* What waiting on a fence needs of the device */
typedef struct granite_fence_env
{
	VkDevice device;
	const struct VolkDeviceTable *table;
	/* where the threads that find another already waiting park; or NULL */
	retro_eventcount_t *wait_ec;
} granite_fence_env_t;

/* Something to wait for a submission on, counted: a VkFence, or a value
 * of a timeline semaphore. Any number of threads may wait on it. */
typedef struct granite_fence
{
	retro_atomic_int_t refs;
	/* its last reference goes on the thread that owns the device */
	bool internal_sync;
	const granite_handle_owner_t *owner;
	granite_lf_pool_t *pool;
	granite_fence_env_t env;
	/* VK_NULL_HANDLE when it is a timeline value */
	VkFence fence;
	VkSemaphore timeline;
	/* 0 when it is a VkFence */
	uint64_t value;
	/* fence_wait.h */
	retro_atomic_int_t wait_state;
} granite_fence_t;

void granite_fence_pool_slots_init(granite_lf_pool_t *pool);

/* An object with one reference for @fence, or, when @value is not 0, for
 * that value of @timeline. NULL when there is no room for one (the fence
 * is then still the caller's). */
granite_fence_t *granite_fence_new(granite_lf_pool_t *pool, const granite_handle_owner_t *owner,
		const granite_fence_env_t *env, VkFence fence, VkSemaphore timeline, uint64_t value);

/* Returns once it is signalled. False when the wait itself failed. */
bool granite_fence_wait_for(granite_fence_t *fence);

/* The last reference gone: a VkFence goes back to the owner, with
 * whether a wait saw it signalled */
void granite_fence_dispose(granite_fence_t *fence);

static INLINE void granite_fence_ref(granite_fence_t *fence)
{
	granite_refs_add(&fence->refs);
}

static INLINE void granite_fence_unref(granite_fence_t *fence)
{
	if (granite_refs_drop(&fence->refs))
		granite_fence_dispose(fence);
}

RETRO_END_DECLS

#endif
