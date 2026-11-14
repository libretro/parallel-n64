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

#include <math.h>
#include <stddef.h>
#include <string.h>
#include "video_interface.h"
#include "pipeline_record.h"
#include "luts.hpp"
#include "upscaler_shaders.hpp"

/* ---- small things ---- */
static unsigned trailing_zeroes(unsigned v)
{
	unsigned n = 0;
	if (!v)
		return 32;
	while (!(v & 1))
	{
		v >>= 1;
		n++;
	}
	return n;
}

static unsigned floor_log2(unsigned v)
{
	unsigned n = 0;
	while (v > 1)
	{
		v >>= 1;
		n++;
	}
	return n;
}

static VkRect2D make_rect(int x, int y, uint32_t width, uint32_t height)
{
	VkRect2D rect;
	rect.offset.x = x;
	rect.offset.y = y;
	rect.extent.width = width;
	rect.extent.height = height;
	return rect;
}

static void shift_rect(VkRect2D *rect, int x, int y)
{
	rect->offset.x += x;
	rect->offset.y += y;

	if (rect->offset.x < 0)
	{
		rect->extent.width += rect->offset.x;
		rect->offset.x = 0;
	}

	if (rect->offset.y < 0)
	{
		rect->extent.height += rect->offset.y;
		rect->offset.y = 0;
	}

	/* Check for signed overflow without relying on -fwrapv. */
	if (rect->extent.width & 0x80000000u)
		rect->extent.width = 0;
	if (rect->extent.height & 0x80000000u)
		rect->extent.height = 0;
}

static VkPipelineStageFlags layout_to_stage(VkImageLayout layout)
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

/* A colour target of one level and layer, in no layout yet */
static void target_info(granite_image_info_t *info, unsigned width, unsigned height, VkFormat format,
		VkImageUsageFlags usage)
{
	memset(info, 0, sizeof(*info));
	info->width = width;
	info->height = height;
	info->depth = 1;
	info->levels = 1;
	info->format = format;
	info->type = VK_IMAGE_TYPE_2D;
	info->layers = 1;
	info->usage = usage;
	info->samples = VK_SAMPLE_COUNT_1_BIT;
	info->initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
}

static void unref_image(granite_image_t **image)
{
	if (*image)
		granite_image_unref(*image);
	*image = NULL;
}

/* ---- pipeline state ---- */
static void set_extract_vram_pipeline(const rdp_vi_t *vi, granite_command_buffer_t *cb, uint32_t status,
		unsigned scaling_factor)
{
	granite_cb_set_program(cb, vi->shader_bank->extract_vram);
	granite_cb_set_spec_constant_mask(cb, 7);
	granite_cb_set_spec_constant(cb, 0, (uint32_t)vi->rdram_size);
	granite_cb_set_spec_constant(cb, 1, status & (VI_CONTROL_TYPE_MASK | VI_CONTROL_META_AA_BIT));
	granite_cb_set_spec_constant(cb, 2, trailing_zeroes(scaling_factor));
}

static void set_upscaler_color_pipeline(const rdp_vi_t *vi, granite_command_buffer_t *cb, bool rgba32,
		unsigned scaling_factor, bool gamma)
{
	granite_cb_set_program(cb, rdp_upscaler_color_program(vi->device));
	granite_cb_set_spec_constant_mask(cb, 0xf);
	granite_cb_set_spec_constant(cb, 0, (uint32_t)vi->rdram_size);
	granite_cb_set_spec_constant(cb, 1, (uint32_t)rgba32);
	granite_cb_set_spec_constant(cb, 2, trailing_zeroes(scaling_factor));
	granite_cb_set_spec_constant(cb, 3, (uint32_t)gamma);
}

static granite_program_t *graphics_program(const rdp_vi_t *vi, const granite_shader_t *vertex,
		const granite_shader_t *fragment)
{
	return granite_program_cache_graphics(&vi->device->program_cache, vertex, fragment);
}

static void set_fetch_pipeline(const rdp_vi_t *vi, granite_command_buffer_t *cb, uint32_t status, bool fetch_bug)
{
	granite_cb_set_opaque_state(cb);
	granite_cb_set_program(cb, graphics_program(vi, vi->shader_bank->fullscreen,
				vi->shader_bank->vi_fetch[fetch_bug ? 1 : 0]));
	granite_cb_set_spec_constant_mask(cb, 3);
	granite_cb_set_spec_constant(cb, 0, (uint32_t)vi->rdram_size);
	granite_cb_set_spec_constant(cb, 1, status & (VI_CONTROL_META_AA_BIT | VI_CONTROL_DITHER_FILTER_ENABLE_BIT));
}

static void set_divot_pipeline(const rdp_vi_t *vi, granite_command_buffer_t *cb, bool fetch_bug)
{
	granite_cb_set_opaque_state(cb);
	granite_cb_set_program(cb, graphics_program(vi, vi->shader_bank->fullscreen,
				vi->shader_bank->vi_divot[fetch_bug ? 1 : 0]));
}

static void set_scale_pipeline(const rdp_vi_t *vi, granite_command_buffer_t *cb, uint32_t status, bool fetch_bug,
		bool fold)
{
	granite_cb_set_spec_constant_mask(cb, (1 << 1) | (1 << 2));
	granite_cb_set_spec_constant(cb, 1, status & (VI_CONTROL_GAMMA_ENABLE_BIT | VI_CONTROL_GAMMA_DITHER_ENABLE_BIT |
				VI_CONTROL_META_SCALE_BIT | VI_CONTROL_META_AA_BIT));
	granite_cb_set_spec_constant(cb, 2, (uint32_t)fetch_bug);
	granite_cb_set_opaque_state(cb);
	granite_cb_set_program(cb, graphics_program(vi, vi->shader_bank->fullscreen,
				fold ? vi->vi_scale_fold : vi->shader_bank->vi_scale));
}

static void set_blend_fields_pipeline(const rdp_vi_t *vi, granite_command_buffer_t *cb)
{
	granite_pipeline_state_t *state = &cb->pipeline_state.static_state;

	state->state.blend_enable = 1;
	state->state.src_color_blend = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
	state->state.src_alpha_blend = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
	state->state.dst_color_blend = VK_BLEND_FACTOR_DST_ALPHA;
	state->state.dst_alpha_blend = VK_BLEND_FACTOR_DST_ALPHA;
	/* Don't overwrite alpha, it's already zero. */
	state->state.write_mask = 0x7;
	granite_cb_set_dirty(cb, GRANITE_COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT);
	granite_cb_set_spec_constant_mask(cb, 0);
	granite_cb_set_program(cb, graphics_program(vi, vi->shader_bank->fullscreen,
				vi->shader_bank->vi_blend_fields));
}

static void set_deinterlace_pipeline(const rdp_vi_t *vi, granite_command_buffer_t *cb)
{
	granite_cb_set_opaque_state(cb);
	granite_cb_set_program(cb, graphics_program(vi, vi->shader_bank->vi_deinterlace_vert,
				vi->shader_bank->vi_deinterlace_frag));
}

/* ---- setup ---- */
void rdp_vi_scanout_options_init(rdp_vi_scanout_options_t *options)
{
	memset(options, 0, sizeof(*options));
	options->upscale_deinterlacing = true;
	options->vi.aa = true;
	options->vi.scale = true;
	options->vi.serrate = true;
	options->vi.dither_filter = true;
	options->vi.divot_filter = true;
	options->vi.gamma_dither = true;
	options->upscaler.jitter_phase = 8;
}

