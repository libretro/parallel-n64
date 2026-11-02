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
#include <boolean.h>
#include "sync_pools.h"
#include "logging.h"

/* items[] (of item_size bytes) with room for one more, or NULL when out
 * of memory; the caller then destroys the handle instead of keeping it */
static void *sync_pool_room(void *items, size_t *capacity, size_t count, size_t item_size)
{
	void *grown;
	size_t cap;
	if (count < *capacity)
		return items;
	cap = *capacity ? *capacity * 2 : 16;
	grown = realloc(items, cap * item_size);
	if (grown)
		*capacity = cap;
	return grown;
}

void granite_fence_pool_init(granite_fence_pool_t *pool, VkDevice device,
		const struct VolkDeviceTable *table)
{
	pool->device = device;
	pool->table = table;
	pool->items = NULL;
	pool->count = 0;
	pool->capacity = 0;
}

void granite_fence_pool_free(granite_fence_pool_t *pool)
{
	size_t i;
	for (i = 0; i < pool->count; i++)
		pool->table->vkDestroyFence(pool->device, pool->items[i], NULL);
	free(pool->items);
	pool->items = NULL;
	pool->count = 0;
	pool->capacity = 0;
}

VkFence granite_fence_pool_request(granite_fence_pool_t *pool)
{
	VkFenceCreateInfo info;
	VkFence fence = VK_NULL_HANDLE;
	if (pool->count)
		return pool->items[--pool->count];
	info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	info.pNext = NULL;
	info.flags = 0;
	if (pool->table->vkCreateFence(pool->device, &info, NULL, &fence) != VK_SUCCESS)
	{
		LOGE("Failed to create fence.\n");
		return VK_NULL_HANDLE;
	}
	return fence;
}

void granite_fence_pool_recycle(granite_fence_pool_t *pool, VkFence fence)
{
	VkFence *items;
	if (fence == VK_NULL_HANDLE)
		return;
	items = (VkFence *)sync_pool_room(pool->items, &pool->capacity, pool->count, sizeof(*items));
	if (!items)
	{
		pool->table->vkDestroyFence(pool->device, fence, NULL);
		return;
	}
	pool->items = items;
	items[pool->count++] = fence;
}

void granite_semaphore_pool_init(granite_semaphore_pool_t *pool, VkDevice device,
		const struct VolkDeviceTable *table)
{
	pool->device = device;
	pool->table = table;
	pool->items = NULL;
	pool->count = 0;
	pool->capacity = 0;
}

void granite_semaphore_pool_free(granite_semaphore_pool_t *pool)
{
	size_t i;
	for (i = 0; i < pool->count; i++)
		pool->table->vkDestroySemaphore(pool->device, pool->items[i], NULL);
	free(pool->items);
	pool->items = NULL;
	pool->count = 0;
	pool->capacity = 0;
}

VkSemaphore granite_semaphore_pool_request(granite_semaphore_pool_t *pool)
{
	VkSemaphoreCreateInfo info;
	VkSemaphore semaphore = VK_NULL_HANDLE;
	if (pool->count)
		return pool->items[--pool->count];
	info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	info.pNext = NULL;
	info.flags = 0;
	if (pool->table->vkCreateSemaphore(pool->device, &info, NULL, &semaphore) != VK_SUCCESS)
	{
		LOGE("Failed to create semaphore.\n");
		return VK_NULL_HANDLE;
	}
	return semaphore;
}

void granite_semaphore_pool_recycle(granite_semaphore_pool_t *pool, VkSemaphore semaphore)
{
	VkSemaphore *items;
	if (semaphore == VK_NULL_HANDLE)
		return;
	items = (VkSemaphore *)sync_pool_room(pool->items, &pool->capacity, pool->count, sizeof(*items));
	if (!items)
	{
		pool->table->vkDestroySemaphore(pool->device, semaphore, NULL);
		return;
	}
	pool->items = items;
	items[pool->count++] = semaphore;
}
