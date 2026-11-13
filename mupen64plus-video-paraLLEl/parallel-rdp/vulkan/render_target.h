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

#ifndef GRANITE_RENDER_TARGET_H
#define GRANITE_RENDER_TARGET_H

#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>
#include "volk.h"
#include "image.h"
#include "render_pass.h"
#include "framebuffer.h"
#include "program_cache.h"

RETRO_BEGIN_DECLS

/* A pass of one subpass that draws to all of these */
typedef struct granite_render_pass_info
{
	const granite_image_view_t *color_attachments[GRANITE_NUM_ATTACHMENTS];
	const granite_image_view_t *depth_stencil;
	unsigned num_color_attachments;
	/* GRANITE_RENDER_PASS_OP_* */
	uint32_t op_flags;
	/* a bit a colour attachment */
	uint32_t clear_attachments;
	uint32_t load_attachments;
	uint32_t store_attachments;
	/* clipped to the framebuffer */
	VkRect2D render_area;
	VkClearColorValue clear_color[GRANITE_NUM_ATTACHMENTS];
	VkClearDepthStencilValue clear_depth_stencil;
} granite_render_pass_info_t;

/* No attachments, the whole framebuffer, depth cleared to 1 */
void granite_render_pass_info_init(granite_render_pass_info_t *info);
/* What the pass is made from */
void granite_render_pass_info_desc(const granite_render_pass_info_t *info, granite_render_pass_desc_t *desc);
/* The values the pass clears with, into @values
 * [GRANITE_NUM_ATTACHMENTS + 1]; returns how many the driver reads */
unsigned granite_render_pass_info_clear_values(const granite_render_pass_info_t *info, VkClearValue *values);

/* The render pass of @info; @compatible: the one pipelines and
 * framebuffers are made for. NULL when out of memory. */
granite_cached_render_pass_t *granite_render_target_pass(granite_program_cache_t *cache,
		const granite_render_pass_info_t *info, bool compatible);
/* The framebuffer of @info's views for its compatible pass, as large as
 * the smallest of them: this frame's when it was asked for lately, or
 * made. Its render_pass is the compatible granite_cached_render_pass_t.
 * NULL when out of memory for the pass. The owner thread only. */
const granite_framebuffer_t *granite_render_target_framebuffer(granite_framebuffer_allocator_t *allocator,
		granite_program_cache_t *cache, const granite_render_pass_info_t *info);

RETRO_END_DECLS

#endif
