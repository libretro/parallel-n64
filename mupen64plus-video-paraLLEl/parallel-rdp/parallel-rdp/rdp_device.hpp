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

#include "device.hpp"
#include "video_interface.hpp"
#include "rdp_renderer.hpp"
#include "rdp_command_decode.h"
#include "rdp_timeline.h"
#include "rdp_common.hpp"
#include "command_ring.h"
#include "rdp_worker.h"
#include <retro_atomic.h>
#include "rdp_dump_write.h"
#include "upscaler_pass.hpp"
#include "temporal_upscaler.h"
#include "rdp_upscaler_view.h"
#include "rdp_commands.h"


namespace RDP
{
struct RGBA
{
	uint8_t r, g, b, a;
};

enum CommandProcessorFlagBits
{
	COMMAND_PROCESSOR_FLAG_HOST_VISIBLE_HIDDEN_RDRAM_BIT = 1 << 0,
	COMMAND_PROCESSOR_FLAG_HOST_VISIBLE_TMEM_BIT = 1 << 1,
	COMMAND_PROCESSOR_FLAG_UPSCALING_2X_BIT = 1 << 2,
	COMMAND_PROCESSOR_FLAG_UPSCALING_4X_BIT = 1 << 3,
	COMMAND_PROCESSOR_FLAG_UPSCALING_8X_BIT = 1 << 4,
	COMMAND_PROCESSOR_FLAG_SUPER_SAMPLED_READ_BACK_BIT = 1 << 5,
	COMMAND_PROCESSOR_FLAG_SUPER_SAMPLED_DITHER_BIT = 1 << 6
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
	CommandProcessor(Vulkan::Device &device,
	                 void *rdram_ptr,
	                 size_t rdram_offset,
	                 size_t rdram_size,
	                 size_t hidden_rdram_size,
	                 CommandProcessorFlags flags);

	~CommandProcessor();

	void set_validation_interface(ValidationInterface *iface);

	bool device_is_supported() const;

	// Synchronization.
	void flush();
	uint64_t signal_timeline();
	void wait_for_timeline(uint64_t index);
	void idle();
	void begin_frame_context();

	// Runs fn(ctx) on the thread that processes commands (the caller's own
	// thread with single-threaded processing) and returns once it has run.
	// For Vulkan work that has to happen where the renderer drives the
	// device. Must not be called from inside the command thread.
	void run_on_command_thread(void (*fn)(void *), void *ctx);

	// Queues the pipelines @records lists to the pipeline worker, ahead of
	// their first use, and returns without waiting for any. The thread that
	// queues commands, before the first one.
	void build_pipelines_ahead(const pipeline_record_t *records, size_t count, bool with_upscaler);
	// Sleeps until the worker has built them. Test hosts only: a frontend
	// never waits, and a draw that gets to one first builds its own.
	void wait_pipelines_ahead_for_test();

	// Queues up state and drawing commands.
	void enqueue_command(unsigned num_words, const uint32_t *words);
	void enqueue_command_direct(unsigned num_words, const uint32_t *words);

	void set_quirks(const Quirks &quirks);

	// Interact with memory.
	void *begin_read_rdram();
	void end_write_rdram();
	void *begin_read_hidden_rdram();
	void end_write_hidden_rdram();
	size_t get_rdram_size() const;
	size_t get_hidden_rdram_size() const;
	// What it renders at: the factor asked for, or less where the device
	// cannot bind buffers that large.
	unsigned get_scaling_factor() const;
	// Whether super-sampled read-back is on: as asked, unless the device
	// cannot bind the write mask with the factor.
	bool get_super_sampled_readback() const;
	void *get_tmem();

	// Sets VI register
	void set_vi_register(VIRegister reg, uint32_t value);

	Vulkan::ImageHandle scanout(const ScanoutOptions &opts = {});
	/* After a scanout with ScanoutOptions::upscaler: that frame's motion
	 * vectors, depth and jitter. No images when it had none. */
	void get_upscaler_frame(UpscalerFrame &frame);
	void scanout_async_buffer(VIScanoutBuffer &buffer, const ScanoutOptions &opts = {});

private:
	Vulkan::Device &device;
	Vulkan::BufferHandle rdram;
	Vulkan::BufferHandle hidden_rdram;
	Vulkan::BufferHandle tmem;
	size_t rdram_offset;
	size_t rdram_size;
	CommandProcessorFlags flags;
	// Outlives the renderer below, whose pipeline worker reads it.
	rdp_shader_bank_t shader_bank = {};

	// Tear-down order is important here.
	Renderer renderer;
	VideoInterface vi;
	// Scanout request/result, handed across with the MetaScanoutPrepare
	// command (written before it is queued / read after the drain).
	ScanoutOptions scanout_opts;
	VkImageLayout scanout_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	Vulkan::ImageHandle scanout_result;
	UpscalerFrame upscaler_frame = {};
	// NULL when commands run on the caller's thread
	rdp_command_ring_t *ring = nullptr;
	static void run_ring_command(void *self, unsigned num_words, const uint32_t *words);

	void clear_hidden_rdram();
	void clear_tmem();
	void clear_buffer(Vulkan::Buffer &buffer, uint32_t value);
	void init_renderer();
	void enqueue_command_inner(unsigned num_words, const uint32_t *words);

	Vulkan::ImageHandle scanout(const ScanoutOptions &opts, VkImageLayout target_layout);




	uint64_t timeline_value = 0;
	// The timeline worker (rdp_timeline.c): waits on fences, copies GPU
	// writes back to RDRAM and publishes timeline values
	rdp_timeline_t timeline;

	uint8_t *host_rdram = nullptr;
	bool single_threaded_processing = false;
	bool is_supported = false;
	bool super_sampled_readback = false;
	// Hidden RDRAM is 16 bits per entry (see RendererOptions::no_storage_8bit).
	bool no_storage_8bit = false;
	void flush_hidden_dram_to_dump();
	bool is_host_coherent = true;

	void enqueue_coherency_operation(const CoherencyOperation &op);
	void drain_command_ring();


	/* The temporal upscaler's view of the state triangles are drawn under */
	rdp_upscaler_t *upscaler = nullptr;
	// The RDP's commands, run (rdp_commands.c)
	rdp_commands_t commands;
	void upscaler_scanout();

	rdp_dump_writer_t *dump_writer = nullptr;
#if defined(__GNUC__)
	__attribute__((noinline, cold))
#elif defined(_MSC_VER)
	__declspec(noinline)
#endif
	void enqueue_command_dumped(unsigned num_words, const uint32_t *words);
	bool dump_in_command_list = false;
};
}
