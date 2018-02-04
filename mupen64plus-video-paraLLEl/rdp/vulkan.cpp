#include "vulkan.hpp"
#include "vulkan_symbol_wrapper.h"
#include <vector>

#ifdef PARALLEL_HAVE_DYLIB
#include <dlfcn.h>
#endif

#include <stdexcept>

using namespace std;

namespace Vulkan
{
VulkanContext::VulkanContext()
    : owned_instance(true)
    , owned_device(true)
{
	if (!create_instance())
	{
		destroy();
		throw runtime_error("Failed to create Vulkan instance.");
	}

	VkPhysicalDeviceFeatures features = {};
	if (!create_device(VK_NULL_HANDLE, VK_NULL_HANDLE, nullptr, 0, nullptr, 0, &features))
	{
		destroy();
		throw runtime_error("Failed to create Vulkan device.");
	}
}

bool VulkanContext::init_loader(PFN_vkGetInstanceProcAddr addr)
{
#ifdef PARALLEL_HAVE_DYLIB
	static void *dylib;
	if (dylib)
		return true;

	if (!addr)
	{
		dylib = dlopen("libvulkan.so", RTLD_NOW);
		if (!dylib)
			return false;

		addr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(dylib, "vkGetInstanceProcAddr"));
		if (!addr)
			return false;
	}
#endif

	vulkan_symbol_wrapper_init(addr);
	return vulkan_symbol_wrapper_load_global_symbols();
}

VulkanContext::VulkanContext(VkInstance instance, VkPhysicalDevice gpu, VkDevice device, VkQueue queue,
                             uint32_t queue_family)
    : device(device)
    , instance(instance)
    , gpu(gpu)
    , queue(queue)
    , queue_family(queue_family)
    , alt_queue(queue)
    , alt_queue_family(queue_family)
    , owned_instance(false)
    , owned_device(false)
{
	vulkan_symbol_wrapper_load_core_instance_symbols(instance);
	vulkan_symbol_wrapper_load_core_device_symbols(device);

	vkGetPhysicalDeviceProperties(gpu, &gpu_props);
	vkGetPhysicalDeviceMemoryProperties(gpu, &mem_props);
}

VulkanContext::VulkanContext(VkInstance instance, VkPhysicalDevice gpu, VkSurfaceKHR surface,
                             const char **required_device_extensions, unsigned num_required_device_extensions,
                             const char **required_device_layers, unsigned num_required_device_layers,
                             const VkPhysicalDeviceFeatures *required_features)
    : instance(instance)
    , owned_instance(false)
    , owned_device(true)
{
	vulkan_symbol_wrapper_load_core_instance_symbols(instance);
	if (!create_device(gpu, surface, required_device_extensions, num_required_device_extensions, required_device_layers,
	                   num_required_device_layers, required_features))
	{
		destroy();
		throw runtime_error("Failed to create Vulkan device.");
	}
}

void VulkanContext::destroy()
{
	if (device != VK_NULL_HANDLE)
		vkDeviceWaitIdle(device);

#ifdef VULKAN_DEBUG
	if (debug_callback)
		vkDestroyDebugReportCallbackEXT(instance, debug_callback, nullptr);
#endif

	if (owned_device && device != VK_NULL_HANDLE)
		vkDestroyDevice(device, nullptr);
	if (owned_instance && instance != VK_NULL_HANDLE)
		vkDestroyInstance(instance, nullptr);
}

VulkanContext::~VulkanContext()
{
	destroy();
}

const VkApplicationInfo &VulkanContext::get_application_info()
{
	static const VkApplicationInfo info = {
		VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr, "paraLLEl RDP", 0, "paraLLEl RDP", 0, VK_MAKE_VERSION(1, 0, 18),
	};
	return info;
}

