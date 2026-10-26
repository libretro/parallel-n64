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

#pragma once

#include "device.hpp"
#include "temporal_upscaler.h"

namespace RDP
{
/* What a frame scanned out for a temporal upscaler brings beside its
 * color, all of the color's size, in SHADER_READ_ONLY_OPTIMAL */
struct UpscalerFrame
{
	/* R16G16_SFLOAT render pixels, from where a surface is to where it was */
	Vulkan::ImageHandle motion_vectors;
	/* D32_SFLOAT (D16_UNORM without it), 0 near */
	Vulkan::ImageHandle depth;
	float jitter_x;
	float jitter_y;
	bool reset;
};

/* upscaler_color.comp: the framebuffer the VI shows, upscaled RDRAM read
 * into an RGBA8 storage image */
Vulkan::Program *upscaler_color_program(Vulkan::Device &device);

/* Draws @geometry's motion vectors and depth into new images of
 * @width x @height, recorded into @cmd */
void upscaler_motion_pass(Vulkan::Device &device, Vulkan::CommandBuffer &cmd,
                          const rdp_upscaler_geometry_t &geometry,
                          unsigned width, unsigned height, UpscalerFrame &frame);
}
