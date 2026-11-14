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

#include "rdp_renderer.hpp"
#include <stdio.h>
#include <stdlib.h>
#include "rdp_device.hpp"
#include "logging.h"
#include "bitops.hpp"
#include "luts.hpp"
#include "pipeline_record_list.h"
#include "timer.h"
#include "thread_name.h"

namespace RDP
{
static inline Vulkan::Buffer &buf(granite_buffer_t *buffer)
{
	return *static_cast<Vulkan::Buffer *>(buffer);
}

static inline Vulkan::BufferView &view(granite_buffer_view_t *view)
{
	return *static_cast<Vulkan::BufferView *>(view);
}

static_assert(RDP_QUEUE_MAX_PRIMITIVES == Limits::MaxPrimitives &&
              RDP_QUEUE_MAX_STATIC_STATES == Limits::MaxStaticRasterizationStates &&
              RDP_QUEUE_MAX_DEPTH_BLEND_STATES == Limits::MaxDepthBlendStates &&
              RDP_QUEUE_MAX_TILE_INFOS == Limits::MaxTileInfoStates &&
              RDP_QUEUE_MAX_UPLOADS == Limits::MaxTMEMInstances &&
              RDP_QUEUE_MAX_SPAN_SETUPS == Limits::MaxSpanSetups &&
              RDP_QUEUE_SPAN_JOB_LINES == ImplementationConstants::DefaultWorkgroupSize,
              "rdp_queue.h queues for other limits.");
static_assert((sizeof(rdp_instance_indices_t) & 15) == 0 && sizeof(rdp_span_info_offsets_t) == 16 &&
              sizeof(rdp_span_job_t) == 8, "The shaders read other layouts.");

static_assert(RDP_SYNC_RING_SIZE == Limits::NumSyncStates, "rdp_sync_ring.h rotates another number of slots.");

static_assert(RDP_CAPS_MAX_WIDTH == Limits::MaxWidth && RDP_CAPS_MAX_HEIGHT == Limits::MaxHeight &&
              RDP_CAPS_TILE_WIDTH == ImplementationConstants::TileWidth &&
              RDP_CAPS_TILE_HEIGHT == ImplementationConstants::TileHeight &&
              RDP_CAPS_MAX_PRIMITIVES == Limits::MaxPrimitives &&
              RDP_CAPS_MAX_STATIC_RASTER_STATES == Limits::MaxStaticRasterizationStates &&
              RDP_CAPS_MAX_TILE_INSTANCES == Limits::MaxTileInstances &&
              RDP_CAPS_TILE_RASTER_WORK_SIZE == sizeof(TileRasterWork),
              "renderer_caps.h sizes buffers for other limits.");

static size_t div_round_up(size_t a, size_t b)
{
	return (a + b - 1) / b;
}

/* Never more than this many misses queued to the pipeline thread. */
static const unsigned MaxQueuedPipelines = 4 * Limits::MaxStaticRasterizationStates;

Renderer::Renderer(CommandProcessor &processor_)
	: processor(processor_)
{
	retro_atomic_int_init(&active_submissions, 0);
	rdp_caps_init(&caps);
	rdp_queue_init(&queue);
	rdp_sync_ring_init(&sync_ring);
	rdp_pipeline_builder_clear(&pipelines);
	// (a tile not set yet is 16 bits)
	memset(tiles, 0, sizeof(tiles));
	for (auto &tile : tiles)
		tile.meta.size = RDP_TEXTURE_SIZE_16;
}

Renderer::~Renderer()
{
	rdp_pipeline_builder_free(&pipelines);
	rdp_coherency_free(&incoherent);
	for (auto &instance : buffer_instances)
		rdp_stream_instance_free(&instance);
	rdp_renderer_buffers_free(&buffers);
	rdp_sync_ring_free(&sync_ring);
}

unsigned Renderer::fitting_upscaling_factor(const Vulkan::Device &device, unsigned factor,
                                            VkDeviceSize rdram_size, VkDeviceSize hidden_rdram_bytes,
                                            bool super_sampled_readback)
{
	return rdp_caps_fitting_upscaling_factor(
			rdp_caps_storage_buffer_range(device.get_gpu_properties().limits.maxStorageBufferRange),
			factor, rdram_size, hidden_rdram_bytes, super_sampled_readback);
}

void Renderer::set_shader_bank(const ShaderBank *bank)
{
	shader_bank = bank;
}

bool Renderer::init_renderer(const RendererOptions &options)
{
	if (options.upscaling_factor == 0)
		return false;
	if (options.upscaling_factor == 1 && options.super_sampled_readback)
		return false;

	// The buffers below are sized for the caps, and those only the split
	// rasterizer uses are not made for the ubershader.
	caps.super_sample_readback = options.super_sampled_readback;
	caps.super_sample_readback_dither = options.super_sampled_readback_dither;
	if (!rdp_caps_init_limits(&caps, options.upscaling_factor, options.no_storage_8bit,
	                          rdp_caps_storage_buffer_range(device->get_gpu_properties().limits.maxStorageBufferRange)))
		return false;
	if (!rdp_caps_init_features(&caps, &device->get_device_features()))
		return false;

	if (!rdp_pipeline_builder_init(&pipelines, device, MaxQueuedPipelines))
		return false;

	{
		static const VkDeviceSize stream_sizes[RDP_STREAM_BUFFERS] = {
			sizeof(TriangleSetup) * Limits::MaxPrimitives,
			sizeof(AttributeSetup) * Limits::MaxPrimitives,
			sizeof(DerivedSetup) * Limits::MaxPrimitives,
			sizeof(ScissorState) * Limits::MaxPrimitives,
			sizeof(StaticRasterizationState) * Limits::MaxStaticRasterizationStates,
			sizeof(DepthBlendState) * Limits::MaxDepthBlendStates,
			sizeof(TileInfo) * Limits::MaxTileInfoStates,
			sizeof(rdp_instance_indices_t) * Limits::MaxPrimitives,
			sizeof(rdp_span_info_offsets_t) * Limits::MaxPrimitives,
			sizeof(rdp_span_job_t) * Limits::MaxSpanSetups,
		};
		for (auto &instance : buffer_instances)
			if (!rdp_stream_instance_init(&instance, device, stream_sizes))
				return false;
	}
	bind_stream_instance();

	static_assert((Limits::MaxPrimitives % 32) == 0, "MaxPrimitives must be divisble by 32.");
	static_assert(Limits::MaxPrimitives <= (32 * 32), "MaxPrimitives must be less-or-equal than 1024.");
	if (!rdp_renderer_buffers_init(&buffers, device, &caps, Limits::MaxTMEMInstances * 0x1000,
	                               options.upscaling_factor * Limits::MaxSpanSetups * sizeof(SpanSetup),
	                               blender_lut, sizeof(blender_lut)))
		return false;
	indirect_cleared = false;
	if (options.upscaling_factor > 1 && !init_internal_upscaling_factor(options))
		return false;
	return true;
}

void Renderer::set_device(Vulkan::Device *device_)
{
	device = device_;
}

int Renderer::resolve_shader_define(const char *name, const char *define) const
{
	return rdp_caps_resolve_shader_define(&caps, name, define);
}

bool Renderer::init_internal_upscaling_factor(const RendererOptions &options)
{
	if (!device || !rdram || !hidden_rdram)
	{
		LOGE("Renderer is not initialized.\n");
		return false;
	}

	caps.upscaling = options.upscaling_factor;
	caps.super_sample_readback = options.super_sampled_readback;
	caps.super_sample_readback_dither = options.super_sampled_readback_dither;
	return rdp_renderer_buffers_init_upscaled(&buffers, device, &caps, rdram_size,
	                                          hidden_rdram->get_create_info().size);
}

bool Renderer::set_rdram(Vulkan::Buffer *buffer, uint8_t *host_rdram, size_t offset, size_t size, bool coherent)
{
	rdram = buffer;
	rdram_offset = offset;
	rdram_size = size;
	is_host_coherent = coherent;

	if (!is_host_coherent)
	{
		// If we're not host coherent (missing VK_EXT_external_memory_host),
		// the GPU has its own RDRAM, kept in step with the host's page by page.
		assert(rdram_offset == 0);
		bool full_upload = false;
		// (every page uploaded whole, to compare against)
		if (const char *env = getenv("PARALLEL_RDP_FULL_RDRAM_UPLOAD"))
			full_upload = strtol(env, nullptr, 0) > 0;
		return rdp_coherency_init(&incoherent, device, rdram, host_rdram, size, full_upload);
	}
	rdp_coherency_free(&incoherent);
	return true;
}

void Renderer::set_hidden_rdram(Vulkan::Buffer *buffer)
{
	hidden_rdram = buffer;
}

void Renderer::set_tmem(Vulkan::Buffer *buffer)
{
	tmem = buffer;
}

void Renderer::flush_and_signal()
{
	flush_queues();
	submit_to_queue();
	assert(!stream.cmd);
}

void Renderer::set_color_framebuffer(uint32_t addr, uint32_t width, FBFormat fmt)
{
	if (fb.addr != addr || fb.width != width || fb.fmt != fmt)
		flush_queues();

	fb.addr = addr;
	fb.width = width;
	fb.fmt = fmt;
}

void Renderer::set_depth_framebuffer(uint32_t addr)
{
	if (fb.depth_addr != addr)
		flush_queues();

	fb.depth_addr = addr;
}

void Renderer::set_scissor_state(const ScissorState &state)
{
	queue.scissor = state;
}

void Renderer::set_static_rasterization_state(const StaticRasterizationState &state)
{
	queue.static_state = state;
}

void Renderer::set_depth_blend_state(const DepthBlendState &state)
{
	queue.depth_blend = state;
}

void Renderer::draw_flat_primitive(TriangleSetup &setup)
{
	if (validation_iface)
		validate_draw_state();
	rdp_queue_add_primitive(&queue, &caps, &constants, tiles, &setup, nullptr);
	submits.primitives++;
	if (rdp_queue_full(&queue, &caps))
		flush_queues();
}

// Out of line and cold: draws and loads pay only the test of validation_iface.
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline, cold))
#endif
void Renderer::report_rdp_crash(ValidationError err, const char *msg) const
{
	validation_iface->report_rdp_crash(validation_iface->user, err, msg);
}

