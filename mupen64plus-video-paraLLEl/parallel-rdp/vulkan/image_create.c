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
#include "image_create.h"
#include "buffer_create.h"
#include "command_ops.h"
#include "logging.h"

#define VIEW_USAGES (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | \
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)
#define ATTACHMENT_USAGES (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)

bool granite_format_is_srgb(VkFormat format)
{
	switch (format)
	{
		case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
		case VK_FORMAT_R8G8B8A8_SRGB:
		case VK_FORMAT_B8G8R8A8_SRGB:
		case VK_FORMAT_R8_SRGB:
		case VK_FORMAT_R8G8_SRGB:
		case VK_FORMAT_R8G8B8_SRGB:
		case VK_FORMAT_B8G8R8_SRGB:
		case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
		case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
		case VK_FORMAT_BC2_SRGB_BLOCK:
		case VK_FORMAT_BC3_SRGB_BLOCK:
		case VK_FORMAT_BC7_SRGB_BLOCK:
		case VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK:
		case VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK:
		case VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK:
		case VK_FORMAT_ASTC_4x4_SRGB_BLOCK:
		case VK_FORMAT_ASTC_5x4_SRGB_BLOCK:
		case VK_FORMAT_ASTC_5x5_SRGB_BLOCK:
		case VK_FORMAT_ASTC_6x5_SRGB_BLOCK:
		case VK_FORMAT_ASTC_6x6_SRGB_BLOCK:
		case VK_FORMAT_ASTC_8x5_SRGB_BLOCK:
		case VK_FORMAT_ASTC_8x6_SRGB_BLOCK:
		case VK_FORMAT_ASTC_8x8_SRGB_BLOCK:
		case VK_FORMAT_ASTC_10x5_SRGB_BLOCK:
		case VK_FORMAT_ASTC_10x6_SRGB_BLOCK:
		case VK_FORMAT_ASTC_10x8_SRGB_BLOCK:
		case VK_FORMAT_ASTC_10x10_SRGB_BLOCK:
		case VK_FORMAT_ASTC_12x10_SRGB_BLOCK:
		case VK_FORMAT_ASTC_12x12_SRGB_BLOCK:
			return true;
		default:
			return false;
	}
}

unsigned granite_srgb_view_formats(VkFormat format, VkFormat *formats)
{
	switch (format)
	{
		case VK_FORMAT_R8G8B8A8_UNORM:
		case VK_FORMAT_R8G8B8A8_SRGB:
			formats[0] = VK_FORMAT_R8G8B8A8_UNORM;
			formats[1] = VK_FORMAT_R8G8B8A8_SRGB;
			return 2;
		case VK_FORMAT_B8G8R8A8_UNORM:
		case VK_FORMAT_B8G8R8A8_SRGB:
			formats[0] = VK_FORMAT_B8G8R8A8_UNORM;
			formats[1] = VK_FORMAT_B8G8R8A8_SRGB;
			return 2;
		case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
		case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
			formats[0] = VK_FORMAT_A8B8G8R8_UNORM_PACK32;
			formats[1] = VK_FORMAT_A8B8G8R8_SRGB_PACK32;
			return 2;
		default:
			return 0;
	}
}

VkFormatFeatureFlags granite_image_usage_features(VkImageUsageFlags usage)
{
	VkFormatFeatureFlags flags = 0;

	if (usage & VK_IMAGE_USAGE_SAMPLED_BIT)
		flags |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
	if (usage & VK_IMAGE_USAGE_STORAGE_BIT)
		flags |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
	if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
		flags |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
	if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
		flags |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
	return flags;
}

VkPipelineStageFlags granite_image_usage_stages(VkImageUsageFlags usage)
{
	VkPipelineStageFlags flags = 0;

	if (usage & (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT))
		flags |= VK_PIPELINE_STAGE_TRANSFER_BIT;
	if (usage & VK_IMAGE_USAGE_SAMPLED_BIT)
		flags |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	if (usage & VK_IMAGE_USAGE_STORAGE_BIT)
		flags |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
		flags |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
		flags |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	if (usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)
		flags |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	return flags;
}

VkAccessFlags granite_image_usage_access(VkImageUsageFlags usage)
{
	VkAccessFlags flags = 0;

	if (usage & (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT))
		flags |= VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
	if (usage & VK_IMAGE_USAGE_SAMPLED_BIT)
		flags |= VK_ACCESS_SHADER_READ_BIT;
	if (usage & VK_IMAGE_USAGE_STORAGE_BIT)
		flags |= VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
	if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
		flags |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
	if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
		flags |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
	if (usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)
		flags |= VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
	return flags;
}

VkAccessFlags granite_image_layout_access(VkImageLayout layout)
{
	switch (layout)
	{
		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
			return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
			return VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
			return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
			return VK_ACCESS_INPUT_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
			return VK_ACCESS_TRANSFER_READ_BIT;
		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			return VK_ACCESS_TRANSFER_WRITE_BIT;
		default:
			return ~(VkAccessFlags)0;
	}
}

bool granite_image_usage_has_view(VkImageUsageFlags usage)
{
	return (usage & VIEW_USAGES) != 0;
}

VkImageViewType granite_image_view_type(const granite_image_desc_t *desc)
{
	return (desc->layers > 1 || desc->force_array) ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
}

bool granite_image_create(const granite_image_env_t *env, const granite_image_desc_t *desc, bool upload,
		VkImage *image, granite_allocation_t *allocation, VkImageUsageFlags *usage, VkImageCreateFlags *flags)
{
	const struct VolkDeviceTable *table = env->table;
	VkImageCreateInfo info;
	VkImageFormatListCreateInfoKHR format_info;
	VkFormat view_formats[2];
	VkMemoryRequirements reqs;
	uint32_t families[4];
	uint32_t sharing[4];
	unsigned num_families = 0;
	granite_allocation_mode_t mode;
	uint32_t type;

	*image = VK_NULL_HANDLE;

	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	info.imageType = VK_IMAGE_TYPE_2D;
	info.format = desc->format;
	info.extent.width = desc->width;
	info.extent.height = desc->height;
	info.extent.depth = 1;
	info.mipLevels = desc->levels;
	info.arrayLayers = desc->layers;
	info.samples = desc->samples;
	info.tiling = VK_IMAGE_TILING_OPTIMAL;
	info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	info.usage = desc->usage;
	info.flags = desc->flags;
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if (upload)
		info.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

	/* a storage image is viewed in other formats of its size as well */
	if ((desc->usage & VK_IMAGE_USAGE_STORAGE_BIT) || desc->mutable_srgb)
		info.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
	if (desc->mutable_srgb && env->format_list)
	{
		memset(&format_info, 0, sizeof(format_info));
		format_info.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO_KHR;
		format_info.pViewFormats = view_formats;
		format_info.viewFormatCount = granite_srgb_view_formats(desc->format, view_formats);
		/* telling the driver which formats lets it keep the image compressed */
		if (format_info.viewFormatCount != 0)
			info.pNext = &format_info;
	}

	/* Shared only when asked for: sharing costs compression on some
	 * drivers. An image used by one family after all is not shared. */
	if (desc->on_graphics || desc->on_compute)
	{
		if (desc->on_graphics)
			families[num_families++] = env->graphics_family;
		if (desc->on_compute)
			families[num_families++] = env->compute_family;
		if (upload)
		{
			families[num_families++] = env->transfer_family;
			families[num_families++] = env->graphics_family;
		}
		info.queueFamilyIndexCount = granite_unique_queue_families(families, num_families, sharing);
		if (info.queueFamilyIndexCount > 1)
		{
			info.sharingMode = VK_SHARING_MODE_CONCURRENT;
			info.pQueueFamilyIndices = sharing;
		}
		else
			info.queueFamilyIndexCount = 0;
	}

	if (table->vkCreateImage(env->device, &info, NULL, image) != VK_SUCCESS)
	{
		LOGE("Failed to create image in vkCreateImage.\n");
		*image = VK_NULL_HANDLE;
		return false;
	}

	table->vkGetImageMemoryRequirements(env->device, *image, &reqs);
	type = granite_find_image_memory_type(env->memory, reqs.memoryTypeBits);
	if (type == GRANITE_NO_MEMORY_TYPE)
	{
		LOGE("Failed to find memory type.\n");
		goto fail;
	}

	/* what is rendered or written to is worth keeping in VRAM under
	 * pressure */
	if (desc->usage & (ATTACHMENT_USAGES | VK_IMAGE_USAGE_STORAGE_BIT))
		mode = GRANITE_ALLOCATION_OPTIMAL_RENDER_TARGET;
	else
		mode = GRANITE_ALLOCATION_OPTIMAL_RESOURCE;

	if (!granite_device_allocator_allocate_image(env->allocator, (uint32_t)reqs.size, (uint32_t)reqs.alignment, mode,
				type, *image, allocation))
	{
		LOGE("Failed to allocate image memory (type %u, size: %u).\n", (unsigned)type, (unsigned)reqs.size);
		goto fail;
	}

	if (table->vkBindImageMemory(env->device, *image, allocation->base, allocation->offset) != VK_SUCCESS)
	{
		LOGE("Failed to bind image memory.\n");
		granite_allocation_free(allocation, env->allocator);
		goto fail;
	}

	*usage = info.usage;
	*flags = info.flags;
	return true;

fail:
	table->vkDestroyImage(env->device, *image, NULL);
	*image = VK_NULL_HANDLE;
	return false;
}

