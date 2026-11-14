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
			sizeof(InstanceIndices) * Limits::MaxPrimitives,
			sizeof(SpanInfoOffsets) * Limits::MaxPrimitives,
			sizeof(SpanInterpolationJob) * Limits::MaxSpanSetups,
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
	stream.scissor_state = state;
}

void Renderer::set_static_rasterization_state(const StaticRasterizationState &state)
{
	stream.static_raster_state = state;
}

void Renderer::set_depth_blend_state(const DepthBlendState &state)
{
	stream.depth_blend_state = state;
}

void Renderer::draw_flat_primitive(TriangleSetup &setup)
{
	AttributeSetup attr = {};
	draw_shaded_primitive(setup, attr);
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
	if ((stream.static_raster_state.flags & RASTERIZATION_FILL_BIT) != 0)
	{
		if (fb.fmt == FBFormat::I4)
		{
			report_rdp_crash(ValidationError::Fill4bpp,
			                                   "Attempted to use Fill mode on 4bpp surface.");
		}

		if ((stream.depth_blend_state.flags & DEPTH_BLEND_DEPTH_TEST_BIT) != 0)
		{
			report_rdp_crash(ValidationError::FillDepthTest,
			                                   "Attempted to use Fill mode with depth test.");
		}

		if ((stream.depth_blend_state.flags & DEPTH_BLEND_IMAGE_READ_ENABLE_BIT) != 0)
		{
			report_rdp_crash(ValidationError::FillImageReadEnable,
			                                   "Attempted to use Fill mode with image read enable.");
		}

		if ((stream.depth_blend_state.flags & DEPTH_BLEND_DEPTH_UPDATE_BIT) != 0 &&
		    !constants.use_prim_depth)
		{
			report_rdp_crash(ValidationError::FillDepthWrite,
			                                   "Attempted to use Fill mode with depth write enabled.");
		}
	}
	else if ((stream.static_raster_state.flags & RASTERIZATION_COPY_BIT) != 0)
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

	rdp_fixup_triangle_setup(&setup, stream.static_raster_state.flags);

	unsigned num_tiles = rdp_conservative_tile_count(&setup, &stream.scissor_state, caps.upscaling);

#if 0
	// Don't exit early, throws off seeding of noise channels.
	if (!num_tiles)
		return;
#endif

	if (!caps.ubershader)
		stream.max_shaded_tiles += num_tiles;

	update_deduced_height(setup);
	stream.span_info_offsets.add(allocate_span_jobs(setup));
	stream.triangle_setup.add(setup);

	if (constants.use_prim_depth)
	{
		auto tmp_attr = attr;
		tmp_attr.z = constants.prim_depth;
		tmp_attr.dzdx = 0;
		tmp_attr.dzde = 0;
		tmp_attr.dzdy = 0;
		stream.attribute_setup.add(tmp_attr);
	}
	else
	{
		stream.attribute_setup.add(attr);
	}

	DerivedSetup derived;
	rdp_derived_setup(&constants, &stream.static_raster_state, &attr, &derived);
	stream.derived_setup.add(derived);
	stream.scissor_setup.add(stream.scissor_state);

	rdp_deduce_static_texture_state(&stream.static_raster_state, tiles, setup.tile & 7, setup.tile >> 3);
	rdp_deduce_noise_state(&stream.static_raster_state);

	InstanceIndices indices = {};
	StaticRasterizationState normalized = stream.static_raster_state;
	rdp_normalize_static_state(&normalized);
	indices.static_index = stream.static_raster_state_cache.add(normalized);
	indices.depth_blend_index = stream.depth_blend_state_cache.add(stream.depth_blend_state);
	indices.tile_instance_index = uint8_t(stream.num_tmem_uploads);
	for (unsigned i = 0; i < 8; i++)
		indices.tile_indices[i] = stream.tile_info_state_cache.add(tiles[i]);
	stream.state_indices.add(indices);

	fb.color_write_pending = true;
	if (stream.depth_blend_state.flags & DEPTH_BLEND_DEPTH_UPDATE_BIT)
		fb.depth_write_pending = true;
	pending_primitives++;

	if (need_flush())
		flush_queues();
}