void Renderer::validate_draw_state() const
{
	if ((queue.static_state.flags & RASTERIZATION_FILL_BIT) != 0)
	{
		if (fb.fmt == FBFormat::I4)
		{
			report_rdp_crash(ValidationError::Fill4bpp,
			                                   "Attempted to use Fill mode on 4bpp surface.");
		}

		if ((queue.depth_blend.flags & DEPTH_BLEND_DEPTH_TEST_BIT) != 0)
		{
			report_rdp_crash(ValidationError::FillDepthTest,
			                                   "Attempted to use Fill mode with depth test.");
		}

		if ((queue.depth_blend.flags & DEPTH_BLEND_IMAGE_READ_ENABLE_BIT) != 0)
		{
			report_rdp_crash(ValidationError::FillImageReadEnable,
			                                   "Attempted to use Fill mode with image read enable.");
		}

		if ((queue.depth_blend.flags & DEPTH_BLEND_DEPTH_UPDATE_BIT) != 0 &&
		    !constants.use_prim_depth)
		{
			report_rdp_crash(ValidationError::FillDepthWrite,
			                                   "Attempted to use Fill mode with depth write enabled.");
		}
	}
	else if ((queue.static_state.flags & RASTERIZATION_COPY_BIT) != 0)
	{
		if (fb.fmt == FBFormat::RGBA8888)
		{
			report_rdp_crash(ValidationError::Copy32bpp,
			                                   "Attempted to use Copy mode on 32bpp surface.");
		}
	}
}

