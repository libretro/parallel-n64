/* parallel-RDP's video interface (parallel-rdp/video_interface.c), on a
 * real device (vulkan/device.c) over a stand-in device table.
 *
 * A scanout reads the framebuffer the VI registers show out of RDRAM on
 * the async queue (extract_vram), and on the graphics queue, waiting for
 * that, runs the fetch pass, the divot pass when the register asks for
 * it, and the scale pass; what it hands out is the scale pass's image
 * in the layout asked for, 640 wide and a field high times the scaling
 * factor, less the crop. Upscaled, the downscales and the deinterlace
 * are folded into the scale pass; without the fold they are blits and a
 * pass of their own, and give an image of the same size. The frame
 * before is blended in when asked, shown again while the registers are
 * invalid for a few frames, and dropped when the VI shows nothing. Its
 * images come from a pool: scanning out the same frame again makes
 * none. The temporal upscaler's scanout is one compute pass at the
 * framebuffer's size. At load every pipeline a scanout can need is
 * handed over. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <boolean.h>

#include "video_interface.h"
#include "pipeline_record.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_rdp_video_interface: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

#include "vk_test_device.h"

/* ---- the VI ---- */
static rdp_vi_t vi;
static rdp_shader_bank_t bank;
static granite_buffer_t *rdram, *hidden_rdram, *upscaled_rdram, *upscaled_hidden_rdram;
static unsigned updates, update_addr, update_pixels, update_size_log2;

static void update_upscaled(void *user, granite_command_buffer_t *cb, unsigned addr, unsigned pixels,
		unsigned pixel_size_log2)
{
	CHECK(user == (void *)&updates && cb, "the upscaled source is asked with its userdata");
	updates++;
	update_addr = addr;
	update_pixels = pixels;
	update_size_log2 = pixel_size_log2;
}

static unsigned upscaled_asked, upscaled_hidden_asked;

static const granite_buffer_t *get_upscaled(void *user)
{
	(void)user;
	upscaled_asked++;
	return upscaled_rdram;
}

static const granite_buffer_t *get_upscaled_hidden(void *user)
{
	(void)user;
	upscaled_hidden_asked++;
	return upscaled_hidden_rdram;
}

static int no_defines(void *user, const char *name, const char *define)
{
	(void)user; (void)name; (void)define;
	return 0;
}

static granite_buffer_t *storage_buffer(VkDeviceSize size)
{
	granite_buffer_info_t info;
	memset(&info, 0, sizeof(info));
	info.domain = GRANITE_BUFFER_DOMAIN_DEVICE;
	info.size = size;
	info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	return granite_device_create_buffer(&dev, &info, NULL);
}

static uint32_t pair(uint32_t hi, uint32_t lo)
{
	return (hi << 16) | lo;
}

/* a 320x240 NTSC picture filling the visible area */
static void ntsc_320x240(void)
{
	memset(vi.registers, 0, sizeof(vi.registers));
	vi.registers[RDP_VI_REGISTER_CONTROL] = VI_CONTROL_TYPE_RGBA5551_BIT | VI_CONTROL_AA_MODE_RESAMP_REPLICATE_BIT;
	vi.registers[RDP_VI_REGISTER_ORIGIN] = 0x100000;
	vi.registers[RDP_VI_REGISTER_WIDTH] = 320;
	vi.registers[RDP_VI_REGISTER_V_SYNC] = VI_V_SYNC_NTSC;
	vi.registers[RDP_VI_REGISTER_H_START] = pair(VI_H_OFFSET_NTSC, VI_H_OFFSET_NTSC + 640);
	vi.registers[RDP_VI_REGISTER_V_START] = pair(VI_V_OFFSET_NTSC, VI_V_OFFSET_NTSC + 480);
	vi.registers[RDP_VI_REGISTER_X_SCALE] = pair(0, 512);
	vi.registers[RDP_VI_REGISTER_Y_SCALE] = pair(0, 1024);
}

static const VkImageCreateInfo *info_of(const granite_image_t *image)
{
	int n = image_index(image->image);
	return n < 0 ? &image_infos[MAX_IMAGES - 1] : &image_infos[n];
}

static VkImageLayout layout_of(const granite_image_t *image)
{
	int n = image_index(image->image);
	return n < 0 ? VK_IMAGE_LAYOUT_MAX_ENUM : image_layouts[n];
}

static int32_t constant_at(int ev, unsigned word)
{
	int32_t value;
	memcpy(&value, events[ev].constants + word * 4, sizeof(value));
	return value;
}

/* (what the frame before left is submitted with it) */
static unsigned frame_submits, frame_waits;

static void new_frame(void)
{
	granite_device_next_frame_context(&dev);
	rdp_vi_begin_frame_context(&vi);
	frame_submits = submits_made;
	frame_waits = submit_waits;
	num_events = 0;
	memset(events, 0, sizeof(events));
}

/* a scanout on the frame after the last */
static granite_image_t *scan(const rdp_vi_scanout_options_t *options, unsigned scale, VkImageLayout layout)
{
	new_frame();
	return rdp_vi_scanout(&vi, layout, options, scale);
}

static void drop(granite_image_t *image)
{
	if (image)
		granite_image_unref(image);
}

/* the pool's own handle to the @nth image made */
static const granite_image_t *pooled(unsigned nth)
{
	unsigned i;
	for (i = 0; i < vi.image_pool.count; i++)
		if (vi.image_pool.entries[i].image && vi.image_pool.entries[i].image->image == image_handles[nth])
			return vi.image_pool.entries[i].image;
	return NULL;
}

/* ---- at load ---- */
static unsigned known_graphics, known_compute, known_bad;

