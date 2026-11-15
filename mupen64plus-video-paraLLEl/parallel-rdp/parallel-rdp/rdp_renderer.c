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


#include <stdlib.h>
#include <string.h>
#include "rdp_renderer.h"
#include "command_buffer.h"
#include "fence.h"
#include "logging.h"
#include "timer.h"
#include "luts.h"

/* Never more than this many misses queued to the pipeline thread */
#define RDP_RENDERER_MAX_QUEUED_PIPELINES (4u * RDP_QUEUE_MAX_STATIC_STATES)

static void flush_queues(rdp_renderer_t *r);
static void submit_to_queue(rdp_renderer_t *r);
static void bind_stream_instance(rdp_renderer_t *r);

void rdp_renderer_init(rdp_renderer_t *r, rdp_timeline_t *timeline)
{
	unsigned i;
	memset(r, 0, sizeof(*r));
	r->timeline = timeline;
	r->fb.fmt = RDP_FB_I8;
	retro_atomic_int_init(&r->active_submissions, 0);
	rdp_caps_init(&r->caps);
	rdp_queue_init(&r->queue);
	rdp_sync_ring_init(&r->sync_ring);
	rdp_pipeline_builder_clear(&r->pipelines);
	/* (a tile not set yet is 16 bits) */
	for (i = 0; i < 8; i++)
		r->tiles[i].meta.size = RDP_TEXTURE_SIZE_16;
}

void rdp_renderer_free(rdp_renderer_t *r)
{
	unsigned i;
	rdp_pipeline_builder_free(&r->pipelines);
	rdp_coherency_free(&r->incoherent);
	for (i = 0; i < RDP_SYNC_RING_SIZE; i++)
		rdp_stream_instance_free(&r->buffer_instances[i]);
	rdp_renderer_buffers_free(&r->buffers);
	rdp_sync_ring_free(&r->sync_ring);
	if (r->cmd)
		granite_cb_unref(r->cmd);
	r->cmd = NULL;
}

void rdp_renderer_set_device(rdp_renderer_t *r, granite_device_t *device)
{
	r->device = device;
}

void rdp_renderer_set_shader_bank(rdp_renderer_t *r, const rdp_shader_bank_t *bank)
{
	r->shader_bank = bank;
}

void rdp_renderer_set_validation(rdp_renderer_t *r, rdp_report_crash_fn report, void *user)
{
	r->report_crash = report;
	r->report_user = user;
}

bool rdp_renderer_set_rdram(rdp_renderer_t *r, granite_buffer_t *buffer, uint8_t *host_rdram, size_t offset,
		size_t size, bool coherent)
{
	r->rdram = buffer;
	r->rdram_offset = offset;
	r->rdram_size = size;
	r->is_host_coherent = coherent;

	if (!coherent)
	{
		/* The GPU has its own RDRAM, kept in step with the host's page by
		 * page (every page uploaded whole, to compare against, with
		 * PARALLEL_RDP_FULL_RDRAM_UPLOAD) */
		const char *env = getenv("PARALLEL_RDP_FULL_RDRAM_UPLOAD");
		bool full_upload = env && strtol(env, NULL, 0) > 0;
		return rdp_coherency_init(&r->incoherent, r->device, buffer, host_rdram, size, full_upload);
	}
	rdp_coherency_free(&r->incoherent);
	return true;
}

void rdp_renderer_set_hidden_rdram(rdp_renderer_t *r, granite_buffer_t *buffer)
{
	r->hidden_rdram = buffer;
}

void rdp_renderer_set_tmem(rdp_renderer_t *r, granite_buffer_t *buffer)
{
	r->tmem = buffer;
}

static VkDeviceSize device_range(const granite_device_t *device)
{
	return rdp_caps_storage_buffer_range(device->gpu_props.limits.maxStorageBufferRange);
}

unsigned rdp_renderer_fitting_upscaling_factor(const granite_device_t *device, unsigned factor,
		VkDeviceSize rdram_size, VkDeviceSize hidden_rdram_bytes, bool super_sampled_readback)
{
	return rdp_caps_fitting_upscaling_factor(device_range(device), factor, rdram_size, hidden_rdram_bytes,
			super_sampled_readback);
}

