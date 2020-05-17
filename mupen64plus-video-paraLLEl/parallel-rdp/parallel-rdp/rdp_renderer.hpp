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
#include "worker_thread.hpp"
#include <unordered_set>

namespace RDP
{
struct CoherencyOperation;

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

enum class UploadMode : uint32_t
{
	Tile = 0,
	TLUT = 1,
	Block = 2
};

struct LoadTileInfo
{
	uint32_t tex_addr;
	uint32_t tex_width;
	uint16_t slo, tlo, shi, thi;
	TextureFormat fmt;
	TextureSize size;
	UploadMode mode;
};

class CommandProcessor;

class Renderer : public Vulkan::DebugChannelInterface
{
public:
	explicit Renderer(CommandProcessor &processor);
	~Renderer();
	bool set_device(Vulkan::Device *device);

	// If coherent is false, RDRAM is a buffer split into data in lower half and writemask state in upper half, each part being size large.
	// offset must be 0 in this case.
	void set_rdram(Vulkan::Buffer *buffer, uint8_t *host_rdram, size_t offset, size_t size, bool coherent);
	void set_hidden_rdram(Vulkan::Buffer *buffer);
	void set_tmem(Vulkan::Buffer *buffer);
	void set_shader_bank(const ShaderBank *bank);

	void draw_flat_primitive(const TriangleSetup &setup);
	void draw_shaded_primitive(const TriangleSetup &setup, const AttributeSetup &attr);

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

	void flush();
	Vulkan::Fence flush_and_signal();

	int resolve_shader_define(const char *name, const char *define) const;

	void resolve_coherency_external(unsigned offset, unsigned length);

private:
	CommandProcessor &processor;
	Vulkan::Device *device = nullptr;
	Vulkan::Buffer *rdram = nullptr;

	struct
	{
		uint8_t *host_rdram = nullptr;
		Vulkan::BufferHandle staging_rdram;
		Vulkan::BufferHandle staging_readback;
		std::unique_ptr<std::atomic_uint32_t[]> pending_writes_for_page;
		std::vector<uint32_t> page_to_direct_copy;
		std::vector<uint32_t> page_to_masked_copy;
		std::vector<uint32_t> page_to_pending_readback;
		unsigned num_pages = 0;
		unsigned staging_readback_pages = 0;
		unsigned staging_readback_index = 0; // Ringbuffer the readbacks.
	} incoherent;

	size_t rdram_offset = 0;
	size_t rdram_size = 0;
	bool is_host_coherent = false;
	Vulkan::Buffer *hidden_rdram = nullptr;
	Vulkan::Buffer *tmem = nullptr;
	const ShaderBank *shader_bank = nullptr;

	bool init_caps();
	void init_blender_lut();
	void init_buffers();

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

		std::vector<UploadInfo> tmem_upload_infos;
		unsigned max_shaded_tiles = 0;
	} stream;

	TileInfo tiles[Limits::MaxNumTiles];
	Vulkan::BufferHandle tmem_instances;
	Vulkan::BufferHandle span_setups;
	Vulkan::BufferHandle blender_divider_lut_buffer;
	Vulkan::BufferViewHandle blender_divider_buffer;

	Vulkan::BufferHandle tile_binning_buffer_prepass;
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
		void init(Vulkan::Device &device, Vulkan::BufferDomain domain, RenderBuffers *borrow);
		static MappedBuffer create_buffer(Vulkan::Device &device, Vulkan::BufferDomain domain, VkDeviceSize size, MappedBuffer *borrow);

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
		void upload(Vulkan::Device &device, const StreamCaches &caches);

		template <typename Cache>
		void upload(Vulkan::CommandBuffer *cmd, Vulkan::Device &device,
		            const MappedBuffer &gpu, const MappedBuffer &cpu, const Cache &cache);

