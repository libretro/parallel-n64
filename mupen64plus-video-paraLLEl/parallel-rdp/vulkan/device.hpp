/* Copyright (c) 2017-2022 Hans-Kristian Arntzen
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

#include "buffer.hpp"
#include "command_buffer.hpp"
#include "command_pool.h"
#include "fence.hpp"
#include "sync_pools.h"
#include "frame_garbage.h"
#include "release_queue.h"
#include "image.hpp"
#include "memory_allocator.hpp"
#include "render_pass.hpp"
#include "sampler.hpp"
#include "semaphore.hpp"
#include "shader.hpp"
#include "context.hpp"
#include "query_pool.hpp"
#include "buffer_pool.hpp"
#include "small_vector.hpp"
#include <stdio.h>

#ifdef GRANITE_VULKAN_MT
#include <rthreads/retro_eventcount.h>
#include <queues/mpsc_stack.h>
#include <retro_atomic.h>
#endif

#include "quirks.hpp"

namespace Granite
{
struct TaskGroup;
}

namespace Vulkan
{
enum class SwapchainRenderPass
{
	ColorOnly,
	Depth,
	DepthStencil
};

struct InitialImageBuffer
{
	BufferHandle buffer;
	Util::SmallVector<VkBufferImageCopy, 32> blits;
};

// Memory handed to the Device to free once the GPU is done with the
// frame it was last used in. Vulkan objects go through release_queue.c.
struct DeferredRelease : mpsc_stack_node_t
{
	DeviceAllocation alloc = {};
};
// A timeline-trace interval registered from a thread other than the
// owner; recorded by the owner when the frame it was sent to begins.
struct DeferredInterval : mpsc_stack_node_t
{
	char tid[32], tag[64];
	QueryPoolHandle start_ts, end_ts;
};

struct HandlePool
{
	VulkanObjectPool<Buffer> buffers;
	VulkanObjectPool<Image> images;
	VulkanObjectPool<LinearHostImage> linear_images;
	VulkanObjectPool<ImageView> image_views;
	VulkanObjectPool<BufferView> buffer_views;
	VulkanObjectPool<Sampler> samplers;
	VulkanObjectPool<FenceHolder> fences;
	VulkanObjectPool<SemaphoreHolder> semaphores;
	VulkanObjectPool<QueryPoolResult> query;
	VulkanObjectPool<CommandBuffer> command_buffers;
	VulkanObjectPool<DeferredRelease> deferred;
	VulkanObjectPool<DeferredInterval> deferred_intervals;
};

// Where a debug channel's messages go: a function and what it is called on
struct DebugChannelInterface
{
	union Word
	{
		uint32_t u32;
		int32_t s32;
		float f32;
	};
	void (*message)(void *user, const char *tag, uint32_t code, uint32_t x, uint32_t y, uint32_t z,
	                uint32_t word_count, const Word *words);
	void *user;
};

namespace Helper
{
struct WaitSemaphores
{
	Util::SmallVector<VkSemaphore> binary_waits;
	Util::SmallVector<VkPipelineStageFlags> binary_wait_stages;
	Util::SmallVector<VkSemaphore> timeline_waits;
	Util::SmallVector<VkPipelineStageFlags> timeline_wait_stages;
	Util::SmallVector<uint64_t> timeline_wait_counts;
};

class BatchComposer
{
public:
	enum { MaxSubmissions = 8 };

	explicit BatchComposer(bool split_binary_timeline_semaphores);
	void add_wait_submissions(WaitSemaphores &sem);
	void add_wait_semaphore(SemaphoreHolder &sem, VkPipelineStageFlags stage);
	void add_signal_semaphore(VkSemaphore sem, uint64_t count);
	void add_command_buffer(VkCommandBuffer cmd);

	void begin_batch();
	Util::SmallVector<VkSubmitInfo, MaxSubmissions> &bake(int profiling_iteration = -1);

private:
	Util::SmallVector<VkSubmitInfo, MaxSubmissions> submits;
	VkTimelineSemaphoreSubmitInfoKHR timeline_infos[Helper::BatchComposer::MaxSubmissions];
	VkPerformanceQuerySubmitInfoKHR profiling_infos[Helper::BatchComposer::MaxSubmissions];

	Util::SmallVector<VkSemaphore> waits[MaxSubmissions];
	Util::SmallVector<uint64_t> wait_counts[MaxSubmissions];
	Util::SmallVector<VkFlags> wait_stages[MaxSubmissions];
	Util::SmallVector<VkSemaphore> signals[MaxSubmissions];
	Util::SmallVector<uint64_t> signal_counts[MaxSubmissions];
	Util::SmallVector<VkCommandBuffer> cmds[MaxSubmissions];

	unsigned submit_index = 0;
	bool split_binary_timeline_semaphores = false;

	bool has_timeline_semaphore_in_batch(unsigned index) const;
	bool has_binary_semaphore_in_batch(unsigned index) const;
};
}

class Device
	: public Util::IntrusivePtrEnabled<Device, Util::DefaultDelete<Device>, HandleCounter>
{
public:
	// Device-based objects which need to poke at internal data structures when their lifetimes end.
	// Don't want to expose a lot of internal guts to make this work.
	friend class QueryPool;
	friend struct QueryPoolResultDeleter;
	friend class SemaphoreHolder;
	friend struct SemaphoreHolderDeleter;
	friend class FenceHolder;
	friend struct FenceHolderDeleter;
	friend class Sampler;
	friend struct SamplerDeleter;
	friend class ImmutableSampler;
	friend class Buffer;
	friend struct BufferDeleter;
	friend class BufferView;
	friend struct BufferViewDeleter;
	friend class ImageView;
	friend struct ImageViewDeleter;
	friend class Image;
	friend struct ImageDeleter;
	friend struct LinearHostImageDeleter;
	friend class CommandBuffer;
	friend struct CommandBufferDeleter;
	friend class Program;
	friend class WSI;
	friend class Cookie;
	friend class Framebuffer;
	friend class PipelineLayout;
	friend class FramebufferAllocator;
	friend class RenderPass;
	friend class Texture;
	friend class DescriptorSetAllocator;
	friend class Shader;
	friend class ImageResourceHolder;

	Device();
	~Device();

	// No move-copy.
	void operator=(Device &&) = delete;
	Device(Device &&) = delete;

	// Only called by main thread, during setup phase.
	void set_context(const Context &context);
	bool can_touch_swapchain_in_command_buffer(CommandBuffer::Type type) const;
	void init_frame_contexts(unsigned count);
	const VolkDeviceTable &get_device_table() const;

	// Profiling

	ImageView &get_swapchain_view();
	ImageView &get_swapchain_view(unsigned index);
	unsigned get_num_frame_contexts() const;

	size_t get_pipeline_cache_size();
	bool get_pipeline_cache_data(uint8_t *data, size_t size);
	bool init_pipeline_cache(const uint8_t *data, size_t size);

	/* The pipelines this device built, as records the next device can
	 * build again ahead of their first use. Recorded by the thread that
	 * built each one, without a lock; read once no thread builds any more. */
	enum { PIPELINE_RECORDS = 4096 };
	void record_pipeline(const DeferredPipelineCompile &compile, uint32_t kind);
	size_t get_pipeline_records(const pipeline_record_t **records) const;
	/* The compatible render pass a signature describes, made again from
	 * 1x1 stand-ins when there is none yet. */
	const RenderPass *recreate_render_pass(const pipeline_record_pass_t &signature);
	/* The pipeline a record describes, made ready to build: its program
	 * (made from the device's shaders when there is none yet) and render
	 * pass, its state; false when what it was made from is not there, or
	 * hashes to another pipeline. The device's owner thread, as above. */
	bool resolve_pipeline_record(const pipeline_record_t &record, DeferredPipelineCompile &compile);

	// Frame-pushing interface.
	void next_frame_context();
	void wait_idle();

	// RenderDoc integration API for app-guided captures.

	// Set names for objects for debuggers and profilers.
	void set_name(const Buffer &buffer, const char *name);
	void set_name(const Image &image, const char *name);
	void set_name(const CommandBuffer &cmd, const char *name);

	// Submission interface, may be called from any thread at any time.
	void flush_frame();
	CommandBufferHandle request_command_buffer(CommandBuffer::Type type = CommandBuffer::Type::Generic);
	CommandBufferHandle request_command_buffer_for_thread(unsigned thread_index, CommandBuffer::Type type = CommandBuffer::Type::Generic);

	void submit(CommandBufferHandle &cmd, Fence *fence = nullptr,
	            unsigned semaphore_count = 0, Semaphore *semaphore = nullptr);
	void submit_empty(CommandBuffer::Type type,
	                  Fence *fence = nullptr,
	                  SemaphoreHolder *semaphore = nullptr);
	void add_wait_semaphore(CommandBuffer::Type type, Semaphore semaphore, VkPipelineStageFlags stages, bool flush);
	QueueIndices get_physical_queue_type(CommandBuffer::Type queue_type) const;
	// Strings are copied; extra is not recorded by the timeline trace.
	void register_time_interval(const char *tid, QueryPoolHandle start_ts, QueryPoolHandle end_ts,
	                            const char *tag, const char *extra = nullptr);

	// Request shaders and programs. These objects are owned by the Device.
	Shader *request_shader(const uint32_t *code, size_t size,
	                       const ResourceLayout *layout = nullptr,
	                       const ImmutableSamplerBank *sampler_bank = nullptr);
	Program *request_program(const uint32_t *vertex_data, size_t vertex_size, const uint32_t *fragment_data,
	                         size_t fragment_size,
	                         const ResourceLayout *vertex_layout = nullptr,
	                         const ResourceLayout *fragment_layout = nullptr);
	Program *request_program(const uint32_t *compute_data, size_t compute_size,
	                         const ResourceLayout *layout = nullptr,
	                         const ImmutableSamplerBank *sampler_bank = nullptr);
	Program *request_program(Shader *vertex, Shader *fragment);
	Program *request_program(Shader *compute);
	const ImmutableSampler *request_immutable_sampler(const SamplerCreateInfo &info, const ImmutableYcbcrConversion *ycbcr);

	// Map and unmap buffer objects.
	void *map_host_buffer(const Buffer &buffer, MemoryAccessFlags access);
	void unmap_host_buffer(const Buffer &buffer, MemoryAccessFlags access);
	void *map_host_buffer(const Buffer &buffer, MemoryAccessFlags access, VkDeviceSize offset, VkDeviceSize length);
	void unmap_host_buffer(const Buffer &buffer, MemoryAccessFlags access, VkDeviceSize offset, VkDeviceSize length);

	// Create buffers and images.
	BufferHandle create_buffer(const BufferCreateInfo &info, const void *initial = nullptr);
	BufferHandle create_imported_host_buffer(const BufferCreateInfo &info, VkExternalMemoryHandleTypeFlagBits type, void *host_buffer);
	ImageHandle create_image(const ImageCreateInfo &info, const ImageInitialData *initial = nullptr);
	ImageHandle create_image_from_staging_buffer(const ImageCreateInfo &info, const InitialImageBuffer *buffer);

	// Create staging buffers for images.
	InitialImageBuffer create_image_staging_buffer(const ImageCreateInfo &info, const ImageInitialData *initial);

	// Create image view, buffer views and samplers.
	ImageViewHandle create_image_view(const ImageViewCreateInfo &view_info);
	BufferViewHandle create_buffer_view(const BufferViewCreateInfo &view_info);

	// Render pass helpers.
	bool image_format_is_supported(VkFormat format, VkFormatFeatureFlags2KHR required, VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL) const;
	void get_format_properties(VkFormat format, VkFormatProperties3KHR *properties) const;
	bool get_image_format_properties(VkFormat format, VkImageType type, VkImageTiling tiling,
	                                 VkImageUsageFlags usage, VkImageCreateFlags flags,
	                                 const void *pNext,
	                                 VkImageFormatProperties2 *properties2) const;

	// Semaphore API:
	// Semaphores in Granite are abstracted to support both binary and timeline semaphores
	// internally.
	// In practice this means that semaphores behave like single-use binary semaphores,
	// with one signal and one wait.
	// A single semaphore handle is not reused for multiple submissions, and they must be recycled through
	// the device. The intended use is device.submit(&sem), device.add_wait_semaphore(sem); dispose(sem);
	// For timeline semaphores, the semaphore is just a proxy object which
	// holds the internally owned VkSemaphore + timeline value and is otherwise lightweight.
	//
	// However, there are various use cases where we explicitly need semaphore objects:
	// - Interoperate with other code that only accepts VkSemaphore.
	// - Interoperate with external objects. We need to know whether to use binary or timeline.
	//   For timelines, we need to know which handle type to use (OPAQUE or ID3D12Fence).
	//   Binary external semaphore is always opaque with TEMPORARY semantics.

	// If transfer_ownership is set, Semaphore owns the VkSemaphore. Otherwise, application must
	// free the semaphore when GPU usage of it is complete.
	Semaphore request_semaphore(VkSemaphoreTypeKHR type, VkSemaphore handle = VK_NULL_HANDLE, bool transfer_ownership = false);

	// Requests a binary or timeline semaphore that can be used to import/export.
	// These semaphores cannot be used directly by add_wait_semaphore() and submit_empty().
	// See request_timeline_semaphore_as_binary() for how to use timelines.
	// The created semaphore does not hold ownership of the VkSemaphore object.
	Semaphore request_timeline_semaphore_as_binary(const SemaphoreHolder &holder, uint64_t value);

	// A proxy semaphore which lets us grab a semaphore handle before we signal it.
	// Move assignment can be used to move a payload.
	// Mostly useful to deal better with render graph implementation.
	// For time being however, we'll support moving the payload over to the proxy object.

	// For compat with existing code that uses this entry point.
	inline Semaphore request_legacy_semaphore() { return request_semaphore(VK_SEMAPHORE_TYPE_BINARY_KHR); }

	VkDevice get_device() const
	{
		return device;
	}

	VkPhysicalDevice get_physical_device() const
	{
		return gpu;
	}

	const VkPhysicalDeviceMemoryProperties &get_memory_properties() const
	{
		return mem_props;
	}

	const VkPhysicalDeviceProperties &get_gpu_properties() const
	{
		return gpu_props;
	}


	const Sampler &get_stock_sampler(StockSampler sampler) const;

	// For some platforms, the device and queue might be shared, possibly across threads, so need some mechanism to
	// lock the global device and queue.
	typedef void (*QueueLockCallback)(void *userdata);
	void set_queue_lock(QueueLockCallback lock_callback, QueueLockCallback unlock_callback, void *userdata);

	// Submits one-off initialisation of a resource the way zero-initialised
	// buffers are: ordered before later work on every queue that may use it.
	void submit_initialization(CommandBufferHandle &cmd, VkBufferUsageFlags usage);

	const ImplementationWorkarounds &get_workarounds() const
	{
		return workarounds;
	}

	const DeviceFeatures &get_device_features() const
	{
		return ext;
	}

	double convert_device_timestamp_delta(uint64_t start_ticks, uint64_t end_ticks) const;
	// Writes a timestamp on host side, which is calibrated to the GPU timebase.
	QueryPoolHandle write_calibrated_timestamp();

	// Promotes any read-write cached state to read-only,
	// which eliminates need to read/write lock.
	// Can be called at any time as long as there is no
	// racing access to:
	// - Command buffer recording which uses pipelines (texture manager is fine).
	// - request_shader()
	// - request_program()
	// Generally, this should be called before you call next_frame_context().

	const Context::SystemHandles &get_system_handles() const
	{
		return system_handles;
	}

	bool supports_subgroup_size_log2(bool subgroup_full_group,
	                                 uint8_t subgroup_minimum_size_log2,
	                                 uint8_t subgroup_maximum_size_log2) const;

	const QueueInfo &get_queue_info() const;

