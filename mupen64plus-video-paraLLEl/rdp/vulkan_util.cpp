#include "vulkan_util.hpp"
#include "common.hpp"
#include <assert.h>

#include <future>
#include <time.h>

#ifndef UINT64_MAX
#define UINT64_MAX		(__UINT64_C(18446744073709551615))
#endif

using namespace std;

namespace Vulkan
{
using namespace Internal;

Device::Device(const VulkanContext &context_, unsigned frames)
    : context(context_)
    , cached_allocator(context_, BufferLocation::CachedHost)
    , device_allocator(context_, BufferLocation::Device)
    , cached_shared_allocator(context_, BufferLocation::CachedHostShared)
    , shared_allocator(context_, BufferLocation::DeviceShared)
    , memory_allocator(context_)
{
	VkPipelineCacheCreateInfo cache = { VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
	V(vkCreatePipelineCache(context.get_device(), &cache, nullptr, &pipeline_cache));

	init_rdp_pipelines();
	init_blit_pipelines();
	init_per_frame(frames);
}

void Device::set_num_frames(unsigned num_frames)
{
   deinit_per_frame();
   init_per_frame(num_frames);
}

void Device::init_per_frame(unsigned num_frames)
{
   per_frame.resize(num_frames);

   VkCommandPoolCreateInfo info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
   for (auto &frame : per_frame)
   {
      unsigned i;

      info.queueFamilyIndex = context.get_queue_family();
      info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
      V(vkCreateCommandPool(context.get_device(), &info, nullptr, &frame.command_pool.pool));

      info.queueFamilyIndex = context.get_alt_queue_family();
      info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
      V(vkCreateCommandPool(context.get_device(), &info, nullptr, &frame.alt_command_pool.pool));

      for (i = 0; i < RDP::DescriptorSetCount; i++)
         frame.descriptor_set_rdp_allocator[i].sizes = rdp.descriptor_pool_sizes[i];
      for (i = 0; i < Blit::DescriptorSetCount; i++)
         frame.descriptor_set_blit_allocator[i].sizes = blit.descriptor_pool_sizes[i];
   }
}

void Device::deinit_per_frame()
{
   vkDeviceWaitIdle(context.get_device());

   for (auto &frame : per_frame)
   {
	   for (auto &defer : frame.defers)
		   defer();

	   auto &fence = frame.fence_allocator;
	   for (auto f : fence.fences)
		   vkDestroyFence(context.get_device(), f, nullptr);

	   auto &semaphore = frame.semaphore_allocator;
	   for (auto s : semaphore.semaphores)
		   vkDestroySemaphore(context.get_device(), s, nullptr);

	   auto &cmd_pool = frame.command_pool;
	   if (!cmd_pool.buffers.empty())
	   {
		   vkFreeCommandBuffers(context.get_device(), cmd_pool.pool, cmd_pool.buffers.size(), cmd_pool.buffers.data());
	   }
	   vkDestroyCommandPool(context.get_device(), cmd_pool.pool, nullptr);

	   auto &alt_cmd_pool = frame.alt_command_pool;
	   if (!alt_cmd_pool.buffers.empty())
	   {
		   vkFreeCommandBuffers(context.get_device(), alt_cmd_pool.pool, alt_cmd_pool.buffers.size(),
				   alt_cmd_pool.buffers.data());
	   }
	   vkDestroyCommandPool(context.get_device(), alt_cmd_pool.pool, nullptr);

	   for (auto &alloc : frame.descriptor_set_rdp_allocator)
		   for (auto &pool : alloc.pools)
			   vkDestroyDescriptorPool(context.get_device(), pool.pool, nullptr);
	   for (auto &alloc : frame.descriptor_set_blit_allocator)
		   for (auto &pool : alloc.pools)
			   vkDestroyDescriptorPool(context.get_device(), pool.pool, nullptr);
   }

   per_frame.clear();
}

Device::~Device()
{
   vkDeviceWaitIdle(context.get_device());

   cached_allocator.current.reset();
   cached_shared_allocator.current.reset();
   device_allocator.current.reset();
   shared_allocator.current.reset();
   memory_allocator.current.reset();

   deinit_per_frame();

   for (auto &pipe : rdp.pipelines)
      vkDestroyPipeline(context.get_device(), pipe, nullptr);
   for (auto &layout : rdp.set_layouts)
      vkDestroyDescriptorSetLayout(context.get_device(), layout, nullptr);
   vkDestroyPipelineLayout(context.get_device(), rdp.layout, nullptr);

   for (auto &pipe : blit.pipelines)
      vkDestroyPipeline(context.get_device(), pipe, nullptr);
   for (auto &layout : blit.set_layouts)
      vkDestroyDescriptorSetLayout(context.get_device(), layout, nullptr);
   vkDestroyPipelineLayout(context.get_device(), blit.layout, nullptr);

   vkDestroyPipelineCache(context.get_device(), pipeline_cache, nullptr);
   vkDestroySampler(context.get_device(), rdp.sampler, nullptr);
}

void Device::begin_index(unsigned index)
{
	auto &frame = per_frame[index];

	// Clear fences
	auto &fences = frame.fence_allocator;
	if (fences.count)
	{
		double current = gettime();
		V(vkWaitForFences(context.get_device(), fences.count, fences.fences.data(), true, UINT64_MAX));
		vkResetFences(context.get_device(), fences.count, fences.fences.data());
		double end = gettime();
#ifdef ENABLE_LOGS
		fprintf(stderr, "Waited for fences for %.3f ms\n", 1000.0 * (end - current));
#endif
	}
	fences.count = 0;

	// Semaphores are implicitly reset upon signal.
	frame.semaphore_allocator.count = 0;

	// Reset command pool
	auto &pool = frame.command_pool;
	V(vkResetCommandPool(context.get_device(), pool.pool, 0));
	pool.count = 0;

	auto &alt_pool = frame.alt_command_pool;
	V(vkResetCommandPool(context.get_device(), alt_pool.pool, 0));
	alt_pool.count = 0;

	// Run deferred calls to cleanup
	for (auto &defer : frame.defers)
		defer();
	frame.defers.clear();
	frame.frame_count++;

	// Descriptor set allocators
	for (auto &alloc : frame.descriptor_set_rdp_allocator)
	{
		for (auto &pool : alloc.pools)
		{
			V(vkResetDescriptorPool(context.get_device(), pool.pool, 0));
			pool.count = 0;
		}
		alloc.count = 0;
	}

	for (auto &alloc : frame.descriptor_set_blit_allocator)
	{
		for (auto &pool : alloc.pools)
		{
			V(vkResetDescriptorPool(context.get_device(), pool.pool, 0));
			pool.count = 0;
		}
		alloc.count = 0;
	}

	current_index = index;
}

CommandBuffer Device::request_command_buffer(CommandPool &pool)
{
   VkCommandBuffer cmd = VK_NULL_HANDLE;
   if (pool.count < pool.buffers.size())
      cmd = pool.buffers[pool.count++];
   else
   {
      VkCommandBufferAllocateInfo info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
      info.commandPool = pool.pool;
      info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      info.commandBufferCount = 1;
      V(vkAllocateCommandBuffers(context.get_device(), &info, &cmd));
#ifdef ENABLE_LOGS
      fprintf(stderr, "ALLOCATING COMMAND BUFFER\n");
#endif

      pool.buffers.push_back(cmd);
      pool.count++;
   }

   VkCommandBufferInheritanceInfo inherit = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO };
   VkCommandBufferBeginInfo info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };

   info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
   info.pInheritanceInfo = &inherit;
   V(vkBeginCommandBuffer(cmd, &info));
   return { cmd, VK_NULL_HANDLE };
}

CommandBuffer Device::request_command_buffer()
{
   auto &pool = per_frame[current_index].command_pool;
   return request_command_buffer(pool);
}

CommandBuffer Device::request_alt_command_buffer()
{
   auto &pool = per_frame[current_index].alt_command_pool;
   return request_command_buffer(pool);
}

Semaphore Device::request_semaphore()
{
   auto &frame = per_frame[current_index];
   auto &semaphores = frame.semaphore_allocator;
   if (semaphores.count < semaphores.semaphores.size())
      return { semaphores.semaphores[semaphores.count++] };
   else
   {
      VkSemaphoreCreateInfo info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
      VkSemaphore sem;
      V(vkCreateSemaphore(context.get_device(), &info, nullptr, &sem));
      semaphores.semaphores.push_back(sem);
      semaphores.count++;
      return { sem };
   }
}

Fence Device::submit(const CommandBuffer &cmd, const Semaphore *wait_semaphore, const Semaphore *signal_semaphore)
{
   return submit(context.get_queue(), cmd, wait_semaphore, signal_semaphore);
}

Fence Device::submit_alt_queue(const CommandBuffer &cmd, const Semaphore *wait_semaphore,
                               const Semaphore *signal_semaphore)
{
   return submit(context.get_alt_queue(), cmd, wait_semaphore, signal_semaphore);
}

Fence Device::submit(VkQueue queue, const CommandBuffer &cmd, const Semaphore *wait_semaphore,
                     const Semaphore *signal_semaphore)
{
   V(vkEndCommandBuffer(cmd.cmd));

   auto &frame = per_frame[current_index];
   auto &fences = frame.fence_allocator;
   VkFence fence = VK_NULL_HANDLE;

   if (fences.count < fences.fences.size())
      fence = fences.fences[fences.count++];
   else
   {
      VkFenceCreateInfo info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
      V(vkCreateFence(context.get_device(), &info, nullptr, &fence));
      fences.fences.push_back(fence);
      fences.count++;
   }

   VkSubmitInfo info = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
   info.commandBufferCount = 1;
   info.pCommandBuffers = &cmd.cmd;

   if (wait_semaphore)
   {
      static const VkPipelineStageFlags compute = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
      info.waitSemaphoreCount = 1;
      info.pWaitDstStageMask = &compute;
      info.pWaitSemaphores = &wait_semaphore->semaphore;
   }

   if (signal_semaphore)
   {
      info.signalSemaphoreCount = 1;
      info.pSignalSemaphores = &signal_semaphore->semaphore;
   }

   V(vkQueueSubmit(queue, 1, &info, fence));

   return { fence, frame.frame_count, current_index };
}

