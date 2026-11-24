/* parallel-rdp's renderer buffers (parallel-rdp/renderer_buffers.c), on a
 * stand-in device.
 *
 * The per-flush streams: where the device's own memory maps for the host
 * the CPU writes into the buffer the GPU reads, and an upload copies
 * nothing (none at all for a stream built in place); where it does not,
 * each stream has a host buffer of its own, and an upload copies what
 * was written, stream by stream, followed by one barrier.
 *
 * The fixed buffers are sized for the caps, and those of the split
 * rasterizer (the indirect dispatch buffer too) are not made for the
 * ubershader. The upscaled RDRAM is sized for the factor and the
 * super-sampled write mask, and its hidden RDRAM filled with full
 * coverage; at 1x there is none. Everything made goes back. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <boolean.h>

#include "renderer_buffers.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_renderer_buffers: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

#include "vk_test_device.h"

static const VkDeviceSize stream_sizes[RDP_STREAM_BUFFERS] = {
	256 * 64, 256 * 128, 256 * 32, 256 * 16, 64 * 64, 64 * 32, 256 * 16, 256 * 4, 256 * 8, 16384 * 8
};

static VkDeviceSize size_of(const granite_buffer_t *buffer)
{
	return buffer_size(buffer->buffer);
}

static void open_device(bool split_memory)
{
	fill_context();
	if (split_memory)
	{
		/* device memory the host cannot map, and host memory */
		context.mem_props.memoryTypeCount = 2;
		context.mem_props.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		context.mem_props.memoryTypes[1].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
				VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		context.mem_props.memoryTypes[1].heapIndex = 0;
		buffer_memory_types = 3;
	}
	else
		buffer_memory_types = 1;
	granite_device_init(&dev);
	granite_device_set_context(&dev, &context);
}

static void *mapped(const granite_buffer_t *buffer)
{
	return granite_device_map_host_buffer(&dev, buffer, GRANITE_MEMORY_ACCESS_WRITE_BIT, 0, buffer->info.size);
}

static void test_streams_shared(void)
{
	rdp_stream_instance_t inst;
	granite_command_buffer_t *cb;
	const void *data[RDP_STREAM_BUFFERS];
	VkDeviceSize bytes[RDP_STREAM_BUFFERS];
	static uint8_t cache[64 * 64];
	unsigned i, copies, barriers;

	open_device(false);
	memset(&inst, 0, sizeof(inst));
	CHECK(rdp_stream_instance_init(&inst, &dev, stream_sizes), "made");
	for (i = 0; i < RDP_STREAM_BUFFERS; i++)
	{
		CHECK(inst.gpu.buffers[i] && inst.gpu.buffers[i] == inst.cpu.buffers[i], "one buffer for both");
		CHECK(inst.gpu.is_host[i] && inst.cpu.is_host[i], "mapped");
		CHECK(size_of(inst.gpu.buffers[i]) == stream_sizes[i], "its size");
	}
	CHECK(inst.gpu.span_info_jobs_view != NULL && inst.cpu.span_info_jobs_view == NULL, "the GPU's view");

	/* streams built in place, a state cache elsewhere */
	for (i = 0; i < RDP_STREAM_BUFFERS; i++)
	{
		data[i] = mapped(inst.cpu.buffers[i]);
		bytes[i] = 64;
	}
	for (i = 0; i < sizeof(cache); i++)
		cache[i] = (uint8_t)(i * 7);
	data[RDP_STREAM_STATIC_RASTER_STATE] = cache;
	bytes[RDP_STREAM_STATIC_RASTER_STATE] = sizeof(cache);
	bytes[RDP_STREAM_SPAN_INFO_JOBS] = 0;
	memset(mapped(inst.cpu.buffers[RDP_STREAM_STATIC_RASTER_STATE]), 0, sizeof(cache));

	copies = num_buffer_copies;
	barriers = num_memory_barriers;
	cb = granite_device_request_command_buffer(&dev, GRANITE_QUEUE_COMPUTE);
	rdp_stream_instance_upload(&inst, &dev, cb, data, bytes);
	granite_device_submit(&dev, cb, NULL, 0, NULL);
	CHECK(num_buffer_copies == copies, "nothing copied");
	CHECK(num_memory_barriers == barriers, "no barrier");
	CHECK(memcmp(mapped(inst.cpu.buffers[RDP_STREAM_STATIC_RASTER_STATE]), cache, sizeof(cache)) == 0,
	      "the cache written where the GPU reads");

	rdp_stream_instance_free(&inst);
	CHECK(inst.gpu.buffers[0] == NULL && inst.gpu.span_info_jobs_view == NULL, "freed");
	granite_device_free(&dev);
}

