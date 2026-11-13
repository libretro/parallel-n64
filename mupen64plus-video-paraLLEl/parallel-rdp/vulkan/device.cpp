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

#include "device.hpp"
#include "format.hpp"
#include "type_to_string.hpp"
#include "quirks.hpp"
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif


#ifdef GRANITE_VULKAN_MT
#include "thread_id.h"
static unsigned get_thread_index()
{
	return granite_thread_index();
}
// No lock. The Device's frame, submission, command pool, buffer pool and
// memory allocator state has a single owner at any time: the thread that
// processes RDP commands (parallel-rdp's command thread, or the emulation
// thread with single-threaded processing). Setup happens on the calling
// thread before the command thread starts, teardown after it is joined;
// in between, everything that needs that state is routed to the command
// thread (scanout, frame switches, run_on_command_thread). Other threads
// only release objects (lock-free per-frame stacks), wait on fences
// (lock-free claim) and use the lock-free pools and caches.
#define LOCK() ((void)0)
#define LOCK_MEMORY() ((void)0)
#define DRAIN_FRAME_LOCK() VK_ASSERT(lock.counter == 0)
#else
#define LOCK() ((void)0)
#define LOCK_MEMORY() ((void)0)
#define DRAIN_FRAME_LOCK() VK_ASSERT(lock.counter == 0)
static unsigned get_thread_index()
{
	return 0;
}
#endif

using namespace Util;

