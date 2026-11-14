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

#include "rdp_data_structures.hpp"
#include "device.hpp"
#include "rdp_common.hpp"
#include "rdp_worker.h"
#include "shader_bank.h"
#include "rdram_coherency.h"
#include "renderer_caps.h"
#include "rdp_pipelines.h"
#include "renderer_buffers.h"
#include "rdp_passes.h"
#include "rdp_queue.h"
#include "rdp_submit.h"
#include "rdp_sync_ring.h"
#include <retro_atomic.h>

namespace RDP
{
struct CoherencyOperation;
using CoherencyCopy = ::rdp_coherency_copy_t;

struct SyncObject
{
	Vulkan::Fence fence;
};

enum class FBFormat : uint32_t
{
	I4 = 0,
	I8 = 1,
	RGBA5551 = 2,
	IA88 = 3,
	RGBA8888 = 4
};

// A load command (tmem_load.h)
using LoadTileInfo = ::rdp_load_tile_info_t;

class CommandProcessor;

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
	Fill4bpp,
	LoadTile4bpp,
	InvalidMultilineLoadTlut,
	FillDepthTest,
	FillDepthWrite,
	FillImageReadEnable,
	Copy32bpp
};

struct ValidationInterface
{
	// Validation errors may be called from a thread as errors are encountered.
	// Reports situations that would cause fatal error on a real RDP.
	// We only opt to report these situations rather than deliberately crashing the renderer.
	// Handling crashes is only relevant during development of N64 homebrew.
	void (*report_rdp_crash)(void *user, ValidationError err, const char *msg);
	void *user;
};

class VideoInterface;

class Renderer
{
public:
	explicit Renderer(CommandProcessor &processor);
	~Renderer();
	void set_device(Vulkan::Device *device);

	void set_validation_interface(ValidationInterface *iface);

	// If coherent is false, RDRAM is a buffer split into data in lower half and writemask state in upper half, each part being size large.
	// offset must be 0 in this case.
	// False when out of memory
	bool set_rdram(Vulkan::Buffer *buffer, uint8_t *host_rdram, size_t offset, size_t size, bool coherent);
	void set_hidden_rdram(Vulkan::Buffer *buffer);
	void set_tmem(Vulkan::Buffer *buffer);
	void set_shader_bank(const ShaderBank *bank);

	bool init_renderer(const RendererOptions &options);

	/* Hands the pipelines @records lists to the pipeline worker: those a
	 * draw cannot do without first, then those it has a fallback for. The
	 * thread that processes commands, once, after init_renderer. */
	void build_pipelines_ahead(const pipeline_record_t *records, size_t count, VideoInterface *vi, bool with_upscaler);
	/* Whether the worker has built the first kind, without waiting; any
	 * thread, after build_pipelines_ahead returned. */
	bool pipelines_ahead_ready();
	/* Sleeps until they are: for test hosts that count what the frame's
	 * threads build, never on a frontend's path */
	void wait_pipelines_ahead_for_test();

	// setup may be mutated to apply various fixups to triangle setup.
	void draw_flat_primitive(TriangleSetup &setup);
	void draw_shaded_primitive(TriangleSetup &setup, const AttributeSetup &attr);

	void set_color_framebuffer(uint32_t addr, uint32_t width, FBFormat fmt);
	void set_depth_framebuffer(uint32_t addr);

	void set_scissor_state(const ScissorState &state);
	void set_static_rasterization_state(const StaticRasterizationState &state);
	void set_depth_blend_state(const DepthBlendState &state);

	void set_tile(uint32_t tile, const TileMeta &info);
	void set_tile_size(uint32_t tile, uint32_t slo, uint32_t shi, uint32_t tlo, uint32_t thi);
	void load_tile(uint32_t tile, const LoadTileInfo &info);
	static void flush_for_queue(void *renderer);

	void set_blend_color(uint32_t color);
	void set_fog_color(uint32_t color);
	void set_env_color(uint32_t color);
	void set_primitive_color(uint8_t min_level, uint8_t prim_lod_frac, uint32_t color);
	void set_fill_color(uint32_t color);
	void set_primitive_depth(uint16_t prim_depth, uint16_t prim_dz);
	void set_enable_primitive_depth(bool enable);
	void set_convert(uint16_t k0, uint16_t k1, uint16_t k2, uint16_t k3, uint16_t k4, uint16_t k5);
	void set_color_key(unsigned component, uint32_t width, uint32_t center, uint32_t scale);

	// Called when the command thread has not seen any activity in a given period of time.
	// This is useful so we don't needlessly queue up work when we might as well kick it to the GPU.
	void notify_idle_command_thread();
	void flush_and_signal();