static void test_streams_split(void)
{
	rdp_stream_instance_t inst;
	granite_command_buffer_t *cb;
	const void *data[RDP_STREAM_BUFFERS];
	VkDeviceSize bytes[RDP_STREAM_BUFFERS];
	static uint8_t cache[64 * 32];
	unsigned i, copies, barriers, seen = 0;

	open_device(true);
	memset(&inst, 0, sizeof(inst));
	CHECK(rdp_stream_instance_init(&inst, &dev, stream_sizes), "made");
	for (i = 0; i < RDP_STREAM_BUFFERS; i++)
	{
		CHECK(inst.gpu.buffers[i] && inst.cpu.buffers[i] && inst.gpu.buffers[i] != inst.cpu.buffers[i],
		      "a host buffer of its own");
		CHECK(!inst.gpu.is_host[i] && inst.cpu.is_host[i], "only the host's mapped");
	}

	for (i = 0; i < RDP_STREAM_BUFFERS; i++)
	{
		data[i] = mapped(inst.cpu.buffers[i]);
		bytes[i] = 0;
	}
	for (i = 0; i < sizeof(cache); i++)
		cache[i] = (uint8_t)(i * 3 + 1);
	bytes[RDP_STREAM_TRIANGLE_SETUP] = 640;
	data[RDP_STREAM_DEPTH_BLEND_STATE] = cache;
	bytes[RDP_STREAM_DEPTH_BLEND_STATE] = sizeof(cache);

	copies = num_buffer_copies;
	barriers = num_memory_barriers;
	cb = granite_device_request_command_buffer(&dev, GRANITE_QUEUE_COMPUTE);
	rdp_stream_instance_upload(&inst, &dev, cb, data, bytes);
	granite_device_submit(&dev, cb, NULL, 0, NULL);
	CHECK(num_buffer_copies == copies + 2, "the two written copied");
	CHECK(num_memory_barriers == barriers + 1, "one barrier");
	for (i = copies; i < num_buffer_copies && i < MAX_BUFFER_COPIES; i++)
	{
		if (buffer_copies[i].dst == inst.gpu.buffers[RDP_STREAM_TRIANGLE_SETUP]->buffer)
		{
			CHECK(buffer_copies[i].src == inst.cpu.buffers[RDP_STREAM_TRIANGLE_SETUP]->buffer &&
			      buffer_copies[i].region.size == 640, "the triangles");
			seen |= 1;
		}
		if (buffer_copies[i].dst == inst.gpu.buffers[RDP_STREAM_DEPTH_BLEND_STATE]->buffer)
		{
			CHECK(buffer_copies[i].src == inst.cpu.buffers[RDP_STREAM_DEPTH_BLEND_STATE]->buffer &&
			      buffer_copies[i].region.size == sizeof(cache), "the depth blend states");
			seen |= 2;
		}
	}
	CHECK(seen == 3, "each to its own");
	CHECK(memcmp(mapped(inst.cpu.buffers[RDP_STREAM_DEPTH_BLEND_STATE]), cache, sizeof(cache)) == 0,
	      "the cache written to the host buffer");

	/* nothing written: nothing copied, no barrier */
	for (i = 0; i < RDP_STREAM_BUFFERS; i++)
		bytes[i] = 0;
	copies = num_buffer_copies;
	barriers = num_memory_barriers;
	cb = granite_device_request_command_buffer(&dev, GRANITE_QUEUE_COMPUTE);
	rdp_stream_instance_upload(&inst, &dev, cb, data, bytes);
	granite_device_submit(&dev, cb, NULL, 0, NULL);
	CHECK(num_buffer_copies == copies && num_memory_barriers == barriers, "an empty flush");

	rdp_stream_instance_free(&inst);
	granite_device_free(&dev);
}

static uint8_t lut[0x8000];

