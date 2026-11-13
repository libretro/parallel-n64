/* parallel-rdp's command buffer operations on images
 * (vulkan/command_ops.c), through a stand-in device table.
 *
 * A format's aspects are colour, depth, stencil or depth and stencil.
 * The stages a barrier waits for are narrowed only when asked and only
 * for ALL_GRAPHICS.
 *
 * An image barrier covers all levels and layers of the format's aspects.
 * A clear goes to the colour or the depth and
 * stencil call, in the layout the image is kept in. A blit is a call a
 * layer. */
#include <stdio.h>
#include <string.h>
#include <boolean.h>

#include "command_ops.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_vk_command_ops: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

#define H(x) ((size_t)(x))
#define CMD  ((VkCommandBuffer)H(9))
#define MAX_CALLS 64

/* every barrier and blit, in the order made */
static unsigned num_barrier_calls, num_blits, color_clears, depth_clears;
static VkPipelineStageFlags barrier_src[MAX_CALLS], barrier_dst[MAX_CALLS];
static uint32_t barrier_counts[MAX_CALLS];
static VkImageMemoryBarrier barriers[MAX_CALLS][2];
static VkImageBlit blits[MAX_CALLS];
static VkImageLayout blit_src_layout, blit_dst_layout, clear_layout;
static VkImage blit_src, blit_dst;
static VkFilter blit_filter;
static VkImageSubresourceRange clear_range;
static VkClearValue clear_value;
/* call sequence: 'b' a barrier, 'B' a blit */
static char sequence[MAX_CALLS * 2 + 1];
static unsigned sequence_len;

static void VKAPI_CALL fake_barrier(VkCommandBuffer cmd, VkPipelineStageFlags src, VkPipelineStageFlags dst,
		VkDependencyFlags flags, uint32_t memory_count, const VkMemoryBarrier *memory, uint32_t buffer_count,
		const VkBufferMemoryBarrier *buffers, uint32_t image_count, const VkImageMemoryBarrier *images)
{
	uint32_t i;
	(void)memory; (void)buffers;
	CHECK(cmd == CMD && flags == 0 && memory_count == 0 && buffer_count == 0, "an image barrier and nothing else");
	if (num_barrier_calls < MAX_CALLS)
	{
		barrier_src[num_barrier_calls] = src;
		barrier_dst[num_barrier_calls] = dst;
		barrier_counts[num_barrier_calls] = image_count;
		for (i = 0; i < image_count && i < 2; i++)
			barriers[num_barrier_calls][i] = images[i];
	}
	num_barrier_calls++;
	if (sequence_len < MAX_CALLS * 2)
		sequence[sequence_len++] = 'b';
}

static void VKAPI_CALL fake_blit(VkCommandBuffer cmd, VkImage src, VkImageLayout src_layout, VkImage dst,
		VkImageLayout dst_layout, uint32_t count, const VkImageBlit *regions, VkFilter filter)
{
	CHECK(cmd == CMD && count == 1, "one region a blit");
	if (num_blits < MAX_CALLS)
		blits[num_blits] = regions[0];
	num_blits++;
	blit_src = src;
	blit_dst = dst;
	blit_src_layout = src_layout;
	blit_dst_layout = dst_layout;
	blit_filter = filter;
	if (sequence_len < MAX_CALLS * 2)
		sequence[sequence_len++] = 'B';
}

static void VKAPI_CALL fake_clear_color(VkCommandBuffer cmd, VkImage image, VkImageLayout layout,
		const VkClearColorValue *value, uint32_t count, const VkImageSubresourceRange *ranges)
{
	(void)cmd; (void)image;
	CHECK(count == 1, "one range");
	color_clears++;
	clear_layout = layout;
	clear_range = ranges[0];
	clear_value.color = *value;
}

static void VKAPI_CALL fake_clear_depth(VkCommandBuffer cmd, VkImage image, VkImageLayout layout,
		const VkClearDepthStencilValue *value, uint32_t count, const VkImageSubresourceRange *ranges)
{
	(void)cmd; (void)image;
	CHECK(count == 1, "one range");
	depth_clears++;
	clear_layout = layout;
	clear_range = ranges[0];
	clear_value.depthStencil = *value;
}

