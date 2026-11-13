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

#ifndef GRANITE_IMAGE_H
#define GRANITE_IMAGE_H

#include "handle.h"
#include "image_create.h"
#include "pool.h"

RETRO_BEGIN_DECLS

struct granite_image;

/* What an image was made as */
/* granite_image_info_t.misc */
#define GRANITE_IMAGE_MISC_FORCE_ARRAY_BIT (1u << 1)
#define GRANITE_IMAGE_MISC_MUTABLE_SRGB_BIT (1u << 2)
#define GRANITE_IMAGE_MISC_CONCURRENT_QUEUE_GRAPHICS_BIT (1u << 3)
#define GRANITE_IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_COMPUTE_BIT (1u << 4)

typedef struct granite_image_info
{
	uint32_t width;
	uint32_t height;
	uint32_t depth;
	uint32_t levels;
	VkFormat format;
	VkImageType type;
	uint32_t layers;
	VkImageUsageFlags usage;
	VkSampleCountFlagBits samples;
	VkImageCreateFlags flags;
	/* the owner's own flags */
	uint32_t misc;
	/* the layout it is in when handed out */
	VkImageLayout initial_layout;
} granite_image_info_t;

typedef struct granite_image_view_info
{
	const struct granite_image *image;
	VkFormat format;
	uint32_t base_level;
	uint32_t levels;
	uint32_t base_layer;
	uint32_t layers;
	VkImageViewType view_type;
} granite_image_view_info_t;

/* A view of an image with the views that go with it (image_create.h),
 * counted. It does not keep its image alive. */
typedef struct granite_image_view
{
	retro_atomic_int_t refs;
	/* its last reference goes on the thread that owns the device */
	bool internal_sync;
	uint64_t cookie;
	const granite_handle_owner_t *owner;
	granite_lf_pool_t *pool;
	granite_image_views_t views;
	granite_image_view_info_t info;
} granite_image_view_t;

/* An image, its memory and its own view, counted */
typedef struct granite_image
{
	retro_atomic_int_t refs;
	bool internal_sync;
	uint64_t cookie;
	const granite_handle_owner_t *owner;
	granite_lf_pool_t *pool;
	VkImage image;
	/* of all of it; NULL for an image that has no view. The image's. */
	granite_image_view_t *view;
	/* base is VK_NULL_HANDLE for memory that is not the allocator's */
	granite_allocation_t alloc;
	granite_image_info_t info;
	/* kept in VK_IMAGE_LAYOUT_GENERAL, not the optimal layout of each use */
	bool general_layout;
	/* the stages and accesses it can be used in */
	VkPipelineStageFlags stage_flags;
	VkAccessFlags access_flags;
} granite_image_t;

/* The slots images and views are made in */
typedef struct granite_image_pools
{
	granite_lf_pool_t images;
	granite_lf_pool_t views;
} granite_image_pools_t;

void granite_image_pools_init(granite_image_pools_t *pools);
/* Every image and view must be gone */
void granite_image_pools_free(granite_image_pools_t *pools);

/* An object for @views, with one reference; NULL when there is no room
 * for one (the views are then still the caller's) */
granite_image_view_t *granite_image_view_new(granite_image_pools_t *pools, const granite_handle_owner_t *owner,
		const granite_image_views_t *views, const granite_image_view_info_t *info);
/* An object for @image and its memory, with one reference. @views: the
 * views of all of it, of type @view_type, which become its own view; or
 * NULL. NULL when there is no room (image, memory and views are then
 * still the caller's). */
granite_image_t *granite_image_new(granite_image_pools_t *pools, const granite_handle_owner_t *owner,
		VkImage image, const granite_allocation_t *alloc, const granite_image_info_t *info,
		const granite_image_views_t *views, VkImageViewType view_type);

/* The last reference gone: the image, then its memory, go to the owner,
 * then the image's own view is let go */
void granite_image_dispose(granite_image_t *image);
/* The last reference gone: every view goes to the owner */
void granite_image_view_dispose(granite_image_view_t *view);

static INLINE void granite_image_ref(granite_image_t *image)
{
	granite_refs_add(&image->refs);
}

static INLINE void granite_image_unref(granite_image_t *image)
{
	if (granite_refs_drop(&image->refs))
		granite_image_dispose(image);
}

static INLINE void granite_image_view_ref(granite_image_view_t *view)
{
	granite_refs_add(&view->refs);
}

static INLINE void granite_image_view_unref(granite_image_view_t *view)
{
	if (granite_refs_drop(&view->refs))
		granite_image_view_dispose(view);
}

/* The size of level @lod, never under one texel */
static INLINE uint32_t granite_image_width(const granite_image_t *image, uint32_t lod)
{
	uint32_t width = image->info.width >> lod;
	return width ? width : 1;
}

static INLINE uint32_t granite_image_height(const granite_image_t *image, uint32_t lod)
{
	uint32_t height = image->info.height >> lod;
	return height ? height : 1;
}

/* The layout the image is in for a use whose best layout is @optimal */
static INLINE VkImageLayout granite_image_layout(const granite_image_t *image, VkImageLayout optimal)
{
	return image->general_layout ? VK_IMAGE_LAYOUT_GENERAL : optimal;
}

/* The view to render to layer @layer of what @view covers: the layer's
 * own where there are layer views, otherwise the view itself */
static INLINE VkImageView granite_image_view_render_target(const granite_image_view_t *view, unsigned layer)
{
	if (view->views.num_layer_views == 0 || layer >= view->views.num_layer_views)
		return view->views.view;
	return view->views.layer_views[layer];
}

/* The view to sample depth, or stencil, alone through: of that aspect
 * where the image has both, otherwise the view itself */
static INLINE VkImageView granite_image_view_depth(const granite_image_view_t *view)
{
	return view->views.depth != VK_NULL_HANDLE ? view->views.depth : view->views.view;
}

static INLINE VkImageView granite_image_view_stencil(const granite_image_view_t *view)
{
	return view->views.stencil != VK_NULL_HANDLE ? view->views.stencil : view->views.view;
}

RETRO_END_DECLS

#endif
