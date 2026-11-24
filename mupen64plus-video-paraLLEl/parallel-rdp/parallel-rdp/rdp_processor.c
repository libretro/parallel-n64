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


#include <stdlib.h>
#include <string.h>
#include "rdp_processor.h"
#include "command_buffer.h"
#include "fence.h"
#include "logging.h"

/* An environment override: whether @name is set above 0, @fallback when
 * it is not set */
static bool env_flag(const char *name, bool fallback)
{
	const char *env = getenv(name);
	return env ? strtol(env, NULL, 0) > 0 : fallback;
}

/* The VI's shaders have no defines; the rest are the renderer's */
static int resolve_shader_define(void *user, const char *name, const char *define)
{
	if (strncmp(name, "vi_", 3) == 0)
		return 0;
	return rdp_renderer_resolve_shader_define((const rdp_renderer_t *)user, name, define);
}

/* Where the VI reads an upscaled frame from */
static void vi_update_upscaled(void *user, granite_command_buffer_t *cb, unsigned addr, unsigned pixels,
		unsigned pixel_size_log2)
{
	rdp_renderer_update_upscaled_domain((rdp_renderer_t *)user, cb, addr, pixels, pixel_size_log2);
}

static const granite_buffer_t *vi_upscaled_rdram(void *user)
{
	return rdp_renderer_upscaled_rdram((const rdp_renderer_t *)user);
}

static const granite_buffer_t *vi_upscaled_hidden_rdram(void *user)
{
	return rdp_renderer_upscaled_hidden_rdram((const rdp_renderer_t *)user);
}

static granite_buffer_t *create_buffer(granite_device_t *device, VkDeviceSize size, VkBufferUsageFlags usage,
		granite_buffer_domain_t domain, uint32_t misc)
{
	granite_buffer_info_t info;
	memset(&info, 0, sizeof(info));
	info.size = size;
	info.usage = usage;
	info.domain = domain;
	info.misc = misc;
	return granite_device_create_buffer(device, &info, NULL);
}

static void clear_buffer(rdp_processor_t *p, granite_buffer_t *buffer, uint32_t value)
{
	if (!buffer)
		return;
	if (!buffer->alloc.host_base)
	{
		granite_command_buffer_t *cb = granite_device_request_command_buffer(p->device, GRANITE_QUEUE_GRAPHICS);
		granite_fence_t *fence = NULL;
		if (!cb)
			return;
		granite_cb_fill_buffer(cb, buffer, value, 0, VK_WHOLE_SIZE);
		granite_device_submit(p->device, cb, &fence, 0, NULL);
		if (fence)
		{
			granite_fence_wait_for(fence);
			granite_fence_unref(fence);
		}
	}
	else
	{
		/* (the whole 32-bit value, as vkCmdFillBuffer writes it: 16-bit
		 * hidden RDRAM is 0x0003 an entry, not 0x0303) */
		uint32_t *mapped = (uint32_t *)granite_device_map_host_buffer(p->device, buffer,
				GRANITE_MEMORY_ACCESS_WRITE_BIT, 0, buffer->info.size);
		const size_t words = (size_t)(buffer->info.size / 4);
		size_t i;
		for (i = 0; i < words; i++)
			mapped[i] = value;
		granite_device_unmap_host_buffer(p->device, buffer, GRANITE_MEMORY_ACCESS_WRITE_BIT, 0, buffer->info.size);
	}
}

