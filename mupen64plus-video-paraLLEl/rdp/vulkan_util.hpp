#ifndef VULKAN_UTIL_HPP
#define VULKAN_UTIL_HPP

#include "vulkan.hpp"
#include <functional>
#include <stack>
#include <stddef.h>
#include <vector>

namespace Vulkan
{
class Device;
struct Image;

static const unsigned MaxBlockSize = 32 * 1024 * 1024;

namespace RDP
{

enum class DescriptorSetType
{
   LUT = 0,
   Buffers = 1,
   Count
};

enum class PipelineType
{
   NoZ_8bit = 0,
   NoZ_16bit = 1,
   NoZ_32bit = 2,
   Z_8bit = 3,
   Z_16bit = 4,
   Z_32bit = 5,
   ColorDepthAlias_16bit = 6,
   Varying = 7,
   Texture = 8,
   Combiner = 9,
   Count
};

enum class BufferLayout
{
   PrimitiveData = 0,
   TileListHeader = 1,
   TileList = 2,
   Color = 3,
   Depth = 4,
   Combiners = 5,
   TileDescriptor = 6,
   TileAtlas = 7,
   WorkDescriptor = 8,
   TileBuffer = 9,
   Count
};

static const unsigned PipelineCount = static_cast<unsigned>(PipelineType::Count);
static const unsigned DescriptorSetCount = static_cast<unsigned>(DescriptorSetType::Count);
static const unsigned BufferLayoutCount = static_cast<unsigned>(BufferLayout::Count);
}

namespace Blit
{
enum class DescriptorSetType
{
   Buffers = 0,
   Count
};

enum class PipelineType
{
   Blit_8bit = 0,
   Blit_16bit = 1,
   Blit_32bit = 2,
   TMEM_RGBA32 = 3,
   TMEM_RGBA16 = 4,
   TMEM_I8 = 5,
   TMEM_IA8 = 6,
   TMEM_IA16 = 7,
   Count
};

enum class BufferLayout
{
   Color = 0,
   Image = 1,
   Count
};

static const unsigned PipelineCount = static_cast<unsigned>(PipelineType::Count);
static const unsigned DescriptorSetCount = static_cast<unsigned>(DescriptorSetType::Count);
static const unsigned BufferLayoutCount = static_cast<unsigned>(BufferLayout::Count);
}

namespace Internal
{
static const unsigned SetsPerPool = 16;

struct FenceAllocator
{
   std::vector<VkFence> fences;
   unsigned count = 0;
};

struct SemaphoreAllocator
{
   std::vector<VkSemaphore> semaphores;
   unsigned count = 0;
};

struct DescriptorPool
{
   VkDescriptorPool pool;
   unsigned count;
   unsigned size;
};

struct DescriptorSetAllocator
{
   std::vector<DescriptorPool> pools;
   std::vector<VkDescriptorPoolSize> sizes;
   unsigned count = 0;
};

struct CommandPool
{
   VkCommandPool pool = VK_NULL_HANDLE;
   std::vector<VkCommandBuffer> buffers;
   unsigned count = 0;
};

struct PerFrame
{
   FenceAllocator fence_allocator;
   SemaphoreAllocator semaphore_allocator;

   CommandPool command_pool;
   CommandPool alt_command_pool;

   DescriptorSetAllocator descriptor_set_rdp_allocator[RDP::DescriptorSetCount];
   DescriptorSetAllocator descriptor_set_blit_allocator[Blit::DescriptorSetCount];

   std::vector<std::function<void()>> defers;
   uint64_t frame_count = 0;
};

struct Block
{
   VkDevice device;
   VkBuffer buffer;
   VkDeviceMemory memory;

   size_t offset;
   size_t size;
   void *mapped;
   bool coherent;
   bool device_local;

   ~Block()
   {
      vkDeviceWaitIdle(device);
      vkDestroyBuffer(device, buffer, nullptr);
      if (mapped)
         vkUnmapMemory(device, memory);
      vkFreeMemory(device, memory, nullptr);
   }
};

struct ImageDeleter
{
   void operator()(Image *image);
};

enum class BufferLocation
{
   Device,
   CachedHost,
   CachedHostShared,
   DeviceShared
};

struct AllocatedBlock
{
   size_t offset;
   size_t size;
   std::shared_ptr<Block> block;
};

struct BufferAllocator
{
	BufferAllocator(const VulkanContext &context, BufferLocation location)
	    : context(context)
	    , location(location)
	{
	}

	const VulkanContext &context;
	BufferLocation location;

	std::stack<std::unique_ptr<Block>> vacant;
	std::shared_ptr<Block> current;
};

struct Memory
{
	VkDevice device;
	VkDeviceMemory memory;
	size_t offset;
	size_t size;

	~Memory()
	{
		//fprintf(stderr, "Destroy memory %p\n", this);
		vkDeviceWaitIdle(device);
		vkFreeMemory(device, memory, nullptr);
	}
};

struct AllocatedMemory
{
	~AllocatedMemory()
	{
		//fprintf(stderr, "Destroy allocated memory %p\n", this);
	}

	size_t offset;
	size_t size;
	std::shared_ptr<Memory> memory;
};

struct MemoryAllocator
{
	MemoryAllocator(const VulkanContext &context)
	    : context(context)
	{
	}

	const VulkanContext &context;
	std::stack<std::unique_ptr<Memory>> vacant;
	std::shared_ptr<Memory> current;
};
}

enum class BufferType
{
   Dynamic = 0,      // Both on GPU and CPU. If GPU is integrated, just one.
   Staging = 1,      // For CPU -> GPU texture
   Device = 2,       // GPU only
   DynamicShared = 3 // Same as dynamic, but device memory can be used by queue and alt_queue.
};

struct Fence
{
	Fence(VkFence fence, uint64_t set_on_frame, unsigned frame_index)
	    : fence(fence)
	    , set_on_frame(set_on_frame)
	    , frame_index(frame_index)
	{
	}

	Fence() = default;

	VkFence fence = VK_NULL_HANDLE;
	uint64_t set_on_frame = 0;
	unsigned frame_index = 0;
};

struct Semaphore
{
   Semaphore() = default;
   Semaphore(VkSemaphore sem)
	   : semaphore(sem)
   {
   }
   VkSemaphore semaphore = VK_NULL_HANDLE;
};

struct Buffer
{
   void *map();
   void unmap();

   Internal::AllocatedBlock staging, device;
   BufferType type = BufferType::Dynamic;
   size_t atom_alignment = 1;
};

struct Image
{
	Image(Device &device, VkImage image, VkImageView view, Internal::AllocatedMemory &&block, unsigned layers)
	    : device(device)
	    , image(image)
	    , view(view)
	    , block(std::move(block))
	    , layers(layers)
	{
	}

	~Image()
	{
		//fprintf(stderr, "Destroy image %p\n", this);
	}

	Device &device;
	VkImage image;
	VkImageView view;
	Internal::AllocatedMemory block;
	unsigned layers;

	VkSampler sampler = VK_NULL_HANDLE;
	VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

using ImageHandle = std::unique_ptr<Image, Internal::ImageDeleter>;

struct Pipeline
{
   VkPipeline pipeline;
   VkPipelineLayout layout;
};

struct DescriptorSet
{
   DescriptorSet(VkDevice device, VkDescriptorSet set)
	    : device(device)
	    , set(set)
   {
   }
   DescriptorSet() = default;

   void set_storage_buffer(unsigned binding, Buffer &buffer, size_t offset, size_t range);
   void set_uniform_buffer(unsigned binding, Buffer &buffer, size_t offset, size_t range);
   void set_storage_buffer(unsigned binding, Buffer &buffer);
   void set_uniform_buffer(unsigned binding, Buffer &buffer);
   void set_image(unsigned binding, Image &image);
   void set_storage_image(unsigned binding, Image &image);

   VkDevice device = VK_NULL_HANDLE;
   VkDescriptorSet set = VK_NULL_HANDLE;
};

struct CommandBuffer
{
   CommandBuffer(VkCommandBuffer cmd, VkPipelineLayout layout)
	    : cmd(cmd)
	    , layout(layout)
   {
   }
   CommandBuffer() = default;

   void sync_buffer_to_gpu(Buffer &buffer);
   void sync_buffer_to_cpu(Buffer &buffer);

   void copy_to_image(Image &image, Buffer &buffer, size_t buffer_offset, unsigned x, unsigned y, unsigned layer,
		   unsigned width, unsigned height, unsigned row_length);
   void copy_buffer(Buffer &dst, Buffer &src);

   void bind_pipeline(const Pipeline &pipeline);
   void bind_descriptor_set(unsigned index, const DescriptorSet &set);
   void dispatch(unsigned x, unsigned y, unsigned z);

   void begin_stream();
   void end_stream();
   void begin_readback();
   void end_readback();

   void push_constants(const void *data, size_t size);

   void prepare_image(Image &image);
   void complete_image(Image &image);
   void prepare_storage_image(Image &image);
   void prepare_mixed_image(Image &image);
   void complete_storage_image(Image &image);
   void complete_mixed_image(Image &image);

   void flush_barrier();

   VkCommandBuffer cmd = VK_NULL_HANDLE;
   VkPipelineLayout layout = VK_NULL_HANDLE;
};

class Device
{
public:
   friend struct Internal::ImageDeleter;

   Device(const VulkanContext &context, unsigned frames);
   ~Device();

   void set_num_frames(unsigned num_frames);

   unsigned get_num_frames() const
   {
      return per_frame.size();
   }

   void begin_index(unsigned index);

   CommandBuffer request_command_buffer();
   CommandBuffer request_alt_command_buffer();
   Semaphore request_semaphore();

   Fence submit(const CommandBuffer &cmd, const Semaphore *wait_semaphore = nullptr,
		   const Semaphore *signal_semaphore = nullptr);
   Fence submit_alt_queue(const CommandBuffer &cmd, const Semaphore *wait_semaphore = nullptr,
		   const Semaphore *signal_semaphore = nullptr);
   void wait(const Fence &fence);

   Buffer request_buffer(BufferType type, size_t size);
   Buffer request_dynamic_buffer(CommandBuffer &cmd, DescriptorSet &set, RDP::BufferLayout binding, size_t size);
   size_t get_buffer_alignment() const;

   DescriptorSet request_rdp_descriptor_set(RDP::DescriptorSetType type);
   DescriptorSet request_blit_descriptor_set(Blit::DescriptorSetType type);

   Pipeline get_rdp_pipeline(RDP::PipelineType type) const
   {
      return { rdp.pipelines[static_cast<uint32_t>(type)], rdp.layout };
   }

   Pipeline get_blit_pipeline(Blit::PipelineType type) const
   {
      return { blit.pipelines[static_cast<uint32_t>(type)], blit.layout };
   }

   ImageHandle create_image_2d(VkFormat format, unsigned width, unsigned height);
   ImageHandle create_image_2d_array(VkFormat format, unsigned width, unsigned height, unsigned layers);

private:
   const VulkanContext &context;
   Internal::BufferAllocator cached_allocator;
   Internal::BufferAllocator device_allocator;
   Internal::BufferAllocator cached_shared_allocator;
   Internal::BufferAllocator shared_allocator;
   Internal::MemoryAllocator memory_allocator;

   void init_per_frame(unsigned num_frames);
   void deinit_per_frame();

   DescriptorSet request_descriptor_set(Internal::DescriptorSetAllocator &alloc, VkDescriptorSetLayout layout);

   struct
   {
      VkPipeline pipelines[RDP::PipelineCount];
      VkDescriptorSetLayout set_layouts[RDP::DescriptorSetCount];
      std::vector<VkDescriptorPoolSize> descriptor_pool_sizes[RDP::DescriptorSetCount];
      VkPipelineLayout layout;
      VkSampler sampler;
   } rdp;

   struct
   {
      VkPipeline pipelines[Blit::PipelineCount];
      VkDescriptorSetLayout set_layouts[Blit::DescriptorSetCount];
      std::vector<VkDescriptorPoolSize> descriptor_pool_sizes[Blit::DescriptorSetCount];
      VkPipelineLayout layout;
   } blit;

   VkPipelineCache pipeline_cache;
   void init_rdp_pipelines();
   void init_blit_pipelines();

   std::vector<Internal::PerFrame> per_frame;
   unsigned current_index = 0;

   Internal::AllocatedBlock allocate_block(Internal::BufferAllocator &alloc, size_t size);
   Internal::AllocatedMemory allocate_memory(Internal::MemoryAllocator &alloc, const VkMemoryRequirements &req);

   bool find_memory_type(uint32_t *index, uint32_t device_req, uint32_t host_req);
   ImageHandle create_image(VkFormat format, unsigned width, unsigned height, unsigned layers, bool array,
		   Internal::MemoryAllocator &mem_alloc);
   void delete_image(Image *image);

   CommandBuffer request_command_buffer(Internal::CommandPool &pool);
   Fence submit(VkQueue queue, const CommandBuffer &cmd, const Semaphore *wait_semaphore = nullptr,
		   const Semaphore *signal_semaphore = nullptr);
};
}

#endif
