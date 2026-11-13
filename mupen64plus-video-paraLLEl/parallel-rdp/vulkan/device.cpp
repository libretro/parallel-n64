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
	for (auto &stack : deferred_releases)
		mpsc_stack_init(&stack);
	pipeline_records = static_cast<pipeline_record_t *>(
			calloc(PIPELINE_RECORDS, sizeof(pipeline_record_t)));
}

void Device::add_wait_semaphore(CommandBuffer::Type type, Semaphore semaphore, VkPipelineStageFlags stages, bool flush)
{
	VK_ASSERT(!semaphore->is_proxy_timeline());

	LOCK();
	add_wait_semaphore_nolock(get_physical_queue_type(type), Util::move(semaphore), stages, flush);
}

void Device::add_wait_semaphore_nolock(QueueIndices physical_type, Semaphore semaphore, VkPipelineStageFlags stages,
                                       bool flush)
{
	VK_ASSERT(stages != 0);
	if (flush)
		flush_frame(physical_type);
	auto &data = queue_data[physical_type];

#ifdef VULKAN_DEBUG
	for (auto &sem : data.wait_semaphores)
		VK_ASSERT(sem.get() != semaphore.get());
#endif

	semaphore->set_pending_wait();
	data.wait_semaphores.push_back(semaphore);
	data.wait_stages.push_back(stages);
	data.need_fence = true;

	// Sanity check.
	VK_ASSERT(data.wait_semaphores.size() < 16 * 1024);
}

void *Device::map_host_buffer(const Buffer &buffer, MemoryAccessFlags access)
{
	void *host = managers.memory.map_memory(buffer.get_allocation(), access, 0, buffer.get_create_info().size);
	return host;
}

void Device::unmap_host_buffer(const Buffer &buffer, MemoryAccessFlags access)
{
	managers.memory.unmap_memory(buffer.get_allocation(), access, 0, buffer.get_create_info().size);
}

void *Device::map_host_buffer(const Buffer &buffer, MemoryAccessFlags access, VkDeviceSize offset, VkDeviceSize length)
{
	VK_ASSERT(offset + length <= buffer.get_create_info().size);
	void *host = managers.memory.map_memory(buffer.get_allocation(), access, offset, length);
	return host;
}