static void create_rdram(rdp_processor_t *p, void *rdram_ptr, size_t rdram_offset)
{
	const VkBufferUsageFlags storage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

	if (!rdram_ptr)
	{
		p->rdram = create_buffer(p->device, p->rdram_size, storage,
				GRANITE_BUFFER_DOMAIN_CACHED_COHERENT_HOST_PREFER_CACHED, GRANITE_BUFFER_MISC_ZERO_INITIALIZE_BIT);
		return;
	}

	if (env_flag("PARALLEL_RDP_ALLOW_EXTERNAL_HOST", true) && p->device->ext.supports_external_memory_host)
	{
		const size_t align = (size_t)p->device->ext.host_memory_properties.minImportedHostPointerAlignment;
		granite_buffer_info_t info;
		memset(&info, 0, sizeof(info));
		info.size = (p->rdram_size + rdram_offset + align - 1) & ~(align - 1);
		info.usage = storage;
		info.domain = GRANITE_BUFFER_DOMAIN_CACHED_COHERENT_HOST_PREFER_CACHED;
		info.misc = GRANITE_BUFFER_MISC_ZERO_INITIALIZE_BIT;
		p->rdram = granite_device_create_imported_host_buffer(p->device, &info, rdram_ptr);
		if (!p->rdram)
			LOGE("Failed to allocate RDRAM with VK_EXT_external_memory_host.\n");
	}

	if (!p->rdram)
	{
		LOGW("VK_EXT_external_memory_host not supported or failed, falling back to a slower path.\n");
		p->is_host_coherent = false;
		p->rdram_offset = 0;
		p->host_rdram = (uint8_t *)rdram_ptr + rdram_offset;
		/* twice the size: the data, then its write mask */
		p->rdram = create_buffer(p->device, (VkDeviceSize)p->rdram_size * 2,
				storage | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				p->device->gpu_props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU
						? GRANITE_BUFFER_DOMAIN_CACHED_COHERENT_HOST_PREFER_CACHED : GRANITE_BUFFER_DOMAIN_DEVICE,
				GRANITE_BUFFER_MISC_ZERO_INITIALIZE_BIT);
	}
}

static void init_renderer(rdp_processor_t *p)
{
	rdp_renderer_options_t options;
	rdp_vi_upscaled_source_t source;
	unsigned factor = 1, plain;
	VkDeviceSize hidden_size;

	p->is_supported = false;
	if (!p->rdram || !p->hidden_rdram || !p->tmem || !p->timeline.worker)
		return;

	rdp_renderer_set_device(&p->renderer, p->device);
	if (!rdp_renderer_set_rdram(&p->renderer, p->rdram, p->host_rdram, p->rdram_offset, p->rdram_size,
			p->is_host_coherent))
		return;
	rdp_renderer_set_hidden_rdram(&p->renderer, p->hidden_rdram);
	rdp_renderer_set_tmem(&p->renderer, p->tmem);

	if (p->flags & RDP_PROCESSOR_UPSCALING_8X)
		factor = 8;
	else if (p->flags & RDP_PROCESSOR_UPSCALING_4X)
		factor = 4;
	else if (p->flags & RDP_PROCESSOR_UPSCALING_2X)
		factor = 2;

	/* Where the device's storage buffer bindings are too small for the
	 * factor, the factor comes down; but where only the super-sampled
	 * read-back's write mask (which sits after the upscaled RDRAM) is what
	 * does not fit, the read-back goes rather than half the resolution. */
	hidden_size = p->hidden_rdram->info.size;
	plain = rdp_renderer_fitting_upscaling_factor(p->device, factor, p->rdram_size, hidden_size, false);
	p->super_sampled_readback = (p->flags & RDP_PROCESSOR_SUPER_SAMPLED_READ_BACK) != 0;
	if (p->super_sampled_readback)
	{
		unsigned with_ssaa = rdp_renderer_fitting_upscaling_factor(p->device, factor, p->rdram_size, hidden_size,
				true);
		if (with_ssaa < plain || with_ssaa == 1)
		{
			LOGW("Super-sampled read-back does not fit this device's storage buffer bindings at %ux: off.\n", plain);
			p->super_sampled_readback = false;
		}
	}
	if (plain < factor)
		LOGW("Upscaling %ux does not fit this device's storage buffer bindings: %ux.\n", factor, plain);
	factor = plain;
	if (factor != 1)
		LOGI("Enabling upscaling: %ux.\n", factor);

	options.upscaling_factor = factor;
	options.super_sampled_readback = p->super_sampled_readback;
	options.super_sampled_readback_dither = p->super_sampled_readback
			&& (p->flags & RDP_PROCESSOR_SUPER_SAMPLED_DITHER) != 0;
	options.no_storage_8bit = p->no_storage_8bit;
	options.no_storage_16bit = p->no_storage_16bit;
	p->is_supported = rdp_renderer_init_renderer(&p->renderer, &options);

	rdp_vi_set_device(&p->vi, p->device);
	rdp_vi_set_rdram(&p->vi, p->rdram, p->rdram_offset, p->rdram_size);
	p->vi.hidden_rdram = p->hidden_rdram;
	source.user = &p->renderer;
	source.update = vi_update_upscaled;
	source.rdram = vi_upscaled_rdram;
	source.hidden_rdram = vi_upscaled_hidden_rdram;
	p->vi.upscaled = source;

	rdp_shader_bank_init(&p->shader_bank, p->device, resolve_shader_define, &p->renderer);
	rdp_renderer_set_shader_bank(&p->renderer, &p->shader_bank);
	rdp_vi_set_shader_bank(&p->vi, &p->shader_bank);
}

