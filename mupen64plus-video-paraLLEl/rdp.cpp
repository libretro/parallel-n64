#include "rdp.hpp"
#include "timeline_trace_file.hpp"
#include "Gfx #1.3.h"
#include "parallel.h"
#include "z64.h"
#include <assert.h>
#include <math.h>
#include <string.h>

using namespace Vulkan;
using namespace std;

extern retro_log_printf_t log_cb;
extern retro_environment_t environ_cb;

namespace RDP
{
const struct retro_hw_render_interface_vulkan *vulkan;

static int cmd_cur;
static int cmd_ptr;
static uint32_t cmd_data[0x00040000 >> 2];
static uint64_t pending_timeline_value, timeline_value;

/* HLE command-FIFO redirect.  The HLE emitter synthesizes an RDP command list
 * into a host buffer (out of guest RDRAM, so Expansion Pak games are not
 * corrupted) and reports it as occupying [hle_cmd_base, hle_cmd_base+len).
 * When active, process_commands() ingests the command words from this host
 * buffer instead of guest DRAM; the address range is otherwise identical, so
 * texture/image reads still hit real RDRAM.  Cleared after each submit, so the
 * LLE path (real games feeding the RDP) is completely unaffected. */
static const uint8_t *hle_cmd_buf = nullptr;
static uint32_t hle_cmd_base = 0;
static uint32_t hle_cmd_len = 0;

static unique_ptr<Util::TimelineTraceFile> timeline_trace_file;
static unique_ptr<CommandProcessor> frontend;
static unique_ptr<Device> device;
static unique_ptr<Context> context;
static QueryPoolHandle begin_ts, end_ts;

/* Set true only when init() fully succeeds (including the device meeting
 * parallel-RDP's feature requirements). The frame-driving entry points
 * (begin_frame / process_commands / complete_frame) hard no-op when this is
 * false, so that a failed/unsupported init -- e.g. a MoltenVK build lacking the
 * 8/16-bit storage parallel-RDP needs -- degrades to "nothing rendered" instead
 * of dereferencing a half-initialized Vulkan device and crashing. */
static bool device_supported = false;

static vector<retro_vulkan_image> retro_images;
static vector<ImageHandle> retro_image_handles;

/* RDRAM as the command processor imports it */
static uintptr_t rdram_base;
static uintptr_t rdram_offset;
/* The upscaling factor the command processor renders at */
static unsigned processor_factor = 1;

/* The frontend's temporal upscaler (RETRO_ENVIRONMENT_GET_HW_RENDER_UPSCALER_INTERFACE) */
static const struct retro_hw_render_upscaler_interface *upscaler_iface;
static struct retro_hw_upscaler_request upscaler_request;
static struct retro_hw_upscaler_inputs upscaler_inputs;
/* Per sync index: motion vectors, depth, then the translucency mask */
static vector<ImageHandle> upscaler_handles;
static bool upscaler_on;
static bool upscaler_reset_pending = true;
/* The size of the framebuffer last shown, in native pixels, and the
 * size the factor was chosen for */
static unsigned upscaler_native_width = 320;
static unsigned upscaler_native_height = 240;
static unsigned upscaler_picked_width;
static unsigned upscaler_picked_height;

/* The last of each state command, to give a command processor made anew
 * the state the RDP had: set_tile and set_tile_size per tile */
static uint32_t state_words[64][2];
static bool state_valid[64];
static uint32_t tile_state_words[2][8][2];
static bool tile_state_valid[2][8];

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

static const unsigned cmd_len_lut[64] = {
	1, 1, 1, 1, 1, 1, 1, 1, 4, 6, 12, 14, 12, 14, 20, 22,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  1,  1,  1,  1,  1,
	1, 1, 1, 1, 2, 2, 1, 1, 1, 1, 1,  1,  1,  1,  1,  1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  1,  1,  1,  1,  1,
};

void set_hle_cmd_buffer(const uint8_t *buf, uint32_t base_byte_addr, uint32_t len_bytes)
{
	hle_cmd_buf = buf;
	hle_cmd_base = base_byte_addr;
	hle_cmd_len = len_bytes;
}

void process_commands()
{
	if (!device_supported)
		return;

	const uint32_t DP_CURRENT = *GET_GFX_INFO(DPC_CURRENT_REG) & 0x00FFFFF8;
	const uint32_t DP_END = *GET_GFX_INFO(DPC_END_REG) & 0x00FFFFF8;

	int length = DP_END - DP_CURRENT;
	if (length <= 0)
		return;

	length = unsigned(length) >> 3;
	if ((cmd_ptr + length) & ~(0x0003FFFF >> 3))
		return;

	uint32_t offset = DP_CURRENT;
	if (*GET_GFX_INFO(DPC_STATUS_REG) & DP_STATUS_XBUS_DMA)
	{
		do
		{
			offset &= 0xFF8;
			cmd_data[2 * cmd_ptr + 0] = *reinterpret_cast<const uint32_t *>(SP_DMEM + offset);
			cmd_data[2 * cmd_ptr + 1] = *reinterpret_cast<const uint32_t *>(SP_DMEM + offset + 4);
			offset += sizeof(uint64_t);
			cmd_ptr++;
		} while (--length > 0);
	}
	else
	{
		if (DP_END > 0x7ffffff || DP_CURRENT > 0x7ffffff)
		{
			return;
		}
		else
		{
			/* Source the command words from the HLE host buffer when a
			 * synthesized FIFO is active, else from guest RDRAM.  Chosen
			 * once here, not per word: a base-pointer swap, no hot-path cost. */
			bool from_hle = (hle_cmd_buf != nullptr
			                 && DP_CURRENT >= hle_cmd_base
			                 && (DP_CURRENT - hle_cmd_base) < hle_cmd_len);
			do
			{
				const uint8_t *p;
				if (from_hle)
				{
					uint32_t rel = offset - hle_cmd_base;
					if (rel + 8 > hle_cmd_len)
						break;
					p = hle_cmd_buf + rel;
				}
				else
				{
					offset &= 0xFFFFF8;
					p = DRAM + offset;
				}
				cmd_data[2 * cmd_ptr + 0] = *reinterpret_cast<const uint32_t *>(p);
				cmd_data[2 * cmd_ptr + 1] = *reinterpret_cast<const uint32_t *>(p + 4);
				offset += sizeof(uint64_t);
				cmd_ptr++;
			} while (--length > 0);
		}
	}

	while (cmd_cur - cmd_ptr < 0)
	{
		uint32_t w1 = cmd_data[2 * cmd_cur];
		uint32_t command = (w1 >> 24) & 63;
		int cmd_length = cmd_len_lut[command];

		if (cmd_ptr - cmd_cur - cmd_length < 0)
		{
			*GET_GFX_INFO(DPC_CURRENT_REG) = *GET_GFX_INFO(DPC_END_REG);
			return;
		}

		if (command >= 8 && frontend)
			frontend->enqueue_command(cmd_length * 2, &cmd_data[2 * cmd_cur]);

		if (command == 0x35 || command == 0x32)
		{
			unsigned set = command == 0x35 ? 0 : 1;
			unsigned tile = (cmd_data[2 * cmd_cur + 1] >> 24) & 7;
			tile_state_words[set][tile][0] = cmd_data[2 * cmd_cur];
			tile_state_words[set][tile][1] = cmd_data[2 * cmd_cur + 1];
			tile_state_valid[set][tile] = true;
		}
		else if (command >= 0x2a && command != 0x30 && command != 0x33 && command != 0x34 && command != 0x36)
		{
			state_words[command][0] = cmd_data[2 * cmd_cur];
			state_words[command][1] = cmd_data[2 * cmd_cur + 1];
			state_valid[command] = true;
		}

		if (RDP::Op(command) == RDP::Op::SyncFull)
		{
			// For synchronous RDP:
			if (synchronous && frontend)
				frontend->wait_for_timeline(frontend->signal_timeline());
			*gfx_info.MI_INTR_REG |= DP_INTERRUPT;
			gfx_info.CheckInterrupts();
		}

		cmd_cur += cmd_length;
	}

	cmd_ptr = 0;
	cmd_cur = 0;
	*GET_GFX_INFO(DPC_CURRENT_REG) = *GET_GFX_INFO(DPC_END_REG);
}

static QueryPoolHandle refresh_begin_ts;

void profile_refresh_begin()
{
	if (device)
		refresh_begin_ts = device->write_calibrated_timestamp();
}

void profile_refresh_end()
{
	if (device)
	{
		device->register_time_interval("Emulation", refresh_begin_ts, device->write_calibrated_timestamp(), "refresh");
		refresh_begin_ts.reset();
	}
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

	if (num_frames != retro_images.size())
	{
		retro_images.resize(num_frames);
		retro_image_handles.resize(num_frames);
	}

	if (upscaler_handles.size() != 3 * num_frames)
		upscaler_handles.resize(3 * num_frames);

	vulkan->wait_sync_index(vulkan->handle);
	if (!begin_ts)
		begin_ts = device->write_calibrated_timestamp();

	upscaler_poll();

	//frontend->wait_for_timeline(pending_timeline_value);
	//pending_timeline_value = timeline_value;
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
	if (factor > 1 && super_sampled_read_back && !upscaler_on)
	{
		flags |= COMMAND_PROCESSOR_FLAG_SUPER_SAMPLED_READ_BACK_BIT;
		log_cb(RETRO_LOG_INFO, "Using super-sampled read-back!\n");
		if (super_sampled_dither)
			flags |= COMMAND_PROCESSOR_FLAG_SUPER_SAMPLED_DITHER_BIT;
	}

	if (upscaler_on)
		flags |= COMMAND_PROCESSOR_FLAG_TEMPORAL_UPSCALER_BIT;

	/* (the old one goes first: both at once may not fit) */
	frontend.reset();
	frontend.reset(new CommandProcessor(*device, reinterpret_cast<void *>(rdram_base),
				rdram_offset, 8 * 1024 * 1024, 4 * 1024 * 1024, flags));

	if (!frontend->device_is_supported())
	{
		log_cb(RETRO_LOG_ERROR, "This device probably does not support 16-bit storage. Make sure you're using up-to-date drivers!\n");
		frontend.reset();
		return false;
	}

	quirks.set_native_texture_lod(native_texture_lod);
	quirks.set_native_resolution_tex_rect(native_tex_rect);
	frontend->set_quirks(quirks);
	processor_factor = factor;
	return true;
}

/* A command processor made while the game runs starts from the state the
 * last one had; what TMEM held is lost */
static void replay_state()
{
	static const unsigned char order[] = {
		0x3f, 0x3e, 0x3d, 0x2f, 0x3c, 0x2d, 0x2c, 0x2a, 0x2b,
		0x37, 0x38, 0x39, 0x3a, 0x3b, 0x2e
	};
	unsigned i, set, tile;

	for (i = 0; i < sizeof(order); i++)
		if (state_valid[order[i]])
			frontend->enqueue_command(2, state_words[order[i]]);
	for (set = 0; set < 2; set++)
		for (tile = 0; tile < 8; tile++)
			if (tile_state_valid[set][tile])
				frontend->enqueue_command(2, tile_state_words[set][tile]);
}

/* The factor for the frames shown, rdp_upscaler_factor(): the one
 * chosen stands while the frames stay the size it was chosen for */
static unsigned upscaler_pick_factor(const struct retro_hw_upscaler_request *req)
{
	rdp_upscaler_range_t range;
	unsigned pick;
	range.want_width = req->render_width;
	range.min_width = req->min_render_width;
	range.min_height = req->min_render_height;
	range.max_width = req->max_render_width;
	range.max_height = req->max_render_height;
	pick = rdp_upscaler_factor(&range, upscaler_native_width, upscaler_native_height,
			upscaler_on ? processor_factor : 0, upscaler_picked_width, upscaler_picked_height);
	if (pick)
	{
		upscaler_picked_width = upscaler_native_width;
		upscaler_picked_height = upscaler_native_height;
	}
	return pick;
}

/* Once a frame, before it runs: whether it is drawn for the upscaler and
 * at which factor. The factor stays while its frames still fit. */
/* Says what the upscaler asked for and what came of it, when that
 * changes: the frontend can only tell that frames came without inputs */
static void upscaler_log(bool requested, bool on, unsigned factor)
{
	static unsigned said = ~0u;
	unsigned now = unsigned(requested) | (unsigned(on) << 1) | (factor << 2);
	const struct retro_hw_upscaler_request *r = &upscaler_request;

	if (now == said)
		return;
	said = now;
	if (!requested)
		log_cb(RETRO_LOG_INFO, "Temporal upscaler: no request, rendering as usual.\n");
	else if (!on)
		log_cb(RETRO_LOG_WARN, "Temporal upscaler: asks for %ux%u (%ux%u to %ux%u) for %ux%u; "
				"no factor of %ux%u fits, rendering as usual.\n",
				r->render_width, r->render_height, r->min_render_width, r->min_render_height,
				r->max_render_width, r->max_render_height, r->output_width, r->output_height,
				upscaler_native_width, upscaler_native_height);
	else
		log_cb(RETRO_LOG_INFO, "Temporal upscaler: drawing at %ux, %ux%u, for %ux%u "
				"(asks for %ux%u, %ux%u to %ux%u).\n",
				factor, upscaler_native_width * factor, upscaler_native_height * factor,
				r->output_width, r->output_height, r->render_width, r->render_height,
				r->min_render_width, r->min_render_height,
				r->max_render_width, r->max_render_height);
}

static void upscaler_poll()
{
	unsigned factor = upscaling;
	bool on = false;
	bool requested = false;
	bool remake;

	if (upscaler_iface && upscaler_iface->get_request(upscaler_iface->handle, &upscaler_request))
	{
		unsigned pick = upscaler_pick_factor(&upscaler_request);
		requested = true;
		if (pick)
		{
			factor = pick;
			on = true;
		}
	}

	/* (super-sampled read-back is off while the upscaler is on) */
	remake = factor != processor_factor
		|| (on != upscaler_on && super_sampled_read_back && factor > 1);
	if (on != upscaler_on)
		upscaler_reset_pending = true;
	upscaler_on = on;

	if (remake)
	{
		if (frontend)
			frontend->idle();
		if (create_processor(factor))
			replay_state();
		else if (!create_processor(1))
			device_supported = false;
		upscaler_reset_pending = true;
	}
	if (upscaler_iface)
		upscaler_log(requested, on, processor_factor);
}

void set_upscaler_interface(const struct retro_hw_render_upscaler_interface *iface)
{
	upscaler_iface = iface;
	upscaler_reset_pending = true;
	/* before the first frame: what it draws at boot is drawn for it */
	if (device_supported)
		upscaler_poll();
}

bool upscaler_active()
{
	return upscaler_on;
}

void upscaler_reset()
{
	upscaler_reset_pending = true;
}

/* Halton (2, 3) over the frames the ratio of the sizes wants: at least
 * 8, at most 64 */
static unsigned upscaler_jitter_phase()
{
	float ratio;
	unsigned phase;
	if (!upscaler_native_width || !processor_factor)
		return 8;
	ratio = float(upscaler_request.output_width) / float(upscaler_native_width * processor_factor);
	phase = unsigned(ceilf(8.0f * ratio * ratio));
	return phase < 8 ? 8 : phase > 64 ? 64 : phase;
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

	retro_images.resize(num_frames);
	retro_image_handles.resize(num_frames);
	upscaler_handles.resize(3 * num_frames);

	device.reset(new Device);
	device->set_context(*context);
	device->init_frame_contexts(num_sync_frames);
	log_cb(RETRO_LOG_INFO, "Using %u sync frames for parallel-RDP.\n", num_sync_frames);
	device->set_queue_lock(
			[]() { vulkan->lock_queue(vulkan->handle); },
			[]() { vulkan->unlock_queue(vulkan->handle); });

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
	memset(state_valid, 0, sizeof(state_valid));
	memset(tile_state_valid, 0, sizeof(tile_state_valid));
	upscaler_on = false;
	upscaler_reset_pending = true;
	if (!create_processor(upscaling))
		return false;

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
	begin_ts.reset();
	end_ts.reset();
	retro_image_handles.clear();
	retro_images.clear();
	upscaler_handles.clear();
	upscaler_iface = nullptr;
	upscaler_on = false;
	frontend.reset();
	device.reset();
	context.reset();
	timeline_trace_file.reset();
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

	auto info = Vulkan::ImageCreateInfo::immutable_2d_image(50, 16, VK_FORMAT_R8G8B8A8_UNORM, false);
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
	assert(index < retro_images.size());

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
	opts.upscaler.enable = upscaler_on;
	opts.upscaler.jitter_phase = upscaler_jitter_phase();
	opts.upscaler.reset = upscaler_reset_pending;
	upscaler_reset_pending = false;
	auto image = frontend->scanout(opts);
	unsigned index = vulkan->get_sync_index(vulkan->handle);
	UpscalerFrame upscaler_frame = {};

	if (upscaler_on)
		frontend->get_upscaler_frame(upscaler_frame);

	if (!image)
	{
		// Recorded and submitted on the command thread, which drives the
		// Vulkan device.
		frontend->run_on_command_thread(make_blank_image, &image);
	}

	assert(index < retro_images.size());

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

	if (upscaler_on && upscaler_frame.motion_vectors && upscaler_frame.depth && upscaler_frame.translucent)
	{
		/* (the frame's images live as long as its color) */
		upscaler_native_width = image->get_width() / processor_factor;
		upscaler_native_height = image->get_height() / processor_factor;
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

	end_ts = device->write_calibrated_timestamp();
	device->register_time_interval("Emulation", begin_ts, end_ts, "frame");
	begin_ts.reset();
	end_ts.reset();

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
struct FrontendDeviceFactory : Vulkan::DeviceFactory
{
	retro_vulkan_create_device_wrapper_t wrapper;
	void *opaque;

	VkDevice create_device(VkPhysicalDevice gpu, const VkDeviceCreateInfo *info) override
	{
		return wrapper(gpu, opaque, info);
	}
};

static bool create_device(struct retro_vulkan_context *frontend_context, VkInstance instance, VkPhysicalDevice gpu,
                          VkSurfaceKHR surface, PFN_vkGetInstanceProcAddr get_instance_proc_addr,
                          const char **required_device_extensions, unsigned num_required_device_extensions,
                          const VkPhysicalDeviceFeatures *required_features, Vulkan::DeviceFactory *factory)
{
	if (!Vulkan::Context::init_loader(get_instance_proc_addr))
		return false;

	::RDP::context.reset(new Vulkan::Context);

	::Vulkan::Context::SystemHandles handles;

	if (const char *env = getenv("PARALLEL_RDP_TIMELINE_TRACE"))
	{
		::RDP::timeline_trace_file.reset(new Util::TimelineTraceFile(env));
		handles.timeline_trace_file = ::RDP::timeline_trace_file.get();
	}

	::RDP::context->set_system_handles(handles);
	::RDP::context->set_device_factory(factory);

	bool ok = ::RDP::context->init_device_from_instance(
			instance, gpu, surface, required_device_extensions, num_required_device_extensions,
			required_features, Vulkan::CONTEXT_CREATION_DISABLE_BINDLESS_BIT);
	::RDP::context->set_device_factory(nullptr);
	if (!ok)
	{
		::RDP::context.reset();
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
	::RDP::FrontendDeviceFactory factory;
	factory.wrapper = create_device_wrapper;
	factory.opaque = opaque;
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

