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

#include <stdlib.h>
#include <string.h>
#include "context.h"
#include "logging.h"

bool granite_context_init_loader(PFN_vkGetInstanceProcAddr addr)
{
	if (!addr)
		return false;
	volkInitializeCustom(addr);
	return true;
}

unsigned granite_context_device_score(const VkPhysicalDeviceProperties *props)
{
	if (props->apiVersion < VK_API_VERSION_1_1)
		return 0;

	switch (props->deviceType)
	{
		case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
			return 3;
		case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
			return 2;
		case VK_PHYSICAL_DEVICE_TYPE_CPU:
			return 1;
		default:
			return 0;
	}
}

/* The best device of the instance; of equals, the first */
static VkPhysicalDevice pick_gpu(VkInstance instance)
{
	VkPhysicalDevice *gpus;
	VkPhysicalDevice best = VK_NULL_HANDLE;
	uint32_t count = 0, i;
	unsigned best_score = 0;

	if (vkEnumeratePhysicalDevices(instance, &count, NULL) != VK_SUCCESS || !count)
		return VK_NULL_HANDLE;
	gpus = (VkPhysicalDevice*)malloc(count * sizeof(*gpus));
	if (!gpus)
		return VK_NULL_HANDLE;
	if (vkEnumeratePhysicalDevices(instance, &count, gpus) != VK_SUCCESS)
		count = 0;

	for (i = 0; i < count; i++)
	{
		VkPhysicalDeviceProperties props;
		unsigned score;
		memset(&props, 0, sizeof(props));
		vkGetPhysicalDeviceProperties(gpus[i], &props);
		score = granite_context_device_score(&props);
		if (best == VK_NULL_HANDLE || score > best_score)
		{
			best = gpus[i];
			best_score = score;
		}
	}

	free(gpus);
	return best;
}

/* Takes one of the queues left of a family that has all of @required
 * and none of @ignore */
static bool take_queue(const VkQueueFamilyProperties *props, uint32_t count, const bool *can_present,
		uint32_t *left, granite_queue_pick_t *pick, unsigned which,
		VkQueueFlags required, VkQueueFlags ignore, float priority)
{
	uint32_t family;

	for (family = 0; family < count; family++)
	{
		if (props[family].queueFlags & ignore)
			continue;
		/* a graphics queue has to present */
		if ((required & VK_QUEUE_GRAPHICS_BIT) && can_present && !can_present[family])
			continue;
		if (!left[family] || (props[family].queueFlags & required) != required)
			continue;

		left[family]--;
		pick->family[which] = family;
		pick->index[which] = pick->counts[family];
		pick->priorities[family][pick->counts[family]] = priority;
		pick->counts[family]++;
		return true;
	}

	return false;
}

bool granite_context_pick_queues(const VkQueueFamilyProperties *props, uint32_t count,
		const bool *can_present, bool single_queue, granite_queue_pick_t *pick)
{
	uint32_t left[GRANITE_CONTEXT_MAX_QUEUE_FAMILIES];
	uint32_t i;

	if (count > GRANITE_CONTEXT_MAX_QUEUE_FAMILIES)
		count = GRANITE_CONTEXT_MAX_QUEUE_FAMILIES;
	memset(pick, 0, sizeof(*pick));
	for (i = 0; i < count; i++)
		left[i] = props[i].queueCount;

	if (!take_queue(props, count, can_present, left, pick, GRANITE_QUEUE_GRAPHICS,
				VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT, 0, 0.5f))
		return false;

	/* the compute queue does the heavy work: it comes first */
	if (     single_queue
			|| (  !take_queue(props, count, can_present, left, pick, GRANITE_QUEUE_COMPUTE,
						VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT, 0, 1.0f)
				&& !take_queue(props, count, can_present, left, pick, GRANITE_QUEUE_COMPUTE,
						VK_QUEUE_COMPUTE_BIT, 0, 1.0f)))
	{
		pick->family[GRANITE_QUEUE_COMPUTE] = pick->family[GRANITE_QUEUE_GRAPHICS];
		pick->index[GRANITE_QUEUE_COMPUTE] = pick->index[GRANITE_QUEUE_GRAPHICS];
	}

	if (     !take_queue(props, count, can_present, left, pick, GRANITE_QUEUE_TRANSFER,
				VK_QUEUE_TRANSFER_BIT, VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT, 0.5f)
			&& !take_queue(props, count, can_present, left, pick, GRANITE_QUEUE_TRANSFER,
				VK_QUEUE_COMPUTE_BIT, VK_QUEUE_GRAPHICS_BIT, 0.5f))
	{
		pick->family[GRANITE_QUEUE_TRANSFER] = pick->family[GRANITE_QUEUE_COMPUTE];
		pick->index[GRANITE_QUEUE_TRANSFER] = pick->index[GRANITE_QUEUE_COMPUTE];
	}

	return true;
}

