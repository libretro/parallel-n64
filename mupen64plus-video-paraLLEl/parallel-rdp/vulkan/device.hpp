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
#include "fence.hpp"
#include "image.hpp"
#include "memory_allocator.hpp"
#include "render_pass.hpp"
#include "sampler.hpp"
#include "semaphore.hpp"
#include "shader.hpp"
#include "context.hpp"
#include "device.h"

namespace Vulkan
{
// The device is C (device.h). This gives it the names and handle types
// the renderer uses.
class Device : public granite_device_t
{
public:
	Device()
	{
		granite_device_init(this);
	}

	~Device()
	{
		granite_device_free(this);
	}

	Device(const Device &) = delete;
	void operator=(const Device &) = delete;

	// Only called by main thread, during setup phase.
	void set_context(const Context &context)
	{
		granite_device_set_context(this, &context.get_context());
	}

	void init_frame_contexts(unsigned count)
	{
		granite_device_init_frame_contexts(this, count);
	}

	const VolkDeviceTable &get_device_table() const
	{
		return *table;
	}

	unsigned get_num_frame_contexts() const
	{
		return num_frames;
	}

	// The driver's pipeline cache and the list of pipelines built
	// (pipeline_cache.h).
	size_t get_pipeline_cache_size()
	{
		return granite_pipeline_cache_blob_size(device, table, pipeline_cache);
	}

	bool get_pipeline_cache_data(uint8_t *data, size_t size)
	{
		return granite_pipeline_cache_blob(device, table, pipeline_cache, gpu_props.pipelineCacheUUID, data, size);
	}

	bool init_pipeline_cache(const uint8_t *data, size_t size)
	{
		return granite_pipeline_cache_init(device, table, gpu_props.pipelineCacheUUID, data, size, &pipeline_cache);
	}

	size_t get_pipeline_records(const pipeline_record_t **records)
	{
		return granite_pipeline_record_table_get(&pipeline_records, records);
	}

	const RenderPass *recreate_render_pass(const pipeline_record_pass_t &signature)
	{
		return static_cast<const RenderPass *>(granite_device_recreate_render_pass(this, &signature));
	}

	bool resolve_pipeline_record(const pipeline_record_t &record, DeferredPipelineCompile &compile)
	{
		return granite_device_resolve_pipeline_record(this, &record, &compile);
	}

	// Frame-pushing interface.
	void next_frame_context()
	{
		granite_device_next_frame_context(this);
	}

	void wait_idle()
	{
		granite_device_wait_idle(this);
	}

	// Submission interface: the device's owner thread.
	void flush_frame()
	{
		granite_device_flush_frame(this);
	}

	CommandBufferHandle request_command_buffer(CommandBuffer::Type type = CommandBuffer::Type::Generic)
	{
		return CommandBufferHandle(static_cast<CommandBuffer *>(
				granite_device_request_command_buffer(this, unsigned(type))));
	}

	CommandBufferHandle request_command_buffer_for_thread(unsigned thread_index,
	                                                      CommandBuffer::Type type = CommandBuffer::Type::Generic)
	{
		return CommandBufferHandle(static_cast<CommandBuffer *>(
				granite_device_request_command_buffer_for_thread(this, thread_index, unsigned(type))));
	}

	// Semaphores (semaphore.h) are single-use: submit(&sem) signals one,
	// add_wait_semaphore(sem) has the next submission of a queue wait for
	// it. At most MAX_SUBMIT_SEMAPHORES a submission.
	enum { MAX_SUBMIT_SEMAPHORES = 4 };
	void submit(CommandBufferHandle &cmd, Fence *fence = nullptr,
	            unsigned semaphore_count = 0, Semaphore *semaphores = nullptr)
	{
		granite_fence_t *signalled = nullptr;
		granite_semaphore_t *signals[MAX_SUBMIT_SEMAPHORES] = {};

		if (semaphore_count > MAX_SUBMIT_SEMAPHORES)
			semaphore_count = MAX_SUBMIT_SEMAPHORES;
		granite_device_submit(this, cmd.release(), fence ? &signalled : nullptr, semaphore_count, signals);
		if (fence)
			*fence = Fence(static_cast<FenceHolder *>(signalled));
		for (unsigned i = 0; i < semaphore_count; i++)
			semaphores[i] = Semaphore(static_cast<SemaphoreHolder *>(signals[i]));
	}

	void submit_empty(CommandBuffer::Type type, Fence *fence = nullptr)
	{
		granite_fence_t *signalled = nullptr;
		granite_device_submit_empty(this, unsigned(type), fence ? &signalled : nullptr);
		if (fence)
			*fence = Fence(static_cast<FenceHolder *>(signalled));
	}

	void add_wait_semaphore(CommandBuffer::Type type, Semaphore semaphore, VkPipelineStageFlags stages, bool flush)
	{
		granite_device_add_wait_semaphore(this, unsigned(type), semaphore.get(), stages, flush);
	}

	// Submits one-off initialisation of a resource the way zero-initialised
	// buffers are: ordered before later work on every queue that may use it.
	void submit_initialization(CommandBufferHandle &cmd, VkBufferUsageFlags usage)
	{
		granite_device_submit_initialization(this, cmd.release(), usage);
	}

	// Shaders and programs are made once and kept (program_cache.h).
	Shader *request_shader(const uint32_t *code, size_t size, const ResourceLayout *layout = nullptr)
	{
		return static_cast<Shader *>(granite_program_cache_shader(&program_cache, code, size, layout));
	}

