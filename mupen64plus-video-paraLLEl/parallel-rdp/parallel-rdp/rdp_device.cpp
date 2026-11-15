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

#include "rdp_device.hpp"
#include <stdint.h>
#include "rdp_common.hpp"
#include "masked_copy.h"



using namespace Vulkan;


namespace RDP
{
// The shader bank asks each shader's owner for its defines.
struct ShaderDefineResolver
{
	const VideoInterface *vi;
	const Renderer *renderer;
};

// Where the VI reads an upscaled frame from
static void vi_update_upscaled(void *user, granite_command_buffer_t *cb, unsigned addr, unsigned pixels,
                               unsigned pixel_size_log2)
{
	static_cast<Renderer *>(user)->submit_update_upscaled_domain_external(cb, addr, pixels, pixel_size_log2);
}

static const granite_buffer_t *vi_upscaled_rdram(void *user)
{
	return static_cast<Renderer *>(user)->get_upscaled_rdram_buffer();
}

static const granite_buffer_t *vi_upscaled_hidden_rdram(void *user)
{
	return static_cast<Renderer *>(user)->get_upscaled_hidden_rdram_buffer();
}

static int resolve_shader_define(void *user, const char *name, const char *define)
{
	const ShaderDefineResolver *resolver = static_cast<const ShaderDefineResolver *>(user);
	if (strncmp(name, "vi_", 3) == 0)
		return resolver->vi->resolve_shader_define(name, define);
	return resolver->renderer->resolve_shader_define(name, define);
}

CommandProcessor::CommandProcessor(Vulkan::Device &device_, void *rdram_ptr,
                                   size_t rdram_offset_, size_t rdram_size_, size_t hidden_rdram_size,
                                   CommandProcessorFlags flags_)
	: device(device_), rdram_offset(rdram_offset_), rdram_size(rdram_size_), flags(flags_), renderer(&timeline)
{
	// Not started only if an allocation failed; init_renderer reports that.
	rdp_timeline_init(&timeline, &device);
	upscaler = rdp_upscaler_new();
	rdp_commands_init(&commands, renderer.get(), upscaler);

	BufferCreateInfo info = {};
	info.size = rdram_size;
	info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	info.domain = BufferDomain::CachedCoherentHostPreferCached;
	info.misc = BUFFER_MISC_ZERO_INITIALIZE_BIT;

	if (const char *env = getenv("PARALLEL_RDP_DUMP_PATH"))
	{
		dump_writer = rdp_dump_writer_new(env, rdram_size, hidden_rdram_size);
		if (!dump_writer)
			LOGE("Failed to init RDP dump: %s.\n", env);
		else
		{
			LOGI("Dumping RDP commands to: %s.\n", env);
			flags |= COMMAND_PROCESSOR_FLAG_HOST_VISIBLE_HIDDEN_RDRAM_BIT;
		}
	}

	if (rdram_ptr)
	{
		bool allow_memory_host = true;
		if (const char *env = getenv("PARALLEL_RDP_ALLOW_EXTERNAL_HOST"))
			allow_memory_host = strtol(env, nullptr, 0) > 0;

		if (allow_memory_host && device.get_device_features().supports_external_memory_host)
		{
			size_t import_size = rdram_size + rdram_offset;
			size_t align = device.get_device_features().host_memory_properties.minImportedHostPointerAlignment;
			import_size = (import_size + align - 1) & ~(align - 1);
			info.size = import_size;
			rdram = device.create_imported_host_buffer(info, rdram_ptr);
			if (!rdram)
				LOGE("Failed to allocate RDRAM with VK_EXT_external_memory_host.\n");
		}

		if (!rdram)
		{
			LOGW("VK_EXT_external_memory_host not supported or failed, falling back to a slower path.\n");
			is_host_coherent = false;
			rdram_offset = 0;
			host_rdram = static_cast<uint8_t *>(rdram_ptr) + rdram_offset_;

			BufferCreateInfo device_rdram = {};
			device_rdram.size = rdram_size * 2; // Need twice the memory amount so we can also store a writemask.
			device_rdram.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
			                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
			                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

			if (device.get_gpu_properties().deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
				device_rdram.domain = BufferDomain::CachedCoherentHostPreferCached;
			else
				device_rdram.domain = BufferDomain::Device;

			device_rdram.misc = BUFFER_MISC_ZERO_INITIALIZE_BIT;
			rdram = device.create_buffer(device_rdram);
		}
	}
	else
		rdram = device.create_buffer(info);

	if (!rdram)
		LOGE("Failed to allocate RDRAM.\n");

	// The timeline worker's nodes, so that none is made mid-game: the waits
	// and timeline values of the submissions in flight, and where the GPU's
	// writes are read back, a few readbacks of the most copies one can have
	// (a run of pages each, so half the pages, rounded up).
	if (!rdp_timeline_reserve(&timeline, 64, is_host_coherent ? 0 : 4,
	                          unsigned((rdram_size / RDP_RDRAM_PAGE_SIZE + 1) / 2 + 1)))
		LOGE("Failed to reserve the timeline worker's nodes.\n");

	// Without 8-bit SSBO access the shaders keep hidden RDRAM as one 16-bit
	// element per entry, so neighbouring pixels never share a word.
	no_storage_8bit = !device.get_device_features().storage_8bit_features.storageBuffer8BitAccess;
	if (const char *env = getenv("PARALLEL_RDP_NO_STORAGE_8BIT"))
	{
		no_storage_8bit = strtol(env, nullptr, 0) > 0;
		LOGI("Overriding no 8-bit storage = %d.\n", int(no_storage_8bit));
	}

	info.size = hidden_rdram_size * (no_storage_8bit ? 2 : 1);
	// Should be CachedHost, but seeing some insane bug on incoherent Arm systems for time being,
	// so just forcing coherent memory here for now. Not sure what is going on.
	info.domain = (flags & COMMAND_PROCESSOR_FLAG_HOST_VISIBLE_HIDDEN_RDRAM_BIT) != 0 ?
	              BufferDomain::CachedCoherentHostPreferCoherent : BufferDomain::Device;
	info.misc = 0;
	hidden_rdram = device.create_buffer(info);

	info.size = 0x1000;
	info.domain = (flags & COMMAND_PROCESSOR_FLAG_HOST_VISIBLE_TMEM_BIT) != 0 ?
	              BufferDomain::CachedCoherentHostPreferCoherent : BufferDomain::Device;
	tmem = device.create_buffer(info);

	clear_hidden_rdram();
	clear_tmem();
	init_renderer();

	if (const char *env = getenv("PARALLEL_RDP_SINGLE_THREADED_COMMAND"))
	{
		single_threaded_processing = strtol(env, nullptr, 0) > 0;
		if (single_threaded_processing)
			LOGI("Will use single threaded command processing.\n");
	}

	if (!single_threaded_processing)
	{
		ring = rdp_command_ring_new(run_ring_command, this, 4 * 1024);
		if (!ring)
			single_threaded_processing = true;
	}
}

CommandProcessor::~CommandProcessor()
{
	idle();
	// Join the command thread before any member goes: an idle tick can
	// still flush and queue work to the timeline worker, which is
	// destroyed before the ring would otherwise be.
	rdp_command_ring_free(ring);
	ring = nullptr;
	rdp_timeline_free(&timeline);
	rdp_upscaler_free(upscaler);
	rdp_dump_writer_free(dump_writer);
}

void CommandProcessor::begin_frame_context()
{
	// The frame switch runs on the command thread, which drives the
	// Vulkan device; waiting for it keeps the caller paced as before.
	flush();
	const uint32_t words[1] = { uint32_t(Op::MetaNextFrameContext) << 24 };
	enqueue_command_inner(1, words);
	drain_command_ring();
}

unsigned CommandProcessor::get_scaling_factor() const
{
	return renderer.get_scaling_factor();
}

bool CommandProcessor::get_super_sampled_readback() const
{
	return super_sampled_readback;
}

void CommandProcessor::init_renderer()
{
	if (!rdram || !timeline.worker)
	{
		is_supported = false;
		return;
	}

	renderer.set_device(&device);
	if (!renderer.set_rdram(rdram.get(), host_rdram, rdram_offset, rdram_size, is_host_coherent))
	{
		is_supported = false;
		return;
	}
	renderer.set_hidden_rdram(hidden_rdram.get());
	renderer.set_tmem(tmem.get());

	unsigned factor = 1;
	if (flags & COMMAND_PROCESSOR_FLAG_UPSCALING_8X_BIT)
		factor = 8;
	else if (flags & COMMAND_PROCESSOR_FLAG_UPSCALING_4X_BIT)
		factor = 4;
	else if (flags & COMMAND_PROCESSOR_FLAG_UPSCALING_2X_BIT)
		factor = 2;

	// Where the device's storage buffer bindings are too small for the
	// factor, the factor comes down; but where only the super-sampled
	// read-back's write mask (which sits after the upscaled RDRAM) is what
	// does not fit, the read-back goes rather than half the resolution.
	{
		const VkDeviceSize hidden_size = hidden_rdram->get_create_info().size;
		unsigned plain = Renderer::fitting_upscaling_factor(device, factor, rdram_size, hidden_size, false);
		super_sampled_readback = (flags & COMMAND_PROCESSOR_FLAG_SUPER_SAMPLED_READ_BACK_BIT) != 0;
		if (super_sampled_readback)
		{
			unsigned with_ssaa = Renderer::fitting_upscaling_factor(device, factor, rdram_size, hidden_size, true);
			if (with_ssaa < plain || with_ssaa == 1)
			{
				LOGW("Super-sampled read-back does not fit this device's storage buffer bindings at %ux: off.\n",
				     plain);
				super_sampled_readback = false;
			}
		}
		if (plain < factor)
			LOGW("Upscaling %ux does not fit this device's storage buffer bindings: %ux.\n", factor, plain);
		factor = plain;
	}
	if (factor != 1)
		LOGI("Enabling upscaling: %ux.\n", factor);

	RendererOptions opts;
	opts.upscaling_factor = factor;
	opts.super_sampled_readback = super_sampled_readback;
	opts.super_sampled_readback_dither = super_sampled_readback &&
	                                     (flags & COMMAND_PROCESSOR_FLAG_SUPER_SAMPLED_DITHER_BIT) != 0;
	opts.no_storage_8bit = no_storage_8bit;

	is_supported = renderer.init_renderer(opts);

	vi.set_device(&device);
	vi.set_rdram(rdram.get(), rdram_offset, rdram_size);
	vi.set_hidden_rdram(hidden_rdram.get());
	{
		rdp_vi_upscaled_source_t source = { &renderer, vi_update_upscaled, vi_upscaled_rdram, vi_upscaled_hidden_rdram };
		vi.set_upscaled_source(source);
	}

	ShaderDefineResolver resolver = { &vi, &renderer };
	rdp_shader_bank_init(&shader_bank, &device, resolve_shader_define, &resolver);
	renderer.set_shader_bank(&shader_bank);
	vi.set_shader_bank(&shader_bank);
}

// Dumps keep one byte per hidden RDRAM entry in either layout.
void CommandProcessor::flush_hidden_dram_to_dump()
{
	const void *hidden = begin_read_hidden_rdram();
	size_t size = hidden_rdram->get_create_info().size;

	if (no_storage_8bit)
		rdp_dump_writer_flush_hidden_dram_wide(dump_writer, static_cast<const uint16_t *>(hidden), uint32_t(size / 2));
	else
		rdp_dump_writer_flush_hidden_dram(dump_writer, hidden, uint32_t(size));
}

bool CommandProcessor::device_is_supported() const
{
	return is_supported;
}

void CommandProcessor::clear_hidden_rdram()
{
	clear_buffer(*hidden_rdram, no_storage_8bit ? 0x00030003u : 0x03030303u);
}

void CommandProcessor::clear_tmem()
{
	clear_buffer(*tmem, 0);
}

void CommandProcessor::clear_buffer(Vulkan::Buffer &buffer, uint32_t value)
{
	if (!buffer.get_allocation().host_base)
	{
		auto cmd = device.request_command_buffer();
		cmd->fill_buffer(buffer, value);
		Fence fence;
		device.submit(cmd, &fence);
		fence->wait();
	}
	else
	{
		auto *mapped = device.map_host_buffer(buffer, MEMORY_ACCESS_WRITE_BIT);
		memset(mapped, value & 0xff, buffer.get_create_info().size);
		device.unmap_host_buffer(buffer, MEMORY_ACCESS_WRITE_BIT);
	}
}

void CommandProcessor::enqueue_command_inner(unsigned num_words, const uint32_t *words)
{
	if (single_threaded_processing)
		enqueue_command_direct(num_words, words);
	else
		rdp_command_ring_enqueue(ring, num_words, words);
}

void CommandProcessor::enqueue_command(unsigned num_words, const uint32_t *words)
{
	// Opcodes 1-7 are invalid RDP commands, no-ops on hardware (angrylion
	// ignores them too), but here they are the processor's own meta ops.
	// Drop them so game data can never signal a timeline, set quirks or
	// trigger a scanout resolve.
	{
		uint32_t cmd_id = (words[0] >> 24) & 63;
		if (cmd_id >= 1 && cmd_id <= 7)
			return;
	}

	if (dump_writer)
	{
		enqueue_command_dumped(num_words, words);
		return;
	}

	enqueue_command_inner(num_words, words);
}

// Out of the way of enqueue_command, which runs for every command.
void CommandProcessor::enqueue_command_dumped(unsigned num_words, const uint32_t *words)
{
	uint32_t cmd_id = (words[0] >> 24) & 63;

	if (!dump_in_command_list)
	{
		wait_for_timeline(signal_timeline());
		rdp_dump_writer_flush_dram(dump_writer, begin_read_rdram(), rdram_size);
		flush_hidden_dram_to_dump();
		dump_in_command_list = true;
	}

	enqueue_command_inner(num_words, words);

	if (Op(cmd_id) == Op::SyncFull)
	{
		rdp_dump_writer_signal_complete(dump_writer);
		dump_in_command_list = false;
	}
	else
		rdp_dump_writer_emit_command(dump_writer, cmd_id, words, num_words);
}

// command_ring.c names the idle tick by number
static_assert(unsigned(Op::MetaIdle) == 3, "RDP_COMMAND_RING_IDLE_OP");

void CommandProcessor::run_ring_command(void *self, unsigned num_words, const uint32_t *words)
{
	static_cast<CommandProcessor *>(self)->enqueue_command_direct(num_words, words);
}

UTIL_NOINLINE void CommandProcessor::enqueue_command_direct(unsigned, const uint32_t *words)
{
	unsigned op = (words[0] >> 24) & 63;
	switch (Op(op))
	{
	case Op::MetaSignalTimeline:
	{
		renderer.flush_and_signal();
		uint64_t val = words[1] | (uint64_t(words[2]) << 32);
		CoherencyOperation signal_op = {};
		signal_op.timeline_value = val;
		enqueue_coherency_operation(signal_op);
		break;
	}

	case Op::MetaFlush:
	{
		renderer.flush_and_signal();
		break;
	}

	case Op::MetaIdle:
	{
		renderer.notify_idle_command_thread();
		break;
	}

	case Op::MetaSetQuirks:
	{
		rdp_commands_set_quirks(&commands, words[1]);
		break;
	}

	case Op::MetaScanoutPrepare:
	{
		// The whole scanout runs where every other command runs: it can
		// never overlap the idle flush, and the Vulkan device is only
		// driven from this thread. The options and VI registers were
		// written by the caller before it queued this command.
		renderer.flush_and_signal();
		if (words[3])
			renderer.resolve_coherency_external(words[1], words[2]);
		if (scanout_opts.upscaler.enable && upscaler)
			upscaler_scanout();
		else
		{
			if (upscaler)
				rdp_upscaler_configure(upscaler, false, 1, false);
			upscaler_frame.clear();
			scanout_result = vi.scanout(scanout_layout, scanout_opts, renderer.get_scaling_factor());
		}
		break;
	}

	case Op::MetaNextFrameContext:
		device.next_frame_context();
		vi.begin_frame_context();
		break;

	case Op::MetaCall:
	{
		// fn and ctx, each split into two 32-bit words
		uintptr_t fn = uintptr_t((uint64_t(words[2]) << 32) | words[1]);
		uintptr_t ctx = uintptr_t((uint64_t(words[4]) << 32) | words[3]);
		reinterpret_cast<void (*)(void *)>(fn)(reinterpret_cast<void *>(ctx));
		break;
	}

	default:
		rdp_commands_run(&commands, words);
		break;
	}
}

void CommandProcessor::set_quirks(const Quirks &quirks_)
{
	const uint32_t words[2] = {
		uint32_t(Op::MetaSetQuirks) << 24u,
		quirks_.flags,
	};
	enqueue_command_inner(2, words);
}

void CommandProcessor::set_vi_register(VIRegister reg, uint32_t value)
{
	vi.set_vi_register(reg, value);
	if (dump_writer)
		rdp_dump_writer_set_vi_register(dump_writer, uint32_t(reg), value);
}

void *CommandProcessor::begin_read_rdram()
{
	if (rdram)
		return device.map_host_buffer(*rdram, MEMORY_ACCESS_READ_BIT);
	else
		return nullptr;
}

void *CommandProcessor::begin_read_hidden_rdram()
{
	return device.map_host_buffer(*hidden_rdram, MEMORY_ACCESS_READ_BIT);
}

void CommandProcessor::idle()
{
	flush();
	wait_for_timeline(signal_timeline());
}

void CommandProcessor::flush()
{
	const uint32_t words[1] = {
		uint32_t(Op::MetaFlush) << 24,
	};
	enqueue_command_inner(1, words);
}

uint64_t CommandProcessor::signal_timeline()
{
	timeline_value++;

	const uint32_t words[3] = {
		uint32_t(Op::MetaSignalTimeline) << 24,
		uint32_t(timeline_value),
		uint32_t(timeline_value >> 32),
	};
	enqueue_command_inner(3, words);

	return timeline_value;
}

void CommandProcessor::wait_for_timeline(uint64_t index)
{
	rdp_timeline_wait(&timeline, index);
}

Vulkan::ImageHandle CommandProcessor::scanout(const ScanoutOptions &opts, VkImageLayout target_layout)
{
	drain_command_ring();

	if (dump_writer)
	{
		wait_for_timeline(signal_timeline());
		rdp_dump_writer_flush_dram(dump_writer, begin_read_rdram(), rdram_size);
		flush_hidden_dram_to_dump();
		rdp_dump_writer_end_frame(dump_writer);
	}

	// Flush, and resolve the scanout range on non-coherent hosts, on the
	// command thread. The VI registers are read here, on the thread that
	// writes them; the range travels in the command.
	{
		unsigned offset = 0, length = 0;
		if (!is_host_coherent)
			vi.scanout_memory_range(offset, length);
		const uint32_t words[4] = {
			uint32_t(Op::MetaScanoutPrepare) << 24,
			offset,
			length,
			uint32_t(!is_host_coherent),
		};
		scanout_opts = opts;
		scanout_layout = target_layout;
		enqueue_command_inner(4, words);
		drain_command_ring();
	}

	return rdp_move(scanout_result);
}

void CommandProcessor::get_upscaler_frame(UpscalerFrame &frame)
{
	/* (kept: a repeated frame hands it over again) */
	frame = upscaler_frame;
}

void CommandProcessor::upscaler_scanout()
{
	rdp_upscaler_targets_t targets = {};
	enum rdp_upscaler_frame frame;

	scanout_result = Vulkan::ImageHandle(static_cast<Vulkan::Image *>(rdp_upscaler_view_scanout(
			upscaler, vi.get_vi(), &device, renderer.get_scaling_factor(), scanout_opts.upscaler.jitter_phase,
			scanout_opts.upscaler.reset, &targets, &frame)));
	if (frame == RDP_UPSCALER_FRAME_NONE)
		upscaler_frame.clear();
	else if (frame == RDP_UPSCALER_FRAME_REPEAT)
		upscaler_frame.reset = false;
	else
		upscaler_frame.take(targets);
}

void CommandProcessor::run_on_command_thread(void (*fn)(void *), void *ctx)
{
	uint64_t f = uint64_t(reinterpret_cast<uintptr_t>(fn));
	uint64_t c = uint64_t(reinterpret_cast<uintptr_t>(ctx));
	const uint32_t words[5] = {
		uint32_t(Op::MetaCall) << 24,
		uint32_t(f), uint32_t(f >> 32),
		uint32_t(c), uint32_t(c >> 32),
	};
	enqueue_command_inner(5, words);
	drain_command_ring();
}

namespace
{
struct PipelinesAhead
{
	VideoInterface *vi;
	bool with_upscaler;
	Renderer *renderer;
	const pipeline_record_t *records;
	size_t count;
};

void build_pipelines_ahead_on_command_thread(void *ctx)
{
	auto *ahead = static_cast<PipelinesAhead *>(ctx);
	ahead->renderer->build_pipelines_ahead(ahead->records, ahead->count, ahead->vi ? ahead->vi->get_vi() : nullptr,
	                                      ahead->with_upscaler);
}
}

void CommandProcessor::build_pipelines_ahead(const pipeline_record_t *records, size_t count, bool with_upscaler)
{
	PipelinesAhead ahead;
	ahead.renderer = &renderer;
	ahead.vi       = &vi;
	ahead.with_upscaler = with_upscaler;
	ahead.records  = records;
	ahead.count    = count;
	/* not waited for: building them on a cold driver cache takes most of
	 * a minute on a software driver and long enough on a real one to look
	 * like a hang, with load (or an upscale change mid-game) blocked on it */
	run_on_command_thread(build_pipelines_ahead_on_command_thread, &ahead);
}

namespace
{
struct ValidationSet
{
	Renderer *renderer;
	ValidationInterface *iface;
};

void set_validation_on_command_thread(void *ctx)
{
	auto *set = static_cast<ValidationSet *>(ctx);
	set->renderer->set_validation_interface(set->iface);
}
}

void CommandProcessor::set_validation_interface(ValidationInterface *iface)
{
	// (where the renderer reads it)
	ValidationSet set = { &renderer, iface };
	run_on_command_thread(set_validation_on_command_thread, &set);
}

void CommandProcessor::wait_pipelines_ahead_for_test()
{
	renderer.wait_pipelines_ahead_for_test();
}

Vulkan::ImageHandle CommandProcessor::scanout(const ScanoutOptions &opts)
{
	return scanout(opts, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void CommandProcessor::drain_command_ring()
{
	if (ring)
		rdp_command_ring_drain(ring);
}

void CommandProcessor::enqueue_coherency_operation(const CoherencyOperation &op)
{
	rdp_timeline_enqueue(&timeline, &op);
}
}