static bool has_extension(const VkExtensionProperties *available, uint32_t count, const char *name)
{
	uint32_t i;
	for (i = 0; i < count; i++)
		if (!strcmp(available[i].extensionName, name))
			return true;
	return false;
}

/* What the renderer enables where the device has it */
#define MAX_OPTIONAL_EXTENSIONS 16

typedef struct extension_list
{
	const char **names;
	uint32_t count;
} extension_list_t;

static void enable_extension(extension_list_t *list, const char *name)
{
	uint32_t i;
	for (i = 0; i < list->count; i++)
		if (!strcmp(list->names[i], name))
			return;
	list->names[list->count++] = name;
}

/* Appends a structure to a pNext chain; returns the new tail */
static void **chain(void **tail, void *structure)
{
	*tail = structure;
	return (void**)&((VkBaseOutStructure*)structure)->pNext;
}

bool granite_context_init(granite_context_t *context, VkInstance instance, VkPhysicalDevice gpu,
		VkSurfaceKHR surface, const char **extensions, unsigned num_extensions,
		const VkPhysicalDeviceFeatures *required_features, granite_create_device_t create_device, void *user)
{
	VkQueueFamilyProperties queue_props[GRANITE_CONTEXT_MAX_QUEUE_FAMILIES];
	bool can_present[GRANITE_CONTEXT_MAX_QUEUE_FAMILIES];
	VkDeviceQueueCreateInfo queue_infos[GRANITE_CONTEXT_MAX_QUEUE_FAMILIES];
	granite_queue_pick_t pick;
	granite_device_features_t *ext = &context->ext;
	VkExtensionProperties *available = NULL;
	extension_list_t enabled;
	VkPhysicalDeviceProperties2 gpu_props2;
	VkPhysicalDeviceProperties2 props;
	VkPhysicalDeviceFeatures2 features;
	VkPhysicalDeviceScalarBlockLayoutFeaturesEXT scalar_block_features;
	VkPhysicalDeviceUniformBufferStandardLayoutFeaturesKHR ubo_std430_features;
	VkPhysicalDeviceFeatures enabled_features;
	VkDeviceCreateInfo device_info;
	uint32_t available_count = 0, queue_family_count = 0, i;
	bool has_subgroup_size_control, single_queue, ok = false;
	void **tail;

	memset(context, 0, sizeof(*context));
	enabled.names = NULL;
	enabled.count = 0;

	if (volkGetInstanceVersion() < VK_API_VERSION_1_1)
	{
		LOGE("Vulkan loader does not support Vulkan 1.1.\n");
		return false;
	}

	context->instance = instance;
	volkLoadInstance(instance);

	if (gpu == VK_NULL_HANDLE)
		gpu = pick_gpu(instance);
	if (gpu == VK_NULL_HANDLE)
		return false;
	context->gpu = gpu;

	vkEnumerateDeviceExtensionProperties(gpu, NULL, &available_count, NULL);
	if (available_count)
	{
		available = (VkExtensionProperties*)malloc(available_count * sizeof(*available));
		if (!available)
			return false;
		if (vkEnumerateDeviceExtensionProperties(gpu, NULL, &available_count, available) != VK_SUCCESS)
			available_count = 0;
	}

	enabled.names = (const char**)malloc((num_extensions + MAX_OPTIONAL_EXTENSIONS) * sizeof(*enabled.names));
	if (!enabled.names)
		goto done;

	for (i = 0; i < num_extensions; i++)
	{
		if (!has_extension(available, available_count, extensions[i]))
			goto done;
		enable_extension(&enabled, extensions[i]);
	}

	memset(&gpu_props2, 0, sizeof(gpu_props2));
	gpu_props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	if (has_extension(available, available_count, VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME))
	{
		ext->supports_driver_properties = true;
		ext->driver_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES_KHR;
		gpu_props2.pNext = &ext->driver_properties;
	}
	vkGetPhysicalDeviceProperties2(gpu, &gpu_props2);
	context->gpu_props = gpu_props2.properties;
	LOGI("Using Vulkan GPU: %s\n", context->gpu_props.deviceName);

	if (context->gpu_props.apiVersion < VK_API_VERSION_1_1)
	{
		LOGE("Found no Vulkan GPU which supports Vulkan 1.1.\n");
		goto done;
	}

	vkGetPhysicalDeviceMemoryProperties(gpu, &context->mem_props);

	/* ---- queues ---- */
	vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queue_family_count, NULL);
	if (queue_family_count > GRANITE_CONTEXT_MAX_QUEUE_FAMILIES)
		queue_family_count = GRANITE_CONTEXT_MAX_QUEUE_FAMILIES;
	vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queue_family_count, queue_props);

	for (i = 0; i < queue_family_count; i++)
	{
		VkBool32 supported = VK_FALSE;
		can_present[i] = surface == VK_NULL_HANDLE
			|| (vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface, &supported) == VK_SUCCESS && supported);
	}

	/* (its scheduler interleaves the work of separate queues badly) */
	single_queue = ext->supports_driver_properties
		&& ext->driver_properties.driverID == VK_DRIVER_ID_SAMSUNG_PROPRIETARY;

	if (!granite_context_pick_queues(queue_props, queue_family_count, can_present, single_queue, &pick))
	{
		LOGE("Could not find suitable graphics queue.\n");
		goto done;
	}

	memset(&device_info, 0, sizeof(device_info));
	device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	device_info.pQueueCreateInfos = queue_infos;
	for (i = 0; i < queue_family_count; i++)
	{
		VkDeviceQueueCreateInfo *info;
		if (!pick.counts[i])
			continue;
		info = &queue_infos[device_info.queueCreateInfoCount++];
		memset(info, 0, sizeof(*info));
		info->sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		info->queueFamilyIndex = i;
		info->queueCount = pick.counts[i];
		info->pQueuePriorities = pick.priorities[i];
	}

	/* ---- extensions and features ---- */
	memset(&features, 0, sizeof(features));
	features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	memset(&scalar_block_features, 0, sizeof(scalar_block_features));
	scalar_block_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES_EXT;
	memset(&ubo_std430_features, 0, sizeof(ubo_std430_features));
	ubo_std430_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFORM_BUFFER_STANDARD_LAYOUT_FEATURES_KHR;
	ext->storage_8bit_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES_KHR;
	ext->storage_16bit_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES_KHR;
	ext->float16_int8_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT16_INT8_FEATURES_KHR;
	ext->timeline_semaphore_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES_KHR;
	ext->subgroup_size_control_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES_EXT;
	ext->memory_priority_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PRIORITY_FEATURES_EXT;
	ext->pipeline_creation_cache_control_features.sType =
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_CREATION_CACHE_CONTROL_FEATURES_EXT;

	tail = &features.pNext;

	if (has_extension(available, available_count, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME))
	{
		ext->supports_push_descriptor = true;
		enable_extension(&enabled, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
	}

	if (has_extension(available, available_count, VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME))
	{
		ext->supports_image_format_list = true;
		enable_extension(&enabled, VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME);
	}

	if (has_extension(available, available_count, VK_EXT_TOOLING_INFO_EXTENSION_NAME))
		ext->supports_tooling_info = true;

	if (has_extension(available, available_count, VK_KHR_8BIT_STORAGE_EXTENSION_NAME))
	{
		enable_extension(&enabled, VK_KHR_8BIT_STORAGE_EXTENSION_NAME);
		tail = chain(tail, &ext->storage_8bit_features);
	}

	if (has_extension(available, available_count, VK_KHR_16BIT_STORAGE_EXTENSION_NAME))
	{
		enable_extension(&enabled, VK_KHR_16BIT_STORAGE_EXTENSION_NAME);
		tail = chain(tail, &ext->storage_16bit_features);
	}

	if (has_extension(available, available_count, VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME))
	{
		enable_extension(&enabled, VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME);
		tail = chain(tail, &ext->float16_int8_features);
	}

	has_subgroup_size_control = has_extension(available, available_count, VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME);
	if (has_subgroup_size_control)
	{
		enable_extension(&enabled, VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME);
		tail = chain(tail, &ext->subgroup_size_control_features);
	}

	if (has_extension(available, available_count, VK_EXT_SCALAR_BLOCK_LAYOUT_EXTENSION_NAME))
	{
		enable_extension(&enabled, VK_EXT_SCALAR_BLOCK_LAYOUT_EXTENSION_NAME);
		tail = chain(tail, &scalar_block_features);
	}

	if (has_extension(available, available_count, VK_KHR_UNIFORM_BUFFER_STANDARD_LAYOUT_EXTENSION_NAME))
	{
		enable_extension(&enabled, VK_KHR_UNIFORM_BUFFER_STANDARD_LAYOUT_EXTENSION_NAME);
		tail = chain(tail, &ubo_std430_features);
	}

	if (has_extension(available, available_count, VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME))
	{
		enable_extension(&enabled, VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
		tail = chain(tail, &ext->timeline_semaphore_features);
	}

	if (has_extension(available, available_count, VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME))
	{
		enable_extension(&enabled, VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME);
		tail = chain(tail, &ext->memory_priority_features);
	}

	if (has_extension(available, available_count, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME))
	{
		ext->supports_memory_budget = true;
		enable_extension(&enabled, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
	}

	if (has_extension(available, available_count, VK_EXT_PIPELINE_CREATION_CACHE_CONTROL_EXTENSION_NAME))
	{
		enable_extension(&enabled, VK_EXT_PIPELINE_CREATION_CACHE_CONTROL_EXTENSION_NAME);
		tail = chain(tail, &ext->pipeline_creation_cache_control_features);
	}

	if (has_extension(available, available_count, VK_KHR_FORMAT_FEATURE_FLAGS_2_EXTENSION_NAME))
	{
		ext->supports_format_feature_flags2 = true;
		enable_extension(&enabled, VK_KHR_FORMAT_FEATURE_FLAGS_2_EXTENSION_NAME);
	}

	/* the emulated memory as a buffer, with no copy */
	if (has_extension(available, available_count, VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME))
	{
		ext->supports_external_memory_host = true;
		enable_extension(&enabled, VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME);
	}

	/* everything of the chained features the device has is enabled */
	vkGetPhysicalDeviceFeatures2(gpu, &features);

	/* of the core features, what the frontend asks for and what the
	 * renderer uses */
	enabled_features = *required_features;
	if (features.features.shaderInt16)
		enabled_features.shaderInt16 = VK_TRUE;
	if (features.features.independentBlend)
		enabled_features.independentBlend = VK_TRUE;
	if (features.features.fragmentStoresAndAtomics)
		enabled_features.fragmentStoresAndAtomics = VK_TRUE;
	features.features = enabled_features;
	ext->enabled_features = enabled_features;

	/* ---- properties ---- */
	memset(&props, 0, sizeof(props));
	props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	ext->subgroup_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;
	ext->host_memory_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT;
	ext->subgroup_size_control_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES_EXT;
	ext->push_descriptor_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_PROPERTIES_KHR;

	tail = chain(&props.pNext, &ext->subgroup_properties);
	if (ext->supports_external_memory_host)
		tail = chain(tail, &ext->host_memory_properties);
	if (has_subgroup_size_control)
		tail = chain(tail, &ext->subgroup_size_control_properties);
	if (ext->supports_push_descriptor)
		tail = chain(tail, &ext->push_descriptor_properties);
	vkGetPhysicalDeviceProperties2(gpu, &props);

	/* ---- the device ---- */
	device_info.pNext = &features;
	device_info.enabledExtensionCount = enabled.count;
	device_info.ppEnabledExtensionNames = enabled.count ? enabled.names : NULL;

	if (create_device)
		context->device = create_device(user, gpu, &device_info);
	else if (vkCreateDevice(gpu, &device_info, NULL, &context->device) != VK_SUCCESS)
		context->device = VK_NULL_HANDLE;
	if (context->device == VK_NULL_HANDLE)
		goto done;
	context->owned_device = true;

	volkLoadDeviceTable(&context->device_table, context->device);

	for (i = 0; i < GRANITE_QUEUE_COUNT; i++)
	{
		context->queue_info.family_indices[i] = pick.family[i];
		context->device_table.vkGetDeviceQueue(context->device, pick.family[i], pick.index[i],
				&context->queue_info.queues[i]);
	}
	ok = true;

done:
	free(available);
	free((void*)enabled.names);
	if (!ok)
		LOGE("Failed to create Vulkan device.\n");
	return ok;
}

void granite_context_destroy(granite_context_t *context)
{
	if (context->device == VK_NULL_HANDLE)
		return;
	context->device_table.vkDeviceWaitIdle(context->device);
	if (context->owned_device)
		context->device_table.vkDestroyDevice(context->device, NULL);
	context->device = VK_NULL_HANDLE;
}
