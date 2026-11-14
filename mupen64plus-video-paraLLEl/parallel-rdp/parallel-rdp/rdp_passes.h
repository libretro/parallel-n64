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


#ifndef RDP_PASSES_H
#define RDP_PASSES_H

/* The compute passes of a flush, recorded into a command buffer: the
 * TMEM update, span setup, tile binning, rasterization, depth and
 * blending, the upscaled domain's updates and the clears they need.
 * C89. */

#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>
#include "command_buffer.h"
#include "rdp_pipelines.h"
#include "renderer_buffers.h"
#include "rdp_state.h"
#include "tmem_load.h"

RETRO_BEGIN_DECLS

/* What the shaders are told of the framebuffer: how pixels snap to the
 * copy pipe's spans (dx_mask, dx_shift) and its pixel size (fb_size, in
 * bytes; 0 for 4 bits) */
typedef struct rdp_global_fb_info
{
	uint32_t dx_shift;
	uint32_t dx_mask;
	uint32_t fb_size;
	uint32_t base_primitive_index;
} rdp_global_fb_info_t;

/* For a framebuffer of format @fmt (FBFormat): I4 to RGBA8888 */
void rdp_pass_fb_info(uint32_t fmt, rdp_global_fb_info_t *info);

/* log2 of a pixel's size in bytes for @fmt (0 for I4 and I8), which is
 * also how far its address is shifted to index pixels */
unsigned rdp_pass_pixel_size_log2(uint32_t fmt);

/* What a flush's passes bind */
typedef struct rdp_pass_resources
{
	const rdp_pipeline_setup_t *setup;
	rdp_pipeline_builder_t *pipelines;
	const rdp_renderer_buffers_t *buffers;
	/* the GPU's streams of this flush */
	const rdp_stream_set_t *streams;
	const granite_buffer_t *rdram;
	VkDeviceSize rdram_offset;
	VkDeviceSize rdram_size;
	const granite_buffer_t *hidden_rdram;
	const granite_buffer_t *tmem;
} rdp_pass_resources_t;

/* The framebuffer a flush draws, and its primitives */
typedef struct rdp_pass_target
{
	uint32_t addr;
	uint32_t depth_addr;
	uint32_t width;
	/* as far down as the primitives reach */
	uint32_t height;
	uint32_t fmt; /* FBFormat */
	/* the flush's first primitive among the frame's, and how many */
	uint32_t base_primitive_index;
	uint32_t num_primitives;
} rdp_pass_target_t;

/* The @count tile loads of @uploads into the TMEM instances */
void rdp_pass_tmem_update(const rdp_pass_resources_t *r, granite_command_buffer_t *cb,
		const rdp_upload_info_t *uploads, uint32_t count);

/* @num_jobs span interpolation jobs */
void rdp_pass_span_setup(const rdp_pass_resources_t *r, granite_command_buffer_t *cb, bool upscale,
		uint32_t num_jobs);

/* Zeroes the indirect dispatches of every static raster state */
void rdp_pass_clear_indirect(const rdp_pass_resources_t *r, granite_command_buffer_t *cb);

/* Bins the primitives to tiles: at the upscaled size when @upscale */
void rdp_pass_tile_binning(const rdp_pass_resources_t *r, granite_command_buffer_t *cb,
		const rdp_pass_target_t *t, bool upscale);

/* The split rasterizer: a dispatch per static raster state of @states
 * (@count), each with its own variant, or the generic one while its
 * variant is built */
void rdp_pass_rasterize(const rdp_pass_resources_t *r, granite_command_buffer_t *cb,
		const rdp_pass_target_t *t, const granite_buffer_t *tmem, bool upscale,
		const rdp_static_state_t *states, uint32_t count);

/* Depth testing and blending into RDRAM, or into the upscaled RDRAM
 * when @upscaled; the ubershader shades here too */
void rdp_pass_depth_blend(const rdp_pass_resources_t *r, granite_command_buffer_t *cb,
		const rdp_pass_target_t *t, const granite_buffer_t *tmem, bool upscaled, bool force_write_mask);

/* The upscaled domain's update for @width x @height pixels of
 * 2^@pixel_size_log2 bytes at @addr, its depth at @depth_addr */
void rdp_pass_update_upscaled_domain(const rdp_pass_resources_t *r, granite_command_buffer_t *cb,
		enum rdp_resolve_stage stage, unsigned addr, unsigned depth_addr,
		unsigned width, unsigned height, unsigned pixel_size_log2);

/* Clears the super-sampled write mask of @width x @height pixels */
void rdp_pass_clear_super_sample_write_mask(const rdp_pass_resources_t *r, granite_command_buffer_t *cb,
		unsigned width, unsigned height);

/* ---- a flush's passes, in order ---- */
typedef struct rdp_pass_flush
{
	rdp_pass_target_t target;
	/* the tile loads, to run before the primitives that read them */
	const rdp_upload_info_t *uploads;
	uint32_t num_uploads;
	uint32_t num_span_jobs;
	const rdp_static_state_t *states;
	uint32_t num_states;
} rdp_pass_flush_t;

/* Whether the flush draws primitives at all (not just tile loads) */
bool rdp_pass_flush_draws(const rdp_pass_flush_t *f);

/* Whether the flush is drawn upscaled too: always with the
 * super-sampled read-back, else only a framebuffer that likely reaches
 * the screen (16 or 32-bit RGBA, at least 256 wide) */
bool rdp_pass_flush_upscaled(const rdp_renderer_caps_t *caps, const rdp_pass_flush_t *f);

/* The flush at native resolution, then the upscaled RDRAM's reference
 * updated from it when it is drawn upscaled too. Never with the
 * super-sampled read-back. */
void rdp_pass_record_native(const rdp_pass_resources_t *r, granite_command_buffer_t *cb, const rdp_pass_flush_t *f);

/* The flush upscaled, and with the super-sampled read-back resolved
 * down to RDRAM */
void rdp_pass_record_upscaled(const rdp_pass_resources_t *r, granite_command_buffer_t *cb, const rdp_pass_flush_t *f);

/* After the flush's passes: what they wrote is there for the next */
void rdp_pass_record_end(granite_command_buffer_t *cb);

RETRO_END_DECLS

#endif
