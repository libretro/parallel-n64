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
#include "buffer.h"
#include "cookie.h"

void granite_buffer_pools_init(granite_buffer_pools_t *pools)
{
	granite_lf_pool_init(&pools->buffers, sizeof(granite_buffer_t), 8);
	granite_lf_pool_init(&pools->views, sizeof(granite_buffer_view_t), 8);
}

void granite_buffer_pools_free(granite_buffer_pools_t *pools)
{
	granite_lf_pool_clear(&pools->buffers);
	granite_lf_pool_clear(&pools->views);
}

granite_buffer_t *granite_buffer_new(granite_buffer_pools_t *pools, const granite_handle_owner_t *owner,
		VkBuffer buffer, const granite_allocation_t *alloc, const granite_buffer_info_t *info)
{
	granite_buffer_t *object = (granite_buffer_t *)granite_lf_pool_alloc(&pools->buffers);

	if (!object)
		return NULL;
	memset(object, 0, sizeof(*object));
	granite_refs_init(&object->refs);
	object->cookie = granite_cookie_next();
	object->owner = owner;
	object->pool = &pools->buffers;
	object->buffer = buffer;
	object->alloc = *alloc;
	object->info = *info;
	return object;
}

void granite_buffer_dispose(granite_buffer_t *buffer)
{
	const granite_handle_owner_t *owner = buffer->owner;

	owner->destroy(owner->user, GRANITE_GARBAGE_BUFFER, GRANITE_VK_TO_BITS(buffer->buffer), buffer->internal_sync);
	owner->free_memory(owner->user, &buffer->alloc, buffer->internal_sync);
	granite_lf_pool_release(buffer->pool, buffer);
}

granite_buffer_view_t *granite_buffer_view_new(granite_buffer_pools_t *pools, const granite_handle_owner_t *owner,
		VkBufferView view, const granite_buffer_view_info_t *info)
{
	granite_buffer_view_t *object = (granite_buffer_view_t *)granite_lf_pool_alloc(&pools->views);

	if (!object)
		return NULL;
	memset(object, 0, sizeof(*object));
	granite_refs_init(&object->refs);
	object->cookie = granite_cookie_next();
	object->owner = owner;
	object->pool = &pools->views;
	object->view = view;
	object->info = *info;
	return object;
}

void granite_buffer_view_dispose(granite_buffer_view_t *view)
{
	const granite_handle_owner_t *owner = view->owner;

	if (view->view != VK_NULL_HANDLE)
		owner->destroy(owner->user, GRANITE_GARBAGE_BUFFER_VIEW, GRANITE_VK_TO_BITS(view->view), view->internal_sync);
	granite_lf_pool_release(view->pool, view);
}