bool rdp_renderer_init_renderer(rdp_renderer_t *r, const rdp_renderer_options_t *options)
{
	VkDeviceSize stream_sizes[RDP_STREAM_BUFFERS];
	unsigned i;

	if (options->upscaling_factor == 0)
		return false;
	if (options->upscaling_factor == 1 && options->super_sampled_readback)
		return false;

	/* The buffers below are sized for the caps, and those only the split
	 * rasterizer uses are not made for the ubershader */
	r->caps.super_sample_readback = options->super_sampled_readback;
	r->caps.super_sample_readback_dither = options->super_sampled_readback_dither;
	if (!rdp_caps_init_limits(&r->caps, options->upscaling_factor, options->no_storage_8bit,
			device_range(r->device)))
		return false;
	if (!rdp_caps_init_features(&r->caps, &r->device->ext))
		return false;

	if (!rdp_pipeline_builder_init(&r->pipelines, r->device, RDP_RENDERER_MAX_QUEUED_PIPELINES))
		return false;

	stream_sizes[RDP_STREAM_TRIANGLE_SETUP] = sizeof(rdp_triangle_setup_t) * RDP_QUEUE_MAX_PRIMITIVES;
	stream_sizes[RDP_STREAM_ATTRIBUTE_SETUP] = sizeof(rdp_attribute_setup_t) * RDP_QUEUE_MAX_PRIMITIVES;
	stream_sizes[RDP_STREAM_DERIVED_SETUP] = sizeof(rdp_derived_setup_t) * RDP_QUEUE_MAX_PRIMITIVES;
	stream_sizes[RDP_STREAM_SCISSOR_SETUP] = sizeof(rdp_scissor_state_t) * RDP_QUEUE_MAX_PRIMITIVES;
	stream_sizes[RDP_STREAM_STATIC_RASTER_STATE] = sizeof(rdp_static_state_t) * RDP_QUEUE_MAX_STATIC_STATES;
	stream_sizes[RDP_STREAM_DEPTH_BLEND_STATE] = sizeof(rdp_depth_blend_state_t) * RDP_QUEUE_MAX_DEPTH_BLEND_STATES;
	stream_sizes[RDP_STREAM_TILE_INFO_STATE] = sizeof(rdp_tile_info_t) * RDP_QUEUE_MAX_TILE_INFOS;
	stream_sizes[RDP_STREAM_STATE_INDICES] = sizeof(rdp_instance_indices_t) * RDP_QUEUE_MAX_PRIMITIVES;
	stream_sizes[RDP_STREAM_SPAN_INFO_OFFSETS] = sizeof(rdp_span_info_offsets_t) * RDP_QUEUE_MAX_PRIMITIVES;
	stream_sizes[RDP_STREAM_SPAN_INFO_JOBS] = sizeof(rdp_span_job_t) * RDP_QUEUE_MAX_SPAN_SETUPS;
	for (i = 0; i < RDP_SYNC_RING_SIZE; i++)
		if (!rdp_stream_instance_init(&r->buffer_instances[i], r->device, stream_sizes))
			return false;
	bind_stream_instance(r);

	if (!rdp_renderer_buffers_init(&r->buffers, r->device, &r->caps, RDP_QUEUE_MAX_UPLOADS * 0x1000,
			(VkDeviceSize)options->upscaling_factor * RDP_QUEUE_MAX_SPAN_SETUPS * RDP_RENDERER_SPAN_SETUP_SIZE,
			blender_lut, sizeof(blender_lut)))
		return false;
	r->indirect_cleared = false;

	if (options->upscaling_factor > 1)
	{
		if (!r->device || !r->rdram || !r->hidden_rdram)
		{
			LOGE("Renderer is not initialized.\n");
			return false;
		}
		r->caps.upscaling = options->upscaling_factor;
		if (!rdp_renderer_buffers_init_upscaled(&r->buffers, r->device, &r->caps, r->rdram_size,
				r->hidden_rdram->info.size))
			return false;
	}
	return true;
}