SpanInfoOffsets Renderer::allocate_span_jobs(const TriangleSetup &setup)
{
	int min_active_sub_scanline = rdp_max(int(setup.yh), int(stream.scissor_state.ylo));
	int min_active_line = min_active_sub_scanline >> 2;

	int max_active_sub_scanline = rdp_min(setup.yl - 1, int(stream.scissor_state.yhi) - 1);
	int max_active_line = max_active_sub_scanline >> 2;

	if (max_active_line < min_active_line)
		return { 0, 0, -1, 0 };

	// Need to poke into next scanline validation for certain workarounds.
	int height = rdp_max(max_active_line - min_active_line + 2, 0);
	height = rdp_min(height, 1024);

	int num_jobs = (height + ImplementationConstants::DefaultWorkgroupSize - 1) / ImplementationConstants::DefaultWorkgroupSize;

	SpanInfoOffsets offsets = {};
	offsets.offset = uint32_t(stream.span_info_jobs.size()) * ImplementationConstants::DefaultWorkgroupSize;
	offsets.ylo = min_active_line;
	offsets.yhi = max_active_line;

	for (int i = 0; i < num_jobs; i++)
	{
		SpanInterpolationJob interpolation_job = {};
		interpolation_job.primitive_index = uint32_t(stream.triangle_setup.size());
		interpolation_job.base_y = min_active_line + ImplementationConstants::DefaultWorkgroupSize * i;
		interpolation_job.max_y = max_active_line + 1;
		stream.span_info_jobs.add(interpolation_job);
	}
	return offsets;
}

void Renderer::update_deduced_height(const TriangleSetup &setup)
{
	int max_active_sub_scanline = rdp_min(setup.yl - 1, int(stream.scissor_state.yhi) - 1);
	int max_active_line = max_active_sub_scanline >> 2;
	int height = rdp_max(max_active_line + 1, 0);
	fb.deduced_height = rdp_max(fb.deduced_height, uint32_t(height));
}

bool Renderer::need_flush() const
{
	bool cache_full =
			stream.static_raster_state_cache.size() >= caps.max_static_raster_states ||
			stream.depth_blend_state_cache.full() ||
			(stream.tile_info_state_cache.size() + 8 > Limits::MaxTileInfoStates);

	bool triangle_full =
			stream.triangle_setup.full();
	bool span_info_full =
			(stream.span_info_jobs.size() * ImplementationConstants::DefaultWorkgroupSize + Limits::MaxHeight > Limits::MaxSpanSetups);
	bool max_shaded_tiles =
			(stream.max_shaded_tiles + caps.max_tiles_x * caps.max_tiles_y > caps.max_num_tile_instances);

#ifdef VULKAN_DEBUG
	if (cache_full)
		LOGI("Cache is full.\n");
	if (triangle_full)
		LOGI("Triangle is full.\n");
	if (span_info_full)
		LOGI("Span info is full.\n");
	if (max_shaded_tiles)
		LOGI("Shaded tiles is full.\n");
#endif

	return cache_full || triangle_full || span_info_full || max_shaded_tiles;
}

void Renderer::upload_streams(Vulkan::CommandBuffer &cmd)
{
	const void *const data[RDP_STREAM_BUFFERS] = {
		stream.triangle_setup.data(), stream.attribute_setup.data(), stream.derived_setup.data(),
		stream.scissor_setup.data(), stream.static_raster_state_cache.data(),
		stream.depth_blend_state_cache.data(), stream.tile_info_state_cache.data(),
		stream.state_indices.data(), stream.span_info_offsets.data(), stream.span_info_jobs.data(),
	};
	const VkDeviceSize bytes[RDP_STREAM_BUFFERS] = {
		stream.triangle_setup.byte_size(), stream.attribute_setup.byte_size(), stream.derived_setup.byte_size(),
		stream.scissor_setup.byte_size(), stream.static_raster_state_cache.byte_size(),
		stream.depth_blend_state_cache.byte_size(), stream.tile_info_state_cache.byte_size(),
		stream.state_indices.byte_size(), stream.span_info_offsets.byte_size(), stream.span_info_jobs.byte_size(),
	};
	rdp_stream_instance_upload(&buffer_instances[buffer_instance], device, &cmd, data, bytes);
}

rdp_pass_resources_t Renderer::pass_resources(const rdp_pipeline_setup_t &setup)
{
	rdp_pass_resources_t r;
	r.setup = &setup;
	r.pipelines = &pipelines;
	r.buffers = &buffers;
	r.streams = &buffer_instances[buffer_instance].gpu;
	r.rdram = rdram;
	r.rdram_offset = rdram_offset;
	r.rdram_size = rdram_size;
	r.hidden_rdram = hidden_rdram;
	r.tmem = tmem;
	return r;
}

