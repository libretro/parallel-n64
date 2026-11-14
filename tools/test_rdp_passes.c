/* parallel-rdp's compute passes (parallel-rdp/rdp_passes.c), recorded on
 * a stand-in device.
 *
 * What the shaders are told of each framebuffer format: a 32-bit
 * framebuffer's copy pipe snaps to pairs of pixels (dx_mask ~1), in the
 * rasterizer as in depth and blending (the rasterizer once left the mask
 * unset). Each pass dispatches the groups its pixels, tiles, primitives
 * or words need, with the push constants its shader reads: tile binning
 * by meta-tiles of the subgroup's size, depth and blending by 8x8 pixels
 * with a pixel index for the framebuffer's address and a bit per group
 * of 32 primitives, the upscaled domain by whole words (or rows, for the
 * SSAA resolve), the rasterizer once per static raster state, from its
 * slot of the indirect dispatches. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <boolean.h>

#include "rdp_passes.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_rdp_passes: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

#include "vk_test_device.h"

/* video_interface.c's: none here */
void rdp_vi_queue_known_pipelines(rdp_vi_t *vi, unsigned upscaling, bool with_upscaler,
		rdp_known_pipeline_fn add, void *ctx)
{
	(void)vi; (void)upscaling; (void)with_upscaler; (void)add; (void)ctx;
}

static int no_defines(void *user, const char *name, const char *define)
{
	(void)user; (void)name; (void)define;
	return 0;
}

static void test_fb_info(void)
{
	static const struct
	{
		uint32_t fmt, dx_shift, dx_mask, fb_size;
		unsigned size_log2;
	} want[5] = {
		{ 0, 0, 0, 0, 0 },      /* I4 */
		{ 1, 3, ~7u, 1, 0 },    /* I8 */
		{ 2, 2, ~3u, 2, 1 },    /* RGBA5551 */
		{ 3, 2, ~3u, 2, 1 },    /* IA88 */
		{ 4, 1, ~1u, 4, 2 },    /* RGBA8888 */
	};
	unsigned i;
	for (i = 0; i < 5; i++)
	{
		rdp_global_fb_info_t info;
		memset(&info, 0x5a, sizeof(info));
		rdp_pass_fb_info(want[i].fmt, &info);
		CHECK(info.dx_shift == want[i].dx_shift, "dx_shift");
		CHECK(info.dx_mask == want[i].dx_mask, "dx_mask, 32-bit pixels too");
		CHECK(info.fb_size == want[i].fb_size, "fb_size");
		CHECK(rdp_pass_pixel_size_log2(want[i].fmt) == want[i].size_log2, "pixel size");
	}
}

/* ---- the passes ---- */
static rdp_shader_bank_t bank;
static rdp_renderer_caps_t caps;
static rdp_pipeline_setup_t setup;
static rdp_pipeline_builder_t builder;
static rdp_renderer_buffers_t buffers;
static rdp_stream_instance_t streams;
static granite_buffer_t *rdram, *hidden, *tmem;
static rdp_pass_resources_t res;
static uint8_t lut[0x8000];

static granite_buffer_t *storage_buffer(VkDeviceSize size)
{
	granite_buffer_info_t info;
	info.domain = GRANITE_BUFFER_DOMAIN_DEVICE;
	info.size = size;
	info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	info.misc = 0;
	return granite_device_create_buffer(&dev, &info, NULL);
}

