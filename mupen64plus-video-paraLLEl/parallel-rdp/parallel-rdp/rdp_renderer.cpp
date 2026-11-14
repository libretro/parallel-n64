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
#include "incoherent_pages.hpp"
#include "pipeline_record_list.h"
#include "timer.h"
#include "thread_name.h"

namespace RDP
{
static size_t div_round_up(size_t a, size_t b)
{
	return (a + b - 1) / b;
}

namespace
{
/* Never more than this many misses queued to the worker. */
const unsigned MaxQueuedPipelines = 4 * Limits::MaxStaticRasterizationStates;

struct PipelineNode : mpsc_stack_node_t
{
	Vulkan::DeferredPipelineCompile compile;
	uint32_t kind;   /* PIPELINE_RECORD_* */
	uint8_t ahead;   /* one of build_pipelines_ahead's block */
	uint8_t urgent;  /* a draw waits for it */
};

void pipeline_worker_start(void *)
{
	granite_set_thread_name("RDP Pipelines");
}
}

Renderer::Renderer(CommandProcessor &processor_)
	: processor(processor_)
{
	retro_atomic_int_init(&active_submissions, 0);
	// (a tile not set yet is 16 bits)
	memset(tiles, 0, sizeof(tiles));
	for (auto &tile : tiles)
		tile.meta.size = RDP_TEXTURE_SIZE_16;
}

Renderer::~Renderer()
{
	/* The worker drains what is queued before it goes, building none of
	 * it: only a compile already in the driver is waited for. What is
	 * skipped was asked for and never used; the next session's list or
	 * draws ask again. */
	retro_atomic_store_release_int(&pipeline_ahead_stop, 1);
	rdp_worker_free(pipeline_worker);
	free(pipeline_ahead_nodes);
	free(pipeline_nodes);
	rdp_hash_set_free(&pending_async_pipelines);
	free(incoherent.arena);
}

// maxStorageBufferRange; PARALLEL_RDP_MAX_STORAGE_BUFFER_RANGE=bytes lowers
// it, to run what a device with a smaller one gets.
static VkDeviceSize storage_buffer_range(const Vulkan::Device &device)
{
	VkDeviceSize range = device.get_gpu_properties().limits.maxStorageBufferRange;
	if (const char *env = getenv("PARALLEL_RDP_MAX_STORAGE_BUFFER_RANGE"))
	{
		VkDeviceSize cap = strtoull(env, nullptr, 0);
		if (cap && cap < range)
			range = cap;
	}
	return range;
}

unsigned Renderer::fitting_upscaling_factor(const Vulkan::Device &device, unsigned factor,
                                            VkDeviceSize rdram_size, VkDeviceSize hidden_rdram_bytes,
                                            bool super_sampled_readback)
{
	const VkDeviceSize range = storage_buffer_range(device);

	for (; factor > 1; factor /= 2)
	{
		const VkDeviceSize f2 = VkDeviceSize(factor) * factor;
		const VkDeviceSize instances = f2 * Limits::MaxTileInstances;
		const VkDeviceSize tiles = f2 * ImplementationConstants::MaxTilesX * ImplementationConstants::MaxTilesY;
		const VkDeviceSize sizes[] = {
			// multisampled RDRAM, with the super-sampled write mask after it
			rdram_size * f2 + (super_sampled_readback ? 2 * Limits::MaxWidth * Limits::MaxHeight / 8 : 0),
			hidden_rdram_bytes * f2,
			// per-tile shaded colour and depth
			sizeof(uint32_t) * instances * ImplementationConstants::TileWidth * ImplementationConstants::TileHeight,
			// tile binning and per-tile offsets
			sizeof(uint32_t) * (Limits::MaxPrimitives / 32) * tiles,
			// one static raster state's slice of the work list
			sizeof(TileRasterWork) * instances,
		};
		bool fits = true;
		for (VkDeviceSize size : sizes)
			fits = fits && size <= range;
		if (fits)
			break;
	}
	return factor;
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

	// Buffers are sized for it below, before init_caps.
	caps.no_storage_8bit = options.no_storage_8bit;

	caps.max_width = options.upscaling_factor * Limits::MaxWidth;
	caps.max_height = options.upscaling_factor * Limits::MaxHeight;
	caps.max_tiles_x = options.upscaling_factor * ImplementationConstants::MaxTilesX;
	caps.max_tiles_y = options.upscaling_factor * ImplementationConstants::MaxTilesY;
	caps.max_num_tile_instances = options.upscaling_factor * options.upscaling_factor * Limits::MaxTileInstances;
	{
		// The work list holds a slice of max_num_tile_instances entries per
		// static raster state, and tile binning binds all of them at once.
		VkDeviceSize range = storage_buffer_range(*device);
		VkDeviceSize slice = sizeof(TileRasterWork) * VkDeviceSize(caps.max_num_tile_instances);
		VkDeviceSize states = range / slice;
		caps.max_static_raster_states = unsigned(rdp_min(VkDeviceSize(Limits::MaxStaticRasterizationStates), states));
		if (caps.max_static_raster_states == 0)
			return false;
		if (caps.max_static_raster_states < Limits::MaxStaticRasterizationStates)
			LOGI("Flushing every %u static raster states: the work list's binding is limited to %llu bytes.\n",
			     caps.max_static_raster_states, (unsigned long long)range);
	}

	rdp_worker_free(pipeline_worker);
	pipeline_worker = rdp_worker_new(pipeline_worker_start, pipeline_work, this);
	if (!pipeline_worker)
		return false;

	/* All a pipeline miss needs, so that it allocates nothing mid-game. */
	free(pipeline_nodes);
	rdp_hash_set_free(&pending_async_pipelines);
	rdp_hash_set_init(&pending_async_pipelines, PIPELINE_RECORD_LIST_MAX);
	rdp_node_pool_init(&pipeline_node_pool);
	auto *nodes = static_cast<PipelineNode *>(calloc(MaxQueuedPipelines, sizeof(PipelineNode)));
	pipeline_nodes = nodes;
	if (nodes)
		for (unsigned i = 0; i < MaxQueuedPipelines; i++)
			rdp_node_pool_release(&pipeline_node_pool, &nodes[i]);

	for (auto &buffer : buffer_instances)
		buffer.init(*device);
	bind_stream_instance();

	{
		Vulkan::BufferCreateInfo info = {};
		info.size = Limits::MaxTMEMInstances * 0x1000;
		info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		info.domain = Vulkan::BufferDomain::Device;
		info.misc = Vulkan::BUFFER_MISC_ZERO_INITIALIZE_BIT;
		tmem_instances = device->create_buffer(info);
	}

	{
		Vulkan::BufferCreateInfo info = {};
		info.size = options.upscaling_factor * Limits::MaxSpanSetups * sizeof(SpanSetup);
		info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		info.domain = Vulkan::BufferDomain::Device;
		info.misc = Vulkan::BUFFER_MISC_ZERO_INITIALIZE_BIT;
		span_setups = device->create_buffer(info);
	}

	init_blender_lut();
	init_buffers(options);
	if (options.upscaling_factor > 1 && !init_internal_upscaling_factor(options))
		return false;
	return init_caps();
}

void Renderer::set_device(Vulkan::Device *device_)
{
	device = device_;
}

bool Renderer::init_caps()
{
	auto &features = device->get_device_features();

	// The split rasterizer / depth-blend path misrenders textured primitives
	// on the proprietary Intel Windows driver, unoptimized shaders included.
	// Keep shading and blending in one pass there. PARALLEL_RDP_UBERSHADER
	// below still overrides it for comparisons.
	if (features.driver_properties.driverID == VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS_KHR)
		caps.ubershader = true;

	if (const char *ubershader = getenv("PARALLEL_RDP_UBERSHADER"))
	{
		caps.ubershader = strtol(ubershader, nullptr, 0) > 0;
		LOGI("Overriding ubershader = %d\n", int(caps.ubershader));
	}

	if (const char *force_sync = getenv("PARALLEL_RDP_FORCE_SYNC_SHADER"))
	{
		caps.force_sync = strtol(force_sync, nullptr, 0) > 0;
		LOGI("Overriding force sync shader = %d\n", int(caps.force_sync));
	}

	bool allow_subgroup = true;
	if (const char *subgroup = getenv("PARALLEL_RDP_SUBGROUP"))
	{
		allow_subgroup = strtol(subgroup, nullptr, 0) > 0;
		LOGI("Allow subgroups = %d\n", int(allow_subgroup));
	}

	bool allow_small_types = true;
	bool forces_small_types = false;
	if (const char *small = getenv("PARALLEL_RDP_SMALL_TYPES"))
	{
		allow_small_types = strtol(small, nullptr, 0) > 0;
		forces_small_types = true;
		LOGI("Allow small types = %d.\n", int(allow_small_types));
	}

	if (!features.storage_16bit_features.storageBuffer16BitAccess)
	{
		LOGE("VK_KHR_16bit_storage for SSBOs is not supported! This is a minimum requirement for paraLLEl-RDP.\n");
		return false;
	}

	if (caps.no_storage_8bit)
	{
		LOGI("VK_KHR_8bit_storage for SSBOs is not used, falling back to 16/32-bit storage.\n");
		allow_small_types = false;
		forces_small_types = true;
	}
	else if (!features.storage_8bit_features.storageBuffer8BitAccess)
	{
		LOGE("VK_KHR_8bit_storage for SSBOs is not supported! This is a minimum requirement for paraLLEl-RDP.\n");
		return false;
	}

	// Driver workarounds here for 8/16-bit integer support.
	if (features.supports_driver_properties && !forces_small_types)
	{
		if (features.driver_properties.driverID == VK_DRIVER_ID_AMD_PROPRIETARY_KHR)
		{
			LOGW("Current proprietary AMD driver is known to be buggy with 8/16-bit integer arithmetic, disabling support for time being.\n");
			allow_small_types = false;
		}
		else if (features.driver_properties.driverID == VK_DRIVER_ID_AMD_OPEN_SOURCE_KHR ||
		         features.driver_properties.driverID == VK_DRIVER_ID_MESA_RADV_KHR)
		{
			LOGW("Current open-source AMD drivers are known to be slightly faster without 8/16-bit integer arithmetic.\n");
			allow_small_types = false;
		}
		else if (features.driver_properties.driverID == VK_DRIVER_ID_NVIDIA_PROPRIETARY_KHR)
		{
			LOGW("Current NVIDIA driver is known to be slightly faster without 8/16-bit integer arithmetic.\n");
			allow_small_types = false;
		}
		else if (features.driver_properties.driverID == VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS_KHR)
		{
			LOGW("Current proprietary Intel Windows driver is tested to perform much better without 8/16-bit integer support.\n");
			allow_small_types = false;
		}
		else if (features.driver_properties.driverID == VK_DRIVER_ID_QUALCOMM_PROPRIETARY_KHR)
		{
			LOGW("Current proprietary Qcom driver is known to be buggy with 8/16-bit integer arithmetic, disabling support for time being.\n");
			allow_small_types = false;
		}

		// Intel ANV *must* use small integer arithmetic, or it doesn't pass test suite.
	}

	if (!allow_small_types)
	{
		caps.supports_small_integer_arithmetic = false;
	}
	else if (features.enabled_features.shaderInt16 && features.float16_int8_features.shaderInt8)
	{
		LOGI("Enabling 8 and 16-bit integer arithmetic support for more efficient shaders!\n");
		caps.supports_small_integer_arithmetic = true;
	}
	else
	{
		LOGW("Device does not support 8 and 16-bit integer arithmetic support. Falling back to 32-bit arithmetic everywhere.\n");
		caps.supports_small_integer_arithmetic = false;
	}

	uint32_t subgroup_size = features.subgroup_properties.subgroupSize;

	const VkSubgroupFeatureFlags required =
			VK_SUBGROUP_FEATURE_BALLOT_BIT |
			VK_SUBGROUP_FEATURE_BASIC_BIT |
			VK_SUBGROUP_FEATURE_VOTE_BIT |
			VK_SUBGROUP_FEATURE_ARITHMETIC_BIT;

	caps.subgroup_tile_binning =
			allow_subgroup &&
			(features.subgroup_properties.supportedOperations & required) == required &&
			(features.subgroup_properties.supportedStages & VK_SHADER_STAGE_COMPUTE_BIT) != 0 &&
			can_support_minimum_subgroup_size(32) && subgroup_size <= 64;

	caps.subgroup_depth_blend =
			caps.super_sample_readback &&
			allow_subgroup &&
			(features.subgroup_properties.supportedOperations & required) == required &&
			(features.subgroup_properties.supportedStages & VK_SHADER_STAGE_COMPUTE_BIT) != 0;

	return true;
}

int Renderer::resolve_shader_define(const char *name, const char *define) const
{
	if (strcmp(define, "DEBUG_ENABLE") == 0)
		return 0;
	else if (strcmp(define, "UBERSHADER") == 0)
		return int(caps.ubershader);
	else if (strcmp(define, "SMALL_TYPES") == 0)
		return int(caps.supports_small_integer_arithmetic);
	else if (strcmp(define, "NO_STORAGE_8BIT") == 0)
		return int(caps.no_storage_8bit);
	else if (strcmp(define, "SUBGROUP") == 0)
	{
		if (strcmp(name, "tile_binning_combined") == 0)
			return int(caps.subgroup_tile_binning);
		else if (strcmp(name, "depth_blend") == 0 || strcmp(name, "ubershader") == 0)
			return int(caps.subgroup_depth_blend);
		else
			return 0;
	}
	else
		return 0;
}

void Renderer::init_buffers(const RendererOptions &options)
{
	Vulkan::BufferCreateInfo info = {};
	info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	info.domain = Vulkan::BufferDomain::Device;
	info.misc = Vulkan::BUFFER_MISC_ZERO_INITIALIZE_BIT;

	static_assert((Limits::MaxPrimitives % 32) == 0, "MaxPrimitives must be divisble by 32.");
	static_assert(Limits::MaxPrimitives <= (32 * 32), "MaxPrimitives must be less-or-equal than 1024.");

	info.size = sizeof(uint32_t) *
	            (Limits::MaxPrimitives / 32) *
	            (caps.max_width / ImplementationConstants::TileWidth) *
	            (caps.max_height / ImplementationConstants::TileHeight);

	tile_binning_buffer = device->create_buffer(info);

	info.size = sizeof(uint32_t) *
	            (caps.max_width / ImplementationConstants::TileWidth) *
	            (caps.max_height / ImplementationConstants::TileHeight);

	tile_binning_buffer_coarse = device->create_buffer(info);

	if (!caps.ubershader)
	{
		info.size = sizeof(uint32_t) *
		            (Limits::MaxPrimitives / 32) *
		            (caps.max_width / ImplementationConstants::TileWidth) *
		            (caps.max_height / ImplementationConstants::TileHeight);

		per_tile_offsets = device->create_buffer(info);

		info.size = sizeof(TileRasterWork) * caps.max_static_raster_states * caps.max_num_tile_instances;
		tile_work_list = device->create_buffer(info);

		info.size = sizeof(uint32_t) *
		            caps.max_num_tile_instances *
		            ImplementationConstants::TileWidth *
		            ImplementationConstants::TileHeight;
		per_tile_shaded_color = device->create_buffer(info);
		per_tile_shaded_depth = device->create_buffer(info);

		info.size = sizeof(uint8_t) *
		            caps.max_num_tile_instances *
		            ImplementationConstants::TileWidth *
		            ImplementationConstants::TileHeight;
		per_tile_shaded_shaded_alpha = device->create_buffer(info);
		// Without 8-bit storage, coverage and shade alpha share one 16-bit
		// element in the coverage buffer (rasterizer.comp).
		if (caps.no_storage_8bit)
			info.size *= sizeof(uint16_t);
		per_tile_shaded_coverage = device->create_buffer(info);
	}
}

void Renderer::init_blender_lut()
{
	Vulkan::BufferCreateInfo info = {};
	info.size = sizeof(blender_lut);
	info.domain = Vulkan::BufferDomain::Device;
	info.usage = VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;

	blender_divider_lut_buffer = device->create_buffer(info, blender_lut);

	Vulkan::BufferViewCreateInfo view = {};
	view.buffer = blender_divider_lut_buffer.get();
	view.format = VK_FORMAT_R8_UINT;
	view.range = info.size;
	blender_divider_buffer = device->create_buffer_view(view);
}

void Renderer::RenderBuffers::init(Vulkan::Device &device, granite_buffer_domain_t domain,
                                   RenderBuffers *borrow)
{
	triangle_setup = create_buffer(device, domain,
	                               sizeof(TriangleSetup) * Limits::MaxPrimitives,
	                               borrow ? &borrow->triangle_setup : nullptr);

	attribute_setup = create_buffer(device, domain,
	                                sizeof(AttributeSetup) * Limits::MaxPrimitives,
	                                borrow ? &borrow->attribute_setup: nullptr);

	derived_setup = create_buffer(device, domain,
	                              sizeof(DerivedSetup) * Limits::MaxPrimitives,
	                              borrow ? &borrow->derived_setup : nullptr);

	scissor_setup = create_buffer(device, domain,
	                              sizeof(ScissorState) * Limits::MaxPrimitives,
	                              borrow ? &borrow->scissor_setup : nullptr);

	static_raster_state = create_buffer(device, domain,
	                                    sizeof(StaticRasterizationState) * Limits::MaxStaticRasterizationStates,
	                                    borrow ? &borrow->static_raster_state : nullptr);

	depth_blend_state = create_buffer(device, domain,
	                                  sizeof(DepthBlendState) * Limits::MaxDepthBlendStates,
	                                  borrow ? &borrow->depth_blend_state : nullptr);

	tile_info_state = create_buffer(device, domain,
	                                sizeof(TileInfo) * Limits::MaxTileInfoStates,
	                                borrow ? &borrow->tile_info_state : nullptr);

	state_indices = create_buffer(device, domain,
	                              sizeof(InstanceIndices) * Limits::MaxPrimitives,
	                              borrow ? &borrow->state_indices : nullptr);

	span_info_offsets = create_buffer(device, domain,
	                                  sizeof(SpanInfoOffsets) * Limits::MaxPrimitives,
	                                  borrow ? &borrow->span_info_offsets : nullptr);

	span_info_jobs = create_buffer(device, domain,
	                               sizeof(SpanInterpolationJob) * Limits::MaxSpanSetups,
	                               borrow ? &borrow->span_info_jobs : nullptr);

	if (!borrow)
	{
		Vulkan::BufferViewCreateInfo info = {};
		info.buffer = span_info_jobs.buffer.get();
		info.format = VK_FORMAT_R16G16B16A16_UINT;
		info.range = span_info_jobs.buffer->get_create_info().size;
		span_info_jobs_view = device.create_buffer_view(info);
	}
}

Renderer::MappedBuffer Renderer::RenderBuffers::create_buffer(
		Vulkan::Device &device, granite_buffer_domain_t domain, VkDeviceSize size,
		Renderer::MappedBuffer *borrow)
{
	Vulkan::BufferCreateInfo info = {};
	info.domain = domain;

	if (domain == Vulkan::BufferDomain::Device || domain == Vulkan::BufferDomain::LinkedDeviceHostPreferDevice)
	{
		info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
		             VK_BUFFER_USAGE_TRANSFER_DST_BIT |
		             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
		             VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
	}
	else if (borrow && borrow->is_host)
	{
		return *borrow;
	}
	else
	{
		info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	}

	info.size = size;
	Renderer::MappedBuffer buffer;
	buffer.buffer = device.create_buffer(info);
	buffer.is_host = device.map_host_buffer(*buffer.buffer, 0) != nullptr;
	return buffer;
}

void Renderer::RenderBuffersUpdater::init(Vulkan::Device &device)
{
	gpu.init(device, Vulkan::BufferDomain::LinkedDeviceHostPreferDevice, nullptr);
	cpu.init(device, Vulkan::BufferDomain::Host, &gpu);
}

bool Renderer::init_internal_upscaling_factor(const RendererOptions &options)
{
	unsigned factor = options.upscaling_factor;
	if (!device || !rdram || !hidden_rdram)
	{
		LOGE("Renderer is not initialized.\n");
		return false;
	}

	caps.upscaling = factor;
	caps.super_sample_readback = options.super_sampled_readback;
	caps.super_sample_readback_dither = options.super_sampled_readback_dither;

	if (factor == 1)
	{
		upscaling_multisampled_hidden_rdram.reset();
		upscaling_reference_rdram.reset();
		upscaling_multisampled_rdram.reset();
		return true;
	}

	Vulkan::BufferCreateInfo info;
	info.domain = Vulkan::BufferDomain::Device;
	info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	info.misc = Vulkan::BUFFER_MISC_ZERO_INITIALIZE_BIT;

	info.size = rdram_size;
	upscaling_reference_rdram = device->create_buffer(info);

	info.size = rdram_size * factor * factor;
	// If we're super-sampling we'll need to carry forward a u8 writemask per unscaled pixel.
	// The resolve pass will conditionally write a resolved pixel to avoid potential race conditions.
	// We allocate 2 bits per pixel (color / depth write).
	// The SSAA resolve shader will convert this to a VRAM write mask if we also need to handle incoherent
	// RDRAM.
	if (caps.super_sample_readback)
		info.size += 2 * Limits::MaxWidth * Limits::MaxHeight / 8;

	upscaling_multisampled_rdram = device->create_buffer(info);

	info.size = hidden_rdram->get_create_info().size * factor * factor;
	upscaling_multisampled_hidden_rdram = device->create_buffer(info);

	{
		// On the queue the renderer and VI use, and ordered before them on
		// every queue: a plain submit on the graphics queue could land after
		// the first frame's work on the compute queue and overwrite it.
		auto cmd = device->request_command_buffer(Vulkan::CommandBuffer::Type::AsyncCompute);
		cmd->fill_buffer(*upscaling_multisampled_hidden_rdram, caps.no_storage_8bit ? 0x00030003u : 0x03030303u);
		device->submit_initialization(cmd, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
	}

	return true;
}

void Renderer::set_rdram(Vulkan::Buffer *buffer, uint8_t *host_rdram, size_t offset, size_t size, bool coherent)
{
	rdram = buffer;
	rdram_offset = offset;
	rdram_size = size;
	is_host_coherent = coherent;

	if (!is_host_coherent)
	{
		assert(rdram_offset == 0);
		incoherent.host_rdram = host_rdram;

		// If we're not host coherent (missing VK_EXT_external_memory_host),
		// we need to create a staging RDRAM buffer which is used for the real RDRAM uploads.
		// RDRAM may be uploaded in a masked way (if GPU has pending writes), or direct copy (if no pending writes are outstanding).
		Vulkan::BufferCreateInfo info = {};
		info.size = size;
		info.domain = Vulkan::BufferDomain::Host;
		info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		incoherent.staging_rdram = device->create_buffer(info);

		if (!rdram->get_allocation().host_base)
		{
			// If we cannot map RDRAM, we need a staging readback buffer.
			Vulkan::BufferCreateInfo readback_info = {};
			readback_info.domain = Vulkan::BufferDomain::CachedCoherentHostPreferCached;
			readback_info.size = rdram_size * Limits::NumSyncStates;
			readback_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
			incoherent.staging_readback = device->create_buffer(readback_info);
			incoherent.staging_readback_pages = div_round_up(readback_info.size, ImplementationConstants::IncoherentPageSize);
		}

		{
			const VkMemoryPropertyFlags cached_coherent =
					VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
			const auto &allocation = rdram->get_allocation();
			incoherent.compare_before_upload = allocation.host_base &&
					(device->get_memory_properties().memoryTypes[allocation.memory_type].propertyFlags &
					 cached_coherent) == cached_coherent;
			// (every page uploaded whole, to compare against)
			if (const char *env = getenv("PARALLEL_RDP_FULL_RDRAM_UPLOAD"))
				if (strtol(env, nullptr, 0) > 0)
					incoherent.compare_before_upload = false;
		}

		auto packed_pages = div_round_up(size, ImplementationConstants::IncoherentPageSize * 32);
		incoherent.num_pages = div_round_up(size, ImplementationConstants::IncoherentPageSize);

		{
			size_t counters = sizeof(retro_atomic_int_t) * incoherent.num_pages;
			size_t bitmaps = sizeof(uint32_t) * packed_pages;
			size_t copies = sizeof(CoherencyCopy) * incoherent.num_pages;
			uint8_t *arena;
			unsigned i;

			counters = (counters + 7) & ~size_t(7);
			bitmaps = (bitmaps + 7) & ~size_t(7);
			free(incoherent.arena);
			arena = static_cast<uint8_t *>(calloc(1, counters + 3 * bitmaps + copies));
			incoherent.arena = arena;
			incoherent.packed_pages = unsigned(packed_pages);
			incoherent.pending_writes_for_page = reinterpret_cast<retro_atomic_int_t *>(arena);
			incoherent.page_to_direct_copy = reinterpret_cast<uint32_t *>(arena + counters);
			incoherent.page_to_masked_copy = reinterpret_cast<uint32_t *>(arena + counters + bitmaps);
			incoherent.page_to_pending_readback = reinterpret_cast<uint32_t *>(arena + counters + 2 * bitmaps);
			incoherent.copies = reinterpret_cast<CoherencyCopy *>(arena + counters + 3 * bitmaps);
			for (i = 0; i < incoherent.num_pages; i++)
				retro_atomic_int_init(&incoherent.pending_writes_for_page[i], 0);
		}
	}
	else
	{
		free(incoherent.arena);
		incoherent = {};
	}
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

template <typename Cache>
void Renderer::RenderBuffersUpdater::upload(Vulkan::CommandBuffer &cmd, Vulkan::Device &device,
                                            const MappedBuffer &gpu, const MappedBuffer &cpu, const Cache &cache,
                                            bool &did_upload)
{
	if (!cache.empty())
	{
		// The streams are built in place; only the state caches are copied.
		void *mapped = device.map_host_buffer(*cpu.buffer, Vulkan::MEMORY_ACCESS_WRITE_BIT);
		if (mapped != static_cast<const void *>(cache.data()))
			memcpy(mapped, cache.data(), cache.byte_size());
		device.unmap_host_buffer(*cpu.buffer, Vulkan::MEMORY_ACCESS_WRITE_BIT, 0, cache.byte_size());
		if (gpu.buffer != cpu.buffer)
		{
			cmd.copy_buffer(*gpu.buffer, 0, *cpu.buffer, 0, cache.byte_size());
			did_upload = true;
		}
	}
}

void Renderer::RenderBuffersUpdater::upload(Vulkan::Device &device, const Renderer::StreamCaches &caches,
                                            Vulkan::CommandBuffer &cmd)
{
	bool did_upload = false;

	upload(cmd, device, gpu.triangle_setup, cpu.triangle_setup, caches.triangle_setup, did_upload);
	upload(cmd, device, gpu.attribute_setup, cpu.attribute_setup, caches.attribute_setup, did_upload);
	upload(cmd, device, gpu.derived_setup, cpu.derived_setup, caches.derived_setup, did_upload);
	upload(cmd, device, gpu.scissor_setup, cpu.scissor_setup, caches.scissor_setup, did_upload);

	upload(cmd, device, gpu.static_raster_state, cpu.static_raster_state, caches.static_raster_state_cache, did_upload);
	upload(cmd, device, gpu.depth_blend_state, cpu.depth_blend_state, caches.depth_blend_state_cache, did_upload);
	upload(cmd, device, gpu.tile_info_state, cpu.tile_info_state, caches.tile_info_state_cache, did_upload);

	upload(cmd, device, gpu.state_indices, cpu.state_indices, caches.state_indices, did_upload);
	upload(cmd, device, gpu.span_info_offsets, cpu.span_info_offsets, caches.span_info_offsets, did_upload);
	upload(cmd, device, gpu.span_info_jobs, cpu.span_info_jobs, caches.span_info_jobs, did_upload);

	if (did_upload)
	{
		cmd.barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
		            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
	}
}

void Renderer::update_tmem_instances(Vulkan::CommandBuffer &cmd)
{
	cmd.set_storage_buffer(0, 0, *rdram, rdram_offset, rdram_size);
	cmd.set_storage_buffer(0, 1, *tmem);
	cmd.set_storage_buffer(0, 2, *tmem_instances);

	memcpy(cmd.allocate_typed_constant_data<UploadInfo>(1, 0, stream.num_tmem_uploads),
	       stream.tmem_upload_infos,
	       stream.num_tmem_uploads * sizeof(UploadInfo));

	auto count = uint32_t(stream.num_tmem_uploads);

	cmd.set_program(shader_bank->tmem_update);

	cmd.push_constants(&count, 0, sizeof(count));
	cmd.set_specialization_constant_mask(1);
	cmd.set_specialization_constant(0, ImplementationConstants::DefaultWorkgroupSize);

	cmd.dispatch(2048 / ImplementationConstants::DefaultWorkgroupSize, 1, 1);
}

void Renderer::set_span_setup_pipeline(Vulkan::CommandBuffer &cmd, bool upscale)
{
	cmd.set_program(shader_bank->span_setup);
	cmd.set_specialization_constant_mask(3);
	cmd.set_specialization_constant(0, (upscale ? caps.upscaling : 1) * ImplementationConstants::DefaultWorkgroupSize);
	cmd.set_specialization_constant(1, upscale ? trailing_zeroes(caps.upscaling) : 0u);
}

void Renderer::submit_span_setup_jobs(Vulkan::CommandBuffer &cmd, bool upscale)
{
	auto &instance = buffer_instances[buffer_instance];
	cmd.set_storage_buffer(0, 0, *instance.gpu.triangle_setup.buffer);
	cmd.set_storage_buffer(0, 1, *instance.gpu.attribute_setup.buffer);
	cmd.set_storage_buffer(0, 2, *instance.gpu.scissor_setup.buffer);
	cmd.set_storage_buffer(0, 3, *span_setups);

	cmd.set_buffer_view(1, 0, *instance.gpu.span_info_jobs_view);
	set_span_setup_pipeline(cmd, upscale);

	cmd.dispatch(stream.span_info_jobs.size(), 1, 1);
}

void Renderer::clear_indirect_buffer(Vulkan::CommandBuffer &cmd)
{

	cmd.set_program(shader_bank->clear_indirect_buffer);

	cmd.set_storage_buffer(0, 0, *indirect_dispatch_buffer);

	static_assert((Limits::MaxStaticRasterizationStates % ImplementationConstants::DefaultWorkgroupSize) == 0, "MaxStaticRasterizationStates does not align.");
	cmd.set_specialization_constant_mask(1);
	cmd.set_specialization_constant(0, ImplementationConstants::DefaultWorkgroupSize);
	cmd.dispatch(Limits::MaxStaticRasterizationStates / ImplementationConstants::DefaultWorkgroupSize, 1, 1);
}

void Renderer::submit_rasterization(Vulkan::CommandBuffer &cmd, Vulkan::Buffer &tmem, bool upscaling)
{
	auto &instance = buffer_instances[buffer_instance];

	cmd.set_storage_buffer(0, 0, *instance.gpu.triangle_setup.buffer);
	cmd.set_storage_buffer(0, 1, *instance.gpu.attribute_setup.buffer);
	cmd.set_storage_buffer(0, 2, *instance.gpu.derived_setup.buffer);
	cmd.set_storage_buffer(0, 3, *instance.gpu.static_raster_state.buffer);
	cmd.set_storage_buffer(0, 4, *instance.gpu.state_indices.buffer);
	cmd.set_storage_buffer(0, 5, *instance.gpu.span_info_offsets.buffer);
	cmd.set_storage_buffer(0, 6, *span_setups);
	cmd.set_storage_buffer(0, 7, tmem);
	cmd.set_storage_buffer(0, 8, *instance.gpu.tile_info_state.buffer);

	cmd.set_storage_buffer(0, 9, *per_tile_shaded_color);
	cmd.set_storage_buffer(0, 10, *per_tile_shaded_depth);
	cmd.set_storage_buffer(0, 11, *per_tile_shaded_shaded_alpha);
	cmd.set_storage_buffer(0, 12, *per_tile_shaded_coverage);

	auto *global_fb_info = cmd.allocate_typed_constant_data<GlobalFBInfo>(2, 0, 1);
	switch (fb.fmt)
	{
	case FBFormat::I4:
		global_fb_info->fb_size = 0;
		global_fb_info->dx_mask = 0;
		global_fb_info->dx_shift = 0;
		break;

	case FBFormat::I8:
		global_fb_info->fb_size = 1;
		global_fb_info->dx_mask = ~7u;
		global_fb_info->dx_shift = 3;
		break;

	case FBFormat::RGBA5551:
	case FBFormat::IA88:
		global_fb_info->fb_size = 2;
		global_fb_info->dx_mask = ~3u;
		global_fb_info->dx_shift = 2;
		break;

	case FBFormat::RGBA8888:
		global_fb_info->fb_size = 4;
		global_fb_info->dx_shift = ~1u;
		global_fb_info->dx_shift = 1;
		break;
	}

	global_fb_info->base_primitive_index = base_primitive_index;

	cmd.set_program(shader_bank->rasterizer);

	cmd.set_specialization_constant(0, ImplementationConstants::TileWidth);
	cmd.set_specialization_constant(1, ImplementationConstants::TileHeight);

	uint32_t scale_log2_bit = (upscaling ? trailing_zeroes(caps.upscaling) : 0u) << RASTERIZATION_UPSCALING_LOG2_BIT_OFFSET;

	for (size_t i = 0; i < stream.static_raster_state_cache.size(); i++)
	{
		cmd.set_storage_buffer(1, 0, *tile_work_list,
		                       i * sizeof(TileRasterWork) * caps.max_num_tile_instances,
		                       sizeof(TileRasterWork) * caps.max_num_tile_instances);

		auto &state = stream.static_raster_state_cache.data()[i];
		cmd.set_specialization_constant(2, state.flags | RASTERIZATION_USE_SPECIALIZATION_CONSTANT_BIT | scale_log2_bit);
		cmd.set_specialization_constant(3, state.combiner[0].rgb);
		cmd.set_specialization_constant(4, state.combiner[0].alpha);
		cmd.set_specialization_constant(5, state.combiner[1].rgb);
		cmd.set_specialization_constant(6, state.combiner[1].alpha);

		cmd.set_specialization_constant(7, state.dither |
		                                   (state.texture_size << 8u) |
		                                   (state.texture_fmt << 16u));
		cmd.set_specialization_constant_mask(0xff);

		if (!caps.force_sync && !cmd.flush_pipeline_state_without_blocking())
		{
			Vulkan::DeferredPipelineCompile compile;
			cmd.extract_pipeline_state(compile);
			/* (no node free: the next miss of this variant asks again) */
			auto *node = static_cast<PipelineNode *>(rdp_node_pool_acquire(&pipeline_node_pool));
			if (node && !rdp_hash_set_insert(&pending_async_pipelines, compile.hash))
			{
				rdp_node_pool_release(&pipeline_node_pool, node);
				node = nullptr;
			}
			if (node)
			{
				node->compile = rdp_move(compile);
				node->kind = PIPELINE_RECORD_COMPUTE;
				node->ahead = 0;
				node->urgent = 0;
				rdp_worker_push(pipeline_worker, node);
			}
			cmd.set_specialization_constant_mask(7);
			cmd.set_specialization_constant(2, scale_log2_bit);
		}

		cmd.dispatch_indirect(*indirect_dispatch_buffer, 4 * sizeof(uint32_t) * i);
	}

}

uint32_t Renderer::set_tile_binning_pipeline(Vulkan::CommandBuffer &cmd, bool upscale)
{
	uint32_t subgroup_size = device->get_device_features().subgroup_properties.subgroupSize;

	cmd.set_specialization_constant_mask(0x7f);
	cmd.set_specialization_constant(1, ImplementationConstants::TileWidth);
	cmd.set_specialization_constant(2, ImplementationConstants::TileHeight);
	cmd.set_specialization_constant(3, Limits::MaxPrimitives);
	cmd.set_specialization_constant(4, upscale ? caps.max_width : Limits::MaxWidth);
	cmd.set_specialization_constant(5, caps.max_num_tile_instances);
	cmd.set_specialization_constant(6, upscale ? caps.upscaling : 1u);

	cmd.set_program(shader_bank->tile_binning_combined);
	if (caps.subgroup_tile_binning)
	{
		if (supports_subgroup_size_control(32, subgroup_size))
		{
			cmd.enable_subgroup_size_control(true);
			cmd.set_subgroup_size_log2(true, 5, trailing_zeroes(subgroup_size));
		}
	}
	else
		subgroup_size = 32;

	cmd.set_specialization_constant(0, subgroup_size);
	return subgroup_size;
}

void Renderer::submit_tile_binning_combined(Vulkan::CommandBuffer &cmd, bool upscale)
{
	auto &instance = buffer_instances[buffer_instance];
	cmd.set_storage_buffer(0, 0, *instance.gpu.triangle_setup.buffer);
	cmd.set_storage_buffer(0, 1, *instance.gpu.scissor_setup.buffer);
	cmd.set_storage_buffer(0, 2, *instance.gpu.state_indices.buffer);
	cmd.set_storage_buffer(0, 3, *tile_binning_buffer);
	cmd.set_storage_buffer(0, 4, *tile_binning_buffer_coarse);

	if (!caps.ubershader)
	{
		cmd.set_storage_buffer(0, 5, *per_tile_offsets);
		cmd.set_storage_buffer(0, 6, *indirect_dispatch_buffer);
		cmd.set_storage_buffer(0, 7, *tile_work_list);
	}

	struct PushData
	{
		uint32_t width, height;
		uint32_t num_primitives;
	} push = {};
	push.width = fb.width;
	push.height = fb.deduced_height;

	if (upscale)
	{
		push.width *= caps.upscaling;
		push.height *= caps.upscaling;
	}

	push.num_primitives = uint32_t(stream.triangle_setup.size());
	unsigned num_primitives_32 = (push.num_primitives + 31) / 32;

	cmd.push_constants(&push, 0, sizeof(push));

	uint32_t subgroup_size = set_tile_binning_pipeline(cmd, upscale);

	unsigned meta_tiles_x = 8;
	unsigned meta_tiles_y = subgroup_size / meta_tiles_x;
	unsigned num_tiles_x = (push.width + ImplementationConstants::TileWidth - 1) / ImplementationConstants::TileWidth;
	unsigned num_tiles_y = (push.height + ImplementationConstants::TileHeight - 1) / ImplementationConstants::TileHeight;
	unsigned num_meta_tiles_x = (num_tiles_x + meta_tiles_x - 1) / meta_tiles_x;
	unsigned num_meta_tiles_y = (num_tiles_y + meta_tiles_y - 1) / meta_tiles_y;
	cmd.dispatch(num_primitives_32, num_meta_tiles_x, num_meta_tiles_y);

	cmd.enable_subgroup_size_control(false);
}

void Renderer::submit_update_upscaled_domain_external(Vulkan::CommandBuffer &cmd,
                                                      unsigned addr, unsigned length, unsigned pixel_size_log2)
{
	submit_update_upscaled_domain(cmd, ResolveStage::Pre, addr, addr, length, 1, pixel_size_log2);
}

void Renderer::set_upscaled_domain_pipeline(Vulkan::CommandBuffer &cmd, ResolveStage stage,
                                            unsigned pixel_size_log2, bool same_addr)
{
	if (stage == ResolveStage::Pre)
		cmd.set_program(shader_bank->update_upscaled_domain_pre);
	else if (stage == ResolveStage::Post)
		cmd.set_program(shader_bank->update_upscaled_domain_post);
	else
		cmd.set_program(shader_bank->update_upscaled_domain_resolve);

	cmd.set_specialization_constant_mask(0x7f);
	cmd.set_specialization_constant(0, uint32_t(rdram_size));
	cmd.set_specialization_constant(1, pixel_size_log2);
	cmd.set_specialization_constant(2, int(same_addr));
	cmd.set_specialization_constant(3, ImplementationConstants::DefaultWorkgroupSize);
	cmd.set_specialization_constant(4, caps.upscaling * caps.upscaling);
	if (stage == ResolveStage::SSAAResolve)
	{
		cmd.set_specialization_constant(5, uint32_t(caps.super_sample_readback_dither));
		cmd.set_specialization_constant(6, uint32_t(!is_host_coherent));
	}
}

void Renderer::submit_update_upscaled_domain(Vulkan::CommandBuffer &cmd, ResolveStage stage,
                                             unsigned addr, unsigned depth_addr,
                                             unsigned width, unsigned height,
                                             unsigned pixel_size_log2)
{
	unsigned num_pixels = width * height;

	if (stage != ResolveStage::SSAAResolve)
	{
		// Ensure that we always process entire words, thus we avoid having to do weird swizzles,
		// and memory access patterns are linear with gl_GlobalInvocationID.x.
		addr &= ~3u;
		depth_addr &= ~3u;
		unsigned align_pixels = 4u >> pixel_size_log2;
		num_pixels = (num_pixels + align_pixels - 1u) & ~(align_pixels - 1u);
	}

	cmd.set_storage_buffer(0, 0, *rdram, rdram_offset,
	                       (stage == ResolveStage::SSAAResolve && !is_host_coherent ? 2 : 1) * rdram_size);
	cmd.set_storage_buffer(0, 1, *hidden_rdram);
	cmd.set_storage_buffer(0, 2, *upscaling_reference_rdram);
	cmd.set_storage_buffer(0, 3, *upscaling_multisampled_rdram);
	cmd.set_storage_buffer(0, 4, *upscaling_multisampled_hidden_rdram);

	set_upscaled_domain_pipeline(cmd, stage, pixel_size_log2, addr == depth_addr);

	uint32_t num_workgroups_x, num_workgroups_y;

	if (stage == ResolveStage::SSAAResolve)
	{
		num_workgroups_x =
				(width + ImplementationConstants::DefaultWorkgroupSize - 1) /
				ImplementationConstants::DefaultWorkgroupSize;
		num_workgroups_y = height;
	}
	else
	{
		num_workgroups_x =
				(num_pixels + ImplementationConstants::DefaultWorkgroupSize - 1) /
				ImplementationConstants::DefaultWorkgroupSize;
		num_workgroups_y = 1;
	}

	struct Push
	{
		uint32_t pixels;
		uint32_t fb_addr, fb_depth_addr;
		uint32_t width, height;
	} push = {};
	push.pixels = num_pixels;
	push.fb_addr = addr >> pixel_size_log2;
	push.fb_depth_addr = depth_addr >> 1;
	push.width = width;
	push.height = height;

	cmd.push_constants(&push, 0, sizeof(push));

	cmd.dispatch(num_workgroups_x, num_workgroups_y, 1);
}

void Renderer::submit_clear_super_sample_write_mask(Vulkan::CommandBuffer &cmd, unsigned width, unsigned height)
{
	// Pack 4x4 block of pixel's writemasks in one u32. Allows for one depth_blend workgroup to target a single u32
	// in all cases, which is nice :)
	unsigned blocks_x = (width + 3) / 4;
	unsigned blocks_y = (height + 3) / 4;
	unsigned num_words = blocks_x * blocks_y;
	unsigned num_workgroups = (num_words + ImplementationConstants::DefaultWorkgroupSize - 1) /
	                          ImplementationConstants::DefaultWorkgroupSize;

	cmd.set_program(shader_bank->clear_super_sampled_write_mask);

	cmd.set_storage_buffer(0, 0, *upscaling_multisampled_rdram, rdram_size * caps.upscaling * caps.upscaling,
	                       2 * Limits::MaxWidth * Limits::MaxHeight / 8);

	cmd.set_specialization_constant_mask(1);
	cmd.set_specialization_constant(0, ImplementationConstants::DefaultWorkgroupSize);
	cmd.dispatch(num_workgroups, 1, 1);
	cmd.set_specialization_constant_mask(0);
}

void Renderer::submit_update_upscaled_domain(Vulkan::CommandBuffer &cmd, ResolveStage stage)
{
	unsigned pixel_size_log2;

	switch (fb.fmt)
	{
	case FBFormat::RGBA8888:
		pixel_size_log2 = 2;
		break;

	case FBFormat::RGBA5551:
	case FBFormat::IA88:
		pixel_size_log2 = 1;
		break;

	default:
		pixel_size_log2 = 0;
		break;
	}

	submit_update_upscaled_domain(cmd, stage, fb.addr, fb.depth_addr,
	                              fb.width, fb.deduced_height, pixel_size_log2);
}

void Renderer::set_depth_blend_pipeline(Vulkan::CommandBuffer &cmd, uint32_t fmt, bool same_addr,
                                        bool upscaled, bool force_write_mask)
{
	cmd.set_program(caps.ubershader ? shader_bank->ubershader : shader_bank->depth_blend);
	cmd.set_specialization_constant_mask(0xff);
	cmd.set_specialization_constant(0, uint32_t(rdram_size));
	cmd.set_specialization_constant(1, fmt);
	cmd.set_specialization_constant(2, int(same_addr));
	cmd.set_specialization_constant(3, ImplementationConstants::TileWidth);
	cmd.set_specialization_constant(4, ImplementationConstants::TileHeight);
	cmd.set_specialization_constant(5, Limits::MaxPrimitives);
	cmd.set_specialization_constant(6, upscaled ? caps.max_width : Limits::MaxWidth);
	cmd.set_specialization_constant(7, uint32_t(force_write_mask || (!is_host_coherent && !upscaled)) |
	                                   ((upscaled ? trailing_zeroes(caps.upscaling) : 0u) << 1u));
}

void Renderer::submit_depth_blend(Vulkan::CommandBuffer &cmd, Vulkan::Buffer &tmem, bool upscaled, bool force_write_mask)
{
	auto &instance = buffer_instances[buffer_instance];

	set_depth_blend_pipeline(cmd, uint32_t(fb.fmt), fb.addr == fb.depth_addr, upscaled, force_write_mask);

	if (upscaled)
		cmd.set_storage_buffer(0, 0, *upscaling_multisampled_rdram);
	else
		cmd.set_storage_buffer(0, 0, *rdram, rdram_offset, rdram_size * (is_host_coherent ? 1 : 2));
	cmd.set_storage_buffer(0, 1, upscaled ? *upscaling_multisampled_hidden_rdram : *hidden_rdram);
	cmd.set_storage_buffer(0, 2, tmem);

	if (!caps.ubershader)
	{
		cmd.set_storage_buffer(0, 3, *per_tile_shaded_color);
		cmd.set_storage_buffer(0, 4, *per_tile_shaded_depth);
		cmd.set_storage_buffer(0, 5, *per_tile_shaded_shaded_alpha);
		cmd.set_storage_buffer(0, 6, *per_tile_shaded_coverage);
		cmd.set_storage_buffer(0, 7, *per_tile_offsets);
	}

	cmd.set_storage_buffer(1, 0, *instance.gpu.triangle_setup.buffer);
	cmd.set_storage_buffer(1, 1, *instance.gpu.attribute_setup.buffer);
	cmd.set_storage_buffer(1, 2, *instance.gpu.derived_setup.buffer);
	cmd.set_storage_buffer(1, 3, *instance.gpu.scissor_setup.buffer);
	cmd.set_storage_buffer(1, 4, *instance.gpu.static_raster_state.buffer);
	cmd.set_storage_buffer(1, 5, *instance.gpu.depth_blend_state.buffer);
	cmd.set_storage_buffer(1, 6, *instance.gpu.state_indices.buffer);
	cmd.set_storage_buffer(1, 7, *instance.gpu.tile_info_state.buffer);
	cmd.set_storage_buffer(1, 8, *span_setups);
	cmd.set_storage_buffer(1, 9, *instance.gpu.span_info_offsets.buffer);
	cmd.set_buffer_view(1, 10, *blender_divider_buffer);
	cmd.set_storage_buffer(1, 11, *tile_binning_buffer);
	cmd.set_storage_buffer(1, 12, *tile_binning_buffer_coarse);

	auto *global_fb_info = cmd.allocate_typed_constant_data<GlobalFBInfo>(2, 0, 1);

	GlobalState push = {};
	push.fb_width = fb.width;
	push.fb_height = fb.deduced_height;

	if (upscaled)
	{
		push.fb_width *= caps.upscaling;
		push.fb_height *= caps.upscaling;
	}

	switch (fb.fmt)
	{
	case FBFormat::I4:
		push.addr_index = fb.addr;
		global_fb_info->fb_size = 0;
		global_fb_info->dx_mask = 0;
		global_fb_info->dx_shift = 0;
		break;

	case FBFormat::I8:
		push.addr_index = fb.addr;
		global_fb_info->fb_size = 1;
		global_fb_info->dx_mask = ~7u;
		global_fb_info->dx_shift = 3;
		break;

	case FBFormat::RGBA5551:
	case FBFormat::IA88:
		push.addr_index = fb.addr >> 1u;
		global_fb_info->fb_size = 2;
		global_fb_info->dx_mask = ~3u;
		global_fb_info->dx_shift = 2;
		break;

	case FBFormat::RGBA8888:
		push.addr_index = fb.addr >> 2u;
		global_fb_info->fb_size = 4;
		global_fb_info->dx_mask = ~1u;
		global_fb_info->dx_shift = 1;
		break;
	}

	global_fb_info->base_primitive_index = base_primitive_index;

	push.depth_addr_index = fb.depth_addr >> 1;
	unsigned num_primitives_32 = (stream.triangle_setup.size() + 31) / 32;
	push.group_mask = (1u << num_primitives_32) - 1;
	cmd.push_constants(&push, 0, sizeof(push));

	cmd.dispatch((push.fb_width + 7) / 8, (push.fb_height + 7) / 8, 1);

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
			submit_update_upscaled_domain(cmd, ResolveStage::Pre);
	}

	if (need_tmem_upload)
		update_tmem_instances(cmd);

	cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
	            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | (!caps.ubershader ? VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT : 0),
	            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
	            (!caps.ubershader ? VK_ACCESS_INDIRECT_COMMAND_READ_BIT : 0));

	if (need_render_pass && !caps.ubershader)
	{
		submit_rasterization(cmd, need_tmem_upload ? *tmem_instances : *tmem, false);
		cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
		            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
	}

	if (need_render_pass)
		submit_depth_blend(cmd, need_tmem_upload ? *tmem_instances : *tmem, false, false);

	if (!caps.ubershader)
		clear_indirect_buffer(cmd);

	if (render_pass_is_upscaled())
	{
		cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
		            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

		// TODO: Could probably do this reference update in the render pass itself,
		// just write output to two buffers ... This is more composable for now.
		submit_update_upscaled_domain(cmd, ResolveStage::Post);
	}
}

void Renderer::submit_render_pass_upscaled(Vulkan::CommandBuffer &cmd)
{

	bool need_tmem_upload = stream.num_tmem_uploads != 0;

	submit_span_setup_jobs(cmd, true);
	submit_tile_binning_combined(cmd, true);
	if (caps.super_sample_readback)
	{
		submit_update_upscaled_domain(cmd, ResolveStage::Pre);
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
		submit_rasterization(cmd, need_tmem_upload ? *tmem_instances : *tmem, true);
		cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		            VK_ACCESS_SHADER_WRITE_BIT,
		            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		            VK_ACCESS_SHADER_READ_BIT);
	}

