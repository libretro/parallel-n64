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

#pragma once

/* The renderer (rdp_renderer.c), for the command processor */

#include "rdp_data_structures.hpp"
#include "device.hpp"
#include "rdp_common.hpp"
#include "rdp_renderer.h"

namespace RDP
{
using CoherencyOperation = ::rdp_timeline_op_t;
using CoherencyCopy = ::rdp_coherency_copy_t;
// A load command (tmem_load.h)
using LoadTileInfo = ::rdp_load_tile_info_t;

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
static_assert(RDP_RENDERER_SPAN_SETUP_SIZE == sizeof(SpanSetup), "rdp_renderer.c sizes span setups otherwise.");
static_assert((Limits::MaxStaticRasterizationStates % ImplementationConstants::DefaultWorkgroupSize) == 0,
              "MaxStaticRasterizationStates does not align.");
static_assert((Limits::MaxPrimitives % 32) == 0 && Limits::MaxPrimitives <= 32 * 32,
              "MaxPrimitives must be a multiple of 32, at most 1024.");
static_assert(RDP_CAPS_MAX_WIDTH == Limits::MaxWidth && RDP_CAPS_MAX_HEIGHT == Limits::MaxHeight &&
              RDP_CAPS_TILE_WIDTH == ImplementationConstants::TileWidth &&
              RDP_CAPS_TILE_HEIGHT == ImplementationConstants::TileHeight &&
              RDP_CAPS_MAX_PRIMITIVES == Limits::MaxPrimitives &&
              RDP_CAPS_MAX_STATIC_RASTER_STATES == Limits::MaxStaticRasterizationStates &&
              RDP_CAPS_MAX_TILE_INSTANCES == Limits::MaxTileInstances &&
              RDP_CAPS_TILE_RASTER_WORK_SIZE == sizeof(TileRasterWork),
              "renderer_caps.h sizes buffers for other limits.");

enum class FBFormat : uint32_t
{
	I4 = RDP_FB_I4,
	I8 = RDP_FB_I8,
	RGBA5551 = RDP_FB_RGBA5551,
	IA88 = RDP_FB_IA88,
	RGBA8888 = RDP_FB_RGBA8888
};

struct RendererOptions
{
	unsigned upscaling_factor = 1;
	bool super_sampled_readback = false;
	bool super_sampled_readback_dither = false;
	// The device has no storageBuffer8BitAccess: use the NO_STORAGE_8BIT
	// shader variants, whose hidden RDRAM is 16 bits per entry.
	bool no_storage_8bit = false;
};

enum class ValidationError
{
	Fill4bpp = RDP_VALIDATION_FILL_4BPP,
	LoadTile4bpp = RDP_VALIDATION_LOAD_TILE_4BPP,
	InvalidMultilineLoadTlut = RDP_VALIDATION_INVALID_MULTILINE_LOAD_TLUT,
	FillDepthTest = RDP_VALIDATION_FILL_DEPTH_TEST,
	FillDepthWrite = RDP_VALIDATION_FILL_DEPTH_WRITE,
	FillImageReadEnable = RDP_VALIDATION_FILL_IMAGE_READ_ENABLE,
	Copy32bpp = RDP_VALIDATION_COPY_32BPP
};

struct ValidationInterface
{
	// Reports, on the thread that processes commands, what would crash a
	// real RDP; it is drawn as well as it can be. Only of use while
	// developing N64 homebrew.
	void (*report_rdp_crash)(void *user, ValidationError err, const char *msg);
	void *user;
};

class VideoInterface;

class Renderer
{
public:
	explicit Renderer(rdp_timeline_t *timeline)
	{
		rdp_renderer_init(&r, timeline);
	}

	~Renderer()
	{
		rdp_renderer_free(&r);
	}

	Renderer(const Renderer &) = delete;
	void operator=(const Renderer &) = delete;

	void set_device(Vulkan::Device *device)
	{
		rdp_renderer_set_device(&r, device);
	}

	void set_validation_interface(ValidationInterface *iface)
	{
		rdp_renderer_set_validation(&r, iface ? report_rdp_crash : nullptr, iface);
	}

	// If coherent is false, RDRAM is a buffer split into data in lower half and writemask state in upper half,
	// each part being size large. offset must be 0 in this case. False when out of memory.
	bool set_rdram(Vulkan::Buffer *buffer, uint8_t *host_rdram, size_t offset, size_t size, bool coherent)
	{
		return rdp_renderer_set_rdram(&r, buffer, host_rdram, offset, size, coherent);
	}

	void set_hidden_rdram(Vulkan::Buffer *buffer)
	{
		rdp_renderer_set_hidden_rdram(&r, buffer);
	}

	void set_tmem(Vulkan::Buffer *buffer)
	{
		rdp_renderer_set_tmem(&r, buffer);
	}

	void set_shader_bank(const ShaderBank *bank)
	{
		rdp_renderer_set_shader_bank(&r, bank);
	}

	bool init_renderer(const RendererOptions &options)
	{
		rdp_renderer_options_t o;
		o.upscaling_factor = options.upscaling_factor;
		o.super_sampled_readback = options.super_sampled_readback;
		o.super_sampled_readback_dither = options.super_sampled_readback_dither;
		o.no_storage_8bit = options.no_storage_8bit;
		return rdp_renderer_init_renderer(&r, &o);
	}