		RenderBuffers cpu, gpu;
	};

	struct InternalSynchronization
	{
		SyncObject complete;
	};

	struct Constants
	{
		uint32_t blend_color = 0;
		uint32_t fog_color = 0;
		uint32_t env_color = 0;
		uint32_t primitive_color = 0;
		uint32_t fill_color = 0;
		uint8_t min_level = 0;
		uint8_t prim_lod_frac = 0;
		int32_t prim_depth = 0;
		uint16_t prim_dz = 0;
		uint16_t convert[6] = {};

		uint16_t key_width[3] = {};
		uint8_t key_center[3] = {};
		uint8_t key_scale[3] = {};

		bool use_prim_depth = false;
	} constants;

	RenderBuffersUpdater buffer_instances[Limits::NumSyncStates];
	InternalSynchronization internal_sync[Limits::NumSyncStates];
	unsigned buffer_instance = 0;
	uint32_t base_primitive_index = 0;

	bool tmem_upload_needs_flush(uint32_t addr) const;

	void flush_queues();
	void submit_render_pass();
	void begin_new_context();
	bool need_flush() const;
	void update_tmem_instances(Vulkan::CommandBuffer &cmd);
	void submit_span_setup_jobs(Vulkan::CommandBuffer &cmd);
	void update_deduced_height(const TriangleSetup &setup);
	void submit_tile_binning_prepass(Vulkan::CommandBuffer &cmd);
	void submit_tile_binning_complete(Vulkan::CommandBuffer &cmd);
	void clear_indirect_buffer(Vulkan::CommandBuffer &cmd);
	void submit_rasterization(Vulkan::CommandBuffer &cmd, Vulkan::Buffer &tmem);

	SpanInfoOffsets allocate_span_jobs(const TriangleSetup &setup);

	DerivedSetup build_derived_attributes(const AttributeSetup &attr) const;
	void build_combiner_constants(DerivedSetup &setup, unsigned cycle) const;
	int filter_debug_channel_x = -1;
	int filter_debug_channel_y = -1;
	bool debug_channel = false;

	void message(const std::string &tag, uint32_t code,
	             uint32_t x, uint32_t y, uint32_t z,
	             uint32_t num_words, const Vulkan::DebugChannelInterface::Word *words) override;

	bool can_support_minimum_subgroup_size(unsigned size) const;
	bool supports_subgroup_size_control(uint32_t minimum_size, uint32_t maximum_size) const;

	std::unordered_set<Util::Hash> pending_async_pipelines;

	unsigned compute_conservative_max_num_tiles(const TriangleSetup &setup) const;

	void deduce_static_texture_state(unsigned tile, unsigned max_lod_level);
	void deduce_noise_state();
	static StaticRasterizationState normalize_static_state(StaticRasterizationState state);

	struct Caps
	{
		bool timestamp = false;
		bool force_sync = false;
		bool ubershader = false;
		bool supports_small_integer_arithmetic = false;
		bool subgroup_tile_binning_prepass = false;
		bool subgroup_tile_binning = false;
	} caps;

	struct PipelineExecutor
	{
		Vulkan::Device *device;
		bool is_sentinel(const Vulkan::DeferredPipelineCompile &compile) const;
		void perform_work(const Vulkan::DeferredPipelineCompile &compile) const;
		void notify_work_locked(const Vulkan::DeferredPipelineCompile &compile) const;
	};

	std::unique_ptr<WorkerThread<Vulkan::DeferredPipelineCompile, PipelineExecutor>> pipeline_worker;

	void resolve_coherency_host_to_gpu();
	void resolve_coherency_gpu_to_host(CoherencyOperation &op, Vulkan::CommandBuffer &cmd);
	uint32_t get_byte_size_for_bound_color_framebuffer() const;
	uint32_t get_byte_size_for_bound_depth_framebuffer() const;
	void mark_pages_for_gpu_read(uint32_t base_addr, uint32_t byte_count);
	void lock_pages_for_gpu_write(uint32_t base_addr, uint32_t byte_count);
};
}