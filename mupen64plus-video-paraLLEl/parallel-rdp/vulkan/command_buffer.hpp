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

#pragma once

#include "buffer.hpp"
#include "buffer_pool.h"
#include "vulkan_headers.hpp"
#include "image.hpp"
#include "render_pass.hpp"
#include "sampler.hpp"
#include "shader.hpp"
#include "vulkan_common.hpp"
#include "pipeline_compile.h"
#include "command_state.h"
#include "command_ops.h"
#include "command_buffer.h"
#include <stdlib.h>
#include <string.h>

namespace Vulkan
{

enum CommandBufferDirtyBits
{
	COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT = GRANITE_COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT,
	COMMAND_BUFFER_DIRTY_PIPELINE_BIT = GRANITE_COMMAND_BUFFER_DIRTY_PIPELINE_BIT,

	COMMAND_BUFFER_DIRTY_VIEWPORT_BIT = GRANITE_COMMAND_BUFFER_DIRTY_VIEWPORT_BIT,
	COMMAND_BUFFER_DIRTY_SCISSOR_BIT = GRANITE_COMMAND_BUFFER_DIRTY_SCISSOR_BIT,
	COMMAND_BUFFER_DIRTY_DEPTH_BIAS_BIT = GRANITE_COMMAND_BUFFER_DIRTY_DEPTH_BIAS_BIT,
	COMMAND_BUFFER_DIRTY_STENCIL_REFERENCE_BIT = GRANITE_COMMAND_BUFFER_DIRTY_STENCIL_REFERENCE_BIT,

	COMMAND_BUFFER_DIRTY_STATIC_VERTEX_BIT = GRANITE_COMMAND_BUFFER_DIRTY_STATIC_VERTEX_BIT,

	COMMAND_BUFFER_DIRTY_PUSH_CONSTANTS_BIT = GRANITE_COMMAND_BUFFER_DIRTY_PUSH_CONSTANTS_BIT,

	COMMAND_BUFFER_DYNAMIC_BITS = COMMAND_BUFFER_DIRTY_VIEWPORT_BIT | COMMAND_BUFFER_DIRTY_SCISSOR_BIT |
	                              COMMAND_BUFFER_DIRTY_DEPTH_BIAS_BIT |
	                              COMMAND_BUFFER_DIRTY_STENCIL_REFERENCE_BIT
};
using CommandBufferDirtyFlags = uint32_t;

// What a pipeline is compiled from is C (pipeline_compile.h).
static_assert(VULKAN_NUM_VERTEX_ATTRIBS == GRANITE_NUM_VERTEX_ATTRIBS &&
              VULKAN_NUM_VERTEX_BUFFERS == GRANITE_NUM_VERTEX_BUFFERS &&
              VULKAN_NUM_USER_SPEC_CONSTANTS == GRANITE_NUM_USER_SPEC_CONSTANTS &&
              VULKAN_NUM_TOTAL_SPEC_CONSTANTS == GRANITE_NUM_TOTAL_SPEC_CONSTANTS,
              "The C pipeline state has other limits.");
using PipelineState = granite_pipeline_state_t;
using PotentialState = granite_potential_state_t;

using VertexAttribState = granite_vertex_attrib_state_t;

using DeferredPipelineCompile = granite_pipeline_compile_t;

class Device;

// The command buffer is C (command_buffer.h). No members of its own:
// these are the names and defaults the rest of Granite uses.
class CommandBuffer : public granite_command_buffer_t
{
public:
	CommandBuffer() = delete;

	enum class Type
	{
		Generic = QUEUE_INDEX_GRAPHICS,
		AsyncCompute = QUEUE_INDEX_COMPUTE,
		AsyncTransfer = QUEUE_INDEX_TRANSFER
	};

	enum class CompileMode
	{
		Sync = GRANITE_COMPILE_SYNC,
		FailOnCompileRequired = GRANITE_COMPILE_FAIL_ON_COMPILE_REQUIRED,
		AsyncThread = GRANITE_COMPILE_ASYNC_THREAD
	};

	void add_reference()
	{
		granite_cb_ref(this);
	}

	void release_reference()
	{
		granite_cb_unref(this);
	}

	VkCommandBuffer get_command_buffer() const
	{
		return cmd;
	}

	// device.hpp
	inline Device &get_device();

	void set_thread_index(unsigned index_)
	{
		state.thread_index = index_;
	}

