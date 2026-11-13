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
static granite_image_ref_t image_ref(const Image &image)
{
	auto &info = image.get_create_info();
	granite_image_ref_t ref;
	ref.image = image.get_image();
	ref.format = info.format;
	ref.width = info.width;
	ref.height = info.height;
	ref.depth = info.depth;
	ref.levels = info.levels;
	ref.layers = info.layers;
	ref.general = image.get_layout_type() != Layout::Optimal;
	return ref;
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
	clear_image(image, value, granite_format_aspect_mask(image.get_format()));
}

void CommandBuffer::clear_image(const Image &image, const VkClearValue &value, VkImageAspectFlags aspect)
{
	VK_ASSERT(!framebuffer);
	VK_ASSERT(!actual_render_pass);
	auto ref = image_ref(image);
	granite_cmd_clear_image(&table, cmd, &ref, &value, aspect);
}

void CommandBuffer::barrier(VkPipelineStageFlags src_stages, VkAccessFlags src_access, VkPipelineStageFlags dst_stages,
                            VkAccessFlags dst_access)
{
	VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
	barrier.srcAccessMask = src_access;
	barrier.dstAccessMask = dst_access;
	this->barrier(src_stages, dst_stages, 1, &barrier, 0, nullptr, 0, nullptr);
}

void CommandBuffer::barrier(VkPipelineStageFlags src_stages, VkPipelineStageFlags dst_stages, unsigned barriers,
                            const VkMemoryBarrier *globals, unsigned buffer_barriers,
                            const VkBufferMemoryBarrier *buffers, unsigned image_barriers,
                            const VkImageMemoryBarrier *images)
{
	VK_ASSERT(!actual_render_pass);
	VK_ASSERT(!framebuffer);
	src_stages = granite_barrier_src_stages(src_stages, device->get_workarounds().optimize_all_graphics_barrier);
	table.vkCmdPipelineBarrier(cmd, src_stages, dst_stages, 0, barriers, globals, buffer_barriers, buffers, image_barriers, images);
}

void CommandBuffer::image_barrier(const Image &image, VkImageLayout old_layout, VkImageLayout new_layout,
                                  VkPipelineStageFlags src_stages, VkAccessFlags src_access,
                                  VkPipelineStageFlags dst_stages, VkAccessFlags dst_access)
{
	VK_ASSERT(!actual_render_pass);
	VK_ASSERT(!framebuffer);
	auto ref = image_ref(image);
	granite_cmd_image_barrier(&table, cmd, &ref, old_layout, new_layout,
	                          granite_barrier_src_stages(src_stages, device->get_workarounds().optimize_all_graphics_barrier),
	                          src_access, dst_stages, dst_access);
}

void CommandBuffer::blit_image(const Image &dst, const Image &src,
                               const VkOffset3D &dst_offset,
                               const VkOffset3D &dst_extent, const VkOffset3D &src_offset, const VkOffset3D &src_extent,
                               unsigned dst_level, unsigned src_level, unsigned dst_base_layer, unsigned src_base_layer,
                               unsigned num_layers, VkFilter filter)
{
	auto dst_ref = image_ref(dst);
	auto src_ref = image_ref(src);
	granite_cmd_blit_image(&table, cmd, &dst_ref, &src_ref, &dst_offset, &dst_extent, &src_offset, &src_extent,
	                       dst_level, src_level, dst_base_layer, src_base_layer, num_layers, filter);
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
	auto data = granite_buffer_block_allocate(&ubo_block, size);
	if (!data.host)
	{
		device->request_uniform_block(ubo_block, size);
		data = granite_buffer_block_allocate(&ubo_block, size);
		// Out of memory: nothing to write to or bind.
		if (!data.host)
			return nullptr;
	}
	set_uniform_buffer(set, binding, *static_cast<Buffer *>(ubo_block.handle), data.offset, data.padded_size);
	return data.host;
}

void *CommandBuffer::allocate_vertex_data(unsigned binding, VkDeviceSize size, VkDeviceSize stride,
                                          VkVertexInputRate step_rate)
{
	auto data = granite_buffer_block_allocate(&vbo_block, size);
	if (!data.host)
	{
		device->request_vertex_block(vbo_block, size);
		data = granite_buffer_block_allocate(&vbo_block, size);
		// Out of memory: nothing to write to or bind.
		if (!data.host)
			return nullptr;
	}

	set_vertex_binding(binding, *static_cast<Buffer *>(vbo_block.handle), data.offset, stride, step_rate);
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

void CommandBuffer::end_threaded_recording()
{
	VK_ASSERT(!debug_channel_buffer);

	if (is_ended)
		return;

	is_ended = true;

	// We must end a command buffer on the same thread index we started it on.
	VK_ASSERT(granite_thread_index() == state.thread_index);

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
