/* Copyright (c) 2020 Themaister
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
#include "renderer_caps.h"
#include "device_caps.h"
#include "logging.h"

#define RDP_CAPS_MAX_TILES_X (RDP_CAPS_MAX_WIDTH / RDP_CAPS_TILE_WIDTH)
#define RDP_CAPS_MAX_TILES_Y (RDP_CAPS_MAX_HEIGHT / RDP_CAPS_TILE_HEIGHT)

void rdp_caps_init(rdp_renderer_caps_t *caps)
{
	memset(caps, 0, sizeof(*caps));
	caps->upscaling                = 1;
	caps->max_num_tile_instances   = RDP_CAPS_MAX_TILE_INSTANCES;
	caps->max_static_raster_states = RDP_CAPS_MAX_STATIC_RASTER_STATES;
	caps->max_tiles_x              = RDP_CAPS_MAX_TILES_X;
	caps->max_tiles_y              = RDP_CAPS_MAX_TILES_Y;
	caps->max_width                = RDP_CAPS_MAX_WIDTH;
	caps->max_height               = RDP_CAPS_MAX_HEIGHT;
}

VkDeviceSize rdp_caps_storage_buffer_range(VkDeviceSize device_range)
{
	const char *env = getenv("PARALLEL_RDP_MAX_STORAGE_BUFFER_RANGE");
	if (env)
	{
		VkDeviceSize cap = (VkDeviceSize)strtoul(env, NULL, 0);
		if (cap && cap < device_range)
			return cap;
	}
	return device_range;
}

unsigned rdp_caps_fitting_upscaling_factor(VkDeviceSize range, unsigned factor,
		VkDeviceSize rdram_size, VkDeviceSize hidden_rdram_bytes, bool super_sampled_readback)
{
	for (; factor > 1; factor /= 2)
	{
		const VkDeviceSize f2        = (VkDeviceSize)factor * factor;
		const VkDeviceSize instances = f2 * RDP_CAPS_MAX_TILE_INSTANCES;
		const VkDeviceSize tiles     = f2 * RDP_CAPS_MAX_TILES_X * RDP_CAPS_MAX_TILES_Y;
		VkDeviceSize sizes[5];
		unsigned i;
		bool fits = true;

		/* multisampled RDRAM, with the super-sampled write mask after it */
		sizes[0] = rdram_size * f2 + (super_sampled_readback
				? 2 * RDP_CAPS_MAX_WIDTH * RDP_CAPS_MAX_HEIGHT / 8 : 0);
		sizes[1] = hidden_rdram_bytes * f2;
		/* per-tile shaded colour and depth */
		sizes[2] = sizeof(uint32_t) * instances * RDP_CAPS_TILE_WIDTH * RDP_CAPS_TILE_HEIGHT;
		/* tile binning and per-tile offsets */
		sizes[3] = sizeof(uint32_t) * (RDP_CAPS_MAX_PRIMITIVES / 32) * tiles;
		/* one static raster state's slice of the work list */
		sizes[4] = RDP_CAPS_TILE_RASTER_WORK_SIZE * instances;
		for (i = 0; i < 5; i++)
			fits = fits && sizes[i] <= range;
		if (fits)
			break;
	}
	return factor;
}

bool rdp_caps_init_limits(rdp_renderer_caps_t *caps, unsigned upscaling_factor,
		bool no_storage_8bit, VkDeviceSize range)
{
	VkDeviceSize slice, states;

	caps->no_storage_8bit        = no_storage_8bit;
	caps->max_width              = upscaling_factor * RDP_CAPS_MAX_WIDTH;
	caps->max_height             = upscaling_factor * RDP_CAPS_MAX_HEIGHT;
	caps->max_tiles_x            = upscaling_factor * RDP_CAPS_MAX_TILES_X;
	caps->max_tiles_y            = upscaling_factor * RDP_CAPS_MAX_TILES_Y;
	caps->max_num_tile_instances = upscaling_factor * upscaling_factor * RDP_CAPS_MAX_TILE_INSTANCES;

	/* The work list holds a slice of max_num_tile_instances entries per
	 * static raster state, and tile binning binds all of them at once. */
	slice  = RDP_CAPS_TILE_RASTER_WORK_SIZE * (VkDeviceSize)caps->max_num_tile_instances;
	states = range / slice;
	caps->max_static_raster_states = states < RDP_CAPS_MAX_STATIC_RASTER_STATES
			? (unsigned)states : RDP_CAPS_MAX_STATIC_RASTER_STATES;
	if (caps->max_static_raster_states == 0)
		return false;
	if (caps->max_static_raster_states < RDP_CAPS_MAX_STATIC_RASTER_STATES)
		LOGI("Flushing every %u static raster states: the work list's binding is limited to %llu bytes.\n",
				caps->max_static_raster_states, (unsigned long long)range);
	return true;
}

bool rdp_caps_tile_binning_size_control(const granite_device_features_t *features)
{
	const uint32_t size = features->subgroup_properties.subgroupSize;
	unsigned log2 = 0;

	while (log2 < 31 && ((uint32_t)2 << log2) <= size)
		log2++;
	return granite_subgroup_size_supported(&features->subgroup_size_control_features,
			&features->subgroup_size_control_properties, true, 5, log2);
}

/* A PARALLEL_RDP_* override: -1 when not set */
static int env_override(const char *name)
{
	const char *value = getenv(name);
	return value ? strtol(value, NULL, 0) > 0 : -1;
}

