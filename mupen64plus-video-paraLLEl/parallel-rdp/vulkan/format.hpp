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

#include "vulkan_headers.hpp"
#include "texture_format.h"
#include "command_ops.h"

namespace Vulkan
{
static inline bool format_has_depth_aspect(VkFormat format)
{
	switch (format)
	{
	case VK_FORMAT_D16_UNORM:
	case VK_FORMAT_D16_UNORM_S8_UINT:
	case VK_FORMAT_D24_UNORM_S8_UINT:
	case VK_FORMAT_D32_SFLOAT:
	case VK_FORMAT_X8_D24_UNORM_PACK32:
	case VK_FORMAT_D32_SFLOAT_S8_UINT:
		return true;

	default:
		return false;
	}
}

static inline bool format_has_stencil_aspect(VkFormat format)
{
	switch (format)
	{
	case VK_FORMAT_D16_UNORM_S8_UINT:
	case VK_FORMAT_D24_UNORM_S8_UINT:
	case VK_FORMAT_D32_SFLOAT_S8_UINT:
	case VK_FORMAT_S8_UINT:
		return true;

	default:
		return false;
	}
}

static inline bool format_has_depth_or_stencil_aspect(VkFormat format)
{
	return format_has_depth_aspect(format) || format_has_stencil_aspect(format);
}

static inline VkImageAspectFlags format_to_aspect_mask(VkFormat format)
{
	return granite_format_aspect_mask(format);
}

static inline bool format_supports_storage_image_read_write_without_format(VkFormat format)
{
	/* from https://www.khronos.org/registry/vulkan/specs/1.2-extensions/html/vkspec.html#formats-without-shader-storage-format */
	static const VkFormat supported_formats[] =
	{
		VK_FORMAT_R8G8B8A8_UNORM,
		VK_FORMAT_R8G8B8A8_SNORM,
		VK_FORMAT_R8G8B8A8_UINT,
		VK_FORMAT_R8G8B8A8_SINT,
		VK_FORMAT_R32_UINT,
		VK_FORMAT_R32_SINT,
		VK_FORMAT_R32_SFLOAT,
		VK_FORMAT_R32G32_UINT,
		VK_FORMAT_R32G32_SINT,
		VK_FORMAT_R32G32_SFLOAT,
		VK_FORMAT_R32G32B32A32_UINT,
		VK_FORMAT_R32G32B32A32_SINT,
		VK_FORMAT_R32G32B32A32_SFLOAT,
		VK_FORMAT_R16G16B16A16_UINT,
		VK_FORMAT_R16G16B16A16_SINT,
		VK_FORMAT_R16G16B16A16_SFLOAT,
		VK_FORMAT_R16G16_SFLOAT,
		VK_FORMAT_B10G11R11_UFLOAT_PACK32,
		VK_FORMAT_R16_SFLOAT,
		VK_FORMAT_R16G16B16A16_UNORM,
		VK_FORMAT_A2B10G10R10_UNORM_PACK32,
		VK_FORMAT_R16G16_UNORM,
		VK_FORMAT_R8G8_UNORM,
		VK_FORMAT_R16_UNORM,
		VK_FORMAT_R8_UNORM,
		VK_FORMAT_R16G16B16A16_SNORM,
		VK_FORMAT_R16G16_SNORM,
		VK_FORMAT_R8G8_SNORM,
		VK_FORMAT_R16_SNORM,
		VK_FORMAT_R8_SNORM,
		VK_FORMAT_R16G16_SINT,
		VK_FORMAT_R8G8_SINT,
		VK_FORMAT_R16_SINT,
		VK_FORMAT_R8_SINT,
		VK_FORMAT_A2B10G10R10_UINT_PACK32,
		VK_FORMAT_R16G16_UINT,
		VK_FORMAT_R8G8_UINT,
		VK_FORMAT_R16_UINT,
		VK_FORMAT_R8_UINT,
	};

	for (auto fmt : supported_formats)
		if (fmt == format)
			return true;

	return false;
}
}
