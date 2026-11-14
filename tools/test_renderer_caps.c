/* parallel-rdp's renderer caps (parallel-rdp/renderer_caps.c), against
 * stand-in devices.
 *
 * The upscaling factor halves until every buffer it scales fits the
 * device's storage buffer bindings; the super-sampled write mask can be
 * what does not fit. One flush holds as many static raster states as the
 * work list's slices that fit a binding, and none is an error.
 *
 * 16-bit storage is required, and 8-bit storage unless it is not used.
 * The ubershader is on for the proprietary Intel Windows driver. Small
 * integer types are off for the drivers known to be slower or buggy with
 * them (unless forced), and need shaderInt16 and shaderInt8. Subgroup
 * tile binning needs the subgroup operations in compute, a subgroup size
 * of at most 64, and size control as Granite builds the pipeline with it;
 * subgroup depth blending needs the super-sampled read-back. The
 * PARALLEL_RDP_* variables override. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <boolean.h>

#include "renderer_caps.h"

/* device_caps.c's other functions call these; nothing here runs them */
PFN_vkGetPhysicalDeviceToolPropertiesEXT vkGetPhysicalDeviceToolPropertiesEXT;
PFN_vkGetPhysicalDeviceFormatProperties2 vkGetPhysicalDeviceFormatProperties2;

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_renderer_caps: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

#define MIB ((VkDeviceSize)1 << 20)

static const char *const overrides[] = {
	"PARALLEL_RDP_UBERSHADER", "PARALLEL_RDP_FORCE_SYNC_SHADER",
	"PARALLEL_RDP_SUBGROUP", "PARALLEL_RDP_SMALL_TYPES",
	"PARALLEL_RDP_MAX_STORAGE_BUFFER_RANGE"
};

static void clear_overrides(void)
{
	unsigned i;
	for (i = 0; i < sizeof(overrides) / sizeof(overrides[0]); i++)
		unsetenv(overrides[i]);
}

/* A device with everything: Mesa's Intel driver, subgroups of 32 */
static void full_device(granite_device_features_t *f)
{
	memset(f, 0, sizeof(*f));
	f->supports_driver_properties = true;
	f->driver_properties.driverID = VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA_KHR;
	f->storage_16bit_features.storageBuffer16BitAccess = VK_TRUE;
	f->storage_8bit_features.storageBuffer8BitAccess = VK_TRUE;
	f->enabled_features.shaderInt16 = VK_TRUE;
	f->float16_int8_features.shaderInt8 = VK_TRUE;
	f->subgroup_properties.subgroupSize = 32;
	f->subgroup_properties.supportedOperations = VK_SUBGROUP_FEATURE_BASIC_BIT | VK_SUBGROUP_FEATURE_VOTE_BIT |
			VK_SUBGROUP_FEATURE_ARITHMETIC_BIT | VK_SUBGROUP_FEATURE_BALLOT_BIT;
	f->subgroup_properties.supportedStages = VK_SHADER_STAGE_COMPUTE_BIT;
	f->subgroup_size_control_features.subgroupSizeControl = VK_TRUE;
	f->subgroup_size_control_features.computeFullSubgroups = VK_TRUE;
	f->subgroup_size_control_properties.minSubgroupSize = 8;
	f->subgroup_size_control_properties.maxSubgroupSize = 32;
	f->subgroup_size_control_properties.requiredSubgroupSizeStages = VK_SHADER_STAGE_COMPUTE_BIT;
}

/* The caps for @f, with the super-sampled read-back and without 8-bit
 * storage as asked; false when the device cannot run the renderer */
static bool caps_for(rdp_renderer_caps_t *caps, const granite_device_features_t *f, bool ssr, bool no8)
{
	rdp_caps_init(caps);
	caps->super_sample_readback = ssr;
	caps->no_storage_8bit = no8;
	return rdp_caps_init_features(caps, f);
}

static void test_defaults(void)
{
	rdp_renderer_caps_t caps;
	rdp_caps_init(&caps);
	CHECK(caps.upscaling == 1 && caps.max_width == 1024 && caps.max_height == 1024, "native size");
	CHECK(caps.max_tiles_x == 128 && caps.max_tiles_y == 128, "native tiles");
	CHECK(caps.max_num_tile_instances == 0x8000 && caps.max_static_raster_states == 64, "native instances");
	CHECK(!caps.ubershader && !caps.force_sync && !caps.supports_small_integer_arithmetic &&
	      !caps.subgroup_tile_binning && !caps.subgroup_depth_blend && !caps.super_sample_readback,
	      "nothing enabled");
}

