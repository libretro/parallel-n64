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
#include "command_buffer.h"
#include "command_ops.h"
#include "pipeline_record.h"
#include "logging.h"

static granite_image_ref_t image_ref(const granite_image_t *image)
{
	granite_image_ref_t ref;
	ref.image = image->image;
	ref.format = image->info.format;
	ref.width = image->info.width;
	ref.height = image->info.height;
	ref.depth = image->info.depth;
	ref.levels = image->info.levels;
	ref.layers = image->info.layers;
	ref.general = image->general_layout;
	return ref;
}

/* A render pass (or, @compute, the time outside one) begins */
static void begin_context(granite_command_buffer_t *cb, bool compute)
{
	granite_command_state_begin(&cb->state, compute);
	cb->pipeline_state.program = NULL;
	cb->pipeline_state.potential_static_state.spec_constant_mask = 0;
	cb->pipeline_state.potential_static_state.internal_spec_constant_mask = 0;
}

void granite_cb_pool_slots_init(granite_lf_pool_t *pool)
{
	granite_lf_pool_init(pool, sizeof(granite_command_buffer_t), 16);
}

granite_command_buffer_t *granite_cb_new(granite_lf_pool_t *pool, const granite_cmd_device_t *dev,
		VkCommandBuffer cmd, VkPipelineCache cache, unsigned type)
{
	const VkPhysicalDeviceSubgroupSizeControlPropertiesEXT *subgroups = &dev->features->subgroup_size_control_properties;
	granite_command_buffer_t *cb = (granite_command_buffer_t*)granite_lf_pool_alloc(pool);

	if (!cb)
		return NULL;
	memset(cb, 0, sizeof(*cb));
	granite_refs_init(&cb->refs);
	cb->pool = pool;
	cb->dev = dev;
	cb->table = dev->table;
	cb->cmd = cmd;
	cb->type = type;
	cb->pipeline_state.cache = cache;
	/* (begins as compute, with everything dirty) */
	granite_command_state_init(&cb->state, dev->device, dev->table, cmd);
	begin_context(cb, true);
	/* what pipelines depend on without it being state: part of their hash */
	cb->pipeline_state.subgroup_size_tag = (subgroups->minSubgroupSize << 0) | (subgroups->maxSubgroupSize << 8);
	return cb;
}

/* ---- transfers and barriers ---- */

void granite_cb_fill_buffer(granite_command_buffer_t *cb, const granite_buffer_t *dst, uint32_t value,
		VkDeviceSize offset, VkDeviceSize size)
{
	cb->table->vkCmdFillBuffer(cb->cmd, dst->buffer, offset, size, value);
}

void granite_cb_copy_buffer(granite_command_buffer_t *cb, const granite_buffer_t *dst, const granite_buffer_t *src,
		const VkBufferCopy *copies, size_t count)
{
	cb->table->vkCmdCopyBuffer(cb->cmd, src->buffer, dst->buffer, (uint32_t)count, copies);
}

void granite_cb_copy_buffer_to_image(granite_command_buffer_t *cb, const granite_image_t *image,
		const granite_buffer_t *buffer, unsigned num_blits, const VkBufferImageCopy *blits)
{
	cb->table->vkCmdCopyBufferToImage(cb->cmd, buffer->buffer, image->image,
			granite_image_layout(image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL), num_blits, blits);
}

void granite_cb_clear_image(granite_command_buffer_t *cb, const granite_image_t *image, const VkClearValue *value,
		VkImageAspectFlags aspect)
{
	granite_image_ref_t ref = image_ref(image);
	granite_cmd_clear_image(cb->table, cb->cmd, &ref, value, aspect);
}

void granite_cb_barrier(granite_command_buffer_t *cb, VkPipelineStageFlags src_stages,
		VkPipelineStageFlags dst_stages, unsigned barriers, const VkMemoryBarrier *globals,
		unsigned buffer_barriers, const VkBufferMemoryBarrier *buffers, unsigned image_barriers,
		const VkImageMemoryBarrier *images)
{
	src_stages = granite_barrier_src_stages(src_stages, cb->dev->workarounds->optimize_all_graphics_barrier);
	cb->table->vkCmdPipelineBarrier(cb->cmd, src_stages, dst_stages, 0, barriers, globals, buffer_barriers,
			buffers, image_barriers, images);
}

