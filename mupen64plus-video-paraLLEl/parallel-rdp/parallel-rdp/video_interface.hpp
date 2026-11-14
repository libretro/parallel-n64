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
#include "temporal_upscaler.h"
#include "upscaler_pass.h"
#include "vi_image_pool.h"
#include "shader_bank.h"

namespace RDP
{
struct ScanoutOptions
{
	// Simple (obsolete) crop method. If crop_rect.enable is false, this
	// crops top / bottom with number of pixels (doubled if interlace),
	// and left / right are cropped in an aspect preserving way.
	// If crop_rect.enable is true,
	// this is ignored and the crop_rect struct is used instead.
	// Crop pixels are adjusted for upscaling, pixels are assumed to
	// be specified for the original resolution.
	unsigned crop_overscan_pixels = 0;

	struct CropRect
	{
		unsigned left = 0;
		unsigned right = 0;
		unsigned top = 0; // Doubled if interlace
		unsigned bottom = 0; // Doubled if interlace
		bool enable = false;
	} crop_rect;

	unsigned downscale_steps = 0;

	// Works around certain game bugs. Considered a hack if enabled.
	bool persist_frame_on_invalid_input = false;

	// To be equivalent to reference behavior where
	// pixels persist for an extra frame.
	// Not hardware accurate, but needed for weave interlace mode.
	bool blend_previous_frame = false;

	// Upscale deinterlacing deinterlaces by upscaling in Y, with an Y coordinate offset matching the field.
	// If disabled, weave interlacing is used.
	// Weave deinterlacing should *not* be used, except to run test suite!
	bool upscale_deinterlacing = true;

	struct
	{
		bool aa = true;
		bool scale = true;
		bool serrate = true;
		bool dither_filter = true;
		bool divot_filter = true;
		bool gamma_dither = true;
	} vi;

	/* For a temporal upscaler: the scanout is the framebuffer as drawn, at
	 * the upscaling factor, without the VI's filters, scaling or crop; the
	 * frame's motion vectors, depth and jitter come with it
	 * (CommandProcessor::get_upscaler_frame). */
	struct
	{
		bool enable = false;
		unsigned jitter_phase = 8;
		bool reset = false;
	} upscaler;
};

struct VIScanoutBuffer
{
	Vulkan::BufferHandle buffer;
	Vulkan::Fence fence;
	unsigned width = 0;
	unsigned height = 0;
};

class Renderer;

class VideoInterface
{
public:
	VideoInterface() = default;
	~VideoInterface();
	VideoInterface(const VideoInterface &) = delete;
	void operator=(const VideoInterface &) = delete;

	/* Hands @add every pipeline a scanout can need. The thread that processes
	 * commands, once, at load. */
	void queue_known_pipelines(unsigned upscaling, bool with_upscaler,
	                           rdp_known_pipeline_fn add, void *ctx);

	void set_device(Vulkan::Device *device);
	void set_renderer(Renderer *renderer);
	void set_vi_register(VIRegister reg, uint32_t value);

	void set_rdram(const Vulkan::Buffer *rdram, size_t offset, size_t size);
	void set_hidden_rdram(const Vulkan::Buffer *hidden_rdram);

	int resolve_shader_define(const char *name, const char *define) const;

	Vulkan::ImageHandle scanout(VkImageLayout target_layout, const ScanoutOptions &options = {}, unsigned scale_factor = 1);
	void scanout_memory_range(unsigned &offset, unsigned &length) const;
	void set_shader_bank(const ShaderBank *bank);
	// Called after each Device::next_frame_context().
	void begin_frame_context();

	/* ScanoutOptions::upscaler's scanout, submitted on the RDP's queue
	 * and waited for by the next graphics submission, ending in
	 * SHADER_READ_ONLY_OPTIMAL; empty when the VI shows nothing. @repeat:
	 * the frame before, shown again. */
	Vulkan::ImageHandle upscaler_scanout(unsigned scale_factor,
	                                     rdp_upscaler_framebuffer_t &fb, bool &repeat);

private:
	void set_extract_vram_pipeline(Vulkan::CommandBuffer &cmd, uint32_t status, unsigned scaling_factor) const;
	void set_fetch_pipeline(Vulkan::CommandBuffer &cmd, uint32_t status, bool fetch_bug) const;
	void set_upscaler_color_pipeline(Vulkan::CommandBuffer &cmd, bool rgba32, unsigned scaling_factor, bool gamma) const;
	void set_divot_pipeline(Vulkan::CommandBuffer &cmd, bool fetch_bug) const;
	void set_scale_pipeline(Vulkan::CommandBuffer &cmd, uint32_t status, bool fetch_bug, bool fold) const;
	void set_blend_fields_pipeline(Vulkan::CommandBuffer &cmd) const;
	void set_deinterlace_pipeline(Vulkan::CommandBuffer &cmd) const;