namespace Vulkan
{
static const char *queue_name_table[] = {
	"Graphics",
	"Compute",
	"Transfer",
	"Video decode"
};

static const QueueIndices queue_flush_order[] = {
	QUEUE_INDEX_TRANSFER,
	QUEUE_INDEX_VIDEO_DECODE,
	QUEUE_INDEX_GRAPHICS,
	QUEUE_INDEX_COMPUTE
};

Device::Device()
{
	granite_framebuffer_allocator_init(&framebuffer_allocator, VK_NULL_HANDLE, nullptr,
	                                   [](void *userdata, VkFramebuffer framebuffer) {
		static_cast<Device *>(userdata)->destroy_framebuffer_nolock(framebuffer);
	}, this);
	granite_submit_composer_init(&composer);
	fence_wait_ec_live = retro_eventcount_init(&fence_wait_ec);
	handle_owner.user = this;
	handle_owner.destroy = handle_destroy;
	handle_owner.free_memory = handle_free_memory;
	handle_owner.recycle_fence = handle_recycle_fence;
	granite_pipeline_record_table_init(&pipeline_records);
}

void Device::add_wait_semaphore(CommandBuffer::Type type, Semaphore semaphore, VkPipelineStageFlags stages, bool flush)
{
	LOCK();
	add_wait_semaphore_nolock(QueueIndices(type), Util::move(semaphore), stages, flush);
}

void Device::add_wait_semaphore_nolock(QueueIndices physical_type, Semaphore semaphore, VkPipelineStageFlags stages,
                                       bool flush)
{
	VK_ASSERT(stages != 0);
	if (flush)
		flush_frame(physical_type);
	auto &data = queue_data[physical_type];

	// (there was no room for the semaphore's object: nothing to wait for)
	if (!semaphore)
		return;

	if (!granite_queue_waits_add(&data.waits, semaphore.get(), stages))
	{
		// No room: an empty submission takes the ones waiting with it.
		submit_queue(physical_type, nullptr, 0, nullptr, true);
		granite_queue_waits_add(&data.waits, semaphore.get(), stages);
	}
	data.need_fence = true;
}

void *Device::map_host_buffer(const Buffer &buffer, MemoryAccessFlags access)
{
	return granite_device_allocator_map(&managers.memory, &buffer.alloc, access, 0, buffer.info.size);
}

void Device::unmap_host_buffer(const Buffer &buffer, MemoryAccessFlags access)
{
	granite_device_allocator_unmap(&managers.memory, &buffer.alloc, access, 0, buffer.info.size);
}

void *Device::map_host_buffer(const Buffer &buffer, MemoryAccessFlags access, VkDeviceSize offset, VkDeviceSize length)
{
	VK_ASSERT(offset + length <= buffer.get_create_info().size);
	return granite_device_allocator_map(&managers.memory, &buffer.alloc, access, offset, length);
}

void Device::unmap_host_buffer(const Buffer &buffer, MemoryAccessFlags access, VkDeviceSize offset, VkDeviceSize length)
{
	VK_ASSERT(offset + length <= buffer.get_create_info().size);
	granite_device_allocator_unmap(&managers.memory, &buffer.alloc, access, offset, length);
}

Shader *Device::request_shader(const uint32_t *data, size_t size, const ResourceLayout *layout)
{
	auto hash = Shader::hash(data, size);
	auto *ret = shaders.find(hash);
	if (!ret)
		ret = shaders.emplace_yield(hash, hash, this, data, size, layout);
	return ret;
}

Program *Device::request_program(Vulkan::Shader *compute_shader)
{
	if (!compute_shader)
		return nullptr;

	Util::Hasher hasher;
	hasher.u64(compute_shader->get_hash());

	auto hash = hasher.get();
	auto *ret = programs.find(hash);
	if (!ret)
		ret = programs.emplace_yield(hash, this, compute_shader);
	return ret;
}

Program *Device::request_program(const uint32_t *compute_data, size_t compute_size,
                                 const ResourceLayout *layout)
{
	if (!compute_size)
		return nullptr;

	auto *compute_shader = request_shader(compute_data, compute_size, layout);
	return request_program(compute_shader);
}

Program *Device::request_program(Shader *vertex, Shader *fragment)
{
	if (!vertex || !fragment)
		return nullptr;

	Util::Hasher hasher;
	hasher.u64(vertex->get_hash());
	hasher.u64(fragment->get_hash());

	auto hash = hasher.get();
	auto *ret = programs.find(hash);

	if (!ret)
		ret = programs.emplace_yield(hash, this, vertex, fragment);
	return ret;
}

Program *Device::request_program(const uint32_t *vertex_data, size_t vertex_size, const uint32_t *fragment_data,
                                 size_t fragment_size, const ResourceLayout *vertex_layout,
                                 const ResourceLayout *fragment_layout)
{
	if (!vertex_size || !fragment_size)
		return nullptr;

	auto *vertex = request_shader(vertex_data, vertex_size, vertex_layout);
	auto *fragment = request_shader(fragment_data, fragment_size, fragment_layout);
	return request_program(vertex, fragment);
}

PipelineLayout *Device::request_pipeline_layout(const CombinedResourceLayout &layout)
{
	auto hash = granite_combined_layout_hash(&layout);
	auto *ret = pipeline_layouts.find(hash);
	if (!ret)
		ret = pipeline_layouts.emplace_yield(hash, hash, this, layout);
	return ret;
}

DescriptorSetAllocator::DescriptorSetAllocator(Hash hash, Device *device, const granite_descriptor_set_layout_t &layout,
                                               const uint32_t *stages_for_bindings, bool push)
	: IntrusiveHashMapEnabled<DescriptorSetAllocator>(hash)
{
	granite_set_allocator_init(&impl, device->get_device(), &device->get_device_table(), &layout,
	                           stages_for_bindings, nullptr, push, device->num_thread_indices);
}

DescriptorSetAllocator *Device::request_descriptor_set_allocator(const granite_descriptor_set_layout_t &layout,
                                                                 const uint32_t *stages_for_bindings, bool push)
{
	Hasher h;
	h.u32(push);
	h.data(reinterpret_cast<const uint32_t *>(&layout), sizeof(layout));
	h.data(stages_for_bindings, sizeof(uint32_t) * VULKAN_NUM_BINDINGS);
	auto hash = h.get();

	auto *ret = descriptor_set_allocators.find(hash);
	if (!ret)
		ret = descriptor_set_allocators.emplace_yield(hash, hash, this, layout, stages_for_bindings, push);
	return ret;
}

void Device::bake_program(Program &program)
{
	CombinedResourceLayout layout;
	for (unsigned i = 0; i < static_cast<unsigned>(ShaderStage::Count); i++)
	{
		auto *shader = program.get_shader(static_cast<ShaderStage>(i));
		if (shader)
			granite_combined_layout_add(&layout, i, &shader->get_layout());
	}
	granite_combined_layout_bake(&layout);
	program.set_pipeline_layout(request_pipeline_layout(layout));
}

// What is left of shader.cpp: the layouts are C (shader.c).
PipelineLayout::PipelineLayout(Hash hash, Device *device, const CombinedResourceLayout &layout_)
	: IntrusiveHashMapEnabled<PipelineLayout>(hash)
	, layout(layout_)
{
	VkDescriptorSetLayout layouts[VULKAN_NUM_DESCRIPTOR_SETS] = {};
	auto &features = device->get_device_features();
	uint32_t push_set_mask = granite_combined_layout_push_set_mask(&layout, features.supports_push_descriptor,
	                                                               features.push_descriptor_properties.maxPushDescriptors);

	for (unsigned i = 0; i < VULKAN_NUM_DESCRIPTOR_SETS; i++)
	{
		auto *allocator = device->request_descriptor_set_allocator(layout.sets[i], layout.stages_for_bindings[i],
		                                                           (push_set_mask & (1u << i)) != 0);
		layouts[i] = allocator->get_layout();
		bind.allocators[i] = allocator->get_impl();
	}

	granite_pipeline_layout_init(&impl, device->get_device(), &device->get_device_table(), &layout, layouts,
	                             push_set_mask, device->get_gpu_properties().limits.maxBoundDescriptorSets);

	bind.hash = hash;
	bind.layout = impl.layout;
	bind.resource_layout = &layout;
	bind.update_templates = impl.update_template;
	bind.push_set_mask = push_set_mask;
}

Util::Hash Shader::hash(const uint32_t *data, size_t size)
{
	Util::Hasher hasher;
	hasher.data(data, size);
	return hasher.get();
}

Shader::Shader(Hash hash, Device *device_, const uint32_t *data, size_t size,
               const ResourceLayout *resource_layout)
	: IntrusiveHashMapEnabled<Shader>(hash)
	, device(device_)
{
	VkShaderModuleCreateInfo info = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
	info.codeSize = size;
	info.pCode = data;

#ifdef VULKAN_DEBUG
	LOGI("Creating shader module.\n");
#endif
	auto &table = device->get_device_table();
	if (table.vkCreateShaderModule(device->get_device(), &info, nullptr, &module) != VK_SUCCESS)
		LOGE("Failed to create shader module.\n");

	if (resource_layout)
		layout = *resource_layout;
}

Shader::~Shader()
{
	auto &table = device->get_device_table();
	if (module)
		table.vkDestroyShaderModule(device->get_device(), module, nullptr);
}

void Program::set_shader(ShaderStage stage, Shader *handle)
{
	shaders[Util::ecast(stage)] = handle;
}

Program::Program(Device *device_, Shader *vertex, Shader *fragment)
    : device(device_)
{
	granite_pipeline_map_init(&pipelines);
	set_shader(ShaderStage::Vertex, vertex);
	set_shader(ShaderStage::Fragment, fragment);
	device->bake_program(*this);
}

Program::Program(Device *device_, Shader *compute_shader)
    : device(device_)
{
	granite_pipeline_map_init(&pipelines);
	set_shader(ShaderStage::Compute, compute_shader);
	device->bake_program(*this);
}

Program::~Program()
{
	granite_pipeline_map_free(&pipelines, [](void *userdata, const granite_pipeline_t *pipeline) {
		static_cast<Device *>(userdata)->destroy_pipeline(pipeline->pipeline);
	}, device);
}

// The driver's pipeline cache and the list of pipelines built are C
// (pipeline_cache.h).
bool Device::init_pipeline_cache(const uint8_t *data, size_t size)
{
	return granite_pipeline_cache_init(device, table, gpu_props.pipelineCacheUUID, data, size, &pipeline_cache);
}

size_t Device::get_pipeline_cache_size()
{
	return granite_pipeline_cache_blob_size(device, table, pipeline_cache);
}

bool Device::get_pipeline_cache_data(uint8_t *data, size_t size)
{
	return granite_pipeline_cache_blob(device, table, pipeline_cache, gpu_props.pipelineCacheUUID, data, size);
}

static_assert(PIPELINE_RECORD_ATTACHMENTS == VULKAN_NUM_ATTACHMENTS, "record attachments");
static_assert(GRANITE_FRAME_SYNC_QUEUES == QUEUE_INDEX_COUNT, "frame sync queues");

void Device::record_pipeline(const DeferredPipelineCompile &compile, uint32_t kind)
{
	const bool graphics = kind == PIPELINE_RECORD_GRAPHICS;

	/* A pass that cannot be made again is not one to build ahead for. */
	if (graphics && !compile.compatible_render_pass->get_signature().recordable)
		return;

	granite_pipeline_record_ids_t ids = {};
	ids.program_hash = compile.program->get_hash();
	const Shader *shader = compile.program->get_shader(graphics ? ShaderStage::Vertex : ShaderStage::Compute);
	ids.shader_hashes[0] = shader ? shader->get_hash() : 0;
	if (graphics)
	{
		shader = compile.program->get_shader(ShaderStage::Fragment);
		ids.shader_hashes[1] = shader ? shader->get_hash() : 0;
		ids.render_pass_hash = compile.compatible_render_pass->get_hash();
		ids.pass = &compile.compatible_render_pass->get_signature();
	}

	pipeline_record_t *record = granite_pipeline_record_table_claim(&pipeline_records);
	if (!record)
		return;
	granite_pipeline_record_fill(record, kind, &compile, &ids);
	granite_pipeline_record_table_commit(&pipeline_records);
}

size_t Device::get_pipeline_records(const pipeline_record_t **records)
{
	return granite_pipeline_record_table_get(&pipeline_records, records);
}

/* A 1x1 attachment of @format: what a listed pass is made from again. */
const RenderPass *Device::recreate_render_pass(const pipeline_record_pass_t &sig)
{
	granite_render_pass_desc_t desc;
	if (!granite_render_pass_desc_from_signature(&desc, &sig))
		return nullptr;
	return &request_render_pass(desc, true);
}

bool Device::resolve_pipeline_record(const pipeline_record_t &record, DeferredPipelineCompile &compile)
{
	Program *program = programs.find(record.program_hash);
	const RenderPass *pass = nullptr;
	const bool graphics = record.kind == PIPELINE_RECORD_GRAPHICS;

	/* A program is made on first use; one this device has not made yet is
	 * made from the shaders it was made from, when those are here. */
	if (!program && !graphics)
		program = request_program(shaders.find(record.shader_hashes[0]));
	else if (!program)
		program = request_program(shaders.find(record.shader_hashes[0]),
		                          shaders.find(record.shader_hashes[1]));
	if (!program || program->get_hash() != record.program_hash || !program->get_pipeline_layout()
	    || (program->get_shader(ShaderStage::Compute) != nullptr) == graphics)
		return false;
	if (graphics)
	{
		pass = render_passes.find(record.render_pass_hash);
		if (!pass)
			pass = recreate_render_pass(record.pass);
		if (!pass || pass->get_hash() != record.render_pass_hash
		    || record.subpass_index != 0)
			return false;
	}

	granite_pipeline_record_apply(&record, &compile, pipeline_cache);
	compile.program = program;
	compile.compatible_render_pass = pass;
	if (graphics)
	{
		uint32_t active_vbos;
		CommandBuffer::update_hash_graphics_pipeline(compile, active_vbos);
	}
	else
		CommandBuffer::update_hash_compute_pipeline(compile);
	return compile.hash == record.pipeline_hash;
}

void Device::init_workarounds()
{
	workarounds = {};

#ifdef __APPLE__
	// Events are not supported in MoltenVK.
	// TODO: Use VK_KHR_portability_subset to determine this.
	workarounds.emulate_event_as_pipeline_barrier = true;
	LOGW("Emulating events as pipeline barriers on Metal emulation.\n");
#else
	if (gpu_props.vendorID == VENDOR_ID_ARM)
	{
		LOGW("Workaround applied: Emulating events as pipeline barriers.\n");
		LOGW("Workaround applied: Optimize ALL_GRAPHICS_BIT barriers.\n");

		// Both are performance related workarounds.
		workarounds.emulate_event_as_pipeline_barrier = true;
		workarounds.optimize_all_graphics_barrier = true;

		if (ext.timeline_semaphore_features.timelineSemaphore)
		{
			LOGW("Workaround applied: Split binary timeline semaphores.\n");
			workarounds.split_binary_timeline_semaphores = true;
		}
	}
	else if (ext.driver_properties.driverID == VK_DRIVER_ID_NVIDIA_PROPRIETARY)
	{
		LOGW("Disabling pipeline cache control.\n");
		workarounds.broken_pipeline_cache_control = true;
	}
#endif

	if (ext.supports_tooling_info && vkGetPhysicalDeviceToolPropertiesEXT)
	{
		uint32_t count = 0;
		vkGetPhysicalDeviceToolPropertiesEXT(gpu, &count, nullptr);
		Util::SmallVector<VkPhysicalDeviceToolPropertiesEXT> tool_props(count);
		for (auto &t : tool_props)
			t = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TOOL_PROPERTIES_EXT };
		vkGetPhysicalDeviceToolPropertiesEXT(gpu, &count, tool_props.data());
		for (auto &t : tool_props)
		{
			LOGI("  Detected attached tool:\n");
			LOGI("    Name: %s\n", t.name);
			LOGI("    Description: %s\n", t.description);
			LOGI("    Version: %s\n", t.version);
			if ((t.purposes & VK_TOOL_PURPOSE_TRACING_BIT_EXT) != 0)
			{
				LOGI("Detected tracing tool, forcing host cached memory types for performance.\n");
				workarounds.force_host_cached = true;
			}
		}
	}
}