static void open_device(unsigned upscaling, uint32_t subgroup_size)
{
	static const VkDeviceSize sizes[RDP_STREAM_BUFFERS] = {
		4096, 4096, 4096, 4096, 2048, 1024, 1024, 4096, 4096, 8192
	};

	fill_context();
	context.ext.subgroup_size_control_features.subgroupSizeControl = VK_TRUE;
	context.ext.subgroup_size_control_features.computeFullSubgroups = VK_TRUE;
	context.ext.subgroup_size_control_properties.minSubgroupSize = 32;
	context.ext.subgroup_size_control_properties.maxSubgroupSize = 64;
	context.ext.subgroup_size_control_properties.requiredSubgroupSizeStages = VK_SHADER_STAGE_COMPUTE_BIT;
	granite_device_init(&dev);
	granite_device_set_context(&dev, &context);
	rdp_shader_bank_init(&bank, &dev, no_defines, NULL);

	/* (four static raster states to a binding: the stand-in's memory is
	 * real) */
	rdp_caps_init(&caps);
	rdp_caps_init_limits(&caps, upscaling, false, (VkDeviceSize)16 * 0x8000 * upscaling * upscaling * 4);
	caps.upscaling = upscaling;
	caps.subgroup_tile_binning = subgroup_size != 32;

	setup.caps = &caps;
	setup.bank = &bank;
	setup.rdram_size = 64 << 10;
	setup.host_coherent = true;
	setup.subgroup_size = subgroup_size;

	rdp_pipeline_builder_clear(&builder);
	CHECK(rdp_pipeline_builder_init(&builder, &dev, 8), "a builder");
	memset(&buffers, 0, sizeof(buffers));
	memset(&streams, 0, sizeof(streams));
	CHECK(rdp_renderer_buffers_init(&buffers, &dev, &caps, 4096, 4096, lut, sizeof(lut)), "buffers");
	CHECK(rdp_renderer_buffers_init_upscaled(&buffers, &dev, &caps, 64 << 10, 32 << 10), "upscaled");
	CHECK(rdp_stream_instance_init(&streams, &dev, sizes), "streams");
	rdram = storage_buffer(128 << 10);
	hidden = storage_buffer(32 << 10);
	tmem = storage_buffer(4096);

	res.setup = &setup;
	res.pipelines = &builder;
	res.buffers = &buffers;
	res.streams = &streams.gpu;
	res.rdram = rdram;
	res.rdram_offset = 0;
	res.rdram_size = 64 << 10;
	res.hidden_rdram = hidden;
	res.tmem = tmem;
}

static void close_device(void)
{
	rdp_pipeline_builder_free(&builder);
	rdp_renderer_buffers_free(&buffers);
	rdp_stream_instance_free(&streams);
	granite_buffer_unref(rdram);
	granite_buffer_unref(hidden);
	granite_buffer_unref(tmem);
	granite_device_free(&dev);
}

static granite_command_buffer_t *begin(void)
{
	num_events = 0;
	return granite_device_request_command_buffer(&dev, GRANITE_QUEUE_COMPUTE);
}

static void end(granite_command_buffer_t *cb)
{
	granite_device_submit(&dev, cb, NULL, 0, NULL);
}

static const uint32_t *pushed(int ev)
{
	return (const uint32_t *)events[ev].constants;
}

static void target(rdp_pass_target_t *t, uint32_t fmt)
{
	t->addr = 0x100000;
	t->depth_addr = 0x200000;
	t->width = 320;
	t->height = 237;
	t->fmt = fmt;
	t->base_primitive_index = 1000;
	t->num_primitives = 70;
}

static void test_tile_binning(void)
{
	granite_command_buffer_t *cb;
	rdp_pass_target_t t;
	int ev;

	open_device(2, 32);
	target(&t, 2);
	cb = begin();
	rdp_pass_tile_binning(&res, cb, &t, false);
	ev = find_event(EV_DISPATCH, 0);
	/* 40x30 tiles, 8x4 meta-tiles of 32 */
	CHECK(events[ev].a == 3 && events[ev].b == 5 && (events[ev].c & 15) == 8, "native: by meta-tiles of 32");
	CHECK(pushed(ev)[0] == 320 && pushed(ev)[1] == 237 && pushed(ev)[2] == 70, "native size");
	CHECK(!cb->pipeline_state.static_state.state.subgroup_control_size, "no subgroup size asked for");
	rdp_pass_tile_binning(&res, cb, &t, true);
	ev = find_event(EV_DISPATCH, 1);
	CHECK(events[ev].a == 3 && events[ev].b == 10 && (events[ev].c & 15) == 15, "upscaled tiles");
	CHECK(pushed(ev)[0] == 640 && pushed(ev)[1] == 474, "upscaled size");
	end(cb);
	close_device();

	/* subgroups of 64: meta-tiles of 8x8, size control off after */
	open_device(1, 64);
	cb = begin();
	rdp_pass_tile_binning(&res, cb, &t, false);
	ev = find_event(EV_DISPATCH, 0);
	CHECK(events[ev].a == 3 && events[ev].b == 5 && (events[ev].c & 15) == 4, "by meta-tiles of 64");
	CHECK(!cb->pipeline_state.static_state.state.subgroup_control_size, "size control off after");
	end(cb);
	close_device();
}

