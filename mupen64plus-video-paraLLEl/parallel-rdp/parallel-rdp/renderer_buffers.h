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


#ifndef RDP_RENDERER_BUFFERS_H
#define RDP_RENDERER_BUFFERS_H

/* The renderer's buffers, all made at load: the fixed ones its passes
 * bind, the upscaled copies of RDRAM, and the per-flush streams the CPU
 * fills and the GPU reads. Nothing here allocates once a game runs.
 * C89. */

#include <stdint.h>
#include <stddef.h>
#include <boolean.h>
#include <retro_common_api.h>
#include "device.h"
#include "command_buffer.h"
#include "renderer_caps.h"

RETRO_BEGIN_DECLS

/* The streams of one flush, in the order the renderer fills them */
enum rdp_stream_buffer
{
	RDP_STREAM_TRIANGLE_SETUP = 0,
	RDP_STREAM_ATTRIBUTE_SETUP,
	RDP_STREAM_DERIVED_SETUP,
	RDP_STREAM_SCISSOR_SETUP,
	RDP_STREAM_STATIC_RASTER_STATE,
	RDP_STREAM_DEPTH_BLEND_STATE,
	RDP_STREAM_TILE_INFO_STATE,
	RDP_STREAM_STATE_INDICES,
	RDP_STREAM_SPAN_INFO_OFFSETS,
	RDP_STREAM_SPAN_INFO_JOBS,
	RDP_STREAM_BUFFERS
};

typedef struct rdp_stream_set
{
	granite_buffer_t *buffers[RDP_STREAM_BUFFERS];
	/* mapped for the host */
	bool is_host[RDP_STREAM_BUFFERS];
	/* the span interpolation jobs as RGBA16 texels: the GPU set's only */
	granite_buffer_view_t *span_info_jobs_view;
} rdp_stream_set_t;

/* What the CPU writes (@cpu) and the GPU reads (@gpu). Where the GPU's
 * buffer is mapped for the host, as on integrated GPUs and with
 * resizable BAR, the two are the same buffer and nothing is copied. */
typedef struct rdp_stream_instance
{
	rdp_stream_set_t gpu;
	rdp_stream_set_t cpu;
} rdp_stream_instance_t;

/* The buffers, @sizes bytes each. False when one cannot be made. */
bool rdp_stream_instance_init(rdp_stream_instance_t *inst, granite_device_t *device,
		const VkDeviceSize sizes[RDP_STREAM_BUFFERS]);
void rdp_stream_instance_free(rdp_stream_instance_t *inst);

/* @bytes[i] of @data[i] into the CPU's buffer i, unless it is already
 * there (streams built in place), and on to the GPU's when that is
 * another buffer, followed by one barrier before the compute shaders
 * read them. 0 bytes: nothing. */
void rdp_stream_instance_upload(const rdp_stream_instance_t *inst, granite_device_t *device,
		granite_command_buffer_t *cb, const void *const data[RDP_STREAM_BUFFERS],
		const VkDeviceSize bytes[RDP_STREAM_BUFFERS]);

/* The renderer's fixed buffers */
typedef struct rdp_renderer_buffers
{
	/* the TMEM of each tile load, and the span setups */
	granite_buffer_t *tmem_instances;
	granite_buffer_t *span_setups;
	/* the blender's divider, an R8 texel buffer */
	granite_buffer_t *blender_divider_lut;
	granite_buffer_view_t *blender_divider;
	/* tile binning */
	granite_buffer_t *tile_binning;
	granite_buffer_t *tile_binning_coarse;
	/* the split rasterizer's, none for the ubershader */
	granite_buffer_t *indirect_dispatch;
	granite_buffer_t *per_tile_offsets;
	granite_buffer_t *tile_work_list;
	granite_buffer_t *per_tile_shaded_color;
	granite_buffer_t *per_tile_shaded_depth;
	granite_buffer_t *per_tile_shaded_alpha;
	granite_buffer_t *per_tile_shaded_coverage;
	/* upscaled: RDRAM as the CPU last left it, and the upscaled RDRAM and
	 * hidden RDRAM */
	granite_buffer_t *upscaling_reference_rdram;
	granite_buffer_t *upscaling_multisampled_rdram;
	granite_buffer_t *upscaling_multisampled_hidden_rdram;
} rdp_renderer_buffers_t;

/* The fixed buffers for @caps (any made before go first): TMEM
 * instances of @tmem_instances_size bytes, span setups of
 * @span_setups_size, and the blender's divider from @blender_lut. False
 * when one cannot be made. */
bool rdp_renderer_buffers_init(rdp_renderer_buffers_t *b, granite_device_t *device,
		const rdp_renderer_caps_t *caps, VkDeviceSize tmem_instances_size, VkDeviceSize span_setups_size,
		const uint8_t *blender_lut, VkDeviceSize blender_lut_size);

/* The upscaled copies for @caps->upscaling, of RDRAM @rdram_size and
 * hidden RDRAM @hidden_rdram_size bytes (with the super-sampled write
 * mask after the RDRAM's), the hidden RDRAM's filled with full coverage
 * ahead of any work that reads it. At 1x, none. False when one cannot be
 * made. */
bool rdp_renderer_buffers_init_upscaled(rdp_renderer_buffers_t *b, granite_device_t *device,
		const rdp_renderer_caps_t *caps, VkDeviceSize rdram_size, VkDeviceSize hidden_rdram_size);

void rdp_renderer_buffers_free(rdp_renderer_buffers_t *b);

RETRO_END_DECLS

#endif
