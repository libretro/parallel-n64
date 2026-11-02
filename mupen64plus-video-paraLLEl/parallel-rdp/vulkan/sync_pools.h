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

#ifndef GRANITE_SYNC_POOLS_H
#define GRANITE_SYNC_POOLS_H

#include <stddef.h>
#include <retro_common_api.h>
#include "volk.h"

RETRO_BEGIN_DECLS

/* Unsignalled fences and semaphores kept for reuse, so a submission does
 * not create and destroy one each time. Each pool belongs to the thread
 * that owns the device: no locking, and none needed. */
typedef struct granite_fence_pool
{
	VkDevice device;
	const struct VolkDeviceTable *table;
	VkFence *items;
	size_t count;
	size_t capacity;
} granite_fence_pool_t;

typedef struct granite_semaphore_pool
{
	VkDevice device;
	const struct VolkDeviceTable *table;
	VkSemaphore *items;
	size_t count;
	size_t capacity;
} granite_semaphore_pool_t;

void granite_fence_pool_init(granite_fence_pool_t *pool, VkDevice device,
		const struct VolkDeviceTable *table);
/* Destroys what it holds */
void granite_fence_pool_free(granite_fence_pool_t *pool);
/* An unsignalled fence; VK_NULL_HANDLE if one cannot be made */
VkFence granite_fence_pool_request(granite_fence_pool_t *pool);
/* fence: reset (unsignalled) */
void granite_fence_pool_recycle(granite_fence_pool_t *pool, VkFence fence);

void granite_semaphore_pool_init(granite_semaphore_pool_t *pool, VkDevice device,
		const struct VolkDeviceTable *table);
void granite_semaphore_pool_free(granite_semaphore_pool_t *pool);
/* An unsignalled binary semaphore; VK_NULL_HANDLE if one cannot be made */
VkSemaphore granite_semaphore_pool_request(granite_semaphore_pool_t *pool);
/* semaphore: unsignalled, with no wait pending on it; VK_NULL_HANDLE is
 * ignored */
void granite_semaphore_pool_recycle(granite_semaphore_pool_t *pool, VkSemaphore semaphore);

RETRO_END_DECLS

#endif