static void test_depth_blend(void)
{
	static const uint32_t addr_index[5] = { 0x100000, 0x100000, 0x80000, 0x80000, 0x40000 };
	granite_command_buffer_t *cb;
	rdp_pass_target_t t;
	uint32_t fmt;
	int ev;

	open_device(2, 32);
	cb = begin();
	for (fmt = 0; fmt < 5; fmt++)
	{
		target(&t, fmt);
		rdp_pass_depth_blend(&res, cb, &t, tmem, false, false);
		ev = find_event(EV_DISPATCH, fmt);
		CHECK(events[ev].a == 40 && events[ev].b == 30, "8x8 pixels to a group");
		CHECK(pushed(ev)[0] == addr_index[fmt], "a pixel index for the address");
		CHECK(pushed(ev)[1] == 0x100000, "the depth's, of 16-bit pixels");
		CHECK(pushed(ev)[2] == 320 && pushed(ev)[3] == 237, "the size");
		CHECK(pushed(ev)[4] == 7, "70 primitives: three groups");
	}
	t.num_primitives = 256;
	rdp_pass_depth_blend(&res, cb, &t, tmem, true, true);
	ev = find_event(EV_DISPATCH, 5);
	CHECK(events[ev].a == 80 && events[ev].b == 60, "upscaled");
	CHECK(pushed(ev)[2] == 640 && pushed(ev)[3] == 474 && pushed(ev)[4] == 0xff, "upscaled, every group");
	end(cb);
	close_device();
}

static void test_upscaled_domain(void)
{
	granite_command_buffer_t *cb;
	int ev;

	open_device(2, 32);
	cb = begin();
	/* 13 16-bit pixels from 0x1003: from the word at 0x1000, 14 pixels */
	rdp_pass_update_upscaled_domain(&res, cb, RDP_RESOLVE_PRE, 0x1003, 0x2001, 13, 1, 1);
	ev = find_event(EV_DISPATCH, 0);
	CHECK(events[ev].a == 1 && events[ev].b == 1, "one group");
	CHECK(pushed(ev)[0] == 14 && pushed(ev)[1] == 0x800 && pushed(ev)[2] == 0x1000, "whole words");
	/* 8-bit pixels: to four */
	rdp_pass_update_upscaled_domain(&res, cb, RDP_RESOLVE_POST, 0x1000, 0x1000, 130, 1, 0);
	ev = find_event(EV_DISPATCH, 1);
	CHECK(events[ev].a == 3 && pushed(ev)[0] == 132, "8-bit, whole words");
	/* the resolve: rows, unaligned */
	rdp_pass_update_upscaled_domain(&res, cb, RDP_RESOLVE_SSAA, 0x1002, 0x2002, 320, 240, 1);
	ev = find_event(EV_DISPATCH, 2);
	CHECK(events[ev].a == 5 && events[ev].b == 240, "a row of groups to a row");
	CHECK(pushed(ev)[0] == 76800 && pushed(ev)[1] == 0x801 && pushed(ev)[2] == 0x1001, "as it is");
	CHECK(pushed(ev)[3] == 320 && pushed(ev)[4] == 240, "the size");

	rdp_pass_clear_super_sample_write_mask(&res, cb, 320, 240);
	ev = find_event(EV_DISPATCH, 3);
	CHECK(events[ev].a == 75 && events[ev].b == 1, "4800 words of 4x4 pixels");
	CHECK(cb->pipeline_state.potential_static_state.spec_constant_mask == 0, "no constants after");
	end(cb);
	close_device();
}

static void test_simple(void)
{
	static rdp_upload_info_t uploads[3];
	granite_command_buffer_t *cb;
	int ev;

	open_device(1, 32);
	cb = begin();
	rdp_pass_tmem_update(&res, cb, uploads, 3);
	ev = find_event(EV_DISPATCH, 0);
	CHECK(events[ev].a == 32 && pushed(ev)[0] == 3, "TMEM: 2048 invocations, the count");
	rdp_pass_span_setup(&res, cb, false, 77);
	ev = find_event(EV_DISPATCH, 1);
	CHECK(events[ev].a == 77 && events[ev].b == 1, "a group per span job");
	rdp_pass_clear_indirect(&res, cb);
	ev = find_event(EV_DISPATCH, 2);
	CHECK(events[ev].a == 1, "64 states, one group");
	end(cb);
	close_device();
}

