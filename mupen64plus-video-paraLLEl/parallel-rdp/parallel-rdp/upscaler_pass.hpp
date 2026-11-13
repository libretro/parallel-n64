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
#include "upscaler_pass.h"

namespace RDP
{
/* rdp_upscaler_targets_t (upscaler_pass.h), with the images held by
 * handles */
struct UpscalerFrame
{
	Vulkan::ImageHandle motion_vectors;
	Vulkan::ImageHandle depth;
	Vulkan::ImageHandle translucent;
	float jitter_x;
	float jitter_y;
	bool reset;

	/* back to none, in place */
	void clear()
	{
		motion_vectors.reset();
		depth.reset();
		translucent.reset();
		jitter_x = 0.0f;
		jitter_y = 0.0f;
		reset = false;
	}

	/* takes the references @targets holds */
	void take(const rdp_upscaler_targets_t &targets)
	{
		motion_vectors = Vulkan::ImageHandle(static_cast<Vulkan::Image *>(targets.motion_vectors));
		depth = Vulkan::ImageHandle(static_cast<Vulkan::Image *>(targets.depth));
		translucent = Vulkan::ImageHandle(static_cast<Vulkan::Image *>(targets.translucent));
		jitter_x = targets.jitter_x;
		jitter_y = targets.jitter_y;
		reset = targets.reset;
	}
};
}
