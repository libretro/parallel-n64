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
#include "submit_batch.h"
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
#include "buffer_pool.h"
#include "pipeline_cache.h"
#include "image_upload.h"
#include "queue_submit.h"
#include "frame_sync.h"
#include "frame_context.h"
#include "submission.h"
#include "memory_release.h"
#include "small_vector.hpp"
#include <stdio.h>

#include <rthreads/retro_eventcount.h>
#include <queues/mpsc_stack.h>
#include <retro_atomic.h>

#include "device_caps.h"

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
	VkBufferImageCopy blits[GRANITE_TEXTURE_MAX_LEVELS];
	unsigned num_blits = 0;
};

struct HandlePool
{
	HandlePool()
	{
		granite_buffer_pools_init(&buffers);
		granite_image_pools_init(&images);
		granite_fence_pool_slots_init(&fences);
		granite_semaphore_pool_slots_init(&semaphores);
		granite_cb_pool_slots_init(&command_buffers);
	}

	~HandlePool()
	{
		granite_buffer_pools_free(&buffers);
		granite_image_pools_free(&images);
		granite_lf_pool_clear(&fences);
		granite_lf_pool_clear(&semaphores);
		granite_lf_pool_clear(&command_buffers);
	}

	// buffers and their views (buffer.h)
	granite_buffer_pools_t buffers;
	// images and their views (image.h)
	granite_image_pools_t images;
	// fence.h
	granite_lf_pool_t fences;
	// semaphore.h
	granite_lf_pool_t semaphores;
	// command_buffer.h
	granite_lf_pool_t command_buffers;
};