Buffer Device::request_buffer(BufferType type, size_t size)
{
   Buffer buffer;
   buffer.type = type;

   switch (type)
   {
      case BufferType::Staging:
         buffer.staging = allocate_block(cached_allocator, size);
	 break;
      case BufferType::Dynamic:
	 buffer.staging = allocate_block(cached_allocator, size);
	 if (!buffer.staging.block->device_local)
            buffer.device = allocate_block(device_allocator, size);
	 break;
      case BufferType::DynamicShared:
	 buffer.staging = allocate_block(cached_shared_allocator, size);
	 if (!buffer.staging.block->device_local)
            buffer.device = allocate_block(shared_allocator, size);
	 break;
      case BufferType::Device:
	 buffer.device = allocate_block(device_allocator, size);
	 break;
   }

   return buffer;
}

Buffer Device::request_dynamic_buffer(CommandBuffer &cmd, DescriptorSet &set, RDP::BufferLayout binding, size_t size)
{
   Buffer tmp = request_buffer(BufferType::Dynamic, size);
   cmd.sync_buffer_to_gpu(tmp);

   if (binding == RDP::BufferLayout::Combiners)
      set.set_uniform_buffer(static_cast<unsigned>(binding), tmp);
   else
      set.set_storage_buffer(static_cast<unsigned>(binding), tmp);

   tmp.atom_alignment = context.get_gpu_props().limits.nonCoherentAtomSize;
   return tmp;
}

void *Buffer::map()
{
	if (!staging.block->coherent)
	{
		VkMappedMemoryRange range = { VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE };
		range.memory = staging.block->memory;
		range.offset = staging.offset;
		range.size = (staging.size + atom_alignment - 1) & ~(atom_alignment - 1);
		V(vkInvalidateMappedMemoryRanges(staging.block->device, 1, &range));
	}

	return static_cast<uint8_t *>(staging.block->mapped) + staging.offset;
}

void Buffer::unmap()
{
   if (!staging.block->coherent)
   {
      VkMappedMemoryRange range = { VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE };
      range.memory = staging.block->memory;
      range.offset = staging.offset;
      range.size = (staging.size + atom_alignment - 1) & ~(atom_alignment - 1);
      V(vkFlushMappedMemoryRanges(staging.block->device, 1, &range));
   }
}