void DeviceAllocator::init(Device *device)
{
	const auto &features = device->get_device_features();
	granite_device_allocator_init(this, device->get_device(), device->get_physical_device(),
	                              &device->get_device_table(), &device->get_memory_properties(),
	                              device->get_gpu_properties().limits.nonCoherentAtomSize,
	                              features.supports_memory_budget,
	                              features.memory_priority_features.memoryPriority != VK_FALSE);
}

void Device::set_context(const Context &context)
{
	table = &context.get_device_table();

#ifdef GRANITE_VULKAN_MT
	granite_register_thread_index(0);
#endif
	instance = context.get_instance();
	gpu = context.get_gpu();
	device = context.get_device();
	num_thread_indices = context.get_num_thread_indices();

	queue_info = context.get_queue_info();

	mem_props = context.get_mem_props();
	gpu_props = context.get_gpu_props();
	ext = context.get_enabled_device_features();

	init_workarounds();

	init_stock_samplers();

	init_timeline_semaphores();

	init_frame_contexts(2); // By default, regular double buffer between CPU and GPU.

	managers.memory.init(this);
	granite_semaphore_pool_init(&managers.semaphore, device, table);
	granite_fence_pool_init(&managers.fence, device, table);
	if (!granite_buffer_pool_init(&managers.vbo, &block_ops, this, 4 * 1024, 16,
	                              VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, 0, 256) ||
	    !granite_buffer_pool_init(&managers.ubo, &block_ops, this, 256 * 1024,
	                              Util::max_of<VkDeviceSize>(16u, gpu_props.limits.minUniformBufferOffsetAlignment),
	                              VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VULKAN_MAX_UBO_SIZE, 64))
		LOGE("Failed to set up the buffer pools.\n");
}

void Device::init_timeline_semaphores()
{
	if (!ext.timeline_semaphore_features.timelineSemaphore)
		return;

	VkSemaphoreTypeCreateInfoKHR type_info = { VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO_KHR };
	VkSemaphoreCreateInfo info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
	info.pNext = &type_info;
	type_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE_KHR;
	type_info.initialValue = 0;

	for (int i = 0; i < QUEUE_INDEX_COUNT; i++)
		if (table->vkCreateSemaphore(device, &info, nullptr, &queue_data[i].timeline_semaphore) != VK_SUCCESS)
			LOGE("Failed to create timeline semaphore.\n");
}

void Device::init_stock_samplers()
{
	granite_sampler_init_linear_clamp(&samplers[unsigned(StockSampler::LinearClamp)], device, table);
}

// The buffers behind the vertex and uniform blocks (buffer_pool.h): memory
// the host writes and the device reads in place.
static void *block_create(void *user, VkDeviceSize size, VkBufferUsageFlags usage, uint8_t **mapped)
{
	auto *device = static_cast<Device *>(user);
	BufferCreateInfo info;
	info.domain = BufferDomain::LinkedDeviceHost;
	info.size = size;
	info.usage = usage;

	auto buffer = device->create_buffer(info, nullptr);
	if (!buffer)
		return nullptr;
	device->set_name(*buffer, "chain-allocated-block");
	buffer->set_internal_sync_object();
	*mapped = static_cast<uint8_t *>(device->map_host_buffer(*buffer, MEMORY_ACCESS_WRITE_BIT));
	// The block owns the reference from here on.
	return buffer.release();
}

static void block_release(void *, void *handle)
{
	static_cast<Buffer *>(handle)->release_reference();
}

static uint8_t *block_map(void *user, void *handle)
{
	return static_cast<uint8_t *>(static_cast<Device *>(user)->map_host_buffer(*static_cast<Buffer *>(handle),
	                                                                             MEMORY_ACCESS_WRITE_BIT));
}

static void block_unmap(void *user, void *handle)
{
	static_cast<Device *>(user)->unmap_host_buffer(*static_cast<Buffer *>(handle), MEMORY_ACCESS_WRITE_BIT);
}

const granite_buffer_pool_ops_t Device::block_ops = { block_create, block_release, block_map, block_unmap };

void Device::request_vertex_block(granite_buffer_block_t &block, VkDeviceSize size)
{
	LOCK();
	request_vertex_block_nolock(block, size);
}

void Device::request_vertex_block_nolock(granite_buffer_block_t &block, VkDeviceSize size)
{
	granite_buffer_pool_exchange(&managers.vbo, &block, size, &frame().vbo_blocks);
}

void Device::request_uniform_block(granite_buffer_block_t &block, VkDeviceSize size)
{
	LOCK();
	request_uniform_block_nolock(block, size);
}

void Device::request_uniform_block_nolock(granite_buffer_block_t &block, VkDeviceSize size)
{
	granite_buffer_pool_exchange(&managers.ubo, &block, size, &frame().ubo_blocks);
}

void Device::submit(CommandBufferHandle &cmd, Fence *fence, unsigned semaphore_count, Semaphore *semaphores)
{
	LOCK();
	submit_nolock(Util::move(cmd), fence, semaphore_count, semaphores);
}

void Device::submit_discard_nolock(CommandBufferHandle &cmd)
{
	cmd.reset();
	decrement_frame_counter_nolock();
}

void Device::submit_nolock(CommandBufferHandle cmd, Fence *fence, unsigned semaphore_count, Semaphore *semaphores)
{
	auto physical_type = QueueIndices(cmd->get_command_buffer_type());
	auto &submissions = frame().submissions[physical_type];

	cmd->end();
	submissions.push_back(Util::move(cmd));

	InternalFence signalled_fence;

	if (fence || semaphore_count)
	{
		submit_queue(physical_type, fence ? &signalled_fence : nullptr,
		             semaphore_count, semaphores);
	}

	if (fence)
	{
		VK_ASSERT(!*fence);
		*fence = wrap_fence(signalled_fence);
	}

	decrement_frame_counter_nolock();
}

void Device::submit_empty(CommandBuffer::Type type, Fence *fence)
{
	LOCK();
	submit_empty_nolock(QueueIndices(type), fence);
}

void Device::submit_empty_nolock(QueueIndices physical_type, Fence *fence)
{
	if (physical_type != QUEUE_INDEX_TRANSFER)
		flush_frame(QUEUE_INDEX_TRANSFER);

	InternalFence signalled_fence;

	submit_queue(physical_type, fence ? &signalled_fence : nullptr, 0, nullptr);

	if (fence)
		*fence = wrap_fence(signalled_fence);
}

void Device::submit_initialization(CommandBufferHandle &cmd, VkBufferUsageFlags usage)
{
	LOCK();
	submit_staging(cmd, usage, true);
}

void Device::submit_staging(CommandBufferHandle &cmd, VkBufferUsageFlags usage, bool flush)
{
	VkQueue src_queue = queue_info.queues[QueueIndices(cmd->get_command_buffer_type())];

	granite_staging_plan_t plan;
	granite_staging_plan(src_queue, queue_info.queues[QUEUE_INDEX_GRAPHICS], queue_info.queues[QUEUE_INDEX_COMPUTE],
	                     buffer_usage_to_possible_stages(usage), buffer_usage_to_possible_access(usage), &plan);

	if (plan.barrier_stages)
		cmd->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
		             plan.barrier_stages, plan.barrier_access);

	// (graphics first, then compute)
	Semaphore semaphores[2];
	unsigned count = unsigned(plan.wait_graphics) + unsigned(plan.wait_compute);
	submit_nolock(cmd, nullptr, count, semaphores);
	if (plan.wait_graphics)
		add_wait_semaphore_nolock(QUEUE_INDEX_GRAPHICS, semaphores[0], plan.graphics_stages, flush);
	if (plan.wait_compute)
		add_wait_semaphore_nolock(QUEUE_INDEX_COMPUTE, semaphores[plan.wait_graphics ? 1 : 0], plan.compute_stages, flush);
}