void Renderer::draw_shaded_primitive(TriangleSetup &setup, const AttributeSetup &attr)
{
	if (validation_iface)
		validate_draw_state();
	rdp_queue_add_primitive(&queue, &caps, &constants, tiles, &setup, &attr);
	submits.primitives++;
	if (rdp_queue_full(&queue, &caps))
		flush_queues();
}

void Renderer::upload_streams(Vulkan::CommandBuffer &cmd)
{
	const void *data[RDP_STREAM_BUFFERS];
	VkDeviceSize bytes[RDP_STREAM_BUFFERS];
	rdp_queue_upload_data(&queue, data, bytes);
	rdp_stream_instance_upload(&buffer_instances[sync_ring.current], device, &cmd, data, bytes);
}

rdp_pass_resources_t Renderer::pass_resources(const rdp_pipeline_setup_t &setup)
{
	rdp_pass_resources_t r;
	r.setup = &setup;
	r.pipelines = &pipelines;
	r.buffers = &buffers;
	r.streams = &buffer_instances[sync_ring.current].gpu;
	r.rdram = rdram;
	r.rdram_offset = rdram_offset;
	r.rdram_size = rdram_size;
	r.hidden_rdram = hidden_rdram;
	r.tmem = tmem;
	return r;
}

rdp_pass_flush_t Renderer::pass_flush() const
{
	rdp_pass_flush_t f;
	f.target = pass_target();
	f.uploads = queue.uploads;
	f.num_uploads = queue.num_uploads;
	f.num_span_jobs = queue.num_span_jobs;
	f.states = queue.static_states;
	f.num_states = queue.num_static_states;
	return f;
}