int rdp_renderer_resolve_shader_define(const rdp_renderer_t *r, const char *name, const char *define)
{
	return rdp_caps_resolve_shader_define(&r->caps, name, define);
}

unsigned rdp_renderer_scaling_factor(const rdp_renderer_t *r)
{
	return r->caps.upscaling;
}

const granite_buffer_t *rdp_renderer_upscaled_rdram(const rdp_renderer_t *r)
{
	return r->buffers.upscaling_multisampled_rdram;
}

const granite_buffer_t *rdp_renderer_upscaled_hidden_rdram(const rdp_renderer_t *r)
{
	return r->buffers.upscaling_multisampled_hidden_rdram;
}

/* ---- what the passes are set up from ---- */
static rdp_pipeline_setup_t pipeline_setup(const rdp_renderer_t *r)
{
	rdp_pipeline_setup_t setup;
	setup.caps = &r->caps;
	setup.bank = r->shader_bank;
	setup.rdram_size = (uint32_t)r->rdram_size;
	setup.host_coherent = r->is_host_coherent;
	setup.subgroup_size = r->device->ext.subgroup_properties.subgroupSize;
	return setup;
}

static rdp_pass_resources_t pass_resources(rdp_renderer_t *r, const rdp_pipeline_setup_t *setup)
{
	rdp_pass_resources_t res;
	res.setup = setup;
	res.pipelines = &r->pipelines;
	res.buffers = &r->buffers;
	res.streams = &r->buffer_instances[r->sync_ring.current].gpu;
	res.rdram = r->rdram;
	res.rdram_offset = r->rdram_offset;
	res.rdram_size = r->rdram_size;
	res.hidden_rdram = r->hidden_rdram;
	res.tmem = r->tmem;
	return res;
}

static rdp_pass_flush_t pass_flush(const rdp_renderer_t *r)
{
	rdp_pass_flush_t f;
	f.target.addr = r->fb.addr;
	f.target.depth_addr = r->fb.depth_addr;
	f.target.width = r->fb.width;
	f.target.height = r->queue.height;
	f.target.fmt = r->fb.fmt;
	f.target.base_primitive_index = r->base_primitive_index;
	f.target.num_primitives = r->queue.num_primitives;
	f.uploads = r->queue.uploads;
	f.num_uploads = r->queue.num_uploads;
	f.num_span_jobs = r->queue.num_span_jobs;
	f.states = r->queue.static_states;
	f.num_states = r->queue.num_static_states;
	return f;
}

void rdp_renderer_build_pipelines_ahead(rdp_renderer_t *r, const pipeline_record_t *records, size_t count,
		rdp_vi_t *vi, bool with_upscaler)
{
	const rdp_pipeline_setup_t setup = pipeline_setup(r);
	rdp_pipeline_builder_ahead(&r->pipelines, &setup, records, count, vi, with_upscaler);
}

bool rdp_renderer_pipelines_ahead_ready(rdp_renderer_t *r)
{
	return rdp_pipeline_builder_ready(&r->pipelines);
}

void rdp_renderer_wait_pipelines_ahead_for_test(rdp_renderer_t *r)
{
	rdp_pipeline_builder_wait(&r->pipelines);
}

/* ---- the command buffer, and what goes to the GPU ---- */
static void ensure_command_buffer(rdp_renderer_t *r)
{
	if (!r->cmd)
		r->cmd = granite_device_request_command_buffer(r->device, GRANITE_QUEUE_COMPUTE);

	if (!r->caps.ubershader && !r->indirect_cleared)
	{
		const rdp_pipeline_setup_t setup = pipeline_setup(r);
		const rdp_pass_resources_t res = pass_resources(r, &setup);
		VkMemoryBarrier global;

		r->indirect_cleared = true;
		rdp_pass_clear_indirect(&res, r->cmd);
		memset(&global, 0, sizeof(global));
		global.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		global.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		global.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
		granite_cb_barrier(r->cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				1, &global, 0, NULL, 0, NULL);
	}
}

/* The timeline worker waits for @fence (its own reference), then counts
 * the submission done */
