#ifndef PARALLEL_H__
#define PARALLEL_H__

#include <vulkan/vulkan.h>
#include "api/libretro.h"
#include "api/libretro_vulkan.h"

#ifdef __cplusplus
extern "C" {
#endif

bool parallel_init(const struct retro_hw_render_interface_vulkan *vulkan);
void parallel_deinit(void);
bool parallel_frame_is_valid(void);
unsigned parallel_frame_width(void);
unsigned parallel_frame_height(void);
void parallel_begin_frame(void);
void parallel_set_synchronous_rdp(bool enable);

const VkApplicationInfo *parallel_get_application_info(void);
bool parallel_create_device(struct retro_vulkan_context *context,
      VkInstance instance,
      VkPhysicalDevice gpu,
      VkSurfaceKHR surface,
      PFN_vkGetInstanceProcAddr get_instance_proc_addr,
      const char **required_device_extensions,
      unsigned num_required_device_extensions,
      const char **required_device_layers,
      unsigned num_required_device_layers,
      const VkPhysicalDeviceFeatures *required_features);

#ifdef __cplusplus
}
#endif

#endif
