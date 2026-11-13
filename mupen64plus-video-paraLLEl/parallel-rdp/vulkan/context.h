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

#ifndef GRANITE_CONTEXT_H
#define GRANITE_CONTEXT_H

#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>
#include "volk.h"

RETRO_BEGIN_DECLS

/* The queues work is submitted to. Two or all three may be the same
 * queue. */
enum granite_queue_index
{
	GRANITE_QUEUE_GRAPHICS = 0,
	GRANITE_QUEUE_COMPUTE,
	GRANITE_QUEUE_TRANSFER,
	GRANITE_QUEUE_COUNT
};

#define GRANITE_CONTEXT_MAX_QUEUE_FAMILIES 32

typedef struct granite_queue_info
{
	VkQueue queues[GRANITE_QUEUE_COUNT];
	uint32_t family_indices[GRANITE_QUEUE_COUNT];
} granite_queue_info_t;

/* What the device was created with */
typedef struct granite_device_features
{
	bool supports_push_descriptor;
	bool supports_external_memory_host;
	bool supports_driver_properties;
	bool supports_memory_budget;
	bool supports_format_feature_flags2;
	bool supports_image_format_list;
	/* the driver can list attached tools; the extension is not enabled */
	bool supports_tooling_info;

	VkPhysicalDeviceFeatures enabled_features;
	VkPhysicalDeviceSubgroupProperties subgroup_properties;

	VkPhysicalDeviceTimelineSemaphoreFeaturesKHR timeline_semaphore_features;
	VkPhysicalDeviceDriverPropertiesKHR driver_properties;
	VkPhysicalDevice8BitStorageFeaturesKHR storage_8bit_features;
	VkPhysicalDevice16BitStorageFeaturesKHR storage_16bit_features;
	VkPhysicalDeviceFloat16Int8FeaturesKHR float16_int8_features;
	VkPhysicalDevicePushDescriptorPropertiesKHR push_descriptor_properties;

	VkPhysicalDeviceExternalMemoryHostPropertiesEXT host_memory_properties;
	VkPhysicalDeviceSubgroupSizeControlFeaturesEXT subgroup_size_control_features;
	VkPhysicalDeviceSubgroupSizeControlPropertiesEXT subgroup_size_control_properties;
	VkPhysicalDeviceMemoryPriorityFeaturesEXT memory_priority_features;
	VkPhysicalDevicePipelineCreationCacheControlFeaturesEXT pipeline_creation_cache_control_features;
} granite_device_features_t;

/* Creates the device in place of vkCreateDevice: a libretro frontend
 * adds what it needs to the create info. VK_NULL_HANDLE when it fails. */
typedef VkDevice (*granite_create_device_t)(void *user, VkPhysicalDevice gpu, const VkDeviceCreateInfo *info);

/* A device on the frontend's instance, and what is known about it */
typedef struct granite_context
{
	VkInstance instance;
	VkPhysicalDevice gpu;
	VkDevice device;
	struct VolkDeviceTable device_table;
	VkPhysicalDeviceProperties gpu_props;
	VkPhysicalDeviceMemoryProperties mem_props;
	granite_queue_info_t queue_info;
	granite_device_features_t ext;
	/* the device is destroyed with the context */
	bool owned_device;
} granite_context_t;

/* Before anything else: where Vulkan's functions come from. False
 * without one. */
bool granite_context_init_loader(PFN_vkGetInstanceProcAddr addr);

/* Creates a device on @instance: on @gpu, or on the best one there is
 * when @gpu is VK_NULL_HANDLE. The graphics queue can present to
 * @surface when there is one. @extensions are enabled with the ones the
 * renderer uses where the device has them, @required_features with the
 * ones it uses. Vulkan 1.1 at least. False, and no device, when it
 * cannot be done. */
bool granite_context_init(granite_context_t *context, VkInstance instance, VkPhysicalDevice gpu,
		VkSurfaceKHR surface, const char **extensions, unsigned num_extensions,
		const VkPhysicalDeviceFeatures *required_features, granite_create_device_t create_device, void *user);
/* Waits for the device to be idle, and destroys it when it is owned */
void granite_context_destroy(granite_context_t *context);

/* 0 for a device that cannot be used; the higher the better */
unsigned granite_context_device_score(const VkPhysicalDeviceProperties *props);

typedef struct granite_queue_pick
{
	uint32_t family[GRANITE_QUEUE_COUNT];
	/* which of the family's queues */
	uint32_t index[GRANITE_QUEUE_COUNT];
	/* how many queues to create of each family, and their priorities */
	uint32_t counts[GRANITE_CONTEXT_MAX_QUEUE_FAMILIES];
	float priorities[GRANITE_CONTEXT_MAX_QUEUE_FAMILIES][GRANITE_QUEUE_COUNT];
} granite_queue_pick_t;

/* Picks the queues among @count families (no more than
 * GRANITE_CONTEXT_MAX_QUEUE_FAMILIES are looked at). Graphics: a queue
 * that does graphics and compute and, with @can_present, can present.
 * Compute: another such queue, or a compute queue, else the graphics
 * queue. Transfer: a queue that only transfers, or a compute queue that
 * does no graphics, else the compute queue. @single_queue: graphics and
 * compute are one queue whatever there is. False without a graphics
 * queue. */
bool granite_context_pick_queues(const VkQueueFamilyProperties *props, uint32_t count,
		const bool *can_present, bool single_queue, granite_queue_pick_t *pick);

RETRO_END_DECLS

#endif
