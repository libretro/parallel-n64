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
#include "pipeline_pending.h"
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
	bool pipelines_ahead_ready() const;
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
	void load_tile_iteration(uint32_t tile, const LoadTileInfo &info, uint32_t tmem_offset);

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

	Vulkan::BufferHandle upscaling_reference_rdram;
	Vulkan::BufferHandle upscaling_multisampled_rdram;
	Vulkan::BufferHandle upscaling_multisampled_hidden_rdram;

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

	bool init_caps();
	void init_blender_lut();
	void init_buffers(const RendererOptions &options);
	bool init_internal_upscaling_factor(const RendererOptions &options);

	struct
	{
		uint32_t addr = 0;
		uint32_t depth_addr = 0;
		uint32_t width = 0;
		uint32_t deduced_height = 0;
		FBFormat fmt = FBFormat::I8;
		bool depth_write_pending = false;
		bool color_write_pending = false;
	} fb;

	struct StreamCaches
	{
		ScissorState scissor_state = {};
		StaticRasterizationState static_raster_state = {};
		DepthBlendState depth_blend_state = {};

		StateCache<StaticRasterizationState, Limits::MaxStaticRasterizationStates> static_raster_state_cache;
		StateCache<DepthBlendState, Limits::MaxDepthBlendStates> depth_blend_state_cache;
		StateCache<TileInfo, Limits::MaxTileInfoStates> tile_info_state_cache;

		StreamCache<TriangleSetup, Limits::MaxPrimitives> triangle_setup;
		StreamCache<ScissorState, Limits::MaxPrimitives> scissor_setup;
		StreamCache<AttributeSetup, Limits::MaxPrimitives> attribute_setup;
		StreamCache<DerivedSetup, Limits::MaxPrimitives> derived_setup;
		StreamCache<InstanceIndices, Limits::MaxPrimitives> state_indices;
		StreamCache<SpanInfoOffsets, Limits::MaxPrimitives> span_info_offsets;
		StreamCache<SpanInterpolationJob, Limits::MaxSpanSetups> span_info_jobs;

		UploadInfo tmem_upload_infos[Limits::MaxTMEMInstances];
		unsigned num_tmem_uploads = 0;
		unsigned max_shaded_tiles = 0;
		Vulkan::CommandBufferHandle cmd;
	} stream;

	void ensure_command_buffer();

	TileInfo tiles[Limits::MaxNumTiles];
	Vulkan::BufferHandle tmem_instances;
	Vulkan::BufferHandle span_setups;
	Vulkan::BufferHandle blender_divider_lut_buffer;
	Vulkan::BufferViewHandle blender_divider_buffer;

	Vulkan::BufferHandle tile_binning_buffer;
	Vulkan::BufferHandle tile_binning_buffer_coarse;

	Vulkan::BufferHandle indirect_dispatch_buffer;
	Vulkan::BufferHandle tile_work_list;
	Vulkan::BufferHandle per_tile_offsets;
	Vulkan::BufferHandle per_tile_shaded_color;
	Vulkan::BufferHandle per_tile_shaded_depth;
	Vulkan::BufferHandle per_tile_shaded_shaded_alpha;
	Vulkan::BufferHandle per_tile_shaded_coverage;

	struct MappedBuffer
	{
		Vulkan::BufferHandle buffer;
		bool is_host = false;
	};

	struct RenderBuffers
	{
		void init(Vulkan::Device &device, granite_buffer_domain_t domain, RenderBuffers *borrow);
		static MappedBuffer create_buffer(Vulkan::Device &device, granite_buffer_domain_t domain, VkDeviceSize size, MappedBuffer *borrow);

		MappedBuffer triangle_setup;
		MappedBuffer attribute_setup;
		MappedBuffer derived_setup;
		MappedBuffer scissor_setup;

		MappedBuffer static_raster_state;
		MappedBuffer depth_blend_state;
		MappedBuffer tile_info_state;

		MappedBuffer state_indices;
		MappedBuffer span_info_offsets;

		MappedBuffer span_info_jobs;
		Vulkan::BufferViewHandle span_info_jobs_view;
	};

	struct RenderBuffersUpdater
	{
		void init(Vulkan::Device &device);
		void upload(Vulkan::Device &device, const StreamCaches &caches, Vulkan::CommandBuffer &cmd);

		template <typename Cache>
		void upload(Vulkan::CommandBuffer &cmd, Vulkan::Device &device,
		            const MappedBuffer &gpu, const MappedBuffer &cpu, const Cache &cache, bool &did_upload);

		RenderBuffers cpu, gpu;
	};

	struct InternalSynchronization
	{
		Vulkan::Fence fence;
	};

	rdp_constants_t constants = {};

	RenderBuffersUpdater buffer_instances[Limits::NumSyncStates];
	InternalSynchronization internal_sync[Limits::NumSyncStates];
	uint32_t sync_indices_needs_flush = 0;
	unsigned buffer_instance = 0;
	void bind_stream_instance();
	uint32_t base_primitive_index = 0;
	unsigned pending_render_passes = 0;
	unsigned pending_render_passes_upscaled = 0;
	unsigned pending_primitives = 0;
	unsigned pending_primitives_upscaled = 0;

	bool tmem_upload_needs_flush(uint32_t addr) const;

	bool render_pass_is_upscaled() const;
	bool should_render_upscaled() const;

	void flush_queues();
	void submit_render_pass(Vulkan::CommandBuffer &cmd);
	void submit_render_pass_upscaled(Vulkan::CommandBuffer &cmd);
	void submit_render_pass_end(Vulkan::CommandBuffer &cmd);
	void submit_to_queue();
	void begin_new_context();
	void reset_context();
	bool need_flush() const;
	void maintain_queues();
	void maintain_queues_idle();
	void update_tmem_instances(Vulkan::CommandBuffer &cmd);
	void submit_span_setup_jobs(Vulkan::CommandBuffer &cmd, bool upscaled);
	void update_deduced_height(const TriangleSetup &setup);
	void submit_tile_binning_combined(Vulkan::CommandBuffer &cmd, bool upscaled);
	void clear_indirect_buffer(Vulkan::CommandBuffer &cmd);

	enum { MaxKnownPipelines = 256 };
	struct KnownPipelines
	{
		void *nodes;
		size_t n, limit;
		Renderer *renderer;
	};
	static void add_known_pipeline(void *list, granite_command_buffer_t *cb, uint32_t kind);
	size_t queue_known_pipelines(void *nodes, size_t n, VideoInterface *vi, bool with_upscaler);
	void set_span_setup_pipeline(Vulkan::CommandBuffer &cmd, bool upscale);
	uint32_t set_tile_binning_pipeline(Vulkan::CommandBuffer &cmd, bool upscale);
	void set_depth_blend_pipeline(Vulkan::CommandBuffer &cmd, uint32_t fmt, bool same_addr,
	                              bool upscaled, bool force_write_mask);
	void submit_rasterization(Vulkan::CommandBuffer &cmd, Vulkan::Buffer &tmem, bool upscaled);
	void submit_depth_blend(Vulkan::CommandBuffer &cmd, Vulkan::Buffer &tmem, bool upscaled, bool force_write_mask);

	enum class ResolveStage { Pre, Post, SSAAResolve };
	void set_upscaled_domain_pipeline(Vulkan::CommandBuffer &cmd, ResolveStage stage,
	                                  unsigned pixel_size_log2, bool same_addr);
	void submit_update_upscaled_domain(Vulkan::CommandBuffer &cmd, ResolveStage stage);
	void submit_update_upscaled_domain(Vulkan::CommandBuffer &cmd, ResolveStage stage,
	                                   unsigned addr, unsigned depth_addr,
	                                   unsigned width, unsigned height,
	                                   unsigned pixel_size_log2);
	void submit_clear_super_sample_write_mask(Vulkan::CommandBuffer &cmd, unsigned width, unsigned height);

	SpanInfoOffsets allocate_span_jobs(const TriangleSetup &setup);

	bool can_support_minimum_subgroup_size(unsigned size) const;
	bool supports_subgroup_size_control(uint32_t minimum_size, uint32_t maximum_size) const;

	// The compute pipelines queued to the worker, and the jobs carrying
	// them (pipeline_nodes): both allocated before the first frame.
	rdp_hash_set_t pending_async_pipelines = {};
	rdp_node_pool_t pipeline_node_pool;
	void *pipeline_nodes = nullptr;


	struct Caps
	{
		bool force_sync = false;
		bool ubershader = false;
		bool supports_small_integer_arithmetic = false;
		bool no_storage_8bit = false;
		bool subgroup_tile_binning = false;
		bool subgroup_depth_blend = false;
		bool super_sample_readback = false;
		bool super_sample_readback_dither = false;
		unsigned upscaling = 1;
		unsigned max_num_tile_instances = Limits::MaxTileInstances;
		// static raster states per flush: as many as the work list's
		// per-state slices that fit in one storage buffer binding
		unsigned max_static_raster_states = Limits::MaxStaticRasterizationStates;
		unsigned max_tiles_x = ImplementationConstants::MaxTilesX;
		unsigned max_tiles_y = ImplementationConstants::MaxTilesY;
		unsigned max_width = Limits::MaxWidth;
		unsigned max_height = Limits::MaxHeight;
	} caps;

	// Background compute pipeline compiles (rdp_worker.c).
	rdp_worker_t *pipeline_worker = nullptr;
	static void pipeline_work(void *user, mpsc_stack_node_t *item);
	/* build_pipelines_ahead's nodes, one block freed once the worker is
	 * gone; the worker counts the urgent ones it is done with, and builds
	 * none past teardown. */
	void *pipeline_ahead_nodes = nullptr;
	int pipeline_ahead_urgent = 0;
	retro_atomic_int_t pipeline_ahead_urgent_done{0};
	retro_atomic_int_t pipeline_ahead_stop{0};
	static bool pipeline_ahead_urgent_built(void *ctx);

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