rdp_pass_target_t Renderer::pass_target() const
{
	rdp_pass_target_t t;
	t.addr = fb.addr;
	t.depth_addr = fb.depth_addr;
	t.width = fb.width;
	t.height = queue.height;
	t.fmt = uint32_t(fb.fmt);
	t.base_primitive_index = base_primitive_index;
	t.num_primitives = queue.num_primitives;
	return t;
}

void Renderer::clear_indirect_buffer(Vulkan::CommandBuffer &cmd)
{
	static_assert((Limits::MaxStaticRasterizationStates % ImplementationConstants::DefaultWorkgroupSize) == 0,
	              "MaxStaticRasterizationStates does not align.");
	const rdp_pipeline_setup_t setup = pipeline_setup();
	const rdp_pass_resources_t r = pass_resources(setup);
	rdp_pass_clear_indirect(&r, &cmd);
}

void Renderer::submit_update_upscaled_domain_external(Vulkan::CommandBuffer &cmd,
                                                      unsigned addr, unsigned length, unsigned pixel_size_log2)
{
	submit_update_upscaled_domain(cmd, RDP_RESOLVE_PRE, addr, addr, length, 1, pixel_size_log2);
}

void Renderer::submit_update_upscaled_domain(Vulkan::CommandBuffer &cmd, rdp_resolve_stage stage,
                                             unsigned addr, unsigned depth_addr,
                                             unsigned width, unsigned height,
                                             unsigned pixel_size_log2)
{
	const rdp_pipeline_setup_t setup = pipeline_setup();
	const rdp_pass_resources_t r = pass_resources(setup);
	rdp_pass_update_upscaled_domain(&r, &cmd, stage, addr, depth_addr, width, height, pixel_size_log2);
}

void Renderer::maintain_queues()
{
	if (rdp_submit_due(&submits, caps.super_sample_readback, retro_atomic_load_relaxed_int(&active_submissions) == 0,
	                   granite_time_nsecs() - last_submit_ns))
		submit_to_queue();
}

void Renderer::maintain_queues_idle()
{
	if (rdp_submit_idle_due(&submits))
	{
		flush_queues();
		submit_to_queue();
	}
}

void Renderer::enqueue_fence_wait(Vulkan::Fence fence)
{
	CoherencyOperation op;
	op.fence = rdp_move(fence);
	op.unlock_cookie = &active_submissions;
	retro_atomic_fetch_add_int(&active_submissions, 1);
	processor.enqueue_coherency_operation(rdp_move(op));
	last_submit_ns = granite_time_nsecs();
}

