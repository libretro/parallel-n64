/* Copyright (c) 2020 Themaister
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
#include "renderer_buffers.h"
#include "buffer_create.h"
#include "memory_allocator.h"

static granite_buffer_t *make(granite_device_t *device, granite_buffer_domain_t domain,
		VkBufferUsageFlags usage, uint32_t misc, VkDeviceSize size, const void *initial)
{
	granite_buffer_info_t info;
	info.domain = domain;
	info.size = size;
	info.usage = usage;
	info.misc = misc;
	return granite_device_create_buffer(device, &info, initial);
}

static void drop(granite_buffer_t **buffer)
{
	if (*buffer)
		granite_buffer_unref(*buffer);
	*buffer = NULL;
}

static void drop_view(granite_buffer_view_t **view)
{
	if (*view)
		granite_buffer_view_unref(*view);
	*view = NULL;
}

/* ---- the streams ---- */

void rdp_stream_instance_free(rdp_stream_instance_t *inst)
{
	unsigned i;
	for (i = 0; i < RDP_STREAM_BUFFERS; i++)
	{
		drop(&inst->cpu.buffers[i]);
		drop(&inst->gpu.buffers[i]);
	}
	drop_view(&inst->gpu.span_info_jobs_view);
	drop_view(&inst->cpu.span_info_jobs_view);
	memset(inst, 0, sizeof(*inst));
}

bool rdp_stream_instance_init(rdp_stream_instance_t *inst, granite_device_t *device,
		const VkDeviceSize sizes[RDP_STREAM_BUFFERS])
{
	granite_buffer_view_info_t view;
	unsigned i;

	rdp_stream_instance_free(inst);
	for (i = 0; i < RDP_STREAM_BUFFERS; i++)
	{
		granite_buffer_t *gpu = make(device, GRANITE_BUFFER_DOMAIN_LINKED_DEVICE_HOST_PREFER_DEVICE,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,
				0, sizes[i], NULL);
		if (!gpu)
			goto fail;
		inst->gpu.buffers[i] = gpu;
		/* mapped for good where it can be */
		inst->gpu.is_host[i] = granite_device_map_host_buffer(device, gpu, 0, 0, sizes[i]) != NULL;

		if (inst->gpu.is_host[i])
		{
			/* the CPU writes where the GPU reads */
			granite_buffer_ref(gpu);
			inst->cpu.buffers[i] = gpu;
			inst->cpu.is_host[i] = true;
		}
		else
		{
			granite_buffer_t *cpu = make(device, GRANITE_BUFFER_DOMAIN_HOST, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
					0, sizes[i], NULL);
			if (!cpu)
				goto fail;
			inst->cpu.buffers[i] = cpu;
			inst->cpu.is_host[i] = granite_device_map_host_buffer(device, cpu, 0, 0, sizes[i]) != NULL;
		}
	}

	view.buffer = inst->gpu.buffers[RDP_STREAM_SPAN_INFO_JOBS];
	view.format = VK_FORMAT_R16G16B16A16_UINT;
	view.offset = 0;
	view.range = sizes[RDP_STREAM_SPAN_INFO_JOBS];
	if (!(inst->gpu.span_info_jobs_view = granite_device_create_buffer_view(device, &view)))
		goto fail;
	return true;

fail:
	rdp_stream_instance_free(inst);
	return false;
}