rdp_pass_target_t Renderer::pass_target() const
{
	rdp_pass_target_t t;
	t.addr = fb.addr;
	t.depth_addr = fb.depth_addr;
	t.width = fb.width;
	t.height = fb.deduced_height;
	t.fmt = uint32_t(fb.fmt);
	t.base_primitive_index = base_primitive_index;
	t.num_primitives = uint32_t(stream.triangle_setup.size());
	return t;
}

void Renderer::update_tmem_instances(Vulkan::CommandBuffer &cmd)
{
	const rdp_pipeline_setup_t setup = pipeline_setup();
	const rdp_pass_resources_t r = pass_resources(setup);
	rdp_pass_tmem_update(&r, &cmd, stream.tmem_upload_infos, stream.num_tmem_uploads);
}

void Renderer::submit_span_setup_jobs(Vulkan::CommandBuffer &cmd, bool upscale)
{
	const rdp_pipeline_setup_t setup = pipeline_setup();
	const rdp_pass_resources_t r = pass_resources(setup);
	rdp_pass_span_setup(&r, &cmd, upscale, uint32_t(stream.span_info_jobs.size()));
}

void Renderer::clear_indirect_buffer(Vulkan::CommandBuffer &cmd)
{
	static_assert((Limits::MaxStaticRasterizationStates % ImplementationConstants::DefaultWorkgroupSize) == 0,
	              "MaxStaticRasterizationStates does not align.");
	const rdp_pipeline_setup_t setup = pipeline_setup();
	const rdp_pass_resources_t r = pass_resources(setup);
	rdp_pass_clear_indirect(&r, &cmd);
}

void Renderer::submit_rasterization(Vulkan::CommandBuffer &cmd, Vulkan::Buffer &tmem_, bool upscaling)
{
	const rdp_pipeline_setup_t setup = pipeline_setup();
	const rdp_pass_resources_t r = pass_resources(setup);
	const rdp_pass_target_t t = pass_target();
	rdp_pass_rasterize(&r, &cmd, &t, &tmem_, upscaling, stream.static_raster_state_cache.data(),
	                   uint32_t(stream.static_raster_state_cache.size()));
}

void Renderer::submit_tile_binning_combined(Vulkan::CommandBuffer &cmd, bool upscale)
{
	const rdp_pipeline_setup_t setup = pipeline_setup();
	const rdp_pass_resources_t r = pass_resources(setup);
	const rdp_pass_target_t t = pass_target();
	rdp_pass_tile_binning(&r, &cmd, &t, upscale);
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

void Renderer::submit_clear_super_sample_write_mask(Vulkan::CommandBuffer &cmd, unsigned width, unsigned height)
{
	const rdp_pipeline_setup_t setup = pipeline_setup();
	const rdp_pass_resources_t r = pass_resources(setup);
	rdp_pass_clear_super_sample_write_mask(&r, &cmd, width, height);
}

void Renderer::submit_update_upscaled_domain(Vulkan::CommandBuffer &cmd, rdp_resolve_stage stage)
{
	submit_update_upscaled_domain(cmd, stage, fb.addr, fb.depth_addr,
	                              fb.width, fb.deduced_height, rdp_pass_pixel_size_log2(uint32_t(fb.fmt)));
}

void Renderer::submit_depth_blend(Vulkan::CommandBuffer &cmd, Vulkan::Buffer &tmem_, bool upscaled, bool force_write_mask)
{
	const rdp_pipeline_setup_t setup = pipeline_setup();
	const rdp_pass_resources_t r = pass_resources(setup);
	const rdp_pass_target_t t = pass_target();
	rdp_pass_depth_blend(&r, &cmd, &t, &tmem_, upscaled, force_write_mask);
}

void Renderer::submit_render_pass(Vulkan::CommandBuffer &cmd)
{
	bool need_render_pass = fb.width != 0 && fb.deduced_height != 0 && !stream.span_info_jobs.empty();
	bool need_tmem_upload = stream.num_tmem_uploads != 0;
	bool need_submit = need_render_pass || need_tmem_upload;
	if (!need_submit)
		return;

	// Here we run 3 dispatches in parallel. Span setup and TMEM instances are low occupancy kind of jobs, but the binning
	// pass should dominate here unless the workload is trivial.
	if (need_render_pass)
	{
		submit_span_setup_jobs(cmd, false);
		submit_tile_binning_combined(cmd, false);
		if (caps.upscaling > 1)
			submit_update_upscaled_domain(cmd, RDP_RESOLVE_PRE);
	}

	if (need_tmem_upload)
		update_tmem_instances(cmd);

	cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
	            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | (!caps.ubershader ? VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT : 0),
	            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
	            (!caps.ubershader ? VK_ACCESS_INDIRECT_COMMAND_READ_BIT : 0));

	if (need_render_pass && !caps.ubershader)
	{
		submit_rasterization(cmd, need_tmem_upload ? buf(buffers.tmem_instances) : *tmem, false);
		cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
		            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
	}

	if (need_render_pass)
		submit_depth_blend(cmd, need_tmem_upload ? buf(buffers.tmem_instances) : *tmem, false, false);

	if (!caps.ubershader)
		clear_indirect_buffer(cmd);

	if (render_pass_is_upscaled())
	{
		cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
		            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

		// TODO: Could probably do this reference update in the render pass itself,
		// just write output to two buffers ... This is more composable for now.
		submit_update_upscaled_domain(cmd, RDP_RESOLVE_POST);
	}
}