void Renderer::submit_to_queue()
{
	const rdp_submit_taken_t taken = rdp_submit_take(&submits, caps.super_sample_readback);
	const bool pending_host_visible_render_passes = taken.host_visible;

	if (!stream.cmd)
	{
		if (pending_host_visible_render_passes)
		{
			Vulkan::Fence fence;
			device->submit_empty(Vulkan::CommandBuffer::Type::AsyncCompute, &fence);
			enqueue_fence_wait(fence);
		}
		return;
	}

	rdp_submit_end_barrier(&*stream.cmd, &taken, is_host_coherent || !incoherent.staging_readback);

	Vulkan::Fence fence;

	if (is_host_coherent)
	{
		device->submit(stream.cmd, &fence);
		if (pending_host_visible_render_passes)
			enqueue_fence_wait(fence);
	}
	else
	{
		CoherencyOperation op;
		if (pending_host_visible_render_passes)
			resolve_coherency_gpu_to_host(op, *stream.cmd);

		device->submit(stream.cmd, &fence);

		if (pending_host_visible_render_passes)
		{
			enqueue_fence_wait(fence);
			op.fence = fence;
			if (op.num_copies)
				processor.enqueue_coherency_operation(rdp_move(op));
		}
	}

	rdp_sync_ring_submitted(&sync_ring, fence.get());
	stream.cmd.reset();
}

void Renderer::reset_context()
{
	rdp_queue_reset(&queue);
}

void Renderer::begin_new_context()
{
	rdp_sync_ring_advance(&sync_ring);
	reset_context();
	bind_stream_instance();
}

// The streams of the next context are written straight into this
// instance's upload buffers, so the GPU must be done with what it last
// read from them before the first primitive is added, not at the flush.
void Renderer::bind_stream_instance()
{
	auto &instance = buffer_instances[sync_ring.current];

	if (rdp_sync_ring_unsubmitted(&sync_ring, sync_ring.current))
		submit_to_queue();
	rdp_sync_ring_wait(&sync_ring, sync_ring.current);

	// The upload buffers are host-visible, so they always map.
	// (the state caches are copied in at the flush, so only the streams)
	static const rdp_stream_buffer streams[] = {
		RDP_STREAM_TRIANGLE_SETUP, RDP_STREAM_ATTRIBUTE_SETUP, RDP_STREAM_DERIVED_SETUP,
		RDP_STREAM_SCISSOR_SETUP, RDP_STREAM_STATE_INDICES, RDP_STREAM_SPAN_INFO_OFFSETS,
		RDP_STREAM_SPAN_INFO_JOBS,
	};
	void *mapped[RDP_STREAM_BUFFERS] = {};
	for (auto i : streams)
		mapped[i] = device->map_host_buffer(buf(instance.cpu.buffers[i]), Vulkan::MEMORY_ACCESS_WRITE_BIT);
	rdp_queue_bind(&queue, mapped);
}

uint32_t Renderer::get_byte_size_for_bound_color_framebuffer() const
{
	unsigned pixel_count = fb.width * queue.height;
	unsigned byte_count;
	switch (fb.fmt)
	{
	case FBFormat::RGBA8888:
		byte_count = pixel_count * 4;
		break;

	case FBFormat::RGBA5551:
	case FBFormat::IA88:
		byte_count = pixel_count * 2;
		break;

	default:
		byte_count = pixel_count;
		break;
	}

	return byte_count;
}

uint32_t Renderer::get_byte_size_for_bound_depth_framebuffer() const
{
	return fb.width * queue.height * 2;
}

void Renderer::mark_pages_for_gpu_read(uint32_t base_addr, uint32_t byte_count)
{
	rdp_coherency_mark_read(&incoherent, base_addr, byte_count);
}

void Renderer::lock_pages_for_gpu_write(uint32_t base_addr, uint32_t byte_count)
{
	rdp_coherency_lock_write(&incoherent, base_addr, byte_count);
}

void Renderer::resolve_coherency_gpu_to_host(CoherencyOperation &op, Vulkan::CommandBuffer &cmd)
{
	const granite_buffer_t *src;
	op.copies = incoherent.copies;
	op.num_copies = rdp_coherency_read_back(&incoherent, &cmd, &src);
	op.src = static_cast<const Vulkan::Buffer *>(src);
	op.dst = incoherent.host_rdram;
	op.timeline_value = 0;
}