void rdp_stream_instance_upload(const rdp_stream_instance_t *inst, granite_device_t *device,
		granite_command_buffer_t *cb, const void *const data[RDP_STREAM_BUFFERS],
		const VkDeviceSize bytes[RDP_STREAM_BUFFERS])
{
	bool copied = false;
	unsigned i;

	for (i = 0; i < RDP_STREAM_BUFFERS; i++)
	{
		const granite_buffer_t *cpu = inst->cpu.buffers[i];
		const granite_buffer_t *gpu = inst->gpu.buffers[i];
		void *mapped;

		if (!bytes[i])
			continue;
		/* The streams are built in place; only the state caches are
		 * copied. */
		mapped = granite_device_map_host_buffer(device, cpu, GRANITE_MEMORY_ACCESS_WRITE_BIT, 0, cpu->info.size);
		if (mapped != data[i])
			memcpy(mapped, data[i], (size_t)bytes[i]);
		granite_device_unmap_host_buffer(device, cpu, GRANITE_MEMORY_ACCESS_WRITE_BIT, 0, bytes[i]);
		if (gpu != cpu)
		{
			VkBufferCopy region;
			region.srcOffset = 0;
			region.dstOffset = 0;
			region.size = bytes[i];
			granite_cb_copy_buffer(cb, gpu, cpu, &region, 1);
			copied = true;
		}
	}

	if (copied)
	{
		VkMemoryBarrier barrier;
		memset(&barrier, 0, sizeof(barrier));
		barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		granite_cb_barrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				1, &barrier, 0, NULL, 0, NULL);
	}
}

/* ---- the fixed buffers ---- */

void rdp_renderer_buffers_free(rdp_renderer_buffers_t *b)
{
	drop(&b->tmem_instances);
	drop(&b->span_setups);
	drop_view(&b->blender_divider);
	drop(&b->blender_divider_lut);
	drop(&b->tile_binning);
	drop(&b->tile_binning_coarse);
	drop(&b->indirect_dispatch);
	drop(&b->per_tile_offsets);
	drop(&b->tile_work_list);
	drop(&b->per_tile_shaded_color);
	drop(&b->per_tile_shaded_depth);
	drop(&b->per_tile_shaded_alpha);
	drop(&b->per_tile_shaded_coverage);
	drop(&b->upscaling_reference_rdram);
	drop(&b->upscaling_multisampled_rdram);
	drop(&b->upscaling_multisampled_hidden_rdram);
}

/* A zeroed storage buffer on the device */
static granite_buffer_t *storage(granite_device_t *device, VkDeviceSize size)
{
	return make(device, GRANITE_BUFFER_DOMAIN_DEVICE, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			GRANITE_BUFFER_MISC_ZERO_INITIALIZE_BIT, size, NULL);
}

bool rdp_renderer_buffers_init(rdp_renderer_buffers_t *b, granite_device_t *device,
		const rdp_renderer_caps_t *caps, VkDeviceSize tmem_instances_size, VkDeviceSize span_setups_size,
		const uint8_t *blender_lut, VkDeviceSize blender_lut_size)
{
	const VkDeviceSize tiles = (VkDeviceSize)(caps->max_width / RDP_CAPS_TILE_WIDTH) *
			(caps->max_height / RDP_CAPS_TILE_HEIGHT);
	const VkDeviceSize tile_pixels = (VkDeviceSize)caps->max_num_tile_instances *
			RDP_CAPS_TILE_WIDTH * RDP_CAPS_TILE_HEIGHT;
	granite_buffer_view_info_t view;

	rdp_renderer_buffers_free(b);

	b->tmem_instances = storage(device, tmem_instances_size);
	b->span_setups = storage(device, span_setups_size);
	b->blender_divider_lut = make(device, GRANITE_BUFFER_DOMAIN_DEVICE, VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,
			0, blender_lut_size, blender_lut);
	if (!b->tmem_instances || !b->span_setups || !b->blender_divider_lut)
		goto fail;
	view.buffer = b->blender_divider_lut;
	view.format = VK_FORMAT_R8_UINT;
	view.offset = 0;
	view.range = blender_lut_size;
	if (!(b->blender_divider = granite_device_create_buffer_view(device, &view)))
		goto fail;

	/* a bit per primitive (32 to a word) per tile, and a word per tile */
	b->tile_binning = storage(device, sizeof(uint32_t) * (RDP_CAPS_MAX_PRIMITIVES / 32) * tiles);
	b->tile_binning_coarse = storage(device, sizeof(uint32_t) * tiles);
	if (!b->tile_binning || !b->tile_binning_coarse)
		goto fail;

	if (!caps->ubershader)
	{
		/* (cleared by the first flush's command buffer) */
		b->indirect_dispatch = make(device, GRANITE_BUFFER_DOMAIN_DEVICE,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, 0,
				4 * sizeof(uint32_t) * RDP_CAPS_MAX_STATIC_RASTER_STATES, NULL);
		b->per_tile_offsets = storage(device, sizeof(uint32_t) * (RDP_CAPS_MAX_PRIMITIVES / 32) * tiles);
		b->tile_work_list = storage(device, (VkDeviceSize)RDP_CAPS_TILE_RASTER_WORK_SIZE *
				caps->max_static_raster_states * caps->max_num_tile_instances);
		b->per_tile_shaded_color = storage(device, sizeof(uint32_t) * tile_pixels);
		b->per_tile_shaded_depth = storage(device, sizeof(uint32_t) * tile_pixels);
		b->per_tile_shaded_alpha = storage(device, tile_pixels);
		/* Without 8-bit storage, coverage and shade alpha share one 16-bit
		 * element in the coverage buffer (rasterizer.comp). */
		b->per_tile_shaded_coverage = storage(device, tile_pixels * (caps->no_storage_8bit ? sizeof(uint16_t) : 1));
		if (!b->indirect_dispatch || !b->per_tile_offsets || !b->tile_work_list || !b->per_tile_shaded_color ||
		    !b->per_tile_shaded_depth || !b->per_tile_shaded_alpha || !b->per_tile_shaded_coverage)
			goto fail;
	}
	return true;

fail:
	rdp_renderer_buffers_free(b);
	return false;
}