static void enqueue_fence_wait(rdp_renderer_t *r, granite_fence_t *fence)
{
	rdp_timeline_op_t op;
	memset(&op, 0, sizeof(op));
	if (fence)
		granite_fence_ref(fence);
	op.fence = fence;
	op.unlock_cookie = &r->active_submissions;
	retro_atomic_fetch_add_int(&r->active_submissions, 1);
	rdp_timeline_enqueue(r->timeline, &op);
	r->last_submit_ns = granite_time_nsecs();
}

static void submit_to_queue(rdp_renderer_t *r)
{
	const rdp_submit_taken_t taken = rdp_submit_take(&r->submits, r->caps.super_sample_readback);
	const bool host_visible = taken.host_visible;
	granite_fence_t *fence = NULL;

	if (!r->cmd)
	{
		if (host_visible)
		{
			granite_device_submit_empty(r->device, GRANITE_QUEUE_COMPUTE, &fence);
			enqueue_fence_wait(r, fence);
			if (fence)
				granite_fence_unref(fence);
		}
		return;
	}

	rdp_submit_end_barrier(r->cmd, &taken, r->is_host_coherent || !r->incoherent.staging_readback);

	if (r->is_host_coherent)
	{
		granite_device_submit(r->device, r->cmd, &fence, 0, NULL);
		if (host_visible)
			enqueue_fence_wait(r, fence);
	}
	else
	{
		rdp_timeline_op_t op;
		memset(&op, 0, sizeof(op));
		if (host_visible)
		{
			/* the GPU's writes, copied back to the host's RDRAM */
			const granite_buffer_t *src = NULL;
			op.copies = r->incoherent.copies;
			op.num_copies = rdp_coherency_read_back(&r->incoherent, r->cmd, &src);
			op.src = src;
			op.dst = r->incoherent.host_rdram;
		}

		granite_device_submit(r->device, r->cmd, &fence, 0, NULL);

		if (host_visible)
		{
			enqueue_fence_wait(r, fence);
			if (op.num_copies)
			{
				/* the worker's own reference */
				if (fence)
					granite_fence_ref(fence);
				op.fence = fence;
				rdp_timeline_enqueue(r->timeline, &op);
			}
		}
	}
	r->cmd = NULL;

	rdp_sync_ring_submitted(&r->sync_ring, fence);
	if (fence)
		granite_fence_unref(fence);
}

static void maintain_queues(rdp_renderer_t *r)
{
	if (rdp_submit_due(&r->submits, r->caps.super_sample_readback,
			retro_atomic_load_relaxed_int(&r->active_submissions) == 0,
			(uint64_t)(granite_time_nsecs() - r->last_submit_ns)))
		submit_to_queue(r);
}

/* The streams of the next flush are written straight into this slot's
 * upload buffers, so the GPU must be done with what it last read from
 * them before the first primitive is added, not at the flush */
static void bind_stream_instance(rdp_renderer_t *r)
{
	static const enum rdp_stream_buffer streams[] = {
		RDP_STREAM_TRIANGLE_SETUP, RDP_STREAM_ATTRIBUTE_SETUP, RDP_STREAM_DERIVED_SETUP,
		RDP_STREAM_SCISSOR_SETUP, RDP_STREAM_STATE_INDICES, RDP_STREAM_SPAN_INFO_OFFSETS,
		RDP_STREAM_SPAN_INFO_JOBS
	};
	rdp_stream_instance_t *instance = &r->buffer_instances[r->sync_ring.current];
	void *mapped[RDP_STREAM_BUFFERS];
	unsigned i;

	if (rdp_sync_ring_unsubmitted(&r->sync_ring, r->sync_ring.current))
		submit_to_queue(r);
	rdp_sync_ring_wait(&r->sync_ring, r->sync_ring.current);

	/* The upload buffers are host-visible, so they always map (the state
	 * caches are copied in at the flush, so only the streams) */
	memset(mapped, 0, sizeof(mapped));
	for (i = 0; i < sizeof(streams) / sizeof(streams[0]); i++)
	{
		const granite_buffer_t *b = instance->cpu.buffers[streams[i]];
		mapped[streams[i]] = granite_device_map_host_buffer(r->device, b, GRANITE_MEMORY_ACCESS_WRITE_BIT, 0,
				b->info.size);
	}
	rdp_queue_bind(&r->queue, mapped);
}