void Renderer::resolve_coherency_external(unsigned offset, unsigned length)
{
	mark_pages_for_gpu_read(offset, length);
	ensure_command_buffer();
	resolve_coherency_host_to_gpu(*stream.cmd);
	device->submit(stream.cmd);
	stream.cmd.reset();
}

unsigned Renderer::get_scaling_factor() const
{
	return caps.upscaling;
}

const Vulkan::Buffer *Renderer::get_upscaled_rdram_buffer() const
{
	return static_cast<Vulkan::Buffer *>(buffers.upscaling_multisampled_rdram);
}

const Vulkan::Buffer *Renderer::get_upscaled_hidden_rdram_buffer() const
{
	return static_cast<Vulkan::Buffer *>(buffers.upscaling_multisampled_hidden_rdram);
}

void Renderer::resolve_coherency_host_to_gpu(Vulkan::CommandBuffer &cmd)
{
	rdp_coherency_upload(&incoherent, &cmd, shader_bank);
}

void Renderer::flush_queues()
{
	if (queue.num_uploads == 0 && queue.num_span_jobs == 0)
	{
		base_primitive_index += queue.num_primitives;
		reset_context();
		return;
	}

	if (!is_host_coherent)
	{
		mark_pages_for_gpu_read(fb.addr, get_byte_size_for_bound_color_framebuffer());
		mark_pages_for_gpu_read(fb.depth_addr, get_byte_size_for_bound_depth_framebuffer());

		// We're going to write to these pages, so lock them down.
		lock_pages_for_gpu_write(fb.addr, get_byte_size_for_bound_color_framebuffer());
		lock_pages_for_gpu_write(fb.depth_addr, get_byte_size_for_bound_depth_framebuffer());
	}

	// bind_stream_instance waited for this instance's last use
	rdp_sync_ring_use(&sync_ring);

	ensure_command_buffer();

	if (!is_host_coherent)
		resolve_coherency_host_to_gpu(*stream.cmd);
	upload_streams(*stream.cmd);

	const rdp_pipeline_setup_t setup = pipeline_setup();
	const rdp_pass_flush_t flush = pass_flush();

	// With the super-sampled read-back only the upscaled passes run.
	bool has_single_sampled_render_pass = !caps.super_sample_readback;

	if (has_single_sampled_render_pass)
	{
		const rdp_pass_resources_t r = pass_resources(setup);
		rdp_pass_record_native(&r, &*stream.cmd, &flush);
		submits.passes++;
	}

	if (rdp_pass_flush_upscaled(&caps, &flush))
	{
		if (has_single_sampled_render_pass)
		{
			maintain_queues();
			ensure_command_buffer();

			// We're going to keep reading the same data structures, so make sure
			// we signal fence after upscaled render pass is submitted.
			rdp_sync_ring_use(&sync_ring);
		}

		const rdp_pass_resources_t r = pass_resources(setup);
		rdp_pass_record_upscaled(&r, &*stream.cmd, &flush);
		submits.passes_upscaled++;
		submits.primitives_upscaled += queue.num_primitives;
	}

	base_primitive_index += queue.num_primitives;
	rdp_pass_record_end(&*stream.cmd);

	begin_new_context();
	maintain_queues();
}

void Renderer::ensure_command_buffer()
{
	if (!stream.cmd)
		stream.cmd = device->request_command_buffer(Vulkan::CommandBuffer::Type::AsyncCompute);

	if (!caps.ubershader && !indirect_cleared)
	{
		indirect_cleared = true;
		clear_indirect_buffer(*stream.cmd);
		stream.cmd->barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
		                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT);
	}
}

void Renderer::set_tile(uint32_t tile, const TileMeta &meta)
{
	tiles[tile].meta = meta;
}

void Renderer::set_tile_size(uint32_t tile, uint32_t slo, uint32_t shi, uint32_t tlo, uint32_t thi)
{
	tiles[tile].size.slo = slo;
	tiles[tile].size.shi = shi;
	tiles[tile].size.tlo = tlo;
	tiles[tile].size.thi = thi;
}

void Renderer::notify_idle_command_thread()
{
	maintain_queues_idle();
}