	unsigned get_thread_index() const
	{
		return state.thread_index;
	}

	inline Type get_command_buffer_type() const
	{
		return Type(type);
	}

	void clear_image(const Image &image, const VkClearValue &value)
	{
		granite_cb_clear_image(this, &image, &value, granite_format_aspect_mask(image.get_format()));
	}

	void clear_image(const Image &image, const VkClearValue &value, VkImageAspectFlags aspect)
	{
		granite_cb_clear_image(this, &image, &value, aspect);
	}

	void fill_buffer(const Buffer &dst, uint32_t value)
	{
		granite_cb_fill_buffer(this, &dst, value, 0, VK_WHOLE_SIZE);
	}

	void fill_buffer(const Buffer &dst, uint32_t value, VkDeviceSize offset, VkDeviceSize size)
	{
		granite_cb_fill_buffer(this, &dst, value, offset, size);
	}

	void copy_buffer(const Buffer &dst, VkDeviceSize dst_offset, const Buffer &src, VkDeviceSize src_offset,
	                 VkDeviceSize size)
	{
		const VkBufferCopy region = { src_offset, dst_offset, size };
		granite_cb_copy_buffer(this, &dst, &src, &region, 1);
	}

	void copy_buffer(const Buffer &dst, const Buffer &src)
	{
		copy_buffer(dst, 0, src, 0, dst.get_create_info().size);
	}

	void copy_buffer(const Buffer &dst, const Buffer &src, const VkBufferCopy *copies, size_t count)
	{
		granite_cb_copy_buffer(this, &dst, &src, copies, count);
	}

	void copy_buffer_to_image(const Image &image, const Buffer &buffer, VkDeviceSize buffer_offset,
	                          const VkOffset3D &offset, const VkExtent3D &extent, unsigned row_length,
	                          unsigned slice_height, const VkImageSubresourceLayers &subresource)
	{
		const VkBufferImageCopy region = { buffer_offset, row_length, slice_height, subresource, offset, extent };
		granite_cb_copy_buffer_to_image(this, &image, &buffer, 1, &region);
	}

	void copy_buffer_to_image(const Image &image, const Buffer &buffer, unsigned num_blits, const VkBufferImageCopy *blits)
	{
		granite_cb_copy_buffer_to_image(this, &image, &buffer, num_blits, blits);
	}

	void barrier(VkPipelineStageFlags src_stage, VkAccessFlags src_access, VkPipelineStageFlags dst_stage,
	             VkAccessFlags dst_access)
	{
		VkMemoryBarrier global = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
		global.srcAccessMask = src_access;
		global.dstAccessMask = dst_access;
		granite_cb_barrier(this, src_stage, dst_stage, 1, &global, 0, nullptr, 0, nullptr);
	}

	void barrier(VkPipelineStageFlags src_stages, VkPipelineStageFlags dst_stages,
	             unsigned barriers, const VkMemoryBarrier *globals,
	             unsigned buffer_barriers, const VkBufferMemoryBarrier *buffers,
	             unsigned image_barriers, const VkImageMemoryBarrier *images)
	{
		granite_cb_barrier(this, src_stages, dst_stages, barriers, globals, buffer_barriers, buffers, image_barriers,
		                   images);
	}

	void image_barrier(const Image &image, VkImageLayout old_layout, VkImageLayout new_layout,
	                   VkPipelineStageFlags src_stage, VkAccessFlags src_access, VkPipelineStageFlags dst_stage,
	                   VkAccessFlags dst_access)
	{
		granite_cb_image_barrier(this, &image, old_layout, new_layout, src_stage, src_access, dst_stage, dst_access);
	}

	void blit_image(const Image &dst,
	                const Image &src,
	                const VkOffset3D &dst_offset0, const VkOffset3D &dst_extent,
	                const VkOffset3D &src_offset0, const VkOffset3D &src_extent, unsigned dst_level, unsigned src_level,
	                unsigned dst_base_layer = 0, uint32_t src_base_layer = 0, unsigned num_layers = 1,
	                VkFilter filter = VK_FILTER_LINEAR)
	{
		granite_cb_blit_image(this, &dst, &src, &dst_offset0, &dst_extent, &src_offset0, &src_extent, dst_level,
		                      src_level, dst_base_layer, src_base_layer, num_layers, filter);
	}

