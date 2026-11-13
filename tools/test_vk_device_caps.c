/* parallel-rdp's device capabilities (vulkan/device_caps.c), against a
 * stand-in physical device.
 *
 * Workarounds go by the device: an ARM GPU gets its barriers after all
 * graphics stages shortened and, with timeline semaphores, binary and
 * timeline waits kept apart; NVIDIA's driver is not asked for pipelines
 * from the cache alone; and when a tool that traces the calls is
 * attached, host memory is cached. Nothing else is worked around.
 *
 * What a format can be used for comes from the newer, longer set of
 * flags where the device has it and from the older set otherwise, for
 * the tiling asked about. A subgroup size range is supported when the
 * device's own range lies within it, or overlaps it and a compute stage
 * may require a size. */
#include <stdio.h>
#include <string.h>
#include <boolean.h>

#include "device_caps.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_vk_device_caps: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

#define GPU ((VkPhysicalDevice)(size_t)0x100)

/* ---- the stand-in device ---- */
static VkToolPurposeFlagsEXT tool_purposes[4];
static uint32_t num_tools;
static unsigned tool_calls;
static bool fail_tools;

static VkResult VKAPI_CALL fake_tools(VkPhysicalDevice gpu, uint32_t *count, VkPhysicalDeviceToolPropertiesEXT *out)
{
	uint32_t i;
	CHECK(gpu == GPU, "the device asked about");
	tool_calls++;
	if (fail_tools)
		return VK_ERROR_OUT_OF_HOST_MEMORY;
	if (!out)
	{
		*count = num_tools;
		return VK_SUCCESS;
	}
	for (i = 0; i < *count && i < num_tools; i++)
	{
		CHECK(out[i].sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TOOL_PROPERTIES_EXT, "a tool structure with its type set");
		out[i].purposes = tool_purposes[i];
	}
	*count = i;
	return VK_SUCCESS;
}

static VkFormat asked_format;
static bool asked_flags2;
static unsigned format_calls;

#define OLD_OPTIMAL (VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)
#define OLD_LINEAR  VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT
#define NEW_OPTIMAL (VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT \
		| VK_FORMAT_FEATURE_2_STORAGE_READ_WITHOUT_FORMAT_BIT)
#define NEW_LINEAR  (VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT)

static void VKAPI_CALL fake_format_props2(VkPhysicalDevice gpu, VkFormat format, VkFormatProperties2 *props)
{
	CHECK(gpu == GPU && props->sType == VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2, "a well-formed query");
	format_calls++;
	asked_format = format;
	asked_flags2 = props->pNext != NULL;
	props->formatProperties.optimalTilingFeatures = OLD_OPTIMAL;
	props->formatProperties.linearTilingFeatures = OLD_LINEAR;
	if (props->pNext)
	{
		VkFormatProperties3KHR *props3 = (VkFormatProperties3KHR *)props->pNext;
		CHECK(props3->sType == VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3_KHR && !props3->pNext, "with the newer flags chained");
		props3->optimalTilingFeatures = NEW_OPTIMAL;
		props3->linearTilingFeatures = NEW_LINEAR;
	}
}

PFN_vkGetPhysicalDeviceToolPropertiesEXT vkGetPhysicalDeviceToolPropertiesEXT = fake_tools;
PFN_vkGetPhysicalDeviceFormatProperties2 vkGetPhysicalDeviceFormatProperties2 = fake_format_props2;