	Vulkan::Device *device = nullptr;
	Renderer *renderer = nullptr;
	uint32_t vi_registers[unsigned(VIRegister::Count)] = {};

	const Vulkan::Buffer *rdram = nullptr;
	const Vulkan::Buffer *hidden_rdram = nullptr;
	Vulkan::BufferHandle gamma_lut;
	Vulkan::BufferViewHandle gamma_lut_view;
	const ShaderBank *shader_bank = nullptr;
	// vi_scale with the passes after it folded in
	Vulkan::Shader *vi_scale_fold = nullptr;
	bool fold_disabled = false;
	// what it reads as the previous frame when there is none
	Vulkan::ImageHandle no_previous_frame;

	void init_gamma_table();
	bool previous_frame_blank = false;

	// Intermediate images, reused instead of created every frame.
	mutable rdp_vi_image_pool_t image_pool = {};

	// The per-line horizontal state the scale pass reads, decoded straight
	// into a persistently mapped buffer; one is reused once its frame
	// context comes round again, as the images are.
	struct HorizontalBuffer
	{
		Vulkan::BufferHandle buffer;
		Vulkan::BufferViewHandle view;
		uint64_t free_serial;
	};
	enum { MAX_HORIZONTAL_BUFFERS = 32 };
	HorizontalBuffer horizontal_buffers[MAX_HORIZONTAL_BUFFERS] = {};
	HorizontalBuffer *acquire_horizontal_buffer();
	uint64_t frame_serial = 0;
	const rdp_vi_image_entry_t &acquire_image(const Vulkan::ImageCreateInfo &info, bool layer_views) const;
	const rdp_vi_image_entry_t &create_pooled_image(const Vulkan::ImageCreateInfo &info, bool layer_views) const;

	// Frame state.
	uint32_t frame_count = 0;
	uint32_t last_valid_frame_count = 0;
	Vulkan::ImageHandle prev_scanout_image;
	VkImageLayout prev_image_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	Vulkan::ImageHandle prev_upscaler_image;
	rdp_upscaler_framebuffer_t prev_upscaler_fb = {};

	size_t rdram_offset = 0;
	size_t rdram_size = 0;

	static void bind_horizontal_info_view(Vulkan::CommandBuffer &cmd, const HorizontalBuffer &buffer);

	// The VI registers as a scanout uses them (vi_registers.h).
	typedef rdp_vi_decoded_t Registers;

	Vulkan::ImageHandle vram_fetch_stage(const Registers &registers,
	                                     unsigned scaling_factor) const;
	Vulkan::ImageHandle aa_fetch_stage(Vulkan::CommandBuffer &cmd,
	                                   Vulkan::Image &vram_image,
	                                   const Registers &registers,
	                                   unsigned scaling_factor) const;
	Vulkan::ImageHandle divot_stage(Vulkan::CommandBuffer &cmd,
	                                Vulkan::Image &aa_image,
	                                const Registers &registers,
	                                unsigned scaling_factor) const;
	// The passes after the scale pass that it can do itself: its 2:1
	// downscales, and the deinterlace of a bobbed field.
	struct ScaleFold
	{
		unsigned downscale_steps;
		bool deinterlace;
		bool field_select;
	};
	Vulkan::ImageHandle scale_stage(Vulkan::CommandBuffer &cmd,
	                                const Vulkan::Image *divot_image,
	                                Registers registers,
	                                const HorizontalBuffer &lines,
	                                unsigned scaling_factor,
	                                bool degenerate,
	                                const ScanoutOptions &options,
	                                bool final_pass,
	                                const ScaleFold *fold = nullptr) const;
	Vulkan::ImageHandle downscale_stage(Vulkan::CommandBuffer &cmd,
	                                    Vulkan::Image &scale_image,
	                                    unsigned scaling_factor,
	                                    unsigned downscale_factor,
										const ScanoutOptions &options,
										bool final_pass) const;
	Vulkan::ImageHandle upscale_deinterlace(Vulkan::CommandBuffer &cmd,
	                                        Vulkan::Image &scale_image,
	                                        unsigned scaling_factor, bool field_select,
	                                        const ScanoutOptions &options) const;
};
}