static struct VolkDeviceTable table;

static void reset(void)
{
	num_barrier_calls = num_blits = color_clears = depth_clears = 0;
	sequence_len = 0;
	memset(sequence, 0, sizeof(sequence));
}

static granite_image_ref_t image(VkFormat format, uint32_t w, uint32_t h, uint32_t levels, uint32_t layers)
{
	granite_image_ref_t ref;
	memset(&ref, 0, sizeof(ref));
	ref.image = (VkImage)H(0x100 + w);
	ref.format = format;
	ref.width = w;
	ref.height = h;
	ref.depth = 1;
	ref.levels = levels;
	ref.layers = layers;
	return ref;
}

static void test_helpers(void)
{
	const VkPipelineStageFlags narrowed = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;

	CHECK(granite_format_aspect_mask(VK_FORMAT_R8G8B8A8_UNORM) == VK_IMAGE_ASPECT_COLOR_BIT, "a colour format");
	CHECK(granite_format_aspect_mask(VK_FORMAT_D32_SFLOAT) == VK_IMAGE_ASPECT_DEPTH_BIT &&
			granite_format_aspect_mask(VK_FORMAT_D16_UNORM) == VK_IMAGE_ASPECT_DEPTH_BIT &&
			granite_format_aspect_mask(VK_FORMAT_X8_D24_UNORM_PACK32) == VK_IMAGE_ASPECT_DEPTH_BIT, "depth formats");
	CHECK(granite_format_aspect_mask(VK_FORMAT_S8_UINT) == VK_IMAGE_ASPECT_STENCIL_BIT, "a stencil format");
	CHECK(granite_format_aspect_mask(VK_FORMAT_D24_UNORM_S8_UINT) ==
			(VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT) &&
			granite_format_aspect_mask(VK_FORMAT_D32_SFLOAT_S8_UINT) ==
			(VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT), "depth and stencil formats");
	CHECK(granite_format_aspect_mask(VK_FORMAT_UNDEFINED) == 0, "no format, no aspect");

	CHECK(granite_barrier_src_stages(VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, false) == VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
			"stages are left alone unless asked");
	CHECK(granite_barrier_src_stages(VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, true) ==
			(narrowed | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT), "ALL_GRAPHICS is narrowed to the fragment stages");
	CHECK(granite_barrier_src_stages(VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, true) ==
			(VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT), "other stages are not");

}

