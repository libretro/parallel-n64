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

#ifndef GRANITE_TEXTURE_FORMAT_H
#define GRANITE_TEXTURE_FORMAT_H

#include <stddef.h>
#include <stdint.h>
#include <retro_common_api.h>
#include <retro_inline.h>
#include "volk.h"

RETRO_BEGIN_DECLS

/* Bytes in a block of @format: a texel, or a compressed block. @aspect
 * picks the plane of a two-plane format; 0 otherwise. */
uint32_t granite_format_block_size(VkFormat format, VkImageAspectFlags aspect);
/* Texels in a block: 1 by 1 unless compressed */
void granite_format_block_dim(VkFormat format, uint32_t *width, uint32_t *height);
/* Down to 1x1x1 */
uint32_t granite_num_miplevels(uint32_t width, uint32_t height, uint32_t depth);

#define GRANITE_TEXTURE_MAX_LEVELS 16

typedef struct granite_mip_info
{
	/* of the level in the buffer */
	size_t offset;
	uint32_t width;
	uint32_t height;
	uint32_t depth;
	/* in blocks */
	uint32_t block_image_height;
	uint32_t block_row_length;
	/* in texels: the blocks' */
	uint32_t image_height;
	uint32_t row_length;
} granite_mip_info_t;

/* How an image's levels and layers lie in one buffer, level after level,
 * each starting on 16 bytes, its layers (or slices) one after another:
 * what an upload's staging buffer holds. */
typedef struct granite_texture_layout
{
	uint8_t *buffer;
	VkImageType image_type;
	VkFormat format;
	size_t required_size;
	uint32_t block_stride;
	uint32_t mip_levels;
	uint32_t array_layers;
	uint32_t block_dim_x;
	uint32_t block_dim_y;
	granite_mip_info_t mips[GRANITE_TEXTURE_MAX_LEVELS];
} granite_texture_layout_t;

/* @mip_levels 0: all of them. A 1D image has no height, only a 3D one
 * depth, and a 3D one a single layer, whatever is passed. */
void granite_texture_layout_set(granite_texture_layout_t *layout, VkImageType type, VkFormat format,
		uint32_t width, uint32_t height, uint32_t depth, uint32_t array_layers, uint32_t mip_levels);
/* One copy a level, all its layers, into copies[mip_levels] */
void granite_texture_layout_copies(const granite_texture_layout_t *layout, VkImageAspectFlags aspect,
		VkBufferImageCopy *copies);

/* Bytes in a row, and in a layer, of a level as the buffer holds it */
static INLINE size_t granite_texture_layout_row_size(const granite_texture_layout_t *layout, uint32_t mip)
{
	return (size_t)layout->mips[mip].block_row_length * layout->block_stride;
}

static INLINE size_t granite_texture_layout_layer_size(const granite_texture_layout_t *layout, uint32_t mip)
{
	return layout->mips[mip].block_image_height * granite_texture_layout_row_size(layout, mip);
}

/* The same of source data @row_length texels wide, @image_height high */
static INLINE size_t granite_texture_layout_row_stride(const granite_texture_layout_t *layout, uint32_t row_length)
{
	return (size_t)((row_length + layout->block_dim_x - 1) / layout->block_dim_x) * layout->block_stride;
}

static INLINE size_t granite_texture_layout_layer_stride(const granite_texture_layout_t *layout,
		uint32_t image_height, size_t row_stride)
{
	return ((image_height + layout->block_dim_y - 1) / layout->block_dim_y) * row_stride;
}

/* Where @layer of level @mip starts in @layout->buffer */
static INLINE uint8_t *granite_texture_layout_data(const granite_texture_layout_t *layout, uint32_t layer, uint32_t mip)
{
	return layout->buffer + layout->mips[mip].offset + layer * granite_texture_layout_layer_size(layout, mip);
}

RETRO_END_DECLS

#endif