void rdp_vi_set_device(rdp_vi_t *vi, granite_device_t *device)
{
	granite_buffer_info_t info;
	granite_buffer_view_info_t view;

	vi->device = device;

	if (vi->gamma_lut_view)
		granite_buffer_view_unref(vi->gamma_lut_view);
	if (vi->gamma_lut)
		granite_buffer_unref(vi->gamma_lut);
	vi->gamma_lut_view = NULL;

	memset(&info, 0, sizeof(info));
	info.domain = GRANITE_BUFFER_DOMAIN_DEVICE;
	info.size = sizeof(gamma_table);
	info.usage = VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
	vi->gamma_lut = granite_device_create_buffer(device, &info, gamma_table);
	if (!vi->gamma_lut)
		return;

	memset(&view, 0, sizeof(view));
	view.buffer = vi->gamma_lut;
	view.range = sizeof(gamma_table);
	view.format = VK_FORMAT_R8_UINT;
	vi->gamma_lut_view = granite_device_create_buffer_view(device, &view);
}

void rdp_vi_free(rdp_vi_t *vi)
{
	unsigned i;

	rdp_vi_image_pool_free(&vi->image_pool);
	unref_image(&vi->prev_upscaler_image);
	unref_image(&vi->prev_scanout_image);
	unref_image(&vi->no_previous_frame);
	for (i = 0; i < RDP_VI_MAX_HORIZONTAL_BUFFERS; i++)
	{
		if (vi->horizontal_buffers[i].view)
			granite_buffer_view_unref(vi->horizontal_buffers[i].view);
		if (vi->horizontal_buffers[i].buffer)
			granite_buffer_unref(vi->horizontal_buffers[i].buffer);
	}
	if (vi->gamma_lut_view)
		granite_buffer_view_unref(vi->gamma_lut_view);
	if (vi->gamma_lut)
		granite_buffer_unref(vi->gamma_lut);
	memset(vi, 0, sizeof(*vi));
}

void rdp_vi_set_rdram(rdp_vi_t *vi, const granite_buffer_t *rdram, size_t offset, size_t size)
{
	vi->rdram = rdram;
	vi->rdram_offset = offset;
	vi->rdram_size = size;
}

void rdp_vi_set_shader_bank(rdp_vi_t *vi, const rdp_shader_bank_t *bank)
{
	static const uint32_t black = 0;
	granite_resource_layout_t layout;
	granite_image_initial_data_t initial;
	granite_image_info_t info;

	vi->shader_bank = bank;
	vi->vi_scale_fold = NULL;
	if (!bank || !vi->device)
		return;

	/* vi_scale's own layout, with the fold's registers after its own. */
	memset(&layout, 0, sizeof(layout));
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
	vi->vi_scale_fold = granite_program_cache_shader(&vi->device->program_cache, vi_scale_fold_frag,
			sizeof(vi_scale_fold_frag), &layout);

	target_info(&info, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);
	info.initial_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	initial.data = &black;
	initial.row_length = 1;
	unref_image(&vi->no_previous_frame);
	vi->no_previous_frame = granite_device_create_image(vi->device, &info, &initial);
}

void rdp_vi_begin_frame_context(rdp_vi_t *vi)
{
	vi->frame_serial++;
	if (vi->device)
		rdp_vi_image_pool_trim(&vi->image_pool, vi->frame_serial, vi->device->num_frames);
}

/* ---- the pipelines a scanout can ask for ---- */
/* On 1x1 targets of the formats scanout uses: only their formats reach a
 * pipeline. */