	submit_depth_blend(cmd, need_tmem_upload ? *tmem_instances : *tmem, true, caps.super_sample_readback);
	if (!caps.ubershader)
		clear_indirect_buffer(cmd);

	if (caps.super_sample_readback)
	{
		cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
		            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

		submit_update_upscaled_domain(cmd, ResolveStage::SSAAResolve);
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
	stream.triangle_setup.bind(device->map_host_buffer(*instance.cpu.triangle_setup.buffer, w));
	stream.scissor_setup.bind(device->map_host_buffer(*instance.cpu.scissor_setup.buffer, w));
	stream.attribute_setup.bind(device->map_host_buffer(*instance.cpu.attribute_setup.buffer, w));
	stream.derived_setup.bind(device->map_host_buffer(*instance.cpu.derived_setup.buffer, w));
	stream.state_indices.bind(device->map_host_buffer(*instance.cpu.state_indices.buffer, w));
	stream.span_info_offsets.bind(device->map_host_buffer(*instance.cpu.span_info_offsets.buffer, w));
	stream.span_info_jobs.bind(device->map_host_buffer(*instance.cpu.span_info_jobs.buffer, w));
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
	if (byte_count == 0)
		return;

	uint32_t start_page = base_addr / ImplementationConstants::IncoherentPageSize;
	uint32_t end_page = (base_addr + byte_count - 1) / ImplementationConstants::IncoherentPageSize + 1;
	retro_atomic_int_t *pending = incoherent.pending_writes_for_page;

	// A page whose write-backs are all done is read straight from host
	// memory: the acquire load pairs with the timeline worker's decrement
	// after its copy, so the copy is seen (as a relaxed load and the fence
	// before the reads would be too, but thread sanitizers see no fences).
	mark_incoherent_pages_for_read(start_page, end_page - start_page, incoherent.num_pages,
	                               incoherent.page_to_pending_readback,
	                               [pending](uint32_t page) { return retro_atomic_load_acquire_int(&pending[page]) != 0; },
	                               incoherent.page_to_masked_copy, incoherent.page_to_direct_copy);
}

void Renderer::lock_pages_for_gpu_write(uint32_t base_addr, uint32_t byte_count)
{
	if (byte_count == 0)
		return;

	uint32_t start_page = base_addr / ImplementationConstants::IncoherentPageSize;
	uint32_t end_page = (base_addr + byte_count - 1) / ImplementationConstants::IncoherentPageSize + 1;

	for (uint32_t page = start_page; page < end_page; page++)
	{
		uint32_t wrapped_page = page & (incoherent.num_pages - 1);
		incoherent.page_to_pending_readback[wrapped_page / 32] |= 1u << (wrapped_page & 31);
	}
}

void Renderer::resolve_coherency_gpu_to_host(CoherencyOperation &op, Vulkan::CommandBuffer &cmd)
{
	op.copies = incoherent.copies;
	op.num_copies = 0;
	if (!incoherent.staging_readback)
	{
		// iGPU path.
		op.src = rdram;
		op.dst = incoherent.host_rdram;
		op.timeline_value = 0;

		unsigned word;
		for (word = 0; word < incoherent.packed_pages; word++)
		{
			uint32_t &readback = incoherent.page_to_pending_readback[word];
			uint32_t base_index = 32 * word;

			for (uint32_t index_bits = readback, index_base = 0, index, count; Util::next_bit_range(index_bits, index_base, index, count);)
			{
				index += base_index;

				for (unsigned i = 0; i < count; i++)
					retro_atomic_fetch_add_int(&incoherent.pending_writes_for_page[index + i], 1);

				CoherencyCopy coherent_copy = {};
				coherent_copy.counter_base = &incoherent.pending_writes_for_page[index];
				coherent_copy.counters = count;
				coherent_copy.src_offset = index * ImplementationConstants::IncoherentPageSize;
				coherent_copy.mask_offset = coherent_copy.src_offset + rdram_size;
				coherent_copy.dst_offset = index * ImplementationConstants::IncoherentPageSize;
				coherent_copy.size = ImplementationConstants::IncoherentPageSize * count;
				op.copies[op.num_copies++] = coherent_copy;
			}

			readback = 0;
		}
	}
	else
	{
		// Discrete GPU path.
		Util::SmallVector<VkBufferCopy, 1024> copies;
		op.src = incoherent.staging_readback.get();
		op.dst = incoherent.host_rdram;
		op.timeline_value = 0;

		unsigned word;
		for (word = 0; word < incoherent.packed_pages; word++)
		{
			uint32_t &readback = incoherent.page_to_pending_readback[word];
			uint32_t base_index = 32 * word;

			for (uint32_t index_bits = readback, index_base = 0, index, count; Util::next_bit_range(index_bits, index_base, index, count);)
			{
				index += base_index;

				for (unsigned i = 0; i < count; i++)
					retro_atomic_fetch_add_int(&incoherent.pending_writes_for_page[index + i], 1);

				VkBufferCopy copy = {};
				copy.srcOffset = index * ImplementationConstants::IncoherentPageSize;

				unsigned dst_page_index = incoherent.staging_readback_index;
				copy.dstOffset = dst_page_index * ImplementationConstants::IncoherentPageSize;

				incoherent.staging_readback_index += count;
				incoherent.staging_readback_index &= (incoherent.staging_readback_pages - 1);
				// Unclean wraparound check.
				if (incoherent.staging_readback_index != 0 && incoherent.staging_readback_index < dst_page_index)
				{
					copy.dstOffset = 0;
					incoherent.staging_readback_index = count;
				}

				copy.size = ImplementationConstants::IncoherentPageSize * count;
				copies.push_back(copy);

				CoherencyCopy coherent_copy = {};
				coherent_copy.counter_base = &incoherent.pending_writes_for_page[index];
				coherent_copy.counters = count;
				coherent_copy.src_offset = copy.dstOffset;
				coherent_copy.dst_offset = index * ImplementationConstants::IncoherentPageSize;
				coherent_copy.size = ImplementationConstants::IncoherentPageSize * count;

				VkBufferCopy mask_copy = {};
				mask_copy.srcOffset = index * ImplementationConstants::IncoherentPageSize + rdram_size;

				dst_page_index = incoherent.staging_readback_index;
				mask_copy.dstOffset = dst_page_index * ImplementationConstants::IncoherentPageSize;

				incoherent.staging_readback_index += count;
				incoherent.staging_readback_index &= (incoherent.staging_readback_pages - 1);
				// Unclean wraparound check.
				if (incoherent.staging_readback_index != 0 && incoherent.staging_readback_index < dst_page_index)
				{
					mask_copy.dstOffset = 0;
					incoherent.staging_readback_index = count;
				}

				mask_copy.size = ImplementationConstants::IncoherentPageSize * count;
				copies.push_back(mask_copy);
				coherent_copy.mask_offset = mask_copy.dstOffset;

				op.copies[op.num_copies++] = coherent_copy;
			}

			readback = 0;
		}

		if (!copies.empty())
		{
			cmd.copy_buffer(*incoherent.staging_readback, *rdram, copies.data(), copies.size());
			cmd.barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
			            VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			            VK_ACCESS_HOST_READ_BIT);
		}
	}
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
	return upscaling_multisampled_rdram.get();
}

