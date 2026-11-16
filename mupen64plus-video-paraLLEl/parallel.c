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


/* paraLLEl-RDP as the core's graphics plugin: the plugin's entry points,
 * the options, and the frame: the RDP's command list run, the VI's image
 * handed to the frontend each frame, with the temporal upscaler's inputs,
 * and the Vulkan device made through the frontend. C89. */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <boolean.h>
#include <compat/msvc.h>

#include "Gfx #1.3.h"
#include "m64p_config.h"
#include "m64p_types.h"
#include "z64.h"
#include "parallel.h"
#include "parallel_internal.h"
#include "rdp_intake.h"
#include "rdp_pipeline_files.h"
#include "rdp_upscaler_policy.h"
#include "context.h"
#include "device.h"
#include "image.h"
#include "command_buffer.h"
#include "command_ops.h"
#include "pipeline_cache.h"
#include "rdp_processor.h"

extern retro_log_printf_t log_cb;
extern retro_environment_t environ_cb;
extern int retro_return(bool just_flipping);

/* Per sync index; the frontend's index mask is 32 bits */
#define MAX_SYNC_FRAMES 32

static const struct retro_hw_render_interface_vulkan *vulkan;

/* The Vulkan device: the frontend's, made through it */
static granite_context_t context;
static bool have_context;
static granite_device_t device;
static bool have_device;
/* The command processor, made anew for another upscaling factor */
static rdp_processor_t processor;
static bool have_processor;

/* Set only when init fully succeeds (including the device meeting
 * parallel-RDP's feature requirements). The frame's entry points do
 * nothing without it, so that a failed or unsupported init (a MoltenVK
 * build without the 8 and 16-bit storage parallel-RDP needs) shows
 * nothing rather than drive a device half made. */
static bool device_supported;

/* The image handed to the frontend at each sync index, held until the
 * index comes round again */
static struct retro_vulkan_image retro_images[MAX_SYNC_FRAMES];
static granite_image_t *retro_image_handles[MAX_SYNC_FRAMES];
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
static granite_image_t *upscaler_handles[3 * MAX_SYNC_FRAMES];
/* What the upscaler is given (rdp_upscaler_policy.c) */
static rdp_upscaler_policy_t upscaler_policy = { false, true, 320, 240, 0, 0, ~0u };
/* test hosts that count what the frame's threads build have load wait
 * for the worker (PARALLEL_RDP_TEST_WAIT_PIPELINES, read at init);
 * nothing else does */
static bool test_wait_pipelines;

/* The command list, where the DPC registers point (rdp_intake.c) */
static rdp_intake_t intake;
/* The driver's pipeline cache and the list of the pipelines built
 * (rdp_pipeline_files.c) */
static rdp_pipeline_files_t pipeline_files;

/* the options */
static unsigned frame_width, frame_height;
static unsigned overscan;
static unsigned upscaling = 1;
static unsigned downscaling_steps;
static bool native_texture_lod;
static bool native_tex_rect = true;
static bool super_sampled_read_back;
static bool super_sampled_dither;
static bool synchronous = true;
static bool divot_filter = true;
static bool gamma_dither = true;
static bool vi_aa = true;
static bool vi_scale = true;
static bool dither_filter = true;
static bool interlacing = true;

static void upscaler_poll(void);

/* @slot takes the caller's reference to @image, letting go of its own */
static void hold(granite_image_t **slot, granite_image_t *image)
{
	if (*slot)
		granite_image_unref(*slot);
	*slot = image;
}

/* The frames released past @n */
static void set_num_sync_frames(unsigned n)
{
	unsigned i;
	if (n > MAX_SYNC_FRAMES)
		n = MAX_SYNC_FRAMES;
	for (i = n; i < MAX_SYNC_FRAMES; i++)
	{
		memset(&retro_images[i], 0, sizeof(retro_images[i]));
		hold(&retro_image_handles[i], NULL);
		hold(&upscaler_handles[3 * i], NULL);
		hold(&upscaler_handles[3 * i + 1], NULL);
		hold(&upscaler_handles[3 * i + 2], NULL);
	}
	num_retro_images = n;
}