void Device::unmap_host_buffer(const Buffer &buffer, MemoryAccessFlags access, VkDeviceSize offset, VkDeviceSize length)
{
	VK_ASSERT(offset + length <= buffer.get_create_info().size);
	managers.memory.unmap_memory(buffer.get_allocation(), access, offset, length);
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

bool Device::init_pipeline_cache(const uint8_t *data, size_t size)
{
	static const auto uuid_size = sizeof(gpu_props.pipelineCacheUUID);
	static const auto hash_size = sizeof(Util::Hash);

	VkPipelineCacheCreateInfo info = { VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
	if (!data || size < uuid_size + hash_size)
	{
		LOGI("Creating a fresh pipeline cache.\n");
	}
	else if (memcmp(data, gpu_props.pipelineCacheUUID, uuid_size) != 0)
	{
		LOGI("Pipeline cache UUID changed.\n");
	}
	else
	{
		Util::Hash reference_hash;
		memcpy(&reference_hash, data + uuid_size, sizeof(reference_hash));

		info.initialDataSize = size - uuid_size - hash_size;
		data += uuid_size + hash_size;
		info.pInitialData = data;

		Util::Hasher h;
		h.data(data, info.initialDataSize);

		if (h.get() == reference_hash)
			LOGI("Initializing pipeline cache.\n");
		else
		{
			LOGW("Pipeline cache is corrupt, creating a fresh cache.\n");
			info.pInitialData = nullptr;
			info.initialDataSize = 0;
		}
	}

	if (pipeline_cache != VK_NULL_HANDLE)
		table->vkDestroyPipelineCache(device, pipeline_cache, nullptr);
	pipeline_cache = VK_NULL_HANDLE;
	return table->vkCreatePipelineCache(device, &info, nullptr, &pipeline_cache) == VK_SUCCESS;
}

void Device::init_pipeline_cache()
{
}

size_t Device::get_pipeline_cache_size()
{
	if (pipeline_cache == VK_NULL_HANDLE)
		return 0;

	static const auto uuid_size = sizeof(gpu_props.pipelineCacheUUID);
	static const auto hash_size = sizeof(Util::Hash);
	size_t size = 0;
	if (table->vkGetPipelineCacheData(device, pipeline_cache, &size, nullptr) != VK_SUCCESS)
	{
		LOGE("Failed to get pipeline cache data.\n");
		return 0;
	}

	return size + uuid_size + hash_size;
}

bool Device::get_pipeline_cache_data(uint8_t *data, size_t size)
{
	if (pipeline_cache == VK_NULL_HANDLE)
		return false;

	static const auto uuid_size = sizeof(gpu_props.pipelineCacheUUID);
	static const auto hash_size = sizeof(Util::Hash);
	if (size < uuid_size + hash_size)
		return false;

	auto *hash_data = data + uuid_size;

	size -= uuid_size + hash_size;
	memcpy(data, gpu_props.pipelineCacheUUID, uuid_size);
	data = hash_data + hash_size;

	if (table->vkGetPipelineCacheData(device, pipeline_cache, &size, data) != VK_SUCCESS)
	{
		LOGE("Failed to get pipeline cache data.\n");
		return false;
	}

	Util::Hasher h;
	h.data(data, size);
	auto blob_hash = h.get();
	memcpy(hash_data, &blob_hash, sizeof(blob_hash));

	return true;
}

static_assert(PIPELINE_RECORD_ATTACHMENTS == VULKAN_NUM_ATTACHMENTS, "record attachments");
static_assert(PIPELINE_RECORD_SPEC_CONSTANTS == VULKAN_NUM_TOTAL_SPEC_CONSTANTS, "record spec constants");
static_assert(PIPELINE_RECORD_VERTEX_ATTRIBS == VULKAN_NUM_VERTEX_ATTRIBS, "record vertex attributes");
static_assert(PIPELINE_RECORD_VERTEX_BUFFERS == VULKAN_NUM_VERTEX_BUFFERS, "record vertex buffers");

void Device::record_pipeline(const DeferredPipelineCompile &compile, uint32_t kind)
{
	pipeline_record_t *r;
	const Shader *shader;
	unsigned i;
	int slot;

	/* A pass that cannot be made again is not one to build ahead for. */
	if (kind == PIPELINE_RECORD_GRAPHICS && !compile.compatible_render_pass->get_signature().recordable)
		return;
	slot = retro_atomic_fetch_add_int(&pipeline_records_claimed, 1);
	if (!pipeline_records || slot < 0 || slot >= int(PIPELINE_RECORDS))
		return;
	r = &pipeline_records[slot];
	r->kind          = kind;
	r->pipeline_hash = compile.hash;
	r->program_hash  = compile.program->get_hash();
	if (kind == PIPELINE_RECORD_COMPUTE)
	{
		shader = compile.program->get_shader(ShaderStage::Compute);
		r->shader_hashes[0] = shader ? shader->get_hash() : 0;
	}
	else
	{
		shader = compile.program->get_shader(ShaderStage::Vertex);
		r->shader_hashes[0] = shader ? shader->get_hash() : 0;
		shader = compile.program->get_shader(ShaderStage::Fragment);
		r->shader_hashes[1] = shader ? shader->get_hash() : 0;
	}
	for (i = 0; i < 4; i++)
		r->static_words[i] = compile.static_state.words[i];
	for (i = 0; i < VULKAN_NUM_TOTAL_SPEC_CONSTANTS; i++)
		r->spec_constants[i] = compile.potential_static_state.spec_constants[i];
	r->spec_masks        = compile.potential_static_state.spec_constant_mask |
	                       (uint32_t(compile.potential_static_state.internal_spec_constant_mask) << 8);
	r->subgroup_size_tag = compile.subgroup_size_tag;
	if (kind == PIPELINE_RECORD_GRAPHICS)
	{
		r->render_pass_hash = compile.compatible_render_pass->get_hash();
		r->subpass_index    = compile.subpass_index;
		memcpy(r->blend_constants, compile.potential_static_state.blend_constants,
		       sizeof(r->blend_constants));
		for (i = 0; i < VULKAN_NUM_VERTEX_ATTRIBS; i++)
		{
			r->attribs[i][0] = compile.attribs[i].binding;
			r->attribs[i][1] = uint32_t(compile.attribs[i].format);
			r->attribs[i][2] = compile.attribs[i].offset;
		}
		for (i = 0; i < VULKAN_NUM_VERTEX_BUFFERS; i++)
		{
			r->strides[i]     = uint32_t(compile.strides[i]);
			r->input_rates[i] = uint32_t(compile.input_rates[i]);
		}
		r->pass = compile.compatible_render_pass->get_signature();
	}
	retro_atomic_fetch_add_int(&pipeline_records_written, 1);
}

size_t Device::get_pipeline_records(const pipeline_record_t **records) const
{
	int n = retro_atomic_load_acquire_int(&pipeline_records_written);
	*records = pipeline_records;
	if (!pipeline_records || n <= 0)
		return 0;
	return n < int(PIPELINE_RECORDS) ? size_t(n) : size_t(PIPELINE_RECORDS);
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
	unsigned i;
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

	memset(&compile, 0, sizeof(compile));
	compile.program = program;
	for (i = 0; i < 4; i++)
		compile.static_state.words[i] = record.static_words[i];
	for (i = 0; i < VULKAN_NUM_TOTAL_SPEC_CONSTANTS; i++)
		compile.potential_static_state.spec_constants[i] = record.spec_constants[i];
	compile.potential_static_state.spec_constant_mask          = uint8_t(record.spec_masks & 0xff);
	compile.potential_static_state.internal_spec_constant_mask = uint8_t((record.spec_masks >> 8) & 0xff);
	compile.subgroup_size_tag = record.subgroup_size_tag;
	compile.cache             = pipeline_cache;
	if (!graphics)
	{
		CommandBuffer::update_hash_compute_pipeline(compile);
		return compile.hash == record.pipeline_hash;
	}

	compile.compatible_render_pass = pass;
	compile.subpass_index          = record.subpass_index;
	memcpy(compile.potential_static_state.blend_constants, record.blend_constants,
	       sizeof(compile.potential_static_state.blend_constants));
	for (i = 0; i < VULKAN_NUM_VERTEX_ATTRIBS; i++)
	{
		compile.attribs[i].binding = record.attribs[i][0];
		compile.attribs[i].format  = VkFormat(record.attribs[i][1]);
		compile.attribs[i].offset  = record.attribs[i][2];
	}
	for (i = 0; i < VULKAN_NUM_VERTEX_BUFFERS; i++)
	{
		compile.strides[i]     = record.strides[i];
		compile.input_rates[i] = VkVertexInputRate(record.input_rates[i]);
	}
	{
		uint32_t active_vbos;
		CommandBuffer::update_hash_graphics_pipeline(compile, active_vbos);
	}
	return compile.hash == record.pipeline_hash;
}

void Device::flush_pipeline_cache()
{
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
	init_pipeline_cache();

	init_timeline_semaphores();

	init_frame_contexts(2); // By default, regular double buffer between CPU and GPU.

	managers.memory.init(this);
	granite_semaphore_pool_init(&managers.semaphore, device, table);
	granite_fence_pool_init(&managers.fence, device, table);
	managers.vbo.init(this, 4 * 1024, 16, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
	                  ImplementationQuirks::get().staging_need_device_local);
	managers.ubo.init(this, 256 * 1024, Util::max_of<VkDeviceSize>(16u, gpu_props.limits.minUniformBufferOffsetAlignment),
	                  VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
	                  ImplementationQuirks::get().staging_need_device_local);
	managers.ubo.set_spill_region_size(VULKAN_MAX_UBO_SIZE);

	managers.vbo.set_max_retained_blocks(256);
	managers.ubo.set_max_retained_blocks(64);
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

void Device::init_stock_sampler(StockSampler mode, float max_aniso, float lod_bias)
{
	SamplerCreateInfo info = {};
	info.max_lod = VK_LOD_CLAMP_NONE;
	info.max_anisotropy = 1.0f;

	switch (mode)
	{
	case StockSampler::NearestShadow:
	case StockSampler::LinearShadow:
		info.compare_enable = true;
		info.compare_op = VK_COMPARE_OP_LESS_OR_EQUAL;
		break;

	default:
		info.compare_enable = false;
		break;
	}

	switch (mode)
	{
	case StockSampler::TrilinearClamp:
	case StockSampler::TrilinearWrap:
	case StockSampler::DefaultGeometryFilterWrap:
	case StockSampler::DefaultGeometryFilterClamp:
		info.mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		break;

	default:
		info.mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		break;
	}

	switch (mode)
	{
	case StockSampler::DefaultGeometryFilterClamp:
	case StockSampler::DefaultGeometryFilterWrap:
	case StockSampler::LinearClamp:
	case StockSampler::LinearWrap:
	case StockSampler::TrilinearClamp:
	case StockSampler::TrilinearWrap:
	case StockSampler::LinearShadow:
		info.mag_filter = VK_FILTER_LINEAR;
		info.min_filter = VK_FILTER_LINEAR;
		break;

	default:
		info.mag_filter = VK_FILTER_NEAREST;
		info.min_filter = VK_FILTER_NEAREST;
		break;
	}

	switch (mode)
	{
	default:
	case StockSampler::DefaultGeometryFilterWrap:
	case StockSampler::LinearWrap:
	case StockSampler::NearestWrap:
	case StockSampler::TrilinearWrap:
		info.address_mode_u = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		info.address_mode_v = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		info.address_mode_w = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		break;

	case StockSampler::DefaultGeometryFilterClamp:
	case StockSampler::LinearClamp:
	case StockSampler::NearestClamp:
	case StockSampler::TrilinearClamp:
	case StockSampler::NearestShadow:
	case StockSampler::LinearShadow:
		info.address_mode_u = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		info.address_mode_v = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		info.address_mode_w = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		break;
	}

	switch (mode)
	{
	case StockSampler::DefaultGeometryFilterWrap:
	case StockSampler::DefaultGeometryFilterClamp:
		if (get_device_features().enabled_features.samplerAnisotropy)
		{
			info.anisotropy_enable = true;
			info.max_anisotropy = Util::min_of(max_aniso, get_gpu_properties().limits.maxSamplerAnisotropy);
		}
		info.mip_lod_bias = lod_bias;
		break;

	default:
		break;
	}

	samplers[unsigned(mode)] = request_immutable_sampler(info);
}

void Device::init_stock_samplers()
{
	for (unsigned i = 0; i < static_cast<unsigned>(StockSampler::Count); i++)
	{
		auto mode = static_cast<StockSampler>(i);
		init_stock_sampler(mode, 8.0f, 0.0f);
	}
}

static void request_block(Device &device, BufferBlock &block, VkDeviceSize size,
                          BufferPool &pool, Util::Vector<BufferBlock> *dma, Util::Vector<BufferBlock> &recycle)
{
	if (block.mapped)
		device.unmap_host_buffer(*block.cpu, MEMORY_ACCESS_WRITE_BIT);

	if (block.offset == 0)
	{
		if (block.size == pool.get_block_size())
			pool.recycle_block(block);
	}
	else
	{
		if (block.cpu != block.gpu)
		{
			VK_ASSERT(dma);
			dma->push_back(block);
		}

		if (block.size == pool.get_block_size())
			recycle.push_back(block);
	}

	if (size)
		block = pool.request_block(size);
	else
		block = {};
}

void Device::request_vertex_block(BufferBlock &block, VkDeviceSize size)
{
	LOCK();
	request_vertex_block_nolock(block, size);
}

void Device::request_vertex_block_nolock(BufferBlock &block, VkDeviceSize size)
{
	request_block(*this, block, size, managers.vbo, &dma.vbo, frame().vbo_blocks);
}

void Device::request_uniform_block(BufferBlock &block, VkDeviceSize size)
{
	LOCK();
	request_uniform_block_nolock(block, size);
}

void Device::request_uniform_block_nolock(BufferBlock &block, VkDeviceSize size)
{
	request_block(*this, block, size, managers.ubo, &dma.ubo, frame().ubo_blocks);
}

void Device::submit(CommandBufferHandle &cmd, Fence *fence, unsigned semaphore_count, Semaphore *semaphores)
{
	cmd->end_debug_channel();

	LOCK();
	submit_nolock(Util::move(cmd), fence, semaphore_count, semaphores);
}

void Device::submit_discard_nolock(CommandBufferHandle &cmd)
{
	cmd.reset();
	decrement_frame_counter_nolock();
}

QueueIndices Device::get_physical_queue_type(CommandBuffer::Type queue_type) const
{
	if (queue_type != CommandBuffer::Type::AsyncGraphics)
	{
		// Enums match.
		return QueueIndices(queue_type);
	}
	else
	{
		if (queue_info.family_indices[QUEUE_INDEX_GRAPHICS] == queue_info.family_indices[QUEUE_INDEX_COMPUTE] &&
		    queue_info.queues[QUEUE_INDEX_GRAPHICS] != queue_info.queues[QUEUE_INDEX_COMPUTE])
		{
			return QUEUE_INDEX_COMPUTE;
		}
		else
		{
			return QUEUE_INDEX_GRAPHICS;
		}
	}
}

void Device::submit_nolock(CommandBufferHandle cmd, Fence *fence, unsigned semaphore_count, Semaphore *semaphores)
{
	auto type = cmd->get_command_buffer_type();
	auto physical_type = get_physical_queue_type(type);
	auto &submissions = frame().submissions[physical_type];

	cmd->end();
	submissions.push_back(Util::move(cmd));

	InternalFence signalled_fence;

	if (fence || semaphore_count)
	{
		submit_queue(physical_type, fence ? &signalled_fence : nullptr,
		             nullptr,
		             semaphore_count, semaphores);
	}

	if (fence)
	{
		VK_ASSERT(!*fence);
		if (signalled_fence.value)
			*fence = Fence(handle_pool.fences.allocate(this, signalled_fence.value, signalled_fence.timeline));
		else
			*fence = Fence(handle_pool.fences.allocate(this, signalled_fence.fence));
	}

	decrement_frame_counter_nolock();
}

void Device::submit_empty(CommandBuffer::Type type, Fence *fence, SemaphoreHolder *semaphore)
{
	VK_ASSERT(!semaphore || !semaphore->is_proxy_timeline());
	LOCK();
	submit_empty_nolock(get_physical_queue_type(type), fence, semaphore);
}

void Device::submit_empty_nolock(QueueIndices physical_type, Fence *fence,
                                 SemaphoreHolder *semaphore)
{
	if (physical_type != QUEUE_INDEX_TRANSFER)
		flush_frame(QUEUE_INDEX_TRANSFER);

	InternalFence signalled_fence;

	submit_queue(physical_type, fence ? &signalled_fence : nullptr, semaphore,
	             0, nullptr);

	if (fence)
	{
		if (signalled_fence.value)
			*fence = Fence(handle_pool.fences.allocate(this, signalled_fence.value, signalled_fence.timeline));
		else
			*fence = Fence(handle_pool.fences.allocate(this, signalled_fence.fence));
	}
}

void Device::submit_empty_inner(QueueIndices physical_type, InternalFence *fence,
                                SemaphoreHolder *external_semaphore,
                                unsigned semaphore_count, Semaphore *semaphores)
{
	auto &data = queue_data[physical_type];
	VkSemaphore timeline_semaphore = data.timeline_semaphore;
	uint64_t timeline_value = ++data.current_timeline;
	VkQueue queue = queue_info.queues[physical_type];
	frame().timeline_fences[physical_type] = data.current_timeline;

	// Add external wait semaphores.
	granite_submit_composer_begin(&composer, get_workarounds().split_binary_timeline_semaphores);
	collect_wait_semaphores(data);

	emit_queue_signals(external_semaphore,
	                   timeline_semaphore, timeline_value,
	                   fence, semaphore_count, semaphores);

	VkFence cleared_fence = fence && !ext.timeline_semaphore_features.timelineSemaphore ?
	                        granite_fence_pool_request(&managers.fence) :
	                        VK_NULL_HANDLE;
	if (fence)
		fence->fence = cleared_fence;

	auto result = submit_batches(queue, cleared_fence);

	if (result != VK_SUCCESS)
		LOGE("vkQueueSubmit failed (code: %d).\n", int(result));
	if (result == VK_ERROR_DEVICE_LOST)
		report_checkpoints();

	if (!ext.timeline_semaphore_features.timelineSemaphore)
		data.need_fence = true;
}

void Device::submit_initialization(CommandBufferHandle &cmd, VkBufferUsageFlags usage)
{
	LOCK();
	submit_staging(cmd, usage, true);
}

void Device::submit_staging(CommandBufferHandle &cmd, VkBufferUsageFlags usage, bool flush)
{
	auto access = buffer_usage_to_possible_access(usage);
	auto stages = buffer_usage_to_possible_stages(usage);
	VkQueue src_queue = queue_info.queues[get_physical_queue_type(cmd->get_command_buffer_type())];

	if (src_queue == queue_info.queues[QUEUE_INDEX_GRAPHICS] && src_queue == queue_info.queues[QUEUE_INDEX_COMPUTE])
	{
		// For single-queue systems, just use a pipeline barrier.
		cmd->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, stages, access);
		submit_nolock(cmd, nullptr, 0, nullptr);
	}
	else
	{
		auto compute_stages = stages &
		                      (VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
		                       VK_PIPELINE_STAGE_TRANSFER_BIT |
		                       VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT);

		auto compute_access = access &
		                      (VK_ACCESS_SHADER_READ_BIT |
		                       VK_ACCESS_SHADER_WRITE_BIT |
		                       VK_ACCESS_TRANSFER_READ_BIT |
		                       VK_ACCESS_UNIFORM_READ_BIT |
		                       VK_ACCESS_TRANSFER_WRITE_BIT |
		                       VK_ACCESS_INDIRECT_COMMAND_READ_BIT);

		auto graphics_stages = stages;

		if (src_queue == queue_info.queues[QUEUE_INDEX_GRAPHICS])
		{
			cmd->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
			             graphics_stages, access);

			if (compute_stages != 0)
			{
				Semaphore sem;
				submit_nolock(cmd, nullptr, 1, &sem);
				add_wait_semaphore_nolock(QUEUE_INDEX_COMPUTE, sem, compute_stages, flush);
			}
			else
				submit_nolock(cmd, nullptr, 0, nullptr);
		}
		else if (src_queue == queue_info.queues[QUEUE_INDEX_COMPUTE])
		{
			cmd->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
			             compute_stages, compute_access);

			if (graphics_stages != 0)
			{
				Semaphore sem;
				submit_nolock(cmd, nullptr, 1, &sem);
				add_wait_semaphore_nolock(QUEUE_INDEX_GRAPHICS, sem, graphics_stages, flush);
			}
			else
				submit_nolock(cmd, nullptr, 0, nullptr);
		}
		else
		{
			if (graphics_stages != 0 && compute_stages != 0)
			{
				Semaphore semaphores[2];
				submit_nolock(cmd, nullptr, 2, semaphores);
				add_wait_semaphore_nolock(QUEUE_INDEX_GRAPHICS, semaphores[0], graphics_stages, flush);
				add_wait_semaphore_nolock(QUEUE_INDEX_COMPUTE, semaphores[1], compute_stages, flush);
			}
			else if (graphics_stages != 0)
			{
				Semaphore sem;
				submit_nolock(cmd, nullptr, 1, &sem);
				add_wait_semaphore_nolock(QUEUE_INDEX_GRAPHICS, sem, graphics_stages, flush);
			}
			else if (compute_stages != 0)
			{
				Semaphore sem;
				submit_nolock(cmd, nullptr, 1, &sem);
				add_wait_semaphore_nolock(QUEUE_INDEX_COMPUTE, sem, compute_stages, flush);
			}
			else
				submit_nolock(cmd, nullptr, 0, nullptr);
		}
	}
}

