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

#ifndef RDP_VIDEO_INTERFACE_H
#define RDP_VIDEO_INTERFACE_H

#include <stddef.h>
#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>
#include <retro_inline.h>
#include "device.h"
#include "shader_bank.h"
#include "temporal_upscaler.h"
#include "upscaler_pass.h"
#include "vi_image_pool.h"
#include "vi_registers.h"

RETRO_BEGIN_DECLS

/* How a frame is scanned out. rdp_vi_scanout_options_init gives the
 * defaults. */
typedef struct rdp_vi_scanout_options
{
	/* Simple (obsolete) crop method. If crop_rect.enable is false, this
	 * crops top / bottom with number of pixels (doubled if interlace),
	 * and left / right are cropped in an aspect preserving way.
	 * If crop_rect.enable is true, this is ignored and the crop_rect
	 * struct is used instead. Crop pixels are adjusted for upscaling,
	 * pixels are assumed to be specified for the original resolution. */
	unsigned crop_overscan_pixels;

	struct
	{
		unsigned left;
		unsigned right;
		unsigned top;    /* doubled if interlace */
		unsigned bottom; /* doubled if interlace */
		bool enable;
	} crop_rect;

	unsigned downscale_steps;

	/* Works around certain game bugs. Considered a hack if enabled. */
	bool persist_frame_on_invalid_input;

	/* To be equivalent to reference behavior where pixels persist for
	 * an extra frame. Not hardware accurate, but needed for weave
	 * interlace mode. */
	bool blend_previous_frame;

	/* Upscale deinterlacing deinterlaces by upscaling in Y, with an Y
	 * coordinate offset matching the field. If disabled, weave
	 * interlacing is used. Weave deinterlacing should *not* be used,
	 * except to run test suite! (Default: on.) */
	bool upscale_deinterlacing;

	/* (default: all on) */
	struct
	{
		bool aa;
		bool scale;
		bool serrate;
		bool dither_filter;
		bool divot_filter;
		bool gamma_dither;
	} vi;

	/* For a temporal upscaler: the scanout is the framebuffer as drawn,
	 * at the upscaling factor, without the VI's filters, scaling or
	 * crop; the frame's motion vectors, depth and jitter come with it.
	 * (Default: off, a jitter phase of 8.) */
	struct
	{
		bool enable;
		unsigned jitter_phase;
		bool reset;
	} upscaler;
} rdp_vi_scanout_options_t;

void rdp_vi_scanout_options_init(rdp_vi_scanout_options_t *options);

/* Where the VI reads an upscaled frame from: the renderer's upscaled
 * RDRAM. Only asked when the scaling factor is above 1. */
typedef struct rdp_vi_upscaled_source
{
	void *user;
	/* Records, into @cb, bringing @pixels pixels of 1 << @pixel_size_log2
	 * bytes from byte address @addr up to date */
	void (*update)(void *user, granite_command_buffer_t *cb, unsigned addr, unsigned pixels,
			unsigned pixel_size_log2);
	const granite_buffer_t *(*rdram)(void *user);
	const granite_buffer_t *(*hidden_rdram)(void *user);
} rdp_vi_upscaled_source_t;

/* The per-line horizontal state the scale pass reads, decoded straight
 * into a persistently mapped buffer; one is reused once its frame
 * context comes round again, as the images are. */
typedef struct rdp_vi_horizontal_buffer
{
	granite_buffer_t *buffer;
	granite_buffer_view_t *view;
	uint64_t free_serial;
} rdp_vi_horizontal_buffer_t;

#define RDP_VI_MAX_HORIZONTAL_BUFFERS 32

/* The video interface: what the VI registers show of RDRAM, as an image */
typedef struct rdp_vi
{
	granite_device_t *device;
	rdp_vi_upscaled_source_t upscaled;
	uint32_t registers[RDP_VI_REGISTER_COUNT];

	/* (not the VI's) */
	const granite_buffer_t *rdram;
	const granite_buffer_t *hidden_rdram;
	size_t rdram_offset;
	size_t rdram_size;

	granite_buffer_t *gamma_lut;
	granite_buffer_view_t *gamma_lut_view;
	const rdp_shader_bank_t *shader_bank;
	/* vi_scale with the passes after it folded in */
	granite_shader_t *vi_scale_fold;
	/* runs the downscale and deinterlace as passes of their own, as
	 * they were: to compare against */
	bool fold_disabled;
	/* what it reads as the previous frame when there is none */
	granite_image_t *no_previous_frame;
	bool previous_frame_blank;

	/* Intermediate images, reused instead of created every frame. */
	rdp_vi_image_pool_t image_pool;
	rdp_vi_horizontal_buffer_t horizontal_buffers[RDP_VI_MAX_HORIZONTAL_BUFFERS];
	uint64_t frame_serial;

	/* Frame state. */
	uint32_t frame_count;
	uint32_t last_valid_frame_count;
	granite_image_t *prev_scanout_image;
	VkImageLayout prev_image_layout;
	granite_image_t *prev_upscaler_image;
	rdp_upscaler_framebuffer_t prev_upscaler_fb;
} rdp_vi_t;

/* @vi: zeroed, or freed. Makes the gamma table; @device outlives the VI. */
void rdp_vi_set_device(rdp_vi_t *vi, granite_device_t *device);
/* Gives back everything the VI holds. Before the device goes. */
void rdp_vi_free(rdp_vi_t *vi);

void rdp_vi_set_rdram(rdp_vi_t *vi, const granite_buffer_t *rdram, size_t offset, size_t size);
/* After rdp_vi_set_device. @bank outlives the VI. */
void rdp_vi_set_shader_bank(rdp_vi_t *vi, const rdp_shader_bank_t *bank);

static INLINE void rdp_vi_set_register(rdp_vi_t *vi, enum rdp_vi_register reg, uint32_t value)
{
	vi->registers[reg] = value;
}

/* After each next frame context of the device */
void rdp_vi_begin_frame_context(rdp_vi_t *vi);

/* Hands @add every pipeline a scanout can need. The thread that processes
 * commands, once, at load. */
void rdp_vi_queue_known_pipelines(rdp_vi_t *vi, unsigned upscaling, bool with_upscaler,
		rdp_known_pipeline_fn add, void *ctx);

/* The frame, in @target_layout: a reference of the caller's, or NULL
 * when the VI shows nothing. */
granite_image_t *rdp_vi_scanout(rdp_vi_t *vi, VkImageLayout target_layout,
		const rdp_vi_scanout_options_t *options, unsigned scaling_factor);

/* rdp_vi_scanout_options_t.upscaler's scanout, submitted on the RDP's
 * queue and waited for by the next graphics submission, ending in
 * SHADER_READ_ONLY_OPTIMAL: a reference of the caller's, or NULL when
 * the VI shows nothing. @repeat: the frame before, shown again. */
granite_image_t *rdp_vi_upscaler_scanout(rdp_vi_t *vi, unsigned scaling_factor,
		rdp_upscaler_framebuffer_t *fb, bool *repeat);

RETRO_END_DECLS

#endif
