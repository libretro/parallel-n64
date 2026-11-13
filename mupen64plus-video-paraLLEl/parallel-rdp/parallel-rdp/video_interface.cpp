/* Copyright (c) 2020 Themaister
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

#include "video_interface.hpp"
#include "rdp_renderer.hpp"
#include "luts.hpp"
#include "bitops.hpp"
#include "upscaler_pass.hpp"
#include "upscaler_shaders.hpp"
#include <math.h>

#include "shaders/slangmosh.hpp"

namespace RDP
{
static void shift_rect(VkRect2D &rect, int x, int y)
{
	rect.offset.x += x;
	rect.offset.y += y;

	if (rect.offset.x < 0)
	{
		rect.extent.width += rect.offset.x;
		rect.offset.x = 0;
	}

	if (rect.offset.y < 0)
	{
		rect.extent.height += rect.offset.y;
		rect.offset.y = 0;
	}

	// Check for signed overflow without relying on -fwrapv.
	if (rect.extent.width & 0x80000000u)
		rect.extent.width = 0;
	if (rect.extent.height & 0x80000000u)
		rect.extent.height = 0;
}

void VideoInterface::set_extract_vram_pipeline(Vulkan::CommandBuffer &cmd, uint32_t status, unsigned scaling_factor) const
{
	cmd.set_program(shader_bank->extract_vram);
	cmd.set_specialization_constant_mask(7);
	cmd.set_specialization_constant(0, uint32_t(rdram_size));
	cmd.set_specialization_constant(1, status & (VI_CONTROL_TYPE_MASK | VI_CONTROL_META_AA_BIT));
	cmd.set_specialization_constant(2, trailing_zeroes(scaling_factor));
}

void VideoInterface::set_upscaler_color_pipeline(Vulkan::CommandBuffer &cmd, bool rgba32, unsigned scaling_factor, bool gamma) const
{
	cmd.set_program(static_cast<Vulkan::Program *>(rdp_upscaler_color_program(device)));
	cmd.set_specialization_constant_mask(0xf);
	cmd.set_specialization_constant(0, uint32_t(rdram_size));
	cmd.set_specialization_constant(1, uint32_t(rgba32));
	cmd.set_specialization_constant(2, trailing_zeroes(scaling_factor));
	cmd.set_specialization_constant(3, uint32_t(gamma));
}

void VideoInterface::set_fetch_pipeline(Vulkan::CommandBuffer &cmd, uint32_t status, bool fetch_bug) const
{
	cmd.set_opaque_state();
	cmd.set_program(device->request_program(shader_bank->fullscreen, shader_bank->vi_fetch[int(fetch_bug)]));
	cmd.set_specialization_constant_mask(3);
	cmd.set_specialization_constant(0, uint32_t(rdram_size));
	cmd.set_specialization_constant(1, status & (VI_CONTROL_META_AA_BIT | VI_CONTROL_DITHER_FILTER_ENABLE_BIT));
}

void VideoInterface::set_divot_pipeline(Vulkan::CommandBuffer &cmd, bool fetch_bug) const
{
	cmd.set_opaque_state();
	cmd.set_program(device->request_program(shader_bank->fullscreen, shader_bank->vi_divot[int(fetch_bug)]));
}

void VideoInterface::set_scale_pipeline(Vulkan::CommandBuffer &cmd, uint32_t status, bool fetch_bug, bool fold) const
{
	cmd.set_specialization_constant_mask((1 << 1) | (1 << 2));
	cmd.set_specialization_constant(1, status & (VI_CONTROL_GAMMA_ENABLE_BIT |
	                                             VI_CONTROL_GAMMA_DITHER_ENABLE_BIT |
	                                             VI_CONTROL_META_SCALE_BIT |
	                                             VI_CONTROL_META_AA_BIT));
	cmd.set_specialization_constant(2, uint32_t(fetch_bug));
	cmd.set_opaque_state();
	cmd.set_program(fold ? device->request_program(shader_bank->fullscreen, vi_scale_fold)
	                     : device->request_program(shader_bank->fullscreen, shader_bank->vi_scale));
}

void VideoInterface::set_blend_fields_pipeline(Vulkan::CommandBuffer &cmd) const
{
	cmd.set_blend_enable(true);
	cmd.set_blend_factors(VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA, VK_BLEND_FACTOR_DST_ALPHA);
	// Don't overwrite alpha, it's already zero.
	cmd.set_color_write_mask(0x7);
	cmd.set_specialization_constant_mask(0);
	cmd.set_program(device->request_program(shader_bank->fullscreen, shader_bank->vi_blend_fields));
}

void VideoInterface::set_deinterlace_pipeline(Vulkan::CommandBuffer &cmd) const
{
	cmd.set_opaque_state();
	cmd.set_program(device->request_program(shader_bank->vi_deinterlace_vert, shader_bank->vi_deinterlace_frag));
}

/* Every pipeline a scanout can ask for, on 1x1 targets of the formats scanout
 * uses: only their formats reach a pipeline. */
template <typename F>
static void each_status(uint32_t mask, const F &fn)
{
	uint32_t s = mask;
	for (;;)
	{
		fn(s);
		if (!s)
			break;
		s = (s - 1) & mask;
	}
}