static void test_fitting_factor(void)
{
	const VkDeviceSize rdram = 8 * MIB, hidden = 4 * MIB;
	const VkDeviceSize big = ((VkDeviceSize)1 << 32) - 1;

	CHECK(rdp_caps_fitting_upscaling_factor(big, 8, rdram, hidden, true) == 8, "8x fits 4 GiB bindings");
	/* at 8x the per-tile colour is 512 MiB; at 4x it and the RDRAM are
	 * 128 MiB, exactly a 128 MiB binding, and the write mask is more */
	CHECK(rdp_caps_fitting_upscaling_factor(128 * MIB, 8, rdram, hidden, false) == 4, "4x fits 128 MiB");
	CHECK(rdp_caps_fitting_upscaling_factor(128 * MIB, 8, rdram, hidden, true) == 2,
	      "the write mask after the 4x RDRAM does not fit");
	CHECK(rdp_caps_fitting_upscaling_factor(128 * MIB - 1, 4, rdram, hidden, false) == 2, "a byte short of 4x");
	CHECK(rdp_caps_fitting_upscaling_factor(1, 8, rdram, hidden, true) == 1, "1x always fits");
	CHECK(rdp_caps_fitting_upscaling_factor(big, 1, rdram, hidden, false) == 1, "1x stays 1x");
	/* the hidden RDRAM alone too big at 2x */
	CHECK(rdp_caps_fitting_upscaling_factor(64 * MIB, 2, rdram, 17 * MIB, false) == 1, "hidden RDRAM");
}

static void test_storage_range(void)
{
	clear_overrides();
	CHECK(rdp_caps_storage_buffer_range(1000) == 1000, "the device's range");
	setenv("PARALLEL_RDP_MAX_STORAGE_BUFFER_RANGE", "0x100", 1);
	CHECK(rdp_caps_storage_buffer_range(1000) == 256, "lowered");
	CHECK(rdp_caps_storage_buffer_range(100) == 100, "never raised");
	setenv("PARALLEL_RDP_MAX_STORAGE_BUFFER_RANGE", "0", 1);
	CHECK(rdp_caps_storage_buffer_range(1000) == 1000, "0 is no cap");
	clear_overrides();
}

static void test_limits(void)
{
	rdp_renderer_caps_t caps;
	const VkDeviceSize big = ((VkDeviceSize)1 << 32) - 1;

	rdp_caps_init(&caps);
	CHECK(rdp_caps_init_limits(&caps, 4, true, big), "4x in 4 GiB");
	CHECK(caps.max_width == 4096 && caps.max_height == 4096, "4x size");
	CHECK(caps.max_tiles_x == 512 && caps.max_tiles_y == 512, "4x tiles");
	CHECK(caps.max_num_tile_instances == 16 * 0x8000, "4x instances");
	CHECK(caps.no_storage_8bit, "no 8-bit storage kept");
	/* a slice is 8 MiB at 4x: 64 of them want 512 MiB */
	CHECK(caps.max_static_raster_states == 64, "every state in 4 GiB");

	rdp_caps_init(&caps);
	CHECK(rdp_caps_init_limits(&caps, 4, false, 128 * MIB + 1), "4x in 128 MiB");
	CHECK(caps.max_static_raster_states == 16, "16 slices of 8 MiB");

	rdp_caps_init(&caps);
	CHECK(!rdp_caps_init_limits(&caps, 4, false, 8 * MIB - 1), "not one slice");
	CHECK(caps.max_static_raster_states == 0, "none");
}

