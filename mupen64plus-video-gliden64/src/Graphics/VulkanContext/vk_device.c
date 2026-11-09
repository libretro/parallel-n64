#include <stdlib.h>
#include <string.h>

#include <boolean.h>

#include "vk_device.h"

static const struct retro_hw_render_interface_vulkan *g_interface;
static int g_enabled;
static int g_interlock;
static int g_dynamic_blend;

void gliden64_vulkan_set_interface(const struct retro_hw_render_interface_vulkan *vk) { g_interface = vk; }
const struct retro_hw_render_interface_vulkan *gliden64_vulkan_interface(void) { return g_interface; }
void gliden64_vulkan_enable(int on) { g_enabled = on; }
int gliden64_vulkan_enabled(void) { return g_enabled; }
int gliden64_vulkan_has_interlock(void) { return g_interlock; }

const VkApplicationInfo *gliden64_vulkan_get_application_info(void)
{
   static VkApplicationInfo info;
   info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
   info.pApplicationName = "parallel-n64";
   info.apiVersion = VK_API_VERSION_1_1;
   return &info;
}

#define MAX_EXTS 32

static int have_extension(const VkExtensionProperties *ext, uint32_t n, const char *name)
{
   uint32_t i;
   for (i = 0; i < n; i++)
      if (strcmp(ext[i].extensionName, name) == 0)
         return 1;
   return 0;
}

static void add_extension(const char **list, unsigned *n, const char *name)
{
   unsigned i;
   for (i = 0; i < *n; i++)
      if (strcmp(list[i], name) == 0)
         return;
   if (*n < MAX_EXTS)
      list[(*n)++] = name;
}

