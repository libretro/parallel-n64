/* Copyright (c) 2026 The RetroArch team
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

#include <stddef.h>
#include <string.h>
#include "upscaler_pass.h"
#include "command_ops.h"
#include "pipeline_record.h"
#include "upscaler_shaders.hpp"

/* Granite is built without SPIR-V reflection: the layouts are spelt out */
granite_program_t *rdp_upscaler_color_program(granite_device_t *device)
{
	granite_resource_layout_t layout;

	memset(&layout, 0, sizeof(layout));
	layout.sets[0].storage_image_mask = 1u << 0;
	layout.sets[0].storage_buffer_mask = 1u << 1;
	layout.sets[0].sampled_texel_buffer_mask = 1u << 2;
	layout.sets[0].fp_mask = 1u << 0;
	layout.sets[0].array_size[0] = 1;
	layout.sets[0].array_size[1] = 1;
	layout.sets[0].array_size[2] = 1;
	layout.push_constant_size = 24;
	layout.spec_constant_mask = 0xf;
	return granite_program_cache_compute(&device->program_cache,
			granite_program_cache_shader(&device->program_cache, upscaler_color_comp,
				sizeof(upscaler_color_comp), &layout));
}

static granite_program_t *motion_program(granite_device_t *device)
{
	granite_resource_layout_t vert;
	granite_resource_layout_t frag;

	memset(&vert, 0, sizeof(vert));
	memset(&frag, 0, sizeof(frag));
	vert.input_mask = 0xff;
	vert.push_constant_size = 8;
	frag.output_mask = 3;
	frag.push_constant_size = 28;
	return granite_program_cache_graphics(&device->program_cache,
			granite_program_cache_shader(&device->program_cache, upscaler_motion_vert,
				sizeof(upscaler_motion_vert), &vert),
			granite_program_cache_shader(&device->program_cache, upscaler_motion_frag,
				sizeof(upscaler_motion_frag), &frag));
}

static VkFormat depth_format(granite_device_t *device)
{
	VkFormatFeatureFlags2KHR need = VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT_KHR
		| VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT_KHR;
	if (granite_image_format_is_supported(device->gpu, VK_FORMAT_D32_SFLOAT, need, VK_IMAGE_TILING_OPTIMAL,
				device->ext.supports_format_feature_flags2))
		return VK_FORMAT_D32_SFLOAT;
	return VK_FORMAT_D16_UNORM;
}

/* A 2D render target of one level; @usage 0 for the attachment and
 * transfers */