static void run_ring_command(void *self, unsigned num_words, const uint32_t *words)
{
	rdp_processor_run((rdp_processor_t *)self, num_words, words);
}

void rdp_processor_init(rdp_processor_t *p, granite_device_t *device, void *rdram_ptr, size_t rdram_offset,
		size_t rdram_size, size_t hidden_rdram_size, uint32_t flags)
{
	const char *env;

	memset(p, 0, sizeof(*p));
	p->device = device;
	p->rdram_offset = rdram_offset;
	p->rdram_size = rdram_size;
	p->flags = flags;
	p->is_host_coherent = true;
	p->scanout_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	rdp_vi_scanout_options_init(&p->scanout_options);

	/* Not started only if an allocation failed; init_renderer says so */
	rdp_timeline_init(&p->timeline, device);
	rdp_renderer_init(&p->renderer, &p->timeline);
	p->upscaler = rdp_upscaler_new();
	rdp_commands_init(&p->commands, &p->renderer, p->upscaler);

	if ((env = getenv("PARALLEL_RDP_DUMP_PATH")) != NULL)
	{
		p->dump_writer = rdp_dump_writer_new(env, (uint32_t)rdram_size, (uint32_t)hidden_rdram_size);
		if (!p->dump_writer)
			LOGE("Failed to init RDP dump: %s.\n", env);
		else
		{
			LOGI("Dumping RDP commands to: %s.\n", env);
			p->flags |= RDP_PROCESSOR_HOST_VISIBLE_HIDDEN_RDRAM;
		}
	}

	create_rdram(p, rdram_ptr, rdram_offset);
	if (!p->rdram)
		LOGE("Failed to allocate RDRAM.\n");

	/* The timeline worker's nodes, so that none is made mid-game: the
	 * waits and timeline values of the submissions in flight, and where
	 * the GPU's writes are read back, a few readbacks of the most copies
	 * one can have (a run of pages each, so half the pages, rounded up) */
	if (!rdp_timeline_reserve(&p->timeline, 64, p->is_host_coherent ? 0 : 4,
			(unsigned)((rdram_size / RDP_RDRAM_PAGE_SIZE + 1) / 2 + 1)))
		LOGE("Failed to reserve the timeline worker's nodes.\n");

	/* Without 8-bit SSBO access the shaders keep hidden RDRAM as one
	 * 16-bit element per entry, so neighbouring pixels never share a word */
	p->no_storage_8bit = !device->ext.storage_8bit_features.storageBuffer8BitAccess;
	if (getenv("PARALLEL_RDP_NO_STORAGE_8BIT"))
	{
		p->no_storage_8bit = env_flag("PARALLEL_RDP_NO_STORAGE_8BIT", false);
		LOGI("Overriding no 8-bit storage = %d.\n", (int)p->no_storage_8bit);
	}
	/* Without 16-bit SSBO access either, the NO_STORAGE_16BIT variants: the
	 * same buffers, hidden RDRAM's layout too, through 32-bit words */
	p->no_storage_16bit = !device->ext.storage_16bit_features.storageBuffer16BitAccess;
	if (getenv("PARALLEL_RDP_NO_STORAGE_16BIT"))
	{
		p->no_storage_16bit = env_flag("PARALLEL_RDP_NO_STORAGE_16BIT", false);
		LOGI("Overriding no 16-bit storage = %d.\n", (int)p->no_storage_16bit);
	}
	if (p->no_storage_16bit)
		p->no_storage_8bit = true;

	/* Should be cached host, but some incoherent Arm systems misbehave with
	 * it, so it is coherent memory for the time being */
	p->hidden_rdram = create_buffer(device, (VkDeviceSize)hidden_rdram_size * (p->no_storage_8bit ? 2 : 1),
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			(p->flags & RDP_PROCESSOR_HOST_VISIBLE_HIDDEN_RDRAM)
					? GRANITE_BUFFER_DOMAIN_CACHED_COHERENT_HOST_PREFER_COHERENT : GRANITE_BUFFER_DOMAIN_DEVICE, 0);
	p->tmem = create_buffer(device, 0x1000, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			(p->flags & RDP_PROCESSOR_HOST_VISIBLE_TMEM)
					? GRANITE_BUFFER_DOMAIN_CACHED_COHERENT_HOST_PREFER_COHERENT : GRANITE_BUFFER_DOMAIN_DEVICE, 0);

	clear_buffer(p, p->hidden_rdram, p->no_storage_8bit ? 0x00030003u : 0x03030303u);
	clear_buffer(p, p->tmem, 0);
	init_renderer(p);

	p->single_threaded = env_flag("PARALLEL_RDP_SINGLE_THREADED_COMMAND", false);
	if (p->single_threaded)
		LOGI("Will use single threaded command processing.\n");
	else if (!(p->ring = rdp_command_ring_new(run_ring_command, p, 4 * 1024)))
		p->single_threaded = true;
}