static void test_fixed(void)
{
	rdp_renderer_buffers_t b;
	rdp_renderer_caps_t caps;
	/* (small: the stand-in's memory is real) */
	const VkDeviceSize rdram = 64 << 10, hidden = 32 << 10;
	unsigned fills;

	open_device(false);
	memset(&b, 0, sizeof(b));

	/* 1x, split rasterizer, 8-bit storage, two static raster states'
	 * slices to a binding */
	rdp_caps_init(&caps);
	rdp_caps_init_limits(&caps, 1, false, (VkDeviceSize)16 * 0x8000 * 2);
	CHECK(caps.max_static_raster_states == 2, "two states");
	CHECK(rdp_renderer_buffers_init(&b, &dev, &caps, 4096 * 4, 2 * 1000, lut, sizeof(lut)), "1x");
	CHECK(size_of(b.tmem_instances) == 4096 * 4 && size_of(b.span_setups) == 2000, "as asked");
	CHECK(size_of(b.blender_divider_lut) == sizeof(lut) && b.blender_divider != NULL, "the divider");
	/* 128 tiles across and down */
	CHECK(size_of(b.tile_binning) == 4 * 8 * 128 * 128, "a bit per primitive per tile");
	CHECK(size_of(b.tile_binning_coarse) == 4 * 128 * 128, "a word per tile");
	CHECK(size_of(b.indirect_dispatch) == 16 * 64, "a dispatch per state");
	CHECK(size_of(b.per_tile_offsets) == 4 * 8 * 128 * 128, "offsets");
	CHECK(size_of(b.tile_work_list) == (VkDeviceSize)16 * 2 * 0x8000, "the work list");
	CHECK(size_of(b.per_tile_shaded_color) == (VkDeviceSize)4 * 0x8000 * 64, "colour");
	CHECK(size_of(b.per_tile_shaded_depth) == (VkDeviceSize)4 * 0x8000 * 64, "depth");
	CHECK(size_of(b.per_tile_shaded_alpha) == (VkDeviceSize)0x8000 * 64, "alpha");
	CHECK(size_of(b.per_tile_shaded_coverage) == (VkDeviceSize)0x8000 * 64, "coverage");

	/* without 8-bit storage, coverage shares a 16-bit element */
	caps.no_storage_8bit = true;
	CHECK(rdp_renderer_buffers_init(&b, &dev, &caps, 4096, 1000, lut, sizeof(lut)), "1x, 16-bit");
	CHECK(size_of(b.per_tile_shaded_coverage) == (VkDeviceSize)2 * 0x8000 * 64, "coverage, 16-bit");
	/* and without 16-bit storage, a 32-bit one */
	caps.no_storage_16bit = true;
	CHECK(rdp_renderer_buffers_init(&b, &dev, &caps, 4096, 1000, lut, sizeof(lut)), "1x, 32-bit");
	CHECK(size_of(b.per_tile_shaded_coverage) == (VkDeviceSize)4 * 0x8000 * 64, "coverage, 32-bit");
	caps.no_storage_16bit = false;
	caps.no_storage_8bit = false;

	/* the ubershader: none of the split rasterizer's */
	caps.ubershader = true;
	CHECK(rdp_renderer_buffers_init(&b, &dev, &caps, 4096, 1000, lut, sizeof(lut)), "ubershader");
	CHECK(b.tile_binning && b.tile_binning_coarse && b.tmem_instances, "binning still");
	CHECK(!b.indirect_dispatch && !b.per_tile_offsets && !b.tile_work_list && !b.per_tile_shaded_color &&
	      !b.per_tile_shaded_depth && !b.per_tile_shaded_alpha && !b.per_tile_shaded_coverage,
	      "nothing of the split rasterizer");
	caps.ubershader = false;

	/* upscaled: 1x has none */
	caps.upscaling = 1;
	CHECK(rdp_renderer_buffers_init_upscaled(&b, &dev, &caps, rdram, hidden), "1x");
	CHECK(!b.upscaling_reference_rdram && !b.upscaling_multisampled_rdram &&
	      !b.upscaling_multisampled_hidden_rdram, "none at 1x");

	caps.upscaling = 4;
	fills = num_buffer_fills;
	CHECK(rdp_renderer_buffers_init_upscaled(&b, &dev, &caps, rdram, hidden), "4x");
	CHECK(size_of(b.upscaling_reference_rdram) == rdram, "the reference");
	CHECK(size_of(b.upscaling_multisampled_rdram) == rdram * 16, "16 samples");
	CHECK(size_of(b.upscaling_multisampled_hidden_rdram) == hidden * 16, "hidden, 16 samples");
	CHECK(num_buffer_fills == fills + 1 && last_fill_buffer == b.upscaling_multisampled_hidden_rdram->buffer &&
	      last_fill_value == 0x03030303u, "full coverage");

	caps.super_sample_readback = true;
	caps.no_storage_8bit = true;
	CHECK(rdp_renderer_buffers_init_upscaled(&b, &dev, &caps, rdram, hidden), "4x, super-sampled");
	CHECK(size_of(b.upscaling_multisampled_rdram) == rdram * 16 + 2 * 1024 * 1024 / 8, "and the write mask");
	CHECK(last_fill_value == 0x00030003u, "full coverage in 16 bits");

	rdp_renderer_buffers_free(&b);
	CHECK(!b.tmem_instances && !b.blender_divider && !b.upscaling_multisampled_rdram, "freed");
	granite_device_free(&dev);
}

int main(void)
{
	test_streams_shared();
	test_streams_split();
	test_fixed();
	CHECK(unmade == made, "everything made went back");

	if (failures)
	{
		fprintf(stderr, "test_renderer_buffers: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_renderer_buffers: all checks passed\n");
	return 0;
}
