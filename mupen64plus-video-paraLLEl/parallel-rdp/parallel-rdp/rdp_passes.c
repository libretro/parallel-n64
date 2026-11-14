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


#include <string.h>
#include "rdp_passes.h"

#define WORKGROUP_SIZE 64u

/* FBFormat */
enum
{
	FB_I4 = 0,
	FB_I8,
	FB_RGBA5551,
	FB_IA88,
	FB_RGBA8888
};

static uint32_t log2_of(uint32_t v)
{
	uint32_t n = 0;
	while (v > 1)
	{
		v >>= 1;
		n++;
	}
	return n;
}

static void storage(granite_command_buffer_t *cb, unsigned set, unsigned binding, const granite_buffer_t *buffer)
{
	granite_cb_set_storage_buffer(cb, set, binding, buffer, 0, buffer->info.size);
}

static void storage_range(granite_command_buffer_t *cb, unsigned set, unsigned binding,
		const granite_buffer_t *buffer, VkDeviceSize offset, VkDeviceSize range)
{
	granite_cb_set_storage_buffer(cb, set, binding, buffer, offset, range);
}

static void stream(const rdp_pass_resources_t *r, granite_command_buffer_t *cb, unsigned set, unsigned binding,
		enum rdp_stream_buffer which)
{
	storage(cb, set, binding, r->streams->buffers[which]);
}

void rdp_pass_fb_info(uint32_t fmt, rdp_global_fb_info_t *info)
{
	switch (fmt)
	{
		case FB_I4:
			info->fb_size = 0;
			info->dx_mask = 0;
			info->dx_shift = 0;
			break;
		case FB_I8:
			info->fb_size = 1;
			info->dx_mask = ~7u;
			info->dx_shift = 3;
			break;
		case FB_RGBA5551:
		case FB_IA88:
			info->fb_size = 2;
			info->dx_mask = ~3u;
			info->dx_shift = 2;
			break;
		default:
			info->fb_size = 4;
			info->dx_mask = ~1u;
			info->dx_shift = 1;
			break;
	}
}

unsigned rdp_pass_pixel_size_log2(uint32_t fmt)
{
	switch (fmt)
	{
		case FB_RGBA8888:
			return 2;
		case FB_RGBA5551:
		case FB_IA88:
			return 1;
		default:
			return 0;
	}
}

/* The framebuffer's info, in the frame's constant data at set 2 */
static void global_fb_info(granite_command_buffer_t *cb, const rdp_pass_target_t *t)
{
	rdp_global_fb_info_t info;
	rdp_pass_fb_info(t->fmt, &info);
	info.base_primitive_index = t->base_primitive_index;
	memcpy(granite_cb_allocate_constant_data(cb, 2, 0, sizeof(info)), &info, sizeof(info));
}

void rdp_pass_tmem_update(const rdp_pass_resources_t *r, granite_command_buffer_t *cb,
		const rdp_upload_info_t *uploads, uint32_t count)
{
	storage_range(cb, 0, 0, r->rdram, r->rdram_offset, r->rdram_size);
	storage(cb, 0, 1, r->tmem);
	storage(cb, 0, 2, r->buffers->tmem_instances);

	memcpy(granite_cb_allocate_constant_data(cb, 1, 0, count * sizeof(*uploads)), uploads,
			count * sizeof(*uploads));

	granite_cb_set_program(cb, r->setup->bank->tmem_update);
	granite_cb_push_constants(cb, &count, 0, sizeof(count));
	granite_cb_set_spec_constant_mask(cb, 1);
	granite_cb_set_spec_constant(cb, 0, WORKGROUP_SIZE);
	granite_cb_dispatch(cb, 2048 / WORKGROUP_SIZE, 1, 1);
}

void rdp_pass_span_setup(const rdp_pass_resources_t *r, granite_command_buffer_t *cb, bool upscale,
		uint32_t num_jobs)
{
	stream(r, cb, 0, 0, RDP_STREAM_TRIANGLE_SETUP);
	stream(r, cb, 0, 1, RDP_STREAM_ATTRIBUTE_SETUP);
	stream(r, cb, 0, 2, RDP_STREAM_SCISSOR_SETUP);
	storage(cb, 0, 3, r->buffers->span_setups);
	granite_cb_set_buffer_view(cb, 1, 0, r->streams->span_info_jobs_view);

	rdp_pipeline_span_setup(r->setup, cb, upscale);
	granite_cb_dispatch(cb, num_jobs, 1, 1);
}