static uint32_t color_bytes(const rdp_renderer_t *r)
{
	uint32_t pixels = r->fb.width * r->queue.height;
	switch (r->fb.fmt)
	{
	case RDP_FB_RGBA8888:
		return pixels * 4;
	case RDP_FB_RGBA5551:
	case RDP_FB_IA88:
		return pixels * 2;
	default:
		return pixels;
	}
}

static void flush_queues(rdp_renderer_t *r)
{
	rdp_pipeline_setup_t setup;
	rdp_pass_flush_t flush;
	bool native;

	if (r->queue.num_uploads == 0 && r->queue.num_span_jobs == 0)
	{
		r->base_primitive_index += r->queue.num_primitives;
		rdp_queue_reset(&r->queue);
		return;
	}

	if (!r->is_host_coherent)
	{
		uint32_t color = color_bytes(r), depth = r->fb.width * r->queue.height * 2;
		rdp_coherency_mark_read(&r->incoherent, r->fb.addr, color);
		rdp_coherency_mark_read(&r->incoherent, r->fb.depth_addr, depth);
		/* they are written, so locked down */
		rdp_coherency_lock_write(&r->incoherent, r->fb.addr, color);
		rdp_coherency_lock_write(&r->incoherent, r->fb.depth_addr, depth);
	}

	/* (bind_stream_instance waited for this slot's last use) */
	rdp_sync_ring_use(&r->sync_ring);
	ensure_command_buffer(r);

	if (!r->is_host_coherent)
		rdp_coherency_upload(&r->incoherent, r->cmd, r->shader_bank);
	{
		const void *data[RDP_STREAM_BUFFERS];
		VkDeviceSize bytes[RDP_STREAM_BUFFERS];
		rdp_queue_upload_data(&r->queue, data, bytes);
		rdp_stream_instance_upload(&r->buffer_instances[r->sync_ring.current], r->device, r->cmd, data, bytes);
	}

	setup = pipeline_setup(r);
	flush = pass_flush(r);

	/* With the super-sampled read-back only the upscaled passes run */
	native = !r->caps.super_sample_readback;
	if (native)
	{
		const rdp_pass_resources_t res = pass_resources(r, &setup);
		rdp_pass_record_native(&res, r->cmd, &flush);
		r->submits.passes++;
	}

	if (rdp_pass_flush_upscaled(&r->caps, &flush))
	{
		rdp_pass_resources_t res;
		if (native)
		{
			maintain_queues(r);
			ensure_command_buffer(r);
			/* the same streams are read on: the slot's fence is the
			 * submission with the upscaled passes */
			rdp_sync_ring_use(&r->sync_ring);
		}

		res = pass_resources(r, &setup);
		rdp_pass_record_upscaled(&res, r->cmd, &flush);
		r->submits.passes_upscaled++;
		r->submits.primitives_upscaled += r->queue.num_primitives;
	}

	r->base_primitive_index += r->queue.num_primitives;
	rdp_pass_record_end(r->cmd);

	/* the next slot */
	rdp_sync_ring_advance(&r->sync_ring);
	rdp_queue_reset(&r->queue);
	bind_stream_instance(r);
	maintain_queues(r);
}

static void flush_for_queue(void *r)
{
	flush_queues((rdp_renderer_t *)r);
}

void rdp_renderer_flush_and_signal(rdp_renderer_t *r)
{
	flush_queues(r);
	submit_to_queue(r);
}

void rdp_renderer_notify_idle(rdp_renderer_t *r)
{
	if (rdp_submit_idle_due(&r->submits))
	{
		flush_queues(r);
		submit_to_queue(r);
	}
}

void rdp_renderer_resolve_coherency_external(rdp_renderer_t *r, unsigned offset, unsigned length)
{
	rdp_coherency_mark_read(&r->incoherent, offset, length);
	ensure_command_buffer(r);
	rdp_coherency_upload(&r->incoherent, r->cmd, r->shader_bank);
	granite_device_submit(r->device, r->cmd, NULL, 0, NULL);
	r->cmd = NULL;
}

