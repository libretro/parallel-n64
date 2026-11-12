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
#include "command_pool.h"
#include "logging.h"

void granite_command_pool_init(granite_command_pool_t *pool, VkDevice device,
		const struct VolkDeviceTable *table, uint32_t queue_family_index)
{
	VkCommandPoolCreateInfo info;
	pool->device = device;
	pool->table = table;
	pool->pool = VK_NULL_HANDLE;
	pool->buffers = NULL;
	pool->count = 0;
	pool->capacity = 0;
	pool->index = 0;
	if (queue_family_index == VK_QUEUE_FAMILY_IGNORED)
		return;
	info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	info.pNext = NULL;
	info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	info.queueFamilyIndex = queue_family_index;
	if (table->vkCreateCommandPool(device, &info, NULL, &pool->pool) != VK_SUCCESS)
	{
		LOGE("Failed to create command pool.\n");
		pool->pool = VK_NULL_HANDLE;
	}
}

void granite_command_pool_free(granite_command_pool_t *pool)
{
	if (pool->pool != VK_NULL_HANDLE)
	{
		if (pool->count)
			pool->table->vkFreeCommandBuffers(pool->device, pool->pool, pool->count, pool->buffers);
		pool->table->vkDestroyCommandPool(pool->device, pool->pool, NULL);
	}
	free(pool->buffers);
	pool->pool = VK_NULL_HANDLE;
	pool->buffers = NULL;
	pool->count = 0;
	pool->capacity = 0;
	pool->index = 0;
}

void granite_command_pool_begin(granite_command_pool_t *pool)
{
	if (pool->pool == VK_NULL_HANDLE)
		return;
	if (pool->index)
		pool->table->vkResetCommandPool(pool->device, pool->pool, 0);
	pool->index = 0;
}

void granite_command_pool_trim(granite_command_pool_t *pool)
{
	if (pool->pool == VK_NULL_HANDLE)
		return;
	pool->table->vkResetCommandPool(pool->device, pool->pool, VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);
	if (pool->count)
		pool->table->vkFreeCommandBuffers(pool->device, pool->pool, pool->count, pool->buffers);
	/* (the array stays: it is filled again without allocating) */
	pool->count = 0;
	pool->index = 0;
	pool->table->vkTrimCommandPool(pool->device, pool->pool, 0);
}

VkCommandBuffer granite_command_pool_request(granite_command_pool_t *pool)
{
	VkCommandBufferAllocateInfo info;
	VkCommandBuffer cmd = VK_NULL_HANDLE;

	if (pool->pool == VK_NULL_HANDLE)
		return VK_NULL_HANDLE;
	if (pool->index < pool->count)
		return pool->buffers[pool->index++];

	if (pool->count == pool->capacity)
	{
		unsigned capacity = pool->capacity ? pool->capacity * 2 : 8;
		VkCommandBuffer *grown = (VkCommandBuffer *)realloc(pool->buffers, capacity * sizeof(*grown));
		if (!grown)
			return VK_NULL_HANDLE;
		pool->buffers = grown;
		pool->capacity = capacity;
	}
	info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	info.pNext = NULL;
	info.commandPool = pool->pool;
	info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	info.commandBufferCount = 1;
	if (pool->table->vkAllocateCommandBuffers(pool->device, &info, &cmd) != VK_SUCCESS)
	{
		LOGE("Failed to allocate command buffer.\n");
		return VK_NULL_HANDLE;
	}
	pool->buffers[pool->count++] = cmd;
	pool->index++;
	return cmd;
}