static void test_workarounds(void)
{
	granite_workarounds_t w;

	memset(&w, 0xff, sizeof(w));
	granite_workarounds_init(&w, GRANITE_VENDOR_ID_AMD, VK_DRIVER_ID_MESA_RADV, true, false);
	CHECK(!w.optimize_all_graphics_barrier && !w.split_binary_timeline_semaphores && !w.broken_pipeline_cache_control
			&& !w.force_host_cached, "an ordinary device needs none");

	granite_workarounds_init(&w, GRANITE_VENDOR_ID_ARM, VK_DRIVER_ID_ARM_PROPRIETARY, true, false);
	CHECK(w.optimize_all_graphics_barrier && w.split_binary_timeline_semaphores && !w.broken_pipeline_cache_control
			&& !w.force_host_cached, "an ARM GPU gets short barriers and split waits");
	granite_workarounds_init(&w, GRANITE_VENDOR_ID_ARM, VK_DRIVER_ID_ARM_PROPRIETARY, false, false);
	CHECK(w.optimize_all_graphics_barrier && !w.split_binary_timeline_semaphores,
			"without timeline semaphores there is nothing to split");

	granite_workarounds_init(&w, GRANITE_VENDOR_ID_NVIDIA, VK_DRIVER_ID_NVIDIA_PROPRIETARY, true, false);
	CHECK(w.broken_pipeline_cache_control && !w.optimize_all_graphics_barrier && !w.split_binary_timeline_semaphores,
			"NVIDIA's driver is not asked for pipelines from the cache alone");
	granite_workarounds_init(&w, GRANITE_VENDOR_ID_NVIDIA, VK_DRIVER_ID_MESA_NVK, true, false);
	CHECK(!w.broken_pipeline_cache_control, "the open driver for the same GPUs is");

	granite_workarounds_init(&w, GRANITE_VENDOR_ID_INTEL, VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA, false, true);
	CHECK(w.force_host_cached && !w.optimize_all_graphics_barrier && !w.broken_pipeline_cache_control,
			"a tracing tool makes host memory cached, on any device");
}

static void test_tools(void)
{
	num_tools = 0;
	CHECK(!granite_device_tracing_tool_attached(GPU) && tool_calls == 1, "no tools: nothing traces");

	num_tools = 3;
	tool_purposes[0] = VK_TOOL_PURPOSE_VALIDATION_BIT_EXT;
	tool_purposes[1] = VK_TOOL_PURPOSE_PROFILING_BIT_EXT;
	tool_purposes[2] = VK_TOOL_PURPOSE_ADDITIONAL_FEATURES_BIT_EXT;
	CHECK(!granite_device_tracing_tool_attached(GPU), "tools that do not trace do not count");
	tool_purposes[2] = VK_TOOL_PURPOSE_TRACING_BIT_EXT | VK_TOOL_PURPOSE_PROFILING_BIT_EXT;
	CHECK(granite_device_tracing_tool_attached(GPU), "one that traces, among others, does");

	fail_tools = true;
	CHECK(!granite_device_tracing_tool_attached(GPU), "a driver that cannot say has none");
	fail_tools = false;

	vkGetPhysicalDeviceToolPropertiesEXT = NULL;
	CHECK(!granite_device_tracing_tool_attached(GPU), "nor has a loader without the function");
	vkGetPhysicalDeviceToolPropertiesEXT = fake_tools;
}

static void test_formats(void)
{
	CHECK(granite_format_features(GPU, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL, true) == NEW_OPTIMAL
			&& asked_format == VK_FORMAT_R8G8B8A8_UNORM && asked_flags2 && format_calls == 1,
			"with the newer flags, those are asked for and returned");
	CHECK(granite_format_features(GPU, VK_FORMAT_R8_UINT, VK_IMAGE_TILING_LINEAR, true) == NEW_LINEAR
			&& asked_format == VK_FORMAT_R8_UINT, "for the tiling asked about");
	CHECK(granite_format_features(GPU, VK_FORMAT_R8_UINT, VK_IMAGE_TILING_OPTIMAL, false) == OLD_OPTIMAL && !asked_flags2,
			"without them, the older set is");
	CHECK(granite_format_features(GPU, VK_FORMAT_R8_UINT, VK_IMAGE_TILING_LINEAR, false) == OLD_LINEAR,
			"for the tiling asked about too");

	CHECK(granite_image_format_is_supported(GPU, VK_FORMAT_R8_UINT,
				VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT,
				VK_IMAGE_TILING_OPTIMAL, false), "a format with all that is required is supported");
	CHECK(!granite_image_format_is_supported(GPU, VK_FORMAT_R8_UINT,
				VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT,
				VK_IMAGE_TILING_LINEAR, false), "one that lacks any of it, for its tiling, is not");
	CHECK(!granite_image_format_is_supported(GPU, VK_FORMAT_R8_UINT, VK_FORMAT_FEATURE_2_STORAGE_READ_WITHOUT_FORMAT_BIT,
				VK_IMAGE_TILING_OPTIMAL, false)
			&& granite_image_format_is_supported(GPU, VK_FORMAT_R8_UINT, VK_FORMAT_FEATURE_2_STORAGE_READ_WITHOUT_FORMAT_BIT,
				VK_IMAGE_TILING_OPTIMAL, true), "a newer flag is only known with the newer set");
	CHECK(granite_image_format_is_supported(GPU, VK_FORMAT_R8_UINT, 0, VK_IMAGE_TILING_LINEAR, false),
			"nothing required is always there");
}