const Vulkan::Buffer *Renderer::get_upscaled_hidden_rdram_buffer() const
{
	return upscaling_multisampled_hidden_rdram.get();
}

/* The direct upload of @count pages from @index, of only what differs: a
 * page the same as the GPU's copy is not written, and its write mask is
 * cleared only when it holds a bit. RDRAM ends up as a full upload leaves
 * it, whatever wrote the guest's pages; no page here has a GPU write
 * pending. */
void Renderer::upload_pages_compared(unsigned index, unsigned count)
{
	static const uint8_t zero_page[ImplementationConstants::IncoherentPageSize] = {};
	const size_t page_size = ImplementationConstants::IncoherentPageSize;
	const size_t offset = page_size * index;
	const size_t size = page_size * count;
	const Vulkan::MemoryAccessFlags access = Vulkan::MEMORY_ACCESS_READ_BIT | Vulkan::MEMORY_ACCESS_WRITE_BIT;
	const uint8_t *host = incoherent.host_rdram + offset;
#ifdef PARALLEL_RDP_VERIFY_UPLOADS
	// The guest's pages as this upload reads them, which another thread
	// may be writing meanwhile: what a full upload of them would leave.
	static uint8_t seen[32 * ImplementationConstants::IncoherentPageSize];
	memcpy(seen, host, size);
	host = seen;
#endif
	uint8_t *data = static_cast<uint8_t *>(device->map_host_buffer(*rdram, access, offset, size));
	uint8_t *mask = static_cast<uint8_t *>(device->map_host_buffer(*rdram, access, offset + rdram_size, size));
	size_t i;

	for (i = 0; i < size; i += page_size)
	{
		if (memcmp(data + i, host + i, page_size))
		{
			memcpy(data + i, host + i, page_size);
			memset(mask + i, 0, page_size);
		}
		else if (memcmp(mask + i, zero_page, page_size))
			memset(mask + i, 0, page_size);
	}

#ifdef PARALLEL_RDP_VERIFY_UPLOADS
	for (i = 0; i < size; i += page_size)
		if (memcmp(data + i, host + i, page_size) || memcmp(mask + i, zero_page, page_size))
		{
			fprintf(stderr, "parallel-rdp: RDRAM page %u is not as a full upload leaves it\n",
			        unsigned(index + i / page_size));
			abort();
		}
#endif

	device->unmap_host_buffer(*rdram, access, offset + rdram_size, size);
	device->unmap_host_buffer(*rdram, access, offset, size);
}

