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

namespace RDP
{
struct ScanoutOptions
{
	bool crop_overscan = false;
	bool persist_frame_on_invalid_input = false;
	struct
	{
		bool aa = true;
		bool scale = true;
		bool serrate = true;
		bool dither_filter = true;
		bool divot_filter = true;
		bool gamma_dither = true;
	} vi;
};

class VideoInterface : public Vulkan::DebugChannelInterface
{
public:
	void set_device(Vulkan::Device *device);
	void set_vi_register(VIRegister reg, uint32_t value);

	void set_rdram(const Vulkan::Buffer *rdram, size_t offset, size_t size);
	void set_hidden_rdram(const Vulkan::Buffer *hidden_rdram);

	int resolve_shader_define(const char *name, const char *define) const;

	Vulkan::ImageHandle scanout(VkImageLayout target_layout, const ScanoutOptions &options = {});
	void set_shader_bank(const ShaderBank *bank);

private:
	Vulkan::Device *device = nullptr;
	uint32_t vi_registers[unsigned(VIRegister::Count)] = {};
	const Vulkan::Buffer *rdram = nullptr;
	const Vulkan::Buffer *hidden_rdram = nullptr;
	Vulkan::BufferHandle gamma_lut;
	Vulkan::BufferViewHandle gamma_lut_view;
	const ShaderBank *shader_bank = nullptr;

	void init_gamma_table();
	bool previous_frame_blank = false;
	bool debug_channel = false;
	int filter_debug_channel_x = -1;
	int filter_debug_channel_y = -1;

	void message(const std::string &tag, uint32_t code,
	             uint32_t x, uint32_t y, uint32_t z,
	             uint32_t num_words, const Vulkan::DebugChannelInterface::Word *words) override;

	// Frame state.
	uint32_t frame_count = 0;
	Vulkan::ImageHandle prev_scanout_image;
	VkImageLayout prev_image_layout = VK_IMAGE_LAYOUT_UNDEFINED;

	size_t rdram_offset = 0;
	size_t rdram_size = 0;
};
}