static void clear_upscaler_frame(rdp_processor_t *p)
{
	rdp_upscaler_targets_t *t = &p->upscaler_frame;
	if (t->motion_vectors)
		granite_image_unref(t->motion_vectors);
	if (t->depth)
		granite_image_unref(t->depth);
	if (t->translucent)
		granite_image_unref(t->translucent);
	memset(t, 0, sizeof(*t));
}

void rdp_processor_free(rdp_processor_t *p)
{
	rdp_processor_idle(p);
	/* The command thread goes before anything else: an idle tick can
	 * still flush and queue work to the timeline worker */
	rdp_command_ring_free(p->ring);
	p->ring = NULL;
	rdp_timeline_free(&p->timeline);
	rdp_upscaler_free(p->upscaler);
	p->upscaler = NULL;
	rdp_dump_writer_free(p->dump_writer);
	p->dump_writer = NULL;
	clear_upscaler_frame(p);
	if (p->scanout_result)
		granite_image_unref(p->scanout_result);
	p->scanout_result = NULL;
	rdp_vi_free(&p->vi);
	rdp_renderer_free(&p->renderer);
	if (p->tmem)
		granite_buffer_unref(p->tmem);
	if (p->hidden_rdram)
		granite_buffer_unref(p->hidden_rdram);
	if (p->rdram)
		granite_buffer_unref(p->rdram);
	p->tmem = p->hidden_rdram = p->rdram = NULL;
}

bool rdp_processor_supported(const rdp_processor_t *p)
{
	return p->is_supported;
}

unsigned rdp_processor_scaling_factor(const rdp_processor_t *p)
{
	return rdp_renderer_scaling_factor(&p->renderer);
}

bool rdp_processor_super_sampled_readback(const rdp_processor_t *p)
{
	return p->super_sampled_readback;
}

/* ---- queuing ---- */
static void enqueue_inner(rdp_processor_t *p, unsigned num_words, const uint32_t *words)
{
	if (p->single_threaded)
		rdp_processor_run(p, num_words, words);
	else
		rdp_command_ring_enqueue(p->ring, num_words, words);
}

static void drain(rdp_processor_t *p)
{
	if (p->ring)
		rdp_command_ring_drain(p->ring);
}

