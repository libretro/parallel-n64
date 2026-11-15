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


#ifndef RDP_PROCESSOR_H
#define RDP_PROCESSOR_H

/* The command processor: the RDP's RDRAM, hidden RDRAM and TMEM on the
 * GPU, the commands queued to the thread that runs them (or run where
 * they are queued), the processor's own commands among them (timeline
 * values, flushes, the scanout), and the scanout through the VI. C89. */

#include <stddef.h>
#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>
#include "device.h"
#include "image.h"
#include "shader_bank.h"
#include "command_ring.h"
#include "rdp_dump_write.h"
#include "rdp_renderer.h"
#include "rdp_commands.h"
#include "rdp_timeline.h"
#include "temporal_upscaler.h"
#include "upscaler_pass.h"
#include "video_interface.h"

RETRO_BEGIN_DECLS

/* The opcodes the RDP does not have (1 to 7): the processor's own. Game
 * data never runs them (rdp_processor_enqueue drops them). */
enum rdp_processor_op
{
	RDP_PROCESSOR_SIGNAL_TIMELINE = 1,
	RDP_PROCESSOR_FLUSH = 2,
	/* (command_ring.c ticks with it) */
	RDP_PROCESSOR_IDLE = 3,
	RDP_PROCESSOR_SET_QUIRKS = 4,
	RDP_PROCESSOR_SCANOUT_PREPARE = 5,
	RDP_PROCESSOR_NEXT_FRAME_CONTEXT = 6,
	RDP_PROCESSOR_CALL = 7
};

#define RDP_PROCESSOR_SYNC_FULL 0x29u

/* rdp_processor_init's @flags */
#define RDP_PROCESSOR_HOST_VISIBLE_HIDDEN_RDRAM (1u << 0)
#define RDP_PROCESSOR_HOST_VISIBLE_TMEM (1u << 1)
#define RDP_PROCESSOR_UPSCALING_2X (1u << 2)
#define RDP_PROCESSOR_UPSCALING_4X (1u << 3)
#define RDP_PROCESSOR_UPSCALING_8X (1u << 4)
#define RDP_PROCESSOR_SUPER_SAMPLED_READ_BACK (1u << 5)
#define RDP_PROCESSOR_SUPER_SAMPLED_DITHER (1u << 6)

typedef struct rdp_processor
{
	granite_device_t *device;
	granite_buffer_t *rdram;
	granite_buffer_t *hidden_rdram;
	granite_buffer_t *tmem;
	size_t rdram_offset;
	size_t rdram_size;
	uint32_t flags;
	/* the host's RDRAM, when the GPU's is not it */
	uint8_t *host_rdram;
	bool is_host_coherent;
	bool is_supported;
	bool super_sampled_readback;
	/* hidden RDRAM is 16 bits per entry */
	bool no_storage_8bit;
	bool single_threaded;

	/* outlives the renderer, whose pipeline worker reads it */
	rdp_shader_bank_t shader_bank;
	rdp_renderer_t renderer;
	rdp_vi_t vi;
	/* or NULL */
	rdp_upscaler_t *upscaler;
	rdp_commands_t commands;

	/* the scanout asked for and its result, handed across with the
	 * scanout command (written before it is queued, read after the
	 * drain) */
	rdp_vi_scanout_options_t scanout_options;
	VkImageLayout scanout_layout;
	granite_image_t *scanout_result;
	/* that scanout's motion vectors, depth and jitter, with the
	 * processor's references */
	rdp_upscaler_targets_t upscaler_frame;

	/* NULL when commands run on the caller's thread */
	rdp_command_ring_t *ring;
	rdp_timeline_t timeline;
	uint64_t timeline_value;

	rdp_dump_writer_t *dump_writer;
	bool dump_in_command_list;
} rdp_processor_t;

/* @rdram_ptr (or NULL: RDRAM of its own) of @rdram_size bytes at
 * @rdram_offset, imported where the device can. Whether the device can
 * run it all: rdp_processor_supported. */
void rdp_processor_init(rdp_processor_t *p, granite_device_t *device, void *rdram_ptr, size_t rdram_offset,
		size_t rdram_size, size_t hidden_rdram_size, uint32_t flags);
/* Finishes what is queued first */
void rdp_processor_free(rdp_processor_t *p);

bool rdp_processor_supported(const rdp_processor_t *p);
unsigned rdp_processor_scaling_factor(const rdp_processor_t *p);
bool rdp_processor_super_sampled_readback(const rdp_processor_t *p);
/* NULL: none (rdp_renderer_set_validation), on the command thread */
void rdp_processor_set_validation(rdp_processor_t *p, rdp_report_crash_fn report, void *user);

/* ---- the thread that queues commands ---- */
/* A command of the game's: the processor's own opcodes are dropped */
void rdp_processor_enqueue(rdp_processor_t *p, unsigned num_words, const uint32_t *words);
/* Runs a command where the commands run: the command thread's side */
void rdp_processor_run(rdp_processor_t *p, unsigned num_words, const uint32_t *words);
/* RDP_DECODE_NATIVE_TEXTURE_LOD and RDP_DECODE_NATIVE_TEX_RECT */
void rdp_processor_set_quirks(rdp_processor_t *p, uint32_t quirks);
void rdp_processor_flush(rdp_processor_t *p);
uint64_t rdp_processor_signal_timeline(rdp_processor_t *p);
void rdp_processor_wait_for_timeline(rdp_processor_t *p, uint64_t value);
/* Flushes and waits for what is queued */
void rdp_processor_idle(rdp_processor_t *p);
void rdp_processor_begin_frame_context(rdp_processor_t *p);
/* fn(ctx) where the commands run; returns once it has. Never from the
 * command thread. */
void rdp_processor_run_on_command_thread(rdp_processor_t *p, void (*fn)(void *), void *ctx);
void rdp_processor_build_pipelines_ahead(rdp_processor_t *p, const pipeline_record_t *records, size_t count,
		bool with_upscaler);
void rdp_processor_wait_pipelines_ahead_for_test(rdp_processor_t *p);

void rdp_processor_set_vi_register(rdp_processor_t *p, enum rdp_vi_register reg, uint32_t value);
/* The VI's image (a reference for the caller, or NULL), in @layout */
granite_image_t *rdp_processor_scanout(rdp_processor_t *p, const rdp_vi_scanout_options_t *options,
		VkImageLayout layout);
/* After a scanout with the upscaler: its targets, with references for
 * the caller */
void rdp_processor_upscaler_frame(rdp_processor_t *p, rdp_upscaler_targets_t *frame);

void *rdp_processor_begin_read_rdram(rdp_processor_t *p);
void *rdp_processor_begin_read_hidden_rdram(rdp_processor_t *p);

RETRO_END_DECLS

#endif