void VideoInterface::queue_known_pipelines(unsigned upscaling, bool with_upscaler,
                                           rdp_known_pipeline_fn add, void *ctx)
{
	auto cmd_handle = device->request_command_buffer();
	auto &cmd = *cmd_handle;

	Vulkan::ImageCreateInfo uint_info = Vulkan::ImageCreateInfo::render_target(1, 1, VK_FORMAT_R8G8B8A8_UINT);
	uint_info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	Vulkan::ImageCreateInfo unorm_info = Vulkan::ImageCreateInfo::render_target(1, 1, VK_FORMAT_R8G8B8A8_UNORM);
	unorm_info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	Vulkan::ImageHandle uint0 = device->create_image(uint_info);
	Vulkan::ImageHandle uint1 = device->create_image(uint_info);
	Vulkan::ImageHandle unorm = device->create_image(unorm_info);
	const Vulkan::ImageHandle *const targets[] = { &uint0, &uint1, &unorm };

	for (auto *target : targets)
		cmd.image_barrier(**target, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
		                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

	/* the fetch bug only exists without upscaling */
	const unsigned bugs = upscaling == 1 ? 2 : 1;

	/* one attachment without the fetch bug, two with it */
	for (unsigned bug = 0; bug < bugs; bug++)
	{
		Vulkan::RenderPassInfo rp;
		rp.color_attachments[0] = &uint0->get_view();
		rp.color_attachments[1] = &uint1->get_view();
		rp.num_color_attachments = bug + 1;
		rp.store_attachments = (1u << rp.num_color_attachments) - 1;

		cmd.begin_render_pass(rp);
		each_status(VI_CONTROL_META_AA_BIT | VI_CONTROL_DITHER_FILTER_ENABLE_BIT, [&](uint32_t status) {
			set_fetch_pipeline(cmd, status, bug != 0);
			add(ctx, &cmd, PIPELINE_RECORD_GRAPHICS);
		});
		set_divot_pipeline(cmd, bug != 0);
		add(ctx, &cmd, PIPELINE_RECORD_GRAPHICS);
		cmd.end_render_pass();
	}

	{
		Vulkan::RenderPassInfo rp;
		rp.color_attachments[0] = &unorm->get_view();
		rp.num_color_attachments = 1;
		rp.store_attachments = 1;

		cmd.begin_render_pass(rp);
		for (unsigned bug = 0; bug < bugs; bug++)
			for (unsigned fold = 0; fold < (vi_scale_fold ? 2u : 1u); fold++)
				each_status(VI_CONTROL_GAMMA_ENABLE_BIT | VI_CONTROL_GAMMA_DITHER_ENABLE_BIT |
				            VI_CONTROL_META_SCALE_BIT | VI_CONTROL_META_AA_BIT, [&](uint32_t status) {
					set_scale_pipeline(cmd, status, bug != 0, fold != 0);
					add(ctx, &cmd, PIPELINE_RECORD_GRAPHICS);
				});

		cmd.set_opaque_state();
		set_blend_fields_pipeline(cmd);
		add(ctx, &cmd, PIPELINE_RECORD_GRAPHICS);

		set_deinterlace_pipeline(cmd);
		add(ctx, &cmd, PIPELINE_RECORD_GRAPHICS);
		cmd.end_render_pass();
	}

	for (unsigned scale = 1; scale <= upscaling; scale = (scale == upscaling) ? scale + 1 : upscaling)
		each_status(VI_CONTROL_TYPE_MASK | VI_CONTROL_META_AA_BIT, [&](uint32_t status) {
			/* a blank or reserved type scans out as RGBA5551 */
			if ((status & VI_CONTROL_TYPE_RGBA5551_BIT) == 0)
				return;
			set_extract_vram_pipeline(cmd, status, scale);
			add(ctx, &cmd, PIPELINE_RECORD_COMPUTE);
		});

	if (with_upscaler)
	{
		for (unsigned scale = 1; scale <= upscaling; scale = (scale == upscaling) ? scale + 1 : upscaling)
			for (unsigned rgba32 = 0; rgba32 < 2; rgba32++)
				for (unsigned gamma = 0; gamma < 2; gamma++)
				{
					set_upscaler_color_pipeline(cmd, rgba32 != 0, scale, gamma != 0);
					add(ctx, &cmd, PIPELINE_RECORD_COMPUTE);
				}
		rdp_upscaler_queue_known_pipelines(device, &cmd, add, ctx);
	}

	device->submit(cmd_handle);
}

void VideoInterface::set_device(Vulkan::Device *device_)
{
	device = device_;
	init_gamma_table();
}

void VideoInterface::set_renderer(Renderer *renderer_)
{
	renderer = renderer_;
}

int VideoInterface::resolve_shader_define(const char *name, const char *define) const
{
	return 0;
}

void VideoInterface::init_gamma_table()
{
	Vulkan::BufferCreateInfo info = {};
	info.domain = Vulkan::BufferDomain::Device;
	info.size = sizeof(gamma_table);
	info.usage = VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;

	gamma_lut = device->create_buffer(info, gamma_table);

	Vulkan::BufferViewCreateInfo view = {};
	view.buffer = gamma_lut.get();
	view.range = sizeof(gamma_table);
	view.format = VK_FORMAT_R8_UINT;
	gamma_lut_view = device->create_buffer_view(view);
}

void VideoInterface::set_vi_register(VIRegister reg, uint32_t value)
{
	vi_registers[unsigned(reg)] = value;
}

void VideoInterface::set_rdram(const Vulkan::Buffer *rdram_, size_t offset, size_t size)
{
	rdram = rdram_;
	rdram_offset = offset;
	rdram_size = size;
}

void VideoInterface::set_hidden_rdram(const Vulkan::Buffer *hidden_rdram_)
{
	hidden_rdram = hidden_rdram_;
}

void VideoInterface::set_shader_bank(const ShaderBank *bank)
{
	shader_bank = bank;
	vi_scale_fold = nullptr;
	if (!bank || !device)
		return;

	// vi_scale's own layout, with the fold's registers after its own.
	Vulkan::ResourceLayout layout;
	layout.sets[0].separate_image_mask = (1u << 0) | (1u << 2);
	layout.sets[0].sampled_texel_buffer_mask = 1u << 1;
	layout.sets[0].fp_mask = 1u << 2;
	layout.sets[0].array_size[0] = 1;
	layout.sets[0].array_size[1] = 1;
	layout.sets[0].array_size[2] = 1;
	layout.sets[1].sampled_texel_buffer_mask = 1u << 0;
	layout.sets[1].array_size[0] = 1;
	layout.output_mask = 1;
	layout.push_constant_size = 120;
	layout.spec_constant_mask = (1u << 1) | (1u << 2);
	vi_scale_fold = device->request_shader(UpscalerShaders::vi_scale_fold_frag,
	                                       sizeof(UpscalerShaders::vi_scale_fold_frag), &layout);

	static const uint32_t black = 0;
	Vulkan::ImageInitialData initial = { &black, 1 };
	no_previous_frame = device->create_image(
			Vulkan::ImageCreateInfo::immutable_2d_image(1, 1, VK_FORMAT_R8G8B8A8_UNORM), &initial);

	// PARALLEL_RDP_VI_NO_FOLD=1 runs the downscale and deinterlace as
	// passes of their own, as they were, to compare against.
	if (const char *env = getenv("PARALLEL_RDP_VI_NO_FOLD"))
		fold_disabled = strtol(env, nullptr, 0) != 0;
}

static VkPipelineStageFlagBits layout_to_stage(VkImageLayout layout)
{
	switch (layout)
	{
	case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
		return VK_PIPELINE_STAGE_TRANSFER_BIT;

	case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
		return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
		return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

	default:
		return VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	}
}

static VkAccessFlags layout_to_access(VkImageLayout layout)
{
	switch (layout)
	{
	case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
		return VK_ACCESS_TRANSFER_READ_BIT;

	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
		return VK_ACCESS_TRANSFER_WRITE_BIT;

	case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
		return VK_ACCESS_SHADER_READ_BIT;

	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
		return VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	default:
		return 0;
	}
}

void VideoInterface::scanout_memory_range(unsigned &offset, unsigned &length) const
{
	rdp_vi_scanout_memory_range(vi_registers, &offset, &length);
}

VideoInterface::~VideoInterface()
{
	rdp_vi_image_pool_free(&image_pool);
}

void VideoInterface::begin_frame_context()
{
	frame_serial++;
	if (device)
		rdp_vi_image_pool_trim(&image_pool, frame_serial, device->get_num_frame_contexts());
}

// A handle of the caller's own to an entry's image (empty if it has none).
static Vulkan::ImageHandle held_image(const rdp_vi_image_entry_t &e)
{
	if (e.image)
		granite_image_ref(e.image);
	return Vulkan::ImageHandle(static_cast<Vulkan::Image *>(e.image));
}

const rdp_vi_image_entry_t &VideoInterface::acquire_image(const Vulkan::ImageCreateInfo &info, bool layer_views) const
{
	rdp_vi_image_entry_t *e = rdp_vi_image_pool_reuse(&image_pool, &info, layer_views, frame_serial,
	                                                  device->get_num_frame_contexts());
	if (e)
		return *e;
	return create_pooled_image(info, layer_views);
}

// Only when the size or format changes, or while the pool fills.
UTIL_NOINLINE_COLD const rdp_vi_image_entry_t &VideoInterface::create_pooled_image(
		const Vulkan::ImageCreateInfo &info, bool layer_views) const
{
	rdp_vi_image_entry_t *e = rdp_vi_image_pool_add(&image_pool, &info, frame_serial,
	                                                device->get_num_frame_contexts());
	// (the entry takes the references)
	e->image = granite_device_create_image(device, &info, nullptr);
	if (layer_views && e->image)
	{
		granite_image_view_info_t view_info = {};
		view_info.image = e->image;
		view_info.view_type = VK_IMAGE_VIEW_TYPE_2D;
		view_info.levels = VK_REMAINING_MIP_LEVELS;
		view_info.layers = 1;
		for (unsigned layer = 0; layer < info.layers && layer < 2; layer++)
		{
			view_info.base_layer = layer;
			e->layer_views[layer] = granite_device_create_image_view(device, &view_info);
		}
	}
	return *e;
}

Vulkan::ImageHandle VideoInterface::vram_fetch_stage(const Registers &regs, unsigned scaling_factor) const
{
	auto async_cmd = device->request_command_buffer(Vulkan::CommandBuffer::Type::AsyncCompute);
	Vulkan::ImageHandle vram_image;
	bool divot = (regs.status & VI_CONTROL_DIVOT_ENABLE_BIT) != 0;

	if (scaling_factor > 1)
	{
		unsigned pixel_size_log2 = ((regs.status & VI_CONTROL_TYPE_MASK) == VI_CONTROL_TYPE_RGBA8888_BIT) ? 2 : 1;
		unsigned offset, length;
		scanout_memory_range(offset, length);
		renderer->submit_update_upscaled_domain_external(*async_cmd, offset, length, pixel_size_log2);
		async_cmd->barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
		                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
	}

	// Need to sample a 2-pixel border to have room for AA filter and divot.
	int extract_width = regs.max_x + 2 + 4 + int(divot) * 2;
	// 1 pixel border on top and bottom.
	int extract_height = regs.max_y + 1 + 4;

	Vulkan::ImageCreateInfo rt_info = Vulkan::ImageCreateInfo::render_target(
			extract_width,
			extract_height,
			VK_FORMAT_R8G8B8A8_UINT);

	rt_info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	rt_info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	rt_info.misc = Vulkan::IMAGE_MISC_CONCURRENT_QUEUE_GRAPHICS_BIT |
	               Vulkan::IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_COMPUTE_BIT;
	vram_image = held_image(acquire_image(rt_info, false));
	vram_image->set_layout(Vulkan::Layout::General);

	async_cmd->image_barrier(*vram_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
	                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
	                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT);

	async_cmd->set_storage_texture(0, 0, vram_image->get_view());

	if (scaling_factor > 1)
	{
		async_cmd->set_storage_buffer(0, 1, *renderer->get_upscaled_rdram_buffer());
		async_cmd->set_storage_buffer(0, 2, *renderer->get_upscaled_hidden_rdram_buffer());
	}
	else
	{
		async_cmd->set_storage_buffer(0, 1, *rdram, rdram_offset, rdram_size);
		async_cmd->set_storage_buffer(0, 2, *hidden_rdram);
	}

	struct Push
	{
		uint32_t fb_offset;
		uint32_t fb_width;
		int32_t x_offset;
		int32_t y_offset;
		int32_t x_res;
		int32_t y_res;
	} push = {};

	if ((regs.status & VI_CONTROL_TYPE_MASK) == VI_CONTROL_TYPE_RGBA8888_BIT)
		push.fb_offset = regs.vi_offset >> 2;
	else
		push.fb_offset = regs.vi_offset >> 1;

	push.fb_width = regs.vi_width;
	push.x_offset = divot ? -3 : -2;
	push.y_offset = -2;
	push.x_res = extract_width;
	push.y_res = extract_height;

	set_extract_vram_pipeline(*async_cmd, regs.status, scaling_factor);

	async_cmd->push_constants(&push, 0, sizeof(push));
	async_cmd->dispatch((extract_width + 15) / 16,
	                    (extract_height + 7) / 8,
	                    1);

	// Just enforce an execution barrier here for rendering work in next frame.
	async_cmd->barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
	                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0);

	Vulkan::Semaphore sem;
	device->submit(async_cmd, nullptr, 1, &sem);
	device->add_wait_semaphore(Vulkan::CommandBuffer::Type::Generic, rdp_move(sem),
	                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, true);

	return vram_image;
}

