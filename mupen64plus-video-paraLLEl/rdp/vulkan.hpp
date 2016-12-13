#ifndef VULKAN_HPP
#define VULKAN_HPP

#include "vulkan_symbol_wrapper.h"
#include <memory>
#include <stdexcept>

#define STRINGIFY(x) #x

#define V(x)                                                                                           \
	do                                                                                                 \
	{                                                                                                  \
		VkResult err = x;                                                                              \
		if (err != VK_SUCCESS && err != VK_INCOMPLETE)                                                 \
			throw std::runtime_error("Vulkan call failed at " __FILE__ ":" STRINGIFY(__LINE__) ".\n"); \
	} while (0)

namespace Vulkan
{
class VulkanContext
{
public:
	VulkanContext();
	VulkanContext(VkInstance instance, VkPhysicalDevice gpu, VkDevice device, VkQueue queue, uint32_t queue_family);
	VulkanContext(VkInstance instance, VkPhysicalDevice gpu, VkSurfaceKHR surface,
	              const char **required_device_extensions, unsigned num_required_device_extensions,
	              const char **required_device_layers, unsigned num_required_device_layers,
	              const VkPhysicalDeviceFeatures *required_features);

	VulkanContext(const VulkanContext &) = delete;
	void operator=(const VulkanContext &) = delete;

	static bool init_loader(PFN_vkGetInstanceProcAddr get_instance_proc_addr = nullptr);

	~VulkanContext();

	VkInstance get_instance() const
	{
		return instance;
	}

	VkPhysicalDevice get_gpu() const
	{
		return gpu;
	}

	VkDevice get_device() const
	{
		return device;
	}

	VkQueue get_queue() const
	{
		return queue;
	}

	VkQueue get_alt_queue() const
	{
		return alt_queue;
	}

	const VkPhysicalDeviceProperties &get_gpu_props() const
	{
		return gpu_props;
	}

	const VkPhysicalDeviceMemoryProperties &get_mem_props() const
	{
		return mem_props;
	}

	uint32_t get_queue_family() const
	{
		return queue_family;
	}

	uint32_t get_alt_queue_family() const
	{
		return alt_queue_family;
	}

	void release_instance()
	{
		owned_instance = false;
	}

	void release_device()
	{
		owned_device = false;
	}

	static const VkApplicationInfo &get_application_info();

private:
	VkDevice device = VK_NULL_HANDLE;
	VkInstance instance = VK_NULL_HANDLE;
	VkPhysicalDevice gpu = VK_NULL_HANDLE;

	VkPhysicalDeviceProperties gpu_props;
	VkPhysicalDeviceMemoryProperties mem_props;

	VkQueue queue = VK_NULL_HANDLE;
	uint32_t queue_family = VK_QUEUE_FAMILY_IGNORED;
	VkQueue alt_queue = VK_NULL_HANDLE;
	uint32_t alt_queue_family = VK_QUEUE_FAMILY_IGNORED;

	bool create_instance();
	bool create_device(VkPhysicalDevice gpu, VkSurfaceKHR surface, const char **required_device_extensions,
	                   unsigned num_required_device_extensions, const char **required_device_layers,
	                   unsigned num_required_device_layers, const VkPhysicalDeviceFeatures *required_features);

	bool owned_instance = false;
	bool owned_device = false;

#ifdef VULKAN_DEBUG
	VkDebugReportCallbackEXT debug_callback = VK_NULL_HANDLE;
#endif

	void destroy();
};
}

#endif