static void test_rasterize(void)
{
	static rdp_static_state_t states[3];
	granite_command_buffer_t *cb;
	rdp_pass_target_t t;
	unsigned i;

	open_device(1, 32);
	target(&t, 4);
	memset(states, 0, sizeof(states));
	for (i = 0; i < 3; i++)
	{
		states[i].flags = i;
		states[i].combiner[0].rgb.muladd = (uint8_t)(i + 1);
	}
	cb = begin();
	rdp_pass_rasterize(&res, cb, &t, tmem, false, states, 3);
	CHECK(count_events(EV_DISPATCH) == 3, "a dispatch per state");
	for (i = 0; i < 3; i++)
	{
		int ev = find_event(EV_DISPATCH, i);
		CHECK(events[ev].a == -1 && events[ev].b == (int)(16 * i), "indirect, from its slot");
	}
	/* the last missed: the generic variant, its own asked for */
	CHECK(cb->pipeline_state.potential_static_state.spec_constant_mask == 7, "the generic variant");
	CHECK(builder.pending.count == 3, "each variant asked for");
	end(cb);
	close_device();
}

/* ---- a flush's passes, in order ---- */

/* The command buffer's passes as letters, by their groups: S span setup
 * (77 jobs), T tile binning (70 primitives), M the TMEM update, Z depth
 * and blending, C clearing the indirect dispatches, U the upscaled
 * domain's update before or after, R its resolve, K clearing the
 * super-sampled write mask, I the rasterizer's indirect dispatch, B a
 * memory barrier */
static const char *recorded(void)
{
	static char s[64];
	unsigned i, n = 0;
	for (i = 0; i < num_events && i < MAX_EVENTS && n + 1 < sizeof(s); i++)
	{
		char c = '?';
		if (events[i].type == EV_MEMORY_BARRIER)
			c = 'B';
		else if (events[i].type != EV_DISPATCH)
			continue;
		else if (events[i].a == -1)
			c = 'I';
		else if (events[i].a == 77)
			c = 'S';
		else if (events[i].a == 3)
			c = 'T';
		else if (events[i].a == 32)
			c = 'M';
		else if (events[i].a == 40 || events[i].a == 80)
			c = 'Z';
		else if (events[i].a == 1)
			c = 'C';
		else if (events[i].a == 1185)
			c = 'U';
		else if (events[i].a == 5 && events[i].b == 237)
			c = 'R';
		else if (events[i].a == 75)
			c = 'K';
		s[n++] = c;
	}
	s[n] = '\0';
	return s;
}

static rdp_upload_info_t flush_uploads[2];
static rdp_static_state_t flush_states[2];

static void flush_of(rdp_pass_flush_t *f, uint32_t fmt, bool draws, bool uploads)
{
	target(&f->target, fmt);
	if (!draws)
		f->target.width = 0;
	f->uploads = flush_uploads;
	f->num_uploads = uploads ? 2 : 0;
	f->num_span_jobs = 77;
	f->states = flush_states;
	f->num_states = 2;
}

static void expect_native(const rdp_pass_flush_t *f, const char *want, const char *what)
{
	granite_command_buffer_t *cb = begin();
	rdp_pass_record_native(&res, cb, f);
	if (strcmp(recorded(), want) != 0)
		fprintf(stderr, "test_rdp_passes: %s: %s, not %s\n", what, recorded(), want);
	CHECK(strcmp(recorded(), want) == 0, what);
	end(cb);
}

static void expect_upscaled(const rdp_pass_flush_t *f, const char *want, const char *what)
{
	granite_command_buffer_t *cb = begin();
	rdp_pass_record_upscaled(&res, cb, f);
	if (strcmp(recorded(), want) != 0)
		fprintf(stderr, "test_rdp_passes: %s: %s, not %s\n", what, recorded(), want);
	CHECK(strcmp(recorded(), want) == 0, what);
	end(cb);
}

