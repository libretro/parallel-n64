/* Copyright (c) 2026 The RetroArch team
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

#ifndef RDP_UPSCALER_PASS_H
#define RDP_UPSCALER_PASS_H

#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>
#include "device.h"
#include "temporal_upscaler.h"

RETRO_BEGIN_DECLS

/* What a frame scanned out for a temporal upscaler brings beside its
 * color, all of the color's size, in SHADER_READ_ONLY_OPTIMAL. Each
 * image is a reference of the holder's, or all three are NULL. */
typedef struct rdp_upscaler_targets
{
	/* R16G16_SFLOAT render pixels, from where a surface is to where it was */
	granite_image_t *motion_vectors;
	/* D32_SFLOAT (D16_UNORM without it), 0 near */
	granite_image_t *depth;
	/* R8_UNORM, 1 where a translucent primitive (RDP_UPSCALER_TRANSLUCENT)
	 * is the last drawn */
	granite_image_t *translucent;
	float jitter_x;
	float jitter_y;
	bool reset;
} rdp_upscaler_targets_t;

/* Called for each pipeline a command buffer is set up to build */
typedef void (*rdp_known_pipeline_fn)(void *ctx, granite_command_buffer_t *cb, uint32_t kind);

/* upscaler_color.comp: the framebuffer the VI shows, upscaled RDRAM read
 * into an RGBA8 storage image */
granite_program_t *rdp_upscaler_color_program(granite_device_t *device);

/* Hands @add the motion pass's pipelines, on 1x1 targets. At load. */
void rdp_upscaler_queue_known_pipelines(granite_device_t *device, granite_command_buffer_t *cb,
		rdp_known_pipeline_fn add, void *ctx);

/* Draws @geometry's motion vectors and depth into new images of
 * @width x @height, recorded into @cb, and fills @targets, which holds
 * nothing. Without memory for the images it is left with none. */
void rdp_upscaler_motion_pass(granite_device_t *device, granite_command_buffer_t *cb,
		const rdp_upscaler_geometry_t *geometry, unsigned width, unsigned height,
		rdp_upscaler_targets_t *targets);

RETRO_END_DECLS

#endif
