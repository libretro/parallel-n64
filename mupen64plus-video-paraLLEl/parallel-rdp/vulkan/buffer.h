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

#ifndef GRANITE_BUFFER_H
#define GRANITE_BUFFER_H

#include "handle.h"
#include "buffer_create.h"
#include "pool.h"

RETRO_BEGIN_DECLS

/* Its memory starts out zeroed */
#define GRANITE_BUFFER_MISC_ZERO_INITIALIZE_BIT 1u

typedef struct granite_buffer_info
{
	granite_buffer_domain_t domain;
	VkDeviceSize size;
	VkBufferUsageFlags usage;
	uint32_t misc;
} granite_buffer_info_t;

/* A buffer and its memory, counted. Made by granite_buffer_new and gone
 * with its last reference. */
typedef struct granite_buffer
{
	retro_atomic_int_t refs;
	/* its last reference goes on the thread that owns the device */
	bool internal_sync;
	/* what tells it from every other object, for caches */
	uint64_t cookie;
	const granite_handle_owner_t *owner;
	granite_lf_pool_t *pool;
	VkBuffer buffer;
	/* base is VK_NULL_HANDLE for memory that is not the allocator's */
	granite_allocation_t alloc;
	granite_buffer_info_t info;
} granite_buffer_t;

typedef struct granite_buffer_view_info
{
	const granite_buffer_t *buffer;
	VkFormat format;
	VkDeviceSize offset;
	VkDeviceSize range;
} granite_buffer_view_info_t;

/* A view of a buffer's texels. It does not keep the buffer alive. */
typedef struct granite_buffer_view
{
	retro_atomic_int_t refs;
	bool internal_sync;
	uint64_t cookie;
	const granite_handle_owner_t *owner;
	granite_lf_pool_t *pool;
	VkBufferView view;
	granite_buffer_view_info_t info;
} granite_buffer_view_t;

/* The slots buffers and views are made in */
typedef struct granite_buffer_pools
{
	granite_lf_pool_t buffers;
	granite_lf_pool_t views;
} granite_buffer_pools_t;

void granite_buffer_pools_init(granite_buffer_pools_t *pools);
/* Every buffer and view must be gone */
void granite_buffer_pools_free(granite_buffer_pools_t *pools);

/* An object for @buffer and its memory, with one reference; NULL when
 * there is no room for one (the buffer is then still the caller's) */
granite_buffer_t *granite_buffer_new(granite_buffer_pools_t *pools, const granite_handle_owner_t *owner,
		VkBuffer buffer, const granite_allocation_t *alloc, const granite_buffer_info_t *info);
granite_buffer_view_t *granite_buffer_view_new(granite_buffer_pools_t *pools, const granite_handle_owner_t *owner,
		VkBufferView view, const granite_buffer_view_info_t *info);

/* The last reference gone: hands the buffer, then its memory, to the
 * owner and gives the object's slot back */
void granite_buffer_dispose(granite_buffer_t *buffer);
void granite_buffer_view_dispose(granite_buffer_view_t *view);

static INLINE void granite_buffer_ref(granite_buffer_t *buffer)
{
	granite_refs_add(&buffer->refs);
}

static INLINE void granite_buffer_unref(granite_buffer_t *buffer)
{
	if (granite_refs_drop(&buffer->refs))
		granite_buffer_dispose(buffer);
}

static INLINE void granite_buffer_view_ref(granite_buffer_view_t *view)
{
	granite_refs_add(&view->refs);
}

static INLINE void granite_buffer_view_unref(granite_buffer_view_t *view)
{
	if (granite_refs_drop(&view->refs))
		granite_buffer_view_dispose(view);
}

RETRO_END_DECLS

#endif