static void test_barriers(void)
{
	granite_image_ref_t color = image(VK_FORMAT_R8G8B8A8_UNORM, 64, 32, 3, 2);
	granite_image_ref_t depth = image(VK_FORMAT_D24_UNORM_S8_UINT, 64, 32, 1, 1);
	const VkImageMemoryBarrier *b;

	reset();
	granite_cmd_image_barrier(&table, CMD, &color, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
	b = &barriers[0][0];
	CHECK(num_barrier_calls == 1 && barrier_counts[0] == 1 && barrier_src[0] == VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT &&
			barrier_dst[0] == VK_PIPELINE_STAGE_TRANSFER_BIT, "an image barrier, with its stages");
	CHECK(b->sType == VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER && b->image == color.image &&
			b->oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && b->newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
			b->srcAccessMask == 0 && b->dstAccessMask == VK_ACCESS_TRANSFER_WRITE_BIT, "its layouts and access");
	CHECK(b->subresourceRange.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT && b->subresourceRange.baseMipLevel == 0 &&
			b->subresourceRange.levelCount == 3 && b->subresourceRange.baseArrayLayer == 0 &&
			b->subresourceRange.layerCount == 2, "all levels and layers");
	CHECK(b->srcQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED && b->dstQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED,
			"no change of owner");
	granite_cmd_image_barrier(&table, CMD, &depth, VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
	CHECK(barriers[1][0].subresourceRange.aspectMask == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT),
			"a depth and stencil image: both aspects");
}

static void test_clear_blit(void)
{
	granite_image_ref_t color = image(VK_FORMAT_R8G8B8A8_UNORM, 64, 32, 3, 2);
	granite_image_ref_t depth = image(VK_FORMAT_D32_SFLOAT, 16, 16, 1, 1);
	granite_image_ref_t dst = image(VK_FORMAT_R8G8B8A8_UNORM, 128, 64, 1, 4);
	VkOffset3D src_offset, src_extent, dst_offset, dst_extent;
	VkClearValue value;

	reset();
	memset(&value, 0, sizeof(value));
	value.color.float32[1] = 0.5f;
	granite_cmd_clear_image(&table, CMD, &color, &value, VK_IMAGE_ASPECT_COLOR_BIT);
	CHECK(color_clears == 1 && depth_clears == 0 && clear_value.color.float32[1] == 0.5f &&
			clear_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, "a colour image is cleared as colour");
	CHECK(clear_range.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT && clear_range.levelCount == 3 &&
			clear_range.layerCount == 2 && clear_range.baseMipLevel == 0, "all its levels and layers");
	value.depthStencil.depth = 0.25f;
	value.depthStencil.stencil = 7;
	depth.general = true;
	granite_cmd_clear_image(&table, CMD, &depth, &value, VK_IMAGE_ASPECT_DEPTH_BIT);
	CHECK(depth_clears == 1 && color_clears == 1 && clear_value.depthStencil.depth == 0.25f &&
			clear_layout == VK_IMAGE_LAYOUT_GENERAL, "a depth image as depth, in the general layout it is kept in");

	/* three layers, from layer 1 of a general image to layer 0 */
	reset();
	color.general = true;
	src_offset.x = 4; src_offset.y = 2; src_offset.z = 0;
	src_extent.x = 32; src_extent.y = 16; src_extent.z = 1;
	dst_offset.x = 0; dst_offset.y = 8; dst_offset.z = 0;
	dst_extent.x = 128; dst_extent.y = 48; dst_extent.z = 1;
	granite_cmd_blit_image(&table, CMD, &dst, &color, &dst_offset, &dst_extent, &src_offset, &src_extent, 0, 1, 0, 1, 3,
			VK_FILTER_NEAREST);
	CHECK(num_blits == 3, "a blit a layer");
	CHECK(blit_src == color.image && blit_dst == dst.image && blit_src_layout == VK_IMAGE_LAYOUT_GENERAL &&
			blit_dst_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && blit_filter == VK_FILTER_NEAREST,
			"between the images, each in the layout it is kept in");
	CHECK(blits[0].srcSubresource.mipLevel == 1 && blits[0].srcSubresource.baseArrayLayer == 1 &&
			blits[0].srcSubresource.layerCount == 1 && blits[0].dstSubresource.mipLevel == 0 &&
			blits[0].dstSubresource.baseArrayLayer == 0 && blits[2].srcSubresource.baseArrayLayer == 3 &&
			blits[2].dstSubresource.baseArrayLayer == 2, "from and to the layers asked for");
	CHECK(blits[1].srcOffsets[0].x == 4 && blits[1].srcOffsets[1].x == 36 && blits[1].srcOffsets[1].y == 18 &&
			blits[1].srcOffsets[1].z == 1 && blits[1].dstOffsets[0].y == 8 && blits[1].dstOffsets[1].x == 128 &&
			blits[1].dstOffsets[1].y == 56, "the extents are sizes from the offsets");
	CHECK(blits[0].srcSubresource.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT &&
			blits[0].dstSubresource.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT, "colour to colour");
}

int main(void)
{
	table.vkCmdPipelineBarrier = fake_barrier;
	table.vkCmdBlitImage = fake_blit;
	table.vkCmdClearColorImage = fake_clear_color;
	table.vkCmdClearDepthStencilImage = fake_clear_depth;

	test_helpers();
	test_barriers();
	test_clear_blit();

	if (failures)
	{
		fprintf(stderr, "test_vk_command_ops: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_vk_command_ops: OK\n");
	return 0;
}