void Device::emit_queue_signals(VkSemaphore sem, uint64_t timeline, InternalFence *fence,
                                unsigned semaphore_count, Semaphore *semaphores)
{
	// Add external signal semaphores.
	if (ext.timeline_semaphore_features.timelineSemaphore)
	{
		// Signal once and distribute the timeline value to all.
		granite_submit_composer_add_signal(&composer, sem, timeline);

		if (fence)
		{
			fence->timeline = sem;
			fence->value = timeline;
			fence->fence = VK_NULL_HANDLE;
		}

		for (unsigned i = 0; i < semaphore_count; i++)
		{
			VK_ASSERT(!semaphores[i]);
			semaphores[i] = Semaphore(static_cast<SemaphoreHolder *>(
					granite_semaphore_new_timeline(&handle_pool.semaphores, &handle_owner, sem, timeline)));
		}
	}
	else
	{
		if (fence)
		{
			fence->timeline = VK_NULL_HANDLE;
			fence->value = 0;
		}

		for (unsigned i = 0; i < semaphore_count; i++)
		{
			VkSemaphore cleared_semaphore = granite_semaphore_pool_request(&managers.semaphore);
			granite_submit_composer_add_signal(&composer, cleared_semaphore, 0);
			VK_ASSERT(!semaphores[i]);
			auto *object = granite_semaphore_new_binary(&handle_pool.semaphores, &handle_owner, cleared_semaphore);
			// Without an object for it nothing will wait for it: it is
			// signalled, and destroyed when this frame comes round.
			if (!object)
				granite_frame_garbage_push(&frame().garbage, GRANITE_GARBAGE_SEMAPHORE, GRANITE_VK_TO_BITS(cleared_semaphore));
			semaphores[i] = Semaphore(static_cast<SemaphoreHolder *>(object));
		}
	}
}

VkResult Device::submit_batches(VkQueue queue, VkFence fence)
{
	if (queue_lock_callback)
		queue_lock_callback(queue_lock_userdata);
	VkResult result = granite_submit_composer_submit(&composer, table, queue, fence,
	                                                 get_workarounds().split_binary_timeline_semaphores);
	if (queue_unlock_callback)
		queue_unlock_callback(queue_lock_userdata);
	return result;
}

// Everything the frame has for the queue goes in one submission, which
// waits for the queue's semaphores and signals the fence and semaphores
// asked for. With nothing to submit and nothing to signal there is no
// submission, unless @force asks for an empty one.
void Device::submit_queue(QueueIndices physical_type, InternalFence *fence,
                          unsigned semaphore_count, Semaphore *semaphores, bool force)
{
	// Always check if we need to flush pending transfers.
	if (physical_type != QUEUE_INDEX_TRANSFER)
		flush_frame(QUEUE_INDEX_TRANSFER);

	auto &data = queue_data[physical_type];
	auto &submissions = frame().submissions[physical_type];

	if (submissions.empty() && !fence && !semaphore_count && !force)
		return;

	VkSemaphore timeline_semaphore = data.timeline_semaphore;
	uint64_t timeline_value = ++data.current_timeline;

	VkQueue queue = queue_info.queues[physical_type];
	frame().sync.timeline_values[physical_type] = data.current_timeline;

	granite_submit_composer_begin(&composer, workarounds.split_binary_timeline_semaphores);
	granite_queue_waits_collect(&data.waits, &composer, &frame().garbage);

	for (auto &cmd : submissions)
		granite_submit_composer_add_command_buffer(&composer, cmd->get_command_buffer());

	VkFence cleared_fence = fence && !ext.timeline_semaphore_features.timelineSemaphore ?
	                        granite_fence_pool_request(&managers.fence) :
	                        VK_NULL_HANDLE;

	if (fence)
		fence->fence = cleared_fence;

	emit_queue_signals(timeline_semaphore, timeline_value,
	                   fence, semaphore_count, semaphores);

	auto result = submit_batches(queue, cleared_fence);

	if (result != VK_SUCCESS)
		LOGE("vkQueueSubmit failed (code: %d).\n", int(result));
	if (result == VK_ERROR_DEVICE_LOST)
		report_checkpoints();
	submissions.clear();

	if (!ext.timeline_semaphore_features.timelineSemaphore)
		data.need_fence = true;
}

void Device::flush_frame(QueueIndices physical_type)
{
	if (queue_info.queues[physical_type] == VK_NULL_HANDLE)
		return;

	submit_queue(physical_type, nullptr);
}

void Device::end_frame_nolock()
{
	// Make sure we have a fence which covers all submissions in the frame.
	InternalFence fence;

	for (auto &i : queue_flush_order)
	{
		if (queue_data[i].need_fence || !frame().submissions[i].empty())
		{
			submit_queue(i, &fence);
			if (fence.fence != VK_NULL_HANDLE)
			{
				// (no room: wait for it here and now)
				if (!granite_frame_sync_add_fence(&frame().sync, fence.fence))
					table->vkWaitForFences(device, 1, &fence.fence, VK_TRUE, UINT64_MAX);
				granite_frame_garbage_push(&frame().garbage, GRANITE_GARBAGE_RECYCLE_FENCE, GRANITE_VK_TO_BITS(fence.fence));
			}
			queue_data[i].need_fence = false;
		}
	}
}

void Device::flush_frame()
{
	LOCK();
	flush_frame_nolock();
}

void Device::flush_frame_nolock()
{
	for (auto &i : queue_flush_order)
		flush_frame(i);
}

CommandBufferHandle Device::request_command_buffer(CommandBuffer::Type type)
{
	return request_command_buffer_for_thread(get_thread_index(), type);
}

CommandBufferHandle Device::request_command_buffer_for_thread(unsigned thread_index, CommandBuffer::Type type)
{
	LOCK();
	return request_command_buffer_nolock(thread_index, type);
}

CommandBufferHandle Device::request_command_buffer_nolock(unsigned thread_index, CommandBuffer::Type type)
{
#ifndef GRANITE_VULKAN_MT
	VK_ASSERT(thread_index == 0);
#endif
	auto physical_type = QueueIndices(type);
	auto cmd = granite_command_pool_request(&frame().cmd_pools[physical_type][thread_index]);
	// Out of memory: nothing to record into.
	if (cmd == VK_NULL_HANDLE)
		return {};

	VkCommandBufferBeginInfo info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	table->vkBeginCommandBuffer(cmd, &info);
	add_frame_counter_nolock();
	CommandBufferHandle handle(handle_pool.command_buffers.allocate(this, cmd, pipeline_cache, type));
	handle->set_thread_index(thread_index);

	return handle;
}

const Sampler &Device::get_stock_sampler(StockSampler sampler) const
{
	return samplers[static_cast<unsigned>(sampler)];
}

Device::~Device()
{
	wait_idle();
	struct FenceEcFree
	{
		Device *d;
		~FenceEcFree()
		{
			if (d->fence_wait_ec_live)
				retro_eventcount_free(&d->fence_wait_ec);
		}
	} fence_ec_free{this};

	if (pipeline_cache != VK_NULL_HANDLE)
	{
		table->vkDestroyPipelineCache(device, pipeline_cache, nullptr);
	}

	for (auto &sampler : samplers)
		granite_sampler_free(&sampler, device, table);


	granite_pipeline_record_table_free(&pipeline_records);

	granite_framebuffer_allocator_free(&framebuffer_allocator);
	granite_submit_composer_free(&composer);

	deinit_timeline_semaphores();
}

void Device::deinit_timeline_semaphores()
{
	for (auto &data : queue_data)
	{
		if (data.timeline_semaphore != VK_NULL_HANDLE)
			table->vkDestroySemaphore(device, data.timeline_semaphore, nullptr);
		data.timeline_semaphore = VK_NULL_HANDLE;
	}

	// Make sure we don't accidentally try to wait for these after we destroy the semaphores.
	for (auto &frame : per_frame)
		granite_frame_sync_forget_timelines(&frame->sync);
}

void Device::init_frame_contexts(unsigned count)
{
	DRAIN_FRAME_LOCK();
	wait_idle_nolock();

	// Clear out caches which might contain stale data from now on.
	granite_framebuffer_allocator_clear(&framebuffer_allocator);
	per_frame.clear();

	if (count > MAX_DEFERRED_FRAMES)
		count = MAX_DEFERRED_FRAMES;
	for (unsigned i = 0; i < count; i++)
	{
		per_frame.push_back(Util::create<PerFrame>(this, i));
	}
	if (frame_context_index >= per_frame.size())
		frame_context_index = 0;
	granite_release_queue_set_frame(&releases.q, frame_context_index);
}

