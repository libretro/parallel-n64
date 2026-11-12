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

#pragma once

#include "render_pass.h"
#include "framebuffer.h"
#include "cookie.hpp"
#include "hash.hpp"
#include "image.hpp"
#include "intrusive.hpp"
#include "limits.hpp"
#include "vulkan_headers.hpp"
#include "pipeline_record.h"

// Render passes and framebuffers are C (render_pass.h, framebuffer.h).
// These give the rest of Granite the names it had.
namespace Vulkan
{
static_assert(VULKAN_NUM_ATTACHMENTS == GRANITE_NUM_ATTACHMENTS, "The C render pass has other attachments.");

enum RenderPassOp
{
	RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT = GRANITE_RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT,
	RENDER_PASS_OP_LOAD_DEPTH_STENCIL_BIT = GRANITE_RENDER_PASS_OP_LOAD_DEPTH_STENCIL_BIT,
	RENDER_PASS_OP_STORE_DEPTH_STENCIL_BIT = GRANITE_RENDER_PASS_OP_STORE_DEPTH_STENCIL_BIT,
	RENDER_PASS_OP_DEPTH_STENCIL_READ_ONLY_BIT = GRANITE_RENDER_PASS_OP_DEPTH_STENCIL_READ_ONLY_BIT,
	RENDER_PASS_OP_ENABLE_TRANSIENT_STORE_BIT = GRANITE_RENDER_PASS_OP_ENABLE_TRANSIENT_STORE_BIT,
	RENDER_PASS_OP_ENABLE_TRANSIENT_LOAD_BIT = GRANITE_RENDER_PASS_OP_ENABLE_TRANSIENT_LOAD_BIT
};
using RenderPassOpFlags = uint32_t;

class ImageView;
// A pass of one subpass that draws to all of these.
struct RenderPassInfo
{
	const ImageView *color_attachments[VULKAN_NUM_ATTACHMENTS];
	const ImageView *depth_stencil = nullptr;
	unsigned num_color_attachments = 0;
	RenderPassOpFlags op_flags = 0;
	uint32_t clear_attachments = 0;
	uint32_t load_attachments = 0;
	uint32_t store_attachments = 0;

	// Render area will be clipped to the actual framebuffer.
	VkRect2D render_area = { { 0, 0 }, { UINT32_MAX, UINT32_MAX } };

	VkClearColorValue clear_color[VULKAN_NUM_ATTACHMENTS] = {};
	VkClearDepthStencilValue clear_depth_stencil = { 1.0f, 0 };
};

class RenderPass : public HashedObject<RenderPass>, public NoCopyNoMove
{
public:
	// device.cpp
	RenderPass(Util::Hash hash, Device *device, const granite_render_pass_desc_t &desc);

	~RenderPass()
	{
		granite_render_pass_free(&impl);
	}

	VkRenderPass get_render_pass() const
	{
		return impl.render_pass;
	}

	// What the pass is made from, without its images.
	const pipeline_record_pass_t &get_signature() const
	{
		return impl.signature;
	}

	uint32_t get_sample_count() const
	{
		return impl.samples;
	}

	unsigned get_num_color_attachments() const
	{
		return impl.num_color;
	}

	bool has_depth() const
	{
		return granite_render_pass_has_depth(&impl);
	}

	bool has_stencil() const
	{
		return granite_render_pass_has_stencil(&impl);
	}

private:
	granite_render_pass_t impl;
};

// Its render_pass is the compatible RenderPass it was made with.
using Framebuffer = granite_framebuffer_t;
}
