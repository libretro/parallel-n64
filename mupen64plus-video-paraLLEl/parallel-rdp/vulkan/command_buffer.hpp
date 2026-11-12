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
#include "buffer_pool.hpp"
#include "vulkan_headers.hpp"
#include "image.hpp"
#include "query_pool.hpp"
#include "render_pass.hpp"
#include "sampler.hpp"
#include "shader.hpp"
#include "vulkan_common.hpp"
#include "pipeline_compile.h"
#include "command_state.h"
#include "command_ops.h"
#include <string.h>

namespace Vulkan
{
struct DebugChannelInterface;

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

struct DeferredPipelineCompile : granite_pipeline_compile_t
{
	Program *program;
	const RenderPass *compatible_render_pass;
};

class CommandBuffer;
struct CommandBufferDeleter
{
	void operator()(CommandBuffer *cmd);
};

class Device;
class CommandBuffer : public Util::IntrusivePtrEnabled<CommandBuffer, CommandBufferDeleter, HandleCounter>
{
public:
	friend struct CommandBufferDeleter;
	enum class Type
	{
		Generic = QUEUE_INDEX_GRAPHICS,
		AsyncCompute = QUEUE_INDEX_COMPUTE,
		AsyncTransfer = QUEUE_INDEX_TRANSFER,
		VideoDecode = QUEUE_INDEX_VIDEO_DECODE,
		AsyncGraphics = QUEUE_INDEX_COUNT, // Aliases with either Generic or AsyncCompute queue
		Count
	};

	~CommandBuffer();
	VkCommandBuffer get_command_buffer() const
	{
		return cmd;
	}

	void begin_region(const char *name, const float *color = nullptr);
	void end_region();

	Device &get_device()
	{
		return *device;
	}

	void set_thread_index(unsigned index_)
	{
		state.thread_index = index_;
	}

	unsigned get_thread_index() const
	{
		return state.thread_index;
	}

	void clear_image(const Image &image, const VkClearValue &value);
	void clear_image(const Image &image, const VkClearValue &value, VkImageAspectFlags aspect);

	void fill_buffer(const Buffer &dst, uint32_t value);
	void fill_buffer(const Buffer &dst, uint32_t value, VkDeviceSize offset, VkDeviceSize size);
	void copy_buffer(const Buffer &dst, VkDeviceSize dst_offset, const Buffer &src, VkDeviceSize src_offset,
	                 VkDeviceSize size);
	void copy_buffer(const Buffer &dst, const Buffer &src);
	void copy_buffer(const Buffer &dst, const Buffer &src, const VkBufferCopy *copies, size_t count);

	void copy_buffer_to_image(const Image &image, const Buffer &buffer, VkDeviceSize buffer_offset,
	                          const VkOffset3D &offset, const VkExtent3D &extent, unsigned row_length,
	                          unsigned slice_height, const VkImageSubresourceLayers &subresrouce);
	void copy_buffer_to_image(const Image &image, const Buffer &buffer, unsigned num_blits, const VkBufferImageCopy *blits);

	void barrier(VkPipelineStageFlags src_stage, VkAccessFlags src_access, VkPipelineStageFlags dst_stage,
	             VkAccessFlags dst_access);

	void barrier(VkPipelineStageFlags src_stages, VkPipelineStageFlags dst_stages,
	             unsigned barriers, const VkMemoryBarrier *globals,
	             unsigned buffer_barriers, const VkBufferMemoryBarrier *buffers,
	             unsigned image_barriers, const VkImageMemoryBarrier *images);

	void image_barrier(const Image &image, VkImageLayout old_layout, VkImageLayout new_layout,
	                   VkPipelineStageFlags src_stage, VkAccessFlags src_access, VkPipelineStageFlags dst_stage,
	                   VkAccessFlags dst_access);

	void blit_image(const Image &dst,
	                const Image &src,
	                const VkOffset3D &dst_offset0, const VkOffset3D &dst_extent,
	                const VkOffset3D &src_offset0, const VkOffset3D &src_extent, unsigned dst_level, unsigned src_level,
	                unsigned dst_base_layer = 0, uint32_t src_base_layer = 0, unsigned num_layers = 1,
	                VkFilter filter = VK_FILTER_LINEAR);

	// Prepares an image to have its mipmap generated.
	// Puts the top-level into TRANSFER_SRC_OPTIMAL, and all other levels are invalidated with an UNDEFINED -> TRANSFER_DST_OPTIMAL.
	void barrier_prepare_generate_mipmap(const Image &image, VkImageLayout base_level_layout, VkPipelineStageFlags src_stage, VkAccessFlags src_access,
	                                     bool need_top_level_barrier = true);

	// The image must have been transitioned with barrier_prepare_generate_mipmap before calling this function.
	// After calling this function, the image will be entirely in TRANSFER_SRC_OPTIMAL layout.
	// Wait for TRANSFER stage to drain before transitioning away from TRANSFER_SRC_OPTIMAL.
	void generate_mipmap(const Image &image);

	void begin_render_pass(const RenderPassInfo &info);
	void end_render_pass();

	void set_program(Program *program);