#ifdef VULKAN_DEBUG
static VKAPI_ATTR VkBool32 VKAPI_CALL vulkan_debug_cb(VkDebugReportFlagsEXT flags,
                                                      VkDebugReportObjectTypeEXT objectType, uint64_t object,
                                                      size_t location, int32_t messageCode, const char *pLayerPrefix,
                                                      const char *pMessage, void *pUserData)
{
	(void)objectType;
	(void)object;
	(void)location;
	(void)messageCode;
	(void)pUserData;

	if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT)
	{
		fprintf(stderr, "[Vulkan]: Error: %s: %s\n", pLayerPrefix, pMessage);
	}
	else if (flags & VK_DEBUG_REPORT_WARNING_BIT_EXT)
	{
		fprintf(stderr, "[Vulkan]: Warning: %s: %s\n", pLayerPrefix, pMessage);
	}
	else if (flags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT)
	{
		fprintf(stderr, "[Vulkan]: Performance warning: %s: %s\n", pLayerPrefix, pMessage);
	}
	else
	{
		fprintf(stderr, "[Vulkan]: Information: %s: %s\n", pLayerPrefix, pMessage);
	}

	return VK_FALSE;
}
#endif

bool VulkanContext::create_instance()
{
	VkInstanceCreateInfo info = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
	info.pApplicationInfo = &get_application_info();

#ifdef VULKAN_DEBUG
	static const char *instance_extensions[] = { "VK_EXT_debug_report" };
	static const char *instance_layers[] = { "VK_LAYER_LUNARG_standard_validation" };
	info.enabledExtensionCount = 1;
	info.ppEnabledExtensionNames = instance_extensions;
	info.enabledLayerCount = 1;
	info.ppEnabledLayerNames = instance_layers;
#endif

	if (vkCreateInstance(&info, nullptr, &instance) != VK_SUCCESS)
		return false;

	vulkan_symbol_wrapper_load_core_instance_symbols(instance);

#ifdef VULKAN_DEBUG
	VULKAN_SYMBOL_WRAPPER_LOAD_INSTANCE_EXTENSION_SYMBOL(instance, vkCreateDebugReportCallbackEXT);
	VULKAN_SYMBOL_WRAPPER_LOAD_INSTANCE_EXTENSION_SYMBOL(instance, vkDebugReportMessageEXT);
	VULKAN_SYMBOL_WRAPPER_LOAD_INSTANCE_EXTENSION_SYMBOL(instance, vkDestroyDebugReportCallbackEXT);

	{
		VkDebugReportCallbackCreateInfoEXT info = { VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT };
		info.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT |
		             VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
		info.pfnCallback = vulkan_debug_cb;
		vkCreateDebugReportCallbackEXT(instance, &info, NULL, &debug_callback);
	}
#endif
	return true;
}

