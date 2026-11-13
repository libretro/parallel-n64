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
#include "image.h"
#include "cookie.h"

void granite_image_pools_init(granite_image_pools_t *pools)
{
	granite_lf_pool_init(&pools->images, sizeof(granite_image_t), 8);
	granite_lf_pool_init(&pools->views, sizeof(granite_image_view_t), 8);
}

void granite_image_pools_free(granite_image_pools_t *pools)
{
	granite_lf_pool_clear(&pools->images);
	granite_lf_pool_clear(&pools->views);
}

granite_image_view_t *granite_image_view_new(granite_image_pools_t *pools, const granite_handle_owner_t *owner,
		const granite_image_views_t *views, const granite_image_view_info_t *info)
{
	granite_image_view_t *object = (granite_image_view_t *)granite_lf_pool_alloc(&pools->views);

	if (!object)
		return NULL;
	memset(object, 0, sizeof(*object));
	granite_refs_init(&object->refs);
	object->cookie = granite_cookie_next();
	object->owner = owner;
	object->pool = &pools->views;
	object->views = *views;
	object->info = *info;
	return object;
}

void granite_image_view_dispose(granite_image_view_t *view)
{
	const granite_handle_owner_t *owner = view->owner;
	const granite_image_views_t *views = &view->views;
	bool sync = view->internal_sync;
	unsigned i;

	if (views->view != VK_NULL_HANDLE)
		owner->destroy(owner->user, GRANITE_GARBAGE_IMAGE_VIEW, GRANITE_VK_TO_BITS(views->view), sync);
	if (views->depth != VK_NULL_HANDLE)
		owner->destroy(owner->user, GRANITE_GARBAGE_IMAGE_VIEW, GRANITE_VK_TO_BITS(views->depth), sync);
	if (views->stencil != VK_NULL_HANDLE)
		owner->destroy(owner->user, GRANITE_GARBAGE_IMAGE_VIEW, GRANITE_VK_TO_BITS(views->stencil), sync);
	for (i = 0; i < views->num_layer_views; i++)
		owner->destroy(owner->user, GRANITE_GARBAGE_IMAGE_VIEW, GRANITE_VK_TO_BITS(views->layer_views[i]), sync);
	granite_lf_pool_release(view->pool, view);
}

granite_image_t *granite_image_new(granite_image_pools_t *pools, const granite_handle_owner_t *owner,
		VkImage image, const granite_allocation_t *alloc, const granite_image_info_t *info,
		const granite_image_views_t *views, VkImageViewType view_type)
{
	granite_image_t *object = (granite_image_t *)granite_lf_pool_alloc(&pools->images);

	if (!object)
		return NULL;
	memset(object, 0, sizeof(*object));
	granite_refs_init(&object->refs);
	object->cookie = granite_cookie_next();
	object->owner = owner;
	object->pool = &pools->images;
	object->image = image;
	object->alloc = *alloc;
	object->info = *info;

	if (views && views->view != VK_NULL_HANDLE)
	{
		granite_image_view_info_t view_info;

		memset(&view_info, 0, sizeof(view_info));
		view_info.image = object;
		view_info.format = info->format;
		view_info.levels = info->levels;
		view_info.layers = info->layers;
		view_info.view_type = view_type;
		object->view = granite_image_view_new(pools, owner, views, &view_info);
		if (!object->view)
		{
			granite_lf_pool_release(&pools->images, object);
			return NULL;
		}
	}
	return object;
}

void granite_image_dispose(granite_image_t *image)
{
	const granite_handle_owner_t *owner = image->owner;

	owner->destroy(owner->user, GRANITE_GARBAGE_IMAGE, GRANITE_VK_TO_BITS(image->image), image->internal_sync);
	if (image->alloc.base != VK_NULL_HANDLE)
		owner->free_memory(owner->user, &image->alloc, image->internal_sync);
	if (image->view)
		granite_image_view_unref(image->view);
	granite_lf_pool_release(image->pool, image);
}