Device::PerFrame::PerFrame(Device *device_, unsigned frame_index_)
    : device(*device_)
    , frame_index(frame_index_)
    , table(device_->get_device_table())
    , managers(device_->managers)
{
	unsigned count = device_->num_thread_indices;
	VkSemaphore timelines[QUEUE_INDEX_COUNT];
	for (int i = 0; i < QUEUE_INDEX_COUNT; i++)
		timelines[i] = device.queue_data[i].timeline_semaphore;
	granite_frame_sync_init(&sync, timelines);
	for (int i = 0; i < QUEUE_INDEX_COUNT; i++)
	{
		cmd_pools[i].resize(count);
		for (unsigned j = 0; j < count; j++)
			granite_command_pool_init(&cmd_pools[i][j], device_->get_device(), &table,
			                          device_->queue_info.family_indices[i]);
	}
}

// Moves everything released to this frame into its lists, which begin()
// then processes. Called after the frame's GPU work has been waited for.
void Device::drain_deferred_releases(PerFrame &f)
{
	granite_release_queue_drain(&releases.q, f.frame_index, &f.garbage);
	granite_memory_release_queue_drain(&releases.memory, f.frame_index, &f.memory);
}

void Device::free_memory_nolock(const DeviceAllocation &alloc)
{
	granite_memory_garbage_push(&frame().memory, &alloc);
}

void Device::destroy_pipeline(VkPipeline pipeline)
{
	release_handle(GRANITE_GARBAGE_PIPELINE, GRANITE_VK_TO_BITS(pipeline));
}

// The counted object for what a submission is waited on by.
Fence Device::wrap_fence(const InternalFence &fence)
{
	granite_fence_env_t env;
	env.device = device;
	env.table = table;
	env.wait_ec = fence_wait_ec_live ? &fence_wait_ec : nullptr;
	auto *object = granite_fence_new(&handle_pool.fences, &handle_owner, &env, fence.fence, fence.timeline, fence.value);
	// Without one the fence still goes back when its frame comes round.
	if (!object && !fence.value && fence.fence != VK_NULL_HANDLE)
		reset_fence(fence.fence, false);
	return Fence(static_cast<FenceHolder *>(object));
}

void Device::handle_recycle_fence(void *user, VkFence fence, bool seen, bool owner_thread)
{
	auto *self = static_cast<Device *>(user);
	if (owner_thread)
		self->reset_fence_nolock(fence, seen);
	else
		self->reset_fence(fence, seen);
}

void Device::reset_fence(VkFence fence, bool observed_wait)
{
	// Reset and recycled when this frame comes round again, whether or
	// not someone already waited on it.
	(void)observed_wait;
	release_handle(GRANITE_GARBAGE_RECYCLE_FENCE, GRANITE_VK_TO_BITS(fence));
}

void Device::free_memory(const DeviceAllocation &alloc)
{
	granite_memory_release_queue_push(&releases.memory, granite_release_queue_frame(&releases.q), &alloc);
}

void Device::reset_fence_nolock(VkFence fence, bool observed_wait)
{
	if (observed_wait)
	{
		table->vkResetFences(device, 1, &fence);
		granite_fence_pool_recycle(&managers.fence, fence);
	}
	else
		granite_frame_garbage_push(&frame().garbage, GRANITE_GARBAGE_RECYCLE_FENCE, GRANITE_VK_TO_BITS(fence));
}

void Device::destroy_framebuffer_nolock(VkFramebuffer framebuffer)
{
	VK_ASSERT(!granite_frame_garbage_has(&frame().garbage, GRANITE_GARBAGE_FRAMEBUFFER, GRANITE_VK_TO_BITS(framebuffer)));
	granite_frame_garbage_push(&frame().garbage, GRANITE_GARBAGE_FRAMEBUFFER, GRANITE_VK_TO_BITS(framebuffer));
}

void Device::clear_wait_semaphores()
{
	// (the device is idle: nothing will wait for them)
	for (auto &data : queue_data)
		granite_queue_waits_clear(&data.waits, device, table);
}

void Device::wait_idle()
{
	DRAIN_FRAME_LOCK();
	wait_idle_nolock();
}

void Device::wait_idle_nolock()
{
	if (!per_frame.empty())
		end_frame_nolock();

	if (device != VK_NULL_HANDLE)
	{
		if (queue_lock_callback)
			queue_lock_callback(queue_lock_userdata);
		auto result = table->vkDeviceWaitIdle(device);
		if (result != VK_SUCCESS)
			LOGE("vkDeviceWaitIdle failed with code: %d\n", result);
		if (result == VK_ERROR_DEVICE_LOST)
			report_checkpoints();
		if (queue_unlock_callback)
			queue_unlock_callback(queue_lock_userdata);
	}

	clear_wait_semaphores();

	// Free memory for buffer pools.
	granite_buffer_pool_reset(&managers.vbo);
	granite_buffer_pool_reset(&managers.ubo);
	for (auto &frame : per_frame)
	{
		granite_buffer_block_list_release(&frame->vbo_blocks, &managers.vbo);
		granite_buffer_block_list_release(&frame->ubo_blocks, &managers.ubo);
	}

	granite_framebuffer_allocator_clear(&framebuffer_allocator);

#ifdef GRANITE_VULKAN_MT
	for (auto &allocator : descriptor_set_allocators.get_read_only())
		allocator.clear();
	for (auto &allocator : descriptor_set_allocators.get_read_write())
		allocator.clear();
#else
	for (auto &allocator : descriptor_set_allocators)
		allocator.clear();
#endif

	for (auto &frame : per_frame)
	{
		// We have done WaitIdle, no need to wait for extra fences, it's also not safe.
		granite_frame_sync_forget_fences(&frame->sync);
		frame->begin();
		frame->trim_command_pools();
	}

	{
		LOCK_MEMORY();
		managers.memory.garbage_collect();
	}
}

void Device::next_frame_context()
{
	DRAIN_FRAME_LOCK();

	// Flush the frame here as we might have pending staging command buffers from init stage.
	end_frame_nolock();

	granite_framebuffer_allocator_begin_frame(&framebuffer_allocator);

#ifdef GRANITE_VULKAN_MT
	for (auto &allocator : descriptor_set_allocators.get_read_only())
		allocator.begin_frame();
	for (auto &allocator : descriptor_set_allocators.get_read_write())
		allocator.begin_frame();
#else
	for (auto &allocator : descriptor_set_allocators)
		allocator.begin_frame();
#endif

	VK_ASSERT(!per_frame.empty());
	frame_context_index++;
	if (frame_context_index >= per_frame.size())
		frame_context_index = 0;

	frame().begin();
	granite_release_queue_set_frame(&releases.q, frame_context_index);
}

void Device::add_frame_counter_nolock()
{
	lock.counter++;
}

void Device::decrement_frame_counter_nolock()
{
	VK_ASSERT(lock.counter > 0);
	lock.counter--;
}

void Device::PerFrame::trim_command_pools()
{
	for (auto &cmd_pool : cmd_pools)
		for (auto &pool : cmd_pool)
			granite_command_pool_trim(&pool);
}

void Device::PerFrame::begin()
{
	VkDevice vkdevice = device.get_device();

	granite_frame_sync_wait(&sync, vkdevice, &table,
	                        device.get_device_features().timeline_semaphore_features.timelineSemaphore != VK_FALSE);

	// This frame's GPU work is done: take what other threads released to it.
	device.drain_deferred_releases(*this);

	for (auto &cmd_pool : cmd_pools)
		for (auto &pool : cmd_pool)
			granite_command_pool_begin(&pool);

	granite_buffer_block_list_recycle(&vbo_blocks, &managers.vbo);
	granite_buffer_block_list_recycle(&ubo_blocks, &managers.ubo);

	granite_frame_garbage_collect(&garbage, vkdevice, &table, &managers.semaphore, &managers.fence);

	granite_memory_garbage_collect(&memory, &managers.memory);
}

Device::PerFrame::~PerFrame()
{
	begin();
	// (begin() gave the blocks back to their pools)
	granite_buffer_block_list_free(&vbo_blocks);
	granite_buffer_block_list_free(&ubo_blocks);
	for (auto &cmd_pool : cmd_pools)
		for (auto &pool : cmd_pool)
			granite_command_pool_free(&pool);
	granite_frame_garbage_free(&garbage);
	granite_memory_garbage_free(&memory);
}

uint32_t Device::find_memory_type(granite_buffer_domain_t domain, uint32_t mask) const
{
	return granite_find_buffer_memory_type(&mem_props, granite_buffer_domain_t(domain), mask,
	                                       workarounds.force_host_cached);
}