void rdp_renderer_update_upscaled_domain(rdp_renderer_t *r, granite_command_buffer_t *cb, unsigned addr,
		unsigned pixels, unsigned pixel_size_log2)
{
	const rdp_pipeline_setup_t setup = pipeline_setup(r);
	const rdp_pass_resources_t res = pass_resources(r, &setup);
	rdp_pass_update_upscaled_domain(&res, cb, RDP_RESOLVE_PRE, addr, addr, pixels, 1, pixel_size_log2);
}

/* ---- the commands ---- */

/* Out of line: draws and loads pay only the test of r->report_crash */
static void report(const rdp_renderer_t *r, enum rdp_validation_error err, const char *msg)
{
	r->report_crash(r->report_user, err, msg);
}

static void validate_draw_state(const rdp_renderer_t *r)
{
	const uint32_t raster = r->queue.static_state.flags;
	const uint32_t depth = r->queue.depth_blend.flags;

	if (raster & RASTERIZATION_FILL_BIT)
	{
		if (r->fb.fmt == RDP_FB_I4)
			report(r, RDP_VALIDATION_FILL_4BPP, "Attempted to use Fill mode on 4bpp surface.");
		if (depth & DEPTH_BLEND_DEPTH_TEST_BIT)
			report(r, RDP_VALIDATION_FILL_DEPTH_TEST, "Attempted to use Fill mode with depth test.");
		if (depth & DEPTH_BLEND_IMAGE_READ_ENABLE_BIT)
			report(r, RDP_VALIDATION_FILL_IMAGE_READ_ENABLE, "Attempted to use Fill mode with image read enable.");
		if ((depth & DEPTH_BLEND_DEPTH_UPDATE_BIT) && !r->constants.use_prim_depth)
			report(r, RDP_VALIDATION_FILL_DEPTH_WRITE, "Attempted to use Fill mode with depth write enabled.");
	}
	else if (raster & RASTERIZATION_COPY_BIT)
	{
		if (r->fb.fmt == RDP_FB_RGBA8888)
			report(r, RDP_VALIDATION_COPY_32BPP, "Attempted to use Copy mode on 32bpp surface.");
	}
}

void rdp_renderer_draw(rdp_renderer_t *r, const rdp_triangle_setup_t *setup, const rdp_attribute_setup_t *attr)
{
	if (r->report_crash)
		validate_draw_state(r);
	rdp_queue_add_primitive(&r->queue, &r->caps, &r->constants, r->tiles, setup, attr);
	r->submits.primitives++;
	if (rdp_queue_full(&r->queue, &r->caps))
		flush_queues(r);
}

void rdp_renderer_set_color_framebuffer(rdp_renderer_t *r, uint32_t addr, uint32_t width, uint32_t fmt)
{
	if (r->fb.addr != addr || r->fb.width != width || r->fb.fmt != fmt)
		flush_queues(r);
	r->fb.addr = addr;
	r->fb.width = width;
	r->fb.fmt = fmt;
}

void rdp_renderer_set_depth_framebuffer(rdp_renderer_t *r, uint32_t addr)
{
	if (r->fb.depth_addr != addr)
		flush_queues(r);
	r->fb.depth_addr = addr;
}

void rdp_renderer_set_scissor_state(rdp_renderer_t *r, const rdp_scissor_state_t *state)
{
	r->queue.scissor = *state;
}

void rdp_renderer_set_static_state(rdp_renderer_t *r, const rdp_static_state_t *state)
{
	r->queue.static_state = *state;
}

void rdp_renderer_set_depth_blend_state(rdp_renderer_t *r, const rdp_depth_blend_state_t *state)
{
	r->queue.depth_blend = *state;
}

void rdp_renderer_set_tile(rdp_renderer_t *r, uint32_t tile, const rdp_tile_meta_t *meta)
{
	r->tiles[tile & 7].meta = *meta;
}