static void test_features(void)
{
	granite_device_features_t f;
	rdp_renderer_caps_t caps;

	clear_overrides();

	full_device(&f);
	CHECK(caps_for(&caps, &f, true, false), "full device");
	CHECK(!caps.ubershader && !caps.force_sync, "split shaders");
	CHECK(caps.supports_small_integer_arithmetic, "ANV small types");
	CHECK(caps.subgroup_tile_binning && caps.subgroup_depth_blend, "subgroups");
	CHECK(caps_for(&caps, &f, false, false) && !caps.subgroup_depth_blend,
	      "subgroup depth blending only with the read-back");

	full_device(&f);
	f.storage_16bit_features.storageBuffer16BitAccess = VK_FALSE;
	CHECK(!caps_for(&caps, &f, false, true), "16-bit storage required");

	full_device(&f);
	f.storage_8bit_features.storageBuffer8BitAccess = VK_FALSE;
	CHECK(!caps_for(&caps, &f, false, false), "8-bit storage required");
	CHECK(caps_for(&caps, &f, false, true), "unless not used");
	CHECK(!caps.supports_small_integer_arithmetic, "no small types without it");

	full_device(&f);
	f.float16_int8_features.shaderInt8 = VK_FALSE;
	CHECK(caps_for(&caps, &f, false, false) && !caps.supports_small_integer_arithmetic, "shaderInt8 needed");
	full_device(&f);
	f.enabled_features.shaderInt16 = VK_FALSE;
	CHECK(caps_for(&caps, &f, false, false) && !caps.supports_small_integer_arithmetic, "shaderInt16 needed");

	{
		static const VkDriverId slow[] = {
			VK_DRIVER_ID_AMD_PROPRIETARY_KHR, VK_DRIVER_ID_AMD_OPEN_SOURCE_KHR, VK_DRIVER_ID_MESA_RADV_KHR,
			VK_DRIVER_ID_NVIDIA_PROPRIETARY_KHR, VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS_KHR,
			VK_DRIVER_ID_QUALCOMM_PROPRIETARY_KHR
		};
		unsigned i;
		for (i = 0; i < sizeof(slow) / sizeof(slow[0]); i++)
		{
			full_device(&f);
			f.driver_properties.driverID = slow[i];
			CHECK(caps_for(&caps, &f, false, false) && !caps.supports_small_integer_arithmetic,
			      "no small types on the slow drivers");
			CHECK(caps.ubershader == (slow[i] == VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS_KHR),
			      "the ubershader on Intel's Windows driver only");
			/* without driver properties the driver is not known */
			f.supports_driver_properties = false;
			CHECK(caps_for(&caps, &f, false, false) && caps.supports_small_integer_arithmetic,
			      "small types where the driver is not known");
		}
	}

	/* subgroups */
	full_device(&f);
	f.subgroup_properties.subgroupSize = 128;
	f.subgroup_size_control_properties.maxSubgroupSize = 128;
	CHECK(caps_for(&caps, &f, true, false) && !caps.subgroup_tile_binning && caps.subgroup_depth_blend,
	      "tile binning: subgroups of at most 64");
	full_device(&f);
	f.subgroup_properties.subgroupSize = 64;
	f.subgroup_size_control_properties.maxSubgroupSize = 64;
	CHECK(caps_for(&caps, &f, false, false) && caps.subgroup_tile_binning, "64 is fine");
	full_device(&f);
	f.subgroup_properties.supportedOperations &= ~VK_SUBGROUP_FEATURE_BALLOT_BIT;
	CHECK(caps_for(&caps, &f, true, false) && !caps.subgroup_tile_binning && !caps.subgroup_depth_blend,
	      "ballot needed");
	full_device(&f);
	f.subgroup_properties.supportedStages = VK_SHADER_STAGE_FRAGMENT_BIT;
	CHECK(caps_for(&caps, &f, true, false) && !caps.subgroup_tile_binning && !caps.subgroup_depth_blend,
	      "subgroups in compute needed");
	full_device(&f);
	f.subgroup_size_control_features.computeFullSubgroups = VK_FALSE;
	CHECK(caps_for(&caps, &f, true, false) && !caps.subgroup_tile_binning && caps.subgroup_depth_blend,
	      "tile binning: full subgroups needed");
	full_device(&f);
	f.subgroup_size_control_features.subgroupSizeControl = VK_FALSE;
	CHECK(caps_for(&caps, &f, false, false) && !caps.subgroup_tile_binning,
	      "tile binning: size control needed, as its pipeline is built");
	/* a device of 16 to 64 that cannot require a size: 32 is not
	 * guaranteed */
	full_device(&f);
	f.subgroup_size_control_properties.minSubgroupSize = 16;
	f.subgroup_size_control_properties.maxSubgroupSize = 64;
	f.subgroup_size_control_properties.requiredSubgroupSizeStages = 0;
	CHECK(caps_for(&caps, &f, false, false) && !caps.subgroup_tile_binning, "a size has to be required");
	f.subgroup_size_control_properties.requiredSubgroupSizeStages = VK_SHADER_STAGE_COMPUTE_BIT;
	CHECK(caps_for(&caps, &f, false, false) && caps.subgroup_tile_binning, "and can be");
	/* all of the device's sizes are 32 or more */
	f.subgroup_size_control_properties.minSubgroupSize = 32;
	f.subgroup_size_control_properties.maxSubgroupSize = 32;
	f.subgroup_size_control_properties.requiredSubgroupSizeStages = 0;
	CHECK(caps_for(&caps, &f, false, false) && caps.subgroup_tile_binning, "any size will do");
	CHECK(rdp_caps_tile_binning_size_control(&f), "the check alone");
	f.subgroup_properties.subgroupSize = 16;
	f.subgroup_size_control_properties.minSubgroupSize = 16;
	f.subgroup_size_control_properties.maxSubgroupSize = 16;
	CHECK(!rdp_caps_tile_binning_size_control(&f), "smaller than 32");
}