bool rdp_caps_init_features(rdp_renderer_caps_t *caps, const granite_device_features_t *features)
{
	const VkSubgroupFeatureFlags required =
			VK_SUBGROUP_FEATURE_BALLOT_BIT |
			VK_SUBGROUP_FEATURE_BASIC_BIT |
			VK_SUBGROUP_FEATURE_VOTE_BIT |
			VK_SUBGROUP_FEATURE_ARITHMETIC_BIT;
	const VkDriverId driver = features->driver_properties.driverID;
	const uint32_t subgroup_size = features->subgroup_properties.subgroupSize;
	bool allow_subgroup = true;
	bool allow_small_types = true;
	bool forces_small_types = false;
	bool subgroup_ops;
	int value;

	/* The split rasterizer / depth-blend path misrenders textured
	 * primitives on the proprietary Intel Windows driver, unoptimized
	 * shaders included. Keep shading and blending in one pass there.
	 * PARALLEL_RDP_UBERSHADER below still overrides it for comparisons. */
	if (driver == VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS_KHR)
		caps->ubershader = true;

	if ((value = env_override("PARALLEL_RDP_UBERSHADER")) >= 0)
	{
		caps->ubershader = value != 0;
		LOGI("Overriding ubershader = %d\n", value);
	}
	if ((value = env_override("PARALLEL_RDP_FORCE_SYNC_SHADER")) >= 0)
	{
		caps->force_sync = value != 0;
		LOGI("Overriding force sync shader = %d\n", value);
	}
	if ((value = env_override("PARALLEL_RDP_SUBGROUP")) >= 0)
	{
		allow_subgroup = value != 0;
		LOGI("Allow subgroups = %d\n", value);
	}
	if ((value = env_override("PARALLEL_RDP_SMALL_TYPES")) >= 0)
	{
		allow_small_types = value != 0;
		forces_small_types = true;
		LOGI("Allow small types = %d.\n", value);
	}

	if (!features->storage_16bit_features.storageBuffer16BitAccess)
	{
		LOGE("VK_KHR_16bit_storage for SSBOs is not supported! This is a minimum requirement for paraLLEl-RDP.\n");
		return false;
	}

	if (caps->no_storage_8bit)
	{
		LOGI("VK_KHR_8bit_storage for SSBOs is not used, falling back to 16/32-bit storage.\n");
		allow_small_types = false;
		forces_small_types = true;
	}
	else if (!features->storage_8bit_features.storageBuffer8BitAccess)
	{
		LOGE("VK_KHR_8bit_storage for SSBOs is not supported! This is a minimum requirement for paraLLEl-RDP.\n");
		return false;
	}

	/* Driver workarounds for 8/16-bit integer support. Intel ANV *must*
	 * use small integer arithmetic, or it doesn't pass the test suite. */
	if (features->supports_driver_properties && !forces_small_types)
	{
		switch (driver)
		{
			case VK_DRIVER_ID_AMD_PROPRIETARY_KHR:
				LOGW("Current proprietary AMD driver is known to be buggy with 8/16-bit integer arithmetic, disabling support for time being.\n");
				allow_small_types = false;
				break;
			case VK_DRIVER_ID_AMD_OPEN_SOURCE_KHR:
			case VK_DRIVER_ID_MESA_RADV_KHR:
				LOGW("Current open-source AMD drivers are known to be slightly faster without 8/16-bit integer arithmetic.\n");
				allow_small_types = false;
				break;
			case VK_DRIVER_ID_NVIDIA_PROPRIETARY_KHR:
				LOGW("Current NVIDIA driver is known to be slightly faster without 8/16-bit integer arithmetic.\n");
				allow_small_types = false;
				break;
			case VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS_KHR:
				LOGW("Current proprietary Intel Windows driver is tested to perform much better without 8/16-bit integer support.\n");
				allow_small_types = false;
				break;
			case VK_DRIVER_ID_QUALCOMM_PROPRIETARY_KHR:
				LOGW("Current proprietary Qcom driver is known to be buggy with 8/16-bit integer arithmetic, disabling support for time being.\n");
				allow_small_types = false;
				break;
			default:
				break;
		}
	}

	if (!allow_small_types)
		caps->supports_small_integer_arithmetic = false;
	else if (features->enabled_features.shaderInt16 && features->float16_int8_features.shaderInt8)
	{
		LOGI("Enabling 8 and 16-bit integer arithmetic support for more efficient shaders!\n");
		caps->supports_small_integer_arithmetic = true;
	}
	else
	{
		LOGW("Device does not support 8 and 16-bit integer arithmetic support. Falling back to 32-bit arithmetic everywhere.\n");
		caps->supports_small_integer_arithmetic = false;
	}

	subgroup_ops = allow_subgroup &&
			(features->subgroup_properties.supportedOperations & required) == required &&
			(features->subgroup_properties.supportedStages & VK_SHADER_STAGE_COMPUTE_BIT) != 0;
	caps->subgroup_tile_binning = subgroup_ops &&
			rdp_caps_tile_binning_size_control(features) && subgroup_size <= 64;
	caps->subgroup_depth_blend = caps->super_sample_readback && subgroup_ops;
	return true;
}

int rdp_caps_resolve_shader_define(const rdp_renderer_caps_t *caps, const char *name, const char *define)
{
	if (strcmp(define, "UBERSHADER") == 0)
		return caps->ubershader;
	if (strcmp(define, "SMALL_TYPES") == 0)
		return caps->supports_small_integer_arithmetic;
	if (strcmp(define, "NO_STORAGE_8BIT") == 0)
		return caps->no_storage_8bit;
	if (strcmp(define, "SUBGROUP") == 0)
	{
		if (strcmp(name, "tile_binning_combined") == 0)
			return caps->subgroup_tile_binning;
		if (strcmp(name, "depth_blend") == 0 || strcmp(name, "ubershader") == 0)
			return caps->subgroup_depth_blend;
	}
	/* DEBUG_ENABLE and the rest */
	return 0;
}