/* The highest sync index the frontend uses, plus one, and how many */
static unsigned sync_frames(unsigned *count)
{
	unsigned mask = vulkan->get_sync_index_mask(vulkan->handle);
	unsigned frames = 0, n = 0, i;
	for (i = 0; i < 32; i++)
		if (mask & (1u << i))
		{
			frames = i + 1;
			n++;
		}
	if (count)
		*count = n;
	return frames;
}

static uint32_t quirks(void)
{
	return (native_texture_lod ? RDP_DECODE_NATIVE_TEXTURE_LOD : 0u)
		| (native_tex_rect ? RDP_DECODE_NATIVE_TEX_RECT : 0u);
}

/* ---- the command list ---- */
static void intake_enqueue(void *user, unsigned num_words, const uint32_t *words)
{
	(void)user;
	if (have_processor)
		rdp_processor_enqueue(&processor, num_words, words);
}

static void intake_sync_full(void *user)
{
	(void)user;
	/* (a synchronous RDP waits for it) */
	if (synchronous && have_processor)
		rdp_processor_wait_for_timeline(&processor, rdp_processor_signal_timeline(&processor));
	*gfx_info.MI_INTR_REG |= DP_INTERRUPT;
	gfx_info.CheckInterrupts();
}

/* The HLE emitter synthesizes an RDP command list into a host buffer (out
 * of guest RDRAM, so Expansion Pak games are not corrupted) and reports
 * it as occupying [base, base + len). While it is set, the command words
 * come from that buffer; the address range is otherwise the same, so
 * texture and image reads still hit RDRAM. Cleared after each submit, so
 * the LLE path is untouched. */
void parallel_set_hle_cmd_buffer(const uint8_t *buf, uint32_t base_byte_addr, uint32_t len_bytes)
{
	rdp_intake_set_hle_buffer(&intake, buf, base_byte_addr, len_bytes);
}

void parallel_process_commands(void)
{
	if (!device_supported)
		return;
	if (rdp_intake_process(&intake, *GET_GFX_INFO(DPC_CURRENT_REG), *GET_GFX_INFO(DPC_END_REG),
			*GET_GFX_INFO(DPC_STATUS_REG), DRAM, SP_DMEM))
		*GET_GFX_INFO(DPC_CURRENT_REG) = *GET_GFX_INFO(DPC_END_REG);
}

void parallel_begin_frame(void)
{
	if (!device_supported)
		return;
	set_num_sync_frames(sync_frames(NULL));
	vulkan->wait_sync_index(vulkan->handle);
	upscaler_poll();
}

/* ---- the pipelines ---- */

/* The pipelines the list and the renderer's options name are built on the
 * worker, and nothing waits for them: a draw that gets to one first builds
 * its own, as on a first run it always did */
static void build_pipelines_ahead(void)
{
	rdp_processor_build_pipelines_ahead(&processor, pipeline_files.list, pipeline_files.list_count,
			upscaler_iface != NULL);
	if (test_wait_pipelines)
		rdp_processor_wait_pipelines_ahead_for_test(&processor);
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
	granite_pipeline_cache_init(device.device, device.table, device.gpu_props.pipelineCacheUUID,
			(const uint8_t *)blob, size, &device.pipeline_cache);
	free(blob);
}

/* Once no thread builds pipelines any more and the device is still there */
static void pipeline_files_save(void)
{
	const pipeline_record_t *built = NULL;
	size_t num_built = granite_pipeline_record_table_get(&device.pipeline_records, &built);
	size_t size = granite_pipeline_cache_blob_size(device.device, device.table, device.pipeline_cache);
	uint8_t *blob = size ? (uint8_t *)malloc(size) : NULL;
	if (blob && !granite_pipeline_cache_blob(device.device, device.table, device.pipeline_cache,
			device.gpu_props.pipelineCacheUUID, blob, size))
	{
		free(blob);
		blob = NULL;
	}
	rdp_pipeline_files_save(&pipeline_files, built, num_built, blob, blob ? size : 0);
	free(blob);
}

/* ---- the command processor ---- */
static void destroy_processor(void)
{
	if (have_processor)
		rdp_processor_free(&processor);
	have_processor = false;
}

