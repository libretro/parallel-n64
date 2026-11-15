#include "rdp.hpp"
#include "Gfx #1.3.h"
#include "parallel.h"
#include "z64.h"
#include <assert.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <retro_miscellaneous.h>
#include "pipeline_record_list.h"
#include "rdp_intake.h"
#include "rdp_pipeline_files.h"
#include "rdp_upscaler_policy.h"

using namespace Vulkan;

extern retro_log_printf_t log_cb;
extern retro_environment_t environ_cb;

namespace RDP
{
const struct retro_hw_render_interface_vulkan *vulkan;

static uint64_t pending_timeline_value, timeline_value;

static CommandProcessor *frontend;
static Device *device;
static Context *context;

/* Set true only when init() fully succeeds (including the device meeting
 * parallel-RDP's feature requirements). The frame-driving entry points
 * (begin_frame / process_commands / complete_frame) hard no-op when this is
 * false, so that a failed/unsupported init -- e.g. a MoltenVK build lacking the
 * 8/16-bit storage parallel-RDP needs -- degrades to "nothing rendered" instead
 * of dereferencing a half-initialized Vulkan device and crashing. */
static bool device_supported = false;

/* Per sync index; the frontend's index mask is 32 bits */
#define RDP_MAX_SYNC_FRAMES 32
static retro_vulkan_image retro_images[RDP_MAX_SYNC_FRAMES];
static Util::HeldHandle<Vulkan::Image> retro_image_handles[RDP_MAX_SYNC_FRAMES];
static unsigned num_retro_images;

/* RDRAM as the command processor imports it */
static uintptr_t rdram_base;
static uintptr_t rdram_offset;
/* The upscaling factor the command processor renders at, and the one it
 * was made for (the device may not fit that) */
static unsigned processor_factor = 1;
static unsigned processor_requested = 1;

/* The frontend's temporal upscaler (RETRO_ENVIRONMENT_GET_HW_RENDER_UPSCALER_INTERFACE) */
static const struct retro_hw_render_upscaler_interface *upscaler_iface;
static struct retro_hw_upscaler_request upscaler_request;
static struct retro_hw_upscaler_inputs upscaler_inputs;
/* Per sync index: motion vectors, depth, then the translucency mask */
static Util::HeldHandle<Vulkan::Image> upscaler_handles[3 * RDP_MAX_SYNC_FRAMES];
/* What the upscaler is given (rdp_upscaler_policy.c) */
static rdp_upscaler_policy_t upscaler_policy = { false, true, 320, 240, 0, 0, ~0u };
/* test hosts that count what the frame's threads build have load wait for
 * the worker (PARALLEL_RDP_TEST_WAIT_PIPELINES, read at init); nothing
 * else does */
static bool test_wait_pipelines;

/* The frames released past n, as a shrinking vector did. */
static void set_num_sync_frames(unsigned n)
{
	unsigned i;
	if (n > RDP_MAX_SYNC_FRAMES)
		n = RDP_MAX_SYNC_FRAMES;
	for (i = n; i < RDP_MAX_SYNC_FRAMES; i++)
	{
		memset(&retro_images[i], 0, sizeof(retro_images[i]));
		retro_image_handles[i].reset();
		upscaler_handles[3 * i].reset();
		upscaler_handles[3 * i + 1].reset();
		upscaler_handles[3 * i + 2].reset();
	}
	num_retro_images = n;
}

static void upscaler_poll();
unsigned width, height;
unsigned overscan;
unsigned upscaling = 1;
unsigned downscaling_steps = 0;
bool native_texture_lod = false;
bool native_tex_rect = true;
bool super_sampled_read_back = false;
bool super_sampled_dither = false;
bool synchronous = true, divot_filter = true, gamma_dither = true;
bool vi_aa = true, vi_scale = true, dither_filter = true;
bool interlacing = true;

/* The command list, where the DPC registers point (rdp_intake.c) */
static rdp_intake_t intake;

static void intake_enqueue(void *user, unsigned num_words, const uint32_t *words)
{
	(void)user;
	if (frontend)
		frontend->enqueue_command(num_words, words);
}

static void intake_sync_full(void *user)
{
	(void)user;
	// For synchronous RDP:
	if (synchronous && frontend)
		frontend->wait_for_timeline(frontend->signal_timeline());
	*gfx_info.MI_INTR_REG |= DP_INTERRUPT;
	gfx_info.CheckInterrupts();
}

/* HLE command-FIFO redirect.  The HLE emitter synthesizes an RDP command list
 * into a host buffer (out of guest RDRAM, so Expansion Pak games are not
 * corrupted) and reports it as occupying [base, base+len). When active,
 * process_commands() ingests the command words from this host buffer
 * instead of guest DRAM; the address range is otherwise identical, so
 * texture/image reads still hit real RDRAM.  Cleared after each submit, so the
 * LLE path (real games feeding the RDP) is completely unaffected. */
void set_hle_cmd_buffer(const uint8_t *buf, uint32_t base_byte_addr, uint32_t len_bytes)
{
	rdp_intake_set_hle_buffer(&intake, buf, base_byte_addr, len_bytes);
}

void process_commands()
{
	if (!device_supported)
		return;
	if (rdp_intake_process(&intake, *GET_GFX_INFO(DPC_CURRENT_REG), *GET_GFX_INFO(DPC_END_REG),
	                       *GET_GFX_INFO(DPC_STATUS_REG), DRAM, SP_DMEM))
		*GET_GFX_INFO(DPC_CURRENT_REG) = *GET_GFX_INFO(DPC_END_REG);
}

void begin_frame()
{
	if (!device_supported)
		return;

	unsigned mask = vulkan->get_sync_index_mask(vulkan->handle);
	unsigned num_frames = 0;
	for (unsigned i = 0; i < 32; i++)
		if (mask & (1u << i))
			num_frames = i + 1;

	set_num_sync_frames(num_frames);

	vulkan->wait_sync_index(vulkan->handle);

	upscaler_poll();

	//frontend->wait_for_timeline(pending_timeline_value);
	//pending_timeline_value = timeline_value;
}

/* The driver's pipeline cache and the list of the pipelines built
 * (rdp_pipeline_files.c) */
static rdp_pipeline_files_t pipeline_files;

/* The pipelines the list and the renderer's options name are built on the
 * worker, and nothing waits for them: a draw that gets to one first builds
 * its own, as on a first run it always did. */
static void build_pipelines_ahead()
{
	frontend->build_pipelines_ahead(pipeline_files.list, pipeline_files.list_count, upscaler_iface != nullptr);
	if (test_wait_pipelines)
		frontend->wait_pipelines_ahead_for_test();
}

static void pipeline_files_load(void)
{
	const char *dir = NULL;
	void *blob;
	size_t size;
	if (!environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &dir))
		dir = NULL;
	rdp_pipeline_files_load(&pipeline_files, dir, &blob, &size);
	/* (one the device does not take is replaced by an empty one) */
	device->init_pipeline_cache((const uint8_t *)blob, size);
	free(blob);
}

