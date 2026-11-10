#ifndef VK_DEVICE_H
#define VK_DEVICE_H

/* The device GLideN64's Vulkan backend needs, asked of the frontend through
 * context negotiation. C89. */

#define VK_NO_PROTOTYPES
#include <boolean.h>
#include <vulkan/vulkan.h>
#include <libretro_vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

const VkApplicationInfo *gliden64_vulkan_get_application_info(void);

bool gliden64_vulkan_create_device(struct retro_vulkan_context *context,
      VkInstance instance, VkPhysicalDevice gpu, VkSurfaceKHR surface,
      PFN_vkGetInstanceProcAddr get_instance_proc_addr,
      const char **required_device_extensions, unsigned num_required_device_extensions,
      const char **required_device_layers, unsigned num_required_device_layers,
      const VkPhysicalDeviceFeatures *required_features);

bool gliden64_vulkan_create_device2(struct retro_vulkan_context *context,
      VkInstance instance, VkPhysicalDevice gpu, VkSurfaceKHR surface,
      PFN_vkGetInstanceProcAddr get_instance_proc_addr,
      retro_vulkan_create_device_wrapper_t create_device_wrapper, void *opaque);

/* Set by the frontend glue at each context_reset. */
void gliden64_vulkan_set_interface(const struct retro_hw_render_interface_vulkan *vk);
const struct retro_hw_render_interface_vulkan *gliden64_vulkan_interface(void);

/* Nonzero when this is the backend the content runs on. */
void gliden64_vulkan_enable(int on);
int  gliden64_vulkan_enabled(void);

/* The negotiated device has pixel interlock. */
int  gliden64_vulkan_has_interlock(void);

/* The negotiated device has blending and depth clamp as dynamic state. */
int  gliden64_vulkan_has_dynamic_blend(void);
int  gliden64_vulkan_has_store_none(void);
int  gliden64_vulkan_has_depth_resolve(void);

#ifdef __cplusplus
}
#endif

#endif