void rdp_pass_clear_indirect(const rdp_pass_resources_t *r, granite_command_buffer_t *cb)
{
	granite_cb_set_program(cb, r->setup->bank->clear_indirect_buffer);
	storage(cb, 0, 0, r->buffers->indirect_dispatch);
	granite_cb_set_spec_constant_mask(cb, 1);
	granite_cb_set_spec_constant(cb, 0, WORKGROUP_SIZE);
	granite_cb_dispatch(cb, RDP_CAPS_MAX_STATIC_RASTER_STATES / WORKGROUP_SIZE, 1, 1);
}

void rdp_pass_tile_binning(const rdp_pass_resources_t *r, granite_command_buffer_t *cb,
		const rdp_pass_target_t *t, bool upscale)
{
	const rdp_renderer_buffers_t *b = r->buffers;
	const unsigned scale = upscale ? r->setup->caps->upscaling : 1;
	struct
	{
		uint32_t width, height;
		uint32_t num_primitives;
	} push;
	unsigned meta_tiles_x = 8, meta_tiles_y, tiles_x, tiles_y;
	uint32_t subgroup_size;

	stream(r, cb, 0, 0, RDP_STREAM_TRIANGLE_SETUP);
	stream(r, cb, 0, 1, RDP_STREAM_SCISSOR_SETUP);
	stream(r, cb, 0, 2, RDP_STREAM_STATE_INDICES);
	storage(cb, 0, 3, b->tile_binning);
	storage(cb, 0, 4, b->tile_binning_coarse);
	if (!r->setup->caps->ubershader)
	{
		storage(cb, 0, 5, b->per_tile_offsets);
		storage(cb, 0, 6, b->indirect_dispatch);
		storage(cb, 0, 7, b->tile_work_list);
	}

	push.width = t->width * scale;
	push.height = t->height * scale;
	push.num_primitives = t->num_primitives;
	granite_cb_push_constants(cb, &push, 0, sizeof(push));

	subgroup_size = rdp_pipeline_tile_binning(r->setup, cb, upscale);

	meta_tiles_y = subgroup_size / meta_tiles_x;
	tiles_x = (push.width + RDP_CAPS_TILE_WIDTH - 1) / RDP_CAPS_TILE_WIDTH;
	tiles_y = (push.height + RDP_CAPS_TILE_HEIGHT - 1) / RDP_CAPS_TILE_HEIGHT;
	granite_cb_dispatch(cb, (push.num_primitives + 31) / 32, (tiles_x + meta_tiles_x - 1) / meta_tiles_x,
			(tiles_y + meta_tiles_y - 1) / meta_tiles_y);

	rdp_pipeline_end_tile_binning(cb);
}

void rdp_pass_rasterize(const rdp_pass_resources_t *r, granite_command_buffer_t *cb,
		const rdp_pass_target_t *t, const granite_buffer_t *tmem, bool upscale,
		const rdp_static_state_t *states, uint32_t count)
{
	const rdp_renderer_caps_t *caps = r->setup->caps;
	const rdp_renderer_buffers_t *b = r->buffers;
	const VkDeviceSize slice = (VkDeviceSize)RDP_CAPS_TILE_RASTER_WORK_SIZE * caps->max_num_tile_instances;
	const uint32_t scale_log2_bit = (upscale ? log2_of(caps->upscaling) : 0u) << RASTERIZATION_UPSCALING_LOG2_BIT_OFFSET;
	uint32_t i;

	stream(r, cb, 0, 0, RDP_STREAM_TRIANGLE_SETUP);
	stream(r, cb, 0, 1, RDP_STREAM_ATTRIBUTE_SETUP);
	stream(r, cb, 0, 2, RDP_STREAM_DERIVED_SETUP);
	stream(r, cb, 0, 3, RDP_STREAM_STATIC_RASTER_STATE);
	stream(r, cb, 0, 4, RDP_STREAM_STATE_INDICES);
	stream(r, cb, 0, 5, RDP_STREAM_SPAN_INFO_OFFSETS);
	storage(cb, 0, 6, b->span_setups);
	storage(cb, 0, 7, tmem);
	stream(r, cb, 0, 8, RDP_STREAM_TILE_INFO_STATE);
	storage(cb, 0, 9, b->per_tile_shaded_color);
	storage(cb, 0, 10, b->per_tile_shaded_depth);
	storage(cb, 0, 11, b->per_tile_shaded_alpha);
	storage(cb, 0, 12, b->per_tile_shaded_coverage);

	global_fb_info(cb, t);

	granite_cb_set_program(cb, r->setup->bank->rasterizer);
	granite_cb_set_spec_constant(cb, 0, RDP_CAPS_TILE_WIDTH);
	granite_cb_set_spec_constant(cb, 1, RDP_CAPS_TILE_HEIGHT);

	for (i = 0; i < count; i++)
	{
		const rdp_static_state_t *state = &states[i];
		uint32_t word;

		storage_range(cb, 1, 0, b->tile_work_list, i * slice, slice);

		granite_cb_set_spec_constant(cb, 2, state->flags | RASTERIZATION_USE_SPECIALIZATION_CONSTANT_BIT |
				scale_log2_bit);
		memcpy(&word, &state->combiner[0].rgb, sizeof(word));
		granite_cb_set_spec_constant(cb, 3, word);
		memcpy(&word, &state->combiner[0].alpha, sizeof(word));
		granite_cb_set_spec_constant(cb, 4, word);
		memcpy(&word, &state->combiner[1].rgb, sizeof(word));
		granite_cb_set_spec_constant(cb, 5, word);
		memcpy(&word, &state->combiner[1].alpha, sizeof(word));
		granite_cb_set_spec_constant(cb, 6, word);
		granite_cb_set_spec_constant(cb, 7, state->dither | (state->texture_size << 8u) |
				(state->texture_fmt << 16u));
		granite_cb_set_spec_constant_mask(cb, 0xff);

		/* Not built yet: the generic variant meanwhile, the specialised
		 * one asked of the pipeline thread */
		if (!caps->force_sync && !granite_cb_flush_state(cb, false))
		{
			rdp_pipeline_builder_miss(r->pipelines, cb);
			granite_cb_set_spec_constant_mask(cb, 7);
			granite_cb_set_spec_constant(cb, 2, scale_log2_bit);
		}

		granite_cb_dispatch_indirect(cb, b->indirect_dispatch, 4 * sizeof(uint32_t) * i);
	}
}

