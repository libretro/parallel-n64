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

#ifndef GRANITE_COMMAND_BUFFER_H
#define GRANITE_COMMAND_BUFFER_H

#include <stddef.h>
#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>
#include <retro_atomic.h>
#include "volk.h"
#include "pool.h"
#include "handle.h"
#include "context.h"
#include "device_caps.h"
#include "buffer.h"
#include "buffer_pool.h"
#include "image.h"
#include "sampler.h"
#include "command_state.h"
#include "pipeline_compile.h"
#include "program_cache.h"
#include "render_target.h"

RETRO_BEGIN_DECLS

/* How a pipeline that is not there yet is made */
enum granite_compile_mode
{
	/* now, however long it takes */
	GRANITE_COMPILE_SYNC = 0,
	/* only if the driver has it cached: otherwise there is none */
	GRANITE_COMPILE_FAIL_ON_COMPILE_REQUIRED,
	/* now, on a thread that may take its time */
	GRANITE_COMPILE_ASYNC_THREAD
};

/* What command buffers need of the device they are of. The device
 * fills one in and it outlives them. */
typedef struct granite_cmd_device
{
	VkDevice device;
	const struct VolkDeviceTable *table;
	const granite_workarounds_t *workarounds;
	const granite_device_features_t *features;
	granite_program_cache_t *programs;
	/* the owner thread's */
	granite_framebuffer_allocator_t *framebuffers;
	/* by stock sampler index */
	const granite_sampler_t *stock_samplers;
	void *user;
	/* A block with room for @size in @block, the old one given back;
	 * @size 0 only gives it back. @vertex: a vertex block, else a
	 * uniform one. The owner thread. */
	void (*request_block)(void *user, bool vertex, granite_buffer_block_t *block, VkDeviceSize size);
	/* A pipeline was built and kept: PIPELINE_RECORD_* @kind. Any
	 * thread. */
	void (*record_pipeline)(void *user, const granite_pipeline_compile_t *compile, uint32_t kind);
} granite_cmd_device_t;

/* A command buffer being recorded, and the state set on it. Counted;
 * its last reference gives it back to its pool. One thread records. */
typedef struct granite_command_buffer
{
	retro_atomic_int_t refs;
	granite_lf_pool_t *pool;
	const granite_cmd_device_t *dev;
	const struct VolkDeviceTable *table;
	VkCommandBuffer cmd;
	/* enum granite_queue_index: the queue it is submitted to */
	unsigned type;
	/* inside a render pass: its framebuffer and pass */
	const granite_framebuffer_t *framebuffer;
	const granite_cached_render_pass_t *actual_render_pass;
	/* what is set and what of it is dirty (command_state.h) */
	granite_command_state_t state;
	bool is_ended;
	granite_pipeline_compile_t pipeline_state;
	/* (buffer_pool.h; given back by granite_cb_end) */
	granite_buffer_block_t vbo_block;
	granite_buffer_block_t ubo_block;
} granite_command_buffer_t;

void granite_cb_pool_slots_init(granite_lf_pool_t *pool);
/* A command buffer with one reference recording into @cmd, which has
 * begun, for queue @type; @cache: the pipeline cache its pipelines are
 * built with. It starts outside a render pass, with everything to set.
 * NULL when there is no room. */
granite_command_buffer_t *granite_cb_new(granite_lf_pool_t *pool, const granite_cmd_device_t *dev,
		VkCommandBuffer cmd, VkPipelineCache cache, unsigned type);

static INLINE void granite_cb_ref(granite_command_buffer_t *cb)
{
	granite_refs_add(&cb->refs);
}

static INLINE void granite_cb_unref(granite_command_buffer_t *cb)
{
	if (granite_refs_drop(&cb->refs))
		granite_lf_pool_release(cb->pool, cb);
}

/* ---- transfers and barriers: outside a render pass ---- */
void granite_cb_fill_buffer(granite_command_buffer_t *cb, const granite_buffer_t *dst, uint32_t value,
		VkDeviceSize offset, VkDeviceSize size);