bool Renderer::tmem_upload_needs_flush(uint32_t addr) const
{
	rdp_queue_fb_t qfb;
	qfb.addr = fb.addr;
	qfb.depth_addr = fb.depth_addr;
	qfb.width = fb.width;
	qfb.fmt = uint32_t(fb.fmt);
	return rdp_queue_upload_reads_pending(&queue, &qfb, uint32_t(rdram_size), addr);
}

void Renderer::load_tile(uint32_t tile, const LoadTileInfo &info)
{
	if (validation_iface && info.mode == RDP_UPLOAD_TLUT && (info.thi >> 2) > (info.tlo >> 2))
		report_rdp_crash(ValidationError::InvalidMultilineLoadTlut, "Attempting to load multiple lines in TLUT.");

	if (tmem_upload_needs_flush(info.tex_addr))
		flush_queues();

	if (rdp_load_is_empty(&info))
		return;

	if (!is_host_coherent)
	{
		uint32_t addr, size;
		rdp_load_rdram_range(&info, &addr, &size);
		mark_pages_for_gpu_read(addr, size);
	}

	unsigned crashes = rdp_queue_load_tile(&queue, tiles, tile, &info, flush_for_queue, this);
	if (validation_iface)
		while (crashes--)
			report_rdp_crash(ValidationError::LoadTile4bpp, "4-bit VRAM pointer crashes the RDP.");
}

void Renderer::flush_for_queue(void *renderer)
{
	static_cast<Renderer *>(renderer)->flush_queues();
}

void Renderer::set_blend_color(uint32_t color)
{
	constants.blend_color = color;
}

void Renderer::set_fog_color(uint32_t color)
{
	constants.fog_color = color;
}

void Renderer::set_env_color(uint32_t color)
{
	constants.env_color = color;
}

void Renderer::set_fill_color(uint32_t color)
{
	constants.fill_color = color;
}

void Renderer::set_primitive_depth(uint16_t prim_depth, uint16_t prim_dz)
{
	constants.prim_depth = int32_t(prim_depth & 0x7fff) << 16;
	constants.prim_dz = prim_dz;
}

void Renderer::set_enable_primitive_depth(bool enable)
{
	constants.use_prim_depth = enable;
}

void Renderer::set_convert(uint16_t k0, uint16_t k1, uint16_t k2, uint16_t k3, uint16_t k4, uint16_t k5)
{
	constants.convert[0] = 2 * sext<9>(k0) + 1;
	constants.convert[1] = 2 * sext<9>(k1) + 1;
	constants.convert[2] = 2 * sext<9>(k2) + 1;
	constants.convert[3] = 2 * sext<9>(k3) + 1;
	constants.convert[4] = k4;
	constants.convert[5] = k5;
}

void Renderer::set_color_key(unsigned component, uint32_t width, uint32_t center, uint32_t scale)
{
	constants.key_width[component] = width;
	constants.key_center[component] = center;
	constants.key_scale[component] = scale;
}

void Renderer::set_primitive_color(uint8_t min_level, uint8_t prim_lod_frac, uint32_t color)
{
	constants.primitive_color = color;
	constants.min_level = min_level;
	constants.prim_lod_frac = prim_lod_frac;
}

rdp_pipeline_setup_t Renderer::pipeline_setup() const
{
	rdp_pipeline_setup_t setup;
	setup.caps = &caps;
	setup.bank = shader_bank;
	setup.rdram_size = uint32_t(rdram_size);
	setup.host_coherent = is_host_coherent;
	setup.subgroup_size = device->get_device_features().subgroup_properties.subgroupSize;
	return setup;
}

void Renderer::build_pipelines_ahead(const pipeline_record_t *records, size_t count, VideoInterface *vi, bool with_upscaler)
{
	const rdp_pipeline_setup_t setup = pipeline_setup();
	rdp_pipeline_builder_ahead(&pipelines, &setup, records, count, vi ? vi->get_vi() : nullptr, with_upscaler);
}

void Renderer::wait_pipelines_ahead_for_test()
{
	rdp_pipeline_builder_wait(&pipelines);
}

bool Renderer::pipelines_ahead_ready()
{
	return rdp_pipeline_builder_ready(&pipelines);
}
}