static void add_known(void *ctx, granite_command_buffer_t *cb, uint32_t kind)
{
	if (ctx != (void *)&known_bad || !cb->pipeline_state.program)
		known_bad++;
	if (kind == PIPELINE_RECORD_GRAPHICS)
	{
		known_graphics++;
		if (!cb->actual_render_pass)
			known_bad++;
	}
	else if (kind == PIPELINE_RECORD_COMPUTE)
		known_compute++;
	else
		known_bad++;
}

static void test_known_pipelines(void)
{
	unsigned before = num_pipelines;

	new_frame();
	rdp_vi_queue_known_pipelines(&vi, 1, false, add_known, &known_bad);
	/* the fetch pass by AA and dither filter and the divot pass, with and
	 * without the fetch bug; the scale pass by gamma, gamma dither, scale
	 * and AA, with and without the bug and the fold; the blend and the
	 * deinterlace; extract_vram by 16 or 32 bits and AA */
	CHECK(known_graphics == 2 * (4 + 1) + 2 * 2 * 16 + 2 && known_compute == 4 && known_bad == 0,
			"at load, every pipeline a scanout at 1x can need");
	CHECK(count_events(EV_PASS) == 3 && count_events(EV_DRAW) == 0 && count_events(EV_DISPATCH) == 0
			&& num_pipelines == before, "set up on 1x1 targets, without building or drawing anything");
	granite_device_flush_frame(&dev);
	CHECK(submits_made == frame_submits + 1, "its command buffer is handed back");

	known_graphics = known_compute = 0;
	new_frame();
	rdp_vi_queue_known_pipelines(&vi, 4, true, add_known, &known_bad);
	/* no fetch bug when upscaled; extract_vram and the upscaler's color
	 * pass at 1x and at 4x; the upscaler's motion pass */
	CHECK(known_graphics == (4 + 1) + 2 * 16 + 2 + 4 && known_compute == 2 * 4 + 2 * 4 && known_bad == 0,
			"and at 4x with the temporal upscaler");
}

/* ---- a frame ---- */
/* the pipeline the plain frame's fetch pass drew with */
static unsigned plain_fetch, plain_divot;