static void test_flush_order(void)
{
	rdp_pass_flush_t f;
	granite_command_buffer_t *cb;
	int ev;

	/* native: binning, span setup and the TMEM update side by side,
	 * then the rasterizer, depth and blending, and the indirect
	 * dispatches cleared for the next */
	open_device(1, 32);
	flush_of(&f, 2, true, true);
	expect_native(&f, "STMBIIBZC", "native");
	/* the primitives read this flush's tile loads, or TMEM as it was */
	cb = begin();
	rdp_pass_record_native(&res, cb, &f);
	CHECK(cb->state.bindings.cookies[0][2] == buffers.tmem_instances->cookie, "the flush's loads");
	end(cb);
	flush_of(&f, 2, true, false);
	cb = begin();
	rdp_pass_record_native(&res, cb, &f);
	CHECK(cb->state.bindings.cookies[0][2] == tmem->cookie, "TMEM as it was");
	end(cb);
	flush_of(&f, 2, true, true);
	ev = find_event(EV_MEMORY_BARRIER, 0);
	CHECK((events[ev].a & VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT) &&
	      (events[ev].b & VK_ACCESS_INDIRECT_COMMAND_READ_BIT), "the rasterizer's dispatches wait for binning");
	flush_of(&f, 2, false, true);
	expect_native(&f, "MBC", "tile loads only");
	flush_of(&f, 2, false, false);
	expect_native(&f, "", "nothing");
	CHECK(!rdp_pass_flush_upscaled(&caps, &f), "not upscaled at 1x");
	cb = begin();
	rdp_pass_record_end(cb);
	CHECK(strcmp(recorded(), "B") == 0, "a barrier at the end");
	end(cb);
	close_device();

	/* 2x: the domain updated before; the reference after, for a
	 * framebuffer drawn upscaled too */
	open_device(2, 32);
	flush_of(&f, 2, true, true);
	CHECK(rdp_pass_flush_upscaled(&caps, &f), "16-bit, 320 wide: upscaled");
	expect_native(&f, "STUMBIIBZCBU", "native, 2x");
	expect_upscaled(&f, "STBIIBZC", "upscaled, the loads already done");
	flush_of(&f, 1, true, true);
	CHECK(!rdp_pass_flush_upscaled(&caps, &f), "8-bit: off-screen");
	expect_native(&f, "STUMBIIBZC", "native, 2x, off-screen");
	flush_of(&f, 4, true, true);
	f.target.width = 255;
	CHECK(!rdp_pass_flush_upscaled(&caps, &f), "narrower than 256: off-screen");
	f.target.width = 256;
	CHECK(rdp_pass_flush_upscaled(&caps, &f), "32-bit, 256 wide: upscaled");
	flush_of(&f, 2, false, true);
	CHECK(!rdp_pass_flush_upscaled(&caps, &f), "nothing drawn: not upscaled");

	/* super-sampled read-back: always upscaled, its own loads, resolved */
	caps.super_sample_readback = true;
	flush_of(&f, 0, false, false);
	CHECK(rdp_pass_flush_upscaled(&caps, &f), "super-sampled: always");
	flush_of(&f, 2, true, true);
	expect_upscaled(&f, "STUKMBIIBZCBR", "super-sampled");
	caps.super_sample_readback = false;
	close_device();

	/* the ubershader: no rasterizer, nothing indirect */
	open_device(2, 32);
	caps.ubershader = true;
	flush_of(&f, 2, true, false);
	expect_native(&f, "STUBZBU", "ubershader, native");
	ev = find_event(EV_MEMORY_BARRIER, 0);
	CHECK(!(events[ev].a & VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT), "nothing indirect to wait for");
	caps.super_sample_readback = true;
	flush_of(&f, 2, true, true);
	expect_upscaled(&f, "STUKMBZBR", "ubershader, super-sampled");
	caps.super_sample_readback = false;
	flush_of(&f, 2, false, true);
	expect_native(&f, "MB", "ubershader, tile loads only");
	close_device();
}

int main(void)
{
	unsetenv("PARALLEL_RDP_NO_KNOWN_PIPELINES");
	test_fb_info();
	test_tile_binning();
	test_depth_blend();
	test_upscaled_domain();
	test_simple();
	test_rasterize();
	test_flush_order();

	if (failures)
	{
		fprintf(stderr, "test_rdp_passes: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_rdp_passes: all checks passed\n");
	return 0;
}
