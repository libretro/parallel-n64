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
#include <string.h>
#include "buffer_pool.h"

static void block_release(granite_buffer_pool_t *pool, granite_buffer_block_t *block)
{
	if (block->handle)
		pool->ops->release(pool->user, block->handle);
	memset(block, 0, sizeof(*block));
}

bool granite_buffer_pool_init(granite_buffer_pool_t *pool, const granite_buffer_pool_ops_t *ops, void *user,
		VkDeviceSize block_size, VkDeviceSize alignment, VkBufferUsageFlags usage, VkDeviceSize spill_size,
		size_t max_retained)
{
	memset(pool, 0, sizeof(*pool));
	pool->ops = ops;
	pool->user = user;
	pool->block_size = block_size;
	pool->alignment = alignment;
	pool->usage = usage;
	pool->spill_size = spill_size;
	if (max_retained)
	{
		pool->blocks = (granite_buffer_block_t *)calloc(max_retained, sizeof(*pool->blocks));
		if (!pool->blocks)
			return false;
	}
	pool->max_retained = max_retained;
	return true;
}

void granite_buffer_pool_reset(granite_buffer_pool_t *pool)
{
	while (pool->count)
		block_release(pool, &pool->blocks[--pool->count]);
}

void granite_buffer_pool_free(granite_buffer_pool_t *pool)
{
	granite_buffer_pool_reset(pool);
	free(pool->blocks);
	pool->blocks = NULL;
	pool->max_retained = 0;
}

granite_buffer_block_allocation_t granite_buffer_block_allocate(granite_buffer_block_t *block, VkDeviceSize size)
{
	granite_buffer_block_allocation_t ret;
	VkDeviceSize aligned_offset;

	ret.host = NULL;
	ret.offset = 0;
	ret.padded_size = 0;
	if (!block->mapped)
		return ret;

	aligned_offset = (block->offset + block->alignment - 1) & ~(block->alignment - 1);
	if (aligned_offset > block->size || size > block->size - aligned_offset)
		return ret;

	ret.host = block->mapped + aligned_offset;
	ret.offset = aligned_offset;
	ret.padded_size = size > block->spill_size ? size : block->spill_size;
	if (ret.padded_size > block->size - aligned_offset)
		ret.padded_size = block->size - aligned_offset;
	block->offset = aligned_offset + size;
	return ret;
}

void granite_buffer_pool_request(granite_buffer_pool_t *pool, VkDeviceSize minimum_size,
		granite_buffer_block_t *block)
{
	memset(block, 0, sizeof(*block));

	if (minimum_size <= pool->block_size && pool->count)
	{
		*block = pool->blocks[--pool->count];
		block->mapped = pool->ops->map(pool->user, block->handle);
		block->offset = 0;
		if (block->mapped)
			return;
		/* cannot be written any more: no use keeping it */
		block_release(pool, block);
	}

	block->size = minimum_size > pool->block_size ? minimum_size : pool->block_size;
	block->handle = pool->ops->create(pool->user, block->size, pool->usage, &block->mapped);
	if (!block->handle || !block->mapped)
	{
		block_release(pool, block);
		return;
	}
	block->alignment = pool->alignment;
	block->spill_size = pool->spill_size;
}

void granite_buffer_pool_recycle(granite_buffer_pool_t *pool, granite_buffer_block_t *block)
{
	if (block->handle && block->size == pool->block_size && pool->count < pool->max_retained)
	{
		pool->blocks[pool->count] = *block;
		pool->blocks[pool->count].mapped = NULL;
		pool->count++;
		memset(block, 0, sizeof(*block));
	}
	else
		block_release(pool, block);
}

static bool list_push(granite_buffer_block_list_t *list, const granite_buffer_block_t *block)
{
	if (list->count == list->capacity)
	{
		size_t capacity = list->capacity ? list->capacity * 2 : 16;
		granite_buffer_block_t *blocks = (granite_buffer_block_t *)realloc(list->blocks,
				capacity * sizeof(*blocks));
		if (!blocks)
			return false;
		list->blocks = blocks;
		list->capacity = capacity;
	}
	list->blocks[list->count++] = *block;
	return true;
}

void granite_buffer_pool_exchange(granite_buffer_pool_t *pool, granite_buffer_block_t *block, VkDeviceSize size,
		granite_buffer_block_list_t *used)
{
	if (block->handle)
	{
		if (block->mapped)
			pool->ops->unmap(pool->user, block->handle);
		block->mapped = NULL;

		if (block->offset == 0)
			granite_buffer_pool_recycle(pool, block);
		else if (block->size == pool->block_size && list_push(used, block))
			memset(block, 0, sizeof(*block));
		else
			/* (released now, destroyed once the GPU is done with it) */
			block_release(pool, block);
	}

	if (size)
		granite_buffer_pool_request(pool, size, block);
	else
		memset(block, 0, sizeof(*block));
}

void granite_buffer_block_list_recycle(granite_buffer_block_list_t *list, granite_buffer_pool_t *pool)
{
	size_t i;

	for (i = 0; i < list->count; i++)
		granite_buffer_pool_recycle(pool, &list->blocks[i]);
	list->count = 0;
}

void granite_buffer_block_list_release(granite_buffer_block_list_t *list, granite_buffer_pool_t *pool)
{
	size_t i;

	for (i = 0; i < list->count; i++)
		block_release(pool, &list->blocks[i]);
	list->count = 0;
}

void granite_buffer_block_list_free(granite_buffer_block_list_t *list)
{
	free(list->blocks);
	memset(list, 0, sizeof(*list));
}