void Renderer::resolve_coherency_host_to_gpu(Vulkan::CommandBuffer &cmd)
{
	// Now, ensure that the GPU sees a coherent view of the CPU memory writes up until now.
	// Writes made by the GPU which are not known to be resolved on the timeline waiter thread will always
	// "win" over writes made by CPU, since CPU is not allowed to meaningfully overwrite data which the GPU
	// is going to touch.


	retro_atomic_thread_fence_acquire();

	Util::SmallVector<VkBufferCopy, 1024> buffer_copies;
	Util::SmallVector<uint32_t, 1024> masked_page_copies;
	Util::SmallVector<uint32_t, 1024> to_clear_write_mask;

	// If we're able to map RDRAM directly, we can just memcpy straight into RDRAM if we have an unmasked copy.
	// Important for iGPU.
	if (rdram->get_allocation().host_base)
	{
		unsigned word;
		for (word = 0; word < incoherent.packed_pages; word++)
		{
			uint32_t &direct = incoherent.page_to_direct_copy[word];
			uint32_t base_index = 32 * word;
			for (uint32_t index_bits = direct, index_base = 0, index, count; Util::next_bit_range(index_bits, index_base, index, count);)
			{
				index += base_index;
				if (incoherent.compare_before_upload)
				{
					upload_pages_compared(index, count);
					continue;
				}
				auto *mapped_rdram = device->map_host_buffer(*rdram, Vulkan::MEMORY_ACCESS_WRITE_BIT,
				                                             ImplementationConstants::IncoherentPageSize * index,
				                                             ImplementationConstants::IncoherentPageSize * count);
				memcpy(mapped_rdram,
				       incoherent.host_rdram + ImplementationConstants::IncoherentPageSize * index,
				       ImplementationConstants::IncoherentPageSize * count);

				device->unmap_host_buffer(*rdram, Vulkan::MEMORY_ACCESS_WRITE_BIT,
				                          ImplementationConstants::IncoherentPageSize * index,
				                          ImplementationConstants::IncoherentPageSize * count);

				mapped_rdram = device->map_host_buffer(*rdram, Vulkan::MEMORY_ACCESS_WRITE_BIT,
				                                       ImplementationConstants::IncoherentPageSize * index + rdram_size,
				                                       ImplementationConstants::IncoherentPageSize * count);

				memset(mapped_rdram, 0, ImplementationConstants::IncoherentPageSize * count);

				device->unmap_host_buffer(*rdram, Vulkan::MEMORY_ACCESS_WRITE_BIT,
				                          ImplementationConstants::IncoherentPageSize * index + rdram_size,
				                          ImplementationConstants::IncoherentPageSize * count);
			}
			direct = 0;
		}

		auto *mapped_staging = static_cast<uint8_t *>(device->map_host_buffer(*incoherent.staging_rdram,
		                                                                      Vulkan::MEMORY_ACCESS_WRITE_BIT));

		for (word = 0; word < incoherent.packed_pages; word++)
		{
			uint32_t &indirect = incoherent.page_to_masked_copy[word];
			uint32_t base_index = 32 * word;
			for (uint32_t index_bits = indirect, index; Util::next_bit(index_bits, index);)
			{
				index += base_index;
				masked_page_copies.push_back(index);
				memcpy(mapped_staging + ImplementationConstants::IncoherentPageSize * index,
				       incoherent.host_rdram + ImplementationConstants::IncoherentPageSize * index,
				       ImplementationConstants::IncoherentPageSize);
			}
			indirect = 0;
		}

		device->unmap_host_buffer(*incoherent.staging_rdram, Vulkan::MEMORY_ACCESS_WRITE_BIT);
	}
	else
	{
		auto *mapped_rdram = static_cast<uint8_t *>(device->map_host_buffer(*incoherent.staging_rdram, Vulkan::MEMORY_ACCESS_WRITE_BIT));

		size_t num_packed_pages = incoherent.packed_pages;
		for (size_t i = 0; i < num_packed_pages; i++)
		{
			uint32_t base_index = 32 * i;
			uint32_t tmp = incoherent.page_to_masked_copy[i] | incoherent.page_to_direct_copy[i];
			for (uint32_t index_bits = tmp, index; Util::next_bit(index_bits, index);)
			{
				unsigned bit = index;
				index += base_index;

				if ((1u << bit) & incoherent.page_to_masked_copy[i])
					masked_page_copies.push_back(index);
				else
				{
					VkBufferCopy copy = {};
					copy.size = ImplementationConstants::IncoherentPageSize;
					copy.dstOffset = copy.srcOffset = index * ImplementationConstants::IncoherentPageSize;
					buffer_copies.push_back(copy);
					to_clear_write_mask.push_back(index);
				}

				memcpy(mapped_rdram + ImplementationConstants::IncoherentPageSize * index,
				       incoherent.host_rdram + ImplementationConstants::IncoherentPageSize * index,
				       ImplementationConstants::IncoherentPageSize);
			}

			incoherent.page_to_masked_copy[i] = 0;
			incoherent.page_to_direct_copy[i] = 0;
		}

		device->unmap_host_buffer(*incoherent.staging_rdram, Vulkan::MEMORY_ACCESS_WRITE_BIT);
	}

	if (!masked_page_copies.empty())
	{
		cmd.set_program(shader_bank->masked_rdram_resolve);
		cmd.set_specialization_constant_mask(3);
		cmd.set_specialization_constant(0, ImplementationConstants::IncoherentPageSize / 4);
		cmd.set_specialization_constant(1, ImplementationConstants::IncoherentPageSize / 4);

		cmd.set_storage_buffer(0, 0, *rdram, rdram_offset, rdram_size);
		cmd.set_storage_buffer(0, 1, *incoherent.staging_rdram);
		cmd.set_storage_buffer(0, 2, *rdram, rdram_offset + rdram_size, rdram_size);

		for (size_t i = 0; i < masked_page_copies.size(); i += 4096)
		{
			size_t to_copy = rdp_min(masked_page_copies.size() - i, size_t(4096));
			memcpy(cmd.allocate_typed_constant_data<uint32_t>(1, 0, to_copy),
				   masked_page_copies.data() + i,
				   to_copy * sizeof(uint32_t));
			cmd.dispatch(to_copy, 1, 1);
		}
	}

	// Could use FillBuffer here, but would need to use TRANSFER stage, and introduce more barriers than needed.
	if (!to_clear_write_mask.empty())
	{
		cmd.set_program(shader_bank->clear_write_mask);
		cmd.set_specialization_constant_mask(3);
		cmd.set_specialization_constant(0, ImplementationConstants::IncoherentPageSize / 4);
		cmd.set_specialization_constant(1, ImplementationConstants::IncoherentPageSize / 4);
		cmd.set_storage_buffer(0, 0, *rdram, rdram_offset + rdram_size, rdram_size);
		for (size_t i = 0; i < to_clear_write_mask.size(); i += 4096)
		{
			size_t to_copy = rdp_min(to_clear_write_mask.size() - i, size_t(4096));
			memcpy(cmd.allocate_typed_constant_data<uint32_t>(1, 0, to_copy),
				   to_clear_write_mask.data() + i,
				   to_copy * sizeof(uint32_t));
			cmd.dispatch(to_copy, 1, 1);
		}
	}

	if (!to_clear_write_mask.empty() || !masked_page_copies.empty())
	{
		cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
		            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
	}

	// If we cannot map the device memory, copy. We're latency sensitive, so don't use DMA queue.
	if (!buffer_copies.empty())
	{
		cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
		            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);

		cmd.copy_buffer(*rdram, *incoherent.staging_rdram, buffer_copies.data(), buffer_copies.size());

		cmd.barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
		            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
	}

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
	instance.upload(*device, stream, *stream.cmd);

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

	if (!caps.ubershader && !indirect_dispatch_buffer)
	{
		Vulkan::BufferCreateInfo indirect_info = {};
		indirect_info.size = 4 * sizeof(uint32_t) * Limits::MaxStaticRasterizationStates;
		indirect_info.domain = Vulkan::BufferDomain::Device;
		indirect_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;

		indirect_dispatch_buffer = device->create_buffer(indirect_info);

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

bool Renderer::can_support_minimum_subgroup_size(unsigned size) const
{
	return supports_subgroup_size_control(size, device->get_device_features().subgroup_properties.subgroupSize);
}

bool Renderer::supports_subgroup_size_control(uint32_t minimum_size, uint32_t maximum_size) const
{
	auto &features = device->get_device_features();

	if (!features.subgroup_size_control_features.computeFullSubgroups)
		return false;

	bool use_varying = minimum_size <= features.subgroup_size_control_properties.minSubgroupSize &&
	                   maximum_size >= features.subgroup_size_control_properties.maxSubgroupSize;

	if (!use_varying)
	{
		bool outside_range = minimum_size > features.subgroup_size_control_properties.maxSubgroupSize ||
		                     maximum_size < features.subgroup_size_control_properties.minSubgroupSize;
		if (outside_range)
			return false;

		if ((features.subgroup_size_control_properties.requiredSubgroupSizeStages & VK_SHADER_STAGE_COMPUTE_BIT) == 0)
			return false;
	}

	return true;
}

void Renderer::pipeline_work(void *user, mpsc_stack_node_t *item)
{
	auto *renderer = static_cast<Renderer *>(user);
	auto *device = renderer->device;
	auto *node = static_cast<PipelineNode *>(item);

	/* Not one built meanwhile (ahead, or by a draw), and none past
	 * teardown: content close does not wait for the queue. */
	if (!retro_atomic_load_acquire_int(&renderer->pipeline_ahead_stop) &&
	    granite_pipeline_map_find(&node->compile.program->pipelines, node->compile.hash).pipeline == VK_NULL_HANDLE)
	{
		if (node->kind == PIPELINE_RECORD_GRAPHICS)
			Vulkan::CommandBuffer::build_graphics_pipeline(device, node->compile,
			                                               Vulkan::CommandBuffer::CompileMode::AsyncThread);
		else
			Vulkan::CommandBuffer::build_compute_pipeline(device, node->compile,
			                                              Vulkan::CommandBuffer::CompileMode::AsyncThread);
	}
	if (node->urgent)
		retro_atomic_fetch_add_int(&renderer->pipeline_ahead_urgent_done, 1);
	if (!node->ahead)
		rdp_node_pool_release(&renderer->pipeline_node_pool, node);
}

/* The compute pipelines every frame can need, whose settings all follow from
 * the renderer's own options: no draw has to build one, even on a first run.
 * The specialised rasterizer variants are not here: the generic one covers
 * them. Each uses the state setter its draw uses, so the two cannot differ. */
void Renderer::add_known_pipeline(void *list_, granite_command_buffer_t *cb, uint32_t kind)
{
	auto &cmd = *static_cast<Vulkan::CommandBuffer *>(cb);
	auto *list = static_cast<KnownPipelines *>(list_);
	auto *nodes = static_cast<PipelineNode *>(list->nodes);
	PipelineNode *node = &nodes[list->n];
	size_t i;

	if (list->n >= list->limit)
		return;
	cmd.extract_pipeline_state(node->compile);
	if (granite_pipeline_map_find(&node->compile.program->pipelines, node->compile.hash).pipeline != VK_NULL_HANDLE)
		return;
	for (i = 0; i < list->n; i++)
		if (nodes[i].kind == kind && nodes[i].compile.hash == node->compile.hash)
			return;
	node->kind   = kind;
	node->ahead  = 1;
	node->urgent = 1;
	list->renderer->pipeline_ahead_urgent++;
	list->n++;
}

size_t Renderer::queue_known_pipelines(void *nodes_, size_t n, VideoInterface *vi, bool with_upscaler)
{
	KnownPipelines list;
	const bool can_upscale = caps.upscaling > 1;
	auto cmd_handle = device->request_command_buffer(Vulkan::CommandBuffer::Type::AsyncCompute);
	auto &cmd = *cmd_handle;

	list.nodes    = nodes_;
	list.n        = n;
	list.limit    = n + MaxKnownPipelines;
	list.renderer = this;

	const auto add = [&]() { add_known_pipeline(&list, &cmd, PIPELINE_RECORD_COMPUTE); };

	cmd.set_program(shader_bank->tmem_update);
	cmd.set_specialization_constant_mask(1);
	cmd.set_specialization_constant(0, ImplementationConstants::DefaultWorkgroupSize);
	add();

	cmd.set_program(shader_bank->clear_indirect_buffer);
	cmd.set_specialization_constant_mask(1);
	cmd.set_specialization_constant(0, ImplementationConstants::DefaultWorkgroupSize);
	add();

	for (unsigned upscaled = 0; upscaled < (can_upscale ? 2u : 1u); upscaled++)
	{
		set_span_setup_pipeline(cmd, upscaled != 0);
		add();

		set_tile_binning_pipeline(cmd, upscaled != 0);
		add();
		cmd.enable_subgroup_size_control(false);

		if (!caps.ubershader)
		{
			cmd.set_program(shader_bank->rasterizer);
			cmd.set_specialization_constant_mask(7);
			cmd.set_specialization_constant(0, ImplementationConstants::TileWidth);
			cmd.set_specialization_constant(1, ImplementationConstants::TileHeight);
			cmd.set_specialization_constant(2, (upscaled ? trailing_zeroes(caps.upscaling) : 0u)
			                                   << RASTERIZATION_UPSCALING_LOG2_BIT_OFFSET);
			add();
		}

		for (uint32_t fmt = uint32_t(FBFormat::I4); fmt <= uint32_t(FBFormat::RGBA8888); fmt++)
			for (unsigned same_addr = 0; same_addr < 2; same_addr++)
			{
				set_depth_blend_pipeline(cmd, fmt, same_addr != 0, upscaled != 0,
				                         upscaled != 0 && caps.super_sample_readback);
				add();
			}
	}

	if (can_upscale)
	{
		static const ResolveStage stages[] = { ResolveStage::Pre, ResolveStage::Post, ResolveStage::SSAAResolve };
		for (auto stage : stages)
			for (unsigned pixel_size_log2 = 0; pixel_size_log2 <= 2; pixel_size_log2++)
				for (unsigned same_addr = 0; same_addr < 2; same_addr++)
				{
					set_upscaled_domain_pipeline(cmd, stage, pixel_size_log2, same_addr != 0);
					add();
				}

		cmd.set_program(shader_bank->clear_super_sampled_write_mask);
		cmd.set_specialization_constant_mask(1);
		cmd.set_specialization_constant(0, ImplementationConstants::DefaultWorkgroupSize);
		add();
	}

	if (!is_host_coherent)
	{
		cmd.set_program(shader_bank->masked_rdram_resolve);
		cmd.set_specialization_constant_mask(3);
		cmd.set_specialization_constant(0, ImplementationConstants::IncoherentPageSize / 4);
		cmd.set_specialization_constant(1, ImplementationConstants::IncoherentPageSize / 4);
		add();

		cmd.set_program(shader_bank->clear_write_mask);
		cmd.set_specialization_constant_mask(3);
		cmd.set_specialization_constant(0, ImplementationConstants::IncoherentPageSize / 4);
		cmd.set_specialization_constant(1, ImplementationConstants::IncoherentPageSize / 4);
		add();
	}

	device->submit(cmd_handle);

	if (vi)
		vi->queue_known_pipelines(caps.upscaling, with_upscaler, add_known_pipeline, &list);
	return list.n;
}

void Renderer::build_pipelines_ahead(const pipeline_record_t *records, size_t count, VideoInterface *vi, bool with_upscaler)
{
	PipelineNode *nodes;
	size_t i, n = 0;
	unsigned pass;

	if (!pipeline_worker || pipeline_ahead_nodes)
		return;
	if (!(nodes = static_cast<PipelineNode *>(calloc(count + MaxKnownPipelines, sizeof(*nodes)))))
		return;
	pipeline_ahead_nodes = nodes;

	for (i = 0; i < count; i++)
	{
		PipelineNode *node = &nodes[n];
		/* (what a record names is gone, or is not the same any more) */
		if (!device->resolve_pipeline_record(records[i], node->compile)
		    || granite_pipeline_map_find(&node->compile.program->pipelines, node->compile.hash).pipeline != VK_NULL_HANDLE)
			continue;
		node->kind   = records[i].kind;
		node->ahead  = 1;
		node->urgent = 1;
		/* The rasterizer's specialised variants: a draw that misses one
		 * runs on the ubershader meanwhile, and asks the worker for it,
		 * which finds it built by then (the worker takes its work in
		 * order). */
		if (node->kind == PIPELINE_RECORD_COMPUTE && !caps.force_sync
		    && node->compile.program == shader_bank->rasterizer
		    && node->compile.potential_static_state.spec_constant_mask == 0xff)
			node->urgent = 0;
		pipeline_ahead_urgent += node->urgent;
		n++;
	}
	/* tools/ci: frames do not depend on it, and a software driver takes most of
	 * a minute over these */
	if (!getenv("PARALLEL_RDP_NO_KNOWN_PIPELINES"))
		n = queue_known_pipelines(nodes, n, vi, with_upscaler);
	for (pass = 0; pass < 2; pass++)
		for (i = 0; i < n; i++)
			if (nodes[i].urgent == (pass == 0))
				rdp_worker_push(pipeline_worker, &nodes[i]);
}

bool Renderer::pipeline_ahead_urgent_built(void *ctx)
{
	return static_cast<Renderer *>(ctx)->pipelines_ahead_ready();
}

void Renderer::wait_pipelines_ahead_for_test()
{
	if (pipeline_worker)
		rdp_worker_wait(pipeline_worker, pipeline_ahead_urgent_built, this);
}

bool Renderer::pipelines_ahead_ready() const
{
	return !pipeline_worker ||
		retro_atomic_load_acquire_int(const_cast<retro_atomic_int_t *>(&pipeline_ahead_urgent_done)) >= pipeline_ahead_urgent;
}
}
