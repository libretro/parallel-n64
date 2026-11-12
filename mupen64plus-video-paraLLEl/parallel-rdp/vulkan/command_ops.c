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
#include "command_ops.h"

VkImageAspectFlags granite_format_aspect_mask(VkFormat format)
{
	switch (format)
	{
		case VK_FORMAT_UNDEFINED:
			return 0;
		case VK_FORMAT_S8_UINT:
			return VK_IMAGE_ASPECT_STENCIL_BIT;
		case VK_FORMAT_D16_UNORM_S8_UINT:
		case VK_FORMAT_D24_UNORM_S8_UINT:
		case VK_FORMAT_D32_SFLOAT_S8_UINT:
			return VK_IMAGE_ASPECT_STENCIL_BIT | VK_IMAGE_ASPECT_DEPTH_BIT;
		case VK_FORMAT_D16_UNORM:
		case VK_FORMAT_D32_SFLOAT:
		case VK_FORMAT_X8_D24_UNORM_PACK32:
			return VK_IMAGE_ASPECT_DEPTH_BIT;
		default:
			break;
	}
	return VK_IMAGE_ASPECT_COLOR_BIT;
}

VkPipelineStageFlags granite_barrier_src_stages(VkPipelineStageFlags src_stages, bool narrow)
{
	if (narrow && (src_stages & VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT))
	{
		src_stages &= ~(VkPipelineStageFlags)VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
		src_stages |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
			VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	}
	return src_stages;
}

static VkImageLayout layout_of(const granite_image_ref_t *image, VkImageLayout optimal)
{
	return image->general ? VK_IMAGE_LAYOUT_GENERAL : optimal;
}

void granite_cmd_image_barrier(const struct VolkDeviceTable *table, VkCommandBuffer cmd,
		const granite_image_ref_t *image, VkImageLayout old_layout, VkImageLayout new_layout,
		VkPipelineStageFlags src_stages, VkAccessFlags src_access, VkPipelineStageFlags dst_stages,
		VkAccessFlags dst_access)
{
	VkImageMemoryBarrier barrier;

	memset(&barrier, 0, sizeof(barrier));
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.srcAccessMask = src_access;
	barrier.dstAccessMask = dst_access;
	barrier.oldLayout = old_layout;
	barrier.newLayout = new_layout;
	barrier.image = image->image;
	barrier.subresourceRange.aspectMask = granite_format_aspect_mask(image->format);
	barrier.subresourceRange.levelCount = image->levels;
	barrier.subresourceRange.layerCount = image->layers;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

	table->vkCmdPipelineBarrier(cmd, src_stages, dst_stages, 0, 0, NULL, 0, NULL, 1, &barrier);
}

void granite_cmd_clear_image(const struct VolkDeviceTable *table, VkCommandBuffer cmd,
		const granite_image_ref_t *image, const VkClearValue *value, VkImageAspectFlags aspect)
{
	VkImageLayout layout = layout_of(image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	VkImageSubresourceRange range;

	memset(&range, 0, sizeof(range));
	range.aspectMask = aspect;
	range.levelCount = image->levels;
	range.layerCount = image->layers;

	if (aspect & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))
		table->vkCmdClearDepthStencilImage(cmd, image->image, layout, &value->depthStencil, 1, &range);
	else
		table->vkCmdClearColorImage(cmd, image->image, layout, &value->color, 1, &range);
}