	// (out of memory leaves nothing to draw to)
	void begin_render_pass(const RenderPassInfo &info)
	{
		if (!granite_cb_begin_render_pass(this, &info))
			abort();
	}

	void end_render_pass()
	{
		granite_cb_end_render_pass(this);
	}

	void set_program(Program *program)
	{
		granite_cb_set_program(this, program);
	}

	void set_buffer_view(unsigned set, unsigned binding, const BufferView &view)
	{
		granite_cb_set_buffer_view(this, set, binding, &view);
	}

	void set_texture(unsigned set, unsigned binding, const ImageView &view)
	{
		granite_cb_set_texture(this, set, binding, &view, -1);
	}

	void set_texture(unsigned set, unsigned binding, const ImageView &view, StockSampler sampler)
	{
		granite_cb_set_texture(this, set, binding, &view, int(sampler));
	}

	void set_storage_texture(unsigned set, unsigned binding, const ImageView &view)
	{
		granite_cb_set_storage_texture(this, set, binding, &view);
	}

	void set_sampler(unsigned set, unsigned binding, const Sampler &sampler)
	{
		granite_cb_set_sampler(this, set, binding, &sampler);
	}

	void set_uniform_buffer(unsigned set, unsigned binding, const Buffer &buffer)
	{
		granite_cb_set_uniform_buffer(this, set, binding, &buffer, 0, buffer.get_create_info().size);
	}

	void set_uniform_buffer(unsigned set, unsigned binding, const Buffer &buffer, VkDeviceSize offset,
	                        VkDeviceSize range)
	{
		granite_cb_set_uniform_buffer(this, set, binding, &buffer, offset, range);
	}

	void set_storage_buffer(unsigned set, unsigned binding, const Buffer &buffer)
	{
		granite_cb_set_storage_buffer(this, set, binding, &buffer, 0, buffer.get_create_info().size);
	}

	void set_storage_buffer(unsigned set, unsigned binding, const Buffer &buffer, VkDeviceSize offset,
	                        VkDeviceSize range)
	{
		granite_cb_set_storage_buffer(this, set, binding, &buffer, offset, range);
	}

	void push_constants(const void *data, VkDeviceSize offset, VkDeviceSize range)
	{
		granite_cb_push_constants(this, data, offset, range);
	}

	void *allocate_constant_data(unsigned set, unsigned binding, VkDeviceSize size)
	{
		return granite_cb_allocate_constant_data(this, set, binding, size);
	}

	template <typename T>
	T *allocate_typed_constant_data(unsigned set, unsigned binding, unsigned count)
	{
		return static_cast<T *>(allocate_constant_data(set, binding, count * sizeof(T)));
	}

	void *allocate_vertex_data(unsigned binding, VkDeviceSize size, VkDeviceSize stride,
	                           VkVertexInputRate step_rate = VK_VERTEX_INPUT_RATE_VERTEX)
	{
		return granite_cb_allocate_vertex_data(this, binding, size, stride, step_rate);
	}

	void set_scissor(const VkRect2D &rect)
	{
		granite_cb_set_scissor(this, &rect);
	}

	void set_vertex_attrib(uint32_t attrib, uint32_t binding, VkFormat format, VkDeviceSize offset)
	{
		granite_cb_set_vertex_attrib(this, attrib, binding, format, offset);
	}

	void set_vertex_binding(uint32_t binding, const Buffer &buffer, VkDeviceSize offset, VkDeviceSize stride,
	                        VkVertexInputRate step_rate = VK_VERTEX_INPUT_RATE_VERTEX)
	{
		granite_cb_set_vertex_binding(this, binding, &buffer, offset, stride, step_rate);
	}

	void draw(uint32_t vertex_count, uint32_t instance_count = 1, uint32_t first_vertex = 0,
	          uint32_t first_instance = 0)
	{
		granite_cb_draw(this, vertex_count, instance_count, first_vertex, first_instance);
	}

	void dispatch(uint32_t groups_x, uint32_t groups_y, uint32_t groups_z)
	{
		granite_cb_dispatch(this, groups_x, groups_y, groups_z);
	}

	void dispatch_indirect(const Buffer &buffer, uint32_t offset)
	{
		granite_cb_dispatch_indirect(this, &buffer, offset);
	}

