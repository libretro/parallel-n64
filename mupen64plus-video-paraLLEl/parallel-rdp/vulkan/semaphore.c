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

#include <string.h>
#include "semaphore.h"

void granite_semaphore_pool_slots_init(granite_lf_pool_t *pool)
{
	granite_lf_pool_init(pool, sizeof(granite_semaphore_t), 8);
}

static granite_semaphore_t *semaphore_new(granite_lf_pool_t *pool, const granite_handle_owner_t *owner,
		VkSemaphore semaphore, uint64_t value, bool owned)
{
	granite_semaphore_t *object = (granite_semaphore_t *)granite_lf_pool_alloc(pool);

	if (!object)
		return NULL;
	memset(object, 0, sizeof(*object));
	granite_refs_init(&object->refs);
	object->owner = owner;
	object->pool = pool;
	object->semaphore = semaphore;
	object->value = value;
	object->signalled = true;
	object->owned = owned;
	return object;
}

granite_semaphore_t *granite_semaphore_new_binary(granite_lf_pool_t *pool, const granite_handle_owner_t *owner,
		VkSemaphore semaphore)
{
	return semaphore_new(pool, owner, semaphore, 0, true);
}

granite_semaphore_t *granite_semaphore_new_timeline(granite_lf_pool_t *pool, const granite_handle_owner_t *owner,
		VkSemaphore semaphore, uint64_t value)
{
	if (value == 0)
		return NULL;
	return semaphore_new(pool, owner, semaphore, value, false);
}

void granite_semaphore_wait(granite_semaphore_t *semaphore, VkPipelineStageFlags stages,
		granite_submit_composer_t *composer, granite_frame_garbage_t *garbage)
{
	VkSemaphore vk_semaphore = semaphore->semaphore;

	/* (waited for already: a semaphore is waited for once) */
	if (vk_semaphore == VK_NULL_HANDLE || !semaphore->signalled)
		return;

	if (!granite_semaphore_is_timeline(semaphore))
		granite_frame_garbage_push(garbage, GRANITE_GARBAGE_RECYCLE_SEMAPHORE, GRANITE_VK_TO_BITS(vk_semaphore));
	granite_submit_composer_add_wait(composer, vk_semaphore, stages, semaphore->value);

	semaphore->semaphore = VK_NULL_HANDLE;
	semaphore->signalled = false;
	semaphore->owned = false;
}

void granite_semaphore_dispose(granite_semaphore_t *semaphore)
{
	const granite_handle_owner_t *owner = semaphore->owner;

	if (semaphore->owned && semaphore->semaphore != VK_NULL_HANDLE)
		owner->destroy(owner->user, semaphore->signalled ? GRANITE_GARBAGE_SEMAPHORE : GRANITE_GARBAGE_RECYCLE_SEMAPHORE,
				GRANITE_VK_TO_BITS(semaphore->semaphore), semaphore->internal_sync);
	granite_lf_pool_release(semaphore->pool, semaphore);
}