// The semaphores the queue's next submission waits for go to the composer:
// the binary ones, then the timeline ones.
void Device::collect_wait_semaphores(QueueData &data)
{
	for (unsigned pass = 0; pass < 2; pass++)
	{
		for (size_t i = 0, n = data.wait_semaphores.size(); i < n; i++)
		{
			auto &semaphore = data.wait_semaphores[i];
			bool timeline = semaphore->get_semaphore_type() == VK_SEMAPHORE_TYPE_TIMELINE_KHR;
			if (timeline != (pass == 1))
				continue;

			auto vk_semaphore = semaphore->consume();
			if (timeline)
			{
				granite_submit_composer_add_wait(&composer, vk_semaphore, data.wait_stages[i],
				                                 semaphore->get_timeline_value());
			}
			else
			{
				if (semaphore->is_external_object_compatible())
					granite_frame_garbage_push(&frame().garbage, GRANITE_GARBAGE_SEMAPHORE, GRANITE_VK_TO_BITS(vk_semaphore));
				else
					granite_frame_garbage_push(&frame().garbage, GRANITE_GARBAGE_RECYCLE_SEMAPHORE, GRANITE_VK_TO_BITS(vk_semaphore));

				granite_submit_composer_add_wait(&composer, vk_semaphore, data.wait_stages[i], 0);
			}
		}
	}

	data.wait_stages.clear();
	data.wait_semaphores.clear();
}

