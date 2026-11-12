/* parallel-rdp's texture formats and upload layout
 * (vulkan/texture_format.c): how many bytes a texel or a compressed block
 * of a format takes and how many texels a block covers; how many levels
 * an image has down to one texel; and how an image's levels and layers
 * lie in an upload's staging buffer: level after level, each starting on
 * 16 bytes, layers one after another, with one copy a level that covers
 * it. */
#include <stdio.h>
#include <string.h>

#include "texture_format.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_vk_texture_format: FAIL: %s\n", what); \
			failures++; \
		} \
	} while (0)

static int dim_is(VkFormat format, uint32_t w, uint32_t h)
{
	uint32_t bw = 0, bh = 0;
	granite_format_block_dim(format, &bw, &bh);
	return bw == w && bh == h;
}

int main(void)
{
	granite_texture_layout_t layout;
	VkBufferImageCopy copies[GRANITE_TEXTURE_MAX_LEVELS];
	uint8_t buffer[4096];
	uint32_t i;
	int ok;

	/* the formats parallel-rdp uploads and reads back, and a few around them */
	CHECK(granite_format_block_size(VK_FORMAT_R8_UNORM, 0) == 1, "R8 is a byte");
	CHECK(granite_format_block_size(VK_FORMAT_R8G8_UNORM, 0) == 2, "RG8 two");
	CHECK(granite_format_block_size(VK_FORMAT_R5G5B5A1_UNORM_PACK16, 0) == 2, "5551 two");
	CHECK(granite_format_block_size(VK_FORMAT_R8G8B8A8_UNORM, 0) == 4, "RGBA8 four");
	CHECK(granite_format_block_size(VK_FORMAT_R16G16_SFLOAT, 0) == 4, "RG16F four");
	CHECK(granite_format_block_size(VK_FORMAT_R16G16B16A16_SFLOAT, 0) == 8, "RGBA16F eight");
	CHECK(granite_format_block_size(VK_FORMAT_R32_UINT, 0) == 4, "R32 four");
	CHECK(granite_format_block_size(VK_FORMAT_D32_SFLOAT, 0) == 4, "D32F four");
	CHECK(granite_format_block_size(VK_FORMAT_D16_UNORM, 0) == 2, "D16 two");
	CHECK(granite_format_block_size(VK_FORMAT_BC1_RGB_UNORM_BLOCK, 0) == 8, "a BC1 block eight");
	CHECK(granite_format_block_size(VK_FORMAT_BC7_UNORM_BLOCK, 0) == 16, "a BC7 block sixteen");
	CHECK(granite_format_block_size(VK_FORMAT_G8_B8R8_2PLANE_420_UNORM, VK_IMAGE_ASPECT_PLANE_0_BIT) == 1
			&& granite_format_block_size(VK_FORMAT_G8_B8R8_2PLANE_420_UNORM, VK_IMAGE_ASPECT_PLANE_1_BIT) == 2,
			"the planes of a two-plane format one and two");

	CHECK(dim_is(VK_FORMAT_R8G8B8A8_UNORM, 1, 1), "a texel is a block");
	CHECK(dim_is(VK_FORMAT_BC3_UNORM_BLOCK, 4, 4), "a BC block is 4x4");
	CHECK(dim_is(VK_FORMAT_ASTC_8x5_UNORM_BLOCK, 8, 5), "an ASTC block its own size");

	CHECK(granite_num_miplevels(1, 1, 1) == 1 && granite_num_miplevels(320, 240, 1) == 9
			&& granite_num_miplevels(4, 1024, 1) == 11 && granite_num_miplevels(2, 2, 64) == 7,
			"levels down to one texel, of the longest side");

	/* 10x6 RGBA8, two layers, every level: 10x6, 5x3, 2x1, 1x1 */
	granite_texture_layout_set(&layout, VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM, 10, 6, 77, 2, 0);
	CHECK(layout.mip_levels == 4 && layout.array_layers == 2 && layout.block_stride == 4, "four levels of two layers");
	CHECK(layout.mips[0].offset == 0 && layout.mips[1].offset == 480 && layout.mips[2].offset == 608
			&& layout.mips[3].offset == 624 && layout.required_size == 632,
			"level after level, each starting on 16 bytes");
	CHECK(layout.mips[1].width == 5 && layout.mips[1].height == 3 && layout.mips[1].depth == 1
			&& layout.mips[3].width == 1 && layout.mips[3].height == 1,
			"each level half the one before, a 2D image without depth");
	CHECK(granite_texture_layout_row_size(&layout, 0) == 40 && granite_texture_layout_layer_size(&layout, 0) == 240
			&& granite_texture_layout_row_size(&layout, 1) == 20,
			"a row and a layer of a level in bytes");
	CHECK(granite_texture_layout_row_stride(&layout, 16) == 64
			&& granite_texture_layout_layer_stride(&layout, 8, 64) == 512,
			"and of source data with its own row length and height");
	layout.buffer = buffer;
	CHECK(granite_texture_layout_data(&layout, 0, 0) == buffer && granite_texture_layout_data(&layout, 1, 0) == buffer + 240
			&& granite_texture_layout_data(&layout, 1, 1) == buffer + 480 + 60,
			"a layer of a level starts after the layers before it");

	memset(copies, 0xff, sizeof(copies));
	granite_texture_layout_copies(&layout, VK_IMAGE_ASPECT_COLOR_BIT, copies);
	ok = 1;
	for (i = 0; i < 4; i++)
		if (     copies[i].bufferOffset != layout.mips[i].offset
				|| copies[i].bufferRowLength != layout.mips[i].row_length
				|| copies[i].bufferImageHeight != layout.mips[i].image_height
				|| copies[i].imageSubresource.aspectMask != VK_IMAGE_ASPECT_COLOR_BIT
				|| copies[i].imageSubresource.mipLevel != i
				|| copies[i].imageSubresource.baseArrayLayer != 0
				|| copies[i].imageSubresource.layerCount != 2
				|| copies[i].imageOffset.x != 0 || copies[i].imageOffset.y != 0 || copies[i].imageOffset.z != 0
				|| copies[i].imageExtent.width != layout.mips[i].width
				|| copies[i].imageExtent.height != layout.mips[i].height
				|| copies[i].imageExtent.depth != 1)
			ok = 0;
	CHECK(ok, "one copy a level, covering it and both layers");
	CHECK(copies[4].bufferOffset == (VkDeviceSize)-1, "and none past the last");

	/* compressed: blocks, not texels */
	granite_texture_layout_set(&layout, VK_IMAGE_TYPE_2D, VK_FORMAT_BC1_RGB_UNORM_BLOCK, 10, 6, 1, 1, 1);
	CHECK(layout.mip_levels == 1 && layout.mips[0].block_row_length == 3 && layout.mips[0].block_image_height == 2
			&& layout.mips[0].row_length == 12 && layout.mips[0].image_height == 8 && layout.required_size == 48,
			"a compressed level is whole blocks");

	/* one dimension, and three */
	granite_texture_layout_set(&layout, VK_IMAGE_TYPE_1D, VK_FORMAT_R8_UNORM, 9, 50, 50, 3, 1);
	CHECK(layout.mips[0].height == 1 && layout.mips[0].depth == 1 && layout.required_size == 27,
			"a 1D image has neither height nor depth");
	granite_texture_layout_set(&layout, VK_IMAGE_TYPE_3D, VK_FORMAT_R8_UNORM, 4, 4, 4, 9, 0);
	CHECK(layout.array_layers == 1 && layout.mip_levels == 3 && layout.mips[1].depth == 2
			&& layout.mips[1].offset == 64 && layout.required_size == 64 + 8 + 8 + 1,
			"a 3D image has depth, halved with the rest, and one layer");

	/* more levels than there are */
	granite_texture_layout_set(&layout, VK_IMAGE_TYPE_2D, VK_FORMAT_R8_UNORM, 4, 4, 1, 1, 40);
	CHECK(layout.mip_levels == GRANITE_TEXTURE_MAX_LEVELS, "never more levels than the layout holds");

	if (failures)
	{
		fprintf(stderr, "test_vk_texture_format: %u failed\n", failures);
		return 1;
	}
	printf("test_vk_texture_format: ok\n");
	return 0;
}