static void test_plain_frame(void)
{
	rdp_vi_scanout_options_t options;
	granite_image_t *image, *again;
	unsigned before_images = num_images;
	unsigned before_made, i;
	int ev;

	rdp_vi_scanout_options_init(&options);
	CHECK(options.upscale_deinterlacing && options.vi.aa && options.vi.scale && options.vi.serrate
			&& options.vi.dither_filter && options.vi.divot_filter && options.vi.gamma_dither
			&& options.upscaler.jitter_phase == 8 && !options.upscaler.enable && !options.crop_rect.enable
			&& !options.blend_previous_frame && !options.persist_frame_on_invalid_input
			&& options.crop_overscan_pixels == 0 && options.downscale_steps == 0, "the options' defaults");

	ntsc_320x240();
	image = scan(&options, 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	CHECK(image && info_of(image)->extent.width == 640 && info_of(image)->extent.height == 240
			&& info_of(image)->format == VK_FORMAT_R8G8B8A8_UNORM
			&& layout_of(image) == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			"a 320x240 picture scans out as 640x240, in the layout asked for");
	CHECK(vi.prev_scanout_image == image && vi.prev_image_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			&& vi.frame_count == 1, "kept as the frame before");
	CHECK(submits_made == frame_submits + 1 && submit_waits == frame_waits,
			"the framebuffer is read on the async queue, submitted at once");
	granite_device_flush_frame(&dev);
	CHECK(submits_made == frame_submits + 2 && submit_waits == frame_waits + 1,
			"and the graphics queue's submission waits for it");
	CHECK(num_images == before_images + 3, "three images: the framebuffer's, the fetch pass's, the scale pass's");
	CHECK(updates == 0 && upscaled_asked == 0 && upscaled_hidden_asked == 0, "at 1x the upscaled source is not asked");

	/* extract_vram: a 2 pixel border at the sides and above, 3 below */
	ev = find_event(EV_DISPATCH, 0);
	CHECK(count_events(EV_DISPATCH) == 1 && events[ev].a == (326 + 15) / 16 && events[ev].b == (245 + 7) / 8
			&& events[ev].constants_size == 24 && constant_at(ev, 0) == 0x100000 >> 1 && constant_at(ev, 1) == 320
			&& constant_at(ev, 2) == -2 && constant_at(ev, 3) == -2 && constant_at(ev, 4) == 326
			&& constant_at(ev, 5) == 245, "the framebuffer is read with its border, 16x8 pixels a group");
	i = (unsigned)(events[ev].c / 16) - 1;
	CHECK(i < MAX_PIPELINES && pipelines[i].compute && pipelines[i].spec_count == 3 && pipelines[i].spec[0] == 8 << 20
			&& pipelines[i].spec[1] == VI_CONTROL_TYPE_RGBA5551_BIT && pipelines[i].spec[2] == 0,
			"by extract_vram for RDRAM's size, 16 bits a pixel, at 1x");
	CHECK(image_infos[before_images].extent.width == 326 && image_infos[before_images].extent.height == 245
			&& image_infos[before_images].format == VK_FORMAT_R8G8B8A8_UINT
			&& image_infos[before_images].usage == (VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
			&& image_infos[before_images].sharingMode == VK_SHARING_MODE_EXCLUSIVE, "into a storage image of that size");
	CHECK(pooled(before_images) && pooled(before_images)->general_layout
			&& image_layouts[before_images] == VK_IMAGE_LAYOUT_GENERAL, "which stays in the general layout");

	/* the fetch pass, then the scale pass: no divot pass */
	CHECK(count_events(EV_PASS) == 2 && count_events(EV_DRAW) == 2 && count_events(EV_BLIT) == 0,
			"then the fetch pass and the scale pass");
	ev = find_event(EV_PASS, 0);
	CHECK(events[ev].a == 1 && events[ev].b == (int)before_images + 1
			&& image_infos[before_images + 1].extent.width == 323 && image_infos[before_images + 1].extent.height == 242
			&& image_infos[before_images + 1].arrayLayers == 1, "the fetch pass draws one layer of 323x242");
	ev = find_event(EV_DRAW, 0);
	CHECK(constant_at(ev, 0) == 2 && constant_at(ev, 1) == 2, "from two pixels into the framebuffer's image");
	plain_fetch = (unsigned)events[ev].a - 1;
	CHECK(image_layouts[before_images + 1] == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			&& pooled(before_images + 1) && !pooled(before_images + 1)->general_layout,
			"and is left to be read by the scale pass");
	ev = find_event(EV_PASS, 1);
	CHECK(events[ev].a == 1 && events[ev].b == image_index(image->image) && events[ev].c == 1,
			"the scale pass clears and draws the image handed out");
	ev = find_event(EV_DRAW, 1);
	CHECK(events[ev].scissor.offset.x == 0 && events[ev].scissor.offset.y == 0
			&& events[ev].scissor.extent.width == 640 && events[ev].scissor.extent.height == 240
			&& events[ev].constants_size == 36 && constant_at(ev, 0) == 0 && constant_at(ev, 1) == 0
			&& constant_at(ev, 2) == 0 && constant_at(ev, 3) == 1024 && constant_at(ev, 4) == 0
			&& constant_at(ev, 8) == 0, "over the picture's rectangle, with vi_scale's registers alone");
	i = (unsigned)events[ev].a - 1;
	CHECK(i < MAX_PIPELINES && !pipelines[i].compute && !pipelines[i].blend && pipelines[i].color_attachments == 1,
			"opaque");
	CHECK(bad_layouts == 0, "every image is moved from the layout it is in");
	CHECK(wrote_buffer_view(0, 0, vi.gamma_lut_view) && pooled(before_images)
			&& wrote_texture(0, 0, pooled(before_images), VK_NULL_HANDLE)
			&& wrote_texture(0, 0, pooled(before_images + 1), VK_NULL_HANDLE),
			"the fetch pass reads the framebuffer's image, the scale pass the fetch pass's and the gamma table");

	/* ---- the same frame again ---- */
	before_made = made;
	for (i = 0; i < 6; i++)
	{
		again = scan(&options, 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		CHECK(again && info_of(again)->extent.width == 640, "the same frame again");
		drop(again);
	}
	CHECK(num_images <= before_images + 3 * 3 && vi.frame_count == 7, "its images come from the pool");
	before_images = num_images;
	before_made = made;
	for (i = 0; i < 6; i++)
		drop(scan(&options, 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
	CHECK(num_images == before_images && made == before_made && num_pipelines == 3,
			"once the pool is full a frame makes nothing: no image, view, buffer, framebuffer or pipeline");
	ev = find_event(EV_DRAW, 1);
	CHECK(constant_at(ev, 4) == 12, "the scale pass is told the frame's number");
	drop(image);
}

static void test_divot_and_crop(void)
{
	rdp_vi_scanout_options_t options;
	granite_image_t *image;
	unsigned before = num_images;
	int ev;

	rdp_vi_scanout_options_init(&options);
	ntsc_320x240();
	vi.registers[RDP_VI_REGISTER_CONTROL] = VI_CONTROL_TYPE_RGBA8888_BIT | VI_CONTROL_DIVOT_ENABLE_BIT;
	image = scan(&options, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	CHECK(image && layout_of(image) == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && count_events(EV_PASS) == 3
			&& count_events(EV_DRAW) == 3, "with the divot filter there is a divot pass between the two");
	ev = find_event(EV_DISPATCH, 0);
	CHECK(constant_at(ev, 0) == 0x100000 >> 2 && constant_at(ev, 2) == -3 && constant_at(ev, 4) == 328,
			"which needs a pixel more at each side, of a 32 bit framebuffer");
	plain_divot = (unsigned)events[find_event(EV_DRAW, 1)].a - 1;
	ev = find_event(EV_PASS, 1);
	CHECK(image_infos[events[ev].b].extent.width == 322 && image_infos[events[ev].b].extent.height == 242
			&& image_infos[before + 1].extent.width == 325, "the divot pass draws 322x242 from a fetch pass of 325");
	drop(image);

	/* the filter can be turned off */
	options.vi.divot_filter = false;
	image = scan(&options, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	CHECK(image && count_events(EV_PASS) == 2, "not with the divot filter turned off in the options");
	drop(image);

	/* ---- crop ---- */
	ntsc_320x240();
	rdp_vi_scanout_options_init(&options);
	options.crop_overscan_pixels = 7;
	image = scan(&options, 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	/* 7 lines are 7 * 640 / 240 = 18.67 pixels across */
	CHECK(image && info_of(image)->extent.width == 640 - 2 * 19 && info_of(image)->extent.height == 240 - 2 * 7,
			"an overscan crop keeps the aspect ratio, to the nearest pixel");
	ev = find_event(EV_DRAW, 1);
	CHECK(constant_at(ev, 0) == 19 && constant_at(ev, 1) == 7 && events[ev].scissor.offset.x == 0
			&& events[ev].scissor.extent.width == 640 - 19 && events[ev].scissor.extent.height == 240 - 7,
			"and moves the picture by the crop");
	drop(image);

	options.crop_rect.enable = true;
	options.crop_rect.left = 10;
	options.crop_rect.right = 20;
	options.crop_rect.top = 5;
	options.crop_rect.bottom = 7;
	image = scan(&options, 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	CHECK(image && info_of(image)->extent.width == 610 && info_of(image)->extent.height == 228,
			"a crop rectangle is taken as given, instead");
	drop(image);
	image = scan(&options, 2, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	ev = find_event(EV_DRAW, 1);
	CHECK(image && info_of(image)->extent.width == 1220 && info_of(image)->extent.height == 456
			&& constant_at(ev, 0) == 20 && constant_at(ev, 1) == 10, "in pixels of the picture: twice that at 2x");
	drop(image);
	/* woven, a line of the crop is a line of each field */
	vi.registers[RDP_VI_REGISTER_CONTROL] |= VI_CONTROL_SERRATE_BIT;
	options.upscale_deinterlacing = false;
	image = scan(&options, 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	CHECK(image && info_of(image)->extent.width == 610 && info_of(image)->extent.height == 480 - 2 * 12,
			"and twice as many lines of a woven frame");
	drop(image);
	options.upscale_deinterlacing = true;
	vi.registers[RDP_VI_REGISTER_CONTROL] &= ~(uint32_t)VI_CONTROL_SERRATE_BIT;

	options.crop_rect.left = 400;
	options.crop_rect.right = 300;
	image = scan(&options, 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	CHECK(image && info_of(image)->extent.width == 640 && info_of(image)->extent.height == 240,
			"a crop that leaves nothing is not applied");
	drop(image);
}

/* ---- the fetch bug ---- */
static void test_fetch_bug(void)
{
	rdp_vi_scanout_options_t options;
	granite_image_t *image;
	unsigned pipeline;
	int ev;

	rdp_vi_scanout_options_init(&options);
	ntsc_320x240();
	vi.registers[RDP_VI_REGISTER_CONTROL] |= VI_CONTROL_DIVOT_ENABLE_BIT;
	/* a step of less than a row a line */
	vi.registers[RDP_VI_REGISTER_Y_SCALE] = pair(0, 1023);
	image = scan(&options, 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	CHECK(image && count_events(EV_PASS) == 3, "a frame that needs the fetch bug");
	ev = find_event(EV_PASS, 0);
	CHECK(events[ev].a == 2 && events[ev].two_layers && image_infos[events[ev].b].arrayLayers == 2,
			"its fetch pass draws the two layers of one image");
	pipeline = (unsigned)events[find_event(EV_DRAW, 0)].a - 1;
	CHECK(pipeline < MAX_PIPELINES && plain_fetch < MAX_PIPELINES && pipelines[pipeline].color_attachments == 2
			&& pipelines[pipeline].fragment != pipelines[plain_fetch].fragment, "with the bug's shader");
	ev = find_event(EV_PASS, 1);
	pipeline = (unsigned)events[find_event(EV_DRAW, 1)].a - 1;
	CHECK(events[ev].a == 2 && events[ev].two_layers && image_infos[events[ev].b].arrayLayers == 2
			&& pipeline < MAX_PIPELINES && plain_divot < MAX_PIPELINES
			&& pipelines[pipeline].fragment != pipelines[plain_divot].fragment, "and so does its divot pass");
	ev = find_event(EV_PASS, 2);
	pipeline = (unsigned)events[find_event(EV_DRAW, 2)].a - 1;
	CHECK(events[ev].a == 1 && pipeline < MAX_PIPELINES && pipelines[pipeline].spec[2] == 1,
			"which the scale pass is built to read");
	drop(image);

	updates = 0;
	image = scan(&options, 2, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	CHECK(updates == 1, "at 2x the upscaled RDRAM is read");
	CHECK(image && events[find_event(EV_PASS, 0)].a == 1 && image_infos[events[find_event(EV_PASS, 0)].b].arrayLayers == 1,
			"upscaled there is no fetch bug");
	drop(image);
}

static void test_nothing_to_show(void)
{
	rdp_vi_scanout_options_t options;
	granite_image_t *image, *shown;

	rdp_vi_scanout_options_init(&options);
	ntsc_320x240();
	shown = scan(&options, 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	CHECK(shown != NULL, "a frame");

	/* ---- invalid registers ---- */
	vi.registers[RDP_VI_REGISTER_H_START] = pair(VI_H_OFFSET_NTSC + 100, VI_H_OFFSET_NTSC + 50);
	image = scan(&options, 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	CHECK(!image && !vi.prev_scanout_image && submits_made == frame_submits && num_events == 0,
			"a picture of no width shows nothing, and the frame before is dropped");

	ntsc_320x240();
	drop(shown);
	shown = scan(&options, 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	options.persist_frame_on_invalid_input = true;
	vi.registers[RDP_VI_REGISTER_H_START] = pair(VI_H_OFFSET_NTSC + 100, VI_H_OFFSET_NTSC + 50);
	image = scan(&options, 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	CHECK(image == shown && count_events(EV_BARRIER) == 0, "asked to persist, the frame before is shown again");
	drop(image);
	image = scan(&options, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	CHECK(image == shown && count_events(EV_BARRIER) == 1 && layout_of(image) == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
			&& vi.prev_image_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && bad_layouts == 0,
			"moved to the layout asked for");
	drop(image);
	image = scan(&options, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	CHECK(!image && !vi.prev_scanout_image, "for two frames, and no longer");
	drop(shown);

	/* ---- no framebuffer ---- */
	ntsc_320x240();
	shown = scan(&options, 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	vi.registers[RDP_VI_REGISTER_ORIGIN] = 0;
	image = scan(&options, 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	CHECK(shown && !image && !vi.prev_scanout_image && num_events == 0, "without a framebuffer nothing is shown");
	drop(shown);

	/* ---- a blank type ---- */
	ntsc_320x240();
	drop(scan(&options, 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
	vi.registers[RDP_VI_REGISTER_CONTROL] = VI_CONTROL_TYPE_BLANK_BIT;
	image = scan(&options, 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	CHECK(image && count_events(EV_PASS) == 2, "the first blank frame is still scanned out");
	{
		unsigned pipeline = (unsigned)(events[find_event(EV_DISPATCH, 0)].c / 16) - 1;
		CHECK(pipeline < MAX_PIPELINES && (pipelines[pipeline].spec[1] & VI_CONTROL_TYPE_MASK) == VI_CONTROL_TYPE_RGBA5551_BIT,
				"as 16 bits a pixel");
	}
	drop(image);
	image = scan(&options, 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	CHECK(!image && !vi.prev_scanout_image && num_events == 0, "the ones after it are not");
	ntsc_320x240();
	image = scan(&options, 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	CHECK(image != NULL, "until the VI shows something again");
	drop(image);
}

static void test_blend(void)
{
	rdp_vi_scanout_options_t options;
	granite_image_t *image;
	unsigned i, blends = 0, moved = 0;
	int before;

	rdp_vi_scanout_options_init(&options);
	options.blend_previous_frame = true;
	/* a picture that leaves room above and below */
	ntsc_320x240();
	vi.registers[RDP_VI_REGISTER_V_START] = pair(VI_V_OFFSET_NTSC + 40, VI_V_OFFSET_NTSC + 440);
	vi.registers[RDP_VI_REGISTER_ORIGIN] = 0;
	drop(scan(&options, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL));
	vi.registers[RDP_VI_REGISTER_ORIGIN] = 0x100000;
	image = scan(&options, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	CHECK(image && count_events(EV_DRAW) == 2, "there is nothing to blend into the first frame");
	CHECK(constant_at(find_event(EV_DRAW, 1), 2) == 20, "(a picture that starts 20 lines down)");
	before = image_index(image->image);
	drop(image);

	image = scan(&options, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	CHECK(image && count_events(EV_PASS) == 2 && count_events(EV_DRAW) == 5,
			"the frame before is blended in above, over and below the picture");
	for (i = 2; i < 5; i++)
	{
		int ev = find_event(EV_DRAW, i);
		unsigned pipeline = (unsigned)events[ev].a - 1;
		if (pipeline < MAX_PIPELINES && pipelines[pipeline].blend && pipelines[pipeline].write_mask == 7)
			blends++;
	}
	CHECK(blends == 3, "by a blend that leaves alpha alone");
	CHECK(events[find_event(EV_DRAW, 2)].scissor.offset.y == 0 && events[find_event(EV_DRAW, 2)].scissor.extent.height == 20
			&& events[find_event(EV_DRAW, 3)].scissor.offset.y == 20
			&& events[find_event(EV_DRAW, 3)].scissor.extent.height == 200
			&& events[find_event(EV_DRAW, 3)].scissor.offset.x == 8
			&& events[find_event(EV_DRAW, 3)].scissor.extent.width == 625
			&& events[find_event(EV_DRAW, 4)].scissor.offset.y == 220
			&& events[find_event(EV_DRAW, 4)].scissor.extent.height == 20, "the middle without the guard band");
	/* the frame before came out for a transfer: it is moved to be read */
	for (i = 0; i < num_events && i < MAX_EVENTS; i++)
		if (events[i].type == EV_BARRIER && events[i].a == before
				&& events[i].b == (int)VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
			moved++;
	CHECK(bad_layouts == 0 && moved == 1 && before != image_index(image->image),
			"the frame before is moved to where it can be read");
	drop(image);

	/* a picture of no height: only the frame before, over its columns */
	vi.registers[RDP_VI_REGISTER_V_START] = pair(VI_V_OFFSET_NTSC + 40, VI_V_OFFSET_NTSC + 40);
	image = scan(&options, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	CHECK(image && count_events(EV_DISPATCH) == 0 && count_events(EV_PASS) == 1 && count_events(EV_DRAW) == 1
			&& events[find_event(EV_DRAW, 0)].scissor.extent.width == 640
			&& events[find_event(EV_DRAW, 0)].scissor.extent.height == 240,
			"a picture of no height is the frame before alone");
	drop(image);

	/* ---- with the fold ---- */
	options.downscale_steps = 1;
	ntsc_320x240();
	vi.registers[RDP_VI_REGISTER_V_START] = pair(VI_V_OFFSET_NTSC + 40, VI_V_OFFSET_NTSC + 440);
	drop(scan(&options, 2, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
	{
		granite_image_t *prev = vi.prev_scanout_image;
		unsigned from = num_sets;
		int ev;

		image = scan(&options, 2, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		CHECK(image && prev && image != prev && count_events(EV_PASS) == 2 && count_events(EV_DRAW) == 2,
				"folded, the scale pass blends the frame before in itself");
		CHECK(wrote_texture(from, 2, prev, VK_NULL_HANDLE), "reading it");
		ev = find_event(EV_DRAW, 1);
		/* above, over and below the picture, at 2x */
		CHECK(constant_at(ev, 18) == 0 && constant_at(ev, 19) == 0 && constant_at(ev, 24) == 1280
				&& constant_at(ev, 25) == 40 && constant_at(ev, 20) == 16 && constant_at(ev, 21) == 40
				&& constant_at(ev, 26) == 16 + 1250 && constant_at(ev, 27) == 440 && constant_at(ev, 22) == 0
				&& constant_at(ev, 23) == 440 && constant_at(ev, 28) == 1280 && constant_at(ev, 29) == 480,
				"within the three rectangles it is told");
		drop(image);
	}
}

/* ---- upscaled ---- */
static void test_upscaled(void)
{
	rdp_vi_scanout_options_t options;
	granite_image_t *image;
	unsigned offset, length, deinterlace_from;
	int ev;

	rdp_vi_scanout_options_init(&options);
	options.downscale_steps = 2;
	ntsc_320x240();
	rdp_vi_scanout_memory_range(vi.registers, &offset, &length);

	updates = upscaled_asked = upscaled_hidden_asked = 0;
	image = scan(&options, 4, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	CHECK(updates == 1 && update_addr == offset && update_pixels == length && update_size_log2 == 1,
			"upscaled, what the VI shows is brought up to date in the upscaled RDRAM first");
	CHECK(upscaled_asked == 1 && upscaled_hidden_asked == 1, "and read from there");
	CHECK(image && info_of(image)->extent.width == 640 && info_of(image)->extent.height == 240
			&& layout_of(image) == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			"4x with two downscales comes out at 1x");
	CHECK(count_events(EV_PASS) == 2 && count_events(EV_BLIT) == 0 && count_events(EV_DRAW) == 2,
			"the downscales folded into the scale pass");
	ev = find_event(EV_DISPATCH, 0);
	CHECK(constant_at(ev, 4) == 320 * 4 + 3 + 6 && constant_at(ev, 5) == 240 * 4 + 3 + 5,
			"the framebuffer is read at 4x");
	ev = find_event(EV_DRAW, 1);
	CHECK(events[ev].constants_size == 120 && constant_at(ev, 10) == 0 && constant_at(ev, 11) == 0
			&& constant_at(ev, 12) == 2560 && constant_at(ev, 13) == 960 && constant_at(ev, 14) == 2
			&& constant_at(ev, 15) == 0 && constant_at(ev, 8) == 2,
			"which gets the fold's registers: the picture's rectangle at 4x and two downscales");
	drop(image);

	/* the lines' horizontal state is at 4x too */
	{
		const rdp_vi_horizontal_buffer_t *newest = &vi.horizontal_buffers[0];
		const rdp_vi_horizontal_info_t *lines;
		unsigned i;

		for (i = 1; i < RDP_VI_MAX_HORIZONTAL_BUFFERS; i++)
			if (vi.horizontal_buffers[i].buffer && vi.horizontal_buffers[i].free_serial > newest->free_serial)
				newest = &vi.horizontal_buffers[i];
		lines = (const rdp_vi_horizontal_info_t *)granite_device_map_host_buffer(&dev, newest->buffer,
				GRANITE_MEMORY_ACCESS_READ_BIT, 0, newest->buffer->info.size);
		CHECK(newest->view && newest->free_serial == vi.frame_serial + dev.num_frames && lines
				&& lines[0].h_start == 0 && lines[0].h_start_clamp == 8 * 4 && lines[0].h_end_clamp == (8 + 625) * 4
				&& lines[239].h_start_clamp == 8 * 4,
				"and so is each line's horizontal state, in a buffer kept until its frame comes round");
	}
	options.downscale_steps = 1;
	image = scan(&options, 4, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	CHECK(image && info_of(image)->extent.width == 1280 && info_of(image)->extent.height == 480
			&& count_events(EV_BLIT) == 0 && constant_at(find_event(EV_DRAW, 1), 14) == 1,
			"one downscale folded leaves 2x");
	drop(image);
	options.downscale_steps = 2;

	/* ---- the same without the fold ---- */
	vi.fold_disabled = true;
	image = scan(&options, 4, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	CHECK(image && info_of(image)->extent.width == 640 && info_of(image)->extent.height == 240
			&& layout_of(image) == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && bad_layouts == 0,
			"without the fold the frame is the same size");
	CHECK(count_events(EV_PASS) == 2 && count_events(EV_BLIT) == 2
			&& events[find_event(EV_BLIT, 0)].a == events[find_event(EV_BLIT, 1)].b
			&& events[find_event(EV_BLIT, 1)].a == image_index(image->image)
			&& image_infos[events[find_event(EV_BLIT, 0)].b].extent.width == 2560
			&& image_infos[events[find_event(EV_BLIT, 0)].a].extent.width == 1280,
			"by two blits, each from the one before");
	ev = find_event(EV_DRAW, 1);
	CHECK(events[ev].constants_size == 36 && events[ev].scissor.extent.width == 2560
			&& events[ev].scissor.extent.height == 960, "after a scale pass at 4x");
	drop(image);

	/* one downscale leaves 2x */
	options.downscale_steps = 1;
	image = scan(&options, 4, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	CHECK(image && info_of(image)->extent.width == 1280 && count_events(EV_BLIT) == 1, "one downscale leaves 2x");
	drop(image);
	/* more than there are stops at 1x */
	options.downscale_steps = 30;
	image = scan(&options, 4, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	CHECK(image && info_of(image)->extent.width == 640 && count_events(EV_BLIT) == 2, "and more than there are stop at 1x");
	drop(image);
	vi.fold_disabled = false;

	/* ---- an interlaced field ---- */
	options.downscale_steps = 2;
	vi.registers[RDP_VI_REGISTER_CONTROL] |= VI_CONTROL_SERRATE_BIT;
	vi.registers[RDP_VI_REGISTER_V_CURRENT_LINE] = 1;
	image = scan(&options, 4, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	CHECK(image && info_of(image)->extent.width == 640 && info_of(image)->extent.height == 480
			&& count_events(EV_PASS) == 2 && count_events(EV_BLIT) == 0,
			"a field is deinterlaced to twice its height, folded into the scale pass too");
	ev = find_event(EV_DRAW, 1);
	CHECK(constant_at(ev, 15) == 2 && constant_at(ev, 16) == 0 && constant_at(ev, 17) == 240,
			"reading the 240 rows of the lower field");
	drop(image);
	vi.registers[RDP_VI_REGISTER_V_CURRENT_LINE] = 0;
	image = scan(&options, 4, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	ev = find_event(EV_DRAW, 1);
	CHECK(image && constant_at(ev, 15) == 2 && constant_at(ev, 16) == -2, "or of the upper");
	drop(image);

	vi.fold_disabled = true;
	deinterlace_from = num_sets;
	image = scan(&options, 4, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	CHECK(image && wrote_texture(deinterlace_from, 0, pooled((unsigned)events[find_event(EV_BLIT, 1)].a),
				dev.samplers[GRANITE_STOCK_SAMPLER_LINEAR_CLAMP].sampler),
			"which reads the last blit's image with a linear sampler");
	CHECK(image && info_of(image)->extent.width == 640 && info_of(image)->extent.height == 480
			&& count_events(EV_PASS) == 3 && count_events(EV_BLIT) == 2 && bad_layouts == 0
			&& layout_of(image) == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			"without the fold, by a pass of its own after the blits");
	ev = find_event(EV_DRAW, 2);
	{
		float y_offset;
		memcpy(&y_offset, events[ev].constants, sizeof(y_offset));
		CHECK(y_offset == -0.25f / 240.0f, "a quarter line up for the upper field");
	}
	drop(image);
	vi.fold_disabled = false;

	/* a field that is deinterlaced is not blended as well in the fold */
	options.blend_previous_frame = true;
	image = scan(&options, 4, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	CHECK(image && info_of(image)->extent.height == 480 && count_events(EV_PASS) == 3 && count_events(EV_BLIT) == 2,
			"a field to blend and to deinterlace is not folded");
	drop(image);
	options.blend_previous_frame = false;

	/* weave: the field is not deinterlaced, the frame is both fields */
	options.upscale_deinterlacing = false;
	options.downscale_steps = 0;
	image = scan(&options, 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	ev = find_event(EV_DRAW, 1);
	CHECK(image && info_of(image)->extent.height == 480 && constant_at(ev, 5) == 1 && constant_at(ev, 6) == 1
			&& constant_at(ev, 7) == 1 && constant_at(ev, 8) == 1, "woven, the frame is both fields' height");
	drop(image);
	options.vi.serrate = false;
	image = scan(&options, 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	CHECK(image && info_of(image)->extent.height == 240, "unless interlacing is turned off in the options");
	drop(image);
}

/* ---- the temporal upscaler's scanout ---- */
static void test_upscaler_scanout(void)
{
	rdp_vi_scanout_options_t options;
	rdp_upscaler_framebuffer_t fb;
	granite_image_t *image, *again;
	bool repeat = true;
	unsigned from = num_sets, i;
	int ev;

	rdp_vi_scanout_options_init(&options);
	ntsc_320x240();
	vi.registers[RDP_VI_REGISTER_ORIGIN] = 0x100001;
	updates = 0;
	new_frame();
	image = rdp_vi_upscaler_scanout(&vi, 2, &fb, &repeat);
	CHECK(image && !repeat && info_of(image)->extent.width == 640 && info_of(image)->extent.height == 480
			&& info_of(image)->format == VK_FORMAT_R8G8B8A8_UNORM
			&& layout_of(image) == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && !image->general_layout,
			"the upscaler's scanout is the framebuffer at the scaling factor, left to be sampled");
	CHECK(fb.origin == 0x100000 && fb.stride == 320 && fb.size_log2 == 1 && fb.width == 320 && fb.height == 240,
			"with where it is in RDRAM");
	CHECK(count_events(EV_DISPATCH) == 1 && count_events(EV_PASS) == 0 && updates == 1
			&& submits_made == frame_submits + 1 && submit_waits == frame_waits,
			"one compute pass on the async queue");
	granite_device_submit(&dev, granite_device_request_command_buffer(&dev, GRANITE_QUEUE_GRAPHICS), NULL, 0, NULL);
	granite_device_flush_frame(&dev);
	CHECK(submit_waits == frame_waits + 1, "which the graphics queue's next submission waits for");
	ev = find_event(EV_DISPATCH, 0);
	CHECK(events[ev].a == 80 && events[ev].b == 60 && constant_at(ev, 0) == 0x100001 >> 1 && constant_at(ev, 1) == 320
			&& constant_at(ev, 2) == 640 && constant_at(ev, 3) == 480 && constant_at(ev, 4) == 4 * 2
			&& constant_at(ev, 5) == 317 * 2, "8x8 pixels a group, black in the guard band at the sides");
	CHECK(vi.prev_upscaler_image == image, "kept as the frame before");
	CHECK(wrote_buffer_view(from, 2, vi.gamma_lut_view), "it reads the gamma table");
	/* (frames go by) */
	for (i = 0; i < 5; i++)
	{
		new_frame();
		drop(image);
		image = rdp_vi_upscaler_scanout(&vi, 2, &fb, &repeat);
	}
	CHECK(image && !repeat, "(frames go by)");

	/* invalid registers: the frame before, for three frames */
	vi.registers[RDP_VI_REGISTER_H_START] = pair(VI_H_OFFSET_NTSC + 100, VI_H_OFFSET_NTSC + 50);
	memset(&fb, 0, sizeof(fb));
	new_frame();
	again = rdp_vi_upscaler_scanout(&vi, 2, &fb, &repeat);
	CHECK(again == image && repeat && fb.origin == 0x100000 && fb.width == 320 && num_events == 0,
			"while the registers are invalid the frame before is shown again");
	drop(again);
	new_frame();
	drop(rdp_vi_upscaler_scanout(&vi, 2, &fb, &repeat));
	new_frame();
	drop(rdp_vi_upscaler_scanout(&vi, 2, &fb, &repeat));
	new_frame();
	again = rdp_vi_upscaler_scanout(&vi, 2, &fb, &repeat);
	CHECK(!again && !repeat && !vi.prev_upscaler_image && fb.width == 0, "for three frames");
	drop(image);

	/* a 32 bit framebuffer narrower than the picture */
	ntsc_320x240();
	vi.registers[RDP_VI_REGISTER_CONTROL] = VI_CONTROL_TYPE_RGBA8888_BIT;
	vi.registers[RDP_VI_REGISTER_ORIGIN] = 0x100002;
	vi.registers[RDP_VI_REGISTER_WIDTH] = 300;
	new_frame();
	image = rdp_vi_upscaler_scanout(&vi, 2, &fb, &repeat);
	ev = find_event(EV_DISPATCH, 0);
	i = (unsigned)(events[ev].c / 16) - 1;
	CHECK(image && info_of(image)->extent.width == 600 && fb.width == 300 && fb.stride == 300 && fb.size_log2 == 2
			&& fb.origin == 0x100000 && constant_at(ev, 0) == 0x100002 >> 2 && constant_at(ev, 5) == 600
			&& i < MAX_PIPELINES && pipelines[i].compute && pipelines[i].spec[1] == 1 && pipelines[i].spec[2] == 1
			&& pipelines[i].spec[0] == 8 << 20, "a 32 bit framebuffer is as wide as it is");
	drop(image);
	vi.registers[RDP_VI_REGISTER_CONTROL] = VI_CONTROL_TYPE_BLANK_BIT;
	new_frame();
	image = rdp_vi_upscaler_scanout(&vi, 2, &fb, &repeat);
	CHECK(!image && !vi.prev_upscaler_image && num_events == 0, "a blank type shows nothing");

	/* without a framebuffer */
	ntsc_320x240();
	updates = 0;
	new_frame();
	image = rdp_vi_upscaler_scanout(&vi, 1, &fb, &repeat);
	CHECK(image && info_of(image)->extent.width == 320 && updates == 0, "at 1x it reads RDRAM itself");
	drop(image);
	vi.registers[RDP_VI_REGISTER_ORIGIN] = 0;
	new_frame();
	image = rdp_vi_upscaler_scanout(&vi, 1, &fb, &repeat);
	CHECK(!image && !vi.prev_upscaler_image, "without a framebuffer it shows nothing");

	/* a plain scanout forgets the upscaler's frame */
	ntsc_320x240();
	new_frame();
	image = rdp_vi_upscaler_scanout(&vi, 1, &fb, &repeat);
	drop(scan(&options, 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
	CHECK(image && !vi.prev_upscaler_image, "a plain scanout drops the upscaler's frame");
	drop(image);
}

int main(void)
{
	unsigned i;

	fill_context();
	granite_device_init(&dev);
	granite_device_set_context(&dev, &context);

	rdram = storage_buffer(8 << 20);
	hidden_rdram = storage_buffer(4 << 20);
	upscaled_rdram = storage_buffer(8 << 20);
	upscaled_hidden_rdram = storage_buffer(8 << 20);
	rdp_shader_bank_init(&bank, &dev, no_defines, NULL);

	rdp_vi_set_device(&vi, &dev);
	rdp_vi_set_rdram(&vi, rdram, 0, 8 << 20);
	vi.hidden_rdram = hidden_rdram;
	vi.upscaled.user = &updates;
	vi.upscaled.update = update_upscaled;
	vi.upscaled.rdram = get_upscaled;
	vi.upscaled.hidden_rdram = get_upscaled_hidden;
	rdp_vi_set_shader_bank(&vi, &bank);
	CHECK(vi.gamma_lut && vi.gamma_lut_view && vi.vi_scale_fold && vi.no_previous_frame
			&& vi.vi_scale_fold->layout.push_constant_size == 120, "the VI makes its gamma table and the fold's shader");

	test_plain_frame();
	test_divot_and_crop();
	test_fetch_bug();
	test_nothing_to_show();
	test_blend();
	test_upscaled();
	test_upscaler_scanout();
	test_known_pipelines();

	/* the plain frame for a while: the other frames' images go */
	{
		rdp_vi_scanout_options_t options;

		rdp_vi_scanout_options_init(&options);
		ntsc_320x240();
		for (i = 0; i < 12; i++)
			drop(scan(&options, 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
		CHECK(vi.image_pool.count <= 9 && num_images > 40 && images_destroyed >= num_images - 9 - 1,
				"images no frame asks for any more go back as the frames go by");
	}
	/* the bank set again */
	rdp_vi_set_shader_bank(&vi, &bank);
	CHECK(vi.vi_scale_fold && vi.no_previous_frame, "the shader bank can be set again");

	rdp_vi_free(&vi);
	granite_buffer_unref(rdram);
	granite_buffer_unref(hidden_rdram);
	granite_buffer_unref(upscaled_rdram);
	granite_buffer_unref(upscaled_hidden_rdram);
	CHECK(!vi.device && !vi.prev_scanout_image && !vi.gamma_lut, "freed, the VI holds nothing");
	granite_device_free(&dev);
	CHECK(images_destroyed == num_images && num_images > 0, "every image went back");
	CHECK(unmade == made + num_pipelines, "and every buffer, view, pass, framebuffer and pipeline");
	for (i = 0; i < num_memory; i++)
		free(memory_data[i]);

	if (failures)
	{
		fprintf(stderr, "test_rdp_video_interface: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_rdp_video_interface: ok\n");
	return 0;
}
