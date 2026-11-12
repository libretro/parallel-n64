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

#include <string.h>
#include "command_state.h"

static uint32_t get_and_clear(granite_command_state_t *state, uint32_t flags)
{
	uint32_t mask = state->dirty & flags;

	state->dirty &= ~flags;
	return mask;
}

void granite_command_state_init(granite_command_state_t *state, VkDevice device,
		const struct VolkDeviceTable *table, VkCommandBuffer cmd)
{
	state->device = device;
	state->table = table;
	state->cmd = cmd;
	state->thread_index = 0;
	state->active_vbos = 0;
	memset(&state->viewport, 0, sizeof(state->viewport));
	memset(&state->scissor, 0, sizeof(state->scissor));
	memset(state->vbo_offsets, 0, sizeof(state->vbo_offsets));
	granite_bindings_init(&state->bindings);
	granite_command_state_begin(state, true);
}

void granite_command_state_begin(granite_command_state_t *state, bool compute)
{
	state->is_compute = compute;
	state->dirty = ~(uint32_t)0;
	state->dirty_vbos = ~(uint32_t)0;
	state->pipeline.pipeline = VK_NULL_HANDLE;
	state->pipeline.dynamic_mask = 0;
	state->layout = NULL;
	granite_bindings_invalidate(&state->bindings);
	memset(state->vbo_buffers, 0, sizeof(state->vbo_buffers));
}

VkRect2D granite_command_state_set_render_area(granite_command_state_t *state, const VkRect2D *area,
		uint32_t width, uint32_t height)
{
	VkRect2D rect = *area;

	if (rect.offset.x > (int32_t)width)
		rect.offset.x = (int32_t)width;
	if (rect.offset.y > (int32_t)height)
		rect.offset.y = (int32_t)height;
	if (rect.extent.width > width - (uint32_t)rect.offset.x)
		rect.extent.width = width - (uint32_t)rect.offset.x;
	if (rect.extent.height > height - (uint32_t)rect.offset.y)
		rect.extent.height = height - (uint32_t)rect.offset.y;

	state->viewport.x = (float)rect.offset.x;
	state->viewport.y = (float)rect.offset.y;
	state->viewport.width = (float)rect.extent.width;
	state->viewport.height = (float)rect.extent.height;
	state->viewport.minDepth = 0.0f;
	state->viewport.maxDepth = 1.0f;
	state->scissor = rect;
	return rect;
}

void granite_command_state_set_scissor(granite_command_state_t *state, const VkRect2D *rect)
{
	state->scissor = *rect;
	state->dirty |= GRANITE_COMMAND_BUFFER_DIRTY_SCISSOR_BIT;
}

void granite_command_state_push_constants(granite_command_state_t *state, const void *data, size_t offset,
		size_t size)
{
	if (offset > GRANITE_PUSH_CONSTANT_SIZE || size > GRANITE_PUSH_CONSTANT_SIZE - offset)
		return;

	memcpy(state->bindings.push_constant_data + offset, data, size);
	state->dirty |= GRANITE_COMMAND_BUFFER_DIRTY_PUSH_CONSTANTS_BIT;
}

void granite_command_state_set_vertex_attrib(granite_command_state_t *state, granite_pipeline_compile_t *compile,
		uint32_t attrib, uint32_t binding, VkFormat format, uint32_t offset)
{
	granite_vertex_attrib_state_t *attr;

	if (attrib >= GRANITE_NUM_VERTEX_ATTRIBS || binding >= GRANITE_NUM_VERTEX_BUFFERS)
		return;

	attr = &compile->attribs[attrib];
	if (attr->binding != binding || attr->format != format || attr->offset != offset)
		state->dirty |= GRANITE_COMMAND_BUFFER_DIRTY_STATIC_VERTEX_BIT;

	attr->binding = binding;
	attr->format = format;
	attr->offset = offset;
}

void granite_command_state_set_vertex_binding(granite_command_state_t *state, granite_pipeline_compile_t *compile,
		uint32_t binding, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize stride, VkVertexInputRate rate)
{
	if (binding >= GRANITE_NUM_VERTEX_BUFFERS)
		return;

	if (state->vbo_buffers[binding] != buffer || state->vbo_offsets[binding] != offset)
		state->dirty_vbos |= (uint32_t)1 << binding;
	if (compile->strides[binding] != stride || compile->input_rates[binding] != rate)
		state->dirty |= GRANITE_COMMAND_BUFFER_DIRTY_STATIC_VERTEX_BIT;

	state->vbo_buffers[binding] = buffer;
	state->vbo_offsets[binding] = offset;
	compile->strides[binding] = stride;
	compile->input_rates[binding] = rate;
}