BufferViewHandle Device::create_buffer_view(const BufferViewCreateInfo &view_info)
{
	VkBufferViewCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO };
	info.buffer = view_info.buffer->get_buffer();
	info.format = view_info.format;
	info.offset = view_info.offset;
	info.range = view_info.range;

	VkBufferView view;
	auto res = table->vkCreateBufferView(device, &info, nullptr, &view);
	if (res != VK_SUCCESS)
		return BufferViewHandle(nullptr);

	granite_buffer_view_info_t object_info;
	object_info.buffer = view_info.buffer;
	object_info.format = view_info.format;
	object_info.offset = view_info.offset;
	object_info.range = view_info.range;
	auto *object = granite_buffer_view_new(&handle_pool.buffers, &handle_owner, view, &object_info);
	if (!object)
		table->vkDestroyBufferView(device, view, nullptr);
	return BufferViewHandle(static_cast<BufferView *>(object));
}

ImageViewHandle Device::create_image_view(const ImageViewCreateInfo &create_info)
{
	auto &image_create_info = create_info.image->info;

	granite_image_view_desc_t desc = {};
	desc.image = create_info.image->image;
	desc.format = create_info.format != VK_FORMAT_UNDEFINED ? create_info.format : image_create_info.format;
	desc.type = create_info.view_type;
	desc.base_level = create_info.base_level;
	desc.base_layer = create_info.base_layer;
	desc.levels = create_info.levels == VK_REMAINING_MIP_LEVELS ?
	              image_create_info.levels - create_info.base_level : create_info.levels;
	desc.layers = create_info.layers == VK_REMAINING_ARRAY_LAYERS ?
	              image_create_info.layers - create_info.base_layer : create_info.layers;
	desc.usage = image_create_info.usage;

	granite_image_views_t views;
	if (!granite_image_views_create(device, table, &desc, &views))
		return ImageViewHandle(nullptr);

	granite_image_view_info_t object_info = create_info;
	object_info.format = desc.format;
	auto *object = granite_image_view_new(&handle_pool.images, &handle_owner, &views, &object_info);
	if (!object)
		granite_image_views_destroy(device, table, &views);
	return ImageViewHandle(static_cast<ImageView *>(object));
}

bool Device::create_image_staging_buffer(const ImageCreateInfo &info, const ImageInitialData *initial,
                                         InitialImageBuffer &result)
{
	granite_texture_layout_t layout;

	if (info.type != VK_IMAGE_TYPE_2D)
		return false;
	granite_texture_layout_set(&layout, info.type, info.format, info.width, info.height, info.depth,
	                           info.layers, info.levels);

	BufferCreateInfo buffer_info = {};
	buffer_info.domain = BufferDomain::Host;
	buffer_info.size = layout.required_size;
	buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	result.buffer = create_buffer(buffer_info, nullptr);
	if (!result.buffer)
		return false;
	set_name(*result.buffer, "image-upload-staging-buffer");

	layout.buffer = static_cast<uint8_t *>(map_host_buffer(*result.buffer, MEMORY_ACCESS_WRITE_BIT));
	if (!layout.buffer)
		return false;
	granite_image_upload_fill(&layout, initial);
	unmap_host_buffer(*result.buffer, MEMORY_ACCESS_WRITE_BIT);

	result.num_blits = layout.mip_levels;
	granite_texture_layout_copies(&layout, format_to_aspect_mask(info.format), result.blits);
	return true;
}

ImageHandle Device::create_image(const ImageCreateInfo &create_info, const ImageInitialData *initial)
{
	if (initial)
	{
		InitialImageBuffer staging_buffer;
		if (!create_image_staging_buffer(create_info, initial, staging_buffer))
			return ImageHandle(nullptr);
		return create_image_from_staging_buffer(create_info, &staging_buffer);
	}
	else
		return create_image_from_staging_buffer(create_info, nullptr);
}

ImageHandle Device::create_image_from_staging_buffer(const ImageCreateInfo &create_info,
                                                     const InitialImageBuffer *staging_buffer)
{
	if (create_info.type != VK_IMAGE_TYPE_2D || create_info.depth != 1)
		return ImageHandle(nullptr);

	granite_image_desc_t desc = {};
	desc.format = create_info.format;
	desc.width = create_info.width;
	desc.height = create_info.height;
	desc.levels = create_info.levels;
	desc.layers = create_info.layers;
	desc.samples = create_info.samples;
	desc.usage = create_info.usage;
	desc.flags = create_info.flags;
	desc.force_array = (create_info.misc & IMAGE_MISC_FORCE_ARRAY_BIT) != 0;
	desc.mutable_srgb = (create_info.misc & IMAGE_MISC_MUTABLE_SRGB_BIT) != 0;
	desc.on_graphics = (create_info.misc & IMAGE_MISC_CONCURRENT_QUEUE_GRAPHICS_BIT) != 0;
	desc.on_compute = (create_info.misc & IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_COMPUTE_BIT) != 0;
	bool concurrent_queue = desc.on_graphics || desc.on_compute;

	VkImageUsageFlags usage = create_info.usage;
	if (staging_buffer)
		usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

	if ((create_info.flags & VK_IMAGE_CREATE_EXTENDED_USAGE_BIT) == 0 &&
	    !image_format_is_supported(create_info.format, granite_image_usage_features(usage)))
	{
		LOGE("Format %u is not supported for usage flags!\n", unsigned(create_info.format));
		return ImageHandle(nullptr);
	}

	granite_image_env_t env = {};
	env.device = device;
	env.table = table;
	env.allocator = &managers.memory;
	env.memory = &mem_props;
	env.graphics_family = queue_info.family_indices[QUEUE_INDEX_GRAPHICS];
	env.compute_family = queue_info.family_indices[QUEUE_INDEX_COMPUTE];
	env.transfer_family = queue_info.family_indices[QUEUE_INDEX_TRANSFER];
	env.format_list = ext.supports_image_format_list;

	VkImage image;
	DeviceAllocation allocation;
	VkImageCreateFlags flags;
	{
		LOCK_MEMORY();
		if (!granite_image_create(&env, &desc, staging_buffer != nullptr, &image, &allocation, &usage, &flags))
			return ImageHandle(nullptr);
	}

	auto tmpinfo = create_info;
	tmpinfo.usage = usage;
	tmpinfo.flags = flags;

	bool has_view = granite_image_usage_has_view(usage);
	VkImageViewType view_type = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
	granite_image_views_t views = {};
	if (has_view)
	{
		granite_image_view_desc_t view_desc = {};
		view_desc.image = image;
		view_desc.format = create_info.format;
		view_desc.type = view_type = granite_image_view_type(&desc);
		view_desc.levels = create_info.levels;
		view_desc.layers = create_info.layers;
		view_desc.usage = usage;
		if (!granite_image_views_create(device, table, &view_desc, &views))
		{
			table->vkDestroyImage(device, image, nullptr);
			LOCK_MEMORY();
			allocation.free_immediate(managers.memory);
			return ImageHandle(nullptr);
		}
	}

	ImageHandle handle(static_cast<Image *>(granite_image_new(&handle_pool.images, &handle_owner, image, &allocation,
	                                                           &tmpinfo, has_view ? &views : nullptr, view_type)));
	if (!handle)
	{
		granite_image_views_destroy(device, table, &views);
		table->vkDestroyImage(device, image, nullptr);
		LOCK_MEMORY();
		allocation.free_immediate(managers.memory);
		return ImageHandle(nullptr);
	}

	// Set possible dstStage and dstAccess.
	handle->set_stage_flags(granite_image_usage_stages(usage));
	handle->set_access_flags(granite_image_usage_access(usage));

	granite_image_upload_queues_t queues;
	queues.graphics = queue_info.queues[QUEUE_INDEX_GRAPHICS];
	queues.compute = queue_info.queues[QUEUE_INDEX_COMPUTE];
	queues.transfer = queue_info.queues[QUEUE_INDEX_TRANSFER];
	queues.graphics_family = queue_info.family_indices[QUEUE_INDEX_GRAPHICS];
	queues.compute_family = queue_info.family_indices[QUEUE_INDEX_COMPUTE];
	queues.transfer_family = queue_info.family_indices[QUEUE_INDEX_TRANSFER];

	granite_image_upload_desc_t upload = {};
	upload.image = image;
	upload.format = create_info.format;
	upload.levels = create_info.levels;
	upload.layers = create_info.layers;
	upload.initial_layout = create_info.initial_layout;
	upload.stages = handle->get_stage_flags();
	upload.access = handle->get_access_flags();
	upload.concurrent = concurrent_queue;
	upload.on_compute = desc.on_compute;
	upload.has_data = staging_buffer != nullptr;

	granite_image_upload_plan_t plan;
	granite_image_upload_plan(&queues, &upload, &plan);
	if (!plan.transition)
		return handle;

	auto graphics_cmd = request_command_buffer(CommandBuffer::Type::Generic);

	// Copy initial data to texture.
	if (staging_buffer)
	{
		VK_ASSERT(create_info.initial_layout != VK_IMAGE_LAYOUT_UNDEFINED);

		// Don't split the upload into multiple command buffers unless we have to.
		CommandBufferHandle transfer_cmd;
		if (plan.split_transfer)
			transfer_cmd = request_command_buffer(CommandBuffer::Type::AsyncTransfer);
		else
			transfer_cmd = graphics_cmd;

		transfer_cmd->image_barrier(*handle, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT,
		                            VK_ACCESS_TRANSFER_WRITE_BIT);

		transfer_cmd->begin_region("copy-image-to-gpu");
		transfer_cmd->copy_buffer_to_image(*handle, *staging_buffer->buffer, staging_buffer->num_blits, staging_buffer->blits);
		transfer_cmd->end_region();

		if (plan.split_transfer)
		{
			if (plan.hand_over)
			{
				transfer_cmd->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
				                      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
				                      0, nullptr, 0, nullptr, 1, &plan.release);

				graphics_cmd->barrier(plan.wait_stages, plan.wait_stages,
				                      0, nullptr, 0, nullptr, 1, &plan.acquire);
			}

			Semaphore sem;
			submit(transfer_cmd, nullptr, 1, &sem);
			add_wait_semaphore(CommandBuffer::Type::Generic, sem, plan.wait_stages, true);
		}
	}

	if (plan.final_barrier)
	{
		graphics_cmd->image_barrier(*handle, plan.old_layout, create_info.initial_layout,
		                            plan.src_stage, plan.src_access,
		                            handle->get_stage_flags(), plan.dst_access);
	}

	// For concurrent queue, make sure that compute can see the final image as well.
	if (plan.share_compute)
	{
		Semaphore sem;
		submit(graphics_cmd, nullptr, 1, &sem);
		add_wait_semaphore(CommandBuffer::Type::AsyncCompute, sem, plan.compute_stages, true);
	}
	else
	{
		LOCK();
		submit_nolock(graphics_cmd, nullptr, 0, nullptr);
		if (plan.flush_graphics)
			flush_frame(QUEUE_INDEX_GRAPHICS);
	}

	return handle;
}