void Renderer::submit_render_pass_upscaled(Vulkan::CommandBuffer &cmd)
{

	bool need_tmem_upload = stream.num_tmem_uploads != 0;

	submit_span_setup_jobs(cmd, true);
	submit_tile_binning_combined(cmd, true);
	if (caps.super_sample_readback)
	{
		submit_update_upscaled_domain(cmd, RDP_RESOLVE_PRE);
		submit_clear_super_sample_write_mask(cmd, fb.width, fb.deduced_height);
		if (need_tmem_upload)
			update_tmem_instances(cmd);
	}

	cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
	            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
	            (!caps.ubershader ? VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT : 0),
	            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
	            (!caps.ubershader ? VK_ACCESS_INDIRECT_COMMAND_READ_BIT : 0));

	if (!caps.ubershader)
	{
		submit_rasterization(cmd, need_tmem_upload ? buf(buffers.tmem_instances) : *tmem, true);
		cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		            VK_ACCESS_SHADER_WRITE_BIT,
		            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		            VK_ACCESS_SHADER_READ_BIT);
	}

	submit_depth_blend(cmd, need_tmem_upload ? buf(buffers.tmem_instances) : *tmem, true, caps.super_sample_readback);
	if (!caps.ubershader)
		clear_indirect_buffer(cmd);

	if (caps.super_sample_readback)
	{
		cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
		            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

		submit_update_upscaled_domain(cmd, RDP_RESOLVE_SSAA);
	}

}

void Renderer::submit_render_pass_end(Vulkan::CommandBuffer &cmd)
{
	base_primitive_index += uint32_t(stream.triangle_setup.size());
	cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
	            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
	            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
}

void Renderer::maintain_queues()
{
	// Some conditions dictate if we should flush a render pass.
	// These heuristics ensures we don't wait too long to flush render passes,
	// and also ensure that we don't spam submissions too often, causing massive bubbles on GPU.

	// If we get a lot of small render passes in a row, it makes sense to batch them up, e.g. 8 at a time.
	// If we get 2 full render passes of ~256 primitives, that's also a good indication we should flush since we're getting spammed.
	// If we have no pending submissions, the GPU is idle and there is no reason not to submit.
	// If we haven't submitted anything in a while (1.0 ms), it's probably fine to submit again.
	if (pending_render_passes >= ImplementationConstants::MaxPendingRenderPassesBeforeFlush ||
	    (caps.super_sample_readback && pending_render_passes_upscaled >= ImplementationConstants::MaxPendingRenderPassesBeforeFlush) ||
	    pending_primitives >= Limits::MaxPrimitives ||
	    pending_primitives_upscaled >= Limits::MaxPrimitives ||
	    retro_atomic_load_relaxed_int(&active_submissions) == 0 ||
	    int64_t(granite_time_nsecs() - last_submit_ns) > 1000000)
	{
		submit_to_queue();
	}
}