static void test_overrides(void)
{
	granite_device_features_t f;
	rdp_renderer_caps_t caps;

	clear_overrides();
	full_device(&f);
	f.driver_properties.driverID = VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS_KHR;
	setenv("PARALLEL_RDP_UBERSHADER", "0", 1);
	CHECK(caps_for(&caps, &f, false, false) && !caps.ubershader, "ubershader off over the driver");
	full_device(&f);
	setenv("PARALLEL_RDP_UBERSHADER", "1", 1);
	setenv("PARALLEL_RDP_FORCE_SYNC_SHADER", "2", 1);
	CHECK(caps_for(&caps, &f, false, false) && caps.ubershader && caps.force_sync, "on");
	clear_overrides();

	full_device(&f);
	f.driver_properties.driverID = VK_DRIVER_ID_NVIDIA_PROPRIETARY_KHR;
	setenv("PARALLEL_RDP_SMALL_TYPES", "1", 1);
	CHECK(caps_for(&caps, &f, false, false) && caps.supports_small_integer_arithmetic, "small types forced on");
	CHECK(caps_for(&caps, &f, false, true) && !caps.supports_small_integer_arithmetic,
	      "but not without 8-bit storage");
	full_device(&f);
	setenv("PARALLEL_RDP_SMALL_TYPES", "0", 1);
	CHECK(caps_for(&caps, &f, false, false) && !caps.supports_small_integer_arithmetic, "small types forced off");
	clear_overrides();

	full_device(&f);
	setenv("PARALLEL_RDP_SUBGROUP", "0", 1);
	CHECK(caps_for(&caps, &f, true, false) && !caps.subgroup_tile_binning && !caps.subgroup_depth_blend,
	      "subgroups off");
	clear_overrides();
}

static void test_defines(void)
{
	rdp_renderer_caps_t caps;
	rdp_caps_init(&caps);
	caps.ubershader = true;
	caps.no_storage_8bit = true;
	caps.subgroup_tile_binning = true;
	CHECK(rdp_caps_resolve_shader_define(&caps, "rasterizer", "DEBUG_ENABLE") == 0, "no debug");
	CHECK(rdp_caps_resolve_shader_define(&caps, "rasterizer", "UBERSHADER") == 1, "UBERSHADER");
	CHECK(rdp_caps_resolve_shader_define(&caps, "rasterizer", "SMALL_TYPES") == 0, "SMALL_TYPES");
	CHECK(rdp_caps_resolve_shader_define(&caps, "rasterizer", "NO_STORAGE_8BIT") == 1, "NO_STORAGE_8BIT");
	CHECK(rdp_caps_resolve_shader_define(&caps, "tile_binning_combined", "SUBGROUP") == 1, "tile binning's");
	CHECK(rdp_caps_resolve_shader_define(&caps, "depth_blend", "SUBGROUP") == 0, "depth blend's");
	caps.subgroup_tile_binning = false;
	caps.subgroup_depth_blend = true;
	caps.supports_small_integer_arithmetic = true;
	CHECK(rdp_caps_resolve_shader_define(&caps, "tile_binning_combined", "SUBGROUP") == 0, "tile binning's, off");
	CHECK(rdp_caps_resolve_shader_define(&caps, "depth_blend", "SUBGROUP") == 1, "depth blend's, on");
	CHECK(rdp_caps_resolve_shader_define(&caps, "ubershader", "SUBGROUP") == 1, "the ubershader's");
	CHECK(rdp_caps_resolve_shader_define(&caps, "rasterizer", "SUBGROUP") == 0, "no other shader's");
	CHECK(rdp_caps_resolve_shader_define(&caps, "rasterizer", "SMALL_TYPES") == 1, "SMALL_TYPES on");
	CHECK(rdp_caps_resolve_shader_define(&caps, "rasterizer", "SOMETHING_ELSE") == 0, "the rest");
}

int main(void)
{
	test_defaults();
	test_fitting_factor();
	test_storage_range();
	test_limits();
	test_features();
	test_overrides();
	test_defines();
	if (failures)
	{
		fprintf(stderr, "test_renderer_caps: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_renderer_caps: all checks passed\n");
	return 0;
}
