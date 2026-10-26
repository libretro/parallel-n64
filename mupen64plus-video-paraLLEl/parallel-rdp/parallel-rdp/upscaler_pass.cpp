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
#include "upscaler_pass.hpp"
#include "upscaler_shaders.hpp"

namespace RDP
{
/* Granite is built without SPIR-V reflection: the layouts are spelt out */
Vulkan::Program *upscaler_color_program(Vulkan::Device &device)
{
	Vulkan::ResourceLayout layout;
	layout.sets[0].storage_image_mask = 1u << 0;
	layout.sets[0].storage_buffer_mask = 1u << 1;
	layout.sets[0].sampled_texel_buffer_mask = 1u << 2;
	layout.sets[0].fp_mask = 1u << 0;
	layout.sets[0].array_size[0] = 1;
	layout.sets[0].array_size[1] = 1;
	layout.sets[0].array_size[2] = 1;
	layout.push_constant_size = 16;
	layout.spec_constant_mask = 0xf;
	return device.request_program(UpscalerShaders::upscaler_color_comp,
	                              sizeof(UpscalerShaders::upscaler_color_comp), &layout);
}

static Vulkan::Program *upscaler_motion_program(Vulkan::Device &device)
{
	Vulkan::ResourceLayout vert;
	Vulkan::ResourceLayout frag;
	vert.input_mask = 3;
	vert.push_constant_size = 8;
	frag.output_mask = 3;
	frag.push_constant_size = 12;
	return device.request_program(UpscalerShaders::upscaler_motion_vert,
	                              sizeof(UpscalerShaders::upscaler_motion_vert),
	                              UpscalerShaders::upscaler_motion_frag,
	                              sizeof(UpscalerShaders::upscaler_motion_frag),
	                              &vert, &frag);
}

static VkFormat upscaler_depth_format(Vulkan::Device &device)
{
	VkFormatFeatureFlags2KHR need = VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT_KHR |
	                                VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT_KHR;
	if (device.image_format_is_supported(VK_FORMAT_D32_SFLOAT, need))
		return VK_FORMAT_D32_SFLOAT;
	return VK_FORMAT_D16_UNORM;
}

void upscaler_motion_pass(Vulkan::Device &device, Vulkan::CommandBuffer &cmd,
                          const rdp_upscaler_geometry_t &geometry,
                          unsigned width, unsigned height, UpscalerFrame &frame)
{
	Vulkan::ImageCreateInfo info;
	Vulkan::RenderPassInfo rp;
	unsigned i;

	info = Vulkan::ImageCreateInfo::render_target(width, height, VK_FORMAT_R16G16_SFLOAT);
	info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	frame.motion_vectors = device.create_image(info);

	info = Vulkan::ImageCreateInfo::render_target(width, height, upscaler_depth_format(device));
	info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	frame.depth = device.create_image(info);

	info = Vulkan::ImageCreateInfo::render_target(width, height, VK_FORMAT_R8_UNORM);
	info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	frame.translucent = device.create_image(info);

	if (!frame.motion_vectors || !frame.depth || !frame.translucent)
	{
		frame.motion_vectors.reset();
		frame.depth.reset();
		frame.translucent.reset();
		return;
	}

	cmd.image_barrier(*frame.motion_vectors, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
	                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
	cmd.image_barrier(*frame.translucent, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
	                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
	cmd.image_barrier(*frame.depth, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
	                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
	                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
	                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

	rp.color_attachments[0] = &frame.motion_vectors->get_view();
	rp.color_attachments[1] = &frame.translucent->get_view();
	rp.depth_stencil = &frame.depth->get_view();
	rp.num_color_attachments = 2;
	rp.clear_attachments = 3;
	rp.store_attachments = 3;
	rp.op_flags = Vulkan::RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT | Vulkan::RENDER_PASS_OP_STORE_DEPTH_STENCIL_BIT;
	memset(&rp.clear_color[0], 0, sizeof(rp.clear_color[0]));
	memset(&rp.clear_color[1], 0, sizeof(rp.clear_color[1]));
	rp.clear_depth_stencil.depth = 1.0f;
	rp.clear_depth_stencil.stencil = 0;
	cmd.begin_render_pass(rp);

	if (geometry.num_vertices)
	{
		void *vertices;
		float inv_resolution[2];

		vertices = cmd.allocate_vertex_data(0, geometry.num_vertices * sizeof(rdp_upscaler_vertex_t),
		                                    sizeof(rdp_upscaler_vertex_t));
		memcpy(vertices, geometry.vertices, geometry.num_vertices * sizeof(rdp_upscaler_vertex_t));
		cmd.set_opaque_state();
		cmd.set_cull_mode(VK_CULL_MODE_NONE);
		cmd.set_depth_compare(VK_COMPARE_OP_LESS_OR_EQUAL);
		cmd.set_program(upscaler_motion_program(device));
		cmd.set_vertex_attrib(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(rdp_upscaler_vertex_t, x));
		cmd.set_vertex_attrib(1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(rdp_upscaler_vertex_t, motion_x));
		inv_resolution[0] = 1.0f / float(width);
		inv_resolution[1] = 1.0f / float(height);
		cmd.push_constants(inv_resolution, 0, sizeof(inv_resolution));

		for (i = 0; i < geometry.num_draws; i++)
		{
			const rdp_upscaler_draw_t *draw = &geometry.draws[i];
			float translucent = (draw->flags & RDP_UPSCALER_TRANSLUCENT) ? 1.0f : 0.0f;
			cmd.set_depth_test((draw->flags & RDP_UPSCALER_DEPTH_TEST) != 0,
			                   (draw->flags & RDP_UPSCALER_DEPTH_WRITE) != 0);
			cmd.push_constants(&translucent, 8, sizeof(translucent));
			cmd.draw(draw->count, 1, draw->first, 0);
		}
	}

	cmd.end_render_pass();

	cmd.image_barrier(*frame.motion_vectors, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
	                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
	                  VK_ACCESS_SHADER_READ_BIT);
	cmd.image_barrier(*frame.translucent, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
	                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
	                  VK_ACCESS_SHADER_READ_BIT);
	cmd.image_barrier(*frame.depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
	                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	                  VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
	                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
	                  VK_ACCESS_SHADER_READ_BIT);

	frame.jitter_x = geometry.jitter_x;
	frame.jitter_y = geometry.jitter_y;
	frame.reset = geometry.reset;
}
}