	void set_buffer_view(unsigned set, unsigned binding, const BufferView &view);
	void set_texture(unsigned set, unsigned binding, const ImageView &view);
	void set_texture(unsigned set, unsigned binding, const ImageView &view, const Sampler &sampler);
	void set_texture(unsigned set, unsigned binding, const ImageView &view, StockSampler sampler);
	void set_storage_texture(unsigned set, unsigned binding, const ImageView &view);
	void set_sampler(unsigned set, unsigned binding, const Sampler &sampler);
	void set_sampler(unsigned set, unsigned binding, StockSampler sampler);
	void set_uniform_buffer(unsigned set, unsigned binding, const Buffer &buffer);
	void set_uniform_buffer(unsigned set, unsigned binding, const Buffer &buffer, VkDeviceSize offset,
	                        VkDeviceSize range);
	void set_storage_buffer(unsigned set, unsigned binding, const Buffer &buffer);
	void set_storage_buffer(unsigned set, unsigned binding, const Buffer &buffer, VkDeviceSize offset,
	                        VkDeviceSize range);

	void push_constants(const void *data, VkDeviceSize offset, VkDeviceSize range);

	void *allocate_constant_data(unsigned set, unsigned binding, VkDeviceSize size);

	template <typename T>
	T *allocate_typed_constant_data(unsigned set, unsigned binding, unsigned count)
	{
		return static_cast<T *>(allocate_constant_data(set, binding, count * sizeof(T)));
	}

	void *allocate_vertex_data(unsigned binding, VkDeviceSize size, VkDeviceSize stride,
	                           VkVertexInputRate step_rate = VK_VERTEX_INPUT_RATE_VERTEX);
	void set_scissor(const VkRect2D &rect);

	void set_vertex_attrib(uint32_t attrib, uint32_t binding, VkFormat format, VkDeviceSize offset);
	void set_vertex_binding(uint32_t binding, const Buffer &buffer, VkDeviceSize offset, VkDeviceSize stride,
	                        VkVertexInputRate step_rate = VK_VERTEX_INPUT_RATE_VERTEX);

	void draw(uint32_t vertex_count, uint32_t instance_count = 1, uint32_t first_vertex = 0,
	          uint32_t first_instance = 0);

	void dispatch(uint32_t groups_x, uint32_t groups_y, uint32_t groups_z);
	void dispatch_indirect(const Buffer &buffer, uint32_t offset);

	void set_opaque_state();

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
		VK_ASSERT((spec_constant_mask & ~((1u << VULKAN_NUM_USER_SPEC_CONSTANTS) - 1u)) == 0u);
		SET_POTENTIALLY_STATIC_STATE(spec_constant_mask);
	}

	template <typename T>
	inline void set_specialization_constant(unsigned index, const T &value)
	{
		VK_ASSERT(index < VULKAN_NUM_USER_SPEC_CONSTANTS);
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
		VK_ASSERT(subgroup_minimum_size_log2 < 8);
		VK_ASSERT(subgroup_maximum_size_log2 < 8);
		SET_STATIC_STATE(subgroup_full_group);
		SET_STATIC_STATE(subgroup_minimum_size_log2);
		SET_STATIC_STATE(subgroup_maximum_size_log2);
	}

	inline Type get_command_buffer_type() const
	{
		return type;
	}

	QueryPoolHandle write_timestamp(VkPipelineStageFlagBits stage);

	// Used when recording command buffers in a thread, and submitting them in a different thread.
	// Need to make sure that no further commands on the VkCommandBuffer happen.
	void end_threaded_recording();
	// End is called automatically by Device in submission. Should not be called by application.
	void end();
	void enable_profiling();
	bool has_profiling() const;

	void begin_debug_channel(DebugChannelInterface *iface, const char *tag, VkDeviceSize size);
	void end_debug_channel();

	void extract_pipeline_state(DeferredPipelineCompile &compile) const;

	enum class CompileMode
	{
		Sync,
		FailOnCompileRequired,
		AsyncThread
	};
	static Pipeline build_graphics_pipeline(Device *device, const DeferredPipelineCompile &compile, CompileMode mode);
	static Pipeline build_compute_pipeline(Device *device, const DeferredPipelineCompile &compile, CompileMode mode);
	static void update_hash_graphics_pipeline(DeferredPipelineCompile &compile, uint32_t &active_vbos);
	static void update_hash_compute_pipeline(DeferredPipelineCompile &compile);
	bool flush_pipeline_state_without_blocking();

private:
	friend class Util::ObjectPool<CommandBuffer>;
	CommandBuffer(Device *device, VkCommandBuffer cmd, VkPipelineCache cache, Type type);

	Device *device;
	const VolkDeviceTable &table;
	VkCommandBuffer cmd;
	Type type;

	const Framebuffer *framebuffer = nullptr;
	const RenderPass *actual_render_pass = nullptr;
	const Vulkan::ImageView *framebuffer_attachments[VULKAN_NUM_ATTACHMENTS + 1] = {};

	// What is set and what of it is dirty (command_state.h).
	granite_command_state_t state;

	bool is_ended = false;

	void set_dirty(CommandBufferDirtyFlags flags)
	{
		state.dirty |= flags;
	}

	DeferredPipelineCompile pipeline_state = {};
#ifndef _MSC_VER
	static_assert(sizeof(pipeline_state.static_state.words) >= sizeof(pipeline_state.static_state.state),
	              "Hashable pipeline state is not large enough!");
#endif

	bool flush_state(bool synchronous);
	void clear_render_state();

	void begin_graphics();
	void begin_compute();
	void begin_context(bool compute);

	BufferBlock vbo_block;
	BufferBlock ubo_block;

	void set_texture(unsigned set, unsigned binding, VkImageView float_view, VkImageView integer_view,
	                 VkImageLayout layout,
	                 uint64_t cookie);


	bool profiling = false;
	char debug_channel_tag[32] = {};
	Vulkan::BufferHandle debug_channel_buffer;
	DebugChannelInterface *debug_channel_interface = nullptr;


};

using CommandBufferHandle = Util::IntrusivePtr<CommandBuffer>;
}
