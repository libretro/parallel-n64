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


#ifndef RDP_RENDERER_CAPS_H
#define RDP_RENDERER_CAPS_H

/* What the renderer does on a device: the upscaling factor that fits its
 * storage buffer bindings, the buffer sizes that follow from the factor,
 * and the shader variants its driver and features call for. Worked out
 * once, before the first frame. C89. */

#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>
#include "context.h"

RETRO_BEGIN_DECLS

/* The N64's limits as the renderer sizes its buffers for them
 * (Limits and ImplementationConstants in rdp_data_structures.hpp) */
#define RDP_CAPS_MAX_WIDTH 1024u
#define RDP_CAPS_MAX_HEIGHT 1024u
#define RDP_CAPS_TILE_WIDTH 8u
#define RDP_CAPS_TILE_HEIGHT 8u
#define RDP_CAPS_MAX_PRIMITIVES 256u
#define RDP_CAPS_MAX_STATIC_RASTER_STATES 64u
#define RDP_CAPS_MAX_TILE_INSTANCES 0x8000u
/* sizeof(TileRasterWork) */
#define RDP_CAPS_TILE_RASTER_WORK_SIZE 16u

typedef struct rdp_renderer_caps
{
	bool force_sync;
	bool ubershader;
	bool supports_small_integer_arithmetic;
	bool no_storage_8bit;
	bool subgroup_tile_binning;
	bool subgroup_depth_blend;
	bool super_sample_readback;
	bool super_sample_readback_dither;
	unsigned upscaling;
	unsigned max_num_tile_instances;
	/* static raster states per flush: as many as the work list's
	 * per-state slices that fit in one storage buffer binding */
	unsigned max_static_raster_states;
	unsigned max_tiles_x;
	unsigned max_tiles_y;
	unsigned max_width;
	unsigned max_height;
} rdp_renderer_caps_t;

/* Native resolution, nothing enabled */
void rdp_caps_init(rdp_renderer_caps_t *caps);

/* The device's maxStorageBufferRange, lowered by
 * PARALLEL_RDP_MAX_STORAGE_BUFFER_RANGE=bytes to run what a device with a
 * smaller one gets */
VkDeviceSize rdp_caps_storage_buffer_range(VkDeviceSize device_range);

/* @factor, halved until every buffer it scales fits in a binding of
 * @range bytes: the multisampled RDRAM (with the super-sampled write mask
 * after it when @super_sampled_readback), the hidden RDRAM, the per-tile
 * colour and depth, tile binning and one static raster state's slice of
 * the work list. 1 always fits. */
unsigned rdp_caps_fitting_upscaling_factor(VkDeviceSize range, unsigned factor,
		VkDeviceSize rdram_size, VkDeviceSize hidden_rdram_bytes, bool super_sampled_readback);

/* The sizes at @upscaling_factor, and how many static raster states one
 * flush holds in a work list binding of @range bytes. False when not
 * even one does. */
bool rdp_caps_init_limits(rdp_renderer_caps_t *caps, unsigned upscaling_factor,
		bool no_storage_8bit, VkDeviceSize range);

/* Whether tile binning can ask for full subgroups of 32 up to the
 * device's subgroup size: the check its pipeline is built with
 * (granite_subgroup_size_supported) */
bool rdp_caps_tile_binning_size_control(const granite_device_features_t *features);

/* The shader variants for the device: the ubershader, small integer
 * types, subgroup tile binning and depth blending, and the
 * PARALLEL_RDP_UBERSHADER, _FORCE_SYNC_SHADER, _SUBGROUP and
 * _SMALL_TYPES overrides. Run after super_sample_readback is set. False
 * when the device lacks 16-bit storage, or 8-bit storage where it is
 * used. */
bool rdp_caps_init_features(rdp_renderer_caps_t *caps, const granite_device_features_t *features);

/* The value of @define in shader @name */
int rdp_caps_resolve_shader_define(const rdp_renderer_caps_t *caps, const char *name, const char *define);

RETRO_END_DECLS

#endif