/* Dumps keep one byte per hidden RDRAM entry in either layout */
static void flush_hidden_rdram_to_dump(rdp_processor_t *p)
{
	const void *hidden = rdp_processor_begin_read_hidden_rdram(p);
	const size_t size = (size_t)p->hidden_rdram->info.size;
	if (p->no_storage_8bit)
		rdp_dump_writer_flush_hidden_dram_wide(p->dump_writer, (const uint16_t *)hidden, (uint32_t)(size / 2));
	else
		rdp_dump_writer_flush_hidden_dram(p->dump_writer, hidden, (uint32_t)size);
}

static void dump_rdram(rdp_processor_t *p)
{
	rdp_processor_wait_for_timeline(p, rdp_processor_signal_timeline(p));
	rdp_dump_writer_flush_dram(p->dump_writer, rdp_processor_begin_read_rdram(p), (uint32_t)p->rdram_size);
	flush_hidden_rdram_to_dump(p);
}

/* Out of the way of rdp_processor_enqueue, which runs for every command */
static void enqueue_dumped(rdp_processor_t *p, unsigned num_words, const uint32_t *words)
{
	const uint32_t op = (words[0] >> 24) & 63;

	if (!p->dump_in_command_list)
	{
		dump_rdram(p);
		p->dump_in_command_list = true;
	}

	enqueue_inner(p, num_words, words);

	if (op == RDP_PROCESSOR_SYNC_FULL)
	{
		rdp_dump_writer_signal_complete(p->dump_writer);
		p->dump_in_command_list = false;
	}
	else
		rdp_dump_writer_emit_command(p->dump_writer, op, words, num_words);
}

void rdp_processor_enqueue(rdp_processor_t *p, unsigned num_words, const uint32_t *words)
{
	/* Opcodes 1 to 7 are invalid RDP commands, no-ops on hardware
	 * (angrylion ignores them too), but here they are the processor's own:
	 * game data never signals a timeline, sets quirks or scans out */
	const uint32_t op = (words[0] >> 24) & 63;
	if (op >= 1 && op <= 7)
		return;
	if (p->dump_writer)
		enqueue_dumped(p, num_words, words);
	else
		enqueue_inner(p, num_words, words);
}

static void upscaler_scanout(rdp_processor_t *p)
{
	rdp_upscaler_targets_t targets;
	enum rdp_upscaler_frame frame;

	memset(&targets, 0, sizeof(targets));
	p->scanout_result = rdp_upscaler_view_scanout(p->upscaler, &p->vi, p->device,
			rdp_renderer_scaling_factor(&p->renderer), p->scanout_options.upscaler.jitter_phase,
			p->scanout_options.upscaler.reset, &targets, &frame);
	if (frame == RDP_UPSCALER_FRAME_NONE)
		clear_upscaler_frame(p);
	else if (frame == RDP_UPSCALER_FRAME_REPEAT)
		p->upscaler_frame.reset = false;
	else
	{
		clear_upscaler_frame(p);
		p->upscaler_frame = targets;
	}
}