BufferHandle Device::create_imported_host_buffer(const BufferCreateInfo &create_info, void *host_buffer)
{
	if (!ext.supports_external_memory_host)
		return BufferHandle{};

	granite_buffer_env_t env;
	env.device = device;
	env.table = table;
	env.allocator = &managers.memory;
	env.memory = &mem_props;
	env.queue_families = queue_info.family_indices;
	env.num_queue_families = QUEUE_INDEX_COUNT;
	env.force_host_cached = workarounds.force_host_cached;

	VkBuffer buffer;
	DeviceAllocation allocation;
	{
		LOCK_MEMORY();
		if (!granite_buffer_create_host_import(&env, create_info.domain, create_info.size, create_info.usage,
		                                       ext.host_memory_properties.minImportedHostPointerAlignment,
		                                       host_buffer, &buffer, &allocation))
			return BufferHandle{};
	}

	return wrap_buffer(buffer, allocation, create_info);
}

// The counted object for a buffer and its memory; without room for one,
// neither is kept.
BufferHandle Device::wrap_buffer(VkBuffer buffer, const DeviceAllocation &allocation, const granite_buffer_info_t &info)
{
	auto *object = granite_buffer_new(&handle_pool.buffers, &handle_owner, buffer, &allocation, &info);
	if (!object)
	{
		table->vkDestroyBuffer(device, buffer, nullptr);
		granite_allocation_t alloc = allocation;
		LOCK_MEMORY();
		granite_allocation_free(&alloc, &managers.memory);
	}
	return BufferHandle(static_cast<Buffer *>(object));
}

// What the device's objects hand back when their last reference goes
// (handle.h): to this frame's garbage on the owner thread, through the
// release queue from any other.
void Device::handle_destroy(void *user, unsigned kind, uint64_t handle, bool owner_thread)
{
	auto *self = static_cast<Device *>(user);
	auto garbage_kind = granite_garbage_kind(kind);
	if (owner_thread)
	{
		VK_ASSERT(!granite_frame_garbage_has(&self->frame().garbage, garbage_kind, handle));
		granite_frame_garbage_push(&self->frame().garbage, garbage_kind, handle);
	}
	else
		self->release_handle(garbage_kind, handle);
}

void Device::handle_free_memory(void *user, const granite_allocation_t *alloc, bool owner_thread)
{
	auto *self = static_cast<Device *>(user);
	DeviceAllocation allocation;
	static_cast<granite_allocation_t &>(allocation) = *alloc;
	if (owner_thread)
		self->free_memory_nolock(allocation);
	else
		self->free_memory(allocation);
}

BufferHandle Device::create_buffer(const BufferCreateInfo &create_info, const void *initial)
{
	DeviceAllocation allocation;
	VkBuffer buffer;
	uint32_t memory_type;

	bool zero_initialize = (create_info.misc & BUFFER_MISC_ZERO_INITIALIZE_BIT) != 0;
	if (initial && zero_initialize)
	{
		LOGE("Cannot initialize buffer with data and clear.\n");
		return BufferHandle{};
	}

	granite_buffer_env_t env;
	env.device = device;
	env.table = table;
	env.allocator = &managers.memory;
	env.memory = &mem_props;
	env.queue_families = queue_info.family_indices;
	env.num_queue_families = QUEUE_INDEX_COUNT;
	env.force_host_cached = workarounds.force_host_cached;
	if (!granite_buffer_create(&env, create_info.domain, create_info.size, create_info.usage,
	                           &buffer, &allocation, &memory_type))
		return BufferHandle(nullptr);

	auto tmpinfo = create_info;
	tmpinfo.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	BufferHandle handle = wrap_buffer(buffer, allocation, tmpinfo);
	if (!handle)
		return handle;

	if (create_info.domain == BufferDomain::Device && (initial || zero_initialize) && !memory_type_is_host_visible(memory_type))
	{
		CommandBufferHandle cmd;
		if (initial)
		{
			auto staging_info = create_info;
			staging_info.domain = BufferDomain::Host;
			auto staging_buffer = create_buffer(staging_info, initial);
			set_name(*staging_buffer, "buffer-upload-staging-buffer");

			cmd = request_command_buffer(CommandBuffer::Type::AsyncTransfer);
			cmd->begin_region("copy-buffer-staging");
			cmd->copy_buffer(*handle, *staging_buffer);
			cmd->end_region();
		}
		else
		{
			cmd = request_command_buffer(CommandBuffer::Type::AsyncCompute);
			cmd->begin_region("fill-buffer-staging");
			cmd->fill_buffer(*handle, 0);
			cmd->end_region();
		}

		LOCK();
		submit_staging(cmd, tmpinfo.usage, true);
	}
	else if (initial || zero_initialize)
	{
		void *ptr = managers.memory.map_memory(allocation, MEMORY_ACCESS_WRITE_BIT, 0, allocation.get_size());
		if (!ptr)
			return BufferHandle(nullptr);

		if (initial)
			memcpy(ptr, initial, create_info.size);
		else
			memset(ptr, 0, create_info.size);
		managers.memory.unmap_memory(allocation, MEMORY_ACCESS_WRITE_BIT, 0, allocation.get_size());
	}
	return handle;
}

