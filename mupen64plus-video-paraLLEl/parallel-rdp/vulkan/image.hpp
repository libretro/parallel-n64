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

#include "cookie.hpp"
#include "format.hpp"
#include "image_create.h"
#include "vulkan_common.hpp"
#include "memory_allocator.hpp"
#include "bitops.hpp"
#include "small_vector.hpp"
#include "vulkan_headers.hpp"

namespace Vulkan
{
class Device;

struct ImageInitialData
{
	const void *data;
	unsigned row_length;
	unsigned image_height;
};

enum ImageMiscFlagBits
{
	IMAGE_MISC_FORCE_ARRAY_BIT = 1 << 1,
	IMAGE_MISC_MUTABLE_SRGB_BIT = 1 << 2,
	IMAGE_MISC_CONCURRENT_QUEUE_GRAPHICS_BIT = 1 << 3,
	IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_COMPUTE_BIT = 1 << 4
};
using ImageMiscFlags = uint32_t;

class Image;

struct ImageViewCreateInfo
{
	const Image *image = nullptr;
	VkFormat format = VK_FORMAT_UNDEFINED;
	unsigned base_level = 0;
	unsigned levels = VK_REMAINING_MIP_LEVELS;
	unsigned base_layer = 0;
	unsigned layers = VK_REMAINING_ARRAY_LAYERS;
	VkImageViewType view_type = VK_IMAGE_VIEW_TYPE_2D;
};

class ImageView;

struct ImageViewDeleter
{
	void operator()(ImageView *view);
};

class ImageView : public Util::IntrusivePtrEnabled<ImageView, ImageViewDeleter, HandleCounter>,
                  public Cookie, public InternalSyncEnabled
{
public:
	friend struct ImageViewDeleter;

	ImageView(Device *device, VkImageView view, const ImageViewCreateInfo &info);

	~ImageView();

	// The views that go with this one (image_create.h); views.view is
	// this view.
	void set_views(const granite_image_views_t &views)
	{
		depth_view = views.depth;
		stencil_view = views.stencil;
		num_render_target_views = views.num_layer_views;
		for (unsigned i = 0; i < views.num_layer_views; i++)
			render_target_views[i] = views.layer_views[i];
	}

	// By default, gets a combined view which includes all aspects in the image.
	// This would be used mostly for render targets.
	VkImageView get_view() const
	{
		return view;
	}

	VkImageView get_render_target_view(unsigned layer) const;

	// Gets an image view which only includes floating point domains.
	// Takes effect when we want to sample from an image which is Depth/Stencil,
	// but we only want to sample depth.
	VkImageView get_float_view() const
	{
		return depth_view != VK_NULL_HANDLE ? depth_view : view;
	}

	// Gets an image view which only includes integer domains.
	// Takes effect when we want to sample from an image which is Depth/Stencil,
	// but we only want to sample stencil.
	VkImageView get_integer_view() const
	{
		return stencil_view != VK_NULL_HANDLE ? stencil_view : view;
	}

	VkFormat get_format() const
	{
		return info.format;
	}

	const Image &get_image() const
	{
		return *info.image;
	}

	const ImageViewCreateInfo &get_create_info() const
	{
		return info;
	}

	unsigned get_view_width() const;
	unsigned get_view_height() const;

private:
	Device *device;
	VkImageView view;
	VkImageView render_target_views[GRANITE_IMAGE_MAX_LAYER_VIEWS] = {};
	unsigned num_render_target_views = 0;
	VkImageView depth_view = VK_NULL_HANDLE;
	VkImageView stencil_view = VK_NULL_HANDLE;
	ImageViewCreateInfo info;
};

using ImageViewHandle = Util::IntrusivePtr<ImageView>;

struct ImageCreateInfo
{
	unsigned width = 0;
	unsigned height = 0;
	unsigned depth = 1;
	unsigned levels = 1;
	VkFormat format = VK_FORMAT_UNDEFINED;
	VkImageType type = VK_IMAGE_TYPE_2D;
	unsigned layers = 1;
	VkImageUsageFlags usage = 0;
	VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
	VkImageCreateFlags flags = 0;
	ImageMiscFlags misc = 0;
	VkImageLayout initial_layout = VK_IMAGE_LAYOUT_GENERAL;

	static ImageCreateInfo immutable_2d_image(unsigned width, unsigned height, VkFormat format)
	{
		ImageCreateInfo info;
		info.width = width;
		info.height = height;
		info.depth = 1;
		info.levels = 1;
		info.format = format;
		info.type = VK_IMAGE_TYPE_2D;
		info.layers = 1;
		info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
		info.samples = VK_SAMPLE_COUNT_1_BIT;
		info.flags = 0;
		info.misc = 0;
		info.initial_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		return info;
	}

	static ImageCreateInfo render_target(unsigned width, unsigned height, VkFormat format)
	{
		ImageCreateInfo info;
		info.width = width;
		info.height = height;
		info.depth = 1;
		info.levels = 1;
		info.format = format;
		info.type = VK_IMAGE_TYPE_2D;
		info.layers = 1;
		info.usage = (format_has_depth_or_stencil_aspect(format) ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT :
		              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) |
		             VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

		info.samples = VK_SAMPLE_COUNT_1_BIT;
		info.flags = 0;
		info.misc = 0;
		info.initial_layout = format_has_depth_or_stencil_aspect(format) ?
		                      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL :
		                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		return info;
	}

};

class Image;

struct ImageDeleter
{
	void operator()(Image *image);
};

enum class Layout
{
	Optimal,
	General
};

class Image : public Util::IntrusivePtrEnabled<Image, ImageDeleter, HandleCounter>,
              public Cookie, public InternalSyncEnabled
{
public:
	friend struct ImageDeleter;

	~Image();

	Image(Image &&) = delete;

	Image &operator=(Image &&) = delete;

	const ImageView &get_view() const
	{
		VK_ASSERT(view);
		return *view;
	}

	ImageView &get_view()
	{
		VK_ASSERT(view);
		return *view;
	}

	VkImage get_image() const
	{
		return image;
	}

	VkFormat get_format() const
	{
		return create_info.format;
	}

	uint32_t get_width(uint32_t lod = 0) const
	{
		return Util::max_of(1u, create_info.width >> lod);
	}

	uint32_t get_height(uint32_t lod = 0) const
	{
		return Util::max_of(1u, create_info.height >> lod);
	}

	uint32_t get_depth(uint32_t lod = 0) const
	{
		return Util::max_of(1u, create_info.depth >> lod);
	}

	const ImageCreateInfo &get_create_info() const
	{
		return create_info;
	}

	VkImageLayout get_layout(VkImageLayout optimal) const
	{
		return layout_type == Layout::Optimal ? optimal : VK_IMAGE_LAYOUT_GENERAL;
	}

	Layout get_layout_type() const
	{
		return layout_type;
	}

	void set_layout(Layout layout)
	{
		layout_type = layout;
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

	const DeviceAllocation &get_allocation() const
	{
		return alloc;
	}

private:
	friend class Util::ObjectPool<Image>;

	Image(Device *device, VkImage image, VkImageView default_view, const DeviceAllocation &alloc,
	      const ImageCreateInfo &info, VkImageViewType view_type);

	Device *device;
	VkImage image;
	ImageViewHandle view;
	DeviceAllocation alloc;
	ImageCreateInfo create_info;

	Layout layout_type = Layout::Optimal;
	VkPipelineStageFlags stage_flags = 0;
	VkAccessFlags access_flags = 0;
};

using ImageHandle = Util::IntrusivePtr<Image>;
}