void rdp_vi_queue_known_pipelines(rdp_vi_t *vi, unsigned upscaling, bool with_upscaler,
		rdp_known_pipeline_fn add, void *ctx)
{
	granite_device_t *device = vi->device;
	granite_command_buffer_t *cb = granite_device_request_command_buffer(device, GRANITE_QUEUE_GRAPHICS);
	granite_image_info_t info;
	granite_image_t *targets[3];
	granite_render_pass_info_t rp;
	/* the fetch bug only exists without upscaling */
	const unsigned bugs = upscaling == 1 ? 2 : 1;
	unsigned bug, fold, scale, rgba32, gamma, i;
	uint32_t mask, status;

	if (!cb)
		return;

	target_info(&info, 1, 1, VK_FORMAT_R8G8B8A8_UINT,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
	targets[0] = granite_device_create_image(device, &info, NULL);
	targets[1] = granite_device_create_image(device, &info, NULL);
	info.format = VK_FORMAT_R8G8B8A8_UNORM;
	targets[2] = granite_device_create_image(device, &info, NULL);

	if (targets[0] && targets[1] && targets[2])
	{
		for (i = 0; i < 3; i++)
			granite_cb_image_barrier(cb, targets[i], VK_IMAGE_LAYOUT_UNDEFINED,
					VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
					VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

		/* one attachment without the fetch bug, two with it */
		for (bug = 0; bug < bugs; bug++)
		{
			granite_render_pass_info_init(&rp);
			rp.color_attachments[0] = targets[0]->view;
			rp.color_attachments[1] = targets[1]->view;
			rp.num_color_attachments = bug + 1;
			rp.store_attachments = (1u << rp.num_color_attachments) - 1;

			if (!granite_cb_begin_render_pass(cb, &rp))
				continue;
			mask = VI_CONTROL_META_AA_BIT | VI_CONTROL_DITHER_FILTER_ENABLE_BIT;
			for (status = mask;; status = (status - 1) & mask)
			{
				set_fetch_pipeline(vi, cb, status, bug != 0);
				add(ctx, cb, PIPELINE_RECORD_GRAPHICS);
				if (!status)
					break;
			}
			set_divot_pipeline(vi, cb, bug != 0);
			add(ctx, cb, PIPELINE_RECORD_GRAPHICS);
			granite_cb_end_render_pass(cb);
		}

		granite_render_pass_info_init(&rp);
		rp.color_attachments[0] = targets[2]->view;
		rp.num_color_attachments = 1;
		rp.store_attachments = 1;

		if (granite_cb_begin_render_pass(cb, &rp))
		{
			mask = VI_CONTROL_GAMMA_ENABLE_BIT | VI_CONTROL_GAMMA_DITHER_ENABLE_BIT |
				VI_CONTROL_META_SCALE_BIT | VI_CONTROL_META_AA_BIT;
			for (bug = 0; bug < bugs; bug++)
				for (fold = 0; fold < (vi->vi_scale_fold ? 2u : 1u); fold++)
					for (status = mask;; status = (status - 1) & mask)
					{
						set_scale_pipeline(vi, cb, status, bug != 0, fold != 0);
						add(ctx, cb, PIPELINE_RECORD_GRAPHICS);
						if (!status)
							break;
					}

			granite_cb_set_opaque_state(cb);
			set_blend_fields_pipeline(vi, cb);
			add(ctx, cb, PIPELINE_RECORD_GRAPHICS);

			set_deinterlace_pipeline(vi, cb);
			add(ctx, cb, PIPELINE_RECORD_GRAPHICS);
			granite_cb_end_render_pass(cb);
		}
	}

	mask = VI_CONTROL_TYPE_MASK | VI_CONTROL_META_AA_BIT;
	for (scale = 1; scale <= upscaling; scale = (scale == upscaling) ? scale + 1 : upscaling)
		for (status = mask;; status = (status - 1) & mask)
		{
			/* a blank or reserved type scans out as RGBA5551 */
			if (status & VI_CONTROL_TYPE_RGBA5551_BIT)
			{
				set_extract_vram_pipeline(vi, cb, status, scale);
				add(ctx, cb, PIPELINE_RECORD_COMPUTE);
			}
			if (!status)
				break;
		}

	if (with_upscaler)
	{
		for (scale = 1; scale <= upscaling; scale = (scale == upscaling) ? scale + 1 : upscaling)
			for (rgba32 = 0; rgba32 < 2; rgba32++)
				for (gamma = 0; gamma < 2; gamma++)
				{
					set_upscaler_color_pipeline(vi, cb, rgba32 != 0, scale, gamma != 0);
					add(ctx, cb, PIPELINE_RECORD_COMPUTE);
				}
		rdp_upscaler_queue_known_pipelines(device, cb, add, ctx);
	}

	granite_device_submit(device, cb, NULL, 0, NULL);
	for (i = 0; i < 3; i++)
		unref_image(&targets[i]);
}

/* ---- pooled images ---- */
/* Only when the size or format changes, or while the pool fills. */
static const rdp_vi_image_entry_t *create_pooled_image(rdp_vi_t *vi, const granite_image_info_t *info,
		bool layer_views)
{
	rdp_vi_image_entry_t *e = rdp_vi_image_pool_add(&vi->image_pool, info, vi->frame_serial,
			vi->device->num_frames);
	unsigned layer;

	/* (the entry takes the references) */
	e->image = granite_device_create_image(vi->device, info, NULL);
	if (layer_views && e->image)
	{
		granite_image_view_info_t view_info;
		memset(&view_info, 0, sizeof(view_info));
		view_info.image = e->image;
		view_info.view_type = VK_IMAGE_VIEW_TYPE_2D;
		view_info.levels = VK_REMAINING_MIP_LEVELS;
		view_info.layers = 1;
		for (layer = 0; layer < info->layers && layer < 2; layer++)
		{
			view_info.base_layer = layer;
			e->layer_views[layer] = granite_device_create_image_view(vi->device, &view_info);
		}
	}
	return e;
}

static const rdp_vi_image_entry_t *acquire_image(rdp_vi_t *vi, const granite_image_info_t *info, bool layer_views)
{
	const rdp_vi_image_entry_t *e = rdp_vi_image_pool_reuse(&vi->image_pool, info, layer_views,
			vi->frame_serial, vi->device->num_frames);
	if (e)
		return e;
	return create_pooled_image(vi, info, layer_views);
}

/* A reference of the caller's own to an entry's image (NULL if it has none) */
static granite_image_t *held_image(const rdp_vi_image_entry_t *e)
{
	if (e->image)
		granite_image_ref(e->image);
	return e->image;
}

/* ---- the stages of a scanout ---- */
static void global_barrier(granite_command_buffer_t *cb, VkPipelineStageFlags src_stage, VkAccessFlags src_access,
		VkPipelineStageFlags dst_stage, VkAccessFlags dst_access)
{
	VkMemoryBarrier global;
	memset(&global, 0, sizeof(global));
	global.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	global.srcAccessMask = src_access;
	global.dstAccessMask = dst_access;
	granite_cb_barrier(cb, src_stage, dst_stage, 1, &global, 0, NULL, 0, NULL);
}

/* The async queue's work is done: the graphics queue's next submission
 * waits for it at @stages */
static void submit_async(rdp_vi_t *vi, granite_command_buffer_t *async_cb, VkPipelineStageFlags stages)
{
	granite_semaphore_t *semaphore = NULL;

	granite_device_submit(vi->device, async_cb, NULL, 1, &semaphore);
	if (semaphore)
	{
		granite_device_add_wait_semaphore(vi->device, GRANITE_QUEUE_GRAPHICS, semaphore, stages, true);
		granite_semaphore_unref(semaphore);
	}
}

static void update_upscaled_domain(rdp_vi_t *vi, granite_command_buffer_t *async_cb, unsigned pixel_size_log2)
{
	unsigned offset, length;

	rdp_vi_scanout_memory_range(vi->registers, &offset, &length);
	vi->upscaled.update(vi->upscaled.user, async_cb, offset, length, pixel_size_log2);
	global_barrier(async_cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
}

static granite_image_t *vram_fetch_stage(rdp_vi_t *vi, const rdp_vi_decoded_t *regs, unsigned scaling_factor)
{
	granite_command_buffer_t *async_cb = granite_device_request_command_buffer(vi->device, GRANITE_QUEUE_COMPUTE);
	granite_image_t *vram_image;
	granite_image_info_t rt_info;
	const bool divot = (regs->status & VI_CONTROL_DIVOT_ENABLE_BIT) != 0;
	/* Need to sample a 2-pixel border to have room for AA filter and divot. */
	const int extract_width = regs->max_x + 2 + 4 + (divot ? 2 : 0);
	/* 1 pixel border on top and bottom. */
	const int extract_height = regs->max_y + 1 + 4;
	struct
	{
		uint32_t fb_offset;
		uint32_t fb_width;
		int32_t x_offset;
		int32_t y_offset;
		int32_t x_res;
		int32_t y_res;
	} push;

	if (!async_cb)
		return NULL;

	if (scaling_factor > 1)
		update_upscaled_domain(vi, async_cb,
				(regs->status & VI_CONTROL_TYPE_MASK) == VI_CONTROL_TYPE_RGBA8888_BIT ? 2 : 1);

	target_info(&rt_info, extract_width, extract_height, VK_FORMAT_R8G8B8A8_UINT,
			VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
	rt_info.misc = GRANITE_IMAGE_MISC_CONCURRENT_QUEUE_GRAPHICS_BIT |
		GRANITE_IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_COMPUTE_BIT;
	vram_image = held_image(acquire_image(vi, &rt_info, false));
	if (!vram_image)
	{
		granite_device_submit(vi->device, async_cb, NULL, 0, NULL);
		return NULL;
	}
	vram_image->general_layout = true;

	granite_cb_image_barrier(async_cb, vram_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT);

	granite_cb_set_storage_texture(async_cb, 0, 0, vram_image->view);

	if (scaling_factor > 1)
	{
		const granite_buffer_t *upscaled = vi->upscaled.rdram(vi->upscaled.user);
		const granite_buffer_t *hidden = vi->upscaled.hidden_rdram(vi->upscaled.user);
		granite_cb_set_storage_buffer(async_cb, 0, 1, upscaled, 0, upscaled->info.size);
		granite_cb_set_storage_buffer(async_cb, 0, 2, hidden, 0, hidden->info.size);
	}
	else
	{
		granite_cb_set_storage_buffer(async_cb, 0, 1, vi->rdram, vi->rdram_offset, vi->rdram_size);
		granite_cb_set_storage_buffer(async_cb, 0, 2, vi->hidden_rdram, 0, vi->hidden_rdram->info.size);
	}

	if ((regs->status & VI_CONTROL_TYPE_MASK) == VI_CONTROL_TYPE_RGBA8888_BIT)
		push.fb_offset = regs->vi_offset >> 2;
	else
		push.fb_offset = regs->vi_offset >> 1;

	push.fb_width = regs->vi_width;
	push.x_offset = divot ? -3 : -2;
	push.y_offset = -2;
	push.x_res = extract_width;
	push.y_res = extract_height;

	set_extract_vram_pipeline(vi, async_cb, regs->status, scaling_factor);

	granite_cb_push_constants(async_cb, &push, 0, sizeof(push));
	granite_cb_dispatch(async_cb, (extract_width + 15) / 16, (extract_height + 7) / 8, 1);

	/* Just enforce an execution barrier here for rendering work in next frame. */
	global_barrier(async_cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0);

	submit_async(vi, async_cb, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
	return vram_image;
}

/* Begins a pass over @e's layers: both with the fetch bug. False when
 * it cannot be. */
static bool begin_layer_pass(granite_command_buffer_t *cb, const rdp_vi_image_entry_t *e, bool fetch_bug)
{
	granite_render_pass_info_t rp;

	granite_render_pass_info_init(&rp);
	rp.color_attachments[0] = e->layer_views[0];
	rp.clear_attachments = 0;

	if (fetch_bug)
	{
		rp.color_attachments[1] = e->layer_views[1];
		rp.num_color_attachments = 2;
		rp.store_attachments = 3;
	}
	else
	{
		rp.num_color_attachments = 1;
		rp.store_attachments = 1;
	}

	granite_cb_image_barrier(cb, e->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

	return e->layer_views[0] && (!fetch_bug || e->layer_views[1]) && granite_cb_begin_render_pass(cb, &rp);
}

/* Draws it, and leaves @image to be read by the next pass */
static void end_layer_pass(granite_command_buffer_t *cb, const granite_image_t *image)
{
	granite_cb_draw(cb, 3, 1, 0, 0);
	granite_cb_end_render_pass(cb);

	granite_cb_image_barrier(cb, image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
}

static granite_image_t *aa_fetch_stage(rdp_vi_t *vi, granite_command_buffer_t *cb, const granite_image_t *vram_image,
		const rdp_vi_decoded_t *regs, unsigned scaling_factor)
{
	const bool fetch_bug = rdp_vi_need_fetch_bug_emulation(regs, scaling_factor);
	const bool divot = (regs->status & VI_CONTROL_DIVOT_ENABLE_BIT) != 0;
	/* For the AA pass, we need to figure out how many pixels we might need to read. */
	const int aa_width = regs->max_x + 3 + (divot ? 2 : 0);
	const int aa_height = regs->max_y + 2;
	granite_image_info_t rt_info;
	const rdp_vi_image_entry_t *aa;
	granite_image_t *aa_image;
	struct
	{
		int32_t x_offset;
		int32_t y_offset;
	} push;

	target_info(&rt_info, aa_width, aa_height, VK_FORMAT_R8G8B8A8_UINT,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
	rt_info.layers = fetch_bug ? 2 : 1;
	rt_info.misc = GRANITE_IMAGE_MISC_FORCE_ARRAY_BIT;
	aa = acquire_image(vi, &rt_info, true);
	aa_image = held_image(aa);
	if (!aa_image)
		return NULL;
	if (!begin_layer_pass(cb, aa, fetch_bug))
	{
		granite_image_unref(aa_image);
		return NULL;
	}

	set_fetch_pipeline(vi, cb, regs->status, fetch_bug);

	push.x_offset = 2;
	push.y_offset = 2;
	granite_cb_push_constants(cb, &push, 0, sizeof(push));

	granite_cb_set_texture(cb, 0, 0, vram_image->view, -1);
	end_layer_pass(cb, aa_image);
	return aa_image;
}

static granite_image_t *divot_stage(rdp_vi_t *vi, granite_command_buffer_t *cb, const granite_image_t *aa_image,
		const rdp_vi_decoded_t *regs, unsigned scaling_factor)
{
	const bool fetch_bug = rdp_vi_need_fetch_bug_emulation(regs, scaling_factor);
	/* For the divot pass, we need to figure out how many pixels we might need to read. */
	const int divot_width = regs->max_x + 2;
	const int divot_height = regs->max_y + 2;
	granite_image_info_t rt_info;
	const rdp_vi_image_entry_t *divot;
	granite_image_t *divot_image;

	target_info(&rt_info, divot_width, divot_height, VK_FORMAT_R8G8B8A8_UINT,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
	rt_info.layers = fetch_bug ? 2 : 1;
	rt_info.misc = GRANITE_IMAGE_MISC_FORCE_ARRAY_BIT;
	divot = acquire_image(vi, &rt_info, true);
	divot_image = held_image(divot);
	if (!divot_image)
		return NULL;
	if (!begin_layer_pass(cb, divot, fetch_bug))
	{
		granite_image_unref(divot_image);
		return NULL;
	}

	set_divot_pipeline(vi, cb, fetch_bug);
	granite_cb_set_texture(cb, 0, 0, aa_image->view, -1);
	end_layer_pass(cb, divot_image);
	return divot_image;
}

static rdp_vi_horizontal_buffer_t *acquire_horizontal_buffer(rdp_vi_t *vi)
{
	const uint64_t contexts = vi->device->num_frames;
	rdp_vi_horizontal_buffer_t *slot = NULL;
	unsigned i;

	for (i = 0; i < RDP_VI_MAX_HORIZONTAL_BUFFERS; i++)
	{
		rdp_vi_horizontal_buffer_t *b = &vi->horizontal_buffers[i];
		if (b->buffer && b->free_serial <= vi->frame_serial)
		{
			slot = b;
			break;
		}
		if (!b->buffer && !slot)
			slot = b;
	}
	if (!slot)
		return NULL;

	if (!slot->buffer)
	{
		granite_buffer_info_t info;
		granite_buffer_view_info_t view_info;

		memset(&info, 0, sizeof(info));
		info.size = VI_MAX_OUTPUT_SCANLINES * sizeof(rdp_vi_horizontal_info_t);
		info.usage = VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
		/* Read back by the CPU while scaling, so cached. */
		info.domain = GRANITE_BUFFER_DOMAIN_CACHED_COHERENT_HOST_PREFER_CACHED;
		slot->buffer = granite_device_create_buffer(vi->device, &info, NULL);
		if (!slot->buffer)
			return NULL;

		memset(&view_info, 0, sizeof(view_info));
		view_info.format = VK_FORMAT_R32G32B32A32_SINT;
		view_info.buffer = slot->buffer;
		view_info.range = VI_MAX_OUTPUT_SCANLINES * sizeof(rdp_vi_horizontal_info_t);
		slot->view = granite_device_create_buffer_view(vi->device, &view_info);
		if (!slot->view)
		{
			granite_buffer_unref(slot->buffer);
			slot->buffer = NULL;
			return NULL;
		}
	}

	slot->free_serial = vi->frame_serial + contexts;
	return slot;
}

/* The passes after the scale pass that it can do itself: its 2:1
 * downscales, and the deinterlace of a bobbed field. */
typedef struct scale_fold
{
	unsigned downscale_steps;
	bool deinterlace;
	bool field_select;
} scale_fold_t;

/* vi_scale's registers, then vi_scale_fold's */
typedef struct scale_push
{
	int32_t h_offset, v_offset;
	int32_t v_start;
	uint32_t y_add;
	uint32_t frame_count;

	uint32_t serrate_shift;
	uint32_t serrate_mask;
	uint32_t serrate_select;

	uint32_t info_y_shift;

	/* vi_scale_fold only */
	int32_t pad;
	int32_t rect_lo[2], rect_hi[2];
	int32_t downscale_steps;
	int32_t deinterlace_mul, deinterlace_add, deinterlace_rows;
	int32_t blend_lo[3][2], blend_hi[3][2];
} scale_push_t;

/* vi_scale_fold's registers are 120 bytes */
typedef char scale_push_is_120_bytes[sizeof(scale_push_t) == 120 ? 1 : -1];

/* Draws the previous frame over @rect, moved by the crop, if any of it is left */
static void draw_blend_rect(granite_command_buffer_t *cb, VkRect2D rect, unsigned crop_left, unsigned crop_top)
{
	shift_rect(&rect, -(int)crop_left, -(int)crop_top);
	if (rect.extent.width > 0 && rect.extent.height > 0)
	{
		granite_cb_set_scissor(cb, &rect);
		granite_cb_draw(cb, 3, 1, 0, 0);
	}
}

static granite_image_t *scale_stage(rdp_vi_t *vi, granite_command_buffer_t *cb, const granite_image_t *divot_image,
		const rdp_vi_decoded_t *registers, const rdp_vi_horizontal_buffer_t *lines, unsigned scaling_factor,
		bool degenerate, const rdp_vi_scanout_options_t *options, const scale_fold_t *fold)
{
	rdp_vi_decoded_t regs = *registers;
	const bool fetch_bug = rdp_vi_need_fetch_bug_emulation(&regs, scaling_factor);
	const bool serrate = (regs.status & VI_CONTROL_SERRATE_BIT) != 0 && !options->upscale_deinterlacing;
	/* vi_scale's registers end where the fold's begin */
	const size_t push_size = fold ? sizeof(scale_push_t) : offsetof(scale_push_t, pad);
	granite_image_t *scale_image;
	granite_image_info_t rt_info;
	granite_render_pass_info_t rp;
	scale_push_t push;
	unsigned crop_left = 0;
	unsigned crop_right = 0;
	unsigned crop_top = 0;
	unsigned crop_bottom = 0;
	unsigned scale_height;
	/* What the folded passes need: the downscaled image's height, and how
	 * the deinterlace reads it, in quarter rows (see vi_scale_fold.frag). */
	int deinterlace_mul = 0, deinterlace_add = 0, deinterlace_rows = 0;

	target_info(&rt_info, VI_SCANOUT_WIDTH * scaling_factor,
			((regs.is_pal ? VI_V_RES_PAL : VI_V_RES_NTSC) >> (serrate ? 0 : 1)) * scaling_factor,
			VK_FORMAT_R8G8B8A8_UNORM,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
	rt_info.misc = GRANITE_IMAGE_MISC_MUTABLE_SRGB_BIT;

	if (options->crop_rect.enable)
	{
		crop_left = options->crop_rect.left;
		crop_right = options->crop_rect.right;
		crop_top = options->crop_rect.top;
		crop_bottom = options->crop_rect.bottom;

		if (serrate)
		{
			crop_top *= 2;
			crop_bottom *= 2;
		}
	}
	else
	{
		/* Rescale crop pixels to preserve aspect ratio. */
		const unsigned crop_pixels_y = options->crop_overscan_pixels * (serrate ? 2 : 1);
		const unsigned crop_pixels_x = (unsigned)floor(
				(double)((float)crop_pixels_y * ((float)rt_info.width / (float)rt_info.height)) + 0.5);

		crop_left = crop_right = crop_pixels_x;
		crop_top = crop_bottom = crop_pixels_y;
	}

	crop_left *= scaling_factor;
	crop_right *= scaling_factor;
	crop_top *= scaling_factor;
	crop_bottom *= scaling_factor;

	/* (a crop that leaves nothing is not applied) */
	if (crop_left + crop_right < rt_info.width && crop_top + crop_bottom < rt_info.height)
	{
		rt_info.width -= crop_left + crop_right;
		rt_info.height -= crop_top + crop_bottom;
	}

	scale_height = rt_info.height;
	if (fold)
	{
		rt_info.width >>= fold->downscale_steps;
		rt_info.height >>= fold->downscale_steps;
		if (fold->deinterlace)
		{
			/* As upscale_deinterlace samples it: a quarter field line up or
			 * down, at twice the height at 1x. */
			const unsigned scaled = scaling_factor >> fold->downscale_steps;
			const int field_scale = (int)(scaled > 1 ? scaled : 1);
			deinterlace_rows = (int)rt_info.height;
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

	scale_image = held_image(acquire_image(vi, &rt_info, false));
	if (!scale_image)
		return NULL;

	granite_render_pass_info_init(&rp);
	rp.color_attachments[0] = scale_image->view;
	memset(&rp.clear_color[0], 0, sizeof(rp.clear_color[0]));
	rp.num_color_attachments = 1;
	rp.clear_attachments = 1;
	rp.store_attachments = 1;

	granite_cb_image_barrier(cb, scale_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

	if (vi->prev_scanout_image && vi->prev_image_layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		granite_cb_image_barrier(cb, vi->prev_scanout_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);

	if (!granite_cb_begin_render_pass(cb, &rp))
	{
		granite_image_unref(scale_image);
		return NULL;
	}

	memset(&push, 0, sizeof(push));
	push.info_y_shift = floor_log2(scaling_factor);

	if (serrate)
	{
		regs.v_start *= 2;
		regs.v_res *= 2;
		push.serrate_shift = 1;
		push.serrate_mask = 1;
		push.serrate_select = regs.v_current_line == 0 ? 1 : 0;
		push.info_y_shift++;
	}

	push.h_offset = (int)crop_left;
	push.v_offset = (int)crop_top;
	push.v_start = regs.v_start;
	push.y_add = regs.init_y_add;
	push.frame_count = vi->frame_count;

	set_scale_pipeline(vi, cb, regs.status, fetch_bug, fold != NULL);
	granite_cb_set_buffer_view(cb, 1, 0, vi->gamma_lut_view);
	/* Written in place: flushed if the memory is not coherent. */
	granite_device_unmap_host_buffer(vi->device, lines->buffer, GRANITE_MEMORY_ACCESS_WRITE_BIT, 0,
			lines->buffer->info.size);
	granite_cb_set_buffer_view(cb, 0, 1, lines->view);

	if (!degenerate && divot_image && regs.h_res > 0 && regs.v_res > 0)
	{
		VkRect2D rect = make_rect(regs.h_start, regs.v_start, (uint32_t)regs.h_res, (uint32_t)regs.v_res);
		shift_rect(&rect, -(int)crop_left, -(int)crop_top);

		if (rect.extent.width > 0 && rect.extent.height > 0)
		{
			granite_cb_set_texture(cb, 0, 0, divot_image->view, -1);
			if (fold)
			{
				/* The blend pass's rects, below, in the scale pass's
				 * coordinates; the previous frame is read where the scale
				 * pixel ends up, so it has to be this frame's size. */
				const granite_image_t *prev = vi->no_previous_frame;

				/* Every output pixel is drawn: the shader leaves the scale
				 * pixels outside the scissor at the clear colour itself. */
				push.rect_lo[0] = rect.offset.x;
				push.rect_lo[1] = rect.offset.y;
				push.rect_hi[0] = rect.offset.x + (int)rect.extent.width;
				push.rect_hi[1] = rect.offset.y + (int)rect.extent.height;
				push.downscale_steps = (int)fold->downscale_steps;
				push.deinterlace_mul = deinterlace_mul;
				push.deinterlace_add = deinterlace_add;
				push.deinterlace_rows = deinterlace_rows;

				if (vi->prev_scanout_image && options->blend_previous_frame &&
				    vi->prev_scanout_image->info.width == rt_info.width &&
				    vi->prev_scanout_image->info.height == rt_info.height)
				{
					const int bottom = (int)(scale_height + crop_top);
					const int below = bottom - (regs.v_start + regs.v_res);
					VkRect2D blend[3];
					bool use[3];
					unsigned i;

					blend[0] = make_rect(regs.h_start, 0, (uint32_t)regs.h_res, (uint32_t)regs.v_start);
					blend[1] = make_rect(regs.h_start_clamp, regs.v_start, (uint32_t)regs.h_res_clamp,
							(uint32_t)regs.v_res);
					blend[2] = make_rect(regs.h_start, regs.v_start + regs.v_res, (uint32_t)regs.h_res,
							(uint32_t)(below > 0 ? below : 0));
					use[0] = regs.h_res > 0 && regs.v_start > 0;
					use[1] = regs.h_res_clamp > 0 && regs.v_res > 0;
					use[2] = regs.h_res > 0 && bottom > regs.v_start + regs.v_res;

					for (i = 0; i < 3; i++)
					{
						if (!use[i])
							continue;
						shift_rect(&blend[i], -(int)crop_left, -(int)crop_top);
						push.blend_lo[i][0] = blend[i].offset.x;
						push.blend_lo[i][1] = blend[i].offset.y;
						push.blend_hi[i][0] = blend[i].offset.x + (int)blend[i].extent.width;
						push.blend_hi[i][1] = blend[i].offset.y + (int)blend[i].extent.height;
					}
					prev = vi->prev_scanout_image;
				}
				granite_cb_set_texture(cb, 0, 2, prev->view, -1);
			}
			else
				granite_cb_set_scissor(cb, &rect);
			granite_cb_push_constants(cb, &push, 0, push_size);
			granite_cb_draw(cb, 3, 1, 0, 0);
		}
	}
	else
		granite_cb_push_constants(cb, &push, 0, push_size);

	/* To deal with weave interlacing and other "persistence effects", we
	 * blend in previous frame's result. This is somewhat arbitrary, but
	 * seems to work well enough in practice. */
	if (vi->prev_scanout_image && options->blend_previous_frame && !fold)
	{
		const uint32_t prev_height = vi->prev_scanout_image->info.height;

		set_blend_fields_pipeline(vi, cb);
		granite_cb_set_texture(cb, 0, 0, vi->prev_scanout_image->view, -1);

		if (degenerate)
		{
			if (regs.h_res > 0)
				draw_blend_rect(cb, make_rect(regs.h_start, 0, (uint32_t)regs.h_res, prev_height),
						crop_left, crop_top);
		}
		else
		{
			/* Top part. */
			if (regs.h_res > 0 && regs.v_start > 0)
				draw_blend_rect(cb, make_rect(regs.h_start, 0, (uint32_t)regs.h_res, (uint32_t)regs.v_start),
						crop_left, crop_top);

			/* Middle part, don't overwrite the 8 pixel guard band. */
			if (regs.h_res_clamp > 0 && regs.v_res > 0)
				draw_blend_rect(cb, make_rect(regs.h_start_clamp, regs.v_start, (uint32_t)regs.h_res_clamp,
							(uint32_t)regs.v_res), crop_left, crop_top);

			/* Bottom part. */
			if (regs.h_res > 0 && prev_height > (uint32_t)(regs.v_start + regs.v_res))
				draw_blend_rect(cb, make_rect(regs.h_start, regs.v_start + regs.v_res, (uint32_t)regs.h_res,
							prev_height - (uint32_t)(regs.v_start + regs.v_res)), crop_left, crop_top);
		}
	}

	granite_cb_end_render_pass(cb);
	return scale_image;
}

/* @scale_image halved @downscale_steps times, or until it is at 1x: the
 * last of them, in TRANSFER_DST_OPTIMAL, or NULL */
static granite_image_t *downscale_stage(rdp_vi_t *vi, granite_command_buffer_t *cb,
		const granite_image_t *scale_image, unsigned scaling_factor, unsigned downscale_steps)
{
	static const VkOffset3D origin = { 0, 0, 0 };
	granite_image_t *downscale_image = NULL;
	const granite_image_t *input = scale_image;

	while (scaling_factor > 1 && downscale_steps)
	{
		const unsigned width = input->info.width;
		const unsigned height = input->info.height;
		granite_image_info_t rt_info;
		granite_image_t *output;
		VkOffset3D dst_extent, src_extent;

		if (input != scale_image)
			granite_cb_image_barrier(cb, input, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
					VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);

		target_info(&rt_info, width / 2, height / 2, VK_FORMAT_R8G8B8A8_UNORM,
				VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
		rt_info.misc = GRANITE_IMAGE_MISC_MUTABLE_SRGB_BIT;

		scaling_factor /= 2;
		downscale_steps--;

		output = held_image(acquire_image(vi, &rt_info, false));
		/* (the pool keeps the one before for as long as the frame needs it) */
		unref_image(&downscale_image);
		if (!output)
			return NULL;
		downscale_image = output;

		granite_cb_image_barrier(cb, downscale_image, VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);

		dst_extent.x = (int)rt_info.width;
		dst_extent.y = (int)rt_info.height;
		dst_extent.z = 1;
		src_extent.x = (int)width;
		src_extent.y = (int)height;
		src_extent.z = 1;
		granite_cb_blit_image(cb, downscale_image, input, &origin, &dst_extent, &origin, &src_extent,
				0, 0, 0, 0, 1, VK_FILTER_LINEAR);

		input = downscale_image;
	}

	return downscale_image;
}

static granite_image_t *upscale_deinterlace(rdp_vi_t *vi, granite_command_buffer_t *cb,
		const granite_image_t *scale_image, unsigned scaling_factor, bool field_select)
{
	granite_image_t *deinterlaced_image;
	granite_image_info_t rt_info;
	granite_render_pass_info_t rp;
	float y_offset;

	/* If we're running upscaled, upscaling Y further is somewhat meaningless and bandwidth intensive. */
	target_info(&rt_info, scale_image->info.width, scale_image->info.height * (scaling_factor == 1 ? 2 : 1),
			VK_FORMAT_R8G8B8A8_UNORM,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
	rt_info.misc = GRANITE_IMAGE_MISC_MUTABLE_SRGB_BIT;

	deinterlaced_image = held_image(acquire_image(vi, &rt_info, false));
	if (!deinterlaced_image)
		return NULL;

	granite_render_pass_info_init(&rp);
	rp.color_attachments[0] = deinterlaced_image->view;
	rp.num_color_attachments = 1;
	rp.store_attachments = 1;

	granite_cb_image_barrier(cb, deinterlaced_image, VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

	if (!granite_cb_begin_render_pass(cb, &rp))
	{
		granite_image_unref(deinterlaced_image);
		return NULL;
	}
	set_deinterlace_pipeline(vi, cb);

	y_offset = ((float)scaling_factor * (field_select ? -0.25f : +0.25f)) / (float)scale_image->info.height;
	granite_cb_push_constants(cb, &y_offset, 0, sizeof(y_offset));

	granite_cb_set_texture(cb, 0, 0, scale_image->view, GRANITE_STOCK_SAMPLER_LINEAR_CLAMP);
	granite_cb_draw(cb, 3, 1, 0, 0);
	granite_cb_end_render_pass(cb);
	return deinterlaced_image;
}

/* ---- scanout ---- */
granite_image_t *rdp_vi_upscaler_scanout(rdp_vi_t *vi, unsigned scaling_factor,
		rdp_upscaler_framebuffer_t *fb, bool *repeat)
{
	rdp_vi_decoded_t regs;
	granite_image_info_t info;
	granite_image_t *image;
	granite_command_buffer_t *async_cb;
	unsigned size_log2;
	int width, height;
	struct
	{
		int32_t fb_offset;
		int32_t fb_width;
		int32_t res_x, res_y;
		int32_t shown_x0, shown_x1;
	} push;

	rdp_vi_decode_registers(vi->registers, &regs, NULL);
	memset(fb, 0, sizeof(*fb));
	*repeat = false;
	vi->frame_count++;

	if (regs.vi_offset == 0 || (regs.status & VI_CONTROL_TYPE_RGBA5551_BIT) == 0)
	{
		unref_image(&vi->prev_upscaler_image);
		return NULL;
	}

	size_log2 = (regs.status & VI_CONTROL_TYPE_MASK) == VI_CONTROL_TYPE_RGBA8888_BIT ? 2 : 1;
	width = regs.max_x < regs.vi_width ? regs.max_x : regs.vi_width;
	height = regs.max_y;

	if (regs.h_res <= 0 || regs.h_start >= VI_SCANOUT_WIDTH || regs.v_res <= 0 || width <= 0 || height <= 0)
	{
		/* As persist_frame_on_invalid_input: games strobe the invalid
		 * state between valid frames. */
		if (vi->prev_upscaler_image && vi->frame_count - vi->last_valid_frame_count < 4)
		{
			*fb = vi->prev_upscaler_fb;
			*repeat = true;
			granite_image_ref(vi->prev_upscaler_image);
			return vi->prev_upscaler_image;
		}
		unref_image(&vi->prev_upscaler_image);
		return NULL;
	}
	vi->last_valid_frame_count = vi->frame_count;

	/* The frame is read where the RDP draws it, on the async compute
	 * queue behind the drawing, and handed to the graphics queue by a
	 * semaphore, as vram_fetch_stage: a graphics command buffer does
	 * not see the RDP's queue */
	async_cb = granite_device_request_command_buffer(vi->device, GRANITE_QUEUE_COMPUTE);
	if (!async_cb)
		return NULL;

	if (scaling_factor > 1)
		update_upscaled_domain(vi, async_cb, size_log2);

	target_info(&info, width * scaling_factor, height * scaling_factor, VK_FORMAT_R8G8B8A8_UNORM,
			VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
	info.misc = GRANITE_IMAGE_MISC_CONCURRENT_QUEUE_GRAPHICS_BIT |
		GRANITE_IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_COMPUTE_BIT;
	image = held_image(acquire_image(vi, &info, false));
	if (!image)
	{
		granite_device_submit(vi->device, async_cb, NULL, 0, NULL);
		return NULL;
	}
	image->general_layout = true;

	granite_cb_image_barrier(async_cb, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT);

	granite_cb_set_storage_texture(async_cb, 0, 0, image->view);
	if (scaling_factor > 1)
	{
		const granite_buffer_t *upscaled = vi->upscaled.rdram(vi->upscaled.user);
		granite_cb_set_storage_buffer(async_cb, 0, 1, upscaled, 0, upscaled->info.size);
	}
	else
		granite_cb_set_storage_buffer(async_cb, 0, 1, vi->rdram, vi->rdram_offset, vi->rdram_size);
	granite_cb_set_buffer_view(async_cb, 0, 2, vi->gamma_lut_view);

	push.fb_offset = regs.vi_offset >> size_log2;
	push.fb_width = regs.vi_width;
	push.res_x = (int32_t)info.width;
	push.res_y = (int32_t)info.height;
	/* black beside what the scanout shows (the guard band it blanks at
	 * the sides), as the screen has it */
	push.shown_x0 = (regs.min_x > 0 ? regs.min_x : 0) * (int32_t)scaling_factor;
	push.shown_x1 = (regs.max_x_clamp < width ? regs.max_x_clamp : width) * (int32_t)scaling_factor;

	set_upscaler_color_pipeline(vi, async_cb, size_log2 == 2, scaling_factor,
			(regs.status & VI_CONTROL_GAMMA_ENABLE_BIT) != 0);
	granite_cb_push_constants(async_cb, &push, 0, sizeof(push));
	granite_cb_dispatch(async_cb, (info.width + 7) / 8, (info.height + 7) / 8, 1);

	granite_cb_image_barrier(async_cb, image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0);
	image->general_layout = false;

	submit_async(vi, async_cb, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

	fb->origin = (uint32_t)regs.vi_offset & ~((1u << size_log2) - 1u);
	fb->stride = (uint32_t)regs.vi_width;
	fb->size_log2 = size_log2;
	fb->width = (unsigned)width;
	fb->height = (unsigned)height;
	unref_image(&vi->prev_upscaler_image);
	granite_image_ref(image);
	vi->prev_upscaler_image = image;
	vi->prev_upscaler_fb = *fb;
	return image;
}

granite_image_t *rdp_vi_scanout(rdp_vi_t *vi, VkImageLayout target_layout,
		const rdp_vi_scanout_options_t *options, unsigned scaling_factor_)
{
	const unsigned downscale_steps = options->downscale_steps < 8 ? options->downscale_steps : 8;
	const int scaling_factor = (int)scaling_factor_;
	rdp_vi_horizontal_buffer_t *horizontal;
	rdp_vi_horizontal_info_t *lines;
	rdp_vi_decoded_t regs;
	granite_command_buffer_t *cb;
	granite_image_t *vram_image = NULL;
	granite_image_t *aa_image = NULL;
	granite_image_t *divot_image = NULL;
	granite_image_t *scale_image;
	granite_image_t *next;
	VkImageLayout src_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	scale_fold_t fold;
	bool status_is_aa, status_is_bilinear, is_blank, divot, degenerate, is_final_pass, serrate, can_fold;
	unsigned n;

	unref_image(&vi->prev_upscaler_image);

	horizontal = acquire_horizontal_buffer(vi);
	if (!horizontal)
		return NULL;
	lines = (rdp_vi_horizontal_info_t *)granite_device_map_host_buffer(vi->device, horizontal->buffer,
			GRANITE_MEMORY_ACCESS_WRITE_BIT, 0, horizontal->buffer->info.size);

	rdp_vi_decode_registers(vi->registers, &regs, lines);

	if (regs.vi_offset == 0)
	{
		unref_image(&vi->prev_scanout_image);
		return NULL;
	}

	if (!options->vi.serrate)
		regs.status &= ~VI_CONTROL_SERRATE_BIT;

	status_is_aa = (regs.status & VI_CONTROL_AA_MODE_MASK) < VI_CONTROL_AA_MODE_RESAMP_ONLY_BIT;
	status_is_bilinear = (regs.status & VI_CONTROL_AA_MODE_MASK) < VI_CONTROL_AA_MODE_RESAMP_REPLICATE_BIT;

	status_is_aa = status_is_aa && options->vi.aa;
	status_is_bilinear = status_is_bilinear && options->vi.scale;

	regs.status &= ~(VI_CONTROL_AA_MODE_MASK | VI_CONTROL_META_AA_BIT | VI_CONTROL_META_SCALE_BIT);
	if (status_is_aa)
		regs.status |= VI_CONTROL_META_AA_BIT;
	if (status_is_bilinear)
		regs.status |= VI_CONTROL_META_SCALE_BIT;

	if (!options->vi.gamma_dither)
		regs.status &= ~VI_CONTROL_GAMMA_DITHER_ENABLE_BIT;
	if (!options->vi.divot_filter)
		regs.status &= ~VI_CONTROL_DIVOT_ENABLE_BIT;
	if (!options->vi.dither_filter)
		regs.status &= ~VI_CONTROL_DITHER_FILTER_ENABLE_BIT;

	is_blank = (regs.status & VI_CONTROL_TYPE_RGBA5551_BIT) == 0;
	if (is_blank && vi->previous_frame_blank)
	{
		vi->frame_count++;
		unref_image(&vi->prev_scanout_image);
		return NULL;
	}

	if (is_blank)
		unref_image(&vi->prev_scanout_image);

	regs.status |= VI_CONTROL_TYPE_RGBA5551_BIT;
	vi->previous_frame_blank = is_blank;

	divot = (regs.status & VI_CONTROL_DIVOT_ENABLE_BIT) != 0;

	if (regs.h_res <= 0 || regs.h_start >= VI_SCANOUT_WIDTH)
	{
		vi->frame_count++;

		/* A dirty hack to make it work for games which strobe the invalid
		 * state (but expect the image to persist), and games which
		 * legitimately render invalid frames for long stretches where a
		 * black screen is expected. */
		if (options->persist_frame_on_invalid_input && (vi->frame_count - vi->last_valid_frame_count < 4))
		{
			granite_image_t *scanout = vi->prev_scanout_image;

			if (!scanout)
				return NULL;
			granite_image_ref(scanout);
			if (vi->prev_image_layout != target_layout
					&& (cb = granite_device_request_command_buffer(vi->device, GRANITE_QUEUE_GRAPHICS)) != NULL)
			{
				granite_cb_image_barrier(cb, scanout, vi->prev_image_layout, target_layout,
						layout_to_stage(vi->prev_image_layout), 0,
						layout_to_stage(target_layout), layout_to_access(target_layout));
				vi->prev_image_layout = target_layout;
				granite_device_submit(vi->device, cb, NULL, 0, NULL);
			}
			return scanout;
		}

		unref_image(&vi->prev_scanout_image);
		return NULL;
	}

	vi->last_valid_frame_count = vi->frame_count;

	degenerate = regs.h_res <= 0 || regs.v_res <= 0;

	regs.h_start *= scaling_factor;
	regs.h_start_clamp *= scaling_factor;
	regs.v_start *= scaling_factor;

	regs.h_res *= scaling_factor;
	regs.h_res_clamp *= scaling_factor;
	regs.v_res *= scaling_factor;

	regs.max_x = regs.max_x * scaling_factor + (scaling_factor - 1);
	regs.max_y = regs.max_y * scaling_factor + (scaling_factor - 1);

	for (n = 0; n < VI_MAX_OUTPUT_SCANLINES; n++)
	{
		rdp_vi_horizontal_info_t *line = &lines[n];
		line->h_start *= scaling_factor;
		line->h_start_clamp *= scaling_factor;
		line->h_end_clamp *= scaling_factor;
		line->x_start *= scaling_factor;
		line->y_start *= scaling_factor;
		line->y_base *= scaling_factor;
	}

	/* First we copy data out of VRAM into a texture which we will then
	 * perform our post-AA on. We do this on the async queue so we don't
	 * have to stall async queue on graphics work to deal with WAR hazards.
	 * After the copy, we can immediately begin rendering new frames while
	 * we do post in parallel. */
	if (!degenerate)
	{
		vram_image = vram_fetch_stage(vi, &regs, scaling_factor);
		/* (out of memory: nothing to show) */
		if (!vram_image)
			degenerate = true;
	}

	cb = granite_device_request_command_buffer(vi->device, GRANITE_QUEUE_GRAPHICS);
	if (!cb)
	{
		unref_image(&vram_image);
		unref_image(&vi->prev_scanout_image);
		return NULL;
	}

	/* In the first pass, we need to read from VRAM and apply the fetch
	 * filter. This is either the AA filter if coverage < 7, or the dither
	 * reconstruction filter if coverage == 7 and enabled. Following that,
	 * post-AA filter, we have the divot filter. In this filter, we need to
	 * find the median value of three horizontal pixels, post AA if any of
	 * them have coverage < 7. Finally, we lerp the result based on x_add
	 * and y_add, and then, apply gamma/dither on top as desired.
	 *
	 * AA -> divot could probably be done with compute and shared memory,
	 * but ideally this is done in fragment shaders in this implementation
	 * so that we can run higher-priority compute shading workload async in
	 * the async queue. We also get to take advantage of framebuffer
	 * compression FWIW. */
	if (!degenerate)
		aa_image = aa_fetch_stage(vi, cb, vram_image, &regs, scaling_factor);

	/* Divot pass */
	if (divot && !degenerate && aa_image)
	{
		divot_image = divot_stage(vi, cb, aa_image, &regs, scaling_factor);
		unref_image(&aa_image);
	}
	else
	{
		divot_image = aa_image;
		aa_image = NULL;
	}

	/* Scale pass */
	is_final_pass = !downscale_steps || scaling_factor <= 1;
	serrate = (regs.status & VI_CONTROL_SERRATE_BIT) != 0;

	/* The downscales and the deinterlace after the scale pass are folded
	 * into it, so their images are never written and read back; the blend
	 * of the previous frame (weave) with them, reading it where each scale
	 * pixel ends up, rather than at the scale pass's own coordinates in the
	 * downscaled frame. (A frame is only blended or deinterlaced, never
	 * both, here.) */
	memset(&fold, 0, sizeof(fold));
	{
		unsigned f = (unsigned)scaling_factor, steps = downscale_steps;
		while (f > 1 && steps)
		{
			f /= 2;
			steps--;
			fold.downscale_steps++;
		}
	}
	/* (the field is deinterlaced only after a downscale: without one, the
	 * frame is the field's height) */
	fold.deinterlace = fold.downscale_steps && serrate && options->upscale_deinterlacing;
	fold.field_select = regs.v_current_line == 0;
	can_fold = vi->vi_scale_fold && !vi->fold_disabled && !degenerate && divot_image &&
		!(options->blend_previous_frame && fold.deinterlace) &&
		fold.downscale_steps > 0 && fold.downscale_steps <= 3;

	scale_image = scale_stage(vi, cb, divot_image, &regs, horizontal, scaling_factor, degenerate, options,
			can_fold ? &fold : NULL);
	if (can_fold)
		is_final_pass = true;

	if (!is_final_pass && scale_image)
	{
		granite_cb_image_barrier(cb, scale_image, src_layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				layout_to_stage(src_layout), layout_to_access(src_layout),
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);

		is_final_pass = !serrate || !options->upscale_deinterlacing;

		next = downscale_stage(vi, cb, scale_image, scaling_factor, downscale_steps);
		granite_image_unref(scale_image);
		scale_image = next;

		src_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	}

	if (!is_final_pass && scale_image)
	{
		const int field_scale = scaling_factor >> downscale_steps;

		granite_cb_image_barrier(cb, scale_image, src_layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				layout_to_stage(src_layout), layout_to_access(src_layout),
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);

		next = upscale_deinterlace(vi, cb, scale_image, field_scale > 1 ? field_scale : 1,
				regs.v_current_line == 0);
		granite_image_unref(scale_image);
		scale_image = next;
		src_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	}

	if (scale_image)
		granite_cb_image_barrier(cb, scale_image, src_layout, target_layout,
				layout_to_stage(src_layout), layout_to_access(src_layout),
				layout_to_stage(target_layout), layout_to_access(target_layout));

	vi->prev_image_layout = target_layout;
	unref_image(&vi->prev_scanout_image);
	if (scale_image)
		granite_image_ref(scale_image);
	vi->prev_scanout_image = scale_image;

	granite_device_submit(vi->device, cb, NULL, 0, NULL);
	unref_image(&vram_image);
	unref_image(&divot_image);
	vi->frame_count++;
	return scale_image;
}
