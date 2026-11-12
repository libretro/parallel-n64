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

#ifndef GRANITE_COMMAND_STATE_H
#define GRANITE_COMMAND_STATE_H

#include <stddef.h>
#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>
#include "volk.h"
#include "resource_bindings.h"
#include "pipeline_compile.h"
#include "pipeline_map.h"

RETRO_BEGIN_DECLS

#define GRANITE_COMMAND_BUFFER_DYNAMIC_BITS \
	(GRANITE_COMMAND_BUFFER_DIRTY_VIEWPORT_BIT | GRANITE_COMMAND_BUFFER_DIRTY_SCISSOR_BIT | \
	 GRANITE_COMMAND_BUFFER_DIRTY_DEPTH_BIAS_BIT | GRANITE_COMMAND_BUFFER_DIRTY_STENCIL_REFERENCE_BIT)

/* What a command buffer has set and what of it the VkCommandBuffer does
 * not have yet.
 *
 * State is set as it comes and only marked dirty; a draw or dispatch
 * flushes what is dirty: the pipeline when the state it is baked from
 * changed, the descriptor sets, the push constants, and for a draw the
 * viewport, scissor and vertex buffers. Setting what is set already
 * costs nothing at the flush. It is one thread's. */
typedef struct granite_command_state
{
	VkDevice device;
	const struct VolkDeviceTable *table;
	VkCommandBuffer cmd;
	/* whose descriptor sets it takes */
	unsigned thread_index;

	granite_resource_bindings_t bindings;
	/* of the program set; NULL before the first */
	const granite_bind_layout_t *layout;
	/* the one bound; VK_NULL_HANDLE when the state has none yet */
	granite_pipeline_t pipeline;

	VkBuffer vbo_buffers[GRANITE_NUM_VERTEX_BUFFERS];
	VkDeviceSize vbo_offsets[GRANITE_NUM_VERTEX_BUFFERS];
	VkViewport viewport;
	VkRect2D scissor;

	/* GRANITE_COMMAND_BUFFER_DIRTY_* */
	uint32_t dirty;
	/* a bit a vertex buffer binding */
	uint32_t dirty_vbos;
	/* those the program's attributes read: the pipeline hash gives it */
	uint32_t active_vbos;
	bool is_compute;
} granite_command_state_t;

void granite_command_state_init(granite_command_state_t *state, VkDevice device,
		const struct VolkDeviceTable *table, VkCommandBuffer cmd);
/* A render pass (or, @compute, the time outside one) begins: nothing is
 * bound in the VkCommandBuffer any more, so everything is dirty and no
 * program, pipeline or vertex buffer is set. The push constants stay. */
void granite_command_state_begin(granite_command_state_t *state, bool compute);

/* The viewport and scissor of a pass that renders @area of a framebuffer
 * of @width by @height: the part of @area inside it. Returns the scissor,
 * which is also the pass's render area. */
VkRect2D granite_command_state_set_render_area(granite_command_state_t *state, const VkRect2D *area,
		uint32_t width, uint32_t height);
void granite_command_state_set_scissor(granite_command_state_t *state, const VkRect2D *rect);
/* @size bytes at @offset of the push constant block. Bytes past the end
 * of the block are refused: nothing is copied. */
void granite_command_state_push_constants(granite_command_state_t *state, const void *data, size_t offset,
		size_t size);

/* The vertex layout is baked into the pipeline, so it lives in @compile;
 * which buffer is bound does not. */
void granite_command_state_set_vertex_attrib(granite_command_state_t *state, granite_pipeline_compile_t *compile,
		uint32_t attrib, uint32_t binding, VkFormat format, uint32_t offset);
void granite_command_state_set_vertex_binding(granite_command_state_t *state, granite_pipeline_compile_t *compile,
		uint32_t binding, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize stride, VkVertexInputRate rate);

/* Another program is set, with pipeline layout @layout; NULL: none. The
 * pipeline is to be found again, and the descriptor sets the layouts do
 * not share are dirty. */
void granite_command_state_set_program(granite_command_state_t *state, const granite_bind_layout_t *layout);

/* True when the pipeline has to be looked up or built again before the
 * next draw or dispatch: the baked state changed since the last one, or
 * there is none. The caller then finds it and sets it. */
bool granite_command_state_pipeline_dirty(granite_command_state_t *state);
/* @pipeline is the pipeline for the state as it is; VK_NULL_HANDLE when
 * there is none to be had. Bound if it is not the one bound; state it
 * bakes in that is dynamic otherwise is then dirty. */
void granite_command_state_set_pipeline(granite_command_state_t *state, const granite_pipeline_t *pipeline);
/* Brings the VkCommandBuffer up to the state. False, and nothing done,
 * when there is no pipeline: the draw or dispatch is to be dropped. */
bool granite_command_state_flush(granite_command_state_t *state);

RETRO_END_DECLS

#endif