void granite_cb_copy_buffer(granite_command_buffer_t *cb, const granite_buffer_t *dst, const granite_buffer_t *src,
		const VkBufferCopy *copies, size_t count);
/* @image is in its transfer destination layout */
void granite_cb_copy_buffer_to_image(granite_command_buffer_t *cb, const granite_image_t *image,
		const granite_buffer_t *buffer, unsigned num_blits, const VkBufferImageCopy *blits);
void granite_cb_clear_image(granite_command_buffer_t *cb, const granite_image_t *image, const VkClearValue *value,
		VkImageAspectFlags aspect);
void granite_cb_barrier(granite_command_buffer_t *cb, VkPipelineStageFlags src_stages,
		VkPipelineStageFlags dst_stages, unsigned barriers, const VkMemoryBarrier *globals,
		unsigned buffer_barriers, const VkBufferMemoryBarrier *buffers, unsigned image_barriers,
		const VkImageMemoryBarrier *images);
void granite_cb_image_barrier(granite_command_buffer_t *cb, const granite_image_t *image,
		VkImageLayout old_layout, VkImageLayout new_layout, VkPipelineStageFlags src_stages,
		VkAccessFlags src_access, VkPipelineStageFlags dst_stages, VkAccessFlags dst_access);
void granite_cb_blit_image(granite_command_buffer_t *cb, const granite_image_t *dst, const granite_image_t *src,
		const VkOffset3D *dst_offset, const VkOffset3D *dst_extent, const VkOffset3D *src_offset,
		const VkOffset3D *src_extent, unsigned dst_level, unsigned src_level, unsigned dst_base_layer,
		unsigned src_base_layer, unsigned num_layers, VkFilter filter);

/* ---- render passes: the owner thread ---- */
/* False, and no pass begun, when there is no memory for the pass or
 * its framebuffer */
bool granite_cb_begin_render_pass(granite_command_buffer_t *cb, const granite_render_pass_info_t *info);
void granite_cb_end_render_pass(granite_command_buffer_t *cb);

/* ---- what to draw or dispatch with ---- */
void granite_cb_set_program(granite_command_buffer_t *cb, granite_program_t *program);
void granite_cb_set_uniform_buffer(granite_command_buffer_t *cb, unsigned set, unsigned binding,
		const granite_buffer_t *buffer, VkDeviceSize offset, VkDeviceSize range);
void granite_cb_set_storage_buffer(granite_command_buffer_t *cb, unsigned set, unsigned binding,
		const granite_buffer_t *buffer, VkDeviceSize offset, VkDeviceSize range);
void granite_cb_set_buffer_view(granite_command_buffer_t *cb, unsigned set, unsigned binding,
		const granite_buffer_view_t *view);
void granite_cb_set_sampler(granite_command_buffer_t *cb, unsigned set, unsigned binding,
		const granite_sampler_t *sampler);
/* A sampled image, in its layout for sampling; @stock_sampler: with
 * that stock sampler, or none for a negative one */
void granite_cb_set_texture(granite_command_buffer_t *cb, unsigned set, unsigned binding,
		const granite_image_view_t *view, int stock_sampler);
void granite_cb_set_storage_texture(granite_command_buffer_t *cb, unsigned set, unsigned binding,
		const granite_image_view_t *view);
void granite_cb_push_constants(granite_command_buffer_t *cb, const void *data, VkDeviceSize offset,
		VkDeviceSize range);
/* @size bytes of a uniform block bound at @set, @binding, to write to;
 * NULL when out of memory */
void *granite_cb_allocate_constant_data(granite_command_buffer_t *cb, unsigned set, unsigned binding,
		VkDeviceSize size);
/* The same of a vertex block, bound as vertex buffer @binding */
void *granite_cb_allocate_vertex_data(granite_command_buffer_t *cb, unsigned binding, VkDeviceSize size,
		VkDeviceSize stride, VkVertexInputRate step_rate);