static granite_image_t *create_target(granite_device_t *device, unsigned width, unsigned height, VkFormat format,
		VkImageUsageFlags usage)
{
	const bool depth = (granite_format_aspect_mask(format) & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) != 0;
	granite_image_info_t info;

	memset(&info, 0, sizeof(info));
	info.width = width;
	info.height = height;
	info.depth = 1;
	info.levels = 1;
	info.layers = 1;
	info.format = format;
	info.type = VK_IMAGE_TYPE_2D;
	info.samples = VK_SAMPLE_COUNT_1_BIT;
	info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	if (usage)
		info.usage = usage | (depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
	else
		info.usage = (depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
			| VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	return granite_device_create_image(device, &info, NULL);
}

static void set_depth_test(granite_command_buffer_t *cb, bool test, bool write)
{
	granite_pipeline_state_t *state = &cb->pipeline_state.static_state;
	if (state->state.depth_test != (unsigned)test || state->state.depth_write != (unsigned)write)
	{
		state->state.depth_test = test;
		state->state.depth_write = write;
		granite_cb_set_dirty(cb, GRANITE_COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT);
	}
}

static void set_motion_pipeline(granite_device_t *device, granite_command_buffer_t *cb)
{
	unsigned i;

	granite_cb_set_opaque_state(cb);
	cb->pipeline_state.static_state.state.cull_mode = VK_CULL_MODE_NONE;
	cb->pipeline_state.static_state.state.depth_compare = VK_COMPARE_OP_LESS_OR_EQUAL;
	granite_cb_set_dirty(cb, GRANITE_COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT);
	granite_cb_set_program(cb, motion_program(device));
	granite_cb_set_vertex_attrib(cb, 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(rdp_upscaler_vertex_t, x));
	granite_cb_set_vertex_attrib(cb, 1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(rdp_upscaler_vertex_t, motion_x));
	for (i = 0; i < 6; i++)
		granite_cb_set_vertex_attrib(cb, 2 + i, 0, VK_FORMAT_R32G32B32_SFLOAT,
				offsetof(rdp_upscaler_vertex_t, plane) + i * 3 * sizeof(float));
}

/* The three new targets go from no layout to being drawn to */
static void begin_targets(granite_command_buffer_t *cb, const granite_image_t *motion,
		const granite_image_t *translucent, const granite_image_t *depth)
{
	granite_cb_image_barrier(cb, motion, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
	granite_cb_image_barrier(cb, translucent, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
	granite_cb_image_barrier(cb, depth, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
}

static void pass_info(granite_render_pass_info_t *rp, const granite_image_t *motion,
		const granite_image_t *translucent, const granite_image_t *depth)
{
	granite_render_pass_info_init(rp);
	rp->color_attachments[0] = motion->view;
	rp->color_attachments[1] = translucent->view;
	rp->depth_stencil = depth->view;
	rp->num_color_attachments = 2;
	rp->store_attachments = 3;
	rp->op_flags = GRANITE_RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT | GRANITE_RENDER_PASS_OP_STORE_DEPTH_STENCIL_BIT;
}

void rdp_upscaler_queue_known_pipelines(granite_device_t *device, granite_command_buffer_t *cb,
		rdp_known_pipeline_fn add, void *ctx)
{
	granite_image_t *motion = create_target(device, 1, 1, VK_FORMAT_R16G16_SFLOAT, 0);
	granite_image_t *depth = create_target(device, 1, 1, depth_format(device), 0);
	granite_image_t *translucent = create_target(device, 1, 1, VK_FORMAT_R8_UNORM, 0);
	granite_render_pass_info_t rp;
	unsigned test, write;

	if (motion && depth && translucent)
	{
		begin_targets(cb, motion, translucent, depth);
		pass_info(&rp, motion, translucent, depth);
		if (granite_cb_begin_render_pass(cb, &rp))
		{
			granite_cb_allocate_vertex_data(cb, 0, sizeof(rdp_upscaler_vertex_t), sizeof(rdp_upscaler_vertex_t),
					VK_VERTEX_INPUT_RATE_VERTEX);
			set_motion_pipeline(device, cb);
			for (test = 0; test < 2; test++)
				for (write = 0; write < 2; write++)
				{
					set_depth_test(cb, test != 0, write != 0);
					add(ctx, cb, PIPELINE_RECORD_GRAPHICS);
				}
			granite_cb_end_render_pass(cb);
		}
	}

	/* (theirs until the frame they were used in comes round) */
	if (motion)
		granite_image_unref(motion);
	if (depth)
		granite_image_unref(depth);
	if (translucent)
		granite_image_unref(translucent);
}

/* Drawn to: to where the upscaler samples them */
static void end_target(granite_command_buffer_t *cb, const granite_image_t *image, bool depth)
{
	granite_cb_image_barrier(cb, image,
			depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			depth ? VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT : VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			depth ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_ACCESS_SHADER_READ_BIT);
}

void rdp_upscaler_motion_pass(granite_device_t *device, granite_command_buffer_t *cb,
		const rdp_upscaler_geometry_t *geometry, unsigned width, unsigned height,
		rdp_upscaler_targets_t *targets)
{
	granite_image_t *motion = create_target(device, width, height, VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT);
	granite_image_t *depth = create_target(device, width, height, depth_format(device), VK_IMAGE_USAGE_SAMPLED_BIT);
	granite_image_t *translucent = create_target(device, width, height, VK_FORMAT_R8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);
	granite_render_pass_info_t rp;
	unsigned i;

	memset(targets, 0, sizeof(*targets));

	if (motion && depth && translucent)
	{
		begin_targets(cb, motion, translucent, depth);
		pass_info(&rp, motion, translucent, depth);
		rp.clear_attachments = 3;
	}
	if (!motion || !depth || !translucent || !granite_cb_begin_render_pass(cb, &rp))
	{
		if (motion)
			granite_image_unref(motion);
		if (depth)
			granite_image_unref(depth);
		if (translucent)
			granite_image_unref(translucent);
		return;
	}

	if (geometry->num_vertices)
	{
		void *vertices;
		float inv_resolution[2];
		float planes[3];

		vertices = granite_cb_allocate_vertex_data(cb, 0, geometry->num_vertices * sizeof(rdp_upscaler_vertex_t),
				sizeof(rdp_upscaler_vertex_t), VK_VERTEX_INPUT_RATE_VERTEX);
		memcpy(vertices, geometry->vertices, geometry->num_vertices * sizeof(rdp_upscaler_vertex_t));
		set_motion_pipeline(device, cb);
		inv_resolution[0] = 1.0f / (float)width;
		inv_resolution[1] = 1.0f / (float)height;
		granite_cb_push_constants(cb, inv_resolution, 0, sizeof(inv_resolution));
		planes[0] = geometry->jitter_x + (float)width * 0.5f;
		planes[1] = geometry->jitter_y + (float)height * 0.5f;
		planes[2] = geometry->farthest;
		granite_cb_push_constants(cb, planes, 16, sizeof(planes));

		for (i = 0; i < geometry->num_draws; i++)
		{
			const rdp_upscaler_draw_t *draw = &geometry->draws[i];
			float translucency = (draw->flags & RDP_UPSCALER_TRANSLUCENT) ? 1.0f : 0.0f;
			set_depth_test(cb, (draw->flags & RDP_UPSCALER_DEPTH_TEST) != 0,
					(draw->flags & RDP_UPSCALER_DEPTH_WRITE) != 0);
			granite_cb_push_constants(cb, &translucency, 8, sizeof(translucency));
			granite_cb_draw(cb, draw->count, 1, draw->first, 0);
		}
	}

	granite_cb_end_render_pass(cb);

	end_target(cb, motion, false);
	end_target(cb, translucent, false);
	end_target(cb, depth, true);

	targets->motion_vectors = motion;
	targets->depth = depth;
	targets->translucent = translucent;
	targets->jitter_x = geometry->jitter_x;
	targets->jitter_y = geometry->jitter_y;
	targets->reset = geometry->reset;
}