void granite_cb_image_barrier(granite_command_buffer_t *cb, const granite_image_t *image,
		VkImageLayout old_layout, VkImageLayout new_layout, VkPipelineStageFlags src_stages,
		VkAccessFlags src_access, VkPipelineStageFlags dst_stages, VkAccessFlags dst_access)
{
	granite_image_ref_t ref = image_ref(image);
	granite_cmd_image_barrier(cb->table, cb->cmd, &ref, old_layout, new_layout,
			granite_barrier_src_stages(src_stages, cb->dev->workarounds->optimize_all_graphics_barrier),
			src_access, dst_stages, dst_access);
}

void granite_cb_blit_image(granite_command_buffer_t *cb, const granite_image_t *dst, const granite_image_t *src,
		const VkOffset3D *dst_offset, const VkOffset3D *dst_extent, const VkOffset3D *src_offset,
		const VkOffset3D *src_extent, unsigned dst_level, unsigned src_level, unsigned dst_base_layer,
		unsigned src_base_layer, unsigned num_layers, VkFilter filter)
{
	granite_image_ref_t dst_ref = image_ref(dst);
	granite_image_ref_t src_ref = image_ref(src);
	granite_cmd_blit_image(cb->table, cb->cmd, &dst_ref, &src_ref, dst_offset, dst_extent, src_offset, src_extent,
			dst_level, src_level, dst_base_layer, src_base_layer, num_layers, filter);
}

/* ---- render passes ---- */

bool granite_cb_begin_render_pass(granite_command_buffer_t *cb, const granite_render_pass_info_t *info)
{
	const granite_framebuffer_t *framebuffer = granite_render_target_framebuffer(cb->dev->framebuffers,
			cb->dev->programs, info);
	const granite_cached_render_pass_t *pass = granite_render_target_pass(cb->dev->programs, info, false);
	VkClearValue clear_values[GRANITE_NUM_ATTACHMENTS + 1];
	VkRenderPassBeginInfo begin_info;

	if (!framebuffer || !pass)
		return false;

	cb->framebuffer = framebuffer;
	cb->actual_render_pass = pass;
	cb->pipeline_state.compatible_render_pass = (const granite_cached_render_pass_t*)framebuffer->render_pass;
	cb->pipeline_state.subpass_index = 0;

	memset(&begin_info, 0, sizeof(begin_info));
	begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	begin_info.renderPass = pass->impl.render_pass;
	begin_info.framebuffer = framebuffer->framebuffer;
	begin_info.renderArea = granite_command_state_set_render_area(&cb->state, &info->render_area,
			framebuffer->width, framebuffer->height);
	begin_info.clearValueCount = granite_render_pass_info_clear_values(info, clear_values);
	begin_info.pClearValues = clear_values;
	cb->table->vkCmdBeginRenderPass(cb->cmd, &begin_info, VK_SUBPASS_CONTENTS_INLINE);

	begin_context(cb, false);
	return true;
}

void granite_cb_end_render_pass(granite_command_buffer_t *cb)
{
	cb->table->vkCmdEndRenderPass(cb->cmd);
	cb->framebuffer = NULL;
	cb->actual_render_pass = NULL;
	cb->pipeline_state.compatible_render_pass = NULL;
	begin_context(cb, true);
}

/* ---- pipelines ---- */

static granite_pipeline_device_t pipeline_device(const granite_cmd_device_t *dev)
{
	const granite_device_features_t *features = dev->features;
	granite_pipeline_device_t d;
	d.device = dev->device;
	d.table = dev->table;
	d.cache_control = !dev->workarounds->broken_pipeline_cache_control
		&& features->pipeline_creation_cache_control_features.pipelineCreationCacheControl;
	d.min_subgroup_size = features->subgroup_size_control_properties.minSubgroupSize;
	d.max_subgroup_size = features->subgroup_size_control_properties.maxSubgroupSize;
	return d;
}

static granite_pipeline_program_t pipeline_program(const granite_program_t *program)
{
	granite_pipeline_program_t p;
	unsigned i;

	p.hash = program->node.hash;
	p.layout = program->layout->impl.layout;
	p.resource_layout = &program->layout->layout;
	for (i = 0; i < GRANITE_SHADER_STAGE_COUNT; i++)
		p.modules[i] = program->shaders[i] ? program->shaders[i]->module : VK_NULL_HANDLE;
	return p;
}

