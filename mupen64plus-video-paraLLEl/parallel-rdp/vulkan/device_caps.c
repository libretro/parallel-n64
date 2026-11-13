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
#include "device_caps.h"

void granite_workarounds_init(granite_workarounds_t *workarounds, uint32_t vendor_id, VkDriverId driver_id,
		bool timeline_semaphores, bool tracing)
{
	memset(workarounds, 0, sizeof(*workarounds));

	if (vendor_id == GRANITE_VENDOR_ID_ARM)
	{
		workarounds->optimize_all_graphics_barrier = true;
		workarounds->split_binary_timeline_semaphores = timeline_semaphores;
	}
	else if (driver_id == VK_DRIVER_ID_NVIDIA_PROPRIETARY)
		workarounds->broken_pipeline_cache_control = true;

	workarounds->force_host_cached = tracing;
}

bool granite_device_tracing_tool_attached(VkPhysicalDevice gpu)
{
	VkPhysicalDeviceToolPropertiesEXT *tools;
	uint32_t count = 0, i;
	bool tracing = false;

	if (!vkGetPhysicalDeviceToolPropertiesEXT)
		return false;
	if (vkGetPhysicalDeviceToolPropertiesEXT(gpu, &count, NULL) != VK_SUCCESS || !count)
		return false;
	tools = (VkPhysicalDeviceToolPropertiesEXT*)calloc(count, sizeof(*tools));
	if (!tools)
		return false;
	for (i = 0; i < count; i++)
		tools[i].sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TOOL_PROPERTIES_EXT;
	if (vkGetPhysicalDeviceToolPropertiesEXT(gpu, &count, tools) != VK_SUCCESS)
		count = 0;
	for (i = 0; i < count; i++)
		if (tools[i].purposes & VK_TOOL_PURPOSE_TRACING_BIT_EXT)
			tracing = true;
	free(tools);
	return tracing;
}

VkFormatFeatureFlags2 granite_format_features(VkPhysicalDevice gpu, VkFormat format, VkImageTiling tiling,
		bool flags2)
{
	VkFormatProperties2 props2;
	VkFormatProperties3KHR props3;

	memset(&props2, 0, sizeof(props2));
	props2.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2;
	memset(&props3, 0, sizeof(props3));
	props3.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3_KHR;
	if (flags2)
		props2.pNext = &props3;
	vkGetPhysicalDeviceFormatProperties2(gpu, format, &props2);

	if (flags2)
		return tiling == VK_IMAGE_TILING_OPTIMAL ? props3.optimalTilingFeatures : props3.linearTilingFeatures;
	return tiling == VK_IMAGE_TILING_OPTIMAL ? props2.formatProperties.optimalTilingFeatures
		: props2.formatProperties.linearTilingFeatures;
}

bool granite_image_format_is_supported(VkPhysicalDevice gpu, VkFormat format, VkFormatFeatureFlags2 required,
		VkImageTiling tiling, bool flags2)
{
	return (granite_format_features(gpu, format, tiling, flags2) & required) == required;
}

bool granite_subgroup_size_supported(const VkPhysicalDeviceSubgroupSizeControlFeaturesEXT *features,
		const VkPhysicalDeviceSubgroupSizeControlPropertiesEXT *properties,
		bool full_group, unsigned min_log2, unsigned max_log2)
{
	uint32_t min_size = (uint32_t)1 << min_log2;
	uint32_t max_size = (uint32_t)1 << max_log2;

	if (!features->subgroupSizeControl)
		return false;
	if (full_group && !features->computeFullSubgroups)
		return false;

	/* any size the device comes up with will do */
	if (min_size <= properties->minSubgroupSize && max_size >= properties->maxSubgroupSize)
		return true;
	/* nothing in common */
	if (min_size > properties->maxSubgroupSize || max_size < properties->minSubgroupSize)
		return false;
	/* a size has to be required */
	return (properties->requiredSubgroupSizeStages & VK_SHADER_STAGE_COMPUTE_BIT) != 0;
}
