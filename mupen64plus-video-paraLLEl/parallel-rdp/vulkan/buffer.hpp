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

#include "cookie.hpp"
#include "vulkan_common.hpp"
#include "memory_allocator.hpp"
#include "buffer_create.h"

namespace Vulkan
{
class Device;

// Creating a buffer is C (buffer_create.h).
static inline VkPipelineStageFlags buffer_usage_to_possible_stages(VkBufferUsageFlags usage)
{
	return granite_buffer_usage_stages(usage);
}

static inline VkAccessFlags buffer_usage_to_possible_access(VkBufferUsageFlags usage)
{
	return granite_buffer_usage_access(usage);
}

enum class BufferDomain
{
	Device = GRANITE_BUFFER_DOMAIN_DEVICE, // Device local. Probably not visible from CPU.
	LinkedDeviceHost = GRANITE_BUFFER_DOMAIN_LINKED_DEVICE_HOST, // On desktop, directly mapped VRAM over PCI.
	LinkedDeviceHostPreferDevice = GRANITE_BUFFER_DOMAIN_LINKED_DEVICE_HOST_PREFER_DEVICE, // Prefer device local of host visible.
	Host = GRANITE_BUFFER_DOMAIN_HOST, // Host-only, needs to be synced to GPU. Might be device local as well on iGPUs.
	CachedHost = GRANITE_BUFFER_DOMAIN_CACHED_HOST,
	CachedCoherentHostPreferCoherent = GRANITE_BUFFER_DOMAIN_CACHED_COHERENT_HOST_PREFER_COHERENT, // Aim for both cached and coherent, but prefer COHERENT
	CachedCoherentHostPreferCached = GRANITE_BUFFER_DOMAIN_CACHED_COHERENT_HOST_PREFER_CACHED, // Aim for both cached and coherent, but prefer CACHED
};

enum BufferMiscFlagBits
{
	BUFFER_MISC_ZERO_INITIALIZE_BIT = 1 << 0
};

using BufferMiscFlags = uint32_t;

struct BufferCreateInfo
{
	BufferDomain domain = BufferDomain::Device;
	VkDeviceSize size = 0;
	VkBufferUsageFlags usage = 0;
	BufferMiscFlags misc = 0;
};

class Buffer;
struct BufferDeleter
{
	void operator()(Buffer *buffer);
};

class BufferView;
struct BufferViewDeleter
{
	void operator()(BufferView *view);
};

class Buffer : public Util::IntrusivePtrEnabled<Buffer, BufferDeleter, HandleCounter>,
               public Cookie, public InternalSyncEnabled
{
public:
	friend struct BufferDeleter;
	~Buffer();

	VkBuffer get_buffer() const
	{
		return buffer;
	}

	const BufferCreateInfo &get_create_info() const
	{
		return info;
	}

	DeviceAllocation &get_allocation()
	{
		return alloc;
	}

	const DeviceAllocation &get_allocation() const
	{
		return alloc;
	}


private:
	friend class Util::ObjectPool<Buffer>;
	Buffer(Device *device, VkBuffer buffer, const DeviceAllocation &alloc, const BufferCreateInfo &info);

	Device *device;
	VkBuffer buffer;
	DeviceAllocation alloc;
	BufferCreateInfo info;
};
using BufferHandle = Util::IntrusivePtr<Buffer>;

struct BufferViewCreateInfo
{
	const Buffer *buffer;
	VkFormat format;
	VkDeviceSize offset;
	VkDeviceSize range;
};

class BufferView : public Util::IntrusivePtrEnabled<BufferView, BufferViewDeleter, HandleCounter>,
                   public Cookie, public InternalSyncEnabled
{
public:
	friend struct BufferViewDeleter;
	~BufferView();

	VkBufferView get_view() const
	{
		return view;
	}

	const BufferViewCreateInfo &get_create_info()
	{
		return info;
	}

	const Buffer &get_buffer() const
	{
		return *info.buffer;
	}

private:
	friend class Util::ObjectPool<BufferView>;
	BufferView(Device *device, VkBufferView view, const BufferViewCreateInfo &info);

	Device *device;
	VkBufferView view;
	BufferViewCreateInfo info;
};
using BufferViewHandle = Util::IntrusivePtr<BufferView>;
}