/* Keeps the pipeline under its hash in the program. If another thread
 * got there first, or there is no memory to keep it, this one goes. */
static granite_pipeline_t keep_pipeline(const granite_cmd_device_t *dev, const granite_pipeline_compile_t *compile,
		VkPipeline pipeline, uint32_t dynamic_mask, uint32_t kind)
{
	granite_pipeline_t made, kept;

	made.pipeline = pipeline;
	made.dynamic_mask = dynamic_mask;
	if (pipeline == VK_NULL_HANDLE)
		return made;

	kept = granite_pipeline_map_add(&compile->program->pipelines, compile->hash, &made);
	if (kept.pipeline != pipeline)
		dev->table->vkDestroyPipeline(dev->device, pipeline, NULL);
	else if (dev->record_pipeline)
		dev->record_pipeline(dev->user, compile, kind);
	return kept;
}

void granite_pipeline_compile_update_hash_compute(granite_pipeline_compile_t *compile)
{
	granite_pipeline_program_t program = pipeline_program(compile->program);
	compile->hash = granite_pipeline_hash_compute(compile, &program);
}

void granite_pipeline_compile_update_hash_graphics(granite_pipeline_compile_t *compile, uint32_t *active_vbos)
{
	granite_pipeline_program_t program = pipeline_program(compile->program);
	compile->hash = granite_pipeline_hash_graphics(compile, &program, compile->compatible_render_pass->node.hash,
			active_vbos);
}

granite_pipeline_t granite_pipeline_compile_build_compute(const granite_cmd_device_t *dev,
		const granite_pipeline_compile_t *compile, enum granite_compile_mode mode)
{
	granite_pipeline_device_t device = pipeline_device(dev);
	granite_pipeline_program_t program = pipeline_program(compile->program);
	VkPipeline pipeline;

	if (     compile->static_state.state.subgroup_control_size
			&& !granite_subgroup_size_supported(&dev->features->subgroup_size_control_features,
				&dev->features->subgroup_size_control_properties,
				compile->static_state.state.subgroup_full_group != 0,
				compile->static_state.state.subgroup_minimum_size_log2,
				compile->static_state.state.subgroup_maximum_size_log2))
	{
		granite_pipeline_t none;
		LOGE("Subgroup size configuration not supported.\n");
		none.pipeline = VK_NULL_HANDLE;
		none.dynamic_mask = 0;
		return none;
	}

	pipeline = granite_pipeline_build_compute(&device, compile, &program,
			mode == GRANITE_COMPILE_FAIL_ON_COMPILE_REQUIRED, NULL);
	return keep_pipeline(dev, compile, pipeline, 0, PIPELINE_RECORD_COMPUTE);
}

granite_pipeline_t granite_pipeline_compile_build_graphics(const granite_cmd_device_t *dev,
		const granite_pipeline_compile_t *compile, enum granite_compile_mode mode)
{
	granite_pipeline_device_t device = pipeline_device(dev);
	granite_pipeline_program_t program = pipeline_program(compile->program);
	uint32_t dynamic_mask = 0;
	VkPipeline pipeline = granite_pipeline_build_graphics(&device, compile, &program,
			&compile->compatible_render_pass->impl, mode == GRANITE_COMPILE_FAIL_ON_COMPILE_REQUIRED,
			&dynamic_mask, NULL);
	return keep_pipeline(dev, compile, pipeline, dynamic_mask, PIPELINE_RECORD_GRAPHICS);
}

void granite_cb_extract_pipeline_state(const granite_command_buffer_t *cb, granite_pipeline_compile_t *compile)
{
	*compile = cb->pipeline_state;

	if (!compile->program)
	{
		LOGE("Attempting to extract pipeline state when no program is bound.\n");
		return;
	}

	if (cb->state.is_compute)
		granite_pipeline_compile_update_hash_compute(compile);
	else
	{
		uint32_t active_vbos = 0;
		granite_pipeline_compile_update_hash_graphics(compile, &active_vbos);
	}
}