void granite_cb_set_scissor(granite_command_buffer_t *cb, const VkRect2D *rect);
void granite_cb_set_vertex_attrib(granite_command_buffer_t *cb, uint32_t attrib, uint32_t binding,
		VkFormat format, VkDeviceSize offset);
void granite_cb_set_vertex_binding(granite_command_buffer_t *cb, uint32_t binding, const granite_buffer_t *buffer,
		VkDeviceSize offset, VkDeviceSize stride, VkVertexInputRate step_rate);
/* Back-face culling, depth test and write, no blending, triangles */
void granite_cb_set_opaque_state(granite_command_buffer_t *cb);

static INLINE void granite_cb_set_dirty(granite_command_buffer_t *cb, uint32_t flags)
{
	cb->state.dirty |= flags;
}

/* The specialization constants the program is built with: a bit each */
static INLINE void granite_cb_set_spec_constant_mask(granite_command_buffer_t *cb, uint32_t mask)
{
	if (cb->pipeline_state.potential_static_state.spec_constant_mask != mask)
	{
		cb->pipeline_state.potential_static_state.spec_constant_mask = (uint8_t)mask;
		granite_cb_set_dirty(cb, GRANITE_COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT);
	}
}

static INLINE void granite_cb_set_spec_constant(granite_command_buffer_t *cb, unsigned index, uint32_t value)
{
	if (cb->pipeline_state.potential_static_state.spec_constants[index] != value)
	{
		cb->pipeline_state.potential_static_state.spec_constants[index] = value;
		if (cb->pipeline_state.potential_static_state.spec_constant_mask & (1u << index))
			granite_cb_set_dirty(cb, GRANITE_COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT);
	}
}

/* ---- drawing ---- */
/* Brings the VkCommandBuffer up to the state set. The pipeline comes
 * from the program's map or is built: now when @synchronous, otherwise
 * only if the driver has it cached. False: nothing to draw with. */
bool granite_cb_flush_state(granite_command_buffer_t *cb, bool synchronous);
/* Each drops the call when the state cannot be flushed */
void granite_cb_draw(granite_command_buffer_t *cb, uint32_t vertex_count, uint32_t instance_count,
		uint32_t first_vertex, uint32_t first_instance);
void granite_cb_dispatch(granite_command_buffer_t *cb, uint32_t groups_x, uint32_t groups_y, uint32_t groups_z);
void granite_cb_dispatch_indirect(granite_command_buffer_t *cb, const granite_buffer_t *buffer, uint32_t offset);

/* ---- ending ---- */
/* Ends the VkCommandBuffer, on the thread that recorded it; once */
void granite_cb_end_recording(granite_command_buffer_t *cb);
/* The same, and its blocks go back: the owner thread, at submission */
void granite_cb_end(granite_command_buffer_t *cb);

/* ---- pipelines ---- */
/* The state set, with its hash, to build its pipeline elsewhere */
void granite_cb_extract_pipeline_state(const granite_command_buffer_t *cb, granite_pipeline_compile_t *compile);
/* Sets compile->hash from the rest of it */
void granite_pipeline_compile_update_hash_compute(granite_pipeline_compile_t *compile);
void granite_pipeline_compile_update_hash_graphics(granite_pipeline_compile_t *compile, uint32_t *active_vbos);
/* Builds the pipeline of @compile and keeps it in its program: the one
 * built, or the one another thread kept first. No pipeline when it
 * cannot be built. Any thread. */
granite_pipeline_t granite_pipeline_compile_build_compute(const granite_cmd_device_t *dev,
		const granite_pipeline_compile_t *compile, enum granite_compile_mode mode);
granite_pipeline_t granite_pipeline_compile_build_graphics(const granite_cmd_device_t *dev,
		const granite_pipeline_compile_t *compile, enum granite_compile_mode mode);

RETRO_END_DECLS

#endif