void granite_image_views_destroy(VkDevice device, const struct VolkDeviceTable *table, granite_image_views_t *views)
{
	unsigned i;

	if (views->view != VK_NULL_HANDLE)
		table->vkDestroyImageView(device, views->view, NULL);
	if (views->depth != VK_NULL_HANDLE)
		table->vkDestroyImageView(device, views->depth, NULL);
	if (views->stencil != VK_NULL_HANDLE)
		table->vkDestroyImageView(device, views->stencil, NULL);
	for (i = 0; i < views->num_layer_views; i++)
		table->vkDestroyImageView(device, views->layer_views[i], NULL);
	memset(views, 0, sizeof(*views));
}

bool granite_image_views_create(VkDevice device, const struct VolkDeviceTable *table,
		const granite_image_view_desc_t *desc, granite_image_views_t *views)
{
	VkImageViewCreateInfo info;
	VkImageViewUsageCreateInfo usage_info;
	VkImageViewCreateInfo sub;
	uint32_t layer;

	memset(views, 0, sizeof(*views));

	if (!granite_image_usage_has_view(desc->usage))
	{
		LOGE("Cannot create image view unless certain usage flags are present.\n");
		return false;
	}

	/* what a view is used for is what the image is, less what its format
	 * cannot do */
	memset(&usage_info, 0, sizeof(usage_info));
	usage_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO;
	usage_info.usage = desc->usage & VIEW_USAGES;
	if (granite_format_is_srgb(desc->format))
		usage_info.usage &= ~(VkImageUsageFlags)VK_IMAGE_USAGE_STORAGE_BIT;

	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	info.pNext = &usage_info;
	info.image = desc->image;
	info.format = desc->format;
	info.viewType = desc->type;
	info.subresourceRange.aspectMask = granite_format_aspect_mask(desc->format);
	info.subresourceRange.baseMipLevel = desc->base_level;
	info.subresourceRange.levelCount = desc->levels;
	info.subresourceRange.baseArrayLayer = desc->base_layer;
	info.subresourceRange.layerCount = desc->layers;

	if (info.subresourceRange.aspectMask == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT) &&
			(desc->usage & ~(VkImageUsageFlags)VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0)
	{
		sub = info;
		sub.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		if (table->vkCreateImageView(device, &sub, NULL, &views->depth) != VK_SUCCESS)
			goto fail;
		sub.subresourceRange.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
		if (table->vkCreateImageView(device, &sub, NULL, &views->stencil) != VK_SUCCESS)
			goto fail;
	}

	if ((desc->usage & ATTACHMENT_USAGES) != 0 && (desc->levels > 1 || desc->layers > 1))
	{
		if (desc->layers > GRANITE_IMAGE_MAX_LAYER_VIEWS)
			goto fail;
		sub = info;
		sub.viewType = VK_IMAGE_VIEW_TYPE_2D;
		sub.subresourceRange.levelCount = 1;
		sub.subresourceRange.layerCount = 1;
		for (layer = 0; layer < desc->layers; layer++)
		{
			sub.subresourceRange.baseArrayLayer = desc->base_layer + layer;
			if (table->vkCreateImageView(device, &sub, NULL, &views->layer_views[layer]) != VK_SUCCESS)
				goto fail;
			views->num_layer_views++;
		}
	}

	if (table->vkCreateImageView(device, &info, NULL, &views->view) != VK_SUCCESS)
		goto fail;
	return true;

fail:
	granite_image_views_destroy(device, table, views);
	return false;
}