private:
	VkInstance instance = VK_NULL_HANDLE;
	VkPhysicalDevice gpu = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;
	const VolkDeviceTable *table = nullptr;
	QueueInfo queue_info;
	unsigned num_thread_indices = 1;

	void bake_program(Program &program);

	void request_vertex_block(BufferBlock &block, VkDeviceSize size);
	void request_uniform_block(BufferBlock &block, VkDeviceSize size);

	QueryPoolHandle write_timestamp(VkCommandBuffer cmd, VkPipelineStageFlagBits stage);

	PipelineLayout *request_pipeline_layout(const CombinedResourceLayout &layout,
	                                        const ImmutableSamplerBank *immutable_samplers);
	DescriptorSetAllocator *request_descriptor_set_allocator(const DescriptorSetLayout &layout,
	                                                         const uint32_t *stages_for_sets,
	                                                         const ImmutableSampler * const *immutable_samplers,
	                                                         bool push);
	const Framebuffer &request_framebuffer(const RenderPassInfo &info);
	const RenderPass &request_render_pass(const RenderPassInfo &info, bool compatible);

	VkPhysicalDeviceMemoryProperties mem_props;
	VkPhysicalDeviceProperties gpu_props;

	DeviceFeatures ext;
	void init_stock_samplers();
	void init_stock_sampler(StockSampler sampler, float max_aniso, float lod_bias);
	void init_timeline_semaphores();
	void deinit_timeline_semaphores();

	uint64_t update_wrapped_device_timestamp(uint64_t ts);
	int64_t convert_timestamp_to_absolute_nsec(const QueryPoolResult &handle);
	Context::SystemHandles system_handles;

	QueryPoolHandle write_timestamp_nolock(VkCommandBuffer cmd, VkPipelineStageFlagBits stage);
	QueryPoolHandle write_calibrated_timestamp_nolock();
	void register_time_interval_nolock(const char *tid, QueryPoolHandle start_ts, QueryPoolHandle end_ts, const char *tag);

	// Make sure this is deleted last.
	HandlePool handle_pool;

	// Released from any thread without a lock: Vulkan objects through
	// release_queue.c, memory and timeline intervals on these stacks. Each
	// frame context drains its own in PerFrame::begin(), after waiting for
	// that frame's GPU work. Fixed arrays, so recreating the frame
	// contexts never moves what a releasing thread is pushing onto. The
	// queue's frame index says where all of them go.
	struct PerFrame;
	enum { MAX_DEFERRED_FRAMES = GRANITE_RELEASE_FRAMES };
	struct ReleaseQueue
	{
		granite_release_queue_t q;
		ReleaseQueue() { granite_release_queue_init(&q); }
		~ReleaseQueue() { granite_release_queue_free(&q); }
	} releases;
	mpsc_stack_t deferred_releases[MAX_DEFERRED_FRAMES];
	mpsc_stack_t deferred_intervals[MAX_DEFERRED_FRAMES];
	void release_handle(granite_garbage_kind kind, uint64_t handle)
	{
		granite_release_queue_push(&releases.q, kind, handle);
	}
	void drain_deferred_releases(PerFrame &frame);

	// Threads that find another thread already waiting on a FenceHolder
	// park here until it is done (see FenceHolder::wait).
	retro_eventcount_t fence_wait_ec{};
	bool fence_wait_ec_live = false;

	// Calibrated timestamps.
	void init_calibrated_timestamps();
	void recalibrate_timestamps_fallback();
	void recalibrate_timestamps();
	bool resample_calibrated_timestamps();
	VkTimeDomainEXT calibrated_time_domain = VK_TIME_DOMAIN_DEVICE_EXT;
	int64_t calibrated_timestamp_device = 0;
	int64_t calibrated_timestamp_host = 0;
	int64_t calibrated_timestamp_device_accum = 0;
	unsigned timestamp_calibration_counter = 0;
	Vulkan::QueryPoolHandle frame_context_begin_ts;

	struct Managers
	{
		DeviceAllocator memory;
		granite_fence_pool_t fence = {};
		granite_semaphore_pool_t semaphore = {};
		BufferPool vbo, ibo, ubo, staging;
		TimestampIntervalManager timestamps;

		~Managers()
		{
			granite_semaphore_pool_free(&semaphore);
			granite_fence_pool_free(&fence);
		}
	};
	Managers managers;

	struct
	{
		// Command buffers handed out this frame and not yet submitted;
		// owner thread only (see device.cpp).
		unsigned counter = 0;
	} lock;

	struct PerFrame
	{
		PerFrame(Device *device, unsigned index);
		~PerFrame();
		void operator=(const PerFrame &) = delete;
		PerFrame(const PerFrame &) = delete;

		void begin();
		void trim_command_pools();

		Device &device;
		unsigned frame_index;
		const VolkDeviceTable &table;
		Managers &managers;

		// One per thread index, of each queue.
		Util::Vector<granite_command_pool_t> cmd_pools[QUEUE_INDEX_COUNT];
		VkSemaphore timeline_semaphores[QUEUE_INDEX_COUNT] = {};
		uint64_t timeline_fences[QUEUE_INDEX_COUNT] = {};

		QueryPool query_pool;

		Util::Vector<BufferBlock> vbo_blocks;
		Util::Vector<BufferBlock> ibo_blocks;
		Util::Vector<BufferBlock> ubo_blocks;
		Util::Vector<BufferBlock> staging_blocks;

		Util::Vector<VkFence> wait_fences;

		Util::Vector<DeviceAllocation> allocations;
		Util::SmallVector<CommandBufferHandle> submissions[QUEUE_INDEX_COUNT];
		// destroyed, or recycled, by begin() (frame_garbage.c)
		granite_frame_garbage_t garbage = {};
		Util::Vector<ImageHandle> keep_alive_images;

		struct DebugChannel
		{
			DebugChannelInterface *iface;
			char tag[32];
			BufferHandle buffer;
		};
		Util::Vector<DebugChannel> debug_channels;

		struct TimestampIntervalHandles
		{
			char tid[32];
			QueryPoolHandle start_ts;
			QueryPoolHandle end_ts;
			TimestampInterval *timestamp_tag;
		};
		Util::Vector<TimestampIntervalHandles> timestamp_intervals;

		bool in_destructor = false;
	};
	// The per frame structure must be destroyed after
	// the hashmap data structures below, so it must be declared before.
	Util::OwnedPtrArray<PerFrame, MAX_DEFERRED_FRAMES> per_frame;

	struct
	{
		Semaphore acquire;
		Semaphore release;
		Util::Vector<ImageHandle> swapchain;
		VkQueue present_queue = VK_NULL_HANDLE;
		uint32_t queue_family_support_mask = 0;
		unsigned index = 0;
		bool consumed = false;
	} wsi;
	bool can_touch_swapchain_in_command_buffer(QueueIndices physical_type) const;

	struct QueueData
	{
		Util::SmallVector<Semaphore> wait_semaphores;
		Util::SmallVector<VkPipelineStageFlags> wait_stages;
		bool need_fence = false;

		VkSemaphore timeline_semaphore = VK_NULL_HANDLE;
		uint64_t current_timeline = 0;
		PerformanceQueryPool performance_query_pool;
	} queue_data[QUEUE_INDEX_COUNT];

	struct InternalFence
	{
		VkFence fence;
		VkSemaphore timeline;
		uint64_t value;
	};

	// Pending buffers which need to be copied from CPU to GPU before submitting graphics or compute work.
	struct
	{
		Util::Vector<BufferBlock> vbo;
		Util::Vector<BufferBlock> ibo;
		Util::Vector<BufferBlock> ubo;
	} dma;

	void submit_queue(QueueIndices physical_type, InternalFence *fence,
	                  SemaphoreHolder *external_semaphore = nullptr,
	                  unsigned semaphore_count = 0,
	                  Semaphore *semaphore = nullptr,
	                  int profiled_iteration = -1);

	PerFrame &frame()
	{
		VK_ASSERT(frame_context_index < per_frame.size());
		VK_ASSERT(per_frame[frame_context_index]);
		return *per_frame[frame_context_index];
	}

	const PerFrame &frame() const
	{
		VK_ASSERT(frame_context_index < per_frame.size());
		VK_ASSERT(per_frame[frame_context_index]);
		return *per_frame[frame_context_index];
	}

	unsigned frame_context_index = 0;

	uint32_t find_memory_type(BufferDomain domain, uint32_t mask) const;
	uint32_t find_memory_type(ImageDomain domain, uint32_t mask) const;
	uint32_t find_memory_type(uint32_t required, uint32_t mask) const;
	bool memory_type_is_host_visible(uint32_t type) const;

	const ImmutableSampler *samplers[static_cast<unsigned>(StockSampler::Count)] = {};

	VulkanCache<PipelineLayout> pipeline_layouts;
	VulkanCache<DescriptorSetAllocator> descriptor_set_allocators;
	VulkanCache<RenderPass> render_passes;
	VulkanCache<Shader> shaders;
	VulkanCache<Program> programs;
	VulkanCache<ImmutableSampler> immutable_samplers;
	VulkanCache<ImmutableYcbcrConversion> immutable_ycbcr_conversions;

	FramebufferAllocator framebuffer_allocator;
	TransientAttachmentAllocator transient_allocator;
	VkPipelineCache pipeline_cache = VK_NULL_HANDLE;
	pipeline_record_t *pipeline_records = nullptr;
	retro_atomic_int_t pipeline_records_claimed{0};
	retro_atomic_int_t pipeline_records_written{0};

	void init_pipeline_cache();
	void flush_pipeline_cache();

	PerformanceQueryPool &get_performance_query_pool(QueueIndices physical_type);
	void clear_wait_semaphores();
	void submit_staging(CommandBufferHandle &cmd, VkBufferUsageFlags usage, bool flush);

	QueueLockCallback queue_lock_callback = nullptr;
	QueueLockCallback queue_unlock_callback = nullptr;
	void *queue_lock_userdata = nullptr;
	void flush_frame(QueueIndices physical_type);
	void sync_buffer_blocks();
	void submit_empty_inner(QueueIndices type, InternalFence *fence,
	                        SemaphoreHolder *external_semaphore,
	                        unsigned semaphore_count,
	                        Semaphore *semaphore);

	void collect_wait_semaphores(QueueData &data, Helper::WaitSemaphores &semaphores);
	void emit_queue_signals(Helper::BatchComposer &composer,
	                        SemaphoreHolder *external_semaphore,
	                        VkSemaphore sem, uint64_t timeline, InternalFence *fence,
	                        unsigned semaphore_count, Semaphore *semaphores);
	VkResult submit_batches(Helper::BatchComposer &composer, VkQueue queue, VkFence fence,
	                        int profiling_iteration = -1);

	void destroy_buffer(VkBuffer buffer);
	void destroy_image(VkImage image);
	void destroy_image_view(VkImageView view);
	void destroy_buffer_view(VkBufferView view);
	void destroy_pipeline(VkPipeline pipeline);
	void destroy_sampler(VkSampler sampler);
	void destroy_framebuffer(VkFramebuffer framebuffer);
	void destroy_semaphore(VkSemaphore semaphore);
	void recycle_semaphore(VkSemaphore semaphore);
	void free_memory(const DeviceAllocation &alloc);
	void reset_fence(VkFence fence, bool observed_wait);

	void destroy_buffer_nolock(VkBuffer buffer);
	void destroy_image_nolock(VkImage image);
	void destroy_image_view_nolock(VkImageView view);
	void destroy_buffer_view_nolock(VkBufferView view);
	void destroy_pipeline_nolock(VkPipeline pipeline);
	void destroy_sampler_nolock(VkSampler sampler);
	void destroy_framebuffer_nolock(VkFramebuffer framebuffer);
	void destroy_semaphore_nolock(VkSemaphore semaphore);
	void recycle_semaphore_nolock(VkSemaphore semaphore);
	void free_memory_nolock(const DeviceAllocation &alloc);
	void reset_fence_nolock(VkFence fence, bool observed_wait);

	void flush_frame_nolock();
	CommandBufferHandle request_command_buffer_nolock(unsigned thread_index, CommandBuffer::Type type, bool profiled);
	void submit_discard_nolock(CommandBufferHandle &cmd);
	void submit_nolock(CommandBufferHandle cmd, Fence *fence,
	                   unsigned semaphore_count, Semaphore *semaphore);
	void submit_empty_nolock(QueueIndices physical_type, Fence *fence,
	                         SemaphoreHolder *semaphore, int profiling_iteration);
	void add_wait_semaphore_nolock(QueueIndices type, Semaphore semaphore, VkPipelineStageFlags stages,
	                               bool flush);

	void request_vertex_block_nolock(BufferBlock &block, VkDeviceSize size);
	void request_index_block_nolock(BufferBlock &block, VkDeviceSize size);
	void request_uniform_block_nolock(BufferBlock &block, VkDeviceSize size);
	void request_staging_block_nolock(BufferBlock &block, VkDeviceSize size);
	void add_frame_counter_nolock();
	void decrement_frame_counter_nolock();
	void submit_secondary(CommandBuffer &primary, CommandBuffer &secondary);
	void wait_idle_nolock();
	void end_frame_nolock();

	void add_debug_channel_buffer(DebugChannelInterface *iface, const char *tag, BufferHandle buffer);
	void parse_debug_channel(const PerFrame::DebugChannel &channel);

	ImplementationWorkarounds workarounds;
	void init_workarounds();
	void report_checkpoints();

	void fill_buffer_sharing_indices(VkBufferCreateInfo &create_info, uint32_t *sharing_indices);

	bool allocate_image_memory(DeviceAllocation *allocation, const ImageCreateInfo &info,
	                           VkImage image, VkImageTiling tiling);
};

// A fairly complex helper used for async queue readbacks.
// Typically used for things like headless backend which emulates WSI through readbacks + encode.
struct OwnershipTransferInfo
{
	CommandBuffer::Type old_queue;
	CommandBuffer::Type new_queue;
	VkImageLayout old_image_layout;
	VkImageLayout new_image_layout;
	VkPipelineStageFlags dst_pipeline_stage;
	VkAccessFlags dst_access;
};

// For an image which was last accessed in old_queue, requests a command buffer
// for new_queue. Commands will be enqueued as necessary in new_queue to ensure that a complete ownership
// transfer has taken place.
// If queue family for old_queue differs from new_queue, a release barrier is enqueued in old_queue.
// In new_queue we perform either an acquire barrier or a simple pipeline barrier to change layout if required.
// If semaphore is a valid handle, it will be waited on in either old_queue to perform release barrier
// or new_queue depending on what is required.
// If the image uses CONCURRENT sharing mode, acquire/release barriers are skipped.

using DeviceHandle = Util::IntrusivePtr<Device>;
}