	void set_opaque_state()
	{
		granite_cb_set_opaque_state(this);
	}

#define SET_STATIC_STATE(value)                               \
	do                                                        \
	{                                                         \
		if (pipeline_state.static_state.state.value != value) \
		{                                                     \
			pipeline_state.static_state.state.value = value;  \
			set_dirty(COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT); \
		}                                                     \
	} while (0)

#define SET_POTENTIALLY_STATIC_STATE(value)                       \
	do                                                            \
	{                                                             \
		if (pipeline_state.potential_static_state.value != value) \
		{                                                         \
			pipeline_state.potential_static_state.value = value;  \
			set_dirty(COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT);     \
		}                                                         \
	} while (0)

	inline void set_depth_test(bool depth_test, bool depth_write)
	{
		SET_STATIC_STATE(depth_test);
		SET_STATIC_STATE(depth_write);
	}

	inline void set_depth_compare(VkCompareOp depth_compare)
	{
		SET_STATIC_STATE(depth_compare);
	}

	inline void set_blend_enable(bool blend_enable)
	{
		SET_STATIC_STATE(blend_enable);
	}

	inline void set_blend_factors(VkBlendFactor src_color_blend, VkBlendFactor src_alpha_blend,
	                              VkBlendFactor dst_color_blend, VkBlendFactor dst_alpha_blend)
	{
		SET_STATIC_STATE(src_color_blend);
		SET_STATIC_STATE(dst_color_blend);
		SET_STATIC_STATE(src_alpha_blend);
		SET_STATIC_STATE(dst_alpha_blend);
	}

	inline void set_blend_factors(VkBlendFactor src_blend, VkBlendFactor dst_blend)
	{
		set_blend_factors(src_blend, src_blend, dst_blend, dst_blend);
	}

	inline void set_color_write_mask(uint32_t write_mask)
	{
		SET_STATIC_STATE(write_mask);
	}

	inline void set_cull_mode(VkCullModeFlags cull_mode)
	{
		SET_STATIC_STATE(cull_mode);
	}

	inline void set_specialization_constant_mask(uint32_t spec_constant_mask)
	{
		SET_POTENTIALLY_STATIC_STATE(spec_constant_mask);
	}

	template <typename T>
	inline void set_specialization_constant(unsigned index, const T &value)
	{
		static_assert(sizeof(value) == sizeof(uint32_t), "Spec constant data must be 32-bit.");
		if (memcmp(&pipeline_state.potential_static_state.spec_constants[index], &value, sizeof(value)))
		{
			memcpy(&pipeline_state.potential_static_state.spec_constants[index], &value, sizeof(value));
			if (pipeline_state.potential_static_state.spec_constant_mask & (1u << index))
				set_dirty(COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT);
		}
	}

	inline void set_specialization_constant(unsigned index, bool value)
	{
		set_specialization_constant(index, uint32_t(value));
	}

	inline void enable_subgroup_size_control(bool subgroup_control_size)
	{
		SET_STATIC_STATE(subgroup_control_size);
	}

	inline void set_subgroup_size_log2(bool subgroup_full_group,
	                                   uint8_t subgroup_minimum_size_log2,
	                                   uint8_t subgroup_maximum_size_log2)
	{
		SET_STATIC_STATE(subgroup_full_group);
		SET_STATIC_STATE(subgroup_minimum_size_log2);
		SET_STATIC_STATE(subgroup_maximum_size_log2);
	}

	// Used when recording command buffers in a thread, and submitting them in a different thread.
	// Need to make sure that no further commands on the VkCommandBuffer happen.
	void end_threaded_recording()
	{
		granite_cb_end_recording(this);
	}

	// End is called automatically by Device in submission. Should not be called by application.
	void end()
	{
		granite_cb_end(this);
	}

	void extract_pipeline_state(DeferredPipelineCompile &compile) const
	{
		granite_cb_extract_pipeline_state(this, &compile);
	}

	// device.hpp
	static inline Pipeline build_graphics_pipeline(Device *device, const DeferredPipelineCompile &compile, CompileMode mode);
	static inline Pipeline build_compute_pipeline(Device *device, const DeferredPipelineCompile &compile, CompileMode mode);

	bool flush_pipeline_state_without_blocking()
	{
		return granite_cb_flush_state(this, false);
	}

private:
	void set_dirty(CommandBufferDirtyFlags flags)
	{
		granite_cb_set_dirty(this, flags);
	}
};

using CommandBufferHandle = Util::IntrusivePtr<CommandBuffer>;
}