void rdp_processor_run(rdp_processor_t *p, unsigned num_words, const uint32_t *words)
{
	(void)num_words;
	switch ((words[0] >> 24) & 63)
	{
	case RDP_PROCESSOR_SIGNAL_TIMELINE:
	{
		rdp_timeline_op_t op;
		rdp_renderer_flush_and_signal(&p->renderer);
		memset(&op, 0, sizeof(op));
		op.timeline_value = words[1] | ((uint64_t)words[2] << 32);
		rdp_timeline_enqueue(&p->timeline, &op);
		break;
	}

	case RDP_PROCESSOR_FLUSH:
		rdp_renderer_flush_and_signal(&p->renderer);
		break;

	case RDP_PROCESSOR_IDLE:
		rdp_renderer_notify_idle(&p->renderer);
		break;

	case RDP_PROCESSOR_SET_QUIRKS:
		rdp_commands_set_quirks(&p->commands, words[1]);
		break;

	case RDP_PROCESSOR_SCANOUT_PREPARE:
		/* The whole scanout runs where every other command runs: it never
		 * overlaps the idle flush, and the device is only driven from this
		 * thread. The options and VI registers were written by the caller
		 * before it queued this command. */
		rdp_renderer_flush_and_signal(&p->renderer);
		if (words[3])
			rdp_renderer_resolve_coherency_external(&p->renderer, words[1], words[2]);
		if (p->scanout_options.upscaler.enable && p->upscaler)
			upscaler_scanout(p);
		else
		{
			if (p->upscaler)
				rdp_upscaler_configure(p->upscaler, false, 1, false);
			clear_upscaler_frame(p);
			p->scanout_result = rdp_vi_scanout(&p->vi, p->scanout_layout, &p->scanout_options,
					rdp_renderer_scaling_factor(&p->renderer));
		}
		break;

	case RDP_PROCESSOR_NEXT_FRAME_CONTEXT:
		granite_device_next_frame_context(p->device);
		rdp_vi_begin_frame_context(&p->vi);
		break;

	case RDP_PROCESSOR_CALL:
	{
		/* fn and ctx, each split into two 32-bit words */
		void (*fn)(void *);
		const uintptr_t f = (uintptr_t)(((uint64_t)words[2] << 32) | words[1]);
		const uintptr_t ctx = (uintptr_t)(((uint64_t)words[4] << 32) | words[3]);
		memcpy(&fn, &f, sizeof(fn));
		fn((void *)ctx);
		break;
	}

	default:
		rdp_commands_run(&p->commands, words);
		break;
	}
}

void rdp_processor_set_quirks(rdp_processor_t *p, uint32_t quirks)
{
	uint32_t words[2];
	words[0] = (uint32_t)RDP_PROCESSOR_SET_QUIRKS << 24;
	words[1] = quirks;
	enqueue_inner(p, 2, words);
}

void rdp_processor_flush(rdp_processor_t *p)
{
	const uint32_t word = (uint32_t)RDP_PROCESSOR_FLUSH << 24;
	enqueue_inner(p, 1, &word);
}

uint64_t rdp_processor_signal_timeline(rdp_processor_t *p)
{
	uint32_t words[3];
	p->timeline_value++;
	words[0] = (uint32_t)RDP_PROCESSOR_SIGNAL_TIMELINE << 24;
	words[1] = (uint32_t)p->timeline_value;
	words[2] = (uint32_t)(p->timeline_value >> 32);
	enqueue_inner(p, 3, words);
	return p->timeline_value;
}

void rdp_processor_wait_for_timeline(rdp_processor_t *p, uint64_t value)
{
	rdp_timeline_wait(&p->timeline, value);
}

void rdp_processor_idle(rdp_processor_t *p)
{
	rdp_processor_flush(p);
	rdp_processor_wait_for_timeline(p, rdp_processor_signal_timeline(p));
}

void rdp_processor_begin_frame_context(rdp_processor_t *p)
{
	/* The frame switch runs on the command thread, which drives the
	 * device; waiting for it keeps the caller paced as before */
	const uint32_t word = (uint32_t)RDP_PROCESSOR_NEXT_FRAME_CONTEXT << 24;
	rdp_processor_flush(p);
	enqueue_inner(p, 1, &word);
	drain(p);
}

void rdp_processor_run_on_command_thread(rdp_processor_t *p, void (*fn)(void *), void *ctx)
{
	uintptr_t f;
	const uint64_t c = (uint64_t)(uintptr_t)ctx;
	uint32_t words[5];
	memcpy(&f, &fn, sizeof(f));
	words[0] = (uint32_t)RDP_PROCESSOR_CALL << 24;
	words[1] = (uint32_t)(uint64_t)f;
	words[2] = (uint32_t)((uint64_t)f >> 32);
	words[3] = (uint32_t)c;
	words[4] = (uint32_t)(c >> 32);
	enqueue_inner(p, 5, words);
	drain(p);
}

typedef struct pipelines_ahead
{
	rdp_processor_t *p;
	const pipeline_record_t *records;
	size_t count;
	bool with_upscaler;
} pipelines_ahead_t;

static void build_pipelines_ahead_on_command_thread(void *ctx)
{
	pipelines_ahead_t *ahead = (pipelines_ahead_t *)ctx;
	rdp_renderer_build_pipelines_ahead(&ahead->p->renderer, ahead->records, ahead->count, &ahead->p->vi,
			ahead->with_upscaler);
}

