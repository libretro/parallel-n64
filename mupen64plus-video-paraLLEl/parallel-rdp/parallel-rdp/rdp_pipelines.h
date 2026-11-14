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


#ifndef RDP_PIPELINES_H
#define RDP_PIPELINES_H

/* The renderer's compute pipelines: how a command buffer is set up for
 * each, the ones every frame can need, and the background thread that
 * builds them, ahead of the first frame or after a draw missed one.
 * C89.
 *
 * Nothing on the frame path allocates or waits: the jobs a miss needs
 * are made at init, and a miss that finds none free, or its variant
 * already queued, draws with the generic rasterizer meanwhile. */

#include <stdint.h>
#include <stddef.h>
#include <boolean.h>
#include <retro_common_api.h>
#include <retro_atomic.h>
#include <queues/mpsc_stack.h>
#include "command_buffer.h"
#include "device.h"
#include "pipeline_record.h"
#include "pipeline_record_list.h"
#include "renderer_caps.h"
#include "shader_bank.h"
#include "upscaler_pass.h"
#include "video_interface.h"
#include "rdp_worker.h"
#include "pipeline_pending.h"

RETRO_BEGIN_DECLS

/* The framebuffer formats depth_blend.comp is specialised for: I4 to
 * RGBA8888 (FBFormat) */
#define RDP_PIPELINE_FB_FORMATS 5u

/* What the update of the upscaled domain does */
enum rdp_resolve_stage
{
	/* native RDRAM written by the CPU, into the upscaled copy */
	RDP_RESOLVE_PRE = 0,
	/* the upscaled render, back to native RDRAM */
	RDP_RESOLVE_POST,
	/* the same, the samples averaged */
	RDP_RESOLVE_SSAA
};

/* What the pipelines are set up from */
typedef struct rdp_pipeline_setup
{
	const rdp_renderer_caps_t *caps;
	const rdp_shader_bank_t *bank;
	uint32_t rdram_size;
	/* the RDRAM the GPU writes is the CPU's own */
	bool host_coherent;
	/* the device's */
	uint32_t subgroup_size;
} rdp_pipeline_setup_t;

/* span_setup.comp, at the upscaled resolution when @upscale */
void rdp_pipeline_span_setup(const rdp_pipeline_setup_t *setup, granite_command_buffer_t *cb, bool upscale);

/* tile_binning_combined.comp. Returns the subgroup size it runs with:
 * the device's with subgroup tile binning, 32 without. Leaves subgroup
 * size control on for the dispatch; turn it off after. */
uint32_t rdp_pipeline_tile_binning(const rdp_pipeline_setup_t *setup, granite_command_buffer_t *cb, bool upscale);

/* Subgroup size control off again, after tile binning's dispatch */
void rdp_pipeline_end_tile_binning(granite_command_buffer_t *cb);

/* rasterizer.comp's generic variant: the one a draw falls back on while
 * its specialised one is built */
void rdp_pipeline_rasterizer(const rdp_pipeline_setup_t *setup, granite_command_buffer_t *cb, bool upscale);

/* depth_blend.comp, or the ubershader, for a framebuffer of format @fmt
 * (FBFormat), its depth at the colour's address or not */
void rdp_pipeline_depth_blend(const rdp_pipeline_setup_t *setup, granite_command_buffer_t *cb, uint32_t fmt,
		bool same_addr, bool upscaled, bool force_write_mask);

/* The update of the upscaled domain for pixels of 2^@pixel_size_log2
 * bytes */
void rdp_pipeline_upscaled_domain(const rdp_pipeline_setup_t *setup, granite_command_buffer_t *cb,
		enum rdp_resolve_stage stage, unsigned pixel_size_log2, bool same_addr);

/* A pipeline for the builder's thread */
typedef struct rdp_pipeline_job
{
	mpsc_stack_node_t node;
	granite_pipeline_compile_t compile;
	uint32_t kind;  /* PIPELINE_RECORD_* */
	uint8_t ahead;  /* one of rdp_pipeline_builder_ahead's block */
	uint8_t urgent; /* a draw waits for it */
} rdp_pipeline_job_t;

/* The most pipelines rdp_pipeline_builder_ahead adds to a list's */
#define RDP_PIPELINE_MAX_KNOWN 256u

typedef struct rdp_pipeline_builder
{
	granite_device_t *device;
	rdp_worker_t *worker;
	/* the variants queued after misses, and the jobs carrying them */
	rdp_hash_set_t pending;
	rdp_node_pool_t pool;
	rdp_pipeline_job_t *jobs;
	/* rdp_pipeline_builder_ahead's, one block freed with the builder */
	rdp_pipeline_job_t *ahead_jobs;
	int ahead_urgent;
	retro_atomic_int_t ahead_urgent_done;
	retro_atomic_int_t stop;
} rdp_pipeline_builder_t;

/* No thread, nothing made */
void rdp_pipeline_builder_clear(rdp_pipeline_builder_t *b);

/* Starts the thread and makes the jobs for @max_misses queued misses.
 * False when the thread does not start. */
bool rdp_pipeline_builder_init(rdp_pipeline_builder_t *b, granite_device_t *device, unsigned max_misses);

/* The thread drains what is queued and goes, building none of it: only
 * a compile already in the driver is waited for. */
void rdp_pipeline_builder_free(rdp_pipeline_builder_t *b);

/* A draw on @cb had no pipeline: its variant goes to the thread, unless
 * it is there already or no job is free. The frame path. */
void rdp_pipeline_builder_miss(rdp_pipeline_builder_t *b, const granite_command_buffer_t *cb);

/* Hands the thread the pipelines of @records not built yet and, unless
 * PARALLEL_RDP_NO_KNOWN_PIPELINES is set, those every frame can need
 * (@vi's scanout too when not NULL): those a draw would wait for first,
 * then the specialised rasterizer variants a draw does not wait for.
 * Once; at load. */
void rdp_pipeline_builder_ahead(rdp_pipeline_builder_t *b, const rdp_pipeline_setup_t *setup,
		const pipeline_record_t *records, size_t count, rdp_vi_t *vi, bool with_upscaler);

/* Whether every pipeline a draw would wait for is built. Any thread. */
bool rdp_pipeline_builder_ready(rdp_pipeline_builder_t *b);

/* Waits until it is: tests only */
void rdp_pipeline_builder_wait(rdp_pipeline_builder_t *b);

RETRO_END_DECLS

#endif