void rdp_renderer_set_tile_size(rdp_renderer_t *r, uint32_t tile, uint32_t slo, uint32_t shi, uint32_t tlo,
		uint32_t thi)
{
	rdp_tile_size_t *size = &r->tiles[tile & 7].size;
	size->slo = slo;
	size->shi = shi;
	size->tlo = tlo;
	size->thi = thi;
}

void rdp_renderer_load_tile(rdp_renderer_t *r, uint32_t tile, const rdp_load_tile_info_t *info)
{
	rdp_queue_fb_t qfb;
	unsigned crashes;

	if (r->report_crash && info->mode == RDP_UPLOAD_TLUT && (info->thi >> 2) > (info->tlo >> 2))
		report(r, RDP_VALIDATION_INVALID_MULTILINE_LOAD_TLUT, "Attempting to load multiple lines in TLUT.");

	qfb.addr = r->fb.addr;
	qfb.depth_addr = r->fb.depth_addr;
	qfb.width = r->fb.width;
	qfb.fmt = r->fb.fmt;
	if (rdp_queue_upload_reads_pending(&r->queue, &qfb, (uint32_t)r->rdram_size, info->tex_addr))
		flush_queues(r);

	if (rdp_load_is_empty(info))
		return;

	if (!r->is_host_coherent)
	{
		uint32_t addr, size;
		rdp_load_rdram_range(info, &addr, &size);
		rdp_coherency_mark_read(&r->incoherent, addr, size);
	}

	crashes = rdp_queue_load_tile(&r->queue, r->tiles, tile, info, flush_for_queue, r);
	if (r->report_crash)
		while (crashes--)
			report(r, RDP_VALIDATION_LOAD_TILE_4BPP, "4-bit VRAM pointer crashes the RDP.");
}

void rdp_renderer_set_blend_color(rdp_renderer_t *r, uint32_t color)
{
	r->constants.blend_color = color;
}

void rdp_renderer_set_fog_color(rdp_renderer_t *r, uint32_t color)
{
	r->constants.fog_color = color;
}

void rdp_renderer_set_env_color(rdp_renderer_t *r, uint32_t color)
{
	r->constants.env_color = color;
}

void rdp_renderer_set_fill_color(rdp_renderer_t *r, uint32_t color)
{
	r->constants.fill_color = color;
}

void rdp_renderer_set_primitive_color(rdp_renderer_t *r, uint8_t min_level, uint8_t prim_lod_frac, uint32_t color)
{
	r->constants.primitive_color = color;
	r->constants.min_level = min_level;
	r->constants.prim_lod_frac = prim_lod_frac;
}

void rdp_renderer_set_primitive_depth(rdp_renderer_t *r, uint16_t prim_depth, uint16_t prim_dz)
{
	r->constants.prim_depth = (int32_t)((uint32_t)(prim_depth & 0x7fff) << 16);
	r->constants.prim_dz = prim_dz;
}

void rdp_renderer_set_enable_primitive_depth(rdp_renderer_t *r, bool enable)
{
	r->constants.use_prim_depth = enable;
}

/* 2 K + 1, K a 9-bit signed value */
static uint16_t convert_factor(uint16_t k)
{
	int32_t v = (int32_t)(k & 0x1ff);
	if (v & 0x100)
		v -= 0x200;
	return (uint16_t)(2 * v + 1);
}

void rdp_renderer_set_convert(rdp_renderer_t *r, uint16_t k0, uint16_t k1, uint16_t k2, uint16_t k3, uint16_t k4,
		uint16_t k5)
{
	r->constants.convert[0] = convert_factor(k0);
	r->constants.convert[1] = convert_factor(k1);
	r->constants.convert[2] = convert_factor(k2);
	r->constants.convert[3] = convert_factor(k3);
	r->constants.convert[4] = k4;
	r->constants.convert[5] = k5;
}

void rdp_renderer_set_color_key(rdp_renderer_t *r, unsigned component, uint32_t width, uint32_t center,
		uint32_t scale)
{
	r->constants.key_width[component] = (uint16_t)width;
	r->constants.key_center[component] = (uint8_t)center;
	r->constants.key_scale[component] = (uint8_t)scale;
}
