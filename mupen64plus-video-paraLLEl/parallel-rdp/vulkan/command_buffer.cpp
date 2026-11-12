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

#include "command_buffer.hpp"
#include "device.hpp"
#include "format.hpp"
#include "thread_id.h"
#include "timer.h"
#include <string.h>

using namespace Util;

namespace Vulkan
{
static VkOffset3D add_offset(const VkOffset3D &a, const VkOffset3D &b)
{
	VkOffset3D o = { a.x + b.x, a.y + b.y, a.z + b.z };
	return o;
}

CommandBuffer::CommandBuffer(Device *device_, VkCommandBuffer cmd_, VkPipelineCache cache, Type type_)
    : device(device_)
    , table(device_->get_device_table())
    , cmd(cmd_)
    , type(type_)
{
	pipeline_state.cache = cache;
	// (begins as compute, with everything dirty)
	granite_command_state_init(&state, device->get_device(), &table, cmd);
	begin_compute();
	memset(&pipeline_state.static_state, 0, sizeof(pipeline_state.static_state));

	// Set up extra state which PSO creation depends on implicitly.
	// This needs to affect hashing to make Fossilize path behave as expected.
	auto &features = device->get_device_features();
	pipeline_state.subgroup_size_tag =
			(features.subgroup_size_control_properties.minSubgroupSize << 0) |
			(features.subgroup_size_control_properties.maxSubgroupSize << 8);
}

CommandBuffer::~CommandBuffer()
{
	VK_ASSERT(vbo_block.mapped == nullptr);
	VK_ASSERT(ubo_block.mapped == nullptr);
}

void CommandBuffer::fill_buffer(const Buffer &dst, uint32_t value)
{
	fill_buffer(dst, value, 0, VK_WHOLE_SIZE);
}

void CommandBuffer::fill_buffer(const Buffer &dst, uint32_t value, VkDeviceSize offset, VkDeviceSize size)
{
	table.vkCmdFillBuffer(cmd, dst.get_buffer(), offset, size, value);
}

void CommandBuffer::copy_buffer(const Buffer &dst, VkDeviceSize dst_offset, const Buffer &src, VkDeviceSize src_offset,
                                VkDeviceSize size)
{
	const VkBufferCopy region = {
		src_offset, dst_offset, size,
	};
	table.vkCmdCopyBuffer(cmd, src.get_buffer(), dst.get_buffer(), 1, &region);
}

void CommandBuffer::copy_buffer(const Buffer &dst, const Buffer &src)
{
	VK_ASSERT(dst.get_create_info().size == src.get_create_info().size);
	copy_buffer(dst, 0, src, 0, dst.get_create_info().size);
}

void CommandBuffer::copy_buffer(const Buffer &dst, const Buffer &src, const VkBufferCopy *copies, size_t count)
{
	table.vkCmdCopyBuffer(cmd, src.get_buffer(), dst.get_buffer(), count, copies);
}

void CommandBuffer::copy_buffer_to_image(const Image &image, const Buffer &buffer, unsigned num_blits,
                                         const VkBufferImageCopy *blits)
{
	table.vkCmdCopyBufferToImage(cmd, buffer.get_buffer(),
	                             image.get_image(), image.get_layout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL), num_blits, blits);
}

void CommandBuffer::copy_buffer_to_image(const Image &image, const Buffer &src, VkDeviceSize buffer_offset,
                                         const VkOffset3D &offset, const VkExtent3D &extent, unsigned row_length,
                                         unsigned slice_height, const VkImageSubresourceLayers &subresource)
{
	const VkBufferImageCopy region = {
		buffer_offset,
		row_length, slice_height,
		subresource, offset, extent,
	};
	table.vkCmdCopyBufferToImage(cmd, src.get_buffer(), image.get_image(), image.get_layout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
	                             1, &region);
}

void CommandBuffer::clear_image(const Image &image, const VkClearValue &value)
{
	auto aspect = format_to_aspect_mask(image.get_format());
	clear_image(image, value, aspect);
}

void CommandBuffer::clear_image(const Image &image, const VkClearValue &value, VkImageAspectFlags aspect)
{
	VK_ASSERT(!framebuffer);
	VK_ASSERT(!actual_render_pass);

	VkImageSubresourceRange range = {};
	range.aspectMask = aspect;
	range.baseArrayLayer = 0;
	range.baseMipLevel = 0;
	range.levelCount = image.get_create_info().levels;
	range.layerCount = image.get_create_info().layers;
	if (aspect & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))
	{
		table.vkCmdClearDepthStencilImage(cmd, image.get_image(), image.get_layout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
		                                  &value.depthStencil, 1, &range);
	}
	else
	{
		table.vkCmdClearColorImage(cmd, image.get_image(), image.get_layout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
		                           &value.color, 1, &range);
	}
}