void Renderer::maintain_queues_idle()
{
	if (pending_primitives >= ImplementationConstants::MinimumPrimitivesForIdleFlush ||
	    pending_render_passes >= ImplementationConstants::MinimumRenderPassesForIdleFlush)
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
	bool pending_host_visible_render_passes =
			(caps.super_sample_readback ? pending_render_passes_upscaled : pending_render_passes) != 0;
	bool pending_upscaled_passes = pending_render_passes_upscaled != 0;
	pending_render_passes = 0;
	pending_render_passes_upscaled = 0;
	pending_primitives = 0;
	pending_primitives_upscaled = 0;

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

	bool need_host_barrier = is_host_coherent || !incoherent.staging_readback;

	// If we maintain queues in-between doing 1x render pass and upscaled render pass,
	// we haven't flushed memory yet.
	bool need_memory_flush = pending_host_visible_render_passes && !pending_upscaled_passes;
	stream.cmd->barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
	                    need_memory_flush ? VK_ACCESS_MEMORY_WRITE_BIT : 0,
	                    (need_host_barrier ? VK_PIPELINE_STAGE_HOST_BIT : VK_PIPELINE_STAGE_TRANSFER_BIT),
	                    (need_host_barrier ? VK_ACCESS_HOST_READ_BIT : VK_ACCESS_TRANSFER_READ_BIT));

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

	for (uint32_t bit_bits = sync_indices_needs_flush, bit; Util::next_bit(bit_bits, bit);)
	{
		auto &sync = internal_sync[bit];
		sync.fence = fence;
	}
	sync_indices_needs_flush = 0;
	stream.cmd.reset();
}

void Renderer::reset_context()
{
	stream.scissor_setup.reset();
	stream.static_raster_state_cache.reset();
	stream.depth_blend_state_cache.reset();
	stream.tile_info_state_cache.reset();
	stream.triangle_setup.reset();
	stream.attribute_setup.reset();
	stream.derived_setup.reset();
	stream.state_indices.reset();
	stream.span_info_offsets.reset();
	stream.span_info_jobs.reset();
	stream.max_shaded_tiles = 0;

	fb.deduced_height = 0;
	fb.color_write_pending = false;
	fb.depth_write_pending = false;

	stream.num_tmem_uploads = 0;
}

void Renderer::begin_new_context()
{
	buffer_instance = (buffer_instance + 1) % Limits::NumSyncStates;
	reset_context();
	bind_stream_instance();
}

// The streams of the next context are written straight into this
// instance's upload buffers, so the GPU must be done with what it last
// read from them before the first primitive is added, not at the flush.
void Renderer::bind_stream_instance()
{
	auto &instance = buffer_instances[buffer_instance];
	auto &sync = internal_sync[buffer_instance];

	if (sync_indices_needs_flush & (1u << buffer_instance))
		submit_to_queue();

	if (sync.fence)
	{
		sync.fence->wait();
		sync.fence.reset();
	}

	// The upload buffers are host-visible, so they always map.
	const Vulkan::MemoryAccessFlags w = Vulkan::MEMORY_ACCESS_WRITE_BIT;
	stream.triangle_setup.bind(device->map_host_buffer(buf(instance.cpu.buffers[RDP_STREAM_TRIANGLE_SETUP]), w));
	stream.scissor_setup.bind(device->map_host_buffer(buf(instance.cpu.buffers[RDP_STREAM_SCISSOR_SETUP]), w));
	stream.attribute_setup.bind(device->map_host_buffer(buf(instance.cpu.buffers[RDP_STREAM_ATTRIBUTE_SETUP]), w));
	stream.derived_setup.bind(device->map_host_buffer(buf(instance.cpu.buffers[RDP_STREAM_DERIVED_SETUP]), w));
	stream.state_indices.bind(device->map_host_buffer(buf(instance.cpu.buffers[RDP_STREAM_STATE_INDICES]), w));
	stream.span_info_offsets.bind(device->map_host_buffer(buf(instance.cpu.buffers[RDP_STREAM_SPAN_INFO_OFFSETS]), w));
	stream.span_info_jobs.bind(device->map_host_buffer(buf(instance.cpu.buffers[RDP_STREAM_SPAN_INFO_JOBS]), w));
}