class Device
	: public Util::IntrusivePtrEnabled<Device, Util::DefaultDelete<Device>, HandleCounter>
{
public:
	// Device-based objects which need to poke at internal data structures when their lifetimes end.
	// Don't want to expose a lot of internal guts to make this work.
	friend class CommandBuffer;
	friend class Cookie;

	Device();
	~Device();

	// No move-copy.
	void operator=(Device &&) = delete;
	Device(Device &&) = delete;

	// Only called by main thread, during setup phase.
	void set_context(const Context &context);
	void init_frame_contexts(unsigned count);
	void free_frame_contexts();
	const VolkDeviceTable &get_device_table() const;

	unsigned get_num_frame_contexts() const;

	size_t get_pipeline_cache_size();
	bool get_pipeline_cache_data(uint8_t *data, size_t size);
	bool init_pipeline_cache(const uint8_t *data, size_t size);

	/* The pipelines this device built, as records the next device can
	 * build again ahead of their first use. Recorded by the thread that
	 * built each one, without a lock; read once no thread builds any more. */
	void record_pipeline(const DeferredPipelineCompile &compile, uint32_t kind);
	size_t get_pipeline_records(const pipeline_record_t **records);
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

	// Submission interface, may be called from any thread at any time.
	void flush_frame();
	CommandBufferHandle request_command_buffer(CommandBuffer::Type type = CommandBuffer::Type::Generic);
	CommandBufferHandle request_command_buffer_for_thread(unsigned thread_index, CommandBuffer::Type type = CommandBuffer::Type::Generic);

	void submit(CommandBufferHandle &cmd, Fence *fence = nullptr,
	            unsigned semaphore_count = 0, Semaphore *semaphore = nullptr);
	void submit_empty(CommandBuffer::Type type, Fence *fence = nullptr);
	void add_wait_semaphore(CommandBuffer::Type type, Semaphore semaphore, VkPipelineStageFlags stages, bool flush);
	// Request shaders and programs. These objects are owned by the Device.
	Shader *request_shader(const uint32_t *code, size_t size,
	                       const ResourceLayout *layout = nullptr);
	Program *request_program(const uint32_t *vertex_data, size_t vertex_size, const uint32_t *fragment_data,
	                         size_t fragment_size,
	                         const ResourceLayout *vertex_layout = nullptr,
	                         const ResourceLayout *fragment_layout = nullptr);
	Program *request_program(const uint32_t *compute_data, size_t compute_size,
	                         const ResourceLayout *layout = nullptr);
	Program *request_program(Shader *vertex, Shader *fragment);
	Program *request_program(Shader *compute);

	// Map and unmap buffer objects.
	void *map_host_buffer(const Buffer &buffer, MemoryAccessFlags access);
	void unmap_host_buffer(const Buffer &buffer, MemoryAccessFlags access);
	void *map_host_buffer(const Buffer &buffer, MemoryAccessFlags access, VkDeviceSize offset, VkDeviceSize length);
	void unmap_host_buffer(const Buffer &buffer, MemoryAccessFlags access, VkDeviceSize offset, VkDeviceSize length);

	// Create buffers and images.
	BufferHandle create_buffer(const BufferCreateInfo &info, const void *initial = nullptr);
	BufferHandle create_imported_host_buffer(const BufferCreateInfo &info, void *host_buffer);
	ImageHandle create_image(const ImageCreateInfo &info, const ImageInitialData *initial = nullptr);
	ImageHandle create_image_from_staging_buffer(const ImageCreateInfo &info, const InitialImageBuffer *buffer);

	// Create staging buffers for images.
	bool create_image_staging_buffer(const ImageCreateInfo &info, const ImageInitialData *initial,
	                                 InitialImageBuffer &result);

	// Create image view, buffer views and samplers.
	ImageViewHandle create_image_view(const ImageViewCreateInfo &view_info);
	BufferViewHandle create_buffer_view(const BufferViewCreateInfo &view_info);

	// Render pass helpers.
	bool image_format_is_supported(VkFormat format, VkFormatFeatureFlags2KHR required, VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL) const;

	// Semaphores (semaphore.h) are single-use: submit(&sem) signals one,
	// add_wait_semaphore(sem) has the next submission of a queue wait for it.

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

	const granite_cmd_device_t &get_command_device() const
	{
		return cmd_device;
	}

	const granite_workarounds_t &get_workarounds() const
	{
		return workarounds;
	}

	const DeviceFeatures &get_device_features() const
	{
		return ext;
	}

	// Promotes any read-write cached state to read-only,
	// which eliminates need to read/write lock.
	// Can be called at any time as long as there is no
	// racing access to:
	// - Command buffer recording which uses pipelines (texture manager is fine).
	// - request_shader()
	// - request_program()
	// Generally, this should be called before you call next_frame_context().

	bool supports_subgroup_size_log2(bool subgroup_full_group,
	                                 uint8_t subgroup_minimum_size_log2,
	                                 uint8_t subgroup_maximum_size_log2) const;

	const QueueInfo &get_queue_info() const;

private:
	VkInstance instance = VK_NULL_HANDLE;
	VkPhysicalDevice gpu = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;
	const VolkDeviceTable *table = nullptr;
	QueueInfo queue_info = {};
	unsigned num_thread_indices = 1;


	void request_vertex_block(granite_buffer_block_t &block, VkDeviceSize size);
	void request_uniform_block(granite_buffer_block_t &block, VkDeviceSize size);

	const Framebuffer &request_framebuffer(const RenderPassInfo &info);
	const RenderPass &request_render_pass(const RenderPassInfo &info, bool compatible);
	const RenderPass &request_render_pass(const granite_render_pass_desc_t &desc, bool compatible);

	VkPhysicalDeviceMemoryProperties mem_props;
	VkPhysicalDeviceProperties gpu_props;

	DeviceFeatures ext;
	void init_stock_samplers();
	void deinit_timeline_semaphores();


	// Make sure this is deleted last.
	HandlePool handle_pool;
	granite_handle_owner_t handle_owner;
	static void handle_destroy(void *user, unsigned kind, uint64_t handle, bool owner_thread);
	static void handle_free_memory(void *user, const granite_allocation_t *alloc, bool owner_thread);
	static void handle_recycle_fence(void *user, VkFence fence, bool seen, bool owner_thread);
	BufferHandle wrap_buffer(VkBuffer buffer, const DeviceAllocation &allocation, const granite_buffer_info_t &info);

	// Released from any thread without a lock: Vulkan objects through
	// release_queue.c, memory and timeline intervals on these stacks. Each
	// frame context drains its own in granite_frame_context_begin(), after waiting for
	// that frame's GPU work. Fixed arrays, so recreating the frame
	// contexts never moves what a releasing thread is pushing onto. The
	// queue's frame index says where all of them go.
	enum { MAX_DEFERRED_FRAMES = GRANITE_RELEASE_FRAMES };
	struct ReleaseQueue
	{
		// Vulkan objects, and device memory (memory_release.h)
		granite_release_queue_t q;
		granite_memory_release_queue_t memory;
		ReleaseQueue()
		{
			granite_release_queue_init(&q);
			granite_memory_release_queue_init(&memory);
		}
		~ReleaseQueue()
		{
			granite_release_queue_free(&q);
			granite_memory_release_queue_free(&memory);
		}
	} releases;
	void release_handle(granite_garbage_kind kind, uint64_t handle)
	{
		granite_release_queue_push(&releases.q, kind, handle);
	}

	// Threads that find another thread already waiting on a FenceHolder
	// park here until it is done (see FenceHolder::wait).
	retro_eventcount_t fence_wait_ec{};
	bool fence_wait_ec_live = false;

	struct Managers
	{
		DeviceAllocator memory;
		granite_fence_pool_t fence = {};
		granite_semaphore_pool_t semaphore = {};
		granite_buffer_pool_t vbo = {}, ubo = {};

		~Managers()
		{
			granite_buffer_pool_free(&vbo);
			granite_buffer_pool_free(&ubo);
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

	// The frame contexts (frame_context.c) and what they share.
	granite_frame_env_t frame_env = {};
	granite_frame_context_t per_frame[MAX_DEFERRED_FRAMES] = {};
	unsigned num_frames = 0;
	// Freed here, as the members after this one go: before the managers
	// and the release queues they give things back to.
	struct FrameContextsFree
	{
		Device *d;
		~FrameContextsFree() { d->free_frame_contexts(); }
	} frame_contexts_free{this};

	// How command buffers get to the queues (submission.c).
	granite_submission_t submission;
	enum { MAX_SUBMIT_SEMAPHORES = 4 };

	granite_frame_context_t &frame()
	{
		VK_ASSERT(frame_context_index < num_frames);
		return per_frame[frame_context_index];
	}

	unsigned frame_context_index = 0;

	uint32_t find_memory_type(granite_buffer_domain_t domain, uint32_t mask) const;
	bool memory_type_is_host_visible(uint32_t type) const;

	Sampler samplers[static_cast<unsigned>(StockSampler::Count)] = {};

	// shaders, programs, pipeline layouts, set allocators and render passes
	// (program_cache.h)
	granite_program_cache_t program_cache;
	bool program_cache_live = false;

	granite_framebuffer_allocator_t framebuffer_allocator;
	VkPipelineCache pipeline_cache = VK_NULL_HANDLE;
	granite_pipeline_record_table_t pipeline_records;


	void submit_staging(CommandBufferHandle &cmd, VkBufferUsageFlags usage, bool flush);

	static const granite_buffer_pool_ops_t block_ops;

	void destroy_pipeline(VkPipeline pipeline);
	void free_memory(const DeviceAllocation &alloc);
	void reset_fence(VkFence fence, bool observed_wait);

	void destroy_framebuffer_nolock(VkFramebuffer framebuffer);
	void free_memory_nolock(const DeviceAllocation &alloc);
	void reset_fence_nolock(VkFence fence, bool observed_wait);

	CommandBufferHandle request_command_buffer_nolock(unsigned thread_index, CommandBuffer::Type type);

	void request_vertex_block_nolock(granite_buffer_block_t &block, VkDeviceSize size);
	void request_uniform_block_nolock(granite_buffer_block_t &block, VkDeviceSize size);
	void add_frame_counter_nolock();
	void decrement_frame_counter_nolock();
	void wait_idle_nolock();
	void end_frame_nolock();


	granite_workarounds_t workarounds = {};
	granite_cmd_device_t cmd_device = {};
	void init_workarounds();


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
inline Device &CommandBuffer::get_device()
{
	return *static_cast<Device *>(dev->user);
}

inline Pipeline CommandBuffer::build_graphics_pipeline(Device *device, const DeferredPipelineCompile &compile,
                                                       CompileMode mode)
{
	return granite_pipeline_compile_build_graphics(&device->get_command_device(), &compile,
	                                               granite_compile_mode(mode));
}

inline Pipeline CommandBuffer::build_compute_pipeline(Device *device, const DeferredPipelineCompile &compile,
                                                      CompileMode mode)
{
	return granite_pipeline_compile_build_compute(&device->get_command_device(), &compile,
	                                              granite_compile_mode(mode));
}
}