static bool create_processor(unsigned factor)
{
	uint32_t flags = 0;

	switch (factor)
	{
		case 2:
			flags |= RDP_PROCESSOR_UPSCALING_2X;
			break;
		case 4:
			flags |= RDP_PROCESSOR_UPSCALING_4X;
			break;
		case 8:
			flags |= RDP_PROCESSOR_UPSCALING_8X;
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
		flags |= RDP_PROCESSOR_SUPER_SAMPLED_READ_BACK;
		if (super_sampled_dither)
			flags |= RDP_PROCESSOR_SUPER_SAMPLED_DITHER;
	}

	/* (the old one goes first: both at once may not fit) */
	destroy_processor();
	rdp_processor_init(&processor, &device, (void *)rdram_base, rdram_offset, 8 * 1024 * 1024, 4 * 1024 * 1024,
			flags);
	have_processor = true;
	if (!rdp_processor_supported(&processor))
	{
		log_cb(RETRO_LOG_ERROR, "This device probably does not support 16-bit storage. Make sure you're using up-to-date drivers!\n");
		destroy_processor();
		return false;
	}

	rdp_processor_set_quirks(&processor, quirks());
	processor_requested = factor;
	processor_factor = rdp_processor_scaling_factor(&processor);
	{
		/* What the device's storage buffer bindings did not fit, said on
		 * screen too: the options still show what was chosen */
		const bool ssaa_asked = (flags & RDP_PROCESSOR_SUPER_SAMPLED_READ_BACK) != 0;
		const bool ssaa = rdp_processor_super_sampled_readback(&processor);
		char text[160];
		text[0] = '\0';
		if (processor_factor != factor)
			snprintf(text, sizeof(text),
					"Upscaling: %ux needs larger storage buffers than this GPU allows, rendering at %ux%s.",
					factor, processor_factor, ssaa_asked && !ssaa ? " without super-sampled read-back" : "");
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

/* ---- the temporal upscaler ---- */

/* Once a frame, before it runs: whether it is drawn for the upscaler and
 * at which factor */
static void upscaler_poll(void)
{
	const bool requested = upscaler_iface && upscaler_iface->get_request(upscaler_iface->handle, &upscaler_request);
	const rdp_upscaler_decision_t d = rdp_upscaler_policy_poll(&upscaler_policy, requested ? &upscaler_request : NULL,
			upscaling, processor_requested, processor_factor, super_sampled_read_back);

	if (d.remake)
	{
		if (have_processor)
			rdp_processor_idle(&processor);
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

/* Before init: init builds this upscaler's pipelines at load */
void parallel_offer_upscaler_interface(const struct retro_hw_render_upscaler_interface *iface)
{
	upscaler_iface = iface;
}

void parallel_set_upscaler_interface(const struct retro_hw_render_upscaler_interface *iface)
{
	upscaler_iface = iface;
	upscaler_policy.reset_pending = true;
	/* before the first frame: what it draws at boot is drawn for it */
	if (device_supported)
		upscaler_poll();
}

bool parallel_upscaler_active(void)
{
	return upscaler_policy.on;
}

void parallel_upscaler_reset(void)
{
	upscaler_policy.reset_pending = true;
}

static uint64_t vk_handle(const void *handle, size_t size)
{
	uint64_t value = 0;
	memcpy(&value, handle, size < sizeof(value) ? size : sizeof(value));
	return value;
}

static void upscaler_image(struct retro_hw_upscaler_image *out, const granite_image_t *image)
{
	out->image = vk_handle(&image->image, sizeof(image->image));
	out->view = vk_handle(&image->view->views.view, sizeof(image->view->views.view));
	out->format = (unsigned)image->info.format;
	out->state = (unsigned)VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	out->width = image->info.width;
	out->height = image->info.height;
}

/* ---- load and close ---- */
bool parallel_init(const struct retro_hw_render_interface_vulkan *iface)
{
	unsigned num_frames, num_sync_frames;
	uintptr_t aligned_rdram, offset = 0;

	vulkan = iface;
	test_wait_pipelines = getenv("PARALLEL_RDP_TEST_WAIT_PIPELINES") != NULL;
	if (!have_context || !vulkan)
		return false;

	num_frames = sync_frames(&num_sync_frames);
	set_num_sync_frames(num_frames);

	destroy_processor();
	if (have_device)
		granite_device_free(&device);
	granite_device_init(&device);
	granite_device_set_context(&device, &context);
	have_device = true;
	pipeline_files_load();
	granite_device_init_frame_contexts(&device, num_sync_frames);
	log_cb(RETRO_LOG_INFO, "Using %u sync frames for parallel-RDP.\n", num_sync_frames);
	device.submission.queue_lock = vulkan->lock_queue;
	device.submission.queue_unlock = vulkan->unlock_queue;
	device.submission.queue_user = vulkan->handle;

	aligned_rdram = (uintptr_t)gfx_info.RDRAM;
	if (device.ext.supports_external_memory_host)
	{
		const size_t align = (size_t)device.ext.host_memory_properties.minImportedHostPointerAlignment;
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

	frame_width = 0;
	frame_height = 0;
	device_supported = true;
	return true;
}

void parallel_deinit(void)
{
	device_supported = false;
	set_num_sync_frames(0);
	upscaler_iface = NULL;
	upscaler_policy.on = false;
	destroy_processor();
	if (have_device)
	{
		pipeline_files_save();
		granite_device_free(&device);
		have_device = false;
	}
	if (have_context)
	{
		granite_context_destroy(&context);
		have_context = false;
	}
	vulkan = NULL;
}

/* ---- the frame ---- */

/* The image handed to the frontend at this sync index: @image's view,
 * the whole of it, sampled */
static void hand_over(unsigned index, granite_image_t *image)
{
	struct retro_vulkan_image *out = &retro_images[index];
	out->image_view = image->view->views.view;
	out->image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	out->create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	out->create_info.image = image->image;
	out->create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	out->create_info.format = VK_FORMAT_R8G8B8A8_UNORM;
	out->create_info.subresourceRange.baseMipLevel = 0;
	out->create_info.subresourceRange.baseArrayLayer = 0;
	out->create_info.subresourceRange.levelCount = 1;
	out->create_info.subresourceRange.layerCount = 1;
	out->create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	out->create_info.components.r = VK_COMPONENT_SWIZZLE_R;
	out->create_info.components.g = VK_COMPONENT_SWIZZLE_G;
	out->create_info.components.b = VK_COMPONENT_SWIZZLE_B;
	out->create_info.components.a = VK_COMPONENT_SWIZZLE_A;
}

static void image_info_2d(granite_image_info_t *info, unsigned width, unsigned height, VkImageUsageFlags usage,
		VkImageLayout initial_layout)
{
	memset(info, 0, sizeof(*info));
	info->width = width;
	info->height = height;
	info->depth = 1;
	info->levels = 1;
	info->format = VK_FORMAT_R8G8B8A8_UNORM;
	info->type = VK_IMAGE_TYPE_2D;
	info->layers = 1;
	info->usage = usage;
	info->samples = VK_SAMPLE_COUNT_1_BIT;
	info->misc = GRANITE_IMAGE_MISC_MUTABLE_SRGB_BIT;
	info->initial_layout = initial_layout;
}

/* Without a command processor: "ERR" */
static void complete_frame_error(void)
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
	static uint32_t tex_data[16][50];
	granite_image_info_t info;
	granite_image_initial_data_t data;
	granite_image_t *image;
	unsigned index, x, y;

	image_info_2d(&info, 50, 16, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	for (y = 0; y < 16; y++)
		for (x = 0; x < 50; x++)
			tex_data[y][x] = error_tex[25 * (y >> 1) + (x >> 1)] != 'o' ? 0xffffffffu : 0u;
	data.data = tex_data;
	data.row_length = 0;
	image = granite_device_create_image(&device, &info, &data);
	if (!image)
		return;

	index = vulkan->get_sync_index(vulkan->handle);
	assert(index < num_retro_images);
	hand_over(index, image);
	vulkan->set_image(vulkan->handle, &retro_images[index], 0, NULL, VK_QUEUE_FAMILY_IGNORED);
	frame_width = image->info.width;
	frame_height = image->info.height;
	hold(&retro_image_handles[index], image);

	granite_device_flush_frame(&device);
}

/* A cleared 1x1 image for when there is nothing to scan out: made on the
 * command thread, which drives the device */
static void make_blank_image(void *out)
{
	granite_image_t **image = (granite_image_t **)out;
	granite_image_info_t info;
	granite_command_buffer_t *cb;
	VkClearValue clear;

	image_info_2d(&info, 1, 1, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
			| VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_LAYOUT_UNDEFINED);
	*image = granite_device_create_image(&device, &info, NULL);
	if (!*image || !(cb = granite_device_request_command_buffer(&device, GRANITE_QUEUE_GRAPHICS)))
		return;
	memset(&clear, 0, sizeof(clear));
	granite_cb_image_barrier(cb, *image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
	granite_cb_clear_image(cb, *image, &clear, granite_format_aspect_mask(info.format));
	granite_cb_image_barrier(cb, *image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_ACCESS_SHADER_READ_BIT);
	granite_device_submit(&device, cb, NULL, 0, NULL);
}

static void drop_targets(rdp_upscaler_targets_t *t)
{
	if (t->motion_vectors)
		granite_image_unref(t->motion_vectors);
	if (t->depth)
		granite_image_unref(t->depth);
	if (t->translucent)
		granite_image_unref(t->translucent);
	memset(t, 0, sizeof(*t));
}

static void complete_frame(void)
{
	rdp_vi_scanout_options_t opts;
	rdp_upscaler_targets_t frame;
	granite_image_t *image;
	unsigned index;

	if (!have_processor)
	{
		complete_frame_error();
		granite_device_next_frame_context(&device);
		return;
	}

	rdp_processor_signal_timeline(&processor);

	rdp_processor_set_vi_register(&processor, RDP_VI_REGISTER_CONTROL, *GET_GFX_INFO(VI_STATUS_REG));
	rdp_processor_set_vi_register(&processor, RDP_VI_REGISTER_ORIGIN, *GET_GFX_INFO(VI_ORIGIN_REG));
	rdp_processor_set_vi_register(&processor, RDP_VI_REGISTER_WIDTH, *GET_GFX_INFO(VI_WIDTH_REG));
	rdp_processor_set_vi_register(&processor, RDP_VI_REGISTER_INTR, *GET_GFX_INFO(VI_INTR_REG));
	rdp_processor_set_vi_register(&processor, RDP_VI_REGISTER_V_CURRENT_LINE, *GET_GFX_INFO(VI_V_CURRENT_LINE_REG));
	rdp_processor_set_vi_register(&processor, RDP_VI_REGISTER_TIMING, *GET_GFX_INFO(VI_V_BURST_REG));
	rdp_processor_set_vi_register(&processor, RDP_VI_REGISTER_V_SYNC, *GET_GFX_INFO(VI_V_SYNC_REG));
	rdp_processor_set_vi_register(&processor, RDP_VI_REGISTER_H_SYNC, *GET_GFX_INFO(VI_H_SYNC_REG));
	rdp_processor_set_vi_register(&processor, RDP_VI_REGISTER_LEAP, *GET_GFX_INFO(VI_LEAP_REG));
	rdp_processor_set_vi_register(&processor, RDP_VI_REGISTER_H_START, *GET_GFX_INFO(VI_H_START_REG));
	rdp_processor_set_vi_register(&processor, RDP_VI_REGISTER_V_START, *GET_GFX_INFO(VI_V_START_REG));
	rdp_processor_set_vi_register(&processor, RDP_VI_REGISTER_V_BURST, *GET_GFX_INFO(VI_V_BURST_REG));
	rdp_processor_set_vi_register(&processor, RDP_VI_REGISTER_X_SCALE, *GET_GFX_INFO(VI_X_SCALE_REG));
	rdp_processor_set_vi_register(&processor, RDP_VI_REGISTER_Y_SCALE, *GET_GFX_INFO(VI_Y_SCALE_REG));

	rdp_vi_scanout_options_init(&opts);
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
	image = rdp_processor_scanout(&processor, &opts, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	index = vulkan->get_sync_index(vulkan->handle);

	memset(&frame, 0, sizeof(frame));
	if (upscaler_policy.on)
		rdp_processor_upscaler_frame(&processor, &frame);

	/* (recorded and submitted on the command thread, which drives the
	 * device) */
	if (!image)
		rdp_processor_run_on_command_thread(&processor, make_blank_image, &image);
	if (!image)
	{
		drop_targets(&frame);
		rdp_processor_begin_frame_context(&processor);
		return;
	}

	assert(index < num_retro_images);
	hand_over(index, image);

	if (upscaler_policy.on && frame.motion_vectors && frame.depth && frame.translucent)
	{
		/* (the frame's images live as long as its color) */
		upscaler_policy.native_width = image->info.width / processor_factor;
		upscaler_policy.native_height = image->info.height / processor_factor;
		memset(&upscaler_inputs, 0, sizeof(upscaler_inputs));
		upscaler_inputs.flags = frame.reset ? RETRO_HW_UPSCALER_RESET : 0;
		upscaler_image(&upscaler_inputs.motion_vectors, frame.motion_vectors);
		upscaler_image(&upscaler_inputs.depth, frame.depth);
		upscaler_image(&upscaler_inputs.transparency_mask, frame.translucent);
		upscaler_inputs.jitter_x = frame.jitter_x;
		upscaler_inputs.jitter_y = frame.jitter_y;
		upscaler_inputs.mv_scale_x = 1.0f;
		upscaler_inputs.mv_scale_y = 1.0f;
		upscaler_inputs.pre_exposure = 1.0f;
		upscaler_inputs.exposure_scale = 1.0f;
		hold(&upscaler_handles[3 * index], frame.motion_vectors);
		hold(&upscaler_handles[3 * index + 1], frame.depth);
		hold(&upscaler_handles[3 * index + 2], frame.translucent);
		memset(&frame, 0, sizeof(frame));
		upscaler_iface->set_inputs(upscaler_iface->handle, &upscaler_inputs);
	}
	else if (upscaler_iface)
	{
		hold(&upscaler_handles[3 * index], NULL);
		hold(&upscaler_handles[3 * index + 1], NULL);
		hold(&upscaler_handles[3 * index + 2], NULL);
	}
	drop_targets(&frame);

	vulkan->set_image(vulkan->handle, &retro_images[index], 0, NULL, VK_QUEUE_FAMILY_IGNORED);
	frame_width = image->info.width;
	frame_height = image->info.height;
	hold(&retro_image_handles[index], image);

	rdp_processor_set_quirks(&processor, quirks());
	rdp_processor_begin_frame_context(&processor);
}

static void parallel_complete_frame(void)
{
	if (device_supported)
		complete_frame();
}

/* ---- the device, made through the frontend ---- */

/* vkCreateDevice through the frontend (negotiation v2), which adds the
 * extensions and features it wants, e.g. for its low-latency modes */
typedef struct frontend_device_factory
{
	retro_vulkan_create_device_wrapper_t wrapper;
	void *opaque;
} frontend_device_factory_t;

static VkDevice frontend_create_device(void *user, VkPhysicalDevice gpu, const VkDeviceCreateInfo *info)
{
	frontend_device_factory_t *f = (frontend_device_factory_t *)user;
	return f->wrapper(gpu, f->opaque, info);
}

static bool create_device(struct retro_vulkan_context *frontend_context, VkInstance instance, VkPhysicalDevice gpu,
		VkSurfaceKHR surface, PFN_vkGetInstanceProcAddr get_instance_proc_addr,
		const char **required_device_extensions, unsigned num_required_device_extensions,
		const VkPhysicalDeviceFeatures *required_features, granite_create_device_t factory, void *factory_user)
{
	if (!granite_context_init_loader(get_instance_proc_addr))
		return false;

	if (have_context)
		granite_context_destroy(&context);
	memset(&context, 0, sizeof(context));
	have_context = true;
	if (!granite_context_init(&context, instance, gpu, surface, required_device_extensions,
			num_required_device_extensions, required_features, factory, factory_user))
	{
		granite_context_destroy(&context);
		have_context = false;
		return false;
	}

	frontend_context->gpu = context.gpu;
	frontend_context->device = context.device;
	frontend_context->queue = context.queue_info.queues[GRANITE_QUEUE_GRAPHICS];
	frontend_context->queue_family_index = context.queue_info.family_indices[GRANITE_QUEUE_GRAPHICS];
	frontend_context->presentation_queue = context.queue_info.queues[GRANITE_QUEUE_GRAPHICS];
	frontend_context->presentation_queue_family_index = context.queue_info.family_indices[GRANITE_QUEUE_GRAPHICS];

	/* the frontend owns the device */
	context.owned_device = false;
	return true;
}

bool parallel_create_device(struct retro_vulkan_context *frontend_context, VkInstance instance, VkPhysicalDevice gpu,
		VkSurfaceKHR surface, PFN_vkGetInstanceProcAddr get_instance_proc_addr,
		const char **required_device_extensions, unsigned num_required_device_extensions,
		const char **required_device_layers, unsigned num_required_device_layers,
		const VkPhysicalDeviceFeatures *required_features)
{
	(void)required_device_layers;
	(void)num_required_device_layers;
	return create_device(frontend_context, instance, gpu, surface, get_instance_proc_addr,
			required_device_extensions, num_required_device_extensions, required_features, NULL, NULL);
}

bool parallel_create_device2(struct retro_vulkan_context *frontend_context, VkInstance instance, VkPhysicalDevice gpu,
		VkSurfaceKHR surface, PFN_vkGetInstanceProcAddr get_instance_proc_addr,
		retro_vulkan_create_device_wrapper_t create_device_wrapper, void *opaque)
{
	static const VkPhysicalDeviceFeatures no_features;
	frontend_device_factory_t frontend;
	frontend.wrapper = create_device_wrapper;
	frontend.opaque = opaque;
	return create_device(frontend_context, instance, gpu, surface, get_instance_proc_addr, NULL, 0, &no_features,
			frontend_create_device, &frontend);
}

static const VkApplicationInfo parallel_app_info = {
	VK_STRUCTURE_TYPE_APPLICATION_INFO,
	NULL,
	"paraLLEl-RDP",
	0,
	"Granite",
	0,
	VK_API_VERSION_1_1
};

const VkApplicationInfo *parallel_get_application_info(void)
{
	return &parallel_app_info;
}

/* ---- the options ---- */
unsigned parallel_frame_width(void) { return frame_width; }
unsigned parallel_frame_height(void) { return frame_height; }
bool parallel_frame_is_valid(void) { return true; }
void parallel_set_synchronous_rdp(bool enable) { synchronous = enable; }
void parallel_set_divot_filter(bool enable) { divot_filter = enable; }
void parallel_set_gamma_dither(bool enable) { gamma_dither = enable; }
void parallel_set_vi_aa(bool enable) { vi_aa = enable; }
void parallel_set_vi_scale(bool enable) { vi_scale = enable; }
void parallel_set_dither_filter(bool enable) { dither_filter = enable; }
void parallel_set_interlacing(bool enable) { interlacing = enable; }
void parallel_set_upscaling(unsigned factor) { upscaling = factor; }
unsigned parallel_get_upscaling(void) { return upscaling; }
void parallel_set_downscaling_steps(unsigned steps) { downscaling_steps = steps; }
void parallel_set_native_texture_lod(bool enable) { native_texture_lod = enable; }
void parallel_set_native_tex_rect(bool enable) { native_tex_rect = enable; }
void parallel_set_super_sampled_read_back(bool enable) { super_sampled_read_back = enable; }
void parallel_set_super_sampled_dither(bool enable) { super_sampled_dither = enable; }
void parallel_set_overscan_crop(unsigned pixels) { overscan = pixels; }

/* ---- the graphics plugin's entry points ---- */
void parallelSetRenderingCallback(void (*callback)(int))
{
}

int parallelInitiateGFX(GFX_INFO Gfx_Info)
{
	return true;
}

void parallelProcessDList(void)
{
	/* RSP-HLE handed us a GFX task: walk the display list through the shared
	 * HLE command emitter and rasterize the synthesized RDP FIFO. */
	parallel_hle_process_dlist();
}

void parallelProcessRDPList(void)
{
	parallel_process_commands();
}

void parallelRomClosed(void)
{
}

int parallelRomOpen(void)
{
	return 1;
}

void parallelUpdateScreen(void)
{
	parallel_complete_frame();
	retro_return(true);
}

void parallelShowCFB(void)
{
	parallelUpdateScreen();
}

void parallelViStatusChanged(void)
{
}

void parallelViWidthChanged(void)
{
}

void parallelFBWrite(unsigned int addr, unsigned int size)
{
}

void parallelFBRead(unsigned int addr)
{
}

void parallelFBGetFrameBufferInfo(void *pinfo)
{
}