uint32_t Renderer::get_byte_size_for_bound_color_framebuffer() const
{
	unsigned pixel_count = fb.width * fb.deduced_height;
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
	return fb.width * fb.deduced_height * 2;
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
	if (stream.num_tmem_uploads == 0 && stream.span_info_jobs.empty())
	{
		base_primitive_index += stream.triangle_setup.size();
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
	auto &instance = buffer_instances[buffer_instance];
	sync_indices_needs_flush |= 1u << buffer_instance;

	ensure_command_buffer();

	if (!is_host_coherent)
		resolve_coherency_host_to_gpu(*stream.cmd);
	upload_streams(*stream.cmd);

	// If we have super-sampled readback, then this is meaningless.
	bool has_single_sampled_render_pass = !caps.super_sample_readback;

	if (has_single_sampled_render_pass)
	{
		submit_render_pass(*stream.cmd);
		pending_render_passes++;
	}

	if (render_pass_is_upscaled())
	{
		if (has_single_sampled_render_pass)
		{
			maintain_queues();
			ensure_command_buffer();

			// We're going to keep reading the same data structures, so make sure
			// we signal fence after upscaled render pass is submitted.
			sync_indices_needs_flush |= 1u << buffer_instance;
		}

		submit_render_pass_upscaled(*stream.cmd);
		pending_render_passes_upscaled++;
		pending_primitives_upscaled += uint32_t(stream.triangle_setup.size());
	}

	submit_render_pass_end(*stream.cmd);

	begin_new_context();
	maintain_queues();
}

bool Renderer::render_pass_is_upscaled() const
{
	if (caps.super_sample_readback)
		return true;

	bool need_render_pass = fb.width != 0 && fb.deduced_height != 0 && !stream.span_info_jobs.empty();
	return need_render_pass && should_render_upscaled();
}

bool Renderer::should_render_upscaled() const
{
	if (caps.upscaling > 1)
	{
		// A heuristic. There is no point to render upscaled for purely off-screen passes.
		// We should ideally only upscale the final pass which hits screen.
		// From a heuristic point-of-view we expect only 16-bit/32-bit frame buffers to be relevant,
		// and only frame buffers with at least 256 pixels.
		return (fb.fmt == FBFormat::RGBA5551 || fb.fmt == FBFormat::RGBA8888) && fb.width >= 256;
	}
	else
		return false;
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
	// Not perfect, since TMEM upload could slice into framebuffer,
	// but I doubt this will be an issue (famous last words ...)

	if (fb.color_write_pending)
	{
		uint32_t offset = (addr - fb.addr) & (rdram_size - 1);
		uint32_t pending_pixels = fb.deduced_height * fb.width;

		switch (fb.fmt)
		{
		case FBFormat::RGBA5551:
		case FBFormat::I8:
			offset >>= 1;
			break;

		case FBFormat::RGBA8888:
			offset >>= 2;
			break;

		default:
			break;
		}

		if (offset < pending_pixels)
		{
			//LOGI("Flushing render pass due to coherent TMEM fetch from color buffer.\n");
			return true;
		}
	}

	if (fb.depth_write_pending)
	{
		uint32_t offset = (addr - fb.depth_addr) & (rdram_size - 1);
		uint32_t pending_pixels = fb.deduced_height * fb.width;
		offset >>= 1;

		if (offset < pending_pixels)
		{
			//LOGI("Flushing render pass due to coherent TMEM fetch from depth buffer.\n");
			return true;
		}
	}

	return false;
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

	unsigned lines_per_part = rdp_load_lines_per_part(&info, &tiles[tile].meta);
	if (lines_per_part == RDP_LOAD_TOO_WIDE)
		return;

	if (lines_per_part == RDP_LOAD_WHOLE)
	{
		load_tile_iteration(tile, info, 0);
		return;
	}

	unsigned num_lines = ((info.thi >> 2) - (info.tlo >> 2)) + 1;
	for (unsigned line = 0; line < num_lines; line += lines_per_part)
	{
		unsigned to_copy_lines = rdp_min(num_lines - line, lines_per_part);
		LoadTileInfo part = info;
		part.tlo = uint16_t(info.tlo + (line << 2));
		part.thi = uint16_t(part.tlo + ((to_copy_lines - 1) << 2));
		load_tile_iteration(tile, part, line * tiles[tile].meta.stride);
	}

	// The tile's size is the whole load's.
	auto &size = tiles[tile].size;
	size.slo = info.slo;
	size.shi = info.shi;
	size.tlo = info.tlo;
	size.thi = info.thi;
}

void Renderer::load_tile_iteration(uint32_t tile, const LoadTileInfo &info, uint32_t tmem_offset)
{
	auto &size = tiles[tile].size;
	size.slo = info.slo;
	size.shi = info.shi;
	size.tlo = info.tlo;
	size.thi = info.thi;

	UploadInfo upload;
	switch (rdp_load_upload_info(&info, &tiles[tile].meta, tmem_offset, &upload))
	{
	case RDP_LOAD_OK:
		break;

	case RDP_LOAD_CRASHES:
		if (validation_iface)
			report_rdp_crash(ValidationError::LoadTile4bpp, "4-bit VRAM pointer crashes the RDP.");
		return;

	default:
		return;
	}

	stream.tmem_upload_infos[stream.num_tmem_uploads++] = upload;
	if (stream.num_tmem_uploads + 1 >= Limits::MaxTMEMInstances)
		flush_queues();
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
