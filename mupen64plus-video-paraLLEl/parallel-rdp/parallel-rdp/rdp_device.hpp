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

/* The command processor (rdp_processor.c), for the frontend */

#include "device.hpp"
#include "video_interface.hpp"
#include "rdp_renderer.hpp"
#include "rdp_common.hpp"
#include "upscaler_pass.hpp"
#include "rdp_processor.h"

namespace RDP
{
static_assert(unsigned(Op::MetaSignalTimeline) == RDP_PROCESSOR_SIGNAL_TIMELINE &&
              unsigned(Op::MetaFlush) == RDP_PROCESSOR_FLUSH && unsigned(Op::MetaIdle) == RDP_PROCESSOR_IDLE &&
              unsigned(Op::MetaSetQuirks) == RDP_PROCESSOR_SET_QUIRKS &&
              unsigned(Op::MetaScanoutPrepare) == RDP_PROCESSOR_SCANOUT_PREPARE &&
              unsigned(Op::MetaNextFrameContext) == RDP_PROCESSOR_NEXT_FRAME_CONTEXT &&
              unsigned(Op::MetaCall) == RDP_PROCESSOR_CALL && unsigned(Op::SyncFull) == RDP_PROCESSOR_SYNC_FULL,
              "rdp_processor.h numbers the processor's own commands otherwise.");
static_assert(unsigned(Op::MetaIdle) == 3, "RDP_COMMAND_RING_IDLE_OP");

struct RGBA
{
	uint8_t r, g, b, a;
};

enum CommandProcessorFlagBits
{
	COMMAND_PROCESSOR_FLAG_HOST_VISIBLE_HIDDEN_RDRAM_BIT = RDP_PROCESSOR_HOST_VISIBLE_HIDDEN_RDRAM,
	COMMAND_PROCESSOR_FLAG_HOST_VISIBLE_TMEM_BIT = RDP_PROCESSOR_HOST_VISIBLE_TMEM,
	COMMAND_PROCESSOR_FLAG_UPSCALING_2X_BIT = RDP_PROCESSOR_UPSCALING_2X,
	COMMAND_PROCESSOR_FLAG_UPSCALING_4X_BIT = RDP_PROCESSOR_UPSCALING_4X,
	COMMAND_PROCESSOR_FLAG_UPSCALING_8X_BIT = RDP_PROCESSOR_UPSCALING_8X,
	COMMAND_PROCESSOR_FLAG_SUPER_SAMPLED_READ_BACK_BIT = RDP_PROCESSOR_SUPER_SAMPLED_READ_BACK,
	COMMAND_PROCESSOR_FLAG_SUPER_SAMPLED_DITHER_BIT = RDP_PROCESSOR_SUPER_SAMPLED_DITHER
};
using CommandProcessorFlags = uint32_t;


// These options control various behavior when upscaling to workaround glitches which arise naturally as part of upscaling.
struct Quirks
{
	// RDP_DECODE_* (primitive_decode.h): as rdp_commands.c decodes by them
	uint32_t flags = RDP_DECODE_NATIVE_TEX_RECT;

	// If true, force TEX_RECT and TEX_RECT_FLIP to render without upscaling.
	// Works around bilinear filtering bugs in Cycle1/Cycle2 mode where game assumed 1:1 pixel transfer.
	inline void set_native_resolution_tex_rect(bool enable)
	{
		flags = enable ? (flags | RDP_DECODE_NATIVE_TEX_RECT) : (flags & ~uint32_t(RDP_DECODE_NATIVE_TEX_RECT));
	}

	// Forces LOD to be computed as 1x upscale.
	// Fixes content which relies on LOD computation to select textures in clever ways.
	inline void set_native_texture_lod(bool enable)
	{
		flags = enable ? (flags | RDP_DECODE_NATIVE_TEXTURE_LOD) : (flags & ~uint32_t(RDP_DECODE_NATIVE_TEXTURE_LOD));
	}
};

class CommandProcessor
{
public:
	CommandProcessor(Vulkan::Device &device, void *rdram_ptr, size_t rdram_offset, size_t rdram_size,
	                 size_t hidden_rdram_size, CommandProcessorFlags flags)
	{
		rdp_processor_init(&p, &device, rdram_ptr, rdram_offset, rdram_size, hidden_rdram_size, flags);
	}