/* Once no thread builds pipelines any more and the device is still there. */
static void pipeline_files_save(void)
{
	const pipeline_record_t *built = NULL;
	size_t num_built = device->get_pipeline_records(&built);
	size_t size = device->get_pipeline_cache_size();
	uint8_t *blob = size ? (uint8_t *)malloc(size) : NULL;
	if (blob && !device->get_pipeline_cache_data(blob, size))
	{
		free(blob);
		blob = NULL;
	}
	rdp_pipeline_files_save(&pipeline_files, built, num_built, blob, blob ? size : 0);
	free(blob);
}

static bool create_processor(unsigned factor)
{
	CommandProcessorFlags flags = 0;
	RDP::Quirks quirks;

	switch (factor)
	{
		case 2:
			flags |= COMMAND_PROCESSOR_FLAG_UPSCALING_2X_BIT;
			break;
		case 4:
			flags |= COMMAND_PROCESSOR_FLAG_UPSCALING_4X_BIT;
			break;
		case 8:
			flags |= COMMAND_PROCESSOR_FLAG_UPSCALING_8X_BIT;
			break;
		default:
			factor = 1;
			break;
	}
	if (factor > 1)
		log_cb(RETRO_LOG_INFO, "Using %ux upscaling!\n", factor);

	/* Upscaled frames read back into RDRAM are normally point-sampled
	 * down to native resolution; these average the samples instead (and
	 * dither the result), which some framebuffer effects benefit from.
	 * They change guest-visible RDRAM, so both default to off. The
	 * temporal upscaler takes the frame as drawn, without them. */
	if (factor > 1 && super_sampled_read_back && !upscaler_policy.on)
	{
		flags |= COMMAND_PROCESSOR_FLAG_SUPER_SAMPLED_READ_BACK_BIT;
		if (super_sampled_dither)
			flags |= COMMAND_PROCESSOR_FLAG_SUPER_SAMPLED_DITHER_BIT;
	}

	/* (the old one goes first: both at once may not fit) */
	Util::destroy(frontend);
	frontend = Util::create<CommandProcessor>(*device, reinterpret_cast<void *>(rdram_base),
				rdram_offset, 8 * 1024 * 1024, 4 * 1024 * 1024, flags);

	if (!frontend->device_is_supported())
	{
		log_cb(RETRO_LOG_ERROR, "This device probably does not support 16-bit storage. Make sure you're using up-to-date drivers!\n");
		Util::destroy(frontend);
		frontend = nullptr;
		return false;
	}

	quirks.set_native_texture_lod(native_texture_lod);
	quirks.set_native_resolution_tex_rect(native_tex_rect);
	frontend->set_quirks(quirks);
	processor_requested = factor;
	processor_factor = frontend->get_scaling_factor();
	{
		/* What the device's storage buffer bindings did not fit, said on
		 * screen too: the options still show what was chosen */
		bool ssaa_asked = (flags & COMMAND_PROCESSOR_FLAG_SUPER_SAMPLED_READ_BACK_BIT) != 0;
		bool ssaa = frontend->get_super_sampled_readback();
		char text[160];
		text[0] = '\0';
		if (processor_factor != factor)
			snprintf(text, sizeof(text),
					"Upscaling: %ux needs larger storage buffers than this GPU allows, rendering at %ux%s.",
					factor, processor_factor,
					ssaa_asked && !ssaa ? " without super-sampled read-back" : "");
		else if (ssaa_asked && !ssaa)
			snprintf(text, sizeof(text),
					"Super-sampled read-back is off: at %ux it needs a larger storage buffer than this GPU allows.",
					processor_factor);
		if (text[0])
		{
			struct retro_message msg;
			msg.msg = text;
			msg.frames = 360;
			log_cb(RETRO_LOG_WARN, "%s\n", text);
			environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE, &msg);
		}
		else if (ssaa)
			log_cb(RETRO_LOG_INFO, "Using super-sampled read-back!\n");
	}
	return true;
}