void rdp_processor_build_pipelines_ahead(rdp_processor_t *p, const pipeline_record_t *records, size_t count,
		bool with_upscaler)
{
	pipelines_ahead_t ahead;
	ahead.p = p;
	ahead.records = records;
	ahead.count = count;
	ahead.with_upscaler = with_upscaler;
	/* (not waited for: building them on a cold driver cache takes most of
	 * a minute on a software driver, and long enough on a real one to
	 * look like a hang, with load blocked on it) */
	rdp_processor_run_on_command_thread(p, build_pipelines_ahead_on_command_thread, &ahead);
}

void rdp_processor_wait_pipelines_ahead_for_test(rdp_processor_t *p)
{
	rdp_renderer_wait_pipelines_ahead_for_test(&p->renderer);
}

typedef struct validation_set
{
	rdp_renderer_t *renderer;
	rdp_report_crash_fn report;
	void *user;
} validation_set_t;

static void set_validation_on_command_thread(void *ctx)
{
	validation_set_t *set = (validation_set_t *)ctx;
	rdp_renderer_set_validation(set->renderer, set->report, set->user);
}

void rdp_processor_set_validation(rdp_processor_t *p, rdp_report_crash_fn report, void *user)
{
	validation_set_t set;
	set.renderer = &p->renderer;
	set.report = report;
	set.user = user;
	rdp_processor_run_on_command_thread(p, set_validation_on_command_thread, &set);
}

/* ---- the VI and memory ---- */
void rdp_processor_set_vi_register(rdp_processor_t *p, enum rdp_vi_register reg, uint32_t value)
{
	rdp_vi_set_register(&p->vi, reg, value);
	if (p->dump_writer)
		rdp_dump_writer_set_vi_register(p->dump_writer, (uint32_t)reg, value);
}

granite_image_t *rdp_processor_scanout(rdp_processor_t *p, const rdp_vi_scanout_options_t *options,
		VkImageLayout layout)
{
	uint32_t words[4];
	unsigned offset = 0, length = 0;
	granite_image_t *image;

	drain(p);
	if (p->dump_writer)
	{
		dump_rdram(p);
		rdp_dump_writer_end_frame(p->dump_writer);
	}

	/* Flush, and resolve the scanout range on non-coherent hosts, on the
	 * command thread. The VI registers are read here, on the thread that
	 * writes them; the range travels in the command. */
	if (!p->is_host_coherent)
		rdp_vi_scanout_memory_range(p->vi.registers, &offset, &length);
	words[0] = (uint32_t)RDP_PROCESSOR_SCANOUT_PREPARE << 24;
	words[1] = offset;
	words[2] = length;
	words[3] = (uint32_t)!p->is_host_coherent;
	p->scanout_options = *options;
	p->scanout_layout = layout;
	enqueue_inner(p, 4, words);
	drain(p);

	image = p->scanout_result;
	p->scanout_result = NULL;
	return image;
}

void rdp_processor_upscaler_frame(rdp_processor_t *p, rdp_upscaler_targets_t *frame)
{
	/* (kept: a repeated frame hands it over again) */
	*frame = p->upscaler_frame;
	if (frame->motion_vectors)
		granite_image_ref(frame->motion_vectors);
	if (frame->depth)
		granite_image_ref(frame->depth);
	if (frame->translucent)
		granite_image_ref(frame->translucent);
}

void *rdp_processor_begin_read_rdram(rdp_processor_t *p)
{
	if (!p->rdram)
		return NULL;
	return granite_device_map_host_buffer(p->device, p->rdram, GRANITE_MEMORY_ACCESS_READ_BIT, 0, p->rdram->info.size);
}

void *rdp_processor_begin_read_hidden_rdram(rdp_processor_t *p)
{
	return granite_device_map_host_buffer(p->device, p->hidden_rdram, GRANITE_MEMORY_ACCESS_READ_BIT, 0,
			p->hidden_rdram->info.size);
}
