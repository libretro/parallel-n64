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


#ifndef RDP_RENDERER_H
#define RDP_RENDERER_H

/* The renderer: the RDP's state as the commands set it, the primitives
 * and tile loads queued into flushes, each flush's passes recorded and
 * submitted to the GPU, and the GPU's RDRAM kept in step with the
 * host's where it is not the host's own. C89. */

#include <stddef.h>
#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>
#include <retro_atomic.h>
#include "device.h"
#include "shader_bank.h"
#include "primitive_decode.h"
#include "rdp_state.h"
#include "tmem_load.h"
#include "rdram_coherency.h"
#include "renderer_caps.h"
#include "renderer_buffers.h"
#include "rdp_pipelines.h"
#include "rdp_passes.h"
#include "rdp_queue.h"
#include "rdp_submit.h"
#include "rdp_sync_ring.h"
#include "rdp_timeline.h"
#include "video_interface.h"

RETRO_BEGIN_DECLS

/* A span setup as span_setup.comp writes it, in bytes */
#define RDP_RENDERER_SPAN_SETUP_SIZE 64u

/* The color image's formats, as set color image has them */
enum rdp_fb_format
{
	RDP_FB_I4 = 0,
	RDP_FB_I8 = 1,
	RDP_FB_RGBA5551 = 2,
	RDP_FB_IA88 = 3,
	RDP_FB_RGBA8888 = 4
};

/* What a real RDP would crash on */
enum rdp_validation_error
{
	RDP_VALIDATION_FILL_4BPP = 0,
	RDP_VALIDATION_LOAD_TILE_4BPP,
	RDP_VALIDATION_INVALID_MULTILINE_LOAD_TLUT,
	RDP_VALIDATION_FILL_DEPTH_TEST,
	RDP_VALIDATION_FILL_DEPTH_WRITE,
	RDP_VALIDATION_FILL_IMAGE_READ_ENABLE,
	RDP_VALIDATION_COPY_32BPP
};

/* Reports a command a real RDP would crash on: it is drawn as well as it
 * can be. Called on the thread that processes commands. */
typedef void (*rdp_report_crash_fn)(void *user, enum rdp_validation_error err, const char *msg);

typedef struct rdp_renderer_options
{
	unsigned upscaling_factor;
	bool super_sampled_readback;
	bool super_sampled_readback_dither;
	/* the device has no storageBuffer8BitAccess: the NO_STORAGE_8BIT
	 * shader variants, whose hidden RDRAM is 16 bits per entry */
	bool no_storage_8bit;
	/* nor storageBuffer16BitAccess: the NO_STORAGE_16BIT variants
	 * (no_storage_8bit too) */
	bool no_storage_16bit;
} rdp_renderer_options_t;

/* The framebuffer the primitives are drawn into */
typedef struct rdp_renderer_fb
{
	uint32_t addr;
	uint32_t depth_addr;
	uint32_t width;
	/* enum rdp_fb_format */
	uint32_t fmt;
} rdp_renderer_fb_t;

typedef struct rdp_renderer
{
	granite_device_t *device;
	granite_buffer_t *rdram;
	granite_buffer_t *hidden_rdram;
	granite_buffer_t *tmem;
	size_t rdram_offset;
	size_t rdram_size;
	/* the GPU's RDRAM is the host's own (VK_EXT_external_memory_host);
	 * when not, @incoherent keeps them in step */
	bool is_host_coherent;
	rdp_rdram_coherency_t incoherent;
	const rdp_shader_bank_t *shader_bank;
	/* where the fences are waited for and the GPU's writes copied back */
	rdp_timeline_t *timeline;
	rdp_report_crash_fn report_crash;
	void *report_user;

	rdp_renderer_caps_t caps;
	/* the compute pipelines, built on a thread of their own */
	rdp_pipeline_builder_t pipelines;
	/* the fixed buffers and the upscaled RDRAM */
	rdp_renderer_buffers_t buffers;
	/* whether the indirect dispatch buffer was cleared yet */
	bool indirect_cleared;

	rdp_renderer_fb_t fb;
	rdp_constants_t constants;
	rdp_tile_info_t tiles[8];

	/* the flush being queued */
	rdp_queue_t queue;
	/* the streams of each slot of @sync_ring, and when the GPU is done
	 * with each */
	rdp_stream_instance_t buffer_instances[RDP_SYNC_RING_SIZE];
	rdp_sync_ring_t sync_ring;
	/* the flush's first primitive among those since the last scanout */
	uint32_t base_primitive_index;

	/* what is recorded and not submitted yet, on the async compute queue */
	granite_command_buffer_t *cmd;
	rdp_submit_counts_t submits;
	/* submissions the timeline worker has not seen signalled yet */
	retro_atomic_int_t active_submissions;
	int64_t last_submit_ns;
} rdp_renderer_t;

/* Nothing made. @timeline: where its fences go (started or not). */
void rdp_renderer_init(rdp_renderer_t *r, rdp_timeline_t *timeline);
void rdp_renderer_free(rdp_renderer_t *r);

void rdp_renderer_set_device(rdp_renderer_t *r, granite_device_t *device);
void rdp_renderer_set_shader_bank(rdp_renderer_t *r, const rdp_shader_bank_t *bank);
/* NULL: none */
void rdp_renderer_set_validation(rdp_renderer_t *r, rdp_report_crash_fn report, void *user);
/* @size bytes of RDRAM at @offset of @buffer. Not @coherent: @buffer is
 * the GPU's own, twice @size (the data, then the write mask), @offset 0,
 * kept in step with @host_rdram. False when out of memory. */
