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

#include "vulkan_common.hpp"
#include "memory_allocator.hpp"
#include "buffer.h"

namespace Vulkan
{
class Device;

// Buffers are C (buffer.h, buffer_create.h). These give them the names
// and accessors the rest of Granite uses.
static inline VkPipelineStageFlags buffer_usage_to_possible_stages(VkBufferUsageFlags usage)
{
	return granite_buffer_usage_stages(usage);
}

static inline VkAccessFlags buffer_usage_to_possible_access(VkBufferUsageFlags usage)
{
	return granite_buffer_usage_access(usage);
}

// granite_buffer_domain_t
namespace BufferDomain
{
static const granite_buffer_domain_t Device = GRANITE_BUFFER_DOMAIN_DEVICE;
static const granite_buffer_domain_t LinkedDeviceHost = GRANITE_BUFFER_DOMAIN_LINKED_DEVICE_HOST;
static const granite_buffer_domain_t LinkedDeviceHostPreferDevice = GRANITE_BUFFER_DOMAIN_LINKED_DEVICE_HOST_PREFER_DEVICE;
static const granite_buffer_domain_t Host = GRANITE_BUFFER_DOMAIN_HOST;
static const granite_buffer_domain_t CachedHost = GRANITE_BUFFER_DOMAIN_CACHED_HOST;
static const granite_buffer_domain_t CachedCoherentHostPreferCoherent = GRANITE_BUFFER_DOMAIN_CACHED_COHERENT_HOST_PREFER_COHERENT;
static const granite_buffer_domain_t CachedCoherentHostPreferCached = GRANITE_BUFFER_DOMAIN_CACHED_COHERENT_HOST_PREFER_CACHED;
}

enum BufferMiscFlagBits
{
	BUFFER_MISC_ZERO_INITIALIZE_BIT = GRANITE_BUFFER_MISC_ZERO_INITIALIZE_BIT
};

using BufferMiscFlags = uint32_t;

// A device buffer of no size unless said otherwise.
struct BufferCreateInfo : granite_buffer_info_t
{
	BufferCreateInfo()
	{
		domain = GRANITE_BUFFER_DOMAIN_DEVICE;
		size = 0;
		usage = 0;
		misc = 0;
	}
};

// No members of its own: a granite_buffer_t by another name.
class Buffer : public granite_buffer_t
{
public:
	Buffer() = delete;

	void add_reference()
	{
		granite_buffer_ref(this);
	}

	void release_reference()
	{
		granite_buffer_unref(this);
	}

	uint64_t get_cookie() const
	{
		return cookie;
	}

	void set_internal_sync_object()
	{
		internal_sync = true;
	}

	VkBuffer get_buffer() const
	{
		return buffer;
	}

	const granite_buffer_info_t &get_create_info() const
	{
		return info;
	}

	const granite_allocation_t &get_allocation() const
	{
		return alloc;
	}
};
using BufferHandle = Util::IntrusivePtr<Buffer>;

struct BufferViewCreateInfo
{
	const Buffer *buffer;
	VkFormat format;
	VkDeviceSize offset;
	VkDeviceSize range;
};

class BufferView : public granite_buffer_view_t
{
public:
	BufferView() = delete;

	void add_reference()
	{
		granite_buffer_view_ref(this);
	}

	void release_reference()
	{
		granite_buffer_view_unref(this);
	}

	uint64_t get_cookie() const
	{
		return cookie;
	}

	void set_internal_sync_object()
	{
		internal_sync = true;
	}

	VkBufferView get_view() const
	{
		return view;
	}

	const granite_buffer_view_info_t &get_create_info() const
	{
		return info;
	}

	const Buffer &get_buffer() const
	{
		return *static_cast<const Buffer *>(info.buffer);
	}
};
using BufferViewHandle = Util::IntrusivePtr<BufferView>;
}
