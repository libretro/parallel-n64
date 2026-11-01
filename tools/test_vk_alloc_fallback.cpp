/* test_vk_alloc_fallback -- Granite's retry after an allocation fails.
 *
 * When vkAllocateMemory fails, DeviceAllocator frees the blocks it keeps
 * cached for reuse and tries again. The loop tested the wrong iterator,
 * so it never ran: the allocation failed with memory still held in the
 * cache. This drives allocate_after_freeing_cached with a fake device
 * table whose allocations fit only once enough has been freed.
 */
#include <stdio.h>
#include <string.h>

#include "memory_allocator.hpp"

static int failures;
#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static uint64_t device_free_bytes;
static unsigned alloc_calls;
static VkDeviceMemory freed[16];
static unsigned num_freed;

static VKAPI_ATTR VkResult VKAPI_CALL fake_allocate(VkDevice, const VkMemoryAllocateInfo *info,
                                                     const VkAllocationCallbacks *, VkDeviceMemory *memory)
{
	alloc_calls++;
	if (info->allocationSize > device_free_bytes)
		return VK_ERROR_OUT_OF_DEVICE_MEMORY;
	device_free_bytes -= info->allocationSize;
	*memory = (VkDeviceMemory)(uintptr_t)0x1000;
	return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL fake_free(VkDevice, VkDeviceMemory memory, const VkAllocationCallbacks *)
{
	freed[num_freed++] = memory;
}

/* The fake frees a block's bytes back to the device. */
static VKAPI_ATTR void VKAPI_CALL fake_free_sized(VkDevice, VkDeviceMemory memory, const VkAllocationCallbacks *)
{
	freed[num_freed++] = memory;
	device_free_bytes += (uint64_t)(uintptr_t)memory;
}

static Vulkan::CachedAllocation block(uint32_t size)
{
	Vulkan::CachedAllocation a = { (VkDeviceMemory)(uintptr_t)size, size, 0, Vulkan::AllocationMode::LinearDevice };
	return a;
}

int main()
{
	VolkDeviceTable table;
	memset(&table, 0, sizeof(table));
	table.vkAllocateMemory = fake_allocate;
	table.vkFreeMemory = fake_free_sized;
	VkDevice dev = (VkDevice)(uintptr_t)1;

	VkMemoryAllocateInfo info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	info.allocationSize = 96;
	VkDeviceMemory mem = VK_NULL_HANDLE;

	/* Three cached blocks of 64; the request needs two of them freed. */
	{
		Util::Vector<Vulkan::CachedAllocation> blocks;
		blocks.push_back(block(64));
		blocks.push_back(block(64));
		blocks.push_back(block(64));
		uint64_t heap_size = 192 + 1000;
		device_free_bytes = 0;
		alloc_calls = 0;
		num_freed = 0;

		VkResult res = Vulkan::allocate_after_freeing_cached(table, dev, blocks, heap_size, info, &mem,
		                                                     VK_ERROR_OUT_OF_DEVICE_MEMORY);
		CHECK(res == VK_SUCCESS);
		CHECK(mem != VK_NULL_HANDLE);
		CHECK(num_freed == 2 && alloc_calls == 2);
		CHECK(blocks.size() == 1);
		CHECK(heap_size == 64 + 1000);
	}

	/* Not enough in the cache: everything is freed and the error kept. */
	{
		Util::Vector<Vulkan::CachedAllocation> blocks;
		blocks.push_back(block(16));
		blocks.push_back(block(16));
		uint64_t heap_size = 32;
		device_free_bytes = 0;
		alloc_calls = 0;
		num_freed = 0;

		VkResult res = Vulkan::allocate_after_freeing_cached(table, dev, blocks, heap_size, info, &mem,
		                                                     VK_ERROR_OUT_OF_DEVICE_MEMORY);
		CHECK(res == VK_ERROR_OUT_OF_DEVICE_MEMORY);
		CHECK(num_freed == 2 && alloc_calls == 2);
		CHECK(blocks.empty() && heap_size == 0);
	}

	/* Empty cache, or an allocation that already succeeded: nothing freed. */
	{
		Util::Vector<Vulkan::CachedAllocation> blocks;
		uint64_t heap_size = 0;
		num_freed = 0;
		alloc_calls = 0;
		VkResult res = Vulkan::allocate_after_freeing_cached(table, dev, blocks, heap_size, info, &mem,
		                                                     VK_ERROR_OUT_OF_DEVICE_MEMORY);
		CHECK(res == VK_ERROR_OUT_OF_DEVICE_MEMORY && num_freed == 0 && alloc_calls == 0);

		blocks.push_back(block(64));
		table.vkFreeMemory = fake_free;
		res = Vulkan::allocate_after_freeing_cached(table, dev, blocks, heap_size, info, &mem, VK_SUCCESS);
		CHECK(res == VK_SUCCESS && num_freed == 0 && alloc_calls == 0 && blocks.size() == 1);
	}

	if (failures)
	{
		fprintf(stderr, "test_vk_alloc_fallback: %d failures\n", failures);
		return 1;
	}
	printf("test_vk_alloc_fallback: ok\n");
	return 0;
}