	void build_pipelines_ahead(const pipeline_record_t *records, size_t count, rdp_vi_t *vi, bool with_upscaler)
	{
		rdp_renderer_build_pipelines_ahead(&r, records, count, vi, with_upscaler);
	}

	bool pipelines_ahead_ready()
	{
		return rdp_renderer_pipelines_ahead_ready(&r);
	}

	void wait_pipelines_ahead_for_test()
	{
		rdp_renderer_wait_pipelines_ahead_for_test(&r);
	}

	void draw_flat_primitive(const TriangleSetup &setup)
	{
		rdp_renderer_draw(&r, &setup, nullptr);
	}

	void draw_shaded_primitive(const TriangleSetup &setup, const AttributeSetup &attr)
	{
		rdp_renderer_draw(&r, &setup, &attr);
	}

	void set_color_framebuffer(uint32_t addr, uint32_t width, FBFormat fmt)
	{
		rdp_renderer_set_color_framebuffer(&r, addr, width, uint32_t(fmt));
	}

	void set_depth_framebuffer(uint32_t addr)
	{
		rdp_renderer_set_depth_framebuffer(&r, addr);
	}

	void set_scissor_state(const ScissorState &state)
	{
		rdp_renderer_set_scissor_state(&r, &state);
	}

	void set_static_rasterization_state(const StaticRasterizationState &state)
	{
		rdp_renderer_set_static_state(&r, &state);
	}

	void set_depth_blend_state(const DepthBlendState &state)
	{
		rdp_renderer_set_depth_blend_state(&r, &state);
	}

	void set_tile(uint32_t tile, const TileMeta &meta)
	{
		rdp_renderer_set_tile(&r, tile, &meta);
	}

	void set_tile_size(uint32_t tile, uint32_t slo, uint32_t shi, uint32_t tlo, uint32_t thi)
	{
		rdp_renderer_set_tile_size(&r, tile, slo, shi, tlo, thi);
	}

	void load_tile(uint32_t tile, const LoadTileInfo &info)
	{
		rdp_renderer_load_tile(&r, tile, &info);
	}

	void set_blend_color(uint32_t color) { rdp_renderer_set_blend_color(&r, color); }
	void set_fog_color(uint32_t color) { rdp_renderer_set_fog_color(&r, color); }
	void set_env_color(uint32_t color) { rdp_renderer_set_env_color(&r, color); }
	void set_fill_color(uint32_t color) { rdp_renderer_set_fill_color(&r, color); }

	void set_primitive_color(uint8_t min_level, uint8_t prim_lod_frac, uint32_t color)
	{
		rdp_renderer_set_primitive_color(&r, min_level, prim_lod_frac, color);
	}

	void set_primitive_depth(uint16_t prim_depth, uint16_t prim_dz)
	{
		rdp_renderer_set_primitive_depth(&r, prim_depth, prim_dz);
	}

	void set_enable_primitive_depth(bool enable)
	{
		rdp_renderer_set_enable_primitive_depth(&r, enable);
	}

	void set_convert(uint16_t k0, uint16_t k1, uint16_t k2, uint16_t k3, uint16_t k4, uint16_t k5)
	{
		rdp_renderer_set_convert(&r, k0, k1, k2, k3, k4, k5);
	}

	void set_color_key(unsigned component, uint32_t width, uint32_t center, uint32_t scale)
	{
		rdp_renderer_set_color_key(&r, component, width, center, scale);
	}

	// The command thread saw no activity for a while: what is queued goes
	// to the GPU rather than wait for more.
	void notify_idle_command_thread()
	{
		rdp_renderer_notify_idle(&r);
	}

	void flush_and_signal()
	{
		rdp_renderer_flush_and_signal(&r);
	}

	int resolve_shader_define(const char *name, const char *define) const
	{
		return rdp_renderer_resolve_shader_define(&r, name, define);
	}

	void resolve_coherency_external(unsigned offset, unsigned length)
	{
		rdp_renderer_resolve_coherency_external(&r, offset, length);
	}

	void submit_update_upscaled_domain_external(granite_command_buffer_t *cmd, unsigned addr, unsigned pixels,
	                                            unsigned pixel_size_log2)
	{
		rdp_renderer_update_upscaled_domain(&r, cmd, addr, pixels, pixel_size_log2);
	}

	unsigned get_scaling_factor() const
	{
		return rdp_renderer_scaling_factor(&r);
	}

	// The largest factor up to 'factor' whose buffers each fit in one
	// storage buffer binding (maxStorageBufferRange).
	static unsigned fitting_upscaling_factor(const Vulkan::Device &device, unsigned factor,
	                                         VkDeviceSize rdram_size, VkDeviceSize hidden_rdram_bytes,
	                                         bool super_sampled_readback)
	{
		return rdp_renderer_fitting_upscaling_factor(&device, factor, rdram_size, hidden_rdram_bytes,
		                                             super_sampled_readback);
	}

	const granite_buffer_t *get_upscaled_rdram_buffer() const
	{
		return rdp_renderer_upscaled_rdram(&r);
	}

	const granite_buffer_t *get_upscaled_hidden_rdram_buffer() const
	{
		return rdp_renderer_upscaled_hidden_rdram(&r);
	}

	rdp_renderer_t *get()
	{
		return &r;
	}

private:
	rdp_renderer_t r;

	static void report_rdp_crash(void *user, rdp_validation_error err, const char *msg)
	{
		auto *iface = static_cast<ValidationInterface *>(user);
		iface->report_rdp_crash(iface->user, ValidationError(err), msg);
	}
};
}