/* Once a frame, before it runs: whether it is drawn for the upscaler and
 * at which factor */
static void upscaler_poll()
{
	const bool requested = upscaler_iface && upscaler_iface->get_request(upscaler_iface->handle, &upscaler_request);
	const rdp_upscaler_decision_t d = rdp_upscaler_policy_poll(&upscaler_policy, requested ? &upscaler_request : NULL,
			upscaling, processor_requested, processor_factor, super_sampled_read_back);

	if (d.remake)
	{
		if (frontend)
			frontend->idle();
		if (create_processor(d.factor))
			rdp_intake_replay_state(&intake);
		else if (!create_processor(1))
			device_supported = false;
		/* (only what this factor adds is new) */
		if (device_supported)
			build_pipelines_ahead();
	}
	if (upscaler_iface)
	{
		/* (when it changes: the frontend can only tell that frames came
		 * without inputs) */
		char msg[512];
		enum retro_log_level level;
		if (rdp_upscaler_policy_message(&upscaler_policy, requested, upscaler_policy.on, processor_factor,
		                                &upscaler_request, msg, sizeof(msg), &level))
			log_cb(level, "%s", msg);
	}
}

/* Before init: init builds this upscaler's pipelines at load. */
void offer_upscaler_interface(const struct retro_hw_render_upscaler_interface *iface)
{
	upscaler_iface = iface;
}

void set_upscaler_interface(const struct retro_hw_render_upscaler_interface *iface)
{
	upscaler_iface = iface;
	upscaler_policy.reset_pending = true;
	/* before the first frame: what it draws at boot is drawn for it */
	if (device_supported)
		upscaler_poll();
}

bool upscaler_active()
{
	return upscaler_policy.on;
}

