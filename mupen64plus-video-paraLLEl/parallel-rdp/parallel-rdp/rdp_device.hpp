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
#include "rdp_common.hpp"
#include "command_ring.h"
#include "rdp_worker.h"
#include <retro_atomic.h>
#include "rdp_dump_write.h"
#include "upscaler_pass.hpp"
#include "temporal_upscaler.h"


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

struct CoherencyCopy
{
	size_t src_offset = 0;
	size_t mask_offset = 0;
	size_t dst_offset = 0;
	size_t size = 0;
	retro_atomic_int_t *counter_base = nullptr;
	unsigned counters = 0;
};

struct CoherencyOperation
{
	Vulkan::Fence fence;
	uint64_t timeline_value = 0;

	uint8_t *dst = nullptr;
	const Vulkan::Buffer *src = nullptr;
	// num_copies of them, in storage of the producer's until enqueued
	CoherencyCopy *copies = nullptr;
	unsigned num_copies = 0;
	retro_atomic_int_t *unlock_cookie = nullptr;
};

// These options control various behavior when upscaling to workaround glitches which arise naturally as part of upscaling.
struct Quirks
{
	inline Quirks()
	{
		u.options.native_resolution_tex_rect = true;
		u.options.native_texture_lod = false;
	}

	inline void set_native_resolution_tex_rect(bool enable)
	{
		u.options.native_resolution_tex_rect = enable;
	}

	inline void set_native_texture_lod(bool enable)
	{
		u.options.native_texture_lod = enable;
	}

	union
	{
		struct Opts
		{
			// If true, force TEX_RECT and TEX_RECT_FLIP to render without upscaling.
			// Works around bilinear filtering bugs in Cycle1/Cycle2 mode where game assumed 1:1 pixel transfer.
			bool native_resolution_tex_rect;

			// Forces LOD to be computed as 1x upscale.
			// Fixes content which relies on LOD computation to select textures in clever ways.
			bool native_texture_lod;
		} options;
		uint32_t words[1];
	} u;
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

#define OP(x) void op_##x(const uint32_t *words)
	OP(triangle);
	OP(texture_rectangle); OP(texture_rectangle_flip); OP(sync_load); OP(sync_pipe);
	OP(sync_tile); OP(sync_full); OP(set_key_gb); OP(set_key_r);
	OP(set_convert); OP(set_scissor); OP(set_prim_depth); OP(set_other_modes);
	OP(load_tlut); OP(set_tile_size); OP(load_block);
	OP(load_tile); OP(set_tile); OP(fill_rectangle); OP(set_fill_color);
	OP(set_fog_color); OP(set_blend_color); OP(set_prim_color); OP(set_env_color);
	OP(set_combine); OP(set_texture_image); OP(set_mask_image); OP(set_color_image);
#undef OP

	ScissorState scissor_state = {};
	StaticRasterizationState static_state = {};
	DepthBlendState depth_blend = {};

	struct
	{
		uint32_t addr;
		uint32_t width;
		uint8_t fmt;  /* enum rdp_texture_format */
		uint8_t size; /* enum rdp_texture_size */
	} texture_image = {};

	uint64_t timeline_value = 0;
	// Last timeline value the timeline worker has passed. Free-running
	// size_t, compared by difference, so it may wrap on 32-bit hosts.
	retro_atomic_size_t thread_timeline_value{0};

	// The nodes of the timeline worker's work, handed back by it once done
	// and reused by the thread that queues them: no allocation per
	// operation once enough are about. Destroyed after the worker below.
	struct CoherencyNodePool
	{
		mpsc_stack_t returned;
		// the queuing thread's own, taken from returned
		mpsc_stack_node_t *spare = nullptr;
		CoherencyNodePool() { mpsc_stack_init(&returned); }
		~CoherencyNodePool();
	} coherency_nodes;

	// The timeline worker (rdp_worker.c): waits on fences, copies GPU
	// writes back to RDRAM and publishes timeline values. Owned so that
	// it is freed here, at the same point the member was destroyed.
	struct TimelineWorker
	{
		rdp_worker_t *worker = nullptr;
		~TimelineWorker() { rdp_worker_free(worker); }
	} timeline_worker;
	static void timeline_work(void *user, mpsc_stack_node_t *item);
	static bool timeline_reached(void *ctx);
	void perform_coherency(CoherencyOperation &work);

	uint8_t *host_rdram = nullptr;
	bool single_threaded_processing = false;
	bool is_supported = false;
	bool super_sampled_readback = false;
	// Hidden RDRAM is 16 bits per entry (see RendererOptions::no_storage_8bit).
	bool no_storage_8bit = false;
	void flush_hidden_dram_to_dump();
	bool is_host_coherent = true;

	friend class Renderer;

	void enqueue_coherency_operation(CoherencyOperation &&op);
	void drain_command_ring();
	unsigned decode_flags() const;
	void texture_rectangle(const uint32_t *words, bool flip);

	Quirks quirks;

	/* The temporal upscaler's view of the state triangles are drawn under */
	rdp_upscaler_t *upscaler = nullptr;
	uint32_t other_modes_words[2] = {};
	uint32_t combine_words[2] = {};
	uint32_t tile_words[8][2] = {};
	uint32_t load_addr = 0;
	int32_t prim_z = 0;
	bool prim_depth = false;
	void upscaler_triangle(unsigned op, TriangleSetup &setup, AttributeSetup &attr);
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