bool rdp_renderer_buffers_init_upscaled(rdp_renderer_buffers_t *b, granite_device_t *device,
		const rdp_renderer_caps_t *caps, VkDeviceSize rdram_size, VkDeviceSize hidden_rdram_size)
{
	const VkDeviceSize samples = (VkDeviceSize)caps->upscaling * caps->upscaling;
	granite_command_buffer_t *cb;
	VkDeviceSize size;

	drop(&b->upscaling_reference_rdram);
	drop(&b->upscaling_multisampled_rdram);
	drop(&b->upscaling_multisampled_hidden_rdram);
	if (caps->upscaling <= 1)
		return true;

	b->upscaling_reference_rdram = storage(device, rdram_size);

	/* Super-sampling carries forward a write mask per native pixel, 2
	 * bits (colour, depth): the resolve writes a pixel only where it was
	 * written, against races. The SSAA resolve turns it into the RDRAM
	 * write mask too where RDRAM is not the CPU's. */
	size = rdram_size * samples;
	if (caps->super_sample_readback)
		size += 2 * RDP_CAPS_MAX_WIDTH * RDP_CAPS_MAX_HEIGHT / 8;
	b->upscaling_multisampled_rdram = storage(device, size);
	b->upscaling_multisampled_hidden_rdram = storage(device, hidden_rdram_size * samples);
	if (!b->upscaling_reference_rdram || !b->upscaling_multisampled_rdram || !b->upscaling_multisampled_hidden_rdram)
		goto fail;

	/* On the queue the renderer and VI use, and ordered before them on
	 * every queue: a plain submit on the graphics queue could land after
	 * the first frame's work on the compute queue and overwrite it. */
	if (!(cb = granite_device_request_command_buffer(device, GRANITE_QUEUE_COMPUTE)))
		goto fail;
	granite_cb_fill_buffer(cb, b->upscaling_multisampled_hidden_rdram,
			caps->no_storage_8bit ? 0x00030003u : 0x03030303u, 0, VK_WHOLE_SIZE);
	granite_device_submit_initialization(device, cb, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
	return true;

fail:
	drop(&b->upscaling_reference_rdram);
	drop(&b->upscaling_multisampled_rdram);
	drop(&b->upscaling_multisampled_hidden_rdram);
	return false;
}