void upscaler_reset()
{
	upscaler_policy.reset_pending = true;
}

static uint64_t vk_handle(const void *handle, size_t size)
{
	uint64_t value = 0;
	memcpy(&value, handle, size < sizeof(value) ? size : sizeof(value));
	return value;
}

static void upscaler_image(struct retro_hw_upscaler_image &out, const Vulkan::Image &image)
{
	VkImage vk_image = image.get_image();
	VkImageView vk_view = image.get_view().get_view();
	out.image = vk_handle(&vk_image, sizeof(vk_image));
	out.view = vk_handle(&vk_view, sizeof(vk_view));
	out.format = unsigned(image.get_format());
	out.state = unsigned(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	out.width = image.get_width();
	out.height = image.get_height();
}

bool init()
{
	test_wait_pipelines = getenv("PARALLEL_RDP_TEST_WAIT_PIPELINES") != nullptr;
	if (!context || !vulkan)
		return false;

	unsigned mask = vulkan->get_sync_index_mask(vulkan->handle);
	unsigned num_frames = 0;
	unsigned num_sync_frames = 0;
	for (unsigned i = 0; i < 32; i++)
	{
		if (mask & (1u << i))
		{
			num_frames = i + 1;
			num_sync_frames++;
		}
	}

	set_num_sync_frames(num_frames);

	Util::destroy(device);
	device = Util::create<Device>();
	device->set_context(*context);
	pipeline_files_load();
	device->init_frame_contexts(num_sync_frames);
	log_cb(RETRO_LOG_INFO, "Using %u sync frames for parallel-RDP.\n", num_sync_frames);
	device->set_queue_lock(vulkan->lock_queue, vulkan->unlock_queue, vulkan->handle);

	uintptr_t aligned_rdram = reinterpret_cast<uintptr_t>(gfx_info.RDRAM);
	uintptr_t offset = 0;

	if (device->get_device_features().supports_external_memory_host)
	{
		size_t align = device->get_device_features().host_memory_properties.minImportedHostPointerAlignment;
		offset = aligned_rdram & (align - 1);
		aligned_rdram -= offset;
	}
	else
		log_cb(RETRO_LOG_WARN, "VK_EXT_external_memory_host is not supported by this device. Application might run slower because of this.\n");

	rdram_base = aligned_rdram;
	rdram_offset = offset;
	{
		/* (nothing of an earlier session's list or state) */
		rdp_intake_callbacks_t cb;
		cb.enqueue = intake_enqueue;
		cb.sync_full = intake_sync_full;
		cb.user = NULL;
		rdp_intake_init(&intake, &cb);
	}
	upscaler_policy.on = false;
	upscaler_policy.reset_pending = true;
	if (!create_processor(upscaling))
		return false;
	build_pipelines_ahead();

	timeline_value = 0;
	pending_timeline_value = 0;
	width = 0;
	height = 0;
	device_supported = true;
	return true;
}

void deinit()
{
	device_supported = false;
	set_num_sync_frames(0);
	upscaler_iface = nullptr;
	upscaler_policy.on = false;
	Util::destroy(frontend);
	frontend = nullptr;
	if (device)
		pipeline_files_save();
	Util::destroy(device);
	device = nullptr;
	Util::destroy(context);
	context = nullptr;
}

static void complete_frame_error()
{
	static const char error_tex[] =
		"ooooooooooooooooooooooooo"
		"ooXXXXXoooXXXXXoooXXXXXoo"
		"ooXXooooooXoooXoooXoooXoo"
		"ooXXXXXoooXXXXXoooXXXXXoo"
		"ooXXXXXoooXoXoooooXoXoooo"
		"ooXXooooooXooXooooXooXooo"
		"ooXXXXXoooXoooXoooXoooXoo"
		"ooooooooooooooooooooooooo";

	auto info = Vulkan::ImageCreateInfo::immutable_2d_image(50, 16, VK_FORMAT_R8G8B8A8_UNORM);
	info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	info.misc = IMAGE_MISC_MUTABLE_SRGB_BIT;

	Vulkan::ImageInitialData data = {};

	uint32_t tex_data[16][50];
	for (unsigned y = 0; y < 16; y++)
		for (unsigned x = 0; x < 50; x++)
			tex_data[y][x] = error_tex[25 * (y >> 1) + (x >> 1)] != 'o' ? 0xffffffffu : 0u;
	data.data = tex_data;
	auto image = device->create_image(info, &data);

	unsigned index = vulkan->get_sync_index(vulkan->handle);
	assert(index < num_retro_images);

	retro_images[index].image_view = image->get_view().get_view();
	retro_images[index].image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	retro_images[index].create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	retro_images[index].create_info.image = image->get_image();
	retro_images[index].create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	retro_images[index].create_info.format = VK_FORMAT_R8G8B8A8_UNORM;
	retro_images[index].create_info.subresourceRange.baseMipLevel = 0;
	retro_images[index].create_info.subresourceRange.baseArrayLayer = 0;
	retro_images[index].create_info.subresourceRange.levelCount = 1;
	retro_images[index].create_info.subresourceRange.layerCount = 1;
	retro_images[index].create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	retro_images[index].create_info.components.r = VK_COMPONENT_SWIZZLE_R;
	retro_images[index].create_info.components.g = VK_COMPONENT_SWIZZLE_G;
	retro_images[index].create_info.components.b = VK_COMPONENT_SWIZZLE_B;
	retro_images[index].create_info.components.a = VK_COMPONENT_SWIZZLE_A;

	vulkan->set_image(vulkan->handle, &retro_images[index], 0, nullptr, VK_QUEUE_FAMILY_IGNORED);
	width = image->get_width();
	height = image->get_height();
	retro_image_handles[index] = image;

	device->flush_frame();
}

// A cleared 1x1 image for when there is nothing to scan out.
static void make_blank_image(void *out)
{
	auto &image = *static_cast<Vulkan::ImageHandle *>(out);
	auto info = Vulkan::ImageCreateInfo::immutable_2d_image(1, 1, VK_FORMAT_R8G8B8A8_UNORM);
	info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
		VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	info.misc = IMAGE_MISC_MUTABLE_SRGB_BIT;
	info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	image = device->create_image(info);

	auto cmd = device->request_command_buffer();
	cmd->image_barrier(*image,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
	cmd->clear_image(*image, {});
	cmd->image_barrier(*image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
	device->submit(cmd);
}

void complete_frame()
{
	if (!device_supported)
		return;

	if (!frontend)
	{
		complete_frame_error();
		device->next_frame_context();
		return;
	}

	timeline_value = frontend->signal_timeline();

	frontend->set_vi_register(VIRegister::Control, *GET_GFX_INFO(VI_STATUS_REG));
	frontend->set_vi_register(VIRegister::Origin, *GET_GFX_INFO(VI_ORIGIN_REG));
	frontend->set_vi_register(VIRegister::Width, *GET_GFX_INFO(VI_WIDTH_REG));
	frontend->set_vi_register(VIRegister::Intr, *GET_GFX_INFO(VI_INTR_REG));
	frontend->set_vi_register(VIRegister::VCurrentLine, *GET_GFX_INFO(VI_V_CURRENT_LINE_REG));
	frontend->set_vi_register(VIRegister::Timing, *GET_GFX_INFO(VI_V_BURST_REG));
	frontend->set_vi_register(VIRegister::VSync, *GET_GFX_INFO(VI_V_SYNC_REG));
	frontend->set_vi_register(VIRegister::HSync, *GET_GFX_INFO(VI_H_SYNC_REG));
	frontend->set_vi_register(VIRegister::Leap, *GET_GFX_INFO(VI_LEAP_REG));
	frontend->set_vi_register(VIRegister::HStart, *GET_GFX_INFO(VI_H_START_REG));
	frontend->set_vi_register(VIRegister::VStart, *GET_GFX_INFO(VI_V_START_REG));
	frontend->set_vi_register(VIRegister::VBurst, *GET_GFX_INFO(VI_V_BURST_REG));
	frontend->set_vi_register(VIRegister::XScale, *GET_GFX_INFO(VI_X_SCALE_REG));
	frontend->set_vi_register(VIRegister::YScale, *GET_GFX_INFO(VI_Y_SCALE_REG));

	ScanoutOptions opts;
	opts.persist_frame_on_invalid_input = true;
	opts.vi.aa = vi_aa;
	opts.vi.scale = vi_scale;
	opts.vi.dither_filter = dither_filter;
	opts.vi.divot_filter = divot_filter;
	opts.vi.gamma_dither = gamma_dither;
	opts.blend_previous_frame = interlacing;
	opts.upscale_deinterlacing = !interlacing;
	opts.downscale_steps = downscaling_steps;
	opts.crop_overscan_pixels = overscan;
	opts.upscaler.enable = upscaler_policy.on;
	opts.upscaler.jitter_phase = rdp_upscaler_policy_jitter_phase(&upscaler_policy, upscaler_request.output_width,
	                                                              processor_factor);
	opts.upscaler.reset = upscaler_policy.reset_pending;
	upscaler_policy.reset_pending = false;
	auto image = frontend->scanout(opts);
	unsigned index = vulkan->get_sync_index(vulkan->handle);
	UpscalerFrame upscaler_frame = {};

	if (upscaler_policy.on)
		frontend->get_upscaler_frame(upscaler_frame);

	if (!image)
	{
		// Recorded and submitted on the command thread, which drives the
		// Vulkan device.
		frontend->run_on_command_thread(make_blank_image, &image);
	}

	assert(index < num_retro_images);

	retro_images[index].image_view = image->get_view().get_view();
	retro_images[index].image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	retro_images[index].create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	retro_images[index].create_info.image = image->get_image();
	retro_images[index].create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	retro_images[index].create_info.format = VK_FORMAT_R8G8B8A8_UNORM;
	retro_images[index].create_info.subresourceRange.baseMipLevel = 0;
	retro_images[index].create_info.subresourceRange.baseArrayLayer = 0;
	retro_images[index].create_info.subresourceRange.levelCount = 1;
	retro_images[index].create_info.subresourceRange.layerCount = 1;
	retro_images[index].create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	retro_images[index].create_info.components.r = VK_COMPONENT_SWIZZLE_R;
	retro_images[index].create_info.components.g = VK_COMPONENT_SWIZZLE_G;
	retro_images[index].create_info.components.b = VK_COMPONENT_SWIZZLE_B;
	retro_images[index].create_info.components.a = VK_COMPONENT_SWIZZLE_A;

	if (upscaler_policy.on && upscaler_frame.motion_vectors && upscaler_frame.depth && upscaler_frame.translucent)
	{
		/* (the frame's images live as long as its color) */
		upscaler_policy.native_width = image->get_width() / processor_factor;
		upscaler_policy.native_height = image->get_height() / processor_factor;
		memset(&upscaler_inputs, 0, sizeof(upscaler_inputs));
		upscaler_inputs.flags = upscaler_frame.reset ? RETRO_HW_UPSCALER_RESET : 0;
		upscaler_image(upscaler_inputs.motion_vectors, *upscaler_frame.motion_vectors);
		upscaler_image(upscaler_inputs.depth, *upscaler_frame.depth);
		upscaler_image(upscaler_inputs.transparency_mask, *upscaler_frame.translucent);
		upscaler_inputs.jitter_x = upscaler_frame.jitter_x;
		upscaler_inputs.jitter_y = upscaler_frame.jitter_y;
		upscaler_inputs.mv_scale_x = 1.0f;
		upscaler_inputs.mv_scale_y = 1.0f;
		upscaler_inputs.pre_exposure = 1.0f;
		upscaler_inputs.exposure_scale = 1.0f;
		upscaler_handles[3 * index] = upscaler_frame.motion_vectors;
		upscaler_handles[3 * index + 1] = upscaler_frame.depth;
		upscaler_handles[3 * index + 2] = upscaler_frame.translucent;
		upscaler_iface->set_inputs(upscaler_iface->handle, &upscaler_inputs);
	}
	else if (upscaler_iface)
	{
		upscaler_handles[3 * index].reset();
		upscaler_handles[3 * index + 1].reset();
		upscaler_handles[3 * index + 2].reset();
	}

	vulkan->set_image(vulkan->handle, &retro_images[index], 0, nullptr, VK_QUEUE_FAMILY_IGNORED);
	width = image->get_width();
	height = image->get_height();
	retro_image_handles[index] = image;


	RDP::Quirks quirks;
	quirks.set_native_texture_lod(native_texture_lod);
	quirks.set_native_resolution_tex_rect(native_tex_rect);
	frontend->set_quirks(quirks);

	frontend->begin_frame_context();
}
}

namespace RDP
{
// vkCreateDevice through the frontend (negotiation v2), which adds the
// extensions and features it wants, e.g. for its low-latency modes.
struct FrontendDeviceFactory
{
	retro_vulkan_create_device_wrapper_t wrapper;
	void *opaque;
};

static VkDevice frontend_create_device(void *user, VkPhysicalDevice gpu, const VkDeviceCreateInfo *info)
{
	auto *f = static_cast<FrontendDeviceFactory *>(user);
	return f->wrapper(gpu, f->opaque, info);
}

static bool create_device(struct retro_vulkan_context *frontend_context, VkInstance instance, VkPhysicalDevice gpu,
                          VkSurfaceKHR surface, PFN_vkGetInstanceProcAddr get_instance_proc_addr,
                          const char **required_device_extensions, unsigned num_required_device_extensions,
                          const VkPhysicalDeviceFeatures *required_features, Vulkan::DeviceFactory *factory)
{
	if (!Vulkan::Context::init_loader(get_instance_proc_addr))
		return false;

	Util::destroy(::RDP::context);
	::RDP::context = Util::create<Vulkan::Context>();

	::RDP::context->set_device_factory(factory);

	bool ok = ::RDP::context->init_device_from_instance(
			instance, gpu, surface, required_device_extensions, num_required_device_extensions,
			required_features);
	::RDP::context->set_device_factory(nullptr);
	if (!ok)
	{
		Util::destroy(::RDP::context);
		::RDP::context = nullptr;
		return false;
	}

	frontend_context->gpu = ::RDP::context->get_gpu();
	frontend_context->device = ::RDP::context->get_device();
	frontend_context->queue = ::RDP::context->get_queue_info().queues[Vulkan::QUEUE_INDEX_GRAPHICS];
	frontend_context->queue_family_index = ::RDP::context->get_queue_info().family_indices[Vulkan::QUEUE_INDEX_GRAPHICS];
	frontend_context->presentation_queue = ::RDP::context->get_queue_info().queues[Vulkan::QUEUE_INDEX_GRAPHICS];
	frontend_context->presentation_queue_family_index = ::RDP::context->get_queue_info().family_indices[Vulkan::QUEUE_INDEX_GRAPHICS];

	// Frontend owns the device.
	::RDP::context->release_device();
	return true;
}
}

bool parallel_create_device(struct retro_vulkan_context *frontend_context, VkInstance instance, VkPhysicalDevice gpu,
                            VkSurfaceKHR surface, PFN_vkGetInstanceProcAddr get_instance_proc_addr,
                            const char **required_device_extensions, unsigned num_required_device_extensions,
                            const char **required_device_layers, unsigned num_required_device_layers,
                            const VkPhysicalDeviceFeatures *required_features)
{
	return ::RDP::create_device(frontend_context, instance, gpu, surface, get_instance_proc_addr,
	                            required_device_extensions, num_required_device_extensions,
	                            required_features, nullptr);
}

bool parallel_create_device2(struct retro_vulkan_context *frontend_context, VkInstance instance, VkPhysicalDevice gpu,
                             VkSurfaceKHR surface, PFN_vkGetInstanceProcAddr get_instance_proc_addr,
                             retro_vulkan_create_device_wrapper_t create_device_wrapper, void *opaque)
{
	static const VkPhysicalDeviceFeatures no_features = {};
	::RDP::FrontendDeviceFactory frontend;
	Vulkan::DeviceFactory factory;
	frontend.wrapper = create_device_wrapper;
	frontend.opaque = opaque;
	factory.create_device = ::RDP::frontend_create_device;
	factory.user = &frontend;
	return ::RDP::create_device(frontend_context, instance, gpu, surface, get_instance_proc_addr,
	                            nullptr, 0, &no_features, &factory);
}

static const VkApplicationInfo parallel_app_info = {
	VK_STRUCTURE_TYPE_APPLICATION_INFO,
	nullptr,
	"paraLLEl-RDP",
	0,
	"Granite",
	0,
	VK_API_VERSION_1_1,
};

const VkApplicationInfo *parallel_get_application_info(void)
{
	return &parallel_app_info;
}