bool VulkanContext::create_device(VkPhysicalDevice gpu, VkSurfaceKHR surface, const char **required_device_extensions,
                                  unsigned num_required_device_extensions, const char **required_device_layers,
                                  unsigned num_required_device_layers,
                                  const VkPhysicalDeviceFeatures *required_features)
{
	if (gpu == VK_NULL_HANDLE)
	{
		uint32_t gpu_count = 0;
		V(vkEnumeratePhysicalDevices(instance, &gpu_count, nullptr));
		vector<VkPhysicalDevice> gpus(gpu_count);
		V(vkEnumeratePhysicalDevices(instance, &gpu_count, gpus.data()));
		gpu = gpus.front();
	}

	this->gpu = gpu;
	vkGetPhysicalDeviceProperties(gpu, &gpu_props);
	vkGetPhysicalDeviceMemoryProperties(gpu, &mem_props);

	uint32_t queue_count;
	vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queue_count, nullptr);
	vector<VkQueueFamilyProperties> queue_props(queue_count);
	vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queue_count, queue_props.data());

	if (surface != VK_NULL_HANDLE)
	{
		VULKAN_SYMBOL_WRAPPER_LOAD_INSTANCE_EXTENSION_SYMBOL(instance, vkGetPhysicalDeviceSurfaceSupportKHR);
	}

	// First, find a graphics capable queue.
	// This will be our alt_queue if we need a different queue family to do async compute.
	bool found_queue = false;
	for (unsigned i = 0; i < queue_count; i++)
	{
		VkBool32 supported = surface == VK_NULL_HANDLE;
		if (surface != VK_NULL_HANDLE)
			vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface, &supported);

		VkQueueFlags required = VK_QUEUE_COMPUTE_BIT | VK_QUEUE_GRAPHICS_BIT;
		if (supported && ((queue_props[i].queueFlags & required) == required))
		{
			found_queue = true;
			queue_family = i;
			break;
		}
	}

	if (!found_queue)
		return false;

	bool same_queue_async = false;
	bool async_compute = false;

	if (queue_props[queue_family].queueCount >= 2)
		same_queue_async = true;

	if (same_queue_async)
	{
		alt_queue_family = queue_family;
		async_compute = true;
	}
	else
	{
		found_queue = false;
		for (unsigned i = 0; i < queue_count; i++)
		{
			if (i == queue_family)
				continue;

			VkQueueFlags required = VK_QUEUE_COMPUTE_BIT;
			if ((queue_props[i].queueFlags & required) == required)
			{
				alt_queue_family = i;
				async_compute = true;
				found_queue = true;
				break;
			}
		}
	}

	static const float prios[] = { 0.5f, 0.5f };
	VkDeviceQueueCreateInfo queue_info[2] = {
		{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO }, { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO },
	};

	VkDeviceCreateInfo device_info = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
	device_info.pQueueCreateInfos = queue_info;

	if (async_compute && same_queue_async)
	{
		device_info.queueCreateInfoCount = 1;
		queue_info[0].queueFamilyIndex = queue_family;
		queue_info[0].queueCount = 2;
		queue_info[0].pQueuePriorities = prios;
	}
	else if (async_compute && !same_queue_async)
	{
		// alt_queue is actually the "external" graphics queue.
		swap(alt_queue_family, queue_family);

		device_info.queueCreateInfoCount = 2;
		queue_info[0].queueFamilyIndex = alt_queue_family;
		queue_info[0].queueCount = 1;
		queue_info[0].pQueuePriorities = &prios[0];
		queue_info[1].queueFamilyIndex = queue_family;
		queue_info[1].queueCount = 1;
		queue_info[1].pQueuePriorities = &prios[1];
	}
	else
	{
		static const float one = 1.0f;
		queue_info[0].queueFamilyIndex = queue_family;
		queue_info[0].queueCount = 1;
		queue_info[0].pQueuePriorities = &one;
		device_info.queueCreateInfoCount = 1;
	}

	// Should query for these, but no big deal for now.
	device_info.ppEnabledExtensionNames = required_device_extensions;
	device_info.enabledExtensionCount = num_required_device_extensions;
	device_info.ppEnabledLayerNames = required_device_layers;
	device_info.enabledLayerCount = num_required_device_layers;
	device_info.pEnabledFeatures = required_features;

#ifdef VULKAN_DEBUG
	static const char *device_layers[] = { "VK_LAYER_LUNARG_standard_validation" };
	device_info.enabledLayerCount = 1;
	device_info.ppEnabledLayerNames = device_layers;
#endif

	if (vkCreateDevice(gpu, &device_info, nullptr, &device) != VK_SUCCESS)
		return false;

	vulkan_symbol_wrapper_load_core_device_symbols(device);

	if (async_compute && same_queue_async)
	{
		vkGetDeviceQueue(device, queue_family, 0, &alt_queue);
		vkGetDeviceQueue(device, queue_family, 1, &queue);
	}
	else if (async_compute && !same_queue_async)
	{
		vkGetDeviceQueue(device, alt_queue_family, 0, &alt_queue);
		vkGetDeviceQueue(device, queue_family, 0, &queue);
	}
	else
	{
		vkGetDeviceQueue(device, queue_family, 0, &queue);
		alt_queue = queue;
		alt_queue_family = queue_family;
	}

	return true;
}
}
