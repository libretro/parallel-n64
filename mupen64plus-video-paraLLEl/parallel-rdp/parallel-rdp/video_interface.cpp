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
#include "luts.hpp"

#ifndef PARALLEL_RDP_SHADER_DIR
#include "shaders/slangmosh.hpp"
#endif

namespace RDP
{
void VideoInterface::set_device(Vulkan::Device *device_)
{
	device = device_;
	init_gamma_table();

	if (const char *env = getenv("VI_DEBUG"))
		debug_channel = strtol(env, nullptr, 0) != 0;
	if (const char *env = getenv("VI_DEBUG_X"))
		filter_debug_channel_x = strtol(env, nullptr, 0);
	if (const char *env = getenv("VI_DEBUG_Y"))
		filter_debug_channel_y = strtol(env, nullptr, 0);

	if (const char *timestamp_env = getenv("PARALLEL_RDP_BENCH"))
		timestamp = strtol(timestamp_env, nullptr, 0) > 0;
}

int VideoInterface::resolve_shader_define(const char *name, const char *define) const
{
	if (strcmp(define, "DEBUG_ENABLE") == 0)
		return int(debug_channel);
	else
		return 0;
}

void VideoInterface::message(const std::string &tag, uint32_t code, uint32_t x, uint32_t y, uint32_t, uint32_t num_words,
                             const Vulkan::DebugChannelInterface::Word *words)
{
	if (filter_debug_channel_x >= 0 && x != uint32_t(filter_debug_channel_x))
		return;
	if (filter_debug_channel_y >= 0 && y != uint32_t(filter_debug_channel_y))
		return;

	switch (num_words)
	{
	case 1:
		LOGI("(%u, %u), line %d.\n", x, y, words[0].s32);
		break;

	case 2:
		LOGI("(%u, %u), line %d: (%d).\n", x, y, words[0].s32, words[1].s32);
		break;

	case 3:
		LOGI("(%u, %u), line %d: (%d, %d).\n", x, y, words[0].s32, words[1].s32, words[2].s32);
		break;

	case 4:
		LOGI("(%u, %u), line %d: (%d, %d, %d).\n", x, y,
		     words[0].s32, words[1].s32, words[2].s32, words[3].s32);
		break;

	default:
		LOGE("Unknown number of generic parameters: %u\n", num_words);
		break;
	}
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

void VideoInterface::scanout_memory_range(unsigned &offset, unsigned &length)
{
	int x_start = (vi_registers[unsigned(VIRegister::XScale)] >> 16) & 0xfff;
	int y_start = (vi_registers[unsigned(VIRegister::YScale)] >> 16) & 0xfff;
	int h_start = (vi_registers[unsigned(VIRegister::HStart)] >> 16) & 0x3ff;
	int v_start = (vi_registers[unsigned(VIRegister::VStart)] >> 16) & 0x3ff;
	int h_end = vi_registers[unsigned(VIRegister::HStart)] & 0x3ff;
	int v_end = vi_registers[unsigned(VIRegister::VStart)] & 0x3ff;
	int h_res = h_end - h_start;
	int v_res = (v_end - v_start) >> 1;
	int x_add = vi_registers[unsigned(VIRegister::XScale)] & 0xfff;
	int y_add = vi_registers[unsigned(VIRegister::YScale)] & 0xfff;
	int v_sync = vi_registers[unsigned(VIRegister::VSync)] & 0x3ff;

	uint32_t status = vi_registers[unsigned(VIRegister::Control)];

	bool divot = (status & VI_CONTROL_DIVOT_ENABLE_BIT) != 0;
	bool is_pal = unsigned(v_sync) > (VI_V_SYNC_NTSC + 25);
	h_start -= is_pal ? VI_H_OFFSET_PAL : VI_H_OFFSET_NTSC;

	int v_start_offset = is_pal ? VI_V_OFFSET_PAL : VI_V_OFFSET_NTSC;
	v_start = (v_start - v_start_offset) / 2;

	if (h_start < 0)
	{
		x_start -= x_add * h_start;
		h_res += h_start;
		h_start = 0;
	}

	if (h_start + h_res > 640)
		h_res = 640 - h_start;

	if (v_start < 0)
	{
		y_start -= y_add * v_start;
		v_start = 0;
	}

	int max_x = (x_start + h_res * x_add) >> 10;
	int max_y = (y_start + v_res * y_add) >> 10;

	// Need to sample a 2-pixel border to have room for AA filter and divot.
	int aa_width = max_x + 2 + 4 + int(divot) * 2;
	// 1 pixel border on top and bottom.
	int aa_height = max_y + 1 + 4;

	int x_off = divot ? -3 : -2;
	int y_off = -2;

	int vi_width = vi_registers[unsigned(VIRegister::Width)] & 0xfff;
	int vi_offset = vi_registers[unsigned(VIRegister::Origin)] & 0xffffff;

	if (vi_offset == 0 || h_res <= 0 || h_start >= 640)
	{
		offset = 0;
		length = 0;
		return;
	}

	int pixel_size = ((status & VI_CONTROL_TYPE_MASK) | VI_CONTROL_TYPE_RGBA5551_BIT) == VI_CONTROL_TYPE_RGBA8888_BIT ? 4 : 2;
	vi_offset &= ~(pixel_size - 1);
	vi_offset += (x_off + y_off * vi_width) * pixel_size;

	offset = vi_offset;
	length = (aa_height * vi_width + aa_width) * pixel_size;
}

Vulkan::ImageHandle VideoInterface::scanout(VkImageLayout target_layout, const ScanoutOptions &options)
{
	Vulkan::ImageHandle scanout;

	int v_start = (vi_registers[unsigned(VIRegister::VStart)] >> 16) & 0x3ff;
	int h_start = (vi_registers[unsigned(VIRegister::HStart)] >> 16) & 0x3ff;

	int v_end = vi_registers[unsigned(VIRegister::VStart)] & 0x3ff;
	int h_end = vi_registers[unsigned(VIRegister::HStart)] & 0x3ff;

	int h_res = h_end - h_start;
	int v_res = (v_end - v_start) >> 1;

	int x_add = vi_registers[unsigned(VIRegister::XScale)] & 0xfff;
	int x_start = (vi_registers[unsigned(VIRegister::XScale)] >> 16) & 0xfff;
	int y_add = vi_registers[unsigned(VIRegister::YScale)] & 0xfff;
	int y_start = (vi_registers[unsigned(VIRegister::YScale)] >> 16) & 0xfff;

	int v_sync = vi_registers[unsigned(VIRegister::VSync)] & 0x3ff;
	unsigned v_current_line = vi_registers[unsigned(VIRegister::VCurrentLine)] & 1;

	int vi_width = vi_registers[unsigned(VIRegister::Width)] & 0xfff;
	int vi_offset = vi_registers[unsigned(VIRegister::Origin)] & 0xffffff;

	if (vi_offset == 0)
	{
		prev_scanout_image.reset();
		return scanout;
	}

	uint32_t status = vi_registers[unsigned(VIRegister::Control)];

	if (!options.vi.serrate)
		status &= ~VI_CONTROL_SERRATE_BIT;

	bool status_is_aa = (status & VI_CONTROL_AA_MODE_MASK) < VI_CONTROL_AA_MODE_RESAMP_ONLY_BIT;
	bool status_is_bilinear = (status & VI_CONTROL_AA_MODE_MASK) < VI_CONTROL_AA_MODE_RESAMP_REPLICATE_BIT;

	status_is_aa = status_is_aa && options.vi.aa;
	status_is_bilinear = status_is_bilinear && options.vi.scale;

	status &= ~(VI_CONTROL_AA_MODE_MASK | VI_CONTROL_META_AA_BIT | VI_CONTROL_META_SCALE_BIT);
	if (status_is_aa)
		status |= VI_CONTROL_META_AA_BIT;
	if (status_is_bilinear)
		status |= VI_CONTROL_META_SCALE_BIT;

	if (!options.vi.gamma_dither)
		status &= ~VI_CONTROL_GAMMA_DITHER_ENABLE_BIT;
	if (!options.vi.divot_filter)
		status &= ~VI_CONTROL_DIVOT_ENABLE_BIT;
	if (!options.vi.dither_filter)
		status &= ~VI_CONTROL_DITHER_FILTER_ENABLE_BIT;

	bool is_blank = (status & VI_CONTROL_TYPE_RGBA5551_BIT) == 0;
	if (is_blank && previous_frame_blank)
	{
		frame_count++;
		prev_scanout_image.reset();
		return scanout;
	}

	if (is_blank)
		prev_scanout_image.reset();

	status |= VI_CONTROL_TYPE_RGBA5551_BIT;
	previous_frame_blank = is_blank;

	bool serrate = (status & VI_CONTROL_SERRATE_BIT) != 0;
	bool field_state = v_current_line == 0;

	bool is_pal = unsigned(v_sync) > (VI_V_SYNC_NTSC + 25);
	h_start -= is_pal ? VI_H_OFFSET_PAL : VI_H_OFFSET_NTSC;

	int v_start_offset = is_pal ? VI_V_OFFSET_PAL : VI_V_OFFSET_NTSC;
	v_start = (v_start - v_start_offset) / 2;

#if 0
	int v_active_lines = v_sync - v_start_offset;
	if (v_active_lines >= 0)
		v_active_lines >>= int(!serrate);
#endif

	bool divot = (status & VI_CONTROL_DIVOT_ENABLE_BIT) != 0;

	bool left_clamp = false;
	bool right_clamp = false;

	if (h_start < 0)
	{
		x_start -= x_add * h_start;
		h_res += h_start;
		h_start = 0;
		left_clamp = true;
	}

	if (h_start + h_res > 640)
	{
		h_res = 640 - h_start;
		right_clamp = true;
	}

	if (v_start < 0)
	{
		y_start -= y_add * v_start;
		v_start = 0;
	}

	if (h_res <= 0 || h_start >= 640)
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

	bool degenerate = h_res <= 0 || v_res <= 0;

	// First we copy data out of VRAM into a texture which we will then perform our post-AA on.
	// We do this on the async queue so we don't have to stall async queue on graphics work to deal with WAR hazards.
	// After the copy, we can immediately begin rendering new frames while we do post in parallel.
	Vulkan::ImageHandle vram_image;
	Vulkan::QueryPoolHandle start_ts, end_ts;

	if (!degenerate)
	{
		auto async_cmd = device->request_command_buffer(Vulkan::CommandBuffer::Type::AsyncCompute);

		if (timestamp)
			start_ts = async_cmd->write_timestamp(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

		int max_x = (x_start + h_res * x_add) >> 10;
		int max_y = (y_start + v_res * y_add) >> 10;

		// Need to sample a 2-pixel border to have room for AA filter and divot.
		int aa_width = max_x + 2 + 4 + int(divot) * 2;
		// 1 pixel border on top and bottom.
		int aa_height = max_y + 1 + 4;

		Vulkan::ImageCreateInfo rt_info = Vulkan::ImageCreateInfo::render_target(aa_width, aa_height, VK_FORMAT_R8G8B8A8_UINT);
		rt_info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		rt_info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		rt_info.misc = Vulkan::IMAGE_MISC_CONCURRENT_QUEUE_GRAPHICS_BIT |
		               Vulkan::IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_COMPUTE_BIT;
		vram_image = device->create_image(rt_info);
		vram_image->set_layout(Vulkan::Layout::General);

		async_cmd->image_barrier(*vram_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
		                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
		                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT);

#ifdef PARALLEL_RDP_SHADER_DIR
		async_cmd->set_program("rdp://extract_vram.comp");
#else
		async_cmd->set_program(shader_bank->extract_vram);
#endif
		async_cmd->set_storage_texture(0, 0, vram_image->get_view());
		async_cmd->set_storage_buffer(0, 1, *rdram, rdram_offset, rdram_size);
		async_cmd->set_storage_buffer(0, 2, *hidden_rdram);

		struct Push
		{
			uint32_t fb_offset;
			uint32_t fb_width;
			int32_t x_offset;
			int32_t y_offset;
			int32_t x_res;
			int32_t y_res;
		} push = {};

		if ((status & VI_CONTROL_TYPE_MASK) == VI_CONTROL_TYPE_RGBA8888_BIT)
			push.fb_offset = vi_offset >> 2;
		else
			push.fb_offset = vi_offset >> 1;

		push.fb_width = vi_width;
		push.x_offset = divot ? -3 : -2;
		push.y_offset = -2;
		push.x_res = aa_width;
		push.y_res = aa_height;

		async_cmd->set_specialization_constant_mask(3);
		async_cmd->set_specialization_constant(0, uint32_t(rdram_size));
		async_cmd->set_specialization_constant(1, status & (VI_CONTROL_TYPE_MASK | VI_CONTROL_META_AA_BIT));

		async_cmd->push_constants(&push, 0, sizeof(push));
		async_cmd->dispatch((aa_width + 15) / 16, (aa_height + 7) / 8, 1);
		// Just enforce an execution barrier here for rendering work in next frame.
		async_cmd->barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
		                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0);

		if (timestamp)
		{
			end_ts = async_cmd->write_timestamp(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
			device->register_time_interval("VI GPU", std::move(start_ts), std::move(end_ts), "extract-vram");
		}

		Vulkan::Semaphore sem;
		device->submit(async_cmd, nullptr, 1, &sem);
		device->add_wait_semaphore(Vulkan::CommandBuffer::Type::Generic, std::move(sem),
		                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, true);
	}

	auto cmd = device->request_command_buffer();

	if (debug_channel)
		cmd->begin_debug_channel(this, "VI", 32 * 1024 * 1024);

	// In the first pass, we need to read from VRAM and apply the fetch filter.
	// This is either the AA filter if coverage < 7, or the dither reconstruction filter if coverage == 7 and enabled.
	// Following that, post-AA filter, we have the divot filter.
	// In this filter, we need to find the median value of three horizontal pixels, post AA if any of them have coverage < 7.
	// Finally, we lerp the result based on x_add and y_add, and then, apply gamma/dither on top as desired.

	Vulkan::ImageHandle aa_image;

	// If we risk sampling same Y coordinate for two scanlines we can trigger this case, so add workaround paths for it.
	bool fetch_bug = y_add < 1024;

	// AA -> divot could probably be done with compute and shared memory, but ideally this is done in fragment shaders in this implementation
	// so that we can run higher-priority compute shading workload async in the async queue.
	// We also get to take advantage of framebuffer compression FWIW.

	if (!degenerate)
	{
		// For the AA pass, we need to figure out how many pixels we might need to read.
		int max_x = (x_start + h_res * x_add) >> 10;
		int max_y = (y_start + v_res * y_add) >> 10;
		int aa_width = max_x + 3 + int(divot) * 2;
		int aa_height = max_y + 2;

		Vulkan::ImageCreateInfo rt_info = Vulkan::ImageCreateInfo::render_target(aa_width, aa_height, VK_FORMAT_R8G8B8A8_UINT);
		rt_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		rt_info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		rt_info.layers = fetch_bug ? 2 : 1;
		rt_info.misc = Vulkan::IMAGE_MISC_FORCE_ARRAY_BIT;
		aa_image = device->create_image(rt_info);

		Vulkan::ImageViewCreateInfo view_info = {};
		view_info.image = aa_image.get();
		view_info.view_type = VK_IMAGE_VIEW_TYPE_2D;
		view_info.layers = 1;

		Vulkan::ImageViewHandle aa_primary, aa_secondary;
		view_info.base_layer = 0;
		aa_primary = device->create_image_view(view_info);

		if (fetch_bug)
		{
			view_info.base_layer = 1;
			aa_secondary = device->create_image_view(view_info);
		}

		Vulkan::RenderPassInfo rp;
		rp.color_attachments[0] = aa_primary.get();
		rp.clear_attachments = 0;

		if (fetch_bug)
		{
			rp.color_attachments[1] = aa_secondary.get();
			rp.num_color_attachments = 2;
			rp.store_attachments = 3;
		}
		else
		{
			rp.num_color_attachments = 1;
			rp.store_attachments = 1;
		}

		cmd->image_barrier(*aa_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
		                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

		if (timestamp)
			start_ts = cmd->write_timestamp(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

		cmd->begin_render_pass(rp);
		cmd->set_opaque_state();

#ifdef PARALLEL_RDP_SHADER_DIR
		cmd->set_program("rdp://fullscreen.vert", "rdp://vi_fetch.frag",
		                 {
				                 { "DEBUG_ENABLE", debug_channel ? 1 : 0 },
				                 { "FETCH_BUG", fetch_bug ? 1 : 0 },
		                 });
#else
		cmd->set_program(device->request_program(shader_bank->fullscreen, shader_bank->vi_fetch[int(fetch_bug)]));
#endif

		struct Push
		{
			int32_t x_offset;
			int32_t y_offset;
		} push = {};

		push.x_offset = 2;
		push.y_offset = 2;

		cmd->push_constants(&push, 0, sizeof(push));

		cmd->set_specialization_constant_mask(3);
		cmd->set_specialization_constant(0, uint32_t(rdram_size));
		cmd->set_specialization_constant(1,
		                                 status & (VI_CONTROL_META_AA_BIT | VI_CONTROL_DITHER_FILTER_ENABLE_BIT));

		cmd->set_texture(0, 0, vram_image->get_view());
		cmd->draw(3);
		cmd->end_render_pass();

		if (timestamp)
		{
			end_ts = cmd->write_timestamp(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
			device->register_time_interval("VI GPU", std::move(start_ts), std::move(end_ts), "vi-fetch");
		}

		cmd->image_barrier(*aa_image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
	}

	// Divot pass
	Vulkan::ImageHandle divot_image;

	if (divot && !degenerate)
	{
		// For the divot pass, we need to figure out how many pixels we might need to read.
		int max_x = (x_start + h_res * x_add) >> 10;
		int max_y = (y_start + v_res * y_add) >> 10;
		int divot_width = max_x + 2;
		int divot_height = max_y + 2;

		Vulkan::ImageCreateInfo rt_info = Vulkan::ImageCreateInfo::render_target(divot_width, divot_height, VK_FORMAT_R8G8B8A8_UINT);
		rt_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		rt_info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		rt_info.layers = fetch_bug ? 2 : 1;
		rt_info.misc = Vulkan::IMAGE_MISC_FORCE_ARRAY_BIT;
		divot_image = device->create_image(rt_info);

		Vulkan::ImageViewCreateInfo view_info = {};
		view_info.image = divot_image.get();
		view_info.view_type = VK_IMAGE_VIEW_TYPE_2D;
		view_info.layers = 1;

		Vulkan::ImageViewHandle divot_primary, divot_secondary;
		view_info.base_layer = 0;
		divot_primary = device->create_image_view(view_info);

		if (fetch_bug)
		{
			view_info.base_layer = 1;
			divot_secondary = device->create_image_view(view_info);
		}

		Vulkan::RenderPassInfo rp;
		rp.color_attachments[0] = divot_primary.get();
		rp.clear_attachments = 0;

		if (fetch_bug)
		{
			rp.color_attachments[1] = divot_secondary.get();
			rp.num_color_attachments = 2;
			rp.store_attachments = 3;
		}
		else
		{
			rp.num_color_attachments = 1;
			rp.store_attachments = 1;
		}

		cmd->image_barrier(*divot_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
		                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

		if (timestamp)
			start_ts = cmd->write_timestamp(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

		cmd->begin_render_pass(rp);
		cmd->set_opaque_state();

#ifdef PARALLEL_RDP_SHADER_DIR
		cmd->set_program("rdp://fullscreen.vert", "rdp://vi_divot.frag", {
				{ "DEBUG_ENABLE", debug_channel ? 1 : 0 },
				{ "FETCH_BUG", fetch_bug ? 1 : 0 },
		});
#else
		cmd->set_program(device->request_program(shader_bank->fullscreen, shader_bank->vi_divot[int(fetch_bug)]));
#endif

		cmd->set_texture(0, 0, aa_image->get_view());
		cmd->draw(3);
		cmd->end_render_pass();

		if (timestamp)
		{
			end_ts = cmd->write_timestamp(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
			device->register_time_interval("VI GPU", std::move(start_ts), std::move(end_ts), "vi-divot");
		}

		cmd->image_barrier(*divot_image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
	}
	else
		divot_image = std::move(aa_image);

	VkRect2D crop_rect = {};
	bool can_crop = false;

	// Scale pass
	Vulkan::ImageHandle scale_image;
	{
		Vulkan::ImageCreateInfo rt_info = Vulkan::ImageCreateInfo::render_target(
				640, (is_pal ? VI_V_RES_PAL: VI_V_RES_NTSC) >> int(!serrate), VK_FORMAT_R8G8B8A8_UNORM);
		rt_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		rt_info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		rt_info.misc = Vulkan::IMAGE_MISC_MUTABLE_SRGB_BIT;
		scale_image = device->create_image(rt_info);

		Vulkan::RenderPassInfo rp;
		rp.color_attachments[0] = &scale_image->get_view();
		memset(&rp.clear_color[0], 0, sizeof(rp.clear_color[0]));
		rp.num_color_attachments = 1;
		rp.clear_attachments = 1;
		rp.store_attachments = 1;

		cmd->image_barrier(*scale_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
		                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

		if (prev_scanout_image && prev_image_layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		{
			cmd->image_barrier(*prev_scanout_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			                   VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
			                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
		}

		if (timestamp)
			start_ts = cmd->write_timestamp(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

		cmd->begin_render_pass(rp);

		cmd->set_specialization_constant_mask((1 << 1) | (1 << 2));
		cmd->set_specialization_constant(1,
		                                 status & (VI_CONTROL_GAMMA_ENABLE_BIT |
		                                           VI_CONTROL_GAMMA_DITHER_ENABLE_BIT |
		                                           VI_CONTROL_META_SCALE_BIT |
		                                           VI_CONTROL_META_AA_BIT));
		cmd->set_specialization_constant(2, uint32_t(fetch_bug));

		struct Push
		{
			int32_t x_offset, y_offset;
			int32_t h_offset, v_offset;
			uint32_t x_add;
			uint32_t y_add;
			uint32_t frame_count;

			uint32_t serrate_shift;
			uint32_t serrate_mask;
			uint32_t serrate_select;
		} push = {};

		if (serrate)
		{
			v_start *= 2;
			v_res *= 2;
			push.serrate_shift = 1;
			push.serrate_mask = 1;
			push.serrate_select = int(field_state);
		}

		push.x_offset = x_start;
		push.y_offset = y_start;
		push.h_offset = h_start;
		push.v_offset = v_start;
		push.x_add = x_add;
		push.y_add = y_add;
		push.frame_count = frame_count;

		cmd->set_opaque_state();
#ifdef PARALLEL_RDP_SHADER_DIR
		cmd->set_program("rdp://fullscreen.vert", "rdp://vi_scale.frag", {
				{ "DEBUG_ENABLE", debug_channel ? 1 : 0 },
		});
#else
		cmd->set_program(device->request_program(shader_bank->fullscreen, shader_bank->vi_scale));
#endif
		cmd->set_buffer_view(1, 0, *gamma_lut_view);

		auto h_start_field = h_start;
		auto h_res_field = h_res;

		if (!left_clamp)
		{
			h_start += 8;
			h_res -= 8;
		}

		if (!right_clamp)
			h_res -= 7;

		cmd->push_constants(&push, 0, sizeof(push));

		if (!degenerate && h_res > 0 && v_res > 0)
		{
			cmd->set_texture(0, 0, divot_image->get_view());
			cmd->set_scissor({{ h_start, v_start }, { uint32_t(h_res), uint32_t(v_res) }});
			cmd->draw(3);

			can_crop = true;
			crop_rect.offset.x = h_start;
			crop_rect.offset.y = v_start;
			crop_rect.extent.width = h_res;
			crop_rect.extent.height = v_res;
		}

		// To deal with interlacing and other "persistence effects", we blend in previous frame's result.
		// This is somewhat arbitrary, but seems to work well enough in practice.

		if (prev_scanout_image)
		{
			cmd->set_blend_enable(true);
			cmd->set_blend_factors(VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA, VK_BLEND_FACTOR_DST_ALPHA);
			// Don't overwrite alpha, it's already zero.
			cmd->set_color_write_mask(0x7);
			cmd->set_specialization_constant_mask(0);
			cmd->set_texture(0, 0, prev_scanout_image->get_view());
#ifdef PARALLEL_RDP_SHADER_DIR
			cmd->set_program("rdp://fullscreen.vert", "rdp://vi_blend_fields.frag", {
					{ "DEBUG_ENABLE", debug_channel ? 1 : 0 },
			});
#else
			cmd->set_program(device->request_program(shader_bank->fullscreen, shader_bank->vi_blend_fields));
#endif

			if (degenerate)
			{
				if (h_res_field > 0)
				{
					cmd->set_scissor({{ h_start_field, 0 }, { uint32_t(h_res_field), prev_scanout_image->get_height() }});
					cmd->draw(3);
				}
			}
			else
			{
				// Top part.
				if (h_res_field > 0 && v_start > 0)
				{
					cmd->set_scissor({{ h_start_field, 0 }, { uint32_t(h_res_field), uint32_t(v_start) }});
					cmd->draw(3);
				}

				// Middle part, don't overwrite the 8 pixel guard band.
				if (h_res > 0 && v_res > 0)
				{
					cmd->set_scissor({{ h_start, v_start }, { uint32_t(h_res), uint32_t(v_res) }});
					cmd->draw(3);
				}

				// Bottom part.
				if (h_res_field > 0 && prev_scanout_image->get_height() > uint32_t(v_start + v_res))
				{
					cmd->set_scissor({{ h_start_field, v_start + v_res },
					                  { uint32_t(h_res_field), prev_scanout_image->get_height() - uint32_t(v_start + v_res) }});
					cmd->draw(3);
				}
			}
		}

		cmd->end_render_pass();

		if (timestamp)
		{
			end_ts = cmd->write_timestamp(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
			device->register_time_interval("VI GPU", std::move(start_ts), std::move(end_ts), "vi-scale");
		}
	}

	VkImageLayout src_layout;

	// Need separate copy pass so we can keep a full reference frame around for interlacing, etc.
	// TODO: Figure out if we can optimize this safely.
	if (options.crop_overscan && can_crop)
	{
		prev_image_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		prev_scanout_image = scale_image;

		cmd->image_barrier(*prev_scanout_image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);

		Vulkan::ImageCreateInfo rt_info = Vulkan::ImageCreateInfo::render_target(
				crop_rect.extent.width, crop_rect.extent.height, VK_FORMAT_R8G8B8A8_UNORM);
		rt_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		rt_info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		rt_info.misc = Vulkan::IMAGE_MISC_MUTABLE_SRGB_BIT;
		scale_image = device->create_image(rt_info);

		cmd->image_barrier(*scale_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
		                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);

		cmd->copy_image(*scale_image, *prev_scanout_image, {}, { crop_rect.offset.x, crop_rect.offset.y, 0 },
		                { crop_rect.extent.width, crop_rect.extent.height, 1 },
		                { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
		                { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 });

		src_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	}
	else
	{
		src_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		prev_image_layout = target_layout;
		prev_scanout_image = scale_image;
	}

	cmd->image_barrier(*scale_image, src_layout, target_layout,
	                   layout_to_stage(src_layout), layout_to_access(src_layout),
	                   layout_to_stage(target_layout), layout_to_access(target_layout));

	device->submit(cmd);
	scanout = std::move(scale_image);
	frame_count++;
	return scanout;
}

}