bool Device::memory_type_is_host_visible(uint32_t type) const
{
	return (mem_props.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
}

static VkFormatFeatureFlags2KHR promote_storage_usage(const DeviceFeatures &features, VkFormat format,
                                                      VkFormatFeatureFlags2KHR supported)
{
	if ((supported & VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT_KHR) != 0 &&
	    format_supports_storage_image_read_write_without_format(format))
	{
		if (features.enabled_features.shaderStorageImageReadWithoutFormat)
			supported |= VK_FORMAT_FEATURE_2_STORAGE_READ_WITHOUT_FORMAT_BIT_KHR;
		if (features.enabled_features.shaderStorageImageWriteWithoutFormat)
			supported |= VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT_KHR;
	}

	return supported;
}

void Device::get_format_properties(VkFormat format, VkFormatProperties3KHR *properties3) const
{
	VkFormatProperties2 properties2 = { VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2 };
	VK_ASSERT(properties3->sType == VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3_KHR);

	if (ext.supports_format_feature_flags2)
	{
		properties2.pNext = properties3;
		vkGetPhysicalDeviceFormatProperties2(gpu, format, &properties2);
	}
	else
	{
		// Skip properties3 and synthesize the results instead.
		properties2.pNext = properties3->pNext;
		vkGetPhysicalDeviceFormatProperties2(gpu, format, &properties2);

		properties3->optimalTilingFeatures = properties2.formatProperties.optimalTilingFeatures;
		properties3->linearTilingFeatures = properties2.formatProperties.linearTilingFeatures;
		properties3->bufferFeatures = properties2.formatProperties.bufferFeatures;

		// Automatically promote for supported formats.
		properties3->optimalTilingFeatures =
				promote_storage_usage(ext, format, properties3->optimalTilingFeatures);
		properties3->linearTilingFeatures =
				promote_storage_usage(ext, format, properties3->linearTilingFeatures);
	}
}

bool Device::image_format_is_supported(VkFormat format, VkFormatFeatureFlags2KHR required, VkImageTiling tiling) const
{
	VkFormatProperties3KHR props3 = { VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3_KHR };
	get_format_properties(format, &props3);
	auto flags = tiling == VK_IMAGE_TILING_OPTIMAL ? props3.optimalTilingFeatures : props3.linearTilingFeatures;
	return (flags & required) == required;
}

static granite_attachment_desc_t attachment_desc(const ImageView &view)
{
	const Image &image = view.get_image();
	granite_attachment_desc_t desc;
	desc.format = view.get_format();
	desc.samples = image.get_create_info().samples;
	// (no image is lazily allocated)
	desc.transient = false;
	desc.general = image.get_layout_type() != Layout::Optimal;
	return desc;
}

const RenderPass &Device::request_render_pass(const RenderPassInfo &info, bool compatible)
{
	granite_render_pass_desc_t desc;
	memset(&desc, 0, sizeof(desc));
	desc.num_color = info.num_color_attachments;
	for (unsigned i = 0; i < info.num_color_attachments; i++)
	{
		VK_ASSERT(info.color_attachments[i]);
		desc.color[i] = attachment_desc(*info.color_attachments[i]);
	}
	if (info.depth_stencil)
		desc.depth_stencil = attachment_desc(*info.depth_stencil);
	desc.op_flags = info.op_flags;
	desc.clear_attachments = info.clear_attachments;
	desc.load_attachments = info.load_attachments;
	desc.store_attachments = info.store_attachments;
	return request_render_pass(desc, compatible);
}

const RenderPass &Device::request_render_pass(const granite_render_pass_desc_t &desc, bool compatible)
{
	auto hash = granite_render_pass_hash(&desc, compatible);
	auto *ret = render_passes.find(hash);
	if (!ret)
		ret = render_passes.emplace_yield(hash, hash, this, desc);
	return *ret;
}

RenderPass::RenderPass(Hash hash, Device *device, const granite_render_pass_desc_t &desc)
	: IntrusiveHashMapEnabled<RenderPass>(hash)
{
	granite_render_pass_init(&impl, device->get_device(), &device->get_device_table(), &desc);
}

// The device's owner thread only: the allocator takes no lock.
const Framebuffer &Device::request_framebuffer(const RenderPassInfo &info)
{
	auto &rp = request_render_pass(info, true);
	VkImageView views[VULKAN_NUM_ATTACHMENTS + 1];
	unsigned num_views = 0;
	uint32_t width = UINT32_MAX;
	uint32_t height = UINT32_MAX;

	Hasher h;
	h.u64(rp.get_hash());
	for (unsigned i = 0; i < info.num_color_attachments; i++)
		h.u64(info.color_attachments[i]->get_cookie());
	if (info.depth_stencil)
		h.u64(info.depth_stencil->get_cookie());
	// (the base layer, when there were layers)
	h.u32(0);

	auto hash = h.get();
	auto *node = granite_framebuffer_allocator_find(&framebuffer_allocator, hash);
	if (node)
		return *node;

	for (unsigned i = 0; i <= info.num_color_attachments; i++)
	{
		auto *view = i < info.num_color_attachments ? info.color_attachments[i] : info.depth_stencil;
		if (!view)
			continue;
		views[num_views++] = view->get_render_target_view(0);
		width = Util::min_of(width, view->get_view_width());
		height = Util::min_of(height, view->get_view_height());
	}

	// (the allocator is made before the device has its handles)
	framebuffer_allocator.device = device;
	framebuffer_allocator.table = table;
	return *granite_framebuffer_allocator_create(&framebuffer_allocator, hash, rp.get_render_pass(), &rp,
	                                             views, num_views, width, height);
}

unsigned Device::get_num_frame_contexts() const
{
	return unsigned(per_frame.size());
}

void Device::set_queue_lock(QueueLockCallback lock_callback, QueueLockCallback unlock_callback, void *userdata)
{
	queue_lock_callback = lock_callback;
	queue_unlock_callback = unlock_callback;
	queue_lock_userdata = userdata;
}

void Device::set_name(const Buffer &buffer, const char *name)
{
	if (ext.supports_debug_utils)
	{
		VkDebugUtilsObjectNameInfoEXT info = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
		info.objectType = VK_OBJECT_TYPE_BUFFER;
		info.objectHandle = (uint64_t)buffer.get_buffer();
		info.pObjectName = name;
		if (vkSetDebugUtilsObjectNameEXT)
			vkSetDebugUtilsObjectNameEXT(device, &info);
	}
}

void Device::set_name(const Image &image, const char *name)
{
	if (ext.supports_debug_utils)
	{
		VkDebugUtilsObjectNameInfoEXT info = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
		info.objectType = VK_OBJECT_TYPE_IMAGE;
		info.objectHandle = (uint64_t)image.get_image();
		info.pObjectName = name;
		if (vkSetDebugUtilsObjectNameEXT)
			vkSetDebugUtilsObjectNameEXT(device, &info);
	}
}

void Device::set_name(const CommandBuffer &cmd, const char *name)
{
	if (ext.supports_debug_utils)
	{
		VkDebugUtilsObjectNameInfoEXT info = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
		info.objectType = VK_OBJECT_TYPE_COMMAND_BUFFER;
		info.objectHandle = (uint64_t)cmd.get_command_buffer();
		info.pObjectName = name;
		if (vkSetDebugUtilsObjectNameEXT)
			vkSetDebugUtilsObjectNameEXT(device, &info);
	}
}

void Device::report_checkpoints()
{
	if (!ext.supports_nv_device_diagnostic_checkpoints)
		return;

	for (int i = 0; i < QUEUE_INDEX_COUNT; i++)
	{
		if (queue_info.queues[i] == VK_NULL_HANDLE)
			continue;

		uint32_t count;
		table->vkGetQueueCheckpointDataNV(queue_info.queues[i], &count, nullptr);
		Util::Vector<VkCheckpointDataNV> checkpoint_data(count);
		for (auto &data : checkpoint_data)
			data.sType = VK_STRUCTURE_TYPE_CHECKPOINT_DATA_NV;
		table->vkGetQueueCheckpointDataNV(queue_info.queues[i], &count, checkpoint_data.data());

		if (!checkpoint_data.empty())
		{
			LOGI("Checkpoints for %s queue:\n", queue_name_table[i]);
			for (auto &d : checkpoint_data)
				LOGI("Stage %u:\n%s\n", d.stage, static_cast<const char *>(d.pCheckpointMarker));
		}
	}
}

const VolkDeviceTable &Device::get_device_table() const
{
	return *table;
}

bool Device::supports_subgroup_size_log2(bool subgroup_full_group, uint8_t subgroup_minimum_size_log2,
                                         uint8_t subgroup_maximum_size_log2) const
{
	if (ImplementationQuirks::get().force_no_subgroup_size_control)
		return false;

	if (!ext.subgroup_size_control_features.subgroupSizeControl)
		return false;
	if (subgroup_full_group && !ext.subgroup_size_control_features.computeFullSubgroups)
		return false;

	uint32_t min_subgroups = 1u << subgroup_minimum_size_log2;
	uint32_t max_subgroups = 1u << subgroup_maximum_size_log2;

	bool full_range = min_subgroups <= ext.subgroup_size_control_properties.minSubgroupSize &&
	                  max_subgroups >= ext.subgroup_size_control_properties.maxSubgroupSize;

	// We can use VARYING size.
	if (full_range)
		return true;

	if (min_subgroups > ext.subgroup_size_control_properties.maxSubgroupSize ||
	    max_subgroups < ext.subgroup_size_control_properties.minSubgroupSize)
	{
		// No overlap in requested subgroup size and available subgroup size.
		return false;
	}

	// We need requiredSubgroupSizeStages support here.
	return (ext.subgroup_size_control_properties.requiredSubgroupSizeStages & VK_SHADER_STAGE_COMPUTE_BIT) != 0;
}

const QueueInfo &Device::get_queue_info() const
{
	return queue_info;
}
static ImplementationQuirks implementation_quirks;
ImplementationQuirks &ImplementationQuirks::get()
{
	return implementation_quirks;
}
}
