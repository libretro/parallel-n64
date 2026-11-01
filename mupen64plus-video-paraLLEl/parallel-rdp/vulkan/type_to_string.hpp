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


namespace Vulkan
{
static inline const char *layout_to_string(VkImageLayout layout)
{
	switch (layout)
	{
	case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
		return "SHADER_READ_ONLY";
	case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
		return "DS_READ_ONLY";
	case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
		return "DS";
	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
		return "COLOR";
	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
		return "TRANSFER_DST";
	case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
		return "TRANSFER_SRC";
	case VK_IMAGE_LAYOUT_GENERAL:
		return "GENERAL";
	case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
		return "PRESENT";
	default:
		return "UNDEFINED";
	}
}
}