AllocatedBlock Device::allocate_block(BufferAllocator &alloc, size_t size)
{
	auto &current = alloc.current;
	auto alignment = get_buffer_alignment();

	if (current)
	{
		size_t aligned = (current->offset + alignment - 1) & ~(alignment - 1);
		if (aligned + size > current->size)
		{
			// Exhausted.
			current.reset();
		}
	}

	if (current)
	{
		size_t aligned = (current->offset + alignment - 1) & ~(alignment - 1);
		current->offset = aligned + size;
		return { aligned, size, current };
	}

	// The shared ptr should recycle the memory when ref-counts are released.
	auto BlockDeleter = [this, &alloc](Block *block) {
		auto &frame = per_frame[current_index];

		// Release the memory block when all GPU uses are complete.
		frame.defers.push_back([this, block, &alloc] {
			// Move the owned ptr into the vacant pool.
			alloc.vacant.emplace(block);
		});
	};

	if (!alloc.vacant.empty())
	{
		// Steal the owned ptr and move it into a shared pointer, with custom deleter.
		current = shared_ptr<Block>(alloc.vacant.top().release(), BlockDeleter);
		alloc.vacant.pop();
		current->offset = size;
		return { 0, size, current };
	}

	VkBufferCreateInfo buffer_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	buffer_info.size = MaxBlockSize;
	buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
	                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	// If we're doing a shared allocator, make sure we allocate memory from that pool.
	uint32_t queue_families[2];
	if ((alloc.location == BufferLocation::CachedHostShared || alloc.location == BufferLocation::DeviceShared) &&
	    (context.get_queue_family() != context.get_alt_queue_family()))
	{
		buffer_info.sharingMode = VK_SHARING_MODE_CONCURRENT;
		buffer_info.pQueueFamilyIndices = queue_families;
		buffer_info.queueFamilyIndexCount = 2;

		queue_families[0] = context.get_queue_family();
		queue_families[1] = context.get_alt_queue_family();
	}

	VkBuffer buffer;
	V(vkCreateBuffer(context.get_device(), &buffer_info, nullptr, &buffer));

	VkMemoryRequirements mem_reqs;
	vkGetBufferMemoryRequirements(context.get_device(), buffer, &mem_reqs);

	uint32_t host_req = 0;
	switch (alloc.location)
	{
	case BufferLocation::DeviceShared:
	case BufferLocation::Device:
		host_req = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		break;

	case BufferLocation::CachedHost:
	case BufferLocation::CachedHostShared:
		host_req = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
		break;
	}

	uint32_t type_index = 0;
	if (!find_memory_type(&type_index, mem_reqs.memoryTypeBits, host_req))
	{
		assert(alloc.location == BufferLocation::CachedHost);
		// Guaranteed to be available.
		bool ret = find_memory_type(&type_index, mem_reqs.memoryTypeBits,
		                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		assert(ret);
	}

	// If the memory is not coherent, we have to do manual cache management, but that's fine.
	bool coherent =
	    context.get_mem_props().memoryTypes[type_index].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

	bool device_local =
	    context.get_mem_props().memoryTypes[type_index].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	VkMemoryAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	alloc_info.allocationSize = mem_reqs.size;
	alloc_info.memoryTypeIndex = type_index;

	VkDeviceMemory memory;
#ifdef ENABLE_LOGS
	fprintf(stderr, "ALLOCATING MEMORY!\n");
#endif
	V(vkAllocateMemory(context.get_device(), &alloc_info, nullptr, &memory));
	V(vkBindBufferMemory(context.get_device(), buffer, memory, 0));

	void *mapped = nullptr;
	if (alloc.location == BufferLocation::CachedHost || alloc.location == BufferLocation::CachedHostShared)
	{
		// Persistently map host visible memory.
		V(vkMapMemory(context.get_device(), memory, 0, buffer_info.size, 0, &mapped));
	}

	current = shared_ptr<Block>(
	    new Block{ context.get_device(), buffer, memory, 0, size_t(buffer_info.size), mapped, coherent, device_local },
	    BlockDeleter);
	current->offset = size;
	return { 0, size, current };
}

bool Device::find_memory_type(uint32_t *index, uint32_t device_req, uint32_t host_req)
{
   uint32_t i;

   for (i = 0; i < context.get_mem_props().memoryTypeCount; i++)
   {
      if ((1u << i) & device_req)
      {
         if ((host_req & context.get_mem_props().memoryTypes[i].propertyFlags) == host_req)
	 {
            *index = i;
	    return true;
	 }
      }
   }

   return false;
}

size_t Device::get_buffer_alignment() const
{
   auto &limits = context.get_gpu_props().limits;
   return max(max<size_t>(limits.minUniformBufferOffsetAlignment, limits.minStorageBufferOffsetAlignment),
	max<size_t>(limits.minMemoryMapAlignment, limits.nonCoherentAtomSize));
}

void Device::wait(const Fence &fence)
{
	auto &frame = per_frame[fence.frame_index];

	// Already signalled and reset.
	if (fence.set_on_frame < frame.frame_count)
		return;

	double current = gettime();
	V(vkWaitForFences(context.get_device(), 1, &fence.fence, true, UINT64_MAX));
#ifdef ENABLE_LOGS
	double end = gettime();
	fprintf(stderr, "Waiting for explicit fence: %.3f ms.\n", 1000.0 * (end - current));
#endif
}

ImageHandle Device::create_image_2d(VkFormat format, unsigned width, unsigned height)
{
   return create_image(format, width, height, 1, false, memory_allocator);
}

ImageHandle Device::create_image_2d_array(VkFormat format, unsigned width, unsigned height, unsigned layers)
{
   return create_image(format, width, height, layers, true, memory_allocator);
}

AllocatedMemory Device::allocate_memory(Internal::MemoryAllocator &alloc, const VkMemoryRequirements &mem_reqs)
{
	auto &current = alloc.current;

	if (current)
	{
		size_t aligned = (current->offset + mem_reqs.alignment - 1) & ~(mem_reqs.alignment - 1);
		// Exhausted.
		if (aligned + mem_reqs.size > current->size)
			current.reset();
	}

	if (current)
	{
		size_t aligned = (current->offset + mem_reqs.alignment - 1) & ~(mem_reqs.alignment - 1);
		current->offset = aligned + mem_reqs.size;
		return { aligned, size_t(mem_reqs.size), current };
	}

	// The shared ptr should recycle the memory when ref-counts are released.
	auto MemoryDeleter = [this, &alloc](Memory *memory) {
		auto &frame = per_frame[current_index];

		// Release the memory block when all GPU uses are complete.
		frame.defers.push_back([this, memory, &alloc] {
			// Move the owned ptr into the vacant pool.
			alloc.vacant.emplace(memory);
		});
	};

	if (!alloc.vacant.empty())
	{
		// Steal the owned ptr and move it into a shared pointer, with custom deleter.
		current = shared_ptr<Memory>(alloc.vacant.top().release(), MemoryDeleter);

		alloc.vacant.pop();
		current->offset = mem_reqs.size;
		return { 0, size_t(mem_reqs.size), current };
	}

	// Allocate new block.
	VkMemoryAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	find_memory_type(&alloc_info.memoryTypeIndex, mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	alloc_info.allocationSize = MaxBlockSize;

	VkDeviceMemory memory;
#ifdef ENABLE_LOGS
	fprintf(stderr, "ALLOCATING MEMORY!\n");
#endif
	V(vkAllocateMemory(context.get_device(), &alloc_info, nullptr, &memory));

	current = shared_ptr<Memory>(new Memory{ context.get_device(), memory, 0, size_t(alloc_info.allocationSize) },
	                             MemoryDeleter);
	current->offset = mem_reqs.size;
	return { 0, size_t(mem_reqs.size), current };
}

ImageHandle Device::create_image(VkFormat format, unsigned width, unsigned height, unsigned layers, bool array,
                                 Internal::MemoryAllocator &mem_allocator)
{
   VkImage image;
   VkImageView view;
   VkMemoryRequirements mem_reqs;
   VkImageCreateInfo info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };

   info.imageType = VK_IMAGE_TYPE_2D;
   info.format = format;
   info.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
   info.extent.width = width;
   info.extent.height = height;
   info.extent.depth = 1;
   info.mipLevels = 1;
   info.arrayLayers = layers;
   info.samples = VK_SAMPLE_COUNT_1_BIT;
   info.tiling = VK_IMAGE_TILING_OPTIMAL;
   info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
   info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
   info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

   V(vkCreateImage(context.get_device(), &info, nullptr, &image));

   vkGetImageMemoryRequirements(context.get_device(), image, &mem_reqs);

   auto block = allocate_memory(mem_allocator, mem_reqs);
   V(vkBindImageMemory(context.get_device(), image, block.memory->memory, block.offset));

   VkImageViewCreateInfo view_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
   view_info.image = image;
   view_info.viewType = array ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
   view_info.format = format;
   view_info.components.r = VK_COMPONENT_SWIZZLE_R;
   view_info.components.g = VK_COMPONENT_SWIZZLE_G;
   view_info.components.b = VK_COMPONENT_SWIZZLE_B;
   view_info.components.a = VK_COMPONENT_SWIZZLE_A;
   view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
   view_info.subresourceRange.levelCount = 1;
   view_info.subresourceRange.layerCount = layers;

   V(vkCreateImageView(context.get_device(), &view_info, nullptr, &view));

   auto ptr = ImageHandle(new Image(*this, image, view, move(block), layers));
   ptr->sampler = rdp.sampler;
   return ptr;
}

void Device::delete_image(Image *image)
{
   auto &frame = per_frame[current_index];

   auto v = image->view;
   auto i = image->image;
   frame.defers.push_back([this, v, i] {
		   vkDestroyImageView(context.get_device(), v, nullptr);
		   vkDestroyImage(context.get_device(), i, nullptr);
		   });
   delete image;
}

void Device::init_blit_pipelines()
{
	// Set #0
	{
		vector<VkDescriptorPoolSize> sizes;
		VkDescriptorSetLayoutCreateInfo info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
		VkDescriptorSetLayoutBinding bindings[Blit::BufferLayoutCount] = {};

		bindings[0].binding = 0;
		bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		bindings[0].descriptorCount = 1;
		bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		bindings[1].binding = 1;
		bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		bindings[1].descriptorCount = 1;
		bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		sizes.push_back({ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, Internal::SetsPerPool });
		sizes.push_back({ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, Internal::SetsPerPool });

		info.bindingCount = Blit::BufferLayoutCount;
		info.pBindings = bindings;

		V(vkCreateDescriptorSetLayout(context.get_device(), &info, nullptr, &blit.set_layouts[0]));
		blit.descriptor_pool_sizes[0] = move(sizes);
	}

	VkPushConstantRange push = {};
	push.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	push.offset = 0;
	push.size = 36; // tmem.comp.

	VkPipelineLayoutCreateInfo info = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	info.setLayoutCount = 1;
	info.pSetLayouts = blit.set_layouts;
	info.pushConstantRangeCount = 1;
	info.pPushConstantRanges = &push;
	V(vkCreatePipelineLayout(context.get_device(), &info, nullptr, &blit.layout));

	VkComputePipelineCreateInfo pipe = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
	pipe.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	pipe.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	pipe.stage.pName = "main";
	pipe.layout = blit.layout;

	static const uint32_t blit_8[] =
#include "glsl/blit.8.inc"
	    ;

	static const uint32_t blit_16[] =
#include "glsl/blit.16.inc"
	    ;

	static const uint32_t blit_32[] =
#include "glsl/blit.32.inc"
	    ;

	static const uint32_t tmem_rgba32[] =
#include "glsl/tmem.rgba32.inc"
	    ;

	static const uint32_t tmem_rgba16[] =
#include "glsl/tmem.rgba16.inc"
	    ;

	static const uint32_t tmem_i8[] =
#include "glsl/tmem.i8.inc"
	    ;

	static const uint32_t tmem_ia8[] =
#include "glsl/tmem.ia8.inc"
	    ;

	static const uint32_t tmem_ia16[] =
#include "glsl/tmem.ia16.inc"
	    ;

	static const uint32_t *shaders[] = {
		blit_8, blit_16, blit_32, tmem_rgba32, tmem_rgba16, tmem_i8, tmem_ia8, tmem_ia16,
	};

	static const size_t shader_sizes[] = {
		sizeof(blit_8),      sizeof(blit_16), sizeof(blit_32),  sizeof(tmem_rgba32),
		sizeof(tmem_rgba16), sizeof(tmem_i8), sizeof(tmem_ia8), sizeof(tmem_ia16),
	};

	for (unsigned i = 0; i < Blit::PipelineCount; i++)
	{
		VkShaderModuleCreateInfo module_info = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
		module_info.codeSize = shader_sizes[i];
		module_info.pCode = reinterpret_cast<const uint32_t *>(shaders[i]);
		V(vkCreateShaderModule(context.get_device(), &module_info, nullptr, &pipe.stage.module));

		V(vkCreateComputePipelines(context.get_device(), pipeline_cache, 1, &pipe, nullptr, &blit.pipelines[i]));
		vkDestroyShaderModule(context.get_device(), pipe.stage.module, nullptr);
	}
}

void Device::init_rdp_pipelines()
{
	// Set #0, LUTs
	{
		vector<VkDescriptorPoolSize> sizes;

		VkDescriptorSetLayoutCreateInfo info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
		VkDescriptorSetLayoutBinding bindings[3] = {};

		for (unsigned i = 0; i < 3; i++)
		{
			VkDescriptorType type =
			    i < 2 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

			bindings[i].binding = i;
			bindings[i].descriptorType = type;
			bindings[i].descriptorCount = 1;
			bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
			sizes.push_back({ type, Internal::SetsPerPool });
		}

		info.bindingCount = 3;
		info.pBindings = bindings;
		V(vkCreateDescriptorSetLayout(context.get_device(), &info, nullptr, &rdp.set_layouts[0]));
		rdp.descriptor_pool_sizes[0] = move(sizes);
	}

	// Set #1, Buffers
	{
		vector<VkDescriptorPoolSize> sizes;

		VkDescriptorSetLayoutCreateInfo info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
		VkDescriptorSetLayoutBinding bindings[RDP::BufferLayoutCount] = {};

		for (unsigned i = 0; i < RDP::BufferLayoutCount; i++)
		{
			VkDescriptorType type;
			switch (static_cast<RDP::BufferLayout>(i))
			{
			case RDP::BufferLayout::Combiners:
				type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				break;

			case RDP::BufferLayout::TileAtlas:
				type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				break;

			default:
				type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				break;
			}
			bindings[i].binding = i;
			bindings[i].descriptorType = type;
			bindings[i].descriptorCount = 1;
			bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
			sizes.push_back({ type, Internal::SetsPerPool });
		}

		info.bindingCount = RDP::BufferLayoutCount;
		info.pBindings = bindings;
		V(vkCreateDescriptorSetLayout(context.get_device(), &info, nullptr, &rdp.set_layouts[1]));
		rdp.descriptor_pool_sizes[1] = move(sizes);
	}

	VkPushConstantRange push = {};
	push.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	push.offset = 0;
	push.size = 24;

	VkPipelineLayoutCreateInfo info = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	info.setLayoutCount = 2;
	info.pSetLayouts = rdp.set_layouts;
	info.pushConstantRangeCount = 1;
	info.pPushConstantRanges = &push;
	V(vkCreatePipelineLayout(context.get_device(), &info, nullptr, &rdp.layout));

	VkComputePipelineCreateInfo pipe = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
	pipe.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	pipe.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	pipe.stage.pName = "main";
	pipe.layout = rdp.layout;

	static const uint32_t rdp_8_noz[] =
#include "glsl/rdp.8.noz.inc"
	    ;

	static const uint32_t rdp_16_noz[] =
#include "glsl/rdp.16.noz.inc"
	    ;

	static const uint32_t rdp_32_noz[] =
#include "glsl/rdp.32.noz.inc"
	    ;

	static const uint32_t rdp_8_z[] =
#include "glsl/rdp.8.z.inc"
	    ;

	static const uint32_t rdp_16_z[] =
#include "glsl/rdp.16.z.inc"
	    ;

	static const uint32_t rdp_32_z[] =
#include "glsl/rdp.32.z.inc"
	    ;

	static const uint32_t rdp_color_depth_alias[] =
#include "glsl/rdp.color.depth.alias.16.inc"
	    ;

	static const uint32_t varying[] =
#include "glsl/varying.inc"
	    ;

	static const uint32_t texture[] =
#include "glsl/texture.inc"
	    ;

	static const uint32_t combiner[] =
#include "glsl/combiner.inc"
	    ;

	static const uint32_t *shaders[] = {
		rdp_8_noz, rdp_16_noz, rdp_32_noz, rdp_8_z, rdp_16_z, rdp_32_z, rdp_color_depth_alias,
		varying,   texture,    combiner,
	};

	static const size_t shader_sizes[] = {
		sizeof(rdp_8_noz),
		sizeof(rdp_16_noz),
		sizeof(rdp_32_noz),
		sizeof(rdp_8_z),
		sizeof(rdp_16_z),
		sizeof(rdp_32_z),
		sizeof(rdp_color_depth_alias),
		sizeof(varying),
		sizeof(texture),
		sizeof(combiner),
	};

	std::future<void> compilations[RDP::PipelineCount];

	for (unsigned i = 0; i < RDP::PipelineCount; i++)
	{
		compilations[i] = async(launch::async, [=] {
			auto tmp = pipe;
			VkShaderModuleCreateInfo module_info = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
			module_info.codeSize = shader_sizes[i];
			module_info.pCode = reinterpret_cast<const uint32_t *>(shaders[i]);
			V(vkCreateShaderModule(context.get_device(), &module_info, nullptr, &tmp.stage.module));

			V(vkCreateComputePipelines(context.get_device(), pipeline_cache, 1, &tmp, nullptr, &rdp.pipelines[i]));
			vkDestroyShaderModule(context.get_device(), tmp.stage.module, nullptr);
		});
	}

	for (auto &comp : compilations)
		comp.wait();

	VkSamplerCreateInfo sampler = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	sampler.magFilter = VK_FILTER_NEAREST;
	sampler.minFilter = VK_FILTER_NEAREST;
	sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	V(vkCreateSampler(context.get_device(), &sampler, nullptr, &rdp.sampler));
}

DescriptorSet Device::request_descriptor_set(DescriptorSetAllocator &alloc, VkDescriptorSetLayout layout)
{
	if (alloc.count >= alloc.pools.size())
	{
		// Create a new pool.
		VkDescriptorPoolCreateInfo info = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
		info.maxSets = Internal::SetsPerPool;
		info.poolSizeCount = alloc.sizes.size();
		info.pPoolSizes = alloc.sizes.data();

		VkDescriptorPool pool;
		V(vkCreateDescriptorPool(context.get_device(), &info, nullptr, &pool));

		alloc.pools.push_back({ pool, 0, Internal::SetsPerPool });
	}

	auto &subpool = alloc.pools[alloc.count];

	VkDescriptorSetAllocateInfo info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
	info.descriptorPool = subpool.pool;
	info.descriptorSetCount = 1;
	info.pSetLayouts = &layout;

	VkDescriptorSet descriptor_set;
	V(vkAllocateDescriptorSets(context.get_device(), &info, &descriptor_set));
	alloc.pools[alloc.count].count++;

	// Exhausted current pool, skip to new one.
	if (subpool.count == subpool.size)
		alloc.count++;

	return { context.get_device(), descriptor_set };
}

DescriptorSet Device::request_rdp_descriptor_set(RDP::DescriptorSetType type)
{
   auto &frame = per_frame[current_index];
   auto &alloc = frame.descriptor_set_rdp_allocator[static_cast<unsigned>(type)];
   return request_descriptor_set(alloc, rdp.set_layouts[static_cast<unsigned>(type)]);
}

DescriptorSet Device::request_blit_descriptor_set(Blit::DescriptorSetType type)
{
   auto &frame = per_frame[current_index];
   auto &alloc = frame.descriptor_set_blit_allocator[static_cast<unsigned>(type)];
   return request_descriptor_set(alloc, blit.set_layouts[static_cast<unsigned>(type)]);
}

void DescriptorSet::set_storage_buffer(unsigned binding, Buffer &buffer, size_t offset, size_t range)
{
   auto &block = buffer.staging.block && buffer.staging.block->device_local ? buffer.staging : buffer.device;
   VkDescriptorBufferInfo buf = { block.block->buffer, block.offset + offset, range };

   VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
   write.dstSet = set;
   write.dstBinding = binding;
   write.descriptorCount = 1;
   write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
   write.pBufferInfo = &buf;

   vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

void DescriptorSet::set_storage_buffer(unsigned binding, Buffer &buffer)
{
   auto &block = buffer.staging.block && buffer.staging.block->device_local ? buffer.staging : buffer.device;
   set_storage_buffer(binding, buffer, 0, block.size);
}

void DescriptorSet::set_uniform_buffer(unsigned binding, Buffer &buffer, size_t offset, size_t range)
{
   auto &block = buffer.staging.block && buffer.staging.block->device_local ? buffer.staging : buffer.device;
   VkDescriptorBufferInfo buf = { block.block->buffer, block.offset + offset, range };

   VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
   write.dstSet = set;
   write.dstBinding = binding;
   write.descriptorCount = 1;
   write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
   write.pBufferInfo = &buf;

   vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

void DescriptorSet::set_uniform_buffer(unsigned binding, Buffer &buffer)
{
   auto &block = buffer.staging.block && buffer.staging.block->device_local ? buffer.staging : buffer.device;
   set_uniform_buffer(binding, buffer, 0, block.size);
}

void DescriptorSet::set_image(unsigned binding, Image &image)
{
   VkDescriptorImageInfo img = { image.sampler, image.view, image.layout };

   VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
   write.dstSet = set;
   write.dstBinding = binding;
   write.descriptorCount = 1;
   write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
   write.pImageInfo = &img;

   vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

void DescriptorSet::set_storage_image(unsigned binding, Image &image)
{
   VkDescriptorImageInfo img  = { VK_NULL_HANDLE, image.view, image.layout };

   VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
   write.dstSet               = set;
   write.dstBinding           = binding;
   write.descriptorCount      = 1;
   write.descriptorType       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
   write.pImageInfo           = &img;

   vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

void CommandBuffer::sync_buffer_to_gpu(Buffer &buffer)
{
   if (!buffer.staging.block->device_local)
   {
      VkBufferCopy region = { buffer.staging.offset, buffer.device.offset, buffer.staging.size };
      vkCmdCopyBuffer(cmd, buffer.staging.block->buffer, buffer.device.block->buffer, 1, &region);
   }
}

void CommandBuffer::sync_buffer_to_cpu(Buffer &buffer)
{
   if (!buffer.staging.block->device_local)
   {
      VkBufferCopy region = { buffer.device.offset, buffer.staging.offset, buffer.staging.size };
      vkCmdCopyBuffer(cmd, buffer.device.block->buffer, buffer.staging.block->buffer, 1, &region);
   }
}

void CommandBuffer::begin_stream()
{
   // Invalidate transfer read cache.
   VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
   barrier.srcAccessMask = 0;
   barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
   vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0,
		   nullptr, 0, nullptr);
}

void CommandBuffer::end_stream()
{
   // Transfers are done, sync compute shaders with this.
   VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
   barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
   barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
   vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0,
		   nullptr, 0, nullptr);
}

void CommandBuffer::begin_readback()
{
   // Compute shaders are done, begin readback.
   VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
   barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
   barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
   vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0,
		   nullptr, 0, nullptr);
}

void CommandBuffer::end_readback()
{
   // Transfers are done, synchronize memory to host.
   VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
   barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
   barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
   vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0,
		   nullptr);
}

void CommandBuffer::flush_barrier()
{
   VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
   barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
   barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
   vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
		   &barrier, 0, nullptr, 0, nullptr);
}