static inline void fixup_src_stage(VkPipelineStageFlags &src_stages, bool fixup)
{
	// ALL_GRAPHICS_BIT waits for vertex as well which causes performance issues on some drivers.
	// It shouldn't matter, but hey.
	//
	// We aren't using vertex with side-effects on relevant hardware so dropping VERTEX_SHADER_BIT is fine.
	if ((src_stages & VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT) != 0 && fixup)
	{
		src_stages &= ~VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
		src_stages |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
		              VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
		              VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	}
}

void CommandBuffer::barrier(VkPipelineStageFlags src_stages, VkAccessFlags src_access, VkPipelineStageFlags dst_stages,
                            VkAccessFlags dst_access)
{
	VK_ASSERT(!actual_render_pass);
	VK_ASSERT(!framebuffer);
	VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
	barrier.srcAccessMask = src_access;
	barrier.dstAccessMask = dst_access;
	fixup_src_stage(src_stages, device->get_workarounds().optimize_all_graphics_barrier);
	table.vkCmdPipelineBarrier(cmd, src_stages, dst_stages, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void CommandBuffer::barrier(VkPipelineStageFlags src_stages, VkPipelineStageFlags dst_stages, unsigned barriers,
                            const VkMemoryBarrier *globals, unsigned buffer_barriers,
                            const VkBufferMemoryBarrier *buffers, unsigned image_barriers,
                            const VkImageMemoryBarrier *images)
{
	VK_ASSERT(!actual_render_pass);
	VK_ASSERT(!framebuffer);
	fixup_src_stage(src_stages, device->get_workarounds().optimize_all_graphics_barrier);
	table.vkCmdPipelineBarrier(cmd, src_stages, dst_stages, 0, barriers, globals, buffer_barriers, buffers, image_barriers, images);
}

// Buffers are always CONCURRENT.
static uint32_t deduce_acquire_release_family_index(Device &device)
{
	uint32_t family = VK_QUEUE_FAMILY_IGNORED;
	auto &queue_info = device.get_queue_info();
	for (auto &i : queue_info.family_indices)
	{
		if (i != VK_QUEUE_FAMILY_IGNORED)
		{
			if (family == VK_QUEUE_FAMILY_IGNORED)
				family = i;
			else if (i != family)
				return VK_QUEUE_FAMILY_IGNORED;
		}
	}

	return family;
}

static uint32_t deduce_acquire_release_family_index(Device &device, const Image &image, uint32_t family_index)
{
	uint32_t family = family_index;
	auto &queue_info = device.get_queue_info();

	if (image.get_create_info().misc & IMAGE_MISC_CONCURRENT_QUEUE_GRAPHICS_BIT)
		if (queue_info.family_indices[QUEUE_INDEX_GRAPHICS] != family)
			return VK_QUEUE_FAMILY_IGNORED;

	if (image.get_create_info().misc &
	    (IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_GRAPHICS_BIT | IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_COMPUTE_BIT))
	{
		if (queue_info.family_indices[QUEUE_INDEX_COMPUTE] != family)
			return VK_QUEUE_FAMILY_IGNORED;
	}

	if (image.get_create_info().misc & IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_TRANSFER_BIT)
		if (queue_info.family_indices[QUEUE_INDEX_COMPUTE] != family)
			return VK_QUEUE_FAMILY_IGNORED;

	return family;
}

void CommandBuffer::release_external_image_barrier(
		const Image &image,
		VkImageLayout old_layout, VkImageLayout new_layout,
		VkPipelineStageFlags src_stage, VkAccessFlags src_access)
{
	VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
	uint32_t family_index = device->get_queue_info().family_indices[device->get_physical_queue_type(type)];

	barrier.image = image.get_image();
	barrier.subresourceRange = {
		format_to_aspect_mask(image.get_format()),
		0, VK_REMAINING_MIP_LEVELS,
		0, VK_REMAINING_ARRAY_LAYERS
	};
	barrier.oldLayout = old_layout;
	barrier.newLayout = new_layout;
	barrier.srcAccessMask = src_access;

	barrier.srcQueueFamilyIndex = deduce_acquire_release_family_index(*device, image, family_index);
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
	table.vkCmdPipelineBarrier(cmd, src_stage, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
	                           0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void CommandBuffer::acquire_external_image_barrier(
		const Image &image,
		VkImageLayout old_layout, VkImageLayout new_layout,
		VkPipelineStageFlags dst_stage, VkAccessFlags dst_access)
{
	VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
	uint32_t family_index = device->get_queue_info().family_indices[device->get_physical_queue_type(type)];

	barrier.image = image.get_image();
	barrier.subresourceRange = {
		format_to_aspect_mask(image.get_format()),
		0, VK_REMAINING_MIP_LEVELS,
		0, VK_REMAINING_ARRAY_LAYERS
	};
	barrier.oldLayout = old_layout;
	barrier.newLayout = new_layout;
	barrier.dstAccessMask = dst_access;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
	barrier.dstQueueFamilyIndex = deduce_acquire_release_family_index(*device, image, family_index);
	table.vkCmdPipelineBarrier(cmd, dst_stage, dst_stage,
	                           0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void CommandBuffer::image_barrier(const Image &image, VkImageLayout old_layout, VkImageLayout new_layout,
                                  VkPipelineStageFlags src_stages, VkAccessFlags src_access,
                                  VkPipelineStageFlags dst_stages, VkAccessFlags dst_access)
{
	VK_ASSERT(!actual_render_pass);
	VK_ASSERT(!framebuffer);
	VK_ASSERT(image.get_create_info().domain != ImageDomain::Transient);

	VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
	barrier.srcAccessMask = src_access;
	barrier.dstAccessMask = dst_access;
	barrier.oldLayout = old_layout;
	barrier.newLayout = new_layout;
	barrier.image = image.get_image();
	barrier.subresourceRange.aspectMask = format_to_aspect_mask(image.get_create_info().format);
	barrier.subresourceRange.levelCount = image.get_create_info().levels;
	barrier.subresourceRange.layerCount = image.get_create_info().layers;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

	fixup_src_stage(src_stages, device->get_workarounds().optimize_all_graphics_barrier);
	table.vkCmdPipelineBarrier(cmd, src_stages, dst_stages, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void CommandBuffer::barrier_prepare_generate_mipmap(const Image &image, VkImageLayout base_level_layout,
                                                    VkPipelineStageFlags src_stage, VkAccessFlags src_access,
                                                    bool need_top_level_barrier)
{
	auto &create_info = image.get_create_info();
	VkImageMemoryBarrier barriers[2] = {};
	VK_ASSERT(create_info.levels > 1);
	(void)create_info;

	for (unsigned i = 0; i < 2; i++)
	{
		barriers[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barriers[i].image = image.get_image();
		barriers[i].subresourceRange.aspectMask = format_to_aspect_mask(image.get_format());
		barriers[i].subresourceRange.layerCount = image.get_create_info().layers;
		barriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

		if (i == 0)
		{
			barriers[i].oldLayout = base_level_layout;
			barriers[i].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barriers[i].srcAccessMask = src_access;
			barriers[i].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			barriers[i].subresourceRange.baseMipLevel = 0;
			barriers[i].subresourceRange.levelCount = 1;
		}
		else
		{
			barriers[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			barriers[i].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barriers[i].srcAccessMask = 0;
			barriers[i].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barriers[i].subresourceRange.baseMipLevel = 1;
			barriers[i].subresourceRange.levelCount = image.get_create_info().levels - 1;
		}
	}

	barrier(src_stage, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, nullptr, 0, nullptr,
	        need_top_level_barrier ? 2 : 1,
	        need_top_level_barrier ? barriers : barriers + 1);
}

void CommandBuffer::generate_mipmap(const Image &image)
{
	auto &create_info = image.get_create_info();
	VkOffset3D size = { int(create_info.width), int(create_info.height), int(create_info.depth) };
	const VkOffset3D origin = { 0, 0, 0 };

	VK_ASSERT(image.get_layout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

	VkImageMemoryBarrier b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
	b.image = image.get_image();
	b.subresourceRange.levelCount = 1;
	b.subresourceRange.layerCount = image.get_create_info().layers;
	b.subresourceRange.aspectMask = format_to_aspect_mask(image.get_format());
	b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

	for (unsigned i = 1; i < create_info.levels; i++)
	{
		VkOffset3D src_size = size;
		size.x = Util::max_of(size.x >> 1, 1);
		size.y = Util::max_of(size.y >> 1, 1);
		size.z = Util::max_of(size.z >> 1, 1);

		blit_image(image, image,
		           origin, size, origin, src_size, i, i - 1, 0, 0, create_info.layers, VK_FILTER_LINEAR);

		b.subresourceRange.baseMipLevel = i;
		barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
		        0, nullptr, 0, nullptr, 1, &b);
	}
}

void CommandBuffer::blit_image(const Image &dst, const Image &src,
                               const VkOffset3D &dst_offset,
                               const VkOffset3D &dst_extent, const VkOffset3D &src_offset, const VkOffset3D &src_extent,
                               unsigned dst_level, unsigned src_level, unsigned dst_base_layer, unsigned src_base_layer,
                               unsigned num_layers, VkFilter filter)
{
#if 0
	const VkImageBlit blit = {
		{ format_to_aspect_mask(src.get_create_info().format), src_level, src_base_layer, num_layers },
		{ src_offset, add_offset(src_offset, src_extent) },
		{ format_to_aspect_mask(dst.get_create_info().format), dst_level, dst_base_layer, num_layers },
		{ dst_offset, add_offset(dst_offset, dst_extent) },
	};

	table.vkCmdBlitImage(cmd,
	                     src.get_image(), src.get_layout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL),
	                     dst.get_image(), dst.get_layout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
	                     1, &blit, filter);
#else
	// RADV workaround.
	for (unsigned i = 0; i < num_layers; i++)
	{
		const VkImageBlit blit = {
				{ format_to_aspect_mask(src.get_create_info().format), src_level, src_base_layer + i, 1 },
				{ src_offset,                                          add_offset(src_offset, src_extent) },
				{ format_to_aspect_mask(dst.get_create_info().format), dst_level, dst_base_layer + i, 1 },
				{ dst_offset,                                          add_offset(dst_offset, dst_extent) },
		};

		table.vkCmdBlitImage(cmd,
		                     src.get_image(), src.get_layout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL),
		                     dst.get_image(), dst.get_layout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
		                     1, &blit, filter);
	}
#endif
}

void CommandBuffer::begin_context(bool compute)
{
	granite_command_state_begin(&state, compute);
	pipeline_state.program = nullptr;
	pipeline_state.potential_static_state.spec_constant_mask = 0;
	pipeline_state.potential_static_state.internal_spec_constant_mask = 0;

	if (debug_channel_buffer)
		set_storage_buffer(VULKAN_NUM_DESCRIPTOR_SETS - 1, VULKAN_NUM_BINDINGS - 1, *debug_channel_buffer);
}

void CommandBuffer::begin_compute()
{
	begin_context(true);
}

void CommandBuffer::begin_graphics()
{
	begin_context(false);
}

void CommandBuffer::begin_render_pass(const RenderPassInfo &info)
{
	VK_ASSERT(!framebuffer);
	VK_ASSERT(!pipeline_state.compatible_render_pass);
	VK_ASSERT(!actual_render_pass);

	framebuffer = &device->request_framebuffer(info);
	pipeline_state.compatible_render_pass = static_cast<const RenderPass *>(framebuffer->render_pass);
	actual_render_pass = &device->request_render_pass(info, false);
	pipeline_state.subpass_index = 0;

	memset(framebuffer_attachments, 0, sizeof(framebuffer_attachments));
	unsigned att;
	for (att = 0; att < info.num_color_attachments; att++)
		framebuffer_attachments[att] = info.color_attachments[att];
	if (info.depth_stencil)
		framebuffer_attachments[att++] = info.depth_stencil;

	VkRect2D render_area = granite_command_state_set_render_area(&state, &info.render_area, framebuffer->width,
	                                                             framebuffer->height);

	VkClearValue clear_values[VULKAN_NUM_ATTACHMENTS + 1];
	unsigned num_clear_values = 0;

	for (unsigned i = 0; i < info.num_color_attachments; i++)
	{
		VK_ASSERT(info.color_attachments[i]);
		if (info.clear_attachments & (1u << i))
		{
			clear_values[i].color = info.clear_color[i];
			num_clear_values = i + 1;
		}

		if (info.color_attachments[i]->get_image().is_swapchain_image())
			swapchain_touch_in_stages(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
	}

	if (info.depth_stencil && (info.op_flags & RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT) != 0)
	{
		clear_values[info.num_color_attachments].depthStencil = info.clear_depth_stencil;
		num_clear_values = info.num_color_attachments + 1;
	}

	VkRenderPassBeginInfo begin_info = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
	begin_info.renderPass = actual_render_pass->get_render_pass();
	begin_info.framebuffer = framebuffer->framebuffer;
	begin_info.renderArea = render_area;
	begin_info.clearValueCount = num_clear_values;
	begin_info.pClearValues = clear_values;

	table.vkCmdBeginRenderPass(cmd, &begin_info, VK_SUBPASS_CONTENTS_INLINE);

	begin_graphics();
}

void CommandBuffer::end_render_pass()
{
	VK_ASSERT(framebuffer);
	VK_ASSERT(actual_render_pass);
	VK_ASSERT(pipeline_state.compatible_render_pass);

	table.vkCmdEndRenderPass(cmd);

	framebuffer = nullptr;
	actual_render_pass = nullptr;
	pipeline_state.compatible_render_pass = nullptr;
	begin_compute();
}

static UTIL_NOINLINE void log_compile_time(const char *tag, Hash hash,
                             int64_t time_ns, VkResult result,
                             CommandBuffer::CompileMode mode)
{
	bool stall = time_ns >= 5 * 1000 * 1000 && mode != CommandBuffer::CompileMode::AsyncThread;
#ifndef VULKAN_DEBUG
	// If a compile takes more than 5 ms and it's not happening on an async thread,
	// we consider it a stall.
	if (stall)
#endif
	{
		double time_us = 1e-3 * double(time_ns);
		const char *mode_str;

		switch (mode)
		{
		case CommandBuffer::CompileMode::Sync:
			mode_str = "sync";
			break;

		case CommandBuffer::CompileMode::FailOnCompileRequired:
			mode_str = "fail-on-compile-required";
			break;

		default:
			mode_str = "async-thread";
			break;
		}

#ifdef VULKAN_DEBUG
		if (!stall)
		{
			LOGI("Compile (%s, %016llx): thread %u - %.3f us (mode: %s, success: %s).\n",
			     tag, static_cast<unsigned long long>(hash),
			     granite_thread_index(),
			     time_us, mode_str, result == VK_SUCCESS ? "yes" : "no");
		}
		else
#endif
		{
			LOGW("Stalled compile (%s, %016llx): thread %u - %.3f us (mode: %s, success: %s).\n",
			     tag, static_cast<unsigned long long>(hash),
			     granite_thread_index(),
			     time_us, mode_str, result == VK_SUCCESS ? "yes" : "no");
		}
	}
}

static granite_pipeline_device_t pipeline_device(Device *device)
{
	auto &features = device->get_device_features();
	granite_pipeline_device_t dev;
	dev.device = device->get_device();
	dev.table = &device->get_device_table();
	dev.cache_control = !device->get_workarounds().broken_pipeline_cache_control &&
	                    features.pipeline_creation_cache_control_features.pipelineCreationCacheControl;
	dev.conservative_rasterization = features.supports_conservative_rasterization;
	dev.min_subgroup_size = features.subgroup_size_control_properties.minSubgroupSize;
	dev.max_subgroup_size = features.subgroup_size_control_properties.maxSubgroupSize;
	return dev;
}

static granite_pipeline_program_t pipeline_program(const Program &program)
{
	granite_pipeline_program_t p;
	p.hash = program.get_hash();
	p.layout = program.get_pipeline_layout()->get_layout();
	p.resource_layout = &program.get_pipeline_layout()->get_resource_layout();
	for (unsigned i = 0; i < GRANITE_SHADER_STAGE_COUNT; i++)
	{
		auto *shader = program.get_shader(static_cast<ShaderStage>(i));
		p.modules[i] = shader ? shader->get_module() : VK_NULL_HANDLE;
	}
	return p;
}

// Keeps the pipeline under its hash in the program. If another thread got
// there first, or there is no memory to keep it, this one goes.
static Pipeline keep_pipeline(Device *device, const DeferredPipelineCompile &compile, VkPipeline pipeline,
                              uint32_t dynamic_mask, uint32_t kind)
{
	if (pipeline == VK_NULL_HANDLE)
		return {};

	auto returned_pipeline = compile.program->add_pipeline(compile.hash, { pipeline, dynamic_mask });
	if (returned_pipeline.pipeline != pipeline)
		device->get_device_table().vkDestroyPipeline(device->get_device(), pipeline, nullptr);
	else
		device->record_pipeline(compile, kind);
	return returned_pipeline;
}

Pipeline CommandBuffer::build_compute_pipeline(Device *device, const DeferredPipelineCompile &compile,
                                               CompileMode mode)
{
	if (compile.static_state.state.subgroup_control_size &&
	    !device->supports_subgroup_size_log2(compile.static_state.state.subgroup_full_group,
	                                         compile.static_state.state.subgroup_minimum_size_log2,
	                                         compile.static_state.state.subgroup_maximum_size_log2))
	{
		LOGE("Subgroup size configuration not supported.\n");
		return {};
	}

	auto dev = pipeline_device(device);
	auto program = pipeline_program(*compile.program);
	VkResult vr = VK_SUCCESS;

	auto start_ts = granite_time_nsecs();
	VkPipeline pipeline = granite_pipeline_build_compute(&dev, &compile, &program,
	                                                     mode == CompileMode::FailOnCompileRequired, &vr);
	auto end_ts = granite_time_nsecs();
	log_compile_time("compute", compile.hash, end_ts - start_ts, vr, mode);

	return keep_pipeline(device, compile, pipeline, 0, PIPELINE_RECORD_COMPUTE);
}

void CommandBuffer::extract_pipeline_state(DeferredPipelineCompile &compile) const
{
	compile = pipeline_state;

	if (!compile.program)
	{
		LOGE("Attempting to extract pipeline state when no program is bound.\n");
		return;
	}

	if (state.is_compute)
		update_hash_compute_pipeline(compile);
	else
	{
		uint32_t active_vbo = 0;
		update_hash_graphics_pipeline(compile, active_vbo);
	}
}

Pipeline CommandBuffer::build_graphics_pipeline(Device *device, const DeferredPipelineCompile &compile,
                                                CompileMode mode)
{
	auto dev = pipeline_device(device);
	auto program = pipeline_program(*compile.program);
	uint32_t dynamic_mask = 0;
	VkResult vr = VK_SUCCESS;

	auto start_ts = granite_time_nsecs();
	VkPipeline pipeline = granite_pipeline_build_graphics(&dev, &compile, &program,
	                                                      &compile.compatible_render_pass->get_impl(),
	                                                      mode == CompileMode::FailOnCompileRequired,
	                                                      &dynamic_mask, &vr);
	auto end_ts = granite_time_nsecs();
	log_compile_time("graphics", compile.hash, end_ts - start_ts, vr, mode);

	return keep_pipeline(device, compile, pipeline, dynamic_mask, PIPELINE_RECORD_GRAPHICS);
}

void CommandBuffer::update_hash_compute_pipeline(DeferredPipelineCompile &compile)
{
	auto program = pipeline_program(*compile.program);
	compile.hash = granite_pipeline_hash_compute(&compile, &program);
}

void CommandBuffer::update_hash_graphics_pipeline(DeferredPipelineCompile &compile, uint32_t &active_vbos)
{
	auto program = pipeline_program(*compile.program);
	compile.hash = granite_pipeline_hash_graphics(&compile, &program, compile.compatible_render_pass->get_hash(),
	                                              &active_vbos);
}

// Brings the VkCommandBuffer up to the state set. The pipeline comes from
// the program's map, or is compiled: at once when @synchronous, otherwise
// only if the driver has it cached. False: nothing to draw with.
bool CommandBuffer::flush_state(bool synchronous)
{
	if (!pipeline_state.program)
		return false;

	if (granite_command_state_pipeline_dirty(&state))
	{
		auto mode = synchronous ? CompileMode::Sync : CompileMode::FailOnCompileRequired;
		if (state.is_compute)
			update_hash_compute_pipeline(pipeline_state);
		else
			update_hash_graphics_pipeline(pipeline_state, state.active_vbos);

		Pipeline pipeline = pipeline_state.program->get_pipeline(pipeline_state.hash);
		if (pipeline.pipeline == VK_NULL_HANDLE)
			pipeline = state.is_compute ? build_compute_pipeline(device, pipeline_state, mode)
			                            : build_graphics_pipeline(device, pipeline_state, mode);
		granite_command_state_set_pipeline(&state, &pipeline);
	}

	return granite_command_state_flush(&state);
}

bool CommandBuffer::flush_pipeline_state_without_blocking()
{
	return flush_state(false);
}

void CommandBuffer::set_vertex_attrib(uint32_t attrib, uint32_t binding, VkFormat format, VkDeviceSize offset)
{
	VK_ASSERT(attrib < VULKAN_NUM_VERTEX_ATTRIBS);
	VK_ASSERT(binding < VULKAN_NUM_VERTEX_BUFFERS);
	VK_ASSERT(framebuffer);
	granite_command_state_set_vertex_attrib(&state, &pipeline_state, attrib, binding, format, uint32_t(offset));
}

void CommandBuffer::set_vertex_binding(uint32_t binding, const Buffer &buffer, VkDeviceSize offset, VkDeviceSize stride,
                                       VkVertexInputRate step_rate)
{
	VK_ASSERT(binding < VULKAN_NUM_VERTEX_BUFFERS);
	VK_ASSERT(framebuffer);
	granite_command_state_set_vertex_binding(&state, &pipeline_state, binding, buffer.get_buffer(), offset, stride,
	                                         step_rate);
}

void CommandBuffer::set_scissor(const VkRect2D &rect)
{
	VK_ASSERT(framebuffer);
	VK_ASSERT(rect.offset.x >= 0);
	VK_ASSERT(rect.offset.y >= 0);
	granite_command_state_set_scissor(&state, &rect);
}

void CommandBuffer::push_constants(const void *data, VkDeviceSize offset, VkDeviceSize range)
{
	VK_ASSERT(offset + range <= VULKAN_PUSH_CONSTANT_SIZE);
	granite_command_state_push_constants(&state, data, size_t(offset), size_t(range));
}

void CommandBuffer::set_program(Program *program)
{
	if (pipeline_state.program == program)
		return;

	pipeline_state.program = program;
	VK_ASSERT(!program || (framebuffer && program->get_shader(ShaderStage::Vertex)) ||
	          (!framebuffer && program->get_shader(ShaderStage::Compute)));
	granite_command_state_set_program(&state, program ? &program->get_pipeline_layout()->get_bind_layout() : nullptr);
}

void *CommandBuffer::allocate_constant_data(unsigned set, unsigned binding, VkDeviceSize size)
{
	VK_ASSERT(size <= VULKAN_MAX_UBO_SIZE);
	auto data = ubo_block.allocate(size);
	if (!data.host)
	{
		device->request_uniform_block(ubo_block, size);
		data = ubo_block.allocate(size);
	}
	set_uniform_buffer(set, binding, *ubo_block.gpu, data.offset, data.padded_size);
	return data.host;
}

void *CommandBuffer::allocate_vertex_data(unsigned binding, VkDeviceSize size, VkDeviceSize stride,
                                          VkVertexInputRate step_rate)
{
	auto data = vbo_block.allocate(size);
	if (!data.host)
	{
		device->request_vertex_block(vbo_block, size);
		data = vbo_block.allocate(size);
	}

	set_vertex_binding(binding, *vbo_block.gpu, data.offset, stride, step_rate);
	return data.host;
}

void CommandBuffer::set_uniform_buffer(unsigned set, unsigned binding, const Buffer &buffer, VkDeviceSize offset,
                                       VkDeviceSize range)
{
	VK_ASSERT(set < VULKAN_NUM_DESCRIPTOR_SETS);
	VK_ASSERT(binding < VULKAN_NUM_BINDINGS);
	VK_ASSERT(buffer.get_create_info().usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
	granite_bindings_set_uniform_buffer(&state.bindings, set, binding, buffer.get_buffer(), buffer.get_cookie(), offset, range);
}

void CommandBuffer::set_storage_buffer(unsigned set, unsigned binding, const Buffer &buffer, VkDeviceSize offset,
                                       VkDeviceSize range)
{
	VK_ASSERT(set < VULKAN_NUM_DESCRIPTOR_SETS);
	VK_ASSERT(binding < VULKAN_NUM_BINDINGS);
	VK_ASSERT(buffer.get_create_info().usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
	granite_bindings_set_storage_buffer(&state.bindings, set, binding, buffer.get_buffer(), buffer.get_cookie(), offset, range);
}

void CommandBuffer::set_uniform_buffer(unsigned set, unsigned binding, const Buffer &buffer)
{
	set_uniform_buffer(set, binding, buffer, 0, buffer.get_create_info().size);
}

void CommandBuffer::set_storage_buffer(unsigned set, unsigned binding, const Buffer &buffer)
{
	set_storage_buffer(set, binding, buffer, 0, buffer.get_create_info().size);
}

void CommandBuffer::set_sampler(unsigned set, unsigned binding, const Sampler &sampler)
{
	VK_ASSERT(set < VULKAN_NUM_DESCRIPTOR_SETS);
	VK_ASSERT(binding < VULKAN_NUM_BINDINGS);
	granite_bindings_set_sampler(&state.bindings, set, binding, sampler.get_sampler(), sampler.get_cookie());
}

void CommandBuffer::set_buffer_view(unsigned set, unsigned binding, const BufferView &view)
{
	VK_ASSERT(set < VULKAN_NUM_DESCRIPTOR_SETS);
	VK_ASSERT(binding < VULKAN_NUM_BINDINGS);
	VK_ASSERT(view.get_buffer().get_create_info().usage & VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT);
	granite_bindings_set_buffer_view(&state.bindings, set, binding, view.get_view(), view.get_cookie());
}

void CommandBuffer::set_texture(unsigned set, unsigned binding,
                                VkImageView float_view, VkImageView integer_view,
                                VkImageLayout layout,
                                uint64_t cookie)
{
	VK_ASSERT(set < VULKAN_NUM_DESCRIPTOR_SETS);
	VK_ASSERT(binding < VULKAN_NUM_BINDINGS);
	granite_bindings_set_texture(&state.bindings, set, binding, float_view, integer_view, layout, cookie);
}

void CommandBuffer::set_texture(unsigned set, unsigned binding, const ImageView &view)
{
	VK_ASSERT(view.get_image().get_create_info().usage & VK_IMAGE_USAGE_SAMPLED_BIT);
	set_texture(set, binding, view.get_float_view(), view.get_integer_view(),
	            view.get_image().get_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL), view.get_cookie());
}

void CommandBuffer::set_texture(unsigned set, unsigned binding, const ImageView &view, const Sampler &sampler)
{
	set_sampler(set, binding, sampler);
	set_texture(set, binding, view);
}

void CommandBuffer::set_texture(unsigned set, unsigned binding, const ImageView &view, StockSampler stock)
{
	VK_ASSERT(set < VULKAN_NUM_DESCRIPTOR_SETS);
	VK_ASSERT(binding < VULKAN_NUM_BINDINGS);
	VK_ASSERT(view.get_image().get_create_info().usage & VK_IMAGE_USAGE_SAMPLED_BIT);
	const auto &sampler = device->get_stock_sampler(stock);
	set_texture(set, binding, view, sampler);
}

void CommandBuffer::set_sampler(unsigned set, unsigned binding, StockSampler stock)
{
	const auto &sampler = device->get_stock_sampler(stock);
	set_sampler(set, binding, sampler);
}

void CommandBuffer::set_storage_texture(unsigned set, unsigned binding, const ImageView &view)
{
	VK_ASSERT(view.get_image().get_create_info().usage & VK_IMAGE_USAGE_STORAGE_BIT);
	set_texture(set, binding, view.get_float_view(), view.get_integer_view(),
	            view.get_image().get_layout(VK_IMAGE_LAYOUT_GENERAL), view.get_cookie());
}

void CommandBuffer::draw(uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance)
{
	VK_ASSERT(!state.is_compute);
	if (flush_state(true))
	{
		table.vkCmdDraw(cmd, vertex_count, instance_count, first_vertex, first_instance);
	}
	else
		LOGE("Failed to flush render state, draw call will be dropped.\n");
}

void CommandBuffer::dispatch_indirect(const Buffer &buffer, uint32_t offset)
{
	VK_ASSERT(state.is_compute);
	if (flush_state(true))
	{
		table.vkCmdDispatchIndirect(cmd, buffer.get_buffer(), offset);
	}
	else
		LOGE("Failed to flush render state, dispatch will be dropped.\n");
}

void CommandBuffer::dispatch(uint32_t groups_x, uint32_t groups_y, uint32_t groups_z)
{
	VK_ASSERT(state.is_compute);
	if (flush_state(true))
	{
		table.vkCmdDispatch(cmd, groups_x, groups_y, groups_z);
	}
	else
		LOGE("Failed to flush render state, dispatch will be dropped.\n");
}

void CommandBuffer::clear_render_state()
{
	// Preserve spec constant mask.
	auto &state = pipeline_state.static_state.state;
	memset(&state, 0, sizeof(state));
}

void CommandBuffer::set_opaque_state()
{
	clear_render_state();
	auto &state = pipeline_state.static_state.state;
	state.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	state.cull_mode = VK_CULL_MODE_BACK_BIT;
	state.blend_enable = false;
	state.depth_test = true;
	state.depth_compare = VK_COMPARE_OP_LESS_OR_EQUAL;
	state.depth_write = true;
	state.depth_bias_enable = false;
	state.primitive_restart = false;
	state.stencil_test = false;
	state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	state.write_mask = ~0u;
	set_dirty(COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT);
}

QueryPoolHandle CommandBuffer::write_timestamp(VkPipelineStageFlagBits stage)
{
	return device->write_timestamp(cmd, stage);
}

void CommandBuffer::end_threaded_recording()
{
	VK_ASSERT(!debug_channel_buffer);

	if (is_ended)
		return;

	is_ended = true;

	// We must end a command buffer on the same thread index we started it on.
	VK_ASSERT(granite_thread_index() == state.thread_index);

	if (has_profiling())
	{
		auto &query_pool = device->get_performance_query_pool(device->get_physical_queue_type(type));
		query_pool.end_command_buffer(cmd);
	}

	if (table.vkEndCommandBuffer(cmd) != VK_SUCCESS)
		LOGE("Failed to end command buffer.\n");
}

void CommandBuffer::end()
{
	end_threaded_recording();

	if (vbo_block.mapped)
		device->request_vertex_block_nolock(vbo_block, 0);
	if (ubo_block.mapped)
		device->request_uniform_block_nolock(ubo_block, 0);
}

void CommandBuffer::begin_region(const char *name, const float *color)
{
	if (device->ext.supports_debug_utils)
	{
		VkDebugUtilsLabelEXT info = { VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT };
		if (color)
		{
			for (unsigned i = 0; i < 4; i++)
				info.color[i] = color[i];
		}
		else
		{
			for (unsigned i = 0; i < 4; i++)
				info.color[i] = 1.0f;
		}

		info.pLabelName = name;
		if (vkCmdBeginDebugUtilsLabelEXT)
			vkCmdBeginDebugUtilsLabelEXT(cmd, &info);
	}
}

void CommandBuffer::end_region()
{
	if (device->ext.supports_debug_utils)
	{
		if (vkCmdEndDebugUtilsLabelEXT)
			vkCmdEndDebugUtilsLabelEXT(cmd);
	}
}

void CommandBuffer::enable_profiling()
{
	profiling = true;
}

bool CommandBuffer::has_profiling() const
{
	return profiling;
}

void CommandBuffer::begin_debug_channel(DebugChannelInterface *iface, const char *tag, VkDeviceSize size)
{
	if (debug_channel_buffer)
		end_debug_channel();

	Util::copy_string(debug_channel_tag, sizeof(debug_channel_tag), tag);
	debug_channel_interface = iface;

	BufferCreateInfo info = {};
	info.size = size;
	info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	info.domain = BufferDomain::Device;
	debug_channel_buffer = device->create_buffer(info);

	fill_buffer(*debug_channel_buffer, 0);
	barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
	        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
	        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

	set_storage_buffer(VULKAN_NUM_DESCRIPTOR_SETS - 1, VULKAN_NUM_BINDINGS - 1, *debug_channel_buffer);
}

void CommandBuffer::end_debug_channel()
{
	if (!debug_channel_buffer)
		return;

	BufferCreateInfo info = {};
	info.size = debug_channel_buffer->get_create_info().size;
	info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	info.domain = BufferDomain::CachedHost;
	auto debug_channel_readback = device->create_buffer(info);
	barrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_SHADER_WRITE_BIT,
	        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
	copy_buffer(*debug_channel_readback, *debug_channel_buffer);
	barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
	        VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);

	debug_channel_buffer.reset();
	device->add_debug_channel_buffer(debug_channel_interface, debug_channel_tag, Util::move(debug_channel_readback));
	debug_channel_readback = {};
	debug_channel_tag[0] = '\0';
	debug_channel_interface = nullptr;
}


void CommandBufferDeleter::operator()(Vulkan::CommandBuffer *cmd)
{
	cmd->device->handle_pool.command_buffers.free(cmd);
}
}