bool granite_cb_flush_state(granite_command_buffer_t *cb, bool synchronous)
{
	if (!cb->pipeline_state.program)
		return false;

	if (granite_command_state_pipeline_dirty(&cb->state))
	{
		enum granite_compile_mode mode = synchronous ? GRANITE_COMPILE_SYNC : GRANITE_COMPILE_FAIL_ON_COMPILE_REQUIRED;
		granite_pipeline_t pipeline;

		if (cb->state.is_compute)
			granite_pipeline_compile_update_hash_compute(&cb->pipeline_state);
		else
			granite_pipeline_compile_update_hash_graphics(&cb->pipeline_state, &cb->state.active_vbos);

		pipeline = granite_pipeline_map_find(&cb->pipeline_state.program->pipelines, cb->pipeline_state.hash);
		if (pipeline.pipeline == VK_NULL_HANDLE)
			pipeline = cb->state.is_compute
				? granite_pipeline_compile_build_compute(cb->dev, &cb->pipeline_state, mode)
				: granite_pipeline_compile_build_graphics(cb->dev, &cb->pipeline_state, mode);
		granite_command_state_set_pipeline(&cb->state, &pipeline);
	}

	return granite_command_state_flush(&cb->state);
}

/* ---- state ---- */

void granite_cb_set_program(granite_command_buffer_t *cb, granite_program_t *program)
{
	if (cb->pipeline_state.program == program)
		return;
	cb->pipeline_state.program = program;
	granite_command_state_set_program(&cb->state, program ? &program->layout->bind : NULL);
}

void granite_cb_set_uniform_buffer(granite_command_buffer_t *cb, unsigned set, unsigned binding,
		const granite_buffer_t *buffer, VkDeviceSize offset, VkDeviceSize range)
{
	granite_bindings_set_uniform_buffer(&cb->state.bindings, set, binding, buffer->buffer, buffer->cookie, offset,
			range);
}

void granite_cb_set_storage_buffer(granite_command_buffer_t *cb, unsigned set, unsigned binding,
		const granite_buffer_t *buffer, VkDeviceSize offset, VkDeviceSize range)
{
	granite_bindings_set_storage_buffer(&cb->state.bindings, set, binding, buffer->buffer, buffer->cookie, offset,
			range);
}

void granite_cb_set_buffer_view(granite_command_buffer_t *cb, unsigned set, unsigned binding,
		const granite_buffer_view_t *view)
{
	granite_bindings_set_buffer_view(&cb->state.bindings, set, binding, view->view, view->cookie);
}

void granite_cb_set_sampler(granite_command_buffer_t *cb, unsigned set, unsigned binding,
		const granite_sampler_t *sampler)
{
	granite_bindings_set_sampler(&cb->state.bindings, set, binding, sampler->sampler, sampler->cookie);
}

void granite_cb_set_texture(granite_command_buffer_t *cb, unsigned set, unsigned binding,
		const granite_image_view_t *view, int stock_sampler)
{
	if (stock_sampler >= 0)
		granite_cb_set_sampler(cb, set, binding, &cb->dev->stock_samplers[stock_sampler]);
	granite_bindings_set_texture(&cb->state.bindings, set, binding, granite_image_view_depth(view),
			granite_image_view_stencil(view),
			granite_image_layout(view->info.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL), view->cookie);
}

void granite_cb_set_storage_texture(granite_command_buffer_t *cb, unsigned set, unsigned binding,
		const granite_image_view_t *view)
{
	granite_bindings_set_texture(&cb->state.bindings, set, binding, granite_image_view_depth(view),
			granite_image_view_stencil(view), granite_image_layout(view->info.image, VK_IMAGE_LAYOUT_GENERAL),
			view->cookie);
}

void granite_cb_push_constants(granite_command_buffer_t *cb, const void *data, VkDeviceSize offset,
		VkDeviceSize range)
{
	granite_command_state_push_constants(&cb->state, data, (size_t)offset, (size_t)range);
}

/* @size bytes of @block, from a new block when it has no room */
static granite_buffer_block_allocation_t allocate_from_block(granite_command_buffer_t *cb, bool vertex,
		granite_buffer_block_t *block, VkDeviceSize size)
{
	granite_buffer_block_allocation_t data = granite_buffer_block_allocate(block, size);
	if (!data.host)
	{
		cb->dev->request_block(cb->dev->user, vertex, block, size);
		data = granite_buffer_block_allocate(block, size);
	}
	return data;
}

void *granite_cb_allocate_constant_data(granite_command_buffer_t *cb, unsigned set, unsigned binding,
		VkDeviceSize size)
{
	granite_buffer_block_allocation_t data = allocate_from_block(cb, false, &cb->ubo_block, size);
	/* out of memory: nothing to write to or bind */
	if (!data.host)
		return NULL;
	granite_cb_set_uniform_buffer(cb, set, binding, (const granite_buffer_t*)cb->ubo_block.handle, data.offset,
			data.padded_size);
	return data.host;
}

