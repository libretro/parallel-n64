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

#pragma once

#include <stdint.h>
#include "device.hpp"
#include "rdp_common.hpp"
#include "video_interface.h"

namespace RDP
{
// rdp_vi_scanout_options_t (video_interface.h), with its defaults.
struct ScanoutOptions : rdp_vi_scanout_options_t
{
	ScanoutOptions()
	{
		rdp_vi_scanout_options_init(this);
	}
};

struct VIScanoutBuffer
{
	Vulkan::BufferHandle buffer;
	Vulkan::Fence fence;
	unsigned width = 0;
	unsigned height = 0;
};

// The video interface (video_interface.h), with handles for what it gives.
class VideoInterface
{
public:
	VideoInterface() = default;
	~VideoInterface()
	{
		rdp_vi_free(&vi);
	}
	VideoInterface(const VideoInterface &) = delete;
	void operator=(const VideoInterface &) = delete;

	void queue_known_pipelines(unsigned upscaling, bool with_upscaler,
	                           rdp_known_pipeline_fn add, void *ctx)
	{
		rdp_vi_queue_known_pipelines(&vi, upscaling, with_upscaler, add, ctx);
	}

	void set_device(Vulkan::Device *device)
	{
		rdp_vi_set_device(&vi, device);
	}

	void set_upscaled_source(const rdp_vi_upscaled_source_t &source)
	{
		vi.upscaled = source;
	}

	void set_vi_register(VIRegister reg, uint32_t value)
	{
		rdp_vi_set_register(&vi, rdp_vi_register(unsigned(reg)), value);
	}

	void set_rdram(const Vulkan::Buffer *rdram, size_t offset, size_t size)
	{
		rdp_vi_set_rdram(&vi, rdram, offset, size);
	}

	void set_hidden_rdram(const Vulkan::Buffer *hidden_rdram)
	{
		vi.hidden_rdram = hidden_rdram;
	}

	int resolve_shader_define(const char *, const char *) const
	{
		return 0;
	}

	Vulkan::ImageHandle scanout(VkImageLayout target_layout, const ScanoutOptions &options = {}, unsigned scale_factor = 1)
	{
		return Vulkan::ImageHandle(static_cast<Vulkan::Image *>(
				rdp_vi_scanout(&vi, target_layout, &options, scale_factor)));
	}

	void scanout_memory_range(unsigned &offset, unsigned &length) const
	{
		rdp_vi_scanout_memory_range(vi.registers, &offset, &length);
	}

	void set_shader_bank(const ShaderBank *bank)
	{
		rdp_vi_set_shader_bank(&vi, bank);
	}

	// Called after each Device::next_frame_context().
	void begin_frame_context()
	{
		rdp_vi_begin_frame_context(&vi);
	}

	Vulkan::ImageHandle upscaler_scanout(unsigned scale_factor,
	                                     rdp_upscaler_framebuffer_t &fb, bool &repeat)
	{
		return Vulkan::ImageHandle(static_cast<Vulkan::Image *>(
				rdp_vi_upscaler_scanout(&vi, scale_factor, &fb, &repeat)));
	}

private:
	rdp_vi_t vi = {};
};
}
