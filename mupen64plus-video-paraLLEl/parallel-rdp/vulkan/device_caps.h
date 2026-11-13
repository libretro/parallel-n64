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

#ifndef GRANITE_DEVICE_CAPS_H
#define GRANITE_DEVICE_CAPS_H

#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>
#include "volk.h"

RETRO_BEGIN_DECLS

#define GRANITE_VENDOR_ID_AMD    0x1002
#define GRANITE_VENDOR_ID_NVIDIA 0x10de
#define GRANITE_VENDOR_ID_INTEL  0x8086
#define GRANITE_VENDOR_ID_ARM    0x13b5
#define GRANITE_VENDOR_ID_QCOM   0x5143

/* What is done differently for a driver's sake */
typedef struct granite_workarounds
{
	/* a barrier after all graphics stages waits for the last of them
	 * only: quicker on a tiler */
	bool optimize_all_graphics_barrier;
	/* a submission waits for binary semaphores or for timeline ones,
	 * never both */
	bool split_binary_timeline_semaphores;
	/* asking for a pipeline from the cache alone does not work */
	bool broken_pipeline_cache_control;
	/* host memory is cached, whatever is asked for: a tool is reading
	 * it all back */
	bool force_host_cached;
} granite_workarounds_t;

/* @vendor_id and @driver_id are the device's; @timeline_semaphores:
 * it has them; @tracing: a tool that traces the calls is attached. */
void granite_workarounds_init(granite_workarounds_t *workarounds, uint32_t vendor_id, VkDriverId driver_id,
		bool timeline_semaphores, bool tracing);

/* A tool that traces Vulkan calls (a capture tool) is attached to
 * @gpu. Only asked of a driver with VK_EXT_tooling_info. */
bool granite_device_tracing_tool_attached(VkPhysicalDevice gpu);

/* What images of @format with @tiling can be used for on @gpu.
 * @flags2: the device has VK_KHR_format_feature_flags2; without it the
 * older, shorter set of flags is what is known. */
VkFormatFeatureFlags2 granite_format_features(VkPhysicalDevice gpu, VkFormat format, VkImageTiling tiling,
		bool flags2);
/* All of @required is among them */
bool granite_image_format_is_supported(VkPhysicalDevice gpu, VkFormat format, VkFormatFeatureFlags2 required,
		VkImageTiling tiling, bool flags2);

/* A compute shader can ask for subgroups of 2^@min_log2 to 2^@max_log2
 * invocations (@full_group: with every subgroup full): the device's
 * own range lies within that, or it overlaps it and the device lets a
 * compute stage require a size. */
bool granite_subgroup_size_supported(const VkPhysicalDeviceSubgroupSizeControlFeaturesEXT *features,
		const VkPhysicalDeviceSubgroupSizeControlPropertiesEXT *properties,
		bool full_group, unsigned min_log2, unsigned max_log2);

RETRO_END_DECLS

#endif