void *granite_cb_allocate_vertex_data(granite_command_buffer_t *cb, unsigned binding, VkDeviceSize size,
		VkDeviceSize stride, VkVertexInputRate step_rate)
{
	granite_buffer_block_allocation_t data = allocate_from_block(cb, true, &cb->vbo_block, size);
	if (!data.host)
		return NULL;
	granite_cb_set_vertex_binding(cb, binding, (const granite_buffer_t*)cb->vbo_block.handle, data.offset, stride,
			step_rate);
	return data.host;
}

void granite_cb_set_scissor(granite_command_buffer_t *cb, const VkRect2D *rect)
{
	granite_command_state_set_scissor(&cb->state, rect);
}

void granite_cb_set_vertex_attrib(granite_command_buffer_t *cb, uint32_t attrib, uint32_t binding,
		VkFormat format, VkDeviceSize offset)
{
	granite_command_state_set_vertex_attrib(&cb->state, &cb->pipeline_state, attrib, binding, format,
			(uint32_t)offset);
}

void granite_cb_set_vertex_binding(granite_command_buffer_t *cb, uint32_t binding, const granite_buffer_t *buffer,
		VkDeviceSize offset, VkDeviceSize stride, VkVertexInputRate step_rate)
{
	granite_command_state_set_vertex_binding(&cb->state, &cb->pipeline_state, binding, buffer->buffer, offset,
			stride, step_rate);
}

void granite_cb_set_opaque_state(granite_command_buffer_t *cb)
{
	memset(&cb->pipeline_state.static_state.state, 0, sizeof(cb->pipeline_state.static_state.state));
	cb->pipeline_state.static_state.state.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	cb->pipeline_state.static_state.state.cull_mode = VK_CULL_MODE_BACK_BIT;
	cb->pipeline_state.static_state.state.depth_test = true;
	cb->pipeline_state.static_state.state.depth_compare = VK_COMPARE_OP_LESS_OR_EQUAL;
	cb->pipeline_state.static_state.state.depth_write = true;
	cb->pipeline_state.static_state.state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	cb->pipeline_state.static_state.state.write_mask = ~0u;
	granite_cb_set_dirty(cb, GRANITE_COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT);
}

/* ---- drawing ---- */

void granite_cb_draw(granite_command_buffer_t *cb, uint32_t vertex_count, uint32_t instance_count,
		uint32_t first_vertex, uint32_t first_instance)
{
	if (granite_cb_flush_state(cb, true))
		cb->table->vkCmdDraw(cb->cmd, vertex_count, instance_count, first_vertex, first_instance);
	else
		LOGE("Failed to flush render state, draw call will be dropped.\n");
}

void granite_cb_dispatch(granite_command_buffer_t *cb, uint32_t groups_x, uint32_t groups_y, uint32_t groups_z)
{
	if (granite_cb_flush_state(cb, true))
		cb->table->vkCmdDispatch(cb->cmd, groups_x, groups_y, groups_z);
	else
		LOGE("Failed to flush render state, dispatch will be dropped.\n");
}

void granite_cb_dispatch_indirect(granite_command_buffer_t *cb, const granite_buffer_t *buffer, uint32_t offset)
{
	if (granite_cb_flush_state(cb, true))
		cb->table->vkCmdDispatchIndirect(cb->cmd, buffer->buffer, offset);
	else
		LOGE("Failed to flush render state, dispatch will be dropped.\n");
}

/* ---- ending ---- */

void granite_cb_end_recording(granite_command_buffer_t *cb)
{
	if (cb->is_ended)
		return;
	cb->is_ended = true;
	if (cb->table->vkEndCommandBuffer(cb->cmd) != VK_SUCCESS)
		LOGE("Failed to end command buffer.\n");
}

void granite_cb_end(granite_command_buffer_t *cb)
{
	granite_cb_end_recording(cb);
	if (cb->vbo_block.mapped)
		cb->dev->request_block(cb->dev->user, true, &cb->vbo_block, 0);
	if (cb->ubo_block.mapped)
		cb->dev->request_block(cb->dev->user, false, &cb->ubo_block, 0);
}