void CommandBuffer::prepare_image(Image &image)
{
   VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
   barrier.srcAccessMask = 0;
   barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
   barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
   barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
   barrier.image = image.image;
   barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
   barrier.subresourceRange.levelCount = 1;
   barrier.subresourceRange.layerCount = image.layers;

   image.layout = barrier.newLayout;

   vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
		   nullptr, 1, &barrier);
}

void CommandBuffer::prepare_storage_image(Image &image)
{
   VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
   barrier.srcAccessMask = 0;
   barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
   barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
   barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
   barrier.image = image.image;
   barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
   barrier.subresourceRange.levelCount = 1;
   barrier.subresourceRange.layerCount = image.layers;

   image.layout = barrier.newLayout;

   vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr,
		   0, nullptr, 1, &barrier);
}

void CommandBuffer::prepare_mixed_image(Image &image)
{
   VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
   barrier.srcAccessMask = 0;
   barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
   barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
   barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
   barrier.image = image.image;
   barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
   barrier.subresourceRange.levelCount = 1;
   barrier.subresourceRange.layerCount = image.layers;

   image.layout = barrier.newLayout;

   vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
		   nullptr, 1, &barrier);
}

void CommandBuffer::copy_to_image(Image &image, Buffer &buffer, size_t offset, unsigned x, unsigned y, unsigned layer,
                                  unsigned width, unsigned height, unsigned row_length)
{
   VkBufferImageCopy copy = {};

   copy.bufferOffset = offset + buffer.staging.offset;
   copy.bufferRowLength = row_length;
   copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
   copy.imageSubresource.baseArrayLayer = layer;
   copy.imageSubresource.layerCount = 1;
   copy.imageOffset.x = x;
   copy.imageOffset.y = y;
   copy.imageExtent.width = width;
   copy.imageExtent.height = height;
   copy.imageExtent.depth = 1;

   vkCmdCopyBufferToImage(cmd, buffer.staging.block->buffer, image.image, image.layout, 1, &copy);
}

