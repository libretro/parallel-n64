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

#include "format.hpp"
#include "image.h"
#include "image_upload.h"
#include "vulkan_common.hpp"
#include "memory_allocator.hpp"
#include "bitops.hpp"
#include "small_vector.hpp"
#include "vulkan_headers.hpp"

namespace Vulkan
{
class Device;

// Images are C (image.h, image_create.h). These give them the names and
// accessors the rest of Granite uses.
using ImageInitialData = granite_image_initial_data_t;

enum ImageMiscFlagBits
{
	IMAGE_MISC_FORCE_ARRAY_BIT = 1 << 1,
	IMAGE_MISC_MUTABLE_SRGB_BIT = 1 << 2,
	IMAGE_MISC_CONCURRENT_QUEUE_GRAPHICS_BIT = 1 << 3,
	IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_COMPUTE_BIT = 1 << 4
};
using ImageMiscFlags = uint32_t;

class Image;

// All of an image, as a 2D view, in its own format unless said otherwise.
struct ImageViewCreateInfo : granite_image_view_info_t
{
	ImageViewCreateInfo()
	{
		image = nullptr;
		format = VK_FORMAT_UNDEFINED;
		base_level = 0;
		levels = VK_REMAINING_MIP_LEVELS;
		base_layer = 0;
		layers = VK_REMAINING_ARRAY_LAYERS;
		view_type = VK_IMAGE_VIEW_TYPE_2D;
	}
};

// No members of its own: a granite_image_view_t by another name.
class ImageView : public granite_image_view_t
{
public:
	ImageView() = delete;

	void add_reference()
	{
		granite_image_view_ref(this);
	}

	void release_reference()
	{
		granite_image_view_unref(this);
	}

	uint64_t get_cookie() const
	{
		return cookie;
	}

	void set_internal_sync_object()
	{
		internal_sync = true;
	}

	// By default, gets a combined view which includes all aspects in the image.
	// This would be used mostly for render targets.
	VkImageView get_view() const
	{
		return views.view;
	}

	VkImageView get_render_target_view(unsigned layer) const
	{
		return granite_image_view_render_target(this, layer);
	}

	// Gets an image view which only includes floating point domains.
	// Takes effect when we want to sample from an image which is Depth/Stencil,
	// but we only want to sample depth.
	VkImageView get_float_view() const
	{
		return granite_image_view_depth(this);
	}

	// Gets an image view which only includes integer domains.
	// Takes effect when we want to sample from an image which is Depth/Stencil,
	// but we only want to sample stencil.
	VkImageView get_integer_view() const
	{
		return granite_image_view_stencil(this);
	}

	VkFormat get_format() const
	{
		return info.format;
	}

	inline const Image &get_image() const;

	const granite_image_view_info_t &get_create_info() const
	{
		return info;
	}

	unsigned get_view_width() const
	{
		return granite_image_width(info.image, info.base_level);
	}

	unsigned get_view_height() const
	{
		return granite_image_height(info.image, info.base_level);
	}
};

using ImageViewHandle = Util::IntrusivePtr<ImageView>;

// A 2D image of one level and layer unless said otherwise.
struct ImageCreateInfo : granite_image_info_t
{
	ImageCreateInfo()
	{
		width = 0;
		height = 0;
		depth = 1;
		levels = 1;
		format = VK_FORMAT_UNDEFINED;
		type = VK_IMAGE_TYPE_2D;
		layers = 1;
		usage = 0;
		samples = VK_SAMPLE_COUNT_1_BIT;
		flags = 0;
		misc = 0;
		initial_layout = VK_IMAGE_LAYOUT_GENERAL;
	}

	static ImageCreateInfo immutable_2d_image(unsigned width, unsigned height, VkFormat format)
	{
		ImageCreateInfo info;
		info.width = width;
		info.height = height;
		info.format = format;
		info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
		info.initial_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		return info;
	}

	static ImageCreateInfo render_target(unsigned width, unsigned height, VkFormat format)
	{
		ImageCreateInfo info;
		info.width = width;
		info.height = height;
		info.format = format;
		info.usage = (format_has_depth_or_stencil_aspect(format) ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT :
		              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) |
		             VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		info.initial_layout = format_has_depth_or_stencil_aspect(format) ?
		                      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL :
		                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		return info;
	}
};

enum class Layout
{
	Optimal,
	General
};

// No members of its own: a granite_image_t by another name.
class Image : public granite_image_t
{
public:
	Image() = delete;

	void add_reference()
	{
		granite_image_ref(this);
	}

	void release_reference()
	{
		granite_image_unref(this);
	}

	/* No other handle to it: none could be in use, or start to be. */
	bool only_reference() const
	{
		return granite_refs_only(const_cast<retro_atomic_int_t *>(&refs));
	}

	uint64_t get_cookie() const
	{
		return cookie;
	}

	void set_internal_sync_object()
	{
		internal_sync = true;
	}

	const ImageView &get_view() const
	{
		VK_ASSERT(view);
		return *static_cast<const ImageView *>(view);
	}

	ImageView &get_view()
	{
		VK_ASSERT(view);
		return *static_cast<ImageView *>(view);
	}

	VkImage get_image() const
	{
		return image;
	}

	VkFormat get_format() const
	{
		return info.format;
	}

	uint32_t get_width(uint32_t lod = 0) const
	{
		return granite_image_width(this, lod);
	}

	uint32_t get_height(uint32_t lod = 0) const
	{
		return granite_image_height(this, lod);
	}

	const granite_image_info_t &get_create_info() const
	{
		return info;
	}

	VkImageLayout get_layout(VkImageLayout optimal) const
	{
		return granite_image_layout(this, optimal);
	}

	Layout get_layout_type() const
	{
		return general_layout ? Layout::General : Layout::Optimal;
	}

	void set_layout(Layout layout)
	{
		general_layout = layout == Layout::General;
	}

	void set_stage_flags(VkPipelineStageFlags flags)
	{
		stage_flags = flags;
	}

	void set_access_flags(VkAccessFlags flags)
	{
		access_flags = flags;
	}

	VkPipelineStageFlags get_stage_flags() const
	{
		return stage_flags;
	}

	VkAccessFlags get_access_flags() const
	{
		return access_flags;
	}
};

inline const Image &ImageView::get_image() const
{
	return *static_cast<const Image *>(info.image);
}

using ImageHandle = Util::IntrusivePtr<Image>;
}