void granite_command_state_set_program(granite_command_state_t *state, const granite_bind_layout_t *layout)
{
	state->pipeline.pipeline = VK_NULL_HANDLE;
	state->pipeline.dynamic_mask = 0;
	state->dirty |= GRANITE_COMMAND_BUFFER_DIRTY_PIPELINE_BIT;
	if (!layout)
		return;

	if (granite_bindings_set_layout(&state->bindings, state->layout, layout))
		state->dirty |= GRANITE_COMMAND_BUFFER_DIRTY_PUSH_CONSTANTS_BIT;
	state->layout = layout;
}

bool granite_command_state_pipeline_dirty(granite_command_state_t *state)
{
	uint32_t baked = GRANITE_COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT | GRANITE_COMMAND_BUFFER_DIRTY_PIPELINE_BIT;

	/* the vertex layout is not part of a compute pipeline */
	if (!state->is_compute)
		baked |= GRANITE_COMMAND_BUFFER_DIRTY_STATIC_VERTEX_BIT;

	if (state->pipeline.pipeline == VK_NULL_HANDLE)
		state->dirty |= GRANITE_COMMAND_BUFFER_DIRTY_PIPELINE_BIT;
	return get_and_clear(state, baked) != 0;
}

void granite_command_state_set_pipeline(granite_command_state_t *state, const granite_pipeline_t *pipeline)
{
	if (pipeline->pipeline != VK_NULL_HANDLE && pipeline->pipeline != state->pipeline.pipeline)
	{
		state->table->vkCmdBindPipeline(state->cmd,
				state->is_compute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS,
				pipeline->pipeline);
		/* What this pipeline bakes in overwrote that dynamic state: it has
		 * to be set again for one that leaves it dynamic. */
		state->dirty |= ~pipeline->dynamic_mask & GRANITE_COMMAND_BUFFER_DYNAMIC_BITS;
	}
	state->pipeline = *pipeline;
}

bool granite_command_state_flush(granite_command_state_t *state)
{
	const struct VolkDeviceTable *table = state->table;
	uint32_t update_vbos;
	unsigned binding;

	if (state->pipeline.pipeline == VK_NULL_HANDLE || !state->layout)
		return false;

	granite_bindings_flush(&state->bindings, state->layout, state->device, table, state->cmd,
			state->is_compute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS,
			state->thread_index);

	if (get_and_clear(state, GRANITE_COMMAND_BUFFER_DIRTY_PUSH_CONSTANTS_BIT))
	{
		const VkPushConstantRange *range = &state->layout->resource_layout->push_constant_range;

		if (range->stageFlags != 0)
			table->vkCmdPushConstants(state->cmd, state->layout->layout, range->stageFlags, 0, range->size,
					state->bindings.push_constant_data);
	}

	if (state->is_compute)
		return true;

	if (get_and_clear(state, GRANITE_COMMAND_BUFFER_DIRTY_VIEWPORT_BIT))
		table->vkCmdSetViewport(state->cmd, 0, 1, &state->viewport);

	if (get_and_clear(state, GRANITE_COMMAND_BUFFER_DIRTY_SCISSOR_BIT))
	{
		/* Vulkan takes no negative offset, and no extent past 2^31 */
		VkRect2D scissor = state->scissor;

		if (scissor.offset.x < 0)
		{
			scissor.extent.width += scissor.offset.x;
			scissor.offset.x = 0;
		}
		if (scissor.offset.y < 0)
		{
			scissor.extent.height += scissor.offset.y;
			scissor.offset.y = 0;
		}
		if (scissor.extent.width > 0x7fffffffu - scissor.offset.x)
			scissor.extent.width = 0x7fffffffu - scissor.offset.x;
		if (scissor.extent.height > 0x7fffffffu - scissor.offset.y)
			scissor.extent.height = 0x7fffffffu - scissor.offset.y;
		table->vkCmdSetScissor(state->cmd, 0, 1, &scissor);
	}

	/* the dirty vertex buffers the pipeline reads, a run of them a call */
	update_vbos = state->dirty_vbos & state->active_vbos;
	for (binding = 0; binding < GRANITE_NUM_VERTEX_BUFFERS; binding++)
	{
		unsigned count = 0;

		while (binding + count < GRANITE_NUM_VERTEX_BUFFERS && (update_vbos & ((uint32_t)1 << (binding + count))))
			count++;
		if (!count)
			continue;

		table->vkCmdBindVertexBuffers(state->cmd, binding, count, state->vbo_buffers + binding,
				state->vbo_offsets + binding);
		binding += count;
	}
	state->dirty_vbos &= ~update_vbos;

	return true;
}