	Program *request_program(const granite_shader_t *compute)
	{
		return static_cast<Program *>(granite_program_cache_compute(&program_cache, compute));
	}

	Program *request_program(const granite_shader_t *vertex, const granite_shader_t *fragment)
	{
		return static_cast<Program *>(granite_program_cache_graphics(&program_cache, vertex, fragment));
	}

	Program *request_program(const uint32_t *compute_data, size_t compute_size,
	                         const ResourceLayout *layout = nullptr)
	{
		if (!compute_size)
			return nullptr;
		return request_program(request_shader(compute_data, compute_size, layout));
	}

	Program *request_program(const uint32_t *vertex_data, size_t vertex_size, const uint32_t *fragment_data,
	                         size_t fragment_size,
	                         const ResourceLayout *vertex_layout = nullptr,
	                         const ResourceLayout *fragment_layout = nullptr)
	{
		if (!vertex_size || !fragment_size)
			return nullptr;
		Shader *vertex = request_shader(vertex_data, vertex_size, vertex_layout);
		Shader *fragment = request_shader(fragment_data, fragment_size, fragment_layout);
		return request_program(vertex, fragment);
	}

	// Map and unmap buffer objects.
	void *map_host_buffer(const Buffer &buffer, MemoryAccessFlags access)
	{
		return granite_device_map_host_buffer(this, &buffer, access, 0, buffer.info.size);
	}

	void unmap_host_buffer(const Buffer &buffer, MemoryAccessFlags access)
	{
		granite_device_unmap_host_buffer(this, &buffer, access, 0, buffer.info.size);
	}

	void *map_host_buffer(const Buffer &buffer, MemoryAccessFlags access, VkDeviceSize offset, VkDeviceSize length)
	{
		return granite_device_map_host_buffer(this, &buffer, access, offset, length);
	}

	void unmap_host_buffer(const Buffer &buffer, MemoryAccessFlags access, VkDeviceSize offset, VkDeviceSize length)
	{
		granite_device_unmap_host_buffer(this, &buffer, access, offset, length);
	}

	// Create buffers, images and their views.
	BufferHandle create_buffer(const BufferCreateInfo &info, const void *initial = nullptr)
	{
		return BufferHandle(static_cast<Buffer *>(granite_device_create_buffer(this, &info, initial)));
	}

	BufferHandle create_imported_host_buffer(const BufferCreateInfo &info, void *host_buffer)
	{
		return BufferHandle(static_cast<Buffer *>(
				granite_device_create_imported_host_buffer(this, &info, host_buffer)));
	}

	ImageHandle create_image(const ImageCreateInfo &info, const ImageInitialData *initial = nullptr)
	{
		return ImageHandle(static_cast<Image *>(granite_device_create_image(this, &info, initial)));
	}

	ImageViewHandle create_image_view(const ImageViewCreateInfo &view_info)
	{
		return ImageViewHandle(static_cast<ImageView *>(granite_device_create_image_view(this, &view_info)));
	}

	BufferViewHandle create_buffer_view(const BufferViewCreateInfo &view_info)
	{
		granite_buffer_view_info_t info;
		info.buffer = view_info.buffer;
		info.format = view_info.format;
		info.offset = view_info.offset;
		info.range = view_info.range;
		return BufferViewHandle(static_cast<BufferView *>(granite_device_create_buffer_view(this, &info)));
	}

	bool image_format_is_supported(VkFormat format, VkFormatFeatureFlags2KHR required,
	                               VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL) const
	{
		return granite_image_format_is_supported(gpu, format, required, tiling, ext.supports_format_feature_flags2);
	}

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

	const Sampler &get_stock_sampler(StockSampler sampler) const
	{
		return samplers[unsigned(sampler)];
	}

	// For some platforms, the device and queue might be shared, possibly across threads, so need some mechanism to
	// lock the global device and queue.
	typedef void (*QueueLockCallback)(void *userdata);
	void set_queue_lock(QueueLockCallback lock_callback, QueueLockCallback unlock_callback, void *userdata)
	{
		submission.queue_lock = lock_callback;
		submission.queue_unlock = unlock_callback;
		submission.queue_user = userdata;
	}

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

	bool supports_subgroup_size_log2(bool subgroup_full_group,
	                                 uint8_t subgroup_minimum_size_log2,
	                                 uint8_t subgroup_maximum_size_log2) const
	{
		return granite_subgroup_size_supported(&ext.subgroup_size_control_features,
		                                       &ext.subgroup_size_control_properties, subgroup_full_group,
		                                       subgroup_minimum_size_log2, subgroup_maximum_size_log2);
	}

	const QueueInfo &get_queue_info() const
	{
		return queue_info;
	}
};

static_assert(PIPELINE_RECORD_ATTACHMENTS == VULKAN_NUM_ATTACHMENTS, "record attachments");
static_assert(GRANITE_FRAME_SYNC_QUEUES == QUEUE_INDEX_COUNT, "frame sync queues");
static_assert(GRANITE_DEVICE_MAX_UBO_SIZE == VULKAN_MAX_UBO_SIZE, "uniform block size");

inline Device &CommandBuffer::get_device()
{
	return *static_cast<Device *>(static_cast<granite_device_t *>(dev->user));
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