void Device::emit_queue_signals(SemaphoreHolder *external_semaphore,
                                VkSemaphore sem, uint64_t timeline, InternalFence *fence,
                                unsigned semaphore_count, Semaphore *semaphores)
{
	if (external_semaphore)
	{
		VK_ASSERT(!external_semaphore->is_signalled());
		VK_ASSERT(!external_semaphore->is_proxy_timeline());
		VK_ASSERT(external_semaphore->get_semaphore());
		external_semaphore->signal_external();
		granite_submit_composer_add_signal(&composer, external_semaphore->get_semaphore(),
		                                   external_semaphore->get_semaphore_type() == VK_SEMAPHORE_TYPE_TIMELINE_KHR ?
		                                   external_semaphore->get_timeline_value() : 0);

		// Make sure we observe that the external semaphore is signalled before fences are signalled.
		granite_submit_composer_begin_batch(&composer);
	}

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
			semaphores[i] = Semaphore(handle_pool.semaphores.allocate(this, timeline, sem, false));
			semaphores[i]->signal_external();
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
			semaphores[i] = Semaphore(handle_pool.semaphores.allocate(this, cleared_semaphore, true, true));
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

void Device::submit_queue(QueueIndices physical_type, InternalFence *fence,
                          SemaphoreHolder *external_semaphore,
                          unsigned semaphore_count, Semaphore *semaphores)
{
	// Always check if we need to flush pending transfers.
	if (physical_type != QUEUE_INDEX_TRANSFER)
		flush_frame(QUEUE_INDEX_TRANSFER);

	auto &data = queue_data[physical_type];
	auto &submissions = frame().submissions[physical_type];

	if (submissions.empty())
	{
		if (fence || semaphore_count)
			submit_empty_inner(physical_type, fence, external_semaphore, semaphore_count, semaphores);
		return;
	}

	VkSemaphore timeline_semaphore = data.timeline_semaphore;
	uint64_t timeline_value = ++data.current_timeline;

	VkQueue queue = queue_info.queues[physical_type];
	frame().timeline_fences[physical_type] = data.current_timeline;

	granite_submit_composer_begin(&composer, workarounds.split_binary_timeline_semaphores);
	collect_wait_semaphores(data);

	for (auto &cmd : submissions)
		granite_submit_composer_add_command_buffer(&composer, cmd->get_command_buffer());

	VkFence cleared_fence = fence && !ext.timeline_semaphore_features.timelineSemaphore ?
	                        granite_fence_pool_request(&managers.fence) :
	                        VK_NULL_HANDLE;

	if (fence)
		fence->fence = cleared_fence;

	emit_queue_signals(external_semaphore, timeline_semaphore, timeline_value,
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

	if (physical_type == QUEUE_INDEX_TRANSFER)
		sync_buffer_blocks();
	submit_queue(physical_type, nullptr);
}

void Device::sync_buffer_blocks()
{
	if (dma.vbo.empty() && dma.ubo.empty())
		return;

	VkBufferUsageFlags usage = 0;

	auto cmd = request_command_buffer_nolock(get_thread_index(), CommandBuffer::Type::AsyncTransfer);

	cmd->begin_region("buffer-block-sync");

	for (auto &block : dma.vbo)
	{
		VK_ASSERT(block.offset != 0);
		cmd->copy_buffer(*block.gpu, 0, *block.cpu, 0, block.offset);
		usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	}

	for (auto &block : dma.ubo)
	{
		VK_ASSERT(block.offset != 0);
		cmd->copy_buffer(*block.gpu, 0, *block.cpu, 0, block.offset);
		usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	}

	dma.vbo.clear();
	dma.ubo.clear();

	cmd->end_region();

	// Do not flush graphics or compute in this context.
	// We must be able to inject semaphores into all currently enqueued graphics / compute.
	submit_staging(cmd, usage, false);
}

void Device::end_frame_nolock()
{
	// Kept handles alive until end-of-frame, free now if appropriate.
	for (auto &image : frame().keep_alive_images)
	{
		image->set_internal_sync_object();
		image->get_view().set_internal_sync_object();
	}
	frame().keep_alive_images.clear();

	// Make sure we have a fence which covers all submissions in the frame.
	InternalFence fence;

	for (auto &i : queue_flush_order)
	{
		if (queue_data[i].need_fence || !frame().submissions[i].empty())
		{
			submit_queue(i, &fence);
			if (fence.fence != VK_NULL_HANDLE)
			{
				frame().wait_fences.push_back(fence.fence);
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
	auto physical_type = get_physical_queue_type(type);
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
	return samplers[static_cast<unsigned>(sampler)]->get_sampler();
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
		flush_pipeline_cache();
		table->vkDestroyPipelineCache(device, pipeline_cache, nullptr);
	}



	free(pipeline_records);
	pipeline_records = nullptr;

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
	{
		for (auto &fence : frame->timeline_fences)
			fence = 0;
		for (auto &timeline : frame->timeline_semaphores)
			timeline = VK_NULL_HANDLE;
	}
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
	for (int i = 0; i < QUEUE_INDEX_COUNT; i++)
	{
		timeline_semaphores[i] = device.queue_data[i].timeline_semaphore;
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
	auto *node = static_cast<DeferredRelease *>(
			mpsc_stack_drain(&deferred_releases[f.frame_index % MAX_DEFERRED_FRAMES]));
	while (node)
	{
		auto *next = static_cast<DeferredRelease *>(node->next);
		f.allocations.push_back(node->alloc);
		handle_pool.deferred.free(node);
		node = next;
	}
}

void Device::free_memory_nolock(const DeviceAllocation &alloc)
{
	frame().allocations.push_back(alloc);
}

void Device::destroy_pipeline(VkPipeline pipeline)
{
	release_handle(GRANITE_GARBAGE_PIPELINE, GRANITE_VK_TO_BITS(pipeline));
}

void Device::reset_fence(VkFence fence, bool observed_wait)
{
	// Reset and recycled when this frame comes round again, whether or
	// not someone already waited on it.
	(void)observed_wait;
	release_handle(GRANITE_GARBAGE_RECYCLE_FENCE, GRANITE_VK_TO_BITS(fence));
}

void Device::destroy_buffer(VkBuffer buffer)
{
	release_handle(GRANITE_GARBAGE_BUFFER, GRANITE_VK_TO_BITS(buffer));
}

void Device::destroy_buffer_view(VkBufferView view)
{
	release_handle(GRANITE_GARBAGE_BUFFER_VIEW, GRANITE_VK_TO_BITS(view));
}

void Device::destroy_image(VkImage image)
{
	release_handle(GRANITE_GARBAGE_IMAGE, GRANITE_VK_TO_BITS(image));
}

void Device::destroy_semaphore(VkSemaphore semaphore)
{
	release_handle(GRANITE_GARBAGE_SEMAPHORE, GRANITE_VK_TO_BITS(semaphore));
}

void Device::recycle_semaphore(VkSemaphore semaphore)
{
	release_handle(GRANITE_GARBAGE_RECYCLE_SEMAPHORE, GRANITE_VK_TO_BITS(semaphore));
}

void Device::free_memory(const DeviceAllocation &alloc)
{
	auto *node = handle_pool.deferred.allocate();
	node->alloc = alloc;
	mpsc_stack_push(&deferred_releases[granite_release_queue_frame(&releases.q) % MAX_DEFERRED_FRAMES], node);
}

void Device::destroy_sampler(VkSampler sampler)
{
	release_handle(GRANITE_GARBAGE_SAMPLER, GRANITE_VK_TO_BITS(sampler));
}

void Device::destroy_image_view(VkImageView view)
{
	release_handle(GRANITE_GARBAGE_IMAGE_VIEW, GRANITE_VK_TO_BITS(view));
}

void Device::destroy_image_view_nolock(VkImageView view)
{
	VK_ASSERT(!granite_frame_garbage_has(&frame().garbage, GRANITE_GARBAGE_IMAGE_VIEW, GRANITE_VK_TO_BITS(view)));
	granite_frame_garbage_push(&frame().garbage, GRANITE_GARBAGE_IMAGE_VIEW, GRANITE_VK_TO_BITS(view));
}

void Device::destroy_buffer_view_nolock(VkBufferView view)
{
	VK_ASSERT(!granite_frame_garbage_has(&frame().garbage, GRANITE_GARBAGE_BUFFER_VIEW, GRANITE_VK_TO_BITS(view)));
	granite_frame_garbage_push(&frame().garbage, GRANITE_GARBAGE_BUFFER_VIEW, GRANITE_VK_TO_BITS(view));
}

void Device::destroy_semaphore_nolock(VkSemaphore semaphore)
{
	VK_ASSERT(!granite_frame_garbage_has(&frame().garbage, GRANITE_GARBAGE_SEMAPHORE, GRANITE_VK_TO_BITS(semaphore)));
	granite_frame_garbage_push(&frame().garbage, GRANITE_GARBAGE_SEMAPHORE, GRANITE_VK_TO_BITS(semaphore));
}

void Device::recycle_semaphore_nolock(VkSemaphore semaphore)
{
	VK_ASSERT(!granite_frame_garbage_has(&frame().garbage, GRANITE_GARBAGE_RECYCLE_SEMAPHORE, GRANITE_VK_TO_BITS(semaphore)));
	granite_frame_garbage_push(&frame().garbage, GRANITE_GARBAGE_RECYCLE_SEMAPHORE, GRANITE_VK_TO_BITS(semaphore));
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

void Device::destroy_image_nolock(VkImage image)
{
	VK_ASSERT(!granite_frame_garbage_has(&frame().garbage, GRANITE_GARBAGE_IMAGE, GRANITE_VK_TO_BITS(image)));
	granite_frame_garbage_push(&frame().garbage, GRANITE_GARBAGE_IMAGE, GRANITE_VK_TO_BITS(image));
}

void Device::destroy_buffer_nolock(VkBuffer buffer)
{
	VK_ASSERT(!granite_frame_garbage_has(&frame().garbage, GRANITE_GARBAGE_BUFFER, GRANITE_VK_TO_BITS(buffer)));
	granite_frame_garbage_push(&frame().garbage, GRANITE_GARBAGE_BUFFER, GRANITE_VK_TO_BITS(buffer));
}

void Device::destroy_sampler_nolock(VkSampler sampler)
{
	VK_ASSERT(!granite_frame_garbage_has(&frame().garbage, GRANITE_GARBAGE_SAMPLER, GRANITE_VK_TO_BITS(sampler)));
	granite_frame_garbage_push(&frame().garbage, GRANITE_GARBAGE_SAMPLER, GRANITE_VK_TO_BITS(sampler));
}

void Device::destroy_framebuffer_nolock(VkFramebuffer framebuffer)
{
	VK_ASSERT(!granite_frame_garbage_has(&frame().garbage, GRANITE_GARBAGE_FRAMEBUFFER, GRANITE_VK_TO_BITS(framebuffer)));
	granite_frame_garbage_push(&frame().garbage, GRANITE_GARBAGE_FRAMEBUFFER, GRANITE_VK_TO_BITS(framebuffer));
}

void Device::clear_wait_semaphores()
{
	for (auto &data : queue_data)
	{
		for (auto &sem : data.wait_semaphores)
			table->vkDestroySemaphore(device, sem->consume(), nullptr);
		data.wait_semaphores.clear();
		data.wait_stages.clear();
	}
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
	managers.vbo.reset();
	managers.ubo.reset();
	for (auto &frame : per_frame)
	{
		frame->vbo_blocks.clear();
		frame->ubo_blocks.clear();
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
		frame->wait_fences.clear();
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

	bool has_timeline = true;
	for (auto &sem : timeline_semaphores)
	{
		if (sem == VK_NULL_HANDLE)
		{
			has_timeline = false;
			break;
		}
	}

	if (device.get_device_features().timeline_semaphore_features.timelineSemaphore && has_timeline)
	{
		VkSemaphoreWaitInfoKHR info = { VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO_KHR };
		VkSemaphore sems[QUEUE_INDEX_COUNT];
		uint64_t values[QUEUE_INDEX_COUNT];
		for (int i = 0; i < QUEUE_INDEX_COUNT; i++)
		{
			if (timeline_fences[i])
			{
				sems[info.semaphoreCount] = timeline_semaphores[i];
				values[info.semaphoreCount] = timeline_fences[i];
				info.semaphoreCount++;
			}
		}

		if (info.semaphoreCount)
		{
			info.pSemaphores = sems;
			info.pValues = values;
			table.vkWaitSemaphoresKHR(vkdevice, &info, UINT64_MAX);
		}
	}

	// If we're using timeline semaphores, these paths should never be hit.
	if (!wait_fences.empty())
	{
		table.vkWaitForFences(vkdevice, wait_fences.size(), wait_fences.data(), VK_TRUE, UINT64_MAX);
		wait_fences.clear();
	}

	// This frame's GPU work is done: take what other threads released to it.
	device.drain_deferred_releases(*this);

	for (auto &cmd_pool : cmd_pools)
		for (auto &pool : cmd_pool)
			granite_command_pool_begin(&pool);

	for (auto &channel : debug_channels)
		device.parse_debug_channel(channel);

	// Free the debug channel buffers here, and they will immediately be recycled with the frame's garbage right below.
	debug_channels.clear();

	for (auto &block : vbo_blocks)
		managers.vbo.recycle_block(block);
	for (auto &block : ubo_blocks)
		managers.ubo.recycle_block(block);
	vbo_blocks.clear();
	ubo_blocks.clear();

	granite_frame_garbage_collect(&garbage, vkdevice, &table, &managers.semaphore, &managers.fence);

	if (!allocations.empty())
	{
		for (auto &alloc : allocations)
			alloc.free_immediate(managers.memory);
	}

	allocations.clear();
}

Device::PerFrame::~PerFrame()
{
	in_destructor = true;
	begin();
	for (auto &cmd_pool : cmd_pools)
		for (auto &pool : cmd_pool)
			granite_command_pool_free(&pool);
	granite_frame_garbage_free(&garbage);
}

uint32_t Device::find_memory_type(BufferDomain domain, uint32_t mask) const
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

	return BufferViewHandle(handle_pool.buffer_views.allocate(this, view, view_info));
}

ImageViewHandle Device::create_image_view(const ImageViewCreateInfo &create_info)
{
	auto &image_create_info = create_info.image->get_create_info();

	granite_image_view_desc_t desc = {};
	desc.image = create_info.image->get_image();
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

	ImageViewCreateInfo tmp = create_info;
	tmp.format = desc.format;
	ImageViewHandle ret(handle_pool.image_views.allocate(this, views.view, tmp));
	if (!ret)
	{
		granite_image_views_destroy(device, table, &views);
		return ImageViewHandle(nullptr);
	}
	ret->set_views(views);
	return ret;
}

InitialImageBuffer Device::create_image_staging_buffer(const ImageCreateInfo &info, const ImageInitialData *initial)
{
	InitialImageBuffer result;

	granite_texture_layout_t layout;
	unsigned copy_levels = info.levels;

	if (info.type != VK_IMAGE_TYPE_1D && info.type != VK_IMAGE_TYPE_2D && info.type != VK_IMAGE_TYPE_3D)
		return {};
	granite_texture_layout_set(&layout, info.type, info.format, info.width, info.height, info.depth,
	                           info.layers, copy_levels);
	// (no more levels than a layout has)
	copy_levels = layout.mip_levels;

	BufferCreateInfo buffer_info = {};
	buffer_info.domain = BufferDomain::Host;
	buffer_info.size = layout.required_size;
	buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	result.buffer = create_buffer(buffer_info, nullptr);
	set_name(*result.buffer, "image-upload-staging-buffer");

	// And now, do the actual copy.
	auto *mapped = static_cast<uint8_t *>(map_host_buffer(*result.buffer, MEMORY_ACCESS_WRITE_BIT));
	unsigned index = 0;

	layout.buffer = mapped;

	for (unsigned level = 0; level < copy_levels; level++)
	{
		const auto &mip_info = layout.mips[level];
		uint32_t dst_height_stride = granite_texture_layout_layer_size(&layout, level);
		size_t row_size = granite_texture_layout_row_size(&layout, level);

		for (unsigned layer = 0; layer < info.layers; layer++, index++)
		{
			uint32_t src_row_length =
					initial[index].row_length ? initial[index].row_length : mip_info.row_length;
			uint32_t src_array_height =
					initial[index].image_height ? initial[index].image_height : mip_info.image_height;

			uint32_t src_row_stride = granite_texture_layout_row_stride(&layout, src_row_length);
			uint32_t src_height_stride = granite_texture_layout_layer_stride(&layout, src_array_height, src_row_stride);

			uint8_t *dst = granite_texture_layout_data(&layout, layer, level);
			const uint8_t *src = static_cast<const uint8_t *>(initial[index].data);

			for (uint32_t z = 0; z < mip_info.depth; z++)
				for (uint32_t y = 0; y < mip_info.block_image_height; y++)
					memcpy(dst + z * dst_height_stride + y * row_size, src + z * src_height_stride + y * src_row_stride, row_size);
		}
	}

	unmap_host_buffer(*result.buffer, MEMORY_ACCESS_WRITE_BIT);
	result.blits.resize(layout.mip_levels);
	granite_texture_layout_copies(&layout, format_to_aspect_mask(info.format), result.blits.data());
	return result;
}

ImageHandle Device::create_image(const ImageCreateInfo &create_info, const ImageInitialData *initial)
{
	if (initial)
	{
		auto staging_buffer = create_image_staging_buffer(create_info, initial);
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

	ImageHandle handle(handle_pool.images.allocate(this, image, views.view, allocation, tmpinfo, view_type));
	if (!handle)
	{
		granite_image_views_destroy(device, table, &views);
		table->vkDestroyImage(device, image, nullptr);
		LOCK_MEMORY();
		allocation.free_immediate(managers.memory);
		return ImageHandle(nullptr);
	}

	if (has_view)
		handle->get_view().set_views(views);

	// Set possible dstStage and dstAccess.
	handle->set_stage_flags(granite_image_usage_stages(usage));
	handle->set_access_flags(granite_image_usage_access(usage));

	bool share_compute = desc.on_compute &&
	                     queue_info.queues[QUEUE_INDEX_GRAPHICS] != queue_info.queues[QUEUE_INDEX_COMPUTE];

	CommandBufferHandle transition_cmd;

	// Copy initial data to texture.
	if (staging_buffer)
	{
		VK_ASSERT(create_info.initial_layout != VK_IMAGE_LAYOUT_UNDEFINED);

		// If queue_info.graphics != queue_info.transfer, we will use a semaphore, so no srcAccess mask is necessary.
		VkAccessFlags final_transition_src_access = 0;
		if (queue_info.queues[QUEUE_INDEX_GRAPHICS] == queue_info.queues[QUEUE_INDEX_TRANSFER])
			final_transition_src_access = VK_ACCESS_TRANSFER_WRITE_BIT;

		bool need_initial_barrier = true;

		// For concurrent queue mode, we just need to inject a semaphore.
		// For non-concurrent queue mode, we will have to inject ownership transfer barrier if the queue families do not match.

		auto graphics_cmd = request_command_buffer(CommandBuffer::Type::Generic);
		CommandBufferHandle transfer_cmd;

		// Don't split the upload into multiple command buffers unless we have to.
		if (queue_info.queues[QUEUE_INDEX_TRANSFER] != queue_info.queues[QUEUE_INDEX_GRAPHICS])
			transfer_cmd = request_command_buffer(CommandBuffer::Type::AsyncTransfer);
		else
			transfer_cmd = graphics_cmd;

		transfer_cmd->image_barrier(*handle, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT,
		                            VK_ACCESS_TRANSFER_WRITE_BIT);

		transfer_cmd->begin_region("copy-image-to-gpu");
		transfer_cmd->copy_buffer_to_image(*handle, *staging_buffer->buffer, staging_buffer->blits.size(), staging_buffer->blits.data());
		transfer_cmd->end_region();

		if (queue_info.queues[QUEUE_INDEX_TRANSFER] != queue_info.queues[QUEUE_INDEX_GRAPHICS])
		{
			VkPipelineStageFlags dst_stages = handle->get_stage_flags();

			// We can't just use semaphores, we will also need a release + acquire barrier to marshal ownership from
			// transfer queue over to graphics ...
			if (!concurrent_queue && queue_info.family_indices[QUEUE_INDEX_TRANSFER] != queue_info.family_indices[QUEUE_INDEX_GRAPHICS])
			{
				VkImageMemoryBarrier release = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
				release.image = handle->get_image();
				release.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				release.dstAccessMask = 0;
				release.srcQueueFamilyIndex = queue_info.family_indices[QUEUE_INDEX_TRANSFER];
				release.dstQueueFamilyIndex = queue_info.family_indices[QUEUE_INDEX_GRAPHICS];
				release.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

				release.newLayout = create_info.initial_layout;
				release.subresourceRange.levelCount = create_info.levels;
				need_initial_barrier = false;

				release.subresourceRange.aspectMask = format_to_aspect_mask(create_info.format);
				release.subresourceRange.layerCount = create_info.layers;

				VkImageMemoryBarrier acquire = release;
				acquire.srcAccessMask = 0;

				acquire.dstAccessMask = handle->get_access_flags() & granite_image_layout_access(create_info.initial_layout);

				transfer_cmd->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
				                      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
				                      0, nullptr, 0, nullptr, 1, &release);

				graphics_cmd->barrier(dst_stages,
				                      dst_stages,
				                      0, nullptr, 0, nullptr, 1, &acquire);
			}

			Semaphore sem;
			submit(transfer_cmd, nullptr, 1, &sem);
			add_wait_semaphore(CommandBuffer::Type::Generic, sem, dst_stages, true);
		}

		if (need_initial_barrier)
		{
			graphics_cmd->image_barrier(
					*handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					create_info.initial_layout,
					VK_PIPELINE_STAGE_TRANSFER_BIT, final_transition_src_access,
					handle->get_stage_flags(),
					handle->get_access_flags() & granite_image_layout_access(create_info.initial_layout));
		}

		transition_cmd = Util::move(graphics_cmd);
	}
	else if (create_info.initial_layout != VK_IMAGE_LAYOUT_UNDEFINED)
	{
		auto cmd = request_command_buffer(CommandBuffer::Type::Generic);
		cmd->image_barrier(*handle, VK_IMAGE_LAYOUT_UNDEFINED, create_info.initial_layout,
		                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, handle->get_stage_flags(),
		                   handle->get_access_flags() &
		                   granite_image_layout_access(create_info.initial_layout));
		transition_cmd = Util::move(cmd);
	}

	// For concurrent queue, make sure that compute can see the final image as well.
	// Also add semaphore if the compute queue can be used for async graphics as well.
	if (transition_cmd)
	{
		if (share_compute)
		{
			Semaphore sem;
			submit(transition_cmd, nullptr, 1, &sem);
			VkPipelineStageFlags dst_stages = handle->get_stage_flags();
			if (queue_info.family_indices[QUEUE_INDEX_GRAPHICS] != queue_info.family_indices[QUEUE_INDEX_COMPUTE])
				dst_stages &= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
			add_wait_semaphore(CommandBuffer::Type::AsyncCompute, sem, dst_stages, true);
		}
		else
		{
			LOCK();
			submit_nolock(transition_cmd, nullptr, 0, nullptr);
			if (concurrent_queue)
				flush_frame(QUEUE_INDEX_GRAPHICS);
		}
	}

	return handle;
}

const ImmutableSampler *Device::request_immutable_sampler(const SamplerCreateInfo &sampler_info)
{
	auto info = Sampler::fill_vk_sampler_info(sampler_info);
	Util::Hasher h;

	h.u32(info.flags);
	h.u32(info.addressModeU);
	h.u32(info.addressModeV);
	h.u32(info.addressModeW);
	h.u32(info.minFilter);
	h.u32(info.magFilter);
	h.u32(info.mipmapMode);
	h.f32(info.minLod);
	h.f32(info.maxLod);
	h.f32(info.mipLodBias);
	h.u32(info.compareEnable);
	h.u32(info.compareOp);
	h.u32(info.anisotropyEnable);
	h.f32(info.maxAnisotropy);
	h.u32(info.borderColor);
	h.u32(info.unnormalizedCoordinates);
	h.u32(0);

	auto *sampler = immutable_samplers.find(h.get());
	if (!sampler)
		sampler = immutable_samplers.emplace_yield(h.get(), h.get(), this, sampler_info);
	return sampler;
}

BufferHandle Device::create_imported_host_buffer(const BufferCreateInfo &create_info, VkExternalMemoryHandleTypeFlagBits type, void *host_buffer)
{
	if (create_info.domain != BufferDomain::Host &&
	    create_info.domain != BufferDomain::CachedHost &&
	    create_info.domain != BufferDomain::CachedCoherentHostPreferCached &&
	    create_info.domain != BufferDomain::CachedCoherentHostPreferCoherent)
	{
		return BufferHandle{};
	}

	if (!ext.supports_external_memory_host)
		return BufferHandle{};

	if ((reinterpret_cast<uintptr_t>(host_buffer) & (ext.host_memory_properties.minImportedHostPointerAlignment - 1)) != 0)
	{
		LOGE("Host buffer is not aligned appropriately.\n");
		return BufferHandle{};
	}

	VkExternalMemoryBufferCreateInfo external_info = { VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO };
	external_info.handleTypes = type;

	VkMemoryHostPointerPropertiesEXT host_pointer_props = { VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT };
	if (table->vkGetMemoryHostPointerPropertiesEXT(device, type, host_buffer, &host_pointer_props) != VK_SUCCESS)
	{
		LOGE("Host pointer is not importable.\n");
		return BufferHandle{};
	}

	VkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	info.size = create_info.size;
	info.usage = create_info.usage;
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	info.pNext = &external_info;

	uint32_t sharing_indices[QUEUE_INDEX_COUNT];
	info.queueFamilyIndexCount = granite_unique_queue_families(queue_info.family_indices, QUEUE_INDEX_COUNT,
	                                                           sharing_indices);
	if (info.queueFamilyIndexCount > 1)
	{
		info.sharingMode = VK_SHARING_MODE_CONCURRENT;
		info.pQueueFamilyIndices = sharing_indices;
	}
	else
		info.queueFamilyIndexCount = 0;

	VkBuffer buffer;
	VkMemoryRequirements reqs;
	if (table->vkCreateBuffer(device, &info, nullptr, &buffer) != VK_SUCCESS)
		return BufferHandle{};

	table->vkGetBufferMemoryRequirements(device, buffer, &reqs);

	// Weird workaround for latest AMD Windows drivers which sets memoryTypeBits to 0 when using the external handle type.
	if (!reqs.memoryTypeBits)
		reqs.memoryTypeBits = ~0u;

	auto plain_reqs = reqs;
	reqs.memoryTypeBits &= host_pointer_props.memoryTypeBits;

	if (reqs.memoryTypeBits == 0)
	{
		LOGE("No compatible host pointer types are available.\n");
		table->vkDestroyBuffer(device, buffer, nullptr);
		return BufferHandle{};
	}

	uint32_t memory_type = find_memory_type(create_info.domain, reqs.memoryTypeBits);

	if (memory_type == UINT32_MAX)
	{
		// Weird workaround for Intel Windows where the only memory type is DEVICE_LOCAL
		// with no HOST_VISIBLE (!?!?!).
		// However, it appears to work just fine to allocate with other memory types as well ...
		// Oh well.

		// Ignore host_pointer_props.
		reqs = plain_reqs;
		memory_type = find_memory_type(create_info.domain, reqs.memoryTypeBits);
	}

	if (memory_type == UINT32_MAX)
	{
		LOGE("Failed to find memory type.\n");
		table->vkDestroyBuffer(device, buffer, nullptr);
		return BufferHandle{};
	}

	VkMemoryAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	alloc_info.allocationSize = (create_info.size + ext.host_memory_properties.minImportedHostPointerAlignment - 1) &
	                            ~(ext.host_memory_properties.minImportedHostPointerAlignment - 1);
	alloc_info.memoryTypeIndex = memory_type;

	VkImportMemoryHostPointerInfoEXT import = { VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT };
	import.handleType = type;
	import.pHostPointer = host_buffer;
	alloc_info.pNext = &import;

	VkDeviceMemory memory;
	if (table->vkAllocateMemory(device, &alloc_info, nullptr, &memory) != VK_SUCCESS)
	{
		table->vkDestroyBuffer(device, buffer, nullptr);
		return BufferHandle{};
	}

	auto allocation = DeviceAllocation::make_imported_allocation(memory, info.size, memory_type);
	if (table->vkMapMemory(device, memory, 0, VK_WHOLE_SIZE, 0, reinterpret_cast<void **>(&allocation.host_base)) != VK_SUCCESS)
	{
		{
			LOCK_MEMORY();
			allocation.free_immediate(managers.memory);
		}
		table->vkDestroyBuffer(device, buffer, nullptr);
		return BufferHandle{};
	}

	if (table->vkBindBufferMemory(device, buffer, memory, 0) != VK_SUCCESS)
	{
		{
			LOCK_MEMORY();
			allocation.free_immediate(managers.memory);
		}
		table->vkDestroyBuffer(device, buffer, nullptr);
		return BufferHandle{};
	}

	BufferHandle handle(handle_pool.buffers.allocate(this, buffer, allocation, create_info));
	return handle;
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
	if (!granite_buffer_create(&env, granite_buffer_domain_t(create_info.domain), create_info.size, create_info.usage,
	                           &buffer, &allocation, &memory_type))
		return BufferHandle(nullptr);

	auto tmpinfo = create_info;
	tmpinfo.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	BufferHandle handle(handle_pool.buffers.allocate(this, buffer, allocation, tmpinfo));

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

void Device::add_debug_channel_buffer(DebugChannelInterface *iface, const char *tag, Vulkan::BufferHandle buffer)
{
	buffer->set_internal_sync_object();
	LOCK();
	frame().debug_channels.emplace_back();
	auto &channel = frame().debug_channels.back();
	channel.iface = iface;
	Util::copy_string(channel.tag, sizeof(channel.tag), tag);
	channel.buffer = Util::move(buffer);
}

void Device::parse_debug_channel(const PerFrame::DebugChannel &channel)
{
	if (!channel.iface)
		return;

	auto *words = static_cast<const DebugChannelInterface::Word *>(map_host_buffer(*channel.buffer, MEMORY_ACCESS_READ_BIT));

	size_t size = channel.buffer->get_create_info().size;
	if (size <= sizeof(uint32_t))
	{
		LOGE("Debug channel buffer is too small.\n");
		return;
	}

	// Format for the debug channel.
	// Word 0: Atomic counter used by shader.
	// Word 1-*: [total message length, code, x, y, z, args]

	size -= sizeof(uint32_t);
	size /= sizeof(uint32_t);

	if (words[0].u32 > size)
	{
		LOGW("Debug channel overflowed and messaged were dropped. Consider increasing debug channel size to at least %u bytes.\n",
		     unsigned((words[0].u32 + 1) * sizeof(uint32_t)));
	}

	words++;

	while (size != 0 && words[0].u32 >= 5 && words[0].u32 <= size)
	{
		channel.iface->message(channel.iface->user, channel.tag, words[1].u32,
		                       words[2].u32, words[3].u32, words[4].u32,
		                       words[0].u32 - 5, &words[5]);
		size -= words[0].u32;
		words += words[0].u32;
	}

	unmap_host_buffer(*channel.buffer, MEMORY_ACCESS_READ_BIT);
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
