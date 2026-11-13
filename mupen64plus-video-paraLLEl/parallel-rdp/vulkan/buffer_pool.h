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

#ifndef GRANITE_BUFFER_POOL_H
#define GRANITE_BUFFER_POOL_H

#include <stddef.h>
#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>
#include "volk.h"

RETRO_BEGIN_DECLS

/* A buffer handed out piece by piece from its start: the vertex and
 * uniform data of the command buffer that holds it. Owned by one thing at
 * a time: a command buffer, a pool, or the list of a frame's used blocks. */
typedef struct granite_buffer_block
{
	/* the owner's buffer object, NULL for no block */
	void *handle;
	/* where it is written, NULL when it is not being written */
	uint8_t *mapped;
	/* how much of it is handed out */
	VkDeviceSize offset;
	VkDeviceSize alignment;
	VkDeviceSize size;
	/* the least a piece is bound with, see granite_buffer_block_allocate */
	VkDeviceSize spill_size;
} granite_buffer_block_t;

typedef struct granite_buffer_block_allocation
{
	/* NULL: the block had no room */
	uint8_t *host;
	VkDeviceSize offset;
	VkDeviceSize padded_size;
} granite_buffer_block_allocation_t;

/* The buffers behind the blocks, which are the owner's */
typedef struct granite_buffer_pool_ops
{
	/* A buffer of @size and @usage the host can write: its object, with
	 * where to write it in @mapped; NULL when there is none to be had */
	void *(*create)(void *user, VkDeviceSize size, VkBufferUsageFlags usage, uint8_t **mapped);
	/* The buffer is no longer needed (the GPU may still be using it) */
	void (*release)(void *user, void *handle);
	/* It is about to be written again from its start: where */
	uint8_t *(*map)(void *user, void *handle);
	/* What was written is to reach the device */
	void (*unmap)(void *user, void *handle);
} granite_buffer_pool_ops_t;

typedef struct granite_buffer_pool
{
	const granite_buffer_pool_ops_t *ops;
	void *user;
	VkDeviceSize block_size;
	VkDeviceSize alignment;
	VkDeviceSize spill_size;
	VkBufferUsageFlags usage;
	/* blocks of block_size kept for reuse: a stack of max_retained */
	granite_buffer_block_t *blocks;
	size_t count;
	size_t max_retained;
} granite_buffer_pool_t;

/* Blocks used in a frame, which go back to their pool when the frame's
 * turn comes again. Keeps its storage when cleared. */
typedef struct granite_buffer_block_list
{
	granite_buffer_block_t *blocks;
	size_t count;
	size_t capacity;
} granite_buffer_block_list_t;

/* @alignment: of each piece, a power of two. @spill_size: for uniform
 * data, bound with a fixed range that must not run past the block.
 * @max_retained: how many blocks are kept; their room is made here.
 * False when it could not be. */
bool granite_buffer_pool_init(granite_buffer_pool_t *pool, const granite_buffer_pool_ops_t *ops, void *user,
		VkDeviceSize block_size, VkDeviceSize alignment, VkBufferUsageFlags usage, VkDeviceSize spill_size,
		size_t max_retained);
/* Releases the blocks kept */
void granite_buffer_pool_reset(granite_buffer_pool_t *pool);
/* Resets and frees the pool's own memory */
void granite_buffer_pool_free(granite_buffer_pool_t *pool);

/* @size bytes of @block, aligned: where to write them, their offset and
 * the range to bind them with (at least the spill size, as far as the
 * block goes). host is NULL when the block, or no block, has no room. */
granite_buffer_block_allocation_t granite_buffer_block_allocate(granite_buffer_block_t *block, VkDeviceSize size);

/* A block with room for @minimum_size in @block: one kept, or a new one
 * (always new for more than the block size). No block (handle NULL) when
 * none could be made. */
void granite_buffer_pool_request(granite_buffer_pool_t *pool, VkDeviceSize minimum_size,
		granite_buffer_block_t *block);
/* Gives @block, of the block size, back: kept, or released when enough
 * are kept. @block is no block afterwards. */
void granite_buffer_pool_recycle(granite_buffer_pool_t *pool, granite_buffer_block_t *block);

/* The holder of @block is done with it and wants one with room for
 * @size, or none (@size 0). What was written is sent to the device. An
 * untouched block goes straight back to the pool; a used one goes to
 * @used until its frame comes round, unless it is of an odd size, which
 * is released. */
void granite_buffer_pool_exchange(granite_buffer_pool_t *pool, granite_buffer_block_t *block, VkDeviceSize size,
		granite_buffer_block_list_t *used);

/* Every block of @list goes back to @pool; the list is empty afterwards */
void granite_buffer_block_list_recycle(granite_buffer_block_list_t *list, granite_buffer_pool_t *pool);
/* Every block of @list is released; the list is empty afterwards */
void granite_buffer_block_list_release(granite_buffer_block_list_t *list, granite_buffer_pool_t *pool);
/* Frees the list's own memory; it must be empty */
void granite_buffer_block_list_free(granite_buffer_block_list_t *list);

RETRO_END_DECLS

#endif