void rdp_pass_depth_blend(const rdp_pass_resources_t *r, granite_command_buffer_t *cb,
		const rdp_pass_target_t *t, const granite_buffer_t *tmem, bool upscaled, bool force_write_mask)
{
	const rdp_renderer_caps_t *caps = r->setup->caps;
	const rdp_renderer_buffers_t *b = r->buffers;
	const unsigned scale = upscaled ? caps->upscaling : 1;
	const unsigned groups = (t->num_primitives + 31) / 32;
	struct
	{
		uint32_t addr_index;
		uint32_t depth_addr_index;
		uint32_t fb_width, fb_height;
		uint32_t group_mask;
	} push;

	rdp_pipeline_depth_blend(r->setup, cb, t->fmt, t->addr == t->depth_addr, upscaled, force_write_mask);

	if (upscaled)
		storage(cb, 0, 0, b->upscaling_multisampled_rdram);
	else
		storage_range(cb, 0, 0, r->rdram, r->rdram_offset, r->rdram_size * (r->setup->host_coherent ? 1 : 2));
	storage(cb, 0, 1, upscaled ? b->upscaling_multisampled_hidden_rdram : r->hidden_rdram);
	storage(cb, 0, 2, tmem);
	if (!caps->ubershader)
	{
		storage(cb, 0, 3, b->per_tile_shaded_color);
		storage(cb, 0, 4, b->per_tile_shaded_depth);
		storage(cb, 0, 5, b->per_tile_shaded_alpha);
		storage(cb, 0, 6, b->per_tile_shaded_coverage);
		storage(cb, 0, 7, b->per_tile_offsets);
	}

	stream(r, cb, 1, 0, RDP_STREAM_TRIANGLE_SETUP);
	stream(r, cb, 1, 1, RDP_STREAM_ATTRIBUTE_SETUP);
	stream(r, cb, 1, 2, RDP_STREAM_DERIVED_SETUP);
	stream(r, cb, 1, 3, RDP_STREAM_SCISSOR_SETUP);
	stream(r, cb, 1, 4, RDP_STREAM_STATIC_RASTER_STATE);
	stream(r, cb, 1, 5, RDP_STREAM_DEPTH_BLEND_STATE);
	stream(r, cb, 1, 6, RDP_STREAM_STATE_INDICES);
	stream(r, cb, 1, 7, RDP_STREAM_TILE_INFO_STATE);
	storage(cb, 1, 8, b->span_setups);
	stream(r, cb, 1, 9, RDP_STREAM_SPAN_INFO_OFFSETS);
	granite_cb_set_buffer_view(cb, 1, 10, b->blender_divider);
	storage(cb, 1, 11, b->tile_binning);
	storage(cb, 1, 12, b->tile_binning_coarse);

	global_fb_info(cb, t);

	push.fb_width = t->width * scale;
	push.fb_height = t->height * scale;
	push.addr_index = t->addr >> rdp_pass_pixel_size_log2(t->fmt);
	push.depth_addr_index = t->depth_addr >> 1;
	/* (a primitive per bit, a bit per group of 32) */
	push.group_mask = groups >= 32 ? ~0u : (1u << groups) - 1;
	granite_cb_push_constants(cb, &push, 0, sizeof(push));

	granite_cb_dispatch(cb, (push.fb_width + 7) / 8, (push.fb_height + 7) / 8, 1);
}