	int resolve_shader_define(const char *name, const char *define) const;

	void resolve_coherency_external(unsigned offset, unsigned length);
	void submit_update_upscaled_domain_external(Vulkan::CommandBuffer &cmd,
	                                            unsigned addr, unsigned pixels, unsigned pixel_size_log2);
	unsigned get_scaling_factor() const;
	// The largest factor up to 'factor' whose buffers each fit in one
	// storage buffer binding (maxStorageBufferRange).
	static unsigned fitting_upscaling_factor(const Vulkan::Device &device, unsigned factor,
	                                         VkDeviceSize rdram_size, VkDeviceSize hidden_rdram_bytes,
	                                         bool super_sampled_readback);

	const Vulkan::Buffer *get_upscaled_rdram_buffer() const;
	const Vulkan::Buffer *get_upscaled_hidden_rdram_buffer() const;


private:
	CommandProcessor &processor;
	Vulkan::Device *device = nullptr;
	Vulkan::Buffer *rdram = nullptr;
	ValidationInterface *validation_iface = nullptr;
	void report_rdp_crash(ValidationError err, const char *msg) const;


	void validate_draw_state() const;

	// The GPU's RDRAM kept in step with the host's, without
	// VK_EXT_external_memory_host (rdram_coherency.h)
	rdp_rdram_coherency_t incoherent = {};

	size_t rdram_offset = 0;
	size_t rdram_size = 0;
	bool is_host_coherent = false;
	Vulkan::Buffer *hidden_rdram = nullptr;
	Vulkan::Buffer *tmem = nullptr;
	const ShaderBank *shader_bank = nullptr;

	bool init_internal_upscaling_factor(const RendererOptions &options);

	struct
	{
		uint32_t addr = 0;
		uint32_t depth_addr = 0;
		uint32_t width = 0;
		FBFormat fmt = FBFormat::I8;
	} fb;

	// The flush being queued (rdp_queue.c)
	rdp_queue_t queue;

	struct
	{
		Vulkan::CommandBufferHandle cmd;
	} stream;

	void ensure_command_buffer();

	TileInfo tiles[Limits::MaxNumTiles];
	// The fixed buffers and the upscaled RDRAM (renderer_buffers.c)
	rdp_renderer_buffers_t buffers = {};
	// Whether the first flush cleared the indirect dispatch buffer
	bool indirect_cleared = false;

	rdp_constants_t constants = {};

	rdp_stream_instance_t buffer_instances[Limits::NumSyncStates] = {};
	void upload_streams(Vulkan::CommandBuffer &cmd);
	// Which of them the flush writes, and when the GPU is done with each
	// (rdp_sync_ring.c)
	rdp_sync_ring_t sync_ring;
	void bind_stream_instance();
	uint32_t base_primitive_index = 0;
	// What was recorded since the last submission (rdp_submit.c)
	rdp_submit_counts_t submits = {};

	bool tmem_upload_needs_flush(uint32_t addr) const;


	void flush_queues();
	void submit_to_queue();
	void begin_new_context();
	void reset_context();
	void maintain_queues();
	void maintain_queues_idle();
	void clear_indirect_buffer(Vulkan::CommandBuffer &cmd);

	rdp_pipeline_setup_t pipeline_setup() const;
	rdp_pass_resources_t pass_resources(const rdp_pipeline_setup_t &setup);
	rdp_pass_target_t pass_target() const;
	rdp_pass_flush_t pass_flush() const;

	void submit_update_upscaled_domain(Vulkan::CommandBuffer &cmd, rdp_resolve_stage stage,
	                                   unsigned addr, unsigned depth_addr,
	                                   unsigned width, unsigned height,
	                                   unsigned pixel_size_log2);



	rdp_renderer_caps_t caps;

	// The compute pipelines built on a thread of their own
	rdp_pipeline_builder_t pipelines;

	void resolve_coherency_host_to_gpu(Vulkan::CommandBuffer &cmd);
	void resolve_coherency_gpu_to_host(CoherencyOperation &op, Vulkan::CommandBuffer &cmd);
	uint32_t get_byte_size_for_bound_color_framebuffer() const;
	uint32_t get_byte_size_for_bound_depth_framebuffer() const;
	void mark_pages_for_gpu_read(uint32_t base_addr, uint32_t byte_count);
	void lock_pages_for_gpu_write(uint32_t base_addr, uint32_t byte_count);

	retro_atomic_int_t active_submissions;
	void enqueue_fence_wait(Vulkan::Fence fence);
	uint64_t last_submit_ns = 0;
};
}