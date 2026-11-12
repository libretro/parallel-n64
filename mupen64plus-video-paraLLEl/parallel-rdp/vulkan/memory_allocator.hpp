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

#include <string.h>
#include "memory_allocator.h"
#include "vulkan_headers.hpp"
#include "vulkan_common.hpp"

// The allocator is C (memory_allocator.h). These give the rest of Granite
// the names it had.
namespace Vulkan
{
// granite_allocation_mode_t
enum class AllocationMode : uint8_t
{
	LinearHostMappable = GRANITE_ALLOCATION_LINEAR_HOST_MAPPABLE,
	LinearDevice = GRANITE_ALLOCATION_LINEAR_DEVICE,
	LinearDeviceHighPriority = GRANITE_ALLOCATION_LINEAR_DEVICE_HIGH_PRIORITY,
	OptimalResource = GRANITE_ALLOCATION_OPTIMAL_RESOURCE,
	OptimalRenderTarget = GRANITE_ALLOCATION_OPTIMAL_RENDER_TARGET,
	External = GRANITE_ALLOCATION_EXTERNAL,
	Count = GRANITE_ALLOCATION_MODE_COUNT
};

enum MemoryAccessFlag : uint32_t
{
	MEMORY_ACCESS_WRITE_BIT = GRANITE_MEMORY_ACCESS_WRITE_BIT,
	MEMORY_ACCESS_READ_BIT = GRANITE_MEMORY_ACCESS_READ_BIT,
	MEMORY_ACCESS_READ_WRITE_BIT = MEMORY_ACCESS_WRITE_BIT | MEMORY_ACCESS_READ_BIT
};
using MemoryAccessFlags = uint32_t;

struct DeviceAllocation : granite_allocation_t
{
	DeviceAllocation()
	{
		memset(static_cast<granite_allocation_t *>(this), 0, sizeof(granite_allocation_t));
		mode = uint8_t(AllocationMode::Count);
	}

	inline VkDeviceMemory get_memory() const
	{
		return base;
	}

	inline bool allocation_is_global() const
	{
		return !alloc && base;
	}

	inline uint32_t get_offset() const
	{
		return offset;
	}

	inline uint32_t get_size() const
	{
		return size;
	}

	inline uint32_t get_mask() const
	{
		return mask;
	}

	inline bool is_host_allocation() const
	{
		return host_base != nullptr;
	}

	inline uint32_t get_memory_type() const
	{
		return memory_type;
	}

	static inline DeviceAllocation make_imported_allocation(VkDeviceMemory memory, VkDeviceSize size,
	                                                        uint32_t memory_type)
	{
		DeviceAllocation alloc;
		alloc.base = memory;
		alloc.size = uint32_t(size);
		alloc.memory_type = uint8_t(memory_type);
		return alloc;
	}

	inline void free_immediate(granite_device_allocator_t &allocator)
	{
		granite_allocation_free(this, &allocator);
	}
};

class Device;

struct DeviceAllocator : granite_device_allocator_t
{
	DeviceAllocator()
	{
		memset(static_cast<granite_device_allocator_t *>(this), 0, sizeof(granite_device_allocator_t));
	}

	~DeviceAllocator()
	{
		granite_device_allocator_free(this);
	}

	DeviceAllocator(const DeviceAllocator &) = delete;
	void operator=(const DeviceAllocator &) = delete;

	// device.cpp
	void init(Device *device);

	inline bool allocate_buffer_memory(uint32_t size, uint32_t alignment, AllocationMode mode, uint32_t memory_type,
	                                   VkBuffer buffer, DeviceAllocation *alloc, const ExternalHandle *external)
	{
		granite_external_memory_t ext;
		if (external)
			ext = external_memory(*external);
		return granite_device_allocator_allocate_buffer(this, size, alignment, granite_allocation_mode_t(mode),
		                                                memory_type, buffer, alloc, external ? &ext : nullptr);
	}

	inline bool allocate_image_memory(uint32_t size, uint32_t alignment, AllocationMode mode, uint32_t memory_type,
	                                  VkImage image, bool force_no_dedicated, DeviceAllocation *alloc,
	                                  const ExternalHandle *external)
	{
		granite_external_memory_t ext;
		if (external)
			ext = external_memory(*external);
		return granite_device_allocator_allocate_image(this, size, alignment, granite_allocation_mode_t(mode),
		                                               memory_type, image, force_no_dedicated, alloc,
		                                               external ? &ext : nullptr);
	}

	inline void garbage_collect()
	{
		granite_device_allocator_collect(this);
	}

	inline void *map_memory(const DeviceAllocation &alloc, MemoryAccessFlags flags, VkDeviceSize offset,
	                        VkDeviceSize length)
	{
		return granite_device_allocator_map(this, &alloc, flags, offset, length);
	}

	inline void unmap_memory(const DeviceAllocation &alloc, MemoryAccessFlags flags, VkDeviceSize offset,
	                         VkDeviceSize length)
	{
		granite_device_allocator_unmap(this, &alloc, flags, offset, length);
	}

private:
	// A handle to import, or the type to make the memory exportable as.
	static inline granite_external_memory_t external_memory(const ExternalHandle &handle)
	{
		granite_external_memory_t external;
		external.import = bool(handle);
		external.handle = handle.handle;
		external.handle_type = handle.memory_handle_type;
		return external;
	}
};
}