void CommandBuffer::copy_buffer(Buffer &dst, Buffer &src)
{
   VkBufferCopy copy = {};

   auto &src_block = src.staging.block && src.staging.block->device_local ? src.staging : src.device;
   auto &dst_block = dst.staging.block && dst.staging.block->device_local ? dst.staging : dst.device;

   copy.srcOffset = src_block.offset;
   copy.dstOffset = dst_block.offset;
   assert(src_block.size == dst_block.size);
   copy.size = src_block.size;

   vkCmdCopyBuffer(cmd, src_block.block->buffer, dst_block.block->buffer, 1, &copy);
}

void CommandBuffer::complete_image(Image &image)
{
   VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
   barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
   barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
   barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
   barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
   barrier.image = image.image;
   barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
   barrier.subresourceRange.levelCount = 1;
   barrier.subresourceRange.layerCount = image.layers;

   image.layout = barrier.newLayout;

   vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
		   nullptr, 1, &barrier);
}

void CommandBuffer::complete_storage_image(Image &image)
{
   VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
   barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
   barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
   barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
   barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
   barrier.image = image.image;
   barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
   barrier.subresourceRange.levelCount = 1;
   barrier.subresourceRange.layerCount = image.layers;

   image.layout = barrier.newLayout;

   vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
		   nullptr, 0, nullptr, 1, &barrier);
}

void CommandBuffer::complete_mixed_image(Image &image)
{
   VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
   barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
   barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
   barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
   barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
   barrier.image = image.image;
   barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
   barrier.subresourceRange.levelCount = 1;
   barrier.subresourceRange.layerCount = image.layers;

   image.layout = barrier.newLayout;

   vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
		   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void CommandBuffer::bind_pipeline(const Pipeline &pipeline)
{
   vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline);
   layout = pipeline.layout;
}

void CommandBuffer::bind_descriptor_set(unsigned index, const DescriptorSet &set)
{
   vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, index, 1, &set.set, 0, nullptr);
}

void CommandBuffer::dispatch(unsigned x, unsigned y, unsigned z)
{
   vkCmdDispatch(cmd, x, y, z);
}

void CommandBuffer::push_constants(const void *data, size_t size)
{
   vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, size, data);
}

namespace Internal
{
void ImageDeleter::operator()(Image *image)
{
   image->device.delete_image(image);
}
}
}
