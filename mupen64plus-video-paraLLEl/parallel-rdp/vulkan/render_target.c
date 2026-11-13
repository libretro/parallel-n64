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
#include "render_target.h"

void granite_render_pass_info_init(granite_render_pass_info_t *info)
{
	memset(info, 0, sizeof(*info));
	info->render_area.extent.width = UINT32_MAX;
	info->render_area.extent.height = UINT32_MAX;
	info->clear_depth_stencil.depth = 1.0f;
}

static granite_attachment_desc_t attachment_desc(const granite_image_view_t *view)
{
	granite_attachment_desc_t desc;
	desc.format = view->info.format;
	desc.samples = view->info.image->info.samples;
	/* (no image is lazily allocated) */
	desc.transient = false;
	desc.general = view->info.image->general_layout;
	return desc;
}

void granite_render_pass_info_desc(const granite_render_pass_info_t *info, granite_render_pass_desc_t *desc)
{
	unsigned i;

	memset(desc, 0, sizeof(*desc));
	desc->num_color = info->num_color_attachments;
	for (i = 0; i < info->num_color_attachments; i++)
		desc->color[i] = attachment_desc(info->color_attachments[i]);
	if (info->depth_stencil)
		desc->depth_stencil = attachment_desc(info->depth_stencil);
	desc->op_flags = info->op_flags;
	desc->clear_attachments = info->clear_attachments;
	desc->load_attachments = info->load_attachments;
	desc->store_attachments = info->store_attachments;
}

unsigned granite_render_pass_info_clear_values(const granite_render_pass_info_t *info, VkClearValue *values)
{
	unsigned i, count = 0;

	for (i = 0; i < info->num_color_attachments; i++)
	{
		if (info->clear_attachments & (1u << i))
		{
			values[i].color = info->clear_color[i];
			count = i + 1;
		}
	}

	if (info->depth_stencil && (info->op_flags & GRANITE_RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT))
	{
		values[info->num_color_attachments].depthStencil = info->clear_depth_stencil;
		count = info->num_color_attachments + 1;
	}

	return count;
}

granite_cached_render_pass_t *granite_render_target_pass(granite_program_cache_t *cache,
		const granite_render_pass_info_t *info, bool compatible)
{
	granite_render_pass_desc_t desc;
	granite_render_pass_info_desc(info, &desc);
	return granite_program_cache_render_pass(cache, &desc, compatible);
}

const granite_framebuffer_t *granite_render_target_framebuffer(granite_framebuffer_allocator_t *allocator,
		granite_program_cache_t *cache, const granite_render_pass_info_t *info)
{
	granite_cached_render_pass_t *pass = granite_render_target_pass(cache, info, true);
	VkImageView views[GRANITE_NUM_ATTACHMENTS + 1];
	granite_framebuffer_t *framebuffer;
	unsigned i, num_views = 0;
	uint32_t width = UINT32_MAX;
	uint32_t height = UINT32_MAX;
	uint64_t hash;

	if (!pass)
		return NULL;

	hash = granite_hash_u64(GRANITE_HASH_INIT, pass->node.hash);
	for (i = 0; i < info->num_color_attachments; i++)
		hash = granite_hash_u64(hash, info->color_attachments[i]->cookie);
	if (info->depth_stencil)
		hash = granite_hash_u64(hash, info->depth_stencil->cookie);
	/* (the base layer, when there were layers) */
	hash = granite_hash_u32(hash, 0);

	framebuffer = granite_framebuffer_allocator_find(allocator, hash);
	if (framebuffer)
		return framebuffer;

	for (i = 0; i <= info->num_color_attachments; i++)
	{
		const granite_image_view_t *view = i < info->num_color_attachments ? info->color_attachments[i]
			: info->depth_stencil;
		uint32_t view_width, view_height;

		if (!view)
			continue;
		views[num_views++] = granite_image_view_render_target(view, 0);
		view_width = granite_image_width(view->info.image, view->info.base_level);
		view_height = granite_image_height(view->info.image, view->info.base_level);
		if (view_width < width)
			width = view_width;
		if (view_height < height)
			height = view_height;
	}

	return granite_framebuffer_allocator_create(allocator, hash, pass->impl.render_pass, pass, views, num_views,
			width, height);
}