void rdp_pass_update_upscaled_domain(const rdp_pass_resources_t *r, granite_command_buffer_t *cb,
		enum rdp_resolve_stage stage, unsigned addr, unsigned depth_addr,
		unsigned width, unsigned height, unsigned pixel_size_log2)
{
	const rdp_renderer_buffers_t *b = r->buffers;
	unsigned num_pixels = width * height;
	uint32_t groups_x, groups_y;
	struct
	{
		uint32_t pixels;
		uint32_t fb_addr, fb_depth_addr;
		uint32_t width, height;
	} push;

	if (stage != RDP_RESOLVE_SSAA)
	{
		/* Whole words always, so there are no odd swizzles, and memory is
		 * accessed linearly with gl_GlobalInvocationID.x */
		unsigned align_pixels = 4u >> pixel_size_log2;
		addr &= ~3u;
		depth_addr &= ~3u;
		num_pixels = (num_pixels + align_pixels - 1u) & ~(align_pixels - 1u);
	}

	storage_range(cb, 0, 0, r->rdram, r->rdram_offset,
			(stage == RDP_RESOLVE_SSAA && !r->setup->host_coherent ? 2 : 1) * r->rdram_size);
	storage(cb, 0, 1, r->hidden_rdram);
	storage(cb, 0, 2, b->upscaling_reference_rdram);
	storage(cb, 0, 3, b->upscaling_multisampled_rdram);
	storage(cb, 0, 4, b->upscaling_multisampled_hidden_rdram);

	rdp_pipeline_upscaled_domain(r->setup, cb, stage, pixel_size_log2, addr == depth_addr);

	if (stage == RDP_RESOLVE_SSAA)
	{
		groups_x = (width + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
		groups_y = height;
	}
	else
	{
		groups_x = (num_pixels + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
		groups_y = 1;
	}

	push.pixels = num_pixels;
	push.fb_addr = addr >> pixel_size_log2;
	push.fb_depth_addr = depth_addr >> 1;
	push.width = width;
	push.height = height;
	granite_cb_push_constants(cb, &push, 0, sizeof(push));

	granite_cb_dispatch(cb, groups_x, groups_y, 1);
}

void rdp_pass_clear_super_sample_write_mask(const rdp_pass_resources_t *r, granite_command_buffer_t *cb,
		unsigned width, unsigned height)
{
	const rdp_renderer_caps_t *caps = r->setup->caps;
	/* A 4x4 block of pixels' write masks to a word: one depth_blend
	 * workgroup targets a single word in all cases. */
	const unsigned words = ((width + 3) / 4) * ((height + 3) / 4);

	granite_cb_set_program(cb, r->setup->bank->clear_super_sampled_write_mask);
	storage_range(cb, 0, 0, r->buffers->upscaling_multisampled_rdram,
			r->rdram_size * caps->upscaling * caps->upscaling, 2 * RDP_CAPS_MAX_WIDTH * RDP_CAPS_MAX_HEIGHT / 8);
	granite_cb_set_spec_constant_mask(cb, 1);
	granite_cb_set_spec_constant(cb, 0, WORKGROUP_SIZE);
	granite_cb_dispatch(cb, (words + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE, 1, 1);
	granite_cb_set_spec_constant_mask(cb, 0);
}

/* ---- a flush's passes, in order ---- */

static void barrier(granite_command_buffer_t *cb, VkPipelineStageFlags src_stages, VkAccessFlags src_access,
		VkPipelineStageFlags dst_stages, VkAccessFlags dst_access)
{
	VkMemoryBarrier global;
	memset(&global, 0, sizeof(global));
	global.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	global.srcAccessMask = src_access;
	global.dstAccessMask = dst_access;
	granite_cb_barrier(cb, src_stages, dst_stages, 1, &global, 0, NULL, 0, NULL);
}

/* After the passes the rasterizer's indirect dispatches come from */
static void barrier_before_raster(const rdp_renderer_caps_t *caps, granite_command_buffer_t *cb)
{
	barrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | (!caps->ubershader ? VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT : 0),
			VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
			(!caps->ubershader ? VK_ACCESS_INDIRECT_COMMAND_READ_BIT : 0));
}

static void barrier_read(granite_command_buffer_t *cb)
{
	barrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
}

static void barrier_read_write(granite_command_buffer_t *cb)
{
	barrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
}

/* The TMEM the primitives read: this flush's loads, or as it was */
static const granite_buffer_t *flush_tmem(const rdp_pass_resources_t *r, const rdp_pass_flush_t *f)
{
	return f->num_uploads ? r->buffers->tmem_instances : r->tmem;
}

static void update_upscaled_domain(const rdp_pass_resources_t *r, granite_command_buffer_t *cb,
		const rdp_pass_target_t *t, enum rdp_resolve_stage stage)
{
	rdp_pass_update_upscaled_domain(r, cb, stage, t->addr, t->depth_addr, t->width, t->height,
			rdp_pass_pixel_size_log2(t->fmt));
}

bool rdp_pass_flush_draws(const rdp_pass_flush_t *f)
{
	return f->target.width != 0 && f->target.height != 0 && f->num_span_jobs != 0;
}

bool rdp_pass_flush_upscaled(const rdp_renderer_caps_t *caps, const rdp_pass_flush_t *f)
{
	if (caps->super_sample_readback)
		return true;
	/* No point upscaling off-screen passes: ideally only the last pass,
	 * to the screen. By heuristic only 16 and 32-bit framebuffers of at
	 * least 256 pixels across are. */
	return caps->upscaling > 1 && rdp_pass_flush_draws(f) &&
			(f->target.fmt == FB_RGBA5551 || f->target.fmt == FB_RGBA8888) && f->target.width >= 256;
}

void rdp_pass_record_native(const rdp_pass_resources_t *r, granite_command_buffer_t *cb, const rdp_pass_flush_t *f)
{
	const rdp_renderer_caps_t *caps = r->setup->caps;
	const bool draws = rdp_pass_flush_draws(f);
	const rdp_pass_target_t *t = &f->target;

	if (!draws && !f->num_uploads)
		return;

	/* Three dispatches side by side: span setup and the TMEM update are
	 * light; binning should dominate unless the work is trivial. */
	if (draws)
	{
		rdp_pass_span_setup(r, cb, false, f->num_span_jobs);
		rdp_pass_tile_binning(r, cb, t, false);
		if (caps->upscaling > 1)
			update_upscaled_domain(r, cb, t, RDP_RESOLVE_PRE);
	}
	if (f->num_uploads)
		rdp_pass_tmem_update(r, cb, f->uploads, f->num_uploads);

	barrier_before_raster(caps, cb);

	if (draws && !caps->ubershader)
	{
		rdp_pass_rasterize(r, cb, t, flush_tmem(r, f), false, f->states, f->num_states);
		barrier_read(cb);
	}
	if (draws)
		rdp_pass_depth_blend(r, cb, t, flush_tmem(r, f), false, false);
	if (!caps->ubershader)
		rdp_pass_clear_indirect(r, cb);

	if (rdp_pass_flush_upscaled(caps, f))
	{
		/* (the reference could be written by the passes themselves, to
		 * two buffers; this composes better for now) */
		barrier_read_write(cb);
		update_upscaled_domain(r, cb, t, RDP_RESOLVE_POST);
	}
}

void rdp_pass_record_upscaled(const rdp_pass_resources_t *r, granite_command_buffer_t *cb, const rdp_pass_flush_t *f)
{
	const rdp_renderer_caps_t *caps = r->setup->caps;
	const rdp_pass_target_t *t = &f->target;

	rdp_pass_span_setup(r, cb, true, f->num_span_jobs);
	rdp_pass_tile_binning(r, cb, t, true);
	if (caps->super_sample_readback)
	{
		update_upscaled_domain(r, cb, t, RDP_RESOLVE_PRE);
		rdp_pass_clear_super_sample_write_mask(r, cb, t->width, t->height);
		if (f->num_uploads)
			rdp_pass_tmem_update(r, cb, f->uploads, f->num_uploads);
	}

	barrier_before_raster(caps, cb);

	if (!caps->ubershader)
	{
		rdp_pass_rasterize(r, cb, t, flush_tmem(r, f), true, f->states, f->num_states);
		barrier_read(cb);
	}
	rdp_pass_depth_blend(r, cb, t, flush_tmem(r, f), true, caps->super_sample_readback);
	if (!caps->ubershader)
		rdp_pass_clear_indirect(r, cb);

	if (caps->super_sample_readback)
	{
		barrier_read_write(cb);
		update_upscaled_domain(r, cb, t, RDP_RESOLVE_SSAA);
	}
}

void rdp_pass_record_end(granite_command_buffer_t *cb)
{
	barrier_read_write(cb);
}
