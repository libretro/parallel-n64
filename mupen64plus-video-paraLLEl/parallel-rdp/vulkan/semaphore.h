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

#ifndef GRANITE_SEMAPHORE_H
#define GRANITE_SEMAPHORE_H

#include "handle.h"
#include "submit_batch.h"
#include "pool.h"

RETRO_BEGIN_DECLS

/* What one submission signals for another to wait on, counted. Used
 * once: signalled by the submission it came from, then waited for by
 * one other. Either a binary semaphore of its own, or, where the device
 * has timeline semaphores, a value of the queue's. */
typedef struct granite_semaphore
{
	retro_atomic_int_t refs;
	/* its last reference goes on the thread that owns the device */
	bool internal_sync;
	const granite_handle_owner_t *owner;
	granite_lf_pool_t *pool;
	/* VK_NULL_HANDLE once it has been waited for */
	VkSemaphore semaphore;
	/* the value to wait for; 0 for a binary semaphore */
	uint64_t value;
	/* a signal is pending or done, and nothing has waited for it */
	bool signalled;
	/* the VkSemaphore is this object's to give back: a binary one that
	 * has not been waited for */
	bool owned;
} granite_semaphore_t;

void granite_semaphore_pool_slots_init(granite_lf_pool_t *pool);

/* An object with one reference for the binary semaphore @semaphore,
 * which a submission is about to signal. NULL when there is no room
 * (the semaphore is then still the caller's). */
granite_semaphore_t *granite_semaphore_new_binary(granite_lf_pool_t *pool, const granite_handle_owner_t *owner,
		VkSemaphore semaphore);
/* An object for @value (not 0) of the timeline semaphore @semaphore,
 * which stays the queue's */
granite_semaphore_t *granite_semaphore_new_timeline(granite_lf_pool_t *pool, const granite_handle_owner_t *owner,
		VkSemaphore semaphore, uint64_t value);

static INLINE bool granite_semaphore_is_timeline(const granite_semaphore_t *semaphore)
{
	return semaphore->value != 0;
}

/* The next submission composed in @composer waits for @semaphore at
 * @stages. A binary semaphore goes to @garbage, to be used again once
 * the frame is done. The object is spent: it holds no semaphore. */
void granite_semaphore_wait(granite_semaphore_t *semaphore, VkPipelineStageFlags stages,
		granite_submit_composer_t *composer, granite_frame_garbage_t *garbage);

/* The last reference gone: a binary semaphore nothing waited for goes
 * back to the owner to be destroyed (it may yet be signalled) */
void granite_semaphore_dispose(granite_semaphore_t *semaphore);

static INLINE void granite_semaphore_ref(granite_semaphore_t *semaphore)
{
	granite_refs_add(&semaphore->refs);
}

static INLINE void granite_semaphore_unref(granite_semaphore_t *semaphore)
{
	if (granite_refs_drop(&semaphore->refs))
		granite_semaphore_dispose(semaphore);
}

RETRO_END_DECLS

#endif
