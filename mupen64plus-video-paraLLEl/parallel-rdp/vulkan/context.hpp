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

#include "vulkan_headers.hpp"
#include "logging.h"
#include "vulkan_common.hpp"
#include "context.h"
#include <string.h>

namespace Vulkan
{
// The context is C (context.h). These give it the names the rest of
// Granite uses.
using DeviceFeatures = granite_device_features_t;
using QueueInfo = granite_queue_info_t;

enum VendorID
{
	VENDOR_ID_AMD = 0x1002,
	VENDOR_ID_NVIDIA = 0x10de,
	VENDOR_ID_INTEL = 0x8086,
	VENDOR_ID_ARM = 0x13b5,
	VENDOR_ID_QCOM = 0x5143
};

// Creates the device in place of vkCreateDevice, e.g. through a
// libretro frontend that adds what it needs to the create info.
struct DeviceFactory
{
	granite_create_device_t create_device;
	void *user;
};

class Context
	: public Util::IntrusivePtrEnabled<Context, Util::DefaultDelete<Context>, HandleCounter>
{
public:
	Context()
	{
		memset(&context, 0, sizeof(context));
	}

	Context(const Context &) = delete;
	void operator=(const Context &) = delete;

	~Context()
	{
		granite_context_destroy(&context);
	}

	static bool init_loader(PFN_vkGetInstanceProcAddr addr)
	{
		return granite_context_init_loader(addr);
	}

	// Call before init_device_from_instance(). Must outlive device creation.
	void set_device_factory(DeviceFactory *factory)
	{
		device_factory = factory;
	}

	bool init_device_from_instance(VkInstance instance, VkPhysicalDevice gpu, VkSurfaceKHR surface,
	                               const char **required_device_extensions, unsigned num_required_device_extensions,
	                               const VkPhysicalDeviceFeatures *required_features)
	{
		granite_context_destroy(&context);
		return granite_context_init(&context, instance, gpu, surface, required_device_extensions,
		                            num_required_device_extensions, required_features,
		                            device_factory ? device_factory->create_device : nullptr,
		                            device_factory ? device_factory->user : nullptr);
	}

	VkInstance get_instance() const
	{
		return context.instance;
	}

	VkPhysicalDevice get_gpu() const
	{
		return context.gpu;
	}

	VkDevice get_device() const
	{
		return context.device;
	}

	const QueueInfo &get_queue_info() const
	{
		return context.queue_info;
	}

	const VkPhysicalDeviceProperties &get_gpu_props() const
	{
		return context.gpu_props;
	}

	const VkPhysicalDeviceMemoryProperties &get_mem_props() const
	{
		return context.mem_props;
	}

	// The device is someone else's to destroy.
	void release_device()
	{
		context.owned_device = false;
	}

	const DeviceFeatures &get_enabled_device_features() const
	{
		return context.ext;
	}

	const VolkDeviceTable &get_device_table() const
	{
		return context.device_table;
	}

private:
	granite_context_t context;
	DeviceFactory *device_factory = nullptr;
};

using ContextHandle = Util::IntrusivePtr<Context>;
}
