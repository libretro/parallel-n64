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

#ifndef GRANITE_COMMAND_OPS_H
#define GRANITE_COMMAND_OPS_H

#include <stddef.h>
#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>
#include "volk.h"

RETRO_BEGIN_DECLS

/* What the operations below need to know of an image */
typedef struct granite_image_ref
{
	VkImage image;
	VkFormat format;
	uint32_t width;
	uint32_t height;
	uint32_t depth;
	uint32_t levels;
	uint32_t layers;
	/* it is kept in VK_IMAGE_LAYOUT_GENERAL, not the optimal layout of
	 * each use */
	bool general;
} granite_image_ref_t;

/* The aspects an image of @format has: colour, or depth and stencil */
VkImageAspectFlags granite_format_aspect_mask(VkFormat format);

/* The stages to wait for in a barrier. With @narrow, on a driver where
 * waiting for ALL_GRAPHICS is slow because it waits for vertex work as
 * well, that bit becomes the fragment and attachment stages. */
VkPipelineStageFlags granite_barrier_src_stages(VkPipelineStageFlags src_stages, bool narrow);

/* Every level and layer of @image moves from @old_layout to @new_layout */
void granite_cmd_image_barrier(const struct VolkDeviceTable *table, VkCommandBuffer cmd,
		const granite_image_ref_t *image, VkImageLayout old_layout, VkImageLayout new_layout,
		VkPipelineStageFlags src_stages, VkAccessFlags src_access, VkPipelineStageFlags dst_stages,
		VkAccessFlags dst_access);
/* Clears every level and layer of @aspect of @image, which is in its
 * transfer destination layout */
void granite_cmd_clear_image(const struct VolkDeviceTable *table, VkCommandBuffer cmd,
		const granite_image_ref_t *image, const VkClearValue *value, VkImageAspectFlags aspect);
/* Blits @num_layers layers, a layer a call (one call for all of them
 * goes wrong on RADV). The extents are sizes, from the offsets. */
void granite_cmd_blit_image(const struct VolkDeviceTable *table, VkCommandBuffer cmd,
		const granite_image_ref_t *dst, const granite_image_ref_t *src, const VkOffset3D *dst_offset,
		const VkOffset3D *dst_extent, const VkOffset3D *src_offset, const VkOffset3D *src_extent,
		unsigned dst_level, unsigned src_level, unsigned dst_base_layer, unsigned src_base_layer,
		unsigned num_layers, VkFilter filter);

RETRO_END_DECLS

#endif