static bool create_device(struct retro_vulkan_context *context,
      VkInstance instance, VkPhysicalDevice gpu, VkSurfaceKHR surface,
      PFN_vkGetInstanceProcAddr get,
      retro_vulkan_create_device_wrapper_t wrapper, void *opaque,
      const char **required_exts, unsigned num_required_exts,
      const char **required_layers, unsigned num_required_layers,
      const VkPhysicalDeviceFeatures *required_features)
{
#define LOAD(T, N) T N = (T)get(instance, #N)
   LOAD(PFN_vkEnumeratePhysicalDevices, vkEnumeratePhysicalDevices);
   LOAD(PFN_vkGetPhysicalDeviceFeatures, vkGetPhysicalDeviceFeatures);
   LOAD(PFN_vkGetPhysicalDeviceQueueFamilyProperties, vkGetPhysicalDeviceQueueFamilyProperties);
   LOAD(PFN_vkEnumerateDeviceExtensionProperties, vkEnumerateDeviceExtensionProperties);
   LOAD(PFN_vkGetPhysicalDeviceSurfaceSupportKHR, vkGetPhysicalDeviceSurfaceSupportKHR);
   LOAD(PFN_vkCreateDevice, vkCreateDevice);
   LOAD(PFN_vkGetDeviceProcAddr, vkGetDeviceProcAddr);
#undef LOAD
   PFN_vkGetPhysicalDeviceFeatures2 features2 = (PFN_vkGetPhysicalDeviceFeatures2)get(instance, "vkGetPhysicalDeviceFeatures2");
   PFN_vkGetDeviceQueue get_queue;
   VkPhysicalDeviceFeatures have, want;
   VkQueueFamilyProperties fam[16];
   VkExtensionProperties *ext;
   uint32_t n_fam = 16, n_ext = 0, i, family = VK_QUEUE_FAMILY_IGNORED;
   const char *exts[MAX_EXTS];
   unsigned n_exts = 0;
   float priority = 1.f;
   VkDeviceQueueCreateInfo qi;
   VkDeviceCreateInfo di;
   VkPhysicalDeviceExtendedDynamicStateFeaturesEXT eds;
   VkPhysicalDeviceExtendedDynamicState3FeaturesEXT eds3, eds3_have;
   VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT interlock, interlock_have;
   VkPhysicalDeviceFeatures2 query;
   void *chain = NULL;
   VkDevice device;

   if (gpu == VK_NULL_HANDLE)
   {
      uint32_t n = 1;
      if (vkEnumeratePhysicalDevices(instance, &n, &gpu) < 0 || n == 0)
         return false;
   }

   g_interlock = 0;
   g_dynamic_blend = 0;
   vkGetPhysicalDeviceFeatures(gpu, &have);
   /* the N64's near plane needs these: without them there is no backend */
   if (!have.depthClamp || !have.shaderClipDistance)
      return false;

   vkGetPhysicalDeviceQueueFamilyProperties(gpu, &n_fam, fam);
   for (i = 0; i < n_fam; i++)
   {
      VkBool32 present = VK_TRUE;
      if (surface != VK_NULL_HANDLE)
         vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface, &present);
      if ((fam[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present)
      {
         family = i;
         break;
      }
   }
   if (family == VK_QUEUE_FAMILY_IGNORED)
      return false;

   vkEnumerateDeviceExtensionProperties(gpu, NULL, &n_ext, NULL);
   ext = (VkExtensionProperties*)malloc(n_ext * sizeof(*ext));
   if (!ext)
      return false;
   vkEnumerateDeviceExtensionProperties(gpu, NULL, &n_ext, ext);

   for (i = 0; i < num_required_exts; i++)
      add_extension(exts, &n_exts, required_exts[i]);
   /* drawn with a flipped viewport into the presented image */
   if (have_extension(ext, n_ext, VK_KHR_MAINTENANCE1_EXTENSION_NAME))
      add_extension(exts, &n_exts, VK_KHR_MAINTENANCE1_EXTENSION_NAME);

   memset(&eds, 0, sizeof(eds));
   memset(&interlock, 0, sizeof(interlock));
   if (have_extension(ext, n_ext, VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME))
   {
      add_extension(exts, &n_exts, VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME);
      eds.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
      eds.extendedDynamicState = VK_TRUE;
      eds.pNext = chain;
      chain = &eds;
   }
   /* what the extensions offer: an extension does not promise its features */
   memset(&eds3, 0, sizeof(eds3));
   memset(&eds3_have, 0, sizeof(eds3_have));
   memset(&interlock_have, 0, sizeof(interlock_have));
   memset(&query, 0, sizeof(query));
   eds3_have.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT;
   interlock_have.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_INTERLOCK_FEATURES_EXT;
   eds3_have.pNext = &interlock_have;
   query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
   query.pNext = &eds3_have;
   if (features2)
      features2(gpu, &query);

   /* blending and depth clamp as dynamic state take them out of the pipeline
    * key: fewer pipelines to build, and fewer binds */
   if (have_extension(ext, n_ext, VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME)
         && eds.extendedDynamicState
         && eds3_have.extendedDynamicState3ColorBlendEnable
         && eds3_have.extendedDynamicState3ColorBlendEquation
         && eds3_have.extendedDynamicState3DepthClampEnable)
   {
      add_extension(exts, &n_exts, VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
      eds3.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT;
      eds3.extendedDynamicState3ColorBlendEnable = VK_TRUE;
      eds3.extendedDynamicState3ColorBlendEquation = VK_TRUE;
      eds3.extendedDynamicState3DepthClampEnable = VK_TRUE;
      eds3.pNext = chain;
      chain = &eds3;
      g_dynamic_blend = 1;
   }
   if (have_extension(ext, n_ext, VK_EXT_FRAGMENT_SHADER_INTERLOCK_EXTENSION_NAME)
         && interlock_have.fragmentShaderPixelInterlock)
   {
      add_extension(exts, &n_exts, VK_EXT_FRAGMENT_SHADER_INTERLOCK_EXTENSION_NAME);
      interlock.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_INTERLOCK_FEATURES_EXT;
      interlock.fragmentShaderPixelInterlock = VK_TRUE;
      interlock.pNext = chain;
      chain = &interlock;
      g_interlock = 1;
   }
   free(ext);

   memset(&want, 0, sizeof(want));
   if (required_features)
      want = *required_features;
   want.depthClamp = VK_TRUE;
   want.shaderClipDistance = VK_TRUE;
   want.samplerAnisotropy = have.samplerAnisotropy;
   want.fragmentStoresAndAtomics = have.fragmentStoresAndAtomics;
   want.wideLines = have.wideLines;
   want.dualSrcBlend = have.dualSrcBlend;

   memset(&qi, 0, sizeof(qi));
   qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
   qi.queueFamilyIndex = family;
   qi.queueCount = 1;
   qi.pQueuePriorities = &priority;

   memset(&di, 0, sizeof(di));
   di.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
   di.pNext = chain;
   di.queueCreateInfoCount = 1;
   di.pQueueCreateInfos = &qi;
   di.enabledExtensionCount = n_exts;
   di.ppEnabledExtensionNames = exts;
   di.enabledLayerCount = num_required_layers;
   di.ppEnabledLayerNames = required_layers;
   di.pEnabledFeatures = &want;
   if (wrapper)
   {
      device = wrapper(gpu, opaque, &di);
      if (device == VK_NULL_HANDLE)
         return false;
   }
   else if (vkCreateDevice(gpu, &di, NULL, &device) != VK_SUCCESS)
      return false;

   get_queue = (PFN_vkGetDeviceQueue)vkGetDeviceProcAddr(device, "vkGetDeviceQueue");
   context->gpu = gpu;
   context->device = device;
   get_queue(device, family, 0, &context->queue);
   context->queue_family_index = family;
   context->presentation_queue = context->queue;
   context->presentation_queue_family_index = family;
   return true;
}

bool gliden64_vulkan_create_device(struct retro_vulkan_context *context,
      VkInstance instance, VkPhysicalDevice gpu, VkSurfaceKHR surface,
      PFN_vkGetInstanceProcAddr get,
      const char **required_exts, unsigned num_required_exts,
      const char **required_layers, unsigned num_required_layers,
      const VkPhysicalDeviceFeatures *required_features)
{
   return create_device(context, instance, gpu, surface, get, NULL, NULL,
         required_exts, num_required_exts, required_layers, num_required_layers, required_features);
}

bool gliden64_vulkan_create_device2(struct retro_vulkan_context *context,
      VkInstance instance, VkPhysicalDevice gpu, VkSurfaceKHR surface,
      PFN_vkGetInstanceProcAddr get,
      retro_vulkan_create_device_wrapper_t wrapper, void *opaque)
{
   return create_device(context, instance, gpu, surface, get, wrapper, opaque, NULL, 0, NULL, 0, NULL);
}

int gliden64_vulkan_has_dynamic_blend(void)
{
   return g_dynamic_blend;
}