bool rdp_renderer_set_rdram(rdp_renderer_t *r, granite_buffer_t *buffer, uint8_t *host_rdram, size_t offset,
		size_t size, bool coherent);
void rdp_renderer_set_hidden_rdram(rdp_renderer_t *r, granite_buffer_t *buffer);
void rdp_renderer_set_tmem(rdp_renderer_t *r, granite_buffer_t *buffer);

/* After the above: the caps, pipelines and buffers. False when the
 * device cannot, or out of memory. */
bool rdp_renderer_init_renderer(rdp_renderer_t *r, const rdp_renderer_options_t *options);

/* The largest factor up to @factor whose buffers each fit one storage
 * buffer binding of @device */
unsigned rdp_renderer_fitting_upscaling_factor(const granite_device_t *device, unsigned factor,
		VkDeviceSize rdram_size, VkDeviceSize hidden_rdram_bytes, bool super_sampled_readback);
int rdp_renderer_resolve_shader_define(const rdp_renderer_t *r, const char *name, const char *define);
unsigned rdp_renderer_scaling_factor(const rdp_renderer_t *r);

/* The pipelines ahead (rdp_pipeline_builder_ahead): once, after
 * init_renderer, on the thread that processes commands */
void rdp_renderer_build_pipelines_ahead(rdp_renderer_t *r, const pipeline_record_t *records, size_t count,
		rdp_vi_t *vi, bool with_upscaler);
bool rdp_renderer_pipelines_ahead_ready(rdp_renderer_t *r);
void rdp_renderer_wait_pipelines_ahead_for_test(rdp_renderer_t *r);

/* ---- the commands, on the thread that processes them ---- */
/* @attr NULL: flat */
void rdp_renderer_draw(rdp_renderer_t *r, const rdp_triangle_setup_t *setup, const rdp_attribute_setup_t *attr);
void rdp_renderer_set_color_framebuffer(rdp_renderer_t *r, uint32_t addr, uint32_t width, uint32_t fmt);
void rdp_renderer_set_depth_framebuffer(rdp_renderer_t *r, uint32_t addr);
void rdp_renderer_set_scissor_state(rdp_renderer_t *r, const rdp_scissor_state_t *state);
void rdp_renderer_set_static_state(rdp_renderer_t *r, const rdp_static_state_t *state);
void rdp_renderer_set_depth_blend_state(rdp_renderer_t *r, const rdp_depth_blend_state_t *state);
void rdp_renderer_set_tile(rdp_renderer_t *r, uint32_t tile, const rdp_tile_meta_t *meta);
void rdp_renderer_set_tile_size(rdp_renderer_t *r, uint32_t tile, uint32_t slo, uint32_t shi, uint32_t tlo,
		uint32_t thi);
void rdp_renderer_load_tile(rdp_renderer_t *r, uint32_t tile, const rdp_load_tile_info_t *info);
void rdp_renderer_set_blend_color(rdp_renderer_t *r, uint32_t color);
void rdp_renderer_set_fog_color(rdp_renderer_t *r, uint32_t color);
void rdp_renderer_set_env_color(rdp_renderer_t *r, uint32_t color);
void rdp_renderer_set_fill_color(rdp_renderer_t *r, uint32_t color);
void rdp_renderer_set_primitive_color(rdp_renderer_t *r, uint8_t min_level, uint8_t prim_lod_frac, uint32_t color);
void rdp_renderer_set_primitive_depth(rdp_renderer_t *r, uint16_t prim_depth, uint16_t prim_dz);
void rdp_renderer_set_enable_primitive_depth(rdp_renderer_t *r, bool enable);
/* K0 to K5, 9 bits each for K0 to K3 */
void rdp_renderer_set_convert(rdp_renderer_t *r, uint16_t k0, uint16_t k1, uint16_t k2, uint16_t k3, uint16_t k4,
		uint16_t k5);
void rdp_renderer_set_color_key(rdp_renderer_t *r, unsigned component, uint32_t width, uint32_t center,
		uint32_t scale);

/* The flush goes, and what is recorded is submitted */
void rdp_renderer_flush_and_signal(rdp_renderer_t *r);
/* The thread that processes commands saw none for a while: work queued
 * goes to the GPU rather than wait for more */
void rdp_renderer_notify_idle(rdp_renderer_t *r);
/* Not host coherent: @length bytes of RDRAM at @offset the GPU reads
 * next, uploaded and submitted now */
void rdp_renderer_resolve_coherency_external(rdp_renderer_t *r, unsigned offset, unsigned length);
/* For the VI: @pixels of 2^@pixel_size_log2 bytes at @addr, from native
 * RDRAM into the upscaled copy, recorded into @cb */
void rdp_renderer_update_upscaled_domain(rdp_renderer_t *r, granite_command_buffer_t *cb, unsigned addr,
		unsigned pixels, unsigned pixel_size_log2);
const granite_buffer_t *rdp_renderer_upscaled_rdram(const rdp_renderer_t *r);
const granite_buffer_t *rdp_renderer_upscaled_hidden_rdram(const rdp_renderer_t *r);

RETRO_END_DECLS

#endif