void granite_cmd_blit_image(const struct VolkDeviceTable *table, VkCommandBuffer cmd,
		const granite_image_ref_t *dst, const granite_image_ref_t *src, const VkOffset3D *dst_offset,
		const VkOffset3D *dst_extent, const VkOffset3D *src_offset, const VkOffset3D *src_extent,
		unsigned dst_level, unsigned src_level, unsigned dst_base_layer, unsigned src_base_layer,
		unsigned num_layers, VkFilter filter)
{
	VkImageLayout src_layout = layout_of(src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	VkImageLayout dst_layout = layout_of(dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	VkImageBlit blit;
	unsigned i;

	memset(&blit, 0, sizeof(blit));
	blit.srcSubresource.aspectMask = granite_format_aspect_mask(src->format);
	blit.srcSubresource.mipLevel = src_level;
	blit.srcSubresource.layerCount = 1;
	blit.srcOffsets[0] = *src_offset;
	blit.srcOffsets[1].x = src_offset->x + src_extent->x;
	blit.srcOffsets[1].y = src_offset->y + src_extent->y;
	blit.srcOffsets[1].z = src_offset->z + src_extent->z;
	blit.dstSubresource.aspectMask = granite_format_aspect_mask(dst->format);
	blit.dstSubresource.mipLevel = dst_level;
	blit.dstSubresource.layerCount = 1;
	blit.dstOffsets[0] = *dst_offset;
	blit.dstOffsets[1].x = dst_offset->x + dst_extent->x;
	blit.dstOffsets[1].y = dst_offset->y + dst_extent->y;
	blit.dstOffsets[1].z = dst_offset->z + dst_extent->z;

	for (i = 0; i < num_layers; i++)
	{
		blit.srcSubresource.baseArrayLayer = src_base_layer + i;
		blit.dstSubresource.baseArrayLayer = dst_base_layer + i;
		table->vkCmdBlitImage(cmd, src->image, src_layout, dst->image, dst_layout, 1, &blit, filter);
	}
}

void granite_cmd_prepare_generate_mipmap(const struct VolkDeviceTable *table, VkCommandBuffer cmd,
		const granite_image_ref_t *image, VkImageLayout base_level_layout, VkPipelineStageFlags src_stages,
		VkAccessFlags src_access, bool top_level)
{
	VkImageMemoryBarrier barriers[2];
	unsigned i;

	/* nothing under level 0: nothing to prepare but level 0 itself */
	if (image->levels <= 1 && !top_level)
		return;

	memset(barriers, 0, sizeof(barriers));
	for (i = 0; i < 2; i++)
	{
		barriers[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barriers[i].image = image->image;
		barriers[i].subresourceRange.aspectMask = granite_format_aspect_mask(image->format);
		barriers[i].subresourceRange.layerCount = image->layers;
		barriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	}

	barriers[0].oldLayout = base_level_layout;
	barriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	barriers[0].srcAccessMask = src_access;
	barriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	barriers[0].subresourceRange.baseMipLevel = 0;
	barriers[0].subresourceRange.levelCount = 1;

	barriers[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	barriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barriers[1].srcAccessMask = 0;
	barriers[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barriers[1].subresourceRange.baseMipLevel = 1;
	barriers[1].subresourceRange.levelCount = image->levels > 1 ? image->levels - 1 : 0;

	if (image->levels <= 1)
		table->vkCmdPipelineBarrier(cmd, src_stages, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, barriers);
	else if (top_level)
		table->vkCmdPipelineBarrier(cmd, src_stages, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 2, barriers);
	else
		table->vkCmdPipelineBarrier(cmd, src_stages, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1,
				barriers + 1);
}

void granite_cmd_generate_mipmap(const struct VolkDeviceTable *table, VkCommandBuffer cmd,
		const granite_image_ref_t *image)
{
	VkImageMemoryBarrier barrier;
	VkImageBlit blit;
	VkOffset3D size;
	uint32_t level, layer;

	size.x = (int32_t)image->width;
	size.y = (int32_t)image->height;
	size.z = (int32_t)image->depth;

	memset(&barrier, 0, sizeof(barrier));
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.image = image->image;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.layerCount = image->layers;
	barrier.subresourceRange.aspectMask = granite_format_aspect_mask(image->format);
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

	memset(&blit, 0, sizeof(blit));
	blit.srcSubresource.aspectMask = barrier.subresourceRange.aspectMask;
	blit.srcSubresource.layerCount = 1;
	blit.dstSubresource.aspectMask = barrier.subresourceRange.aspectMask;
	blit.dstSubresource.layerCount = 1;

	for (level = 1; level < image->levels; level++)
	{
		blit.srcOffsets[1] = size;
		if (size.x > 1)
			size.x >>= 1;
		if (size.y > 1)
			size.y >>= 1;
		if (size.z > 1)
			size.z >>= 1;
		blit.dstOffsets[1] = size;
		blit.srcSubresource.mipLevel = level - 1;
		blit.dstSubresource.mipLevel = level;

		/* (a layer a call, as in granite_cmd_blit_image) */
		for (layer = 0; layer < image->layers; layer++)
		{
			blit.srcSubresource.baseArrayLayer = layer;
			blit.dstSubresource.baseArrayLayer = layer;
			table->vkCmdBlitImage(cmd, image->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image->image,
					VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
		}

		/* this level is the source of the next */
		barrier.subresourceRange.baseMipLevel = level;
		table->vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL,
				0, NULL, 1, &barrier);
	}
}