static void test_subgroups(void)
{
	VkPhysicalDeviceSubgroupSizeControlFeaturesEXT f;
	VkPhysicalDeviceSubgroupSizeControlPropertiesEXT p;

	memset(&f, 0, sizeof(f));
	memset(&p, 0, sizeof(p));
	f.subgroupSizeControl = VK_TRUE;
	f.computeFullSubgroups = VK_TRUE;
	p.minSubgroupSize = 8;
	p.maxSubgroupSize = 32;
	p.requiredSubgroupSizeStages = VK_SHADER_STAGE_COMPUTE_BIT;

	/* the device's whole range fits: any of its sizes will do */
	CHECK(granite_subgroup_size_supported(&f, &p, true, 3, 5), "a range the device's lies within");
	CHECK(granite_subgroup_size_supported(&f, &p, false, 0, 7), "or a wider one");
	p.requiredSubgroupSizeStages = 0;
	CHECK(granite_subgroup_size_supported(&f, &p, false, 2, 6), "with no size to require");

	/* an overlap: a size has to be required */
	CHECK(!granite_subgroup_size_supported(&f, &p, false, 4, 5) && !granite_subgroup_size_supported(&f, &p, false, 3, 4)
			&& !granite_subgroup_size_supported(&f, &p, false, 5, 6),
			"part of the device's range, when compute cannot require a size");
	p.requiredSubgroupSizeStages = VK_SHADER_STAGE_FRAGMENT_BIT;
	CHECK(!granite_subgroup_size_supported(&f, &p, false, 4, 5), "another stage's ability does not help");
	p.requiredSubgroupSizeStages = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	CHECK(granite_subgroup_size_supported(&f, &p, false, 4, 5) && granite_subgroup_size_supported(&f, &p, false, 3, 4)
			&& granite_subgroup_size_supported(&f, &p, false, 5, 6) && granite_subgroup_size_supported(&f, &p, false, 5, 5)
			&& granite_subgroup_size_supported(&f, &p, false, 3, 3), "part of it, when it can");

	/* nothing in common */
	CHECK(!granite_subgroup_size_supported(&f, &p, false, 0, 2) && !granite_subgroup_size_supported(&f, &p, false, 6, 7),
			"a range below or above the device's");

	/* the features */
	f.computeFullSubgroups = VK_FALSE;
	CHECK(!granite_subgroup_size_supported(&f, &p, true, 3, 5) && granite_subgroup_size_supported(&f, &p, false, 3, 5),
			"full subgroups only where the device has them");
	f.subgroupSizeControl = VK_FALSE;
	f.computeFullSubgroups = VK_TRUE;
	CHECK(!granite_subgroup_size_supported(&f, &p, false, 3, 5), "and nothing without size control");
}

int main(void)
{
	test_workarounds();
	test_tools();
	test_formats();
	test_subgroups();

	if (failures)
	{
		fprintf(stderr, "test_vk_device_caps: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_vk_device_caps: ok\n");
	return 0;
}