Vulkan::ImageHandle VideoInterface::aa_fetch_stage(Vulkan::CommandBuffer &cmd, Vulkan::Image &vram_image,
                                                   const Registers &regs, unsigned scaling_factor) const
{
	Vulkan::ImageHandle aa_image;
	bool fetch_bug = rdp_vi_need_fetch_bug_emulation(&regs, scaling_factor);
	bool divot = (regs.status & VI_CONTROL_DIVOT_ENABLE_BIT) != 0;

	// For the AA pass, we need to figure out how many pixels we might need to read.
	int aa_width = regs.max_x + 3 + int(divot) * 2;
	int aa_height = regs.max_y + 2;

	Vulkan::ImageCreateInfo rt_info = Vulkan::ImageCreateInfo::render_target(aa_width, aa_height,
	                                                                         VK_FORMAT_R8G8B8A8_UINT);
	rt_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	rt_info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	rt_info.layers = fetch_bug ? 2 : 1;
	rt_info.misc = Vulkan::IMAGE_MISC_FORCE_ARRAY_BIT;
	const rdp_vi_image_entry_t &aa = acquire_image(rt_info, true);
	aa_image = held_image(aa);
	const Vulkan::ImageView *aa_primary = static_cast<const Vulkan::ImageView *>(aa.layer_views[0]);
	const Vulkan::ImageView *aa_secondary = static_cast<const Vulkan::ImageView *>(aa.layer_views[1]);

	Vulkan::RenderPassInfo rp;
	rp.color_attachments[0] = aa_primary;
	rp.clear_attachments = 0;

	if (fetch_bug)
	{
		rp.color_attachments[1] = aa_secondary;
		rp.num_color_attachments = 2;
		rp.store_attachments = 3;
	}
	else
	{
		rp.num_color_attachments = 1;
		rp.store_attachments = 1;
	}

	cmd.image_barrier(*aa_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
	                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

	cmd.begin_render_pass(rp);
	set_fetch_pipeline(cmd, regs.status, fetch_bug);

	struct Push
	{
		int32_t x_offset;
		int32_t y_offset;
	} push = {};

	push.x_offset = 2;
	push.y_offset = 2;

	cmd.push_constants(&push, 0, sizeof(push));

	cmd.set_texture(0, 0, vram_image.get_view());
	cmd.draw(3);
	cmd.end_render_pass();

	cmd.image_barrier(*aa_image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
	                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);

	return aa_image;
}

Vulkan::ImageHandle VideoInterface::divot_stage(Vulkan::CommandBuffer &cmd, Vulkan::Image &aa_image,
                                                const Registers &regs, unsigned scaling_factor) const
{
	Vulkan::ImageHandle divot_image;
	bool fetch_bug = rdp_vi_need_fetch_bug_emulation(&regs, scaling_factor);

	// For the divot pass, we need to figure out how many pixels we might need to read.
	int divot_width = regs.max_x + 2;
	int divot_height = regs.max_y + 2;

	Vulkan::ImageCreateInfo rt_info = Vulkan::ImageCreateInfo::render_target(divot_width, divot_height,
	                                                                         VK_FORMAT_R8G8B8A8_UINT);
	rt_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	rt_info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	rt_info.layers = fetch_bug ? 2 : 1;
	rt_info.misc = Vulkan::IMAGE_MISC_FORCE_ARRAY_BIT;
	const rdp_vi_image_entry_t &divot = acquire_image(rt_info, true);
	divot_image = held_image(divot);
	const Vulkan::ImageView *divot_primary = static_cast<const Vulkan::ImageView *>(divot.layer_views[0]);
	const Vulkan::ImageView *divot_secondary = static_cast<const Vulkan::ImageView *>(divot.layer_views[1]);

	Vulkan::RenderPassInfo rp;
	rp.color_attachments[0] = divot_primary;
	rp.clear_attachments = 0;

	if (fetch_bug)
	{
		rp.color_attachments[1] = divot_secondary;
		rp.num_color_attachments = 2;
		rp.store_attachments = 3;
	}
	else
	{
		rp.num_color_attachments = 1;
		rp.store_attachments = 1;
	}

	cmd.image_barrier(*divot_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
	                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

	cmd.begin_render_pass(rp);
	set_divot_pipeline(cmd, fetch_bug);

	cmd.set_texture(0, 0, aa_image.get_view());
	cmd.draw(3);
	cmd.end_render_pass();

	cmd.image_barrier(*divot_image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
	                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);

	return divot_image;
}

VideoInterface::HorizontalBuffer *VideoInterface::acquire_horizontal_buffer()
{
	uint64_t contexts = device->get_num_frame_contexts();
	HorizontalBuffer *slot = nullptr;
	unsigned i;

	for (i = 0; i < MAX_HORIZONTAL_BUFFERS; i++)
	{
		HorizontalBuffer &b = horizontal_buffers[i];
		if (b.buffer && b.free_serial <= frame_serial)
		{
			slot = &b;
			break;
		}
		if (!b.buffer && !slot)
			slot = &b;
	}
	if (!slot)
		return nullptr;

	if (!slot->buffer)
	{
		Vulkan::BufferCreateInfo info = {};
		info.size = VI_MAX_OUTPUT_SCANLINES * sizeof(rdp_vi_horizontal_info_t);
		info.usage = VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
		// Read back by the CPU while scaling, so cached.
		info.domain = Vulkan::BufferDomain::CachedCoherentHostPreferCached;
		slot->buffer = device->create_buffer(info);
		if (!slot->buffer)
			return nullptr;

		Vulkan::BufferViewCreateInfo view_info = {};
		view_info.format = VK_FORMAT_R32G32B32A32_SINT;
		view_info.buffer = slot->buffer.get();
		view_info.range = VI_MAX_OUTPUT_SCANLINES * sizeof(rdp_vi_horizontal_info_t);
		slot->view = device->create_buffer_view(view_info);
	}

	slot->free_serial = frame_serial + contexts;
	return slot;
}

void VideoInterface::bind_horizontal_info_view(Vulkan::CommandBuffer &cmd, const HorizontalBuffer &buffer)
{
	// Written in place: flushed if the memory is not coherent.
	cmd.get_device().unmap_host_buffer(*buffer.buffer, Vulkan::MEMORY_ACCESS_WRITE_BIT);
	cmd.set_buffer_view(0, 1, *buffer.view);
}

Vulkan::ImageHandle VideoInterface::scale_stage(Vulkan::CommandBuffer &cmd, const Vulkan::Image *divot_image,
                                                Registers regs, const HorizontalBuffer &lines,
                                                unsigned scaling_factor, bool degenerate,
                                                const ScanoutOptions &options, bool final_pass,
                                                const ScaleFold *fold) const
{
	Vulkan::ImageHandle scale_image;
	bool fetch_bug = rdp_vi_need_fetch_bug_emulation(&regs, scaling_factor);
	bool serrate = (regs.status & VI_CONTROL_SERRATE_BIT) != 0 && !options.upscale_deinterlacing;

	Vulkan::ImageCreateInfo rt_info = Vulkan::ImageCreateInfo::render_target(
			VI_SCANOUT_WIDTH * scaling_factor,
			((regs.is_pal ? VI_V_RES_PAL: VI_V_RES_NTSC) >> int(!serrate)) * scaling_factor,
			VK_FORMAT_R8G8B8A8_UNORM);

	unsigned crop_left = 0;
	unsigned crop_right = 0;
	unsigned crop_top = 0;
	unsigned crop_bottom = 0;

	if (options.crop_rect.enable)
	{
		crop_left = options.crop_rect.left;
		crop_right = options.crop_rect.right;
		crop_top = options.crop_rect.top;
		crop_bottom = options.crop_rect.bottom;

		if (serrate)
		{
			crop_top *= 2;
			crop_bottom *= 2;
		}
	}
	else
	{
		// Rescale crop pixels to preserve aspect ratio.
		auto crop_pixels_y = options.crop_overscan_pixels * (serrate ? 2 : 1);
		auto crop_pixels_x = unsigned(floor(double(float(crop_pixels_y) * (float(rt_info.width) / float(rt_info.height))) + 0.5));

		crop_left = crop_right = crop_pixels_x;
		crop_top = crop_bottom = crop_pixels_y;
	}

	crop_left *= scaling_factor;
	crop_right *= scaling_factor;
	crop_top *= scaling_factor;
	crop_bottom *= scaling_factor;

	if (crop_left + crop_right < rt_info.width && crop_top + crop_bottom < rt_info.height)
	{
		rt_info.width -= crop_left + crop_right;
		rt_info.height -= crop_top + crop_bottom;
	}
	else
	{
		LOGE("Too large crop of %u x %u for RT %u x %u.\n",
			 crop_left + crop_right, crop_top + crop_bottom, rt_info.width, rt_info.height);
	}

	// What the folded passes need: the downscaled image's height, and how
	// the deinterlace reads it, in quarter rows (see vi_scale_fold.frag).
	int deinterlace_mul = 0, deinterlace_add = 0, deinterlace_rows = 0;
	const unsigned scale_height = rt_info.height;
	if (fold)
	{
		rt_info.width >>= fold->downscale_steps;
		rt_info.height >>= fold->downscale_steps;
		if (fold->deinterlace)
		{
			// As upscale_deinterlace samples it: a quarter field line up or
			// down, at twice the height at 1x.
			int field_scale = int(rdp_max(1u, scaling_factor >> fold->downscale_steps));
			deinterlace_rows = int(rt_info.height);
			if (field_scale == 1)
			{
				rt_info.height *= 2;
				deinterlace_mul = 2;
				deinterlace_add = fold->field_select ? -2 : 0;
			}
			else
			{
				deinterlace_mul = 4;
				deinterlace_add = fold->field_select ? -field_scale : field_scale;
			}
		}
	}

	rt_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	rt_info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	rt_info.misc = Vulkan::IMAGE_MISC_MUTABLE_SRGB_BIT;

	scale_image = held_image(acquire_image(rt_info, false));

	if (!scale_image)
	{
		LOGE("Failed to allocate scale image.\n");
		return {};
	}

	Vulkan::RenderPassInfo rp;
	rp.color_attachments[0] = &scale_image->get_view();
	memset(&rp.clear_color[0], 0, sizeof(rp.clear_color[0]));
	rp.num_color_attachments = 1;
	rp.clear_attachments = 1;
	rp.store_attachments = 1;

	cmd.image_barrier(*scale_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
	                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

	if (prev_scanout_image)
	{
		if (prev_image_layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		{
			VK_ASSERT(prev_image_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
			cmd.image_barrier(*prev_scanout_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			                  VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
			                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
		}
	}

	cmd.begin_render_pass(rp);

	struct Push
	{
		int32_t h_offset, v_offset;
		int32_t v_start;
		uint32_t y_add;
		uint32_t frame_count;

		uint32_t serrate_shift;
		uint32_t serrate_mask;
		uint32_t serrate_select;

		uint32_t info_y_shift;

		// vi_scale_fold only
		int32_t pad;
		int32_t rect_lo[2], rect_hi[2];
		int32_t downscale_steps;
		int32_t deinterlace_mul, deinterlace_add, deinterlace_rows;
		int32_t blend_lo[3][2], blend_hi[3][2];
	} push = {};
	static_assert(sizeof(push) == 120, "vi_scale_fold's registers are 120 bytes");
	// vi_scale's registers end where the fold's begin
	const size_t push_size = fold ? sizeof(push) : offsetof(Push, pad);

	push.info_y_shift = Util::floor_log2(scaling_factor);

	if (serrate)
	{
		regs.v_start *= 2;
		regs.v_res *= 2;
		push.serrate_shift = 1;
		push.serrate_mask = 1;
		bool field_state = regs.v_current_line == 0;
		push.serrate_select = int(field_state);
		push.info_y_shift++;
	}

	push.h_offset = int(crop_left);
	push.v_offset = int(crop_top);
	push.v_start = regs.v_start;
	push.y_add = regs.init_y_add;
	push.frame_count = frame_count;

	set_scale_pipeline(cmd, regs.status, fetch_bug, fold != nullptr);
	cmd.set_buffer_view(1, 0, *gamma_lut_view);
	bind_horizontal_info_view(cmd, lines);

	if (!degenerate && divot_image && regs.h_res > 0 && regs.v_res > 0)
	{
		VkRect2D rect = {{ regs.h_start, regs.v_start }, { uint32_t(regs.h_res), uint32_t(regs.v_res) }};
		shift_rect(rect, -int(crop_left), -int(crop_top));

		if (rect.extent.width > 0 && rect.extent.height > 0)
		{
			cmd.set_texture(0, 0, divot_image->get_view());
			if (fold)
			{
				// Every output pixel is drawn: the shader leaves the scale
				// pixels outside the scissor at the clear colour itself.
				push.rect_lo[0] = rect.offset.x;
				push.rect_lo[1] = rect.offset.y;
				push.rect_hi[0] = rect.offset.x + int(rect.extent.width);
				push.rect_hi[1] = rect.offset.y + int(rect.extent.height);
				push.downscale_steps = int(fold->downscale_steps);
				push.deinterlace_mul = deinterlace_mul;
				push.deinterlace_add = deinterlace_add;
				push.deinterlace_rows = deinterlace_rows;

				// The blend pass's rects, below, in the scale pass's
				// coordinates; the previous frame is read where the scale
				// pixel ends up, so it has to be this frame's size.
				const Vulkan::Image *prev = no_previous_frame.get();
				if (prev_scanout_image && options.blend_previous_frame &&
				    prev_scanout_image->get_width() == rt_info.width &&
				    prev_scanout_image->get_height() == rt_info.height)
				{
					const int bottom = int(scale_height + crop_top);
					VkRect2D blend[3] = {
						{{ regs.h_start, 0 }, { uint32_t(regs.h_res), uint32_t(regs.v_start) }},
						{{ regs.h_start_clamp, regs.v_start }, { uint32_t(regs.h_res_clamp), uint32_t(regs.v_res) }},
						{{ regs.h_start, regs.v_start + regs.v_res },
						 { uint32_t(regs.h_res), uint32_t(rdp_max(0, bottom - (regs.v_start + regs.v_res))) }},
					};
					bool use[3] = {
						regs.h_res > 0 && regs.v_start > 0,
						regs.h_res_clamp > 0 && regs.v_res > 0,
						regs.h_res > 0 && bottom > regs.v_start + regs.v_res,
					};
					for (unsigned i = 0; i < 3; i++)
					{
						if (!use[i])
							continue;
						shift_rect(blend[i], -int(crop_left), -int(crop_top));
						push.blend_lo[i][0] = blend[i].offset.x;
						push.blend_lo[i][1] = blend[i].offset.y;
						push.blend_hi[i][0] = blend[i].offset.x + int(blend[i].extent.width);
						push.blend_hi[i][1] = blend[i].offset.y + int(blend[i].extent.height);
					}
					prev = prev_scanout_image.get();
				}
				cmd.set_texture(0, 2, prev->get_view());
			}
			else
				cmd.set_scissor(rect);
			cmd.push_constants(&push, 0, push_size);
			cmd.draw(3);
		}
	}
	else
		cmd.push_constants(&push, 0, push_size);

	// To deal with weave interlacing and other "persistence effects", we blend in previous frame's result.
	// This is somewhat arbitrary, but seems to work well enough in practice.

	if (prev_scanout_image && options.blend_previous_frame && !fold)
	{
		set_blend_fields_pipeline(cmd);
		cmd.set_texture(0, 0, prev_scanout_image->get_view());

		if (degenerate)
		{
			if (regs.h_res > 0)
			{
				VkRect2D rect = {{ regs.h_start, 0 }, { uint32_t(regs.h_res), prev_scanout_image->get_height() }};
				shift_rect(rect, -int(crop_left), -int(crop_top));
				if (rect.extent.width > 0 && rect.extent.height > 0)
				{
					cmd.set_scissor(rect);
					cmd.draw(3);
				}
			}
		}
		else
		{
			// Top part.
			if (regs.h_res > 0 && regs.v_start > 0)
			{
				VkRect2D rect = {{ regs.h_start, 0 }, { uint32_t(regs.h_res), uint32_t(regs.v_start) }};
				shift_rect(rect, -int(crop_left), -int(crop_top));
				if (rect.extent.width > 0 && rect.extent.height > 0)
				{
					cmd.set_scissor(rect);
					cmd.draw(3);
				}
			}

			// Middle part, don't overwrite the 8 pixel guard band.
			if (regs.h_res_clamp > 0 && regs.v_res > 0)
			{
				VkRect2D rect = {{ regs.h_start_clamp, regs.v_start }, { uint32_t(regs.h_res_clamp), uint32_t(regs.v_res) }};
				shift_rect(rect, -int(crop_left), -int(crop_top));
				if (rect.extent.width > 0 && rect.extent.height > 0)
				{
					cmd.set_scissor(rect);
					cmd.draw(3);
				}
			}

			// Bottom part.
			if (regs.h_res > 0 && prev_scanout_image->get_height() > uint32_t(regs.v_start + regs.v_res))
			{
				VkRect2D rect = {{ regs.h_start, regs.v_start + regs.v_res },
				                 { uint32_t(regs.h_res), prev_scanout_image->get_height() - uint32_t(regs.v_start + regs.v_res) }};
				shift_rect(rect, -int(crop_left), -int(crop_top));
				if (rect.extent.width > 0 && rect.extent.height > 0)
				{
					cmd.set_scissor(rect);
					cmd.draw(3);
				}
			}
		}
	}

	cmd.end_render_pass();

	return scale_image;
}

Vulkan::ImageHandle VideoInterface::downscale_stage(Vulkan::CommandBuffer &cmd, Vulkan::Image &scale_image,
                                                    unsigned scaling_factor, unsigned downscale_steps,
                                                    const ScanoutOptions &options, bool final_pass) const
{
	Vulkan::ImageHandle downscale_image;
	const Vulkan::Image *input = &scale_image;
	Vulkan::ImageHandle holder;

	bool need_pass = scaling_factor > 1 && downscale_steps;

	// TODO: Could optimize this to happen in one pass, but ... eh.
	while (need_pass)
	{
		if (input != &scale_image)
		{
			cmd.image_barrier(*input, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			                  VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
			                  VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
		}

		unsigned width = input->get_width();
		unsigned height = input->get_height();

		Vulkan::ImageCreateInfo rt_info = Vulkan::ImageCreateInfo::render_target(
				width / 2, height / 2,
				VK_FORMAT_R8G8B8A8_UNORM);

		rt_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		rt_info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		rt_info.misc = Vulkan::IMAGE_MISC_MUTABLE_SRGB_BIT;

		scaling_factor /= 2;
		downscale_steps--;
		need_pass = scaling_factor > 1 && downscale_steps;

		downscale_image = held_image(acquire_image(rt_info, false));

		if (!downscale_image)
		{
			LOGE("Failed to allocate downscale image.\n");
			return {};
		}

		cmd.image_barrier(*downscale_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
		                  VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);

		cmd.blit_image(*downscale_image, *input,
		               {}, {int(rt_info.width), int(rt_info.height), 1},
		               {}, {int(width), int(height), 1},
		               0, 0);

		input = downscale_image.get();
		holder = downscale_image;
	}

	return downscale_image;
}

Vulkan::ImageHandle VideoInterface::upscale_deinterlace(Vulkan::CommandBuffer &cmd, Vulkan::Image &scale_image,
                                                        unsigned scaling_factor, bool field_select,
                                                        const ScanoutOptions &options) const
{
	Vulkan::ImageHandle deinterlaced_image;

	// If we're running upscaled, upscaling Y further is somewhat meaningless and bandwidth intensive.
	Vulkan::ImageCreateInfo rt_info = Vulkan::ImageCreateInfo::render_target(
			scale_image.get_width(), scale_image.get_height() * (scaling_factor == 1 ? 2 : 1),
			VK_FORMAT_R8G8B8A8_UNORM);

	rt_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	rt_info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	rt_info.misc = Vulkan::IMAGE_MISC_MUTABLE_SRGB_BIT;

	deinterlaced_image = held_image(acquire_image(rt_info, false));

	if (!deinterlaced_image)
	{
		LOGE("Failed to allocate deinterlace image.\n");
		return {};
	}

	Vulkan::RenderPassInfo rp;
	rp.color_attachments[0] = &deinterlaced_image->get_view();
	rp.num_color_attachments = 1;
	rp.store_attachments = 1;

	cmd.image_barrier(*deinterlaced_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
	                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

	cmd.begin_render_pass(rp);
	set_deinterlace_pipeline(cmd);

	struct Push
	{
		float y_offset;
	} push = {};
	push.y_offset = (float(scaling_factor) * (field_select ? -0.25f : +0.25f)) / float(scale_image.get_height());
	cmd.push_constants(&push, 0, sizeof(push));

	cmd.set_texture(0, 0, scale_image.get_view(), Vulkan::StockSampler::LinearClamp);
	cmd.draw(3);
	cmd.end_render_pass();
	return deinterlaced_image;
}

Vulkan::ImageHandle VideoInterface::upscaler_scanout(unsigned scaling_factor,
                                                     rdp_upscaler_framebuffer_t &fb, bool &repeat)
{
	Registers regs;
	Vulkan::ImageCreateInfo info;
	Vulkan::ImageHandle image;
	unsigned size_log2;
	int width, height;
	struct Push
	{
		int32_t fb_offset;
		int32_t fb_width;
		int32_t res_x, res_y;
		int32_t shown_x0, shown_x1;
	} push;

	rdp_vi_decode_registers(vi_registers, &regs, nullptr);
	memset(&fb, 0, sizeof(fb));
	repeat = false;
	frame_count++;

	if (regs.vi_offset == 0 || (regs.status & VI_CONTROL_TYPE_RGBA5551_BIT) == 0)
	{
		prev_upscaler_image.reset();
		return {};
	}

	size_log2 = (regs.status & VI_CONTROL_TYPE_MASK) == VI_CONTROL_TYPE_RGBA8888_BIT ? 2 : 1;
	width = rdp_min(regs.max_x, regs.vi_width);
	height = regs.max_y;

	if (regs.h_res <= 0 || regs.h_start >= VI_SCANOUT_WIDTH || regs.v_res <= 0 || width <= 0 || height <= 0)
	{
		/* As persist_frame_on_invalid_input: games strobe the invalid
		 * state between valid frames. */
		if (prev_upscaler_image && frame_count - last_valid_frame_count < 4)
		{
			fb = prev_upscaler_fb;
			repeat = true;
			return prev_upscaler_image;
		}
		prev_upscaler_image.reset();
		return {};
	}
	last_valid_frame_count = frame_count;

	/* The frame is read where the RDP draws it, on the async compute
	 * queue behind the drawing, and handed to the graphics queue by a
	 * semaphore, as vram_fetch_stage: a graphics command buffer does
	 * not see the RDP's queue */
	auto async_cmd = device->request_command_buffer(Vulkan::CommandBuffer::Type::AsyncCompute);

	if (scaling_factor > 1)
	{
		unsigned offset, length;
		scanout_memory_range(offset, length);
		renderer->submit_update_upscaled_domain_external(*async_cmd, offset, length, size_log2);
		async_cmd->barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
		                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
	}

	info = Vulkan::ImageCreateInfo::render_target(width * scaling_factor, height * scaling_factor,
	                                              VK_FORMAT_R8G8B8A8_UNORM);
	info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	info.misc = Vulkan::IMAGE_MISC_CONCURRENT_QUEUE_GRAPHICS_BIT |
	            Vulkan::IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_COMPUTE_BIT;
	image = held_image(acquire_image(info, false));
	if (!image)
	{
		device->submit(async_cmd);
		return {};
	}
	image->set_layout(Vulkan::Layout::General);

	async_cmd->image_barrier(*image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
	                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
	                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT);

	async_cmd->set_storage_texture(0, 0, image->get_view());
	if (scaling_factor > 1)
		async_cmd->set_storage_buffer(0, 1, *renderer->get_upscaled_rdram_buffer());
	else
		async_cmd->set_storage_buffer(0, 1, *rdram, rdram_offset, rdram_size);
	async_cmd->set_buffer_view(0, 2, *gamma_lut_view);

	push.fb_offset = regs.vi_offset >> size_log2;
	push.fb_width = regs.vi_width;
	push.res_x = int32_t(info.width);
	push.res_y = int32_t(info.height);
	/* black beside what the scanout shows (the guard band it blanks at
	 * the sides), as the screen has it */
	push.shown_x0 = rdp_max(regs.min_x, 0) * int32_t(scaling_factor);
	push.shown_x1 = rdp_min(regs.max_x_clamp, width) * int32_t(scaling_factor);

	set_upscaler_color_pipeline(*async_cmd, size_log2 == 2, scaling_factor, (regs.status & VI_CONTROL_GAMMA_ENABLE_BIT) != 0);
	async_cmd->push_constants(&push, 0, sizeof(push));
	async_cmd->dispatch((info.width + 7) / 8, (info.height + 7) / 8, 1);
	async_cmd->set_specialization_constant_mask(0);

	async_cmd->image_barrier(*image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
	                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0);
	image->set_layout(Vulkan::Layout::Optimal);

	{
		Vulkan::Semaphore sem;
		device->submit(async_cmd, nullptr, 1, &sem);
		device->add_wait_semaphore(Vulkan::CommandBuffer::Type::Generic, rdp_move(sem),
		                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		                           true);
	}

	fb.origin = uint32_t(regs.vi_offset) & ~((1u << size_log2) - 1u);
	fb.stride = uint32_t(regs.vi_width);
	fb.size_log2 = size_log2;
	fb.width = unsigned(width);
	fb.height = unsigned(height);
	prev_upscaler_image = image;
	prev_upscaler_fb = fb;
	return image;
}

Vulkan::ImageHandle VideoInterface::scanout(VkImageLayout target_layout, const ScanoutOptions &options, unsigned scaling_factor_)
{
	prev_upscaler_image.reset();

	unsigned downscale_steps = rdp_min(8u, options.downscale_steps);
	int scaling_factor = int(scaling_factor_);
	Vulkan::ImageHandle scanout;
	HorizontalBuffer *horizontal = acquire_horizontal_buffer();
	if (!horizontal)
		return scanout;
	auto *lines = static_cast<rdp_vi_horizontal_info_t *>(
			device->map_host_buffer(*horizontal->buffer, Vulkan::MEMORY_ACCESS_WRITE_BIT));

	Registers regs;
	rdp_vi_decode_registers(vi_registers, &regs, lines);

	if (regs.vi_offset == 0)
	{
		prev_scanout_image.reset();
		return scanout;
	}

	if (!options.vi.serrate)
		regs.status &= ~VI_CONTROL_SERRATE_BIT;

	bool status_is_aa = (regs.status & VI_CONTROL_AA_MODE_MASK) < VI_CONTROL_AA_MODE_RESAMP_ONLY_BIT;
	bool status_is_bilinear = (regs.status & VI_CONTROL_AA_MODE_MASK) < VI_CONTROL_AA_MODE_RESAMP_REPLICATE_BIT;

	status_is_aa = status_is_aa && options.vi.aa;
	status_is_bilinear = status_is_bilinear && options.vi.scale;

	regs.status &= ~(VI_CONTROL_AA_MODE_MASK | VI_CONTROL_META_AA_BIT | VI_CONTROL_META_SCALE_BIT);
	if (status_is_aa)
		regs.status |= VI_CONTROL_META_AA_BIT;
	if (status_is_bilinear)
		regs.status |= VI_CONTROL_META_SCALE_BIT;

	if (!options.vi.gamma_dither)
		regs.status &= ~VI_CONTROL_GAMMA_DITHER_ENABLE_BIT;
	if (!options.vi.divot_filter)
		regs.status &= ~VI_CONTROL_DIVOT_ENABLE_BIT;
	if (!options.vi.dither_filter)
		regs.status &= ~VI_CONTROL_DITHER_FILTER_ENABLE_BIT;

	bool is_blank = (regs.status & VI_CONTROL_TYPE_RGBA5551_BIT) == 0;
	if (is_blank && previous_frame_blank)
	{
		frame_count++;
		prev_scanout_image.reset();
		return scanout;
	}

	if (is_blank)
		prev_scanout_image.reset();

	regs.status |= VI_CONTROL_TYPE_RGBA5551_BIT;
	previous_frame_blank = is_blank;

	bool divot = (regs.status & VI_CONTROL_DIVOT_ENABLE_BIT) != 0;

	if (regs.h_res <= 0 || regs.h_start >= VI_SCANOUT_WIDTH)
	{
		frame_count++;

		// A dirty hack to make it work for games which strobe the invalid state (but expect the image to persist),
		// and games which legitimately render invalid frames for long stretches where a black screen is expected.
		if (options.persist_frame_on_invalid_input && (frame_count - last_valid_frame_count < 4))
		{
			scanout = prev_scanout_image;

			if (scanout && prev_image_layout != target_layout)
			{
				auto cmd = device->request_command_buffer();
				cmd->image_barrier(*scanout, prev_image_layout, target_layout,
				                   layout_to_stage(prev_image_layout), 0,
				                   layout_to_stage(target_layout), layout_to_access(target_layout));
				prev_image_layout = target_layout;
				device->submit(cmd);
			}
		}
		else
			prev_scanout_image.reset();

		return scanout;
	}

	last_valid_frame_count = frame_count;

	bool degenerate = regs.h_res <= 0 || regs.v_res <= 0;

	regs.h_start *= scaling_factor;
	regs.h_start_clamp *= scaling_factor;
	regs.v_start *= scaling_factor;

	regs.h_res *= scaling_factor;
	regs.h_res_clamp *= scaling_factor;
	regs.v_res *= scaling_factor;

	regs.max_x = regs.max_x * scaling_factor + (scaling_factor - 1);
	regs.max_y = regs.max_y * scaling_factor + (scaling_factor - 1);

	for (unsigned n = 0; n < VI_MAX_OUTPUT_SCANLINES; n++)
	{
		auto &line = lines[n];
		line.h_start *= scaling_factor;
		line.h_start_clamp *= scaling_factor;
		line.h_end_clamp *= scaling_factor;
		line.x_start *= scaling_factor;
		line.y_start *= scaling_factor;
		line.y_base *= scaling_factor;
	}

	// First we copy data out of VRAM into a texture which we will then perform our post-AA on.
	// We do this on the async queue so we don't have to stall async queue on graphics work to deal with WAR hazards.
	// After the copy, we can immediately begin rendering new frames while we do post in parallel.
	Vulkan::ImageHandle vram_image;
	if (!degenerate)
		vram_image = vram_fetch_stage(regs, scaling_factor);

	auto cmd = device->request_command_buffer();

	// In the first pass, we need to read from VRAM and apply the fetch filter.
	// This is either the AA filter if coverage < 7, or the dither reconstruction filter if coverage == 7 and enabled.
	// Following that, post-AA filter, we have the divot filter.
	// In this filter, we need to find the median value of three horizontal pixels, post AA if any of them have coverage < 7.
	// Finally, we lerp the result based on x_add and y_add, and then, apply gamma/dither on top as desired.

	// AA -> divot could probably be done with compute and shared memory, but ideally this is done in fragment shaders in this implementation
	// so that we can run higher-priority compute shading workload async in the async queue.
	// We also get to take advantage of framebuffer compression FWIW.

	Vulkan::ImageHandle aa_image;
	if (!degenerate)
		aa_image = aa_fetch_stage(*cmd, *vram_image, regs, scaling_factor);

	// Divot pass
	Vulkan::ImageHandle divot_image;
	if (divot && !degenerate)
		divot_image = divot_stage(*cmd, *aa_image, regs, scaling_factor);
	else
		divot_image = rdp_move(aa_image);

	// Scale pass
	bool is_final_pass = !downscale_steps || scaling_factor <= 1;
	bool serrate = (regs.status & VI_CONTROL_SERRATE_BIT) != 0;

	// The downscales and the deinterlace after the scale pass are folded
	// into it, so their images are never written and read back; the blend
	// of the previous frame (weave) with them, reading it where each scale
	// pixel ends up, rather than at the scale pass's own coordinates in the
	// downscaled frame. (A frame is only blended or deinterlaced, never
	// both, here.)
	ScaleFold fold = {};
	{
		unsigned f = unsigned(scaling_factor), steps = downscale_steps;
		while (f > 1 && steps)
		{
			f /= 2;
			steps--;
			fold.downscale_steps++;
		}
	}
	// (the field is deinterlaced only after a downscale: without one, the
	// frame is the field's height)
	fold.deinterlace = fold.downscale_steps && serrate && options.upscale_deinterlacing;
	fold.field_select = regs.v_current_line == 0;
	bool can_fold = vi_scale_fold && !fold_disabled && !degenerate && divot_image &&
	                !(options.blend_previous_frame && fold.deinterlace) &&
	                fold.downscale_steps > 0 && fold.downscale_steps <= 3;

	auto scale_image = scale_stage(*cmd, divot_image.get(),
	                               regs, *horizontal,
	                               scaling_factor, degenerate, options,
	                               is_final_pass || can_fold, can_fold ? &fold : nullptr);
	if (can_fold)
		is_final_pass = true;

	auto src_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	if (!is_final_pass && scale_image)
	{
		cmd->image_barrier(*scale_image, src_layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		                   layout_to_stage(src_layout), layout_to_access(src_layout),
		                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);

		is_final_pass = !serrate || !options.upscale_deinterlacing;

		scale_image = downscale_stage(*cmd, *scale_image, scaling_factor, downscale_steps,
									  options, is_final_pass);

		src_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	}

	if (!is_final_pass && scale_image)
	{
		cmd->image_barrier(*scale_image, src_layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		                   layout_to_stage(src_layout), layout_to_access(src_layout),
		                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);

		bool field_state = regs.v_current_line == 0;
		scale_image = upscale_deinterlace(*cmd, *scale_image,
		                                  rdp_max(1, scaling_factor >> downscale_steps),
		                                  field_state, options);
		src_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	}

	if (scale_image)
	{
		cmd->image_barrier(*scale_image, src_layout, target_layout,
		                   layout_to_stage(src_layout), layout_to_access(src_layout),
		                   layout_to_stage(target_layout), layout_to_access(target_layout));
	}

	prev_image_layout = target_layout;
	prev_scanout_image = scale_image;

	device->submit(cmd);
	scanout = rdp_move(scale_image);
	frame_count++;
	return scanout;
}
}