	~CommandProcessor()
	{
		rdp_processor_free(&p);
	}

	CommandProcessor(const CommandProcessor &) = delete;
	void operator=(const CommandProcessor &) = delete;

	// Reports, on the command thread, what would crash a real RDP; NULL: none.
	void set_validation_interface(ValidationInterface *iface)
	{
		rdp_processor_set_validation(&p, iface ? report_rdp_crash : nullptr, iface);
	}

	bool device_is_supported() const
	{
		return rdp_processor_supported(&p);
	}

	// Synchronization.
	void flush()
	{
		rdp_processor_flush(&p);
	}

	uint64_t signal_timeline()
	{
		return rdp_processor_signal_timeline(&p);
	}

	void wait_for_timeline(uint64_t index)
	{
		rdp_processor_wait_for_timeline(&p, index);
	}

	void idle()
	{
		rdp_processor_idle(&p);
	}

	void begin_frame_context()
	{
		rdp_processor_begin_frame_context(&p);
	}

	// Runs fn(ctx) on the thread that processes commands (the caller's own
	// thread with single-threaded processing) and returns once it has run.
	// For Vulkan work that has to happen where the renderer drives the
	// device. Must not be called from inside the command thread.
	void run_on_command_thread(void (*fn)(void *), void *ctx)
	{
		rdp_processor_run_on_command_thread(&p, fn, ctx);
	}

	// Queues the pipelines @records lists to the pipeline worker, ahead of
	// their first use, and returns without waiting for any. The thread that
	// queues commands, before the first one.
	void build_pipelines_ahead(const pipeline_record_t *records, size_t count, bool with_upscaler)
	{
		rdp_processor_build_pipelines_ahead(&p, records, count, with_upscaler);
	}

	// Sleeps until the worker has built them. Test hosts only: a frontend
	// never waits, and a draw that gets to one first builds its own.
	void wait_pipelines_ahead_for_test()
	{
		rdp_processor_wait_pipelines_ahead_for_test(&p);
	}

	// Queues up state and drawing commands.
	void enqueue_command(unsigned num_words, const uint32_t *words)
	{
		rdp_processor_enqueue(&p, num_words, words);
	}

	void set_quirks(const Quirks &quirks)
	{
		rdp_processor_set_quirks(&p, quirks.flags);
	}

	// Interact with memory.
	void *begin_read_rdram()
	{
		return rdp_processor_begin_read_rdram(&p);
	}

	void *begin_read_hidden_rdram()
	{
		return rdp_processor_begin_read_hidden_rdram(&p);
	}

	// What it renders at: the factor asked for, or less where the device
	// cannot bind buffers that large.
	unsigned get_scaling_factor() const
	{
		return rdp_processor_scaling_factor(&p);
	}

	// Whether super-sampled read-back is on: as asked, unless the device
	// cannot bind the write mask with the factor.
	bool get_super_sampled_readback() const
	{
		return rdp_processor_super_sampled_readback(&p);
	}

	void set_vi_register(VIRegister reg, uint32_t value)
	{
		rdp_processor_set_vi_register(&p, rdp_vi_register(unsigned(reg)), value);
	}

	Vulkan::ImageHandle scanout(const ScanoutOptions &opts = {})
	{
		return Vulkan::ImageHandle(static_cast<Vulkan::Image *>(
				rdp_processor_scanout(&p, &opts, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)));
	}

	/* After a scanout with ScanoutOptions::upscaler: that frame's motion
	 * vectors, depth and jitter. No images when it had none. */
	void get_upscaler_frame(UpscalerFrame &frame)
	{
		rdp_upscaler_targets_t targets;
		rdp_processor_upscaler_frame(&p, &targets);
		frame.take(targets);
	}

private:
	rdp_processor_t p;

	static void report_rdp_crash(void *user, rdp_validation_error err, const char *msg)
	{
		auto *iface = static_cast<ValidationInterface *>(user);
		iface->report_rdp_crash(iface->user, ValidationError(err), msg);
	}
};
}
