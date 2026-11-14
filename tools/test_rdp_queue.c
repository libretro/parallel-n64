/* parallel-rdp's draw queue (parallel-rdp/rdp_queue.c).
 *
 * Each different static, depth blend and tile state is kept once and a
 * primitive points at it. A primitive gets a span interpolation job per
 * 64 of its lines inside the scissor (one more line than it has), none
 * outside it; the framebuffer's height grows to the lowest line drawn.
 * Primitive depth replaces the primitive's own. The flush has to go
 * when one more primitive or load might not fit.
 *
 * A tile load reads a pending framebuffer when its address falls among
 * the pixels the queued primitives write, at the framebuffer's own
 * pixel size: an IA88 framebuffer's are 2 bytes (the queue once took
 * them as 1, and missed its second half). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rdp_queue.h"

/* device_caps.c's other functions call these; nothing here runs them */
PFN_vkGetPhysicalDeviceToolPropertiesEXT vkGetPhysicalDeviceToolPropertiesEXT;
PFN_vkGetPhysicalDeviceFormatProperties2 vkGetPhysicalDeviceFormatProperties2;

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_rdp_queue: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

static rdp_triangle_setup_t triangles[RDP_QUEUE_MAX_PRIMITIVES];
static rdp_attribute_setup_t attributes[RDP_QUEUE_MAX_PRIMITIVES];
static rdp_derived_setup_t derived[RDP_QUEUE_MAX_PRIMITIVES];
static rdp_scissor_state_t scissors[RDP_QUEUE_MAX_PRIMITIVES];
static rdp_instance_indices_t indices[RDP_QUEUE_MAX_PRIMITIVES];
static rdp_span_info_offsets_t span_offsets[RDP_QUEUE_MAX_PRIMITIVES];
static rdp_span_job_t span_jobs[RDP_QUEUE_MAX_SPAN_SETUPS];

static rdp_queue_t q;
static rdp_renderer_caps_t caps;
static rdp_constants_t constants;
static rdp_tile_info_t tiles[8];

static void fresh(void)
{
	void *mapped[RDP_STREAM_BUFFERS];
	memset(mapped, 0, sizeof(mapped));
	mapped[RDP_STREAM_TRIANGLE_SETUP] = triangles;
	mapped[RDP_STREAM_ATTRIBUTE_SETUP] = attributes;
	mapped[RDP_STREAM_DERIVED_SETUP] = derived;
	mapped[RDP_STREAM_SCISSOR_SETUP] = scissors;
	mapped[RDP_STREAM_STATE_INDICES] = indices;
	mapped[RDP_STREAM_SPAN_INFO_OFFSETS] = span_offsets;
	mapped[RDP_STREAM_SPAN_INFO_JOBS] = span_jobs;

	rdp_queue_init(&q);
	rdp_queue_bind(&q, mapped);
	q.scissor.xlo = 0;
	q.scissor.ylo = 0;
	q.scissor.xhi = 320 * 4;
	q.scissor.yhi = 240 * 4;
	rdp_caps_init(&caps);
	memset(&constants, 0, sizeof(constants));
	memset(tiles, 0, sizeof(tiles));
}

/* A triangle from line @top to just before line @bottom */
static rdp_triangle_setup_t triangle(int top, int bottom)
{
	rdp_triangle_setup_t t;
	memset(&t, 0, sizeof(t));
	t.yh = (int16_t)(top * 4);
	t.ym = (int16_t)(top * 4);
	t.yl = (int16_t)(bottom * 4 + 1);
	/* from x 10 to 100 */
	t.flags = TRIANGLE_SETUP_FLIP_BIT;
	t.xh = 10 << 16;
	t.xm = 100 << 16;
	t.xl = 100 << 16;
	return t;
}

static void test_spans(void)
{
	rdp_triangle_setup_t t;

	fresh();
	t = triangle(10, 100);
	rdp_queue_add_primitive(&q, &caps, &constants, tiles, &t, NULL);
	/* lines 10 to 100, and one more: 92, two jobs */
	CHECK(q.num_primitives == 1 && q.num_span_jobs == 2, "two jobs");
	CHECK(span_offsets[0].offset == 0 && span_offsets[0].ylo == 10 && span_offsets[0].yhi == 100, "its lines");
	CHECK(span_jobs[0].primitive_index == 0 && span_jobs[0].base_y == 10 && span_jobs[0].max_y == 101, "job 0");
	CHECK(span_jobs[1].base_y == 74 && span_jobs[1].max_y == 101, "job 1");
	CHECK(q.height == 101, "the height to its lowest line");

	/* lines 0 to 63: 64 lines and the one after, two jobs */
	t = triangle(0, 63);
	t.yl = 64 * 4;
	rdp_queue_add_primitive(&q, &caps, &constants, tiles, &t, NULL);
	CHECK(span_offsets[1].yhi == 63 && q.num_span_jobs == 4, "a line past the 64th: another job");
	rdp_queue_reset(&q);
	t = triangle(10, 100);
	rdp_queue_add_primitive(&q, &caps, &constants, tiles, &t, NULL);

	/* clipped to the scissor */
	t = triangle(200, 300);
	rdp_queue_add_primitive(&q, &caps, &constants, tiles, &t, NULL);
	CHECK(span_offsets[1].offset == 128 && span_offsets[1].ylo == 200 && span_offsets[1].yhi == 239, "to the scissor");
	CHECK(q.num_span_jobs == 3 && span_jobs[2].primitive_index == 1 && span_jobs[2].max_y == 240, "one job");
	CHECK(q.height == 240, "no further than the scissor");

	/* outside it */
	t = triangle(250, 260);
	rdp_queue_add_primitive(&q, &caps, &constants, tiles, &t, NULL);
	CHECK(span_offsets[2].yhi == -1 && span_offsets[2].offset == 0 && q.num_span_jobs == 3, "no jobs outside");
	CHECK(q.num_primitives == 3, "still a primitive");
	CHECK(q.color_write_pending && !q.depth_write_pending, "colour pending");

	q.depth_blend.flags = DEPTH_BLEND_DEPTH_UPDATE_BIT;
	rdp_queue_add_primitive(&q, &caps, &constants, tiles, &t, NULL);
	CHECK(q.depth_write_pending, "depth pending");

	rdp_queue_reset(&q);
	CHECK(q.num_primitives == 0 && q.num_span_jobs == 0 && q.height == 0 && !q.color_write_pending &&
	      !q.depth_write_pending, "reset");
	CHECK(q.depth_blend.flags == DEPTH_BLEND_DEPTH_UPDATE_BIT, "the state kept");
}

static void test_attributes(void)
{
	rdp_triangle_setup_t t;
	rdp_attribute_setup_t a;

	fresh();
	t = triangle(0, 10);
	memset(&a, 0, sizeof(a));
	a.r = 5;
	a.z = 1000;
	a.dzdx = 7;
	a.dzde = 8;
	a.dzdy = 9;
	rdp_queue_add_primitive(&q, &caps, &constants, tiles, &t, &a);
	CHECK(attributes[0].r == 5 && attributes[0].z == 1000 && attributes[0].dzdy == 9, "as given");

	constants.use_prim_depth = true;
	constants.prim_depth = 4242;
	rdp_queue_add_primitive(&q, &caps, &constants, tiles, &t, &a);
	CHECK(attributes[1].r == 5 && attributes[1].z == 4242 && attributes[1].dzdx == 0 && attributes[1].dzde == 0 &&
	      attributes[1].dzdy == 0, "primitive depth");

	rdp_queue_add_primitive(&q, &caps, &constants, tiles, &t, NULL);
	CHECK(attributes[2].r == 0 && attributes[2].z == 4242, "flat");
	CHECK(scissors[2].yhi == 240 * 4, "the scissor");
}

static void test_state_caches(void)
{
	rdp_triangle_setup_t t = triangle(0, 4);
	const void *data[RDP_STREAM_BUFFERS];
	VkDeviceSize bytes[RDP_STREAM_BUFFERS];
	unsigned i;

	fresh();
	/* A, B, A: two depth blend states */
	q.depth_blend.flags = 1;
	rdp_queue_add_primitive(&q, &caps, &constants, tiles, &t, NULL);
	q.depth_blend.flags = 2;
	rdp_queue_add_primitive(&q, &caps, &constants, tiles, &t, NULL);
	q.depth_blend.flags = 1;
	rdp_queue_add_primitive(&q, &caps, &constants, tiles, &t, NULL);
	CHECK(q.num_depth_blend_states == 2, "each once");
	CHECK(indices[0].depth_blend_index == 0 && indices[1].depth_blend_index == 1 && indices[2].depth_blend_index == 0,
	      "the primitives point at them");
	CHECK(q.num_static_states == 1 && indices[2].static_index == 0, "one static state");
	/* eight tiles alike: one tile state */
	CHECK(q.num_tile_infos == 1 && indices[0].tile_indices[7] == 0, "one tile state");
	tiles[3].size.slo = 9;
	rdp_queue_add_primitive(&q, &caps, &constants, tiles, &t, NULL);
	CHECK(q.num_tile_infos == 2 && indices[3].tile_indices[2] == 0 && indices[3].tile_indices[3] == 1 &&
	      indices[3].tile_indices[4] == 0, "a different tile");

	/* the loads before a primitive */
	{
		rdp_upload_info_t up;
		memset(&up, 0, sizeof(up));
		rdp_queue_add_upload(&q, &up);
		rdp_queue_add_upload(&q, &up);
	}
	rdp_queue_add_primitive(&q, &caps, &constants, tiles, &t, NULL);
	CHECK(indices[4].tile_instance_index == 2, "after two loads");

	rdp_queue_upload_data(&q, data, bytes);
	CHECK(data[RDP_STREAM_TRIANGLE_SETUP] == triangles && bytes[RDP_STREAM_TRIANGLE_SETUP] == 5 * sizeof(triangles[0]),
	      "the triangles where they are");
	CHECK(data[RDP_STREAM_DEPTH_BLEND_STATE] == q.depth_blend_states &&
	      bytes[RDP_STREAM_DEPTH_BLEND_STATE] == 2 * sizeof(rdp_depth_blend_state_t), "the depth blend states");
	CHECK(bytes[RDP_STREAM_TILE_INFO_STATE] == 2 * sizeof(rdp_tile_info_t), "the tile states");
	CHECK(bytes[RDP_STREAM_SPAN_INFO_JOBS] == 5 * sizeof(rdp_span_job_t), "a job each");
	for (i = 0; i < RDP_STREAM_BUFFERS; i++)
		CHECK(data[i] != NULL, "every stream");
}

static void test_full(void)
{
	rdp_triangle_setup_t t = triangle(0, 4);
	rdp_upload_info_t up;
	unsigned i;

	fresh();
	for (i = 0; i < RDP_QUEUE_MAX_PRIMITIVES - 1; i++)
		rdp_queue_add_primitive(&q, &caps, &constants, tiles, &t, NULL);
	CHECK(!rdp_queue_full(&q, &caps), "255 primitives");
	rdp_queue_add_primitive(&q, &caps, &constants, tiles, &t, NULL);
	CHECK(rdp_queue_full(&q, &caps), "256");

	fresh();
	caps.max_static_raster_states = 3;
	for (i = 0; i < 3; i++)
	{
		CHECK(!rdp_queue_full(&q, &caps), "static states");
		q.static_state.dither = i;
		rdp_queue_add_primitive(&q, &caps, &constants, tiles, &t, NULL);
	}
	CHECK(rdp_queue_full(&q, &caps), "as many static states as a flush holds");

	fresh();
	for (i = 0; i < RDP_QUEUE_MAX_DEPTH_BLEND_STATES; i++)
	{
		CHECK(!rdp_queue_full(&q, &caps), "depth blend states");
		q.depth_blend.flags = i << 8;
		rdp_queue_add_primitive(&q, &caps, &constants, tiles, &t, NULL);
	}
	CHECK(rdp_queue_full(&q, &caps), "64 depth blend states");

	fresh();
	q.num_tile_infos = RDP_QUEUE_MAX_TILE_INFOS - 8;
	CHECK(!rdp_queue_full(&q, &caps), "eight more tile states fit");
	q.num_tile_infos++;
	CHECK(rdp_queue_full(&q, &caps), "eight more do not");

	fresh();
	q.num_span_jobs = (RDP_QUEUE_MAX_SPAN_SETUPS - 1024) / 64;
	CHECK(!rdp_queue_full(&q, &caps), "the tallest primitive's jobs fit");
	q.num_span_jobs++;
	CHECK(rdp_queue_full(&q, &caps), "they do not");

	fresh();
	q.max_shaded_tiles = caps.max_num_tile_instances - caps.max_tiles_x * caps.max_tiles_y;
	CHECK(!rdp_queue_full(&q, &caps), "a primitive over every tile fits");
	q.max_shaded_tiles++;
	CHECK(rdp_queue_full(&q, &caps), "it does not");
	/* the ubershader shades no tiles of its own */
	fresh();
	caps.ubershader = true;
	t = triangle(0, 200);
	rdp_queue_add_primitive(&q, &caps, &constants, tiles, &t, NULL);
	CHECK(q.max_shaded_tiles == 0, "no shaded tiles with the ubershader");
	caps.ubershader = false;
	rdp_queue_add_primitive(&q, &caps, &constants, tiles, &t, NULL);
	CHECK(q.max_shaded_tiles > 0, "the split rasterizer's");

	fresh();
	memset(&up, 0, sizeof(up));
	for (i = 0; i < RDP_QUEUE_MAX_UPLOADS - 2; i++)
		CHECK(!rdp_queue_add_upload(&q, &up), "loads");
	CHECK(rdp_queue_add_upload(&q, &up), "255 loads: one more might not fit");
	CHECK(q.num_uploads == RDP_QUEUE_MAX_UPLOADS - 1, "counted");
}

static void test_upload_reads_pending(void)
{
	rdp_queue_fb_t fb;
	const uint32_t rdram = 8 << 20;

	fresh();
	fb.addr = 0x100000;
	fb.depth_addr = 0x200000;
	fb.width = 320;
	fb.fmt = 3; /* IA88 */
	q.height = 10;
	/* 3200 pixels */
	CHECK(!rdp_queue_upload_reads_pending(&q, &fb, rdram, fb.addr), "nothing pending");
	q.color_write_pending = true;
	CHECK(rdp_queue_upload_reads_pending(&q, &fb, rdram, fb.addr + 100), "its first pixels");
	CHECK(rdp_queue_upload_reads_pending(&q, &fb, rdram, fb.addr + 4000), "IA88: pixel 2000 of 3200");
	CHECK(rdp_queue_upload_reads_pending(&q, &fb, rdram, fb.addr + 6398), "IA88: the last pixel");
	CHECK(!rdp_queue_upload_reads_pending(&q, &fb, rdram, fb.addr + 6400), "IA88: past it");
	CHECK(!rdp_queue_upload_reads_pending(&q, &fb, rdram, fb.addr - 2), "before it");

	fb.fmt = 2; /* RGBA5551 */
	CHECK(rdp_queue_upload_reads_pending(&q, &fb, rdram, fb.addr + 6398), "RGBA5551: the last pixel");
	CHECK(!rdp_queue_upload_reads_pending(&q, &fb, rdram, fb.addr + 6400), "RGBA5551: past it");
	fb.fmt = 1; /* I8 */
	CHECK(rdp_queue_upload_reads_pending(&q, &fb, rdram, fb.addr + 3199), "I8: the last pixel");
	CHECK(!rdp_queue_upload_reads_pending(&q, &fb, rdram, fb.addr + 4000), "I8: past it");
	fb.fmt = 4; /* RGBA8888 */
	CHECK(rdp_queue_upload_reads_pending(&q, &fb, rdram, fb.addr + 12796), "RGBA8888: the last pixel");
	CHECK(!rdp_queue_upload_reads_pending(&q, &fb, rdram, fb.addr + 12800), "RGBA8888: past it");

	/* depth: 16-bit */
	q.color_write_pending = false;
	CHECK(!rdp_queue_upload_reads_pending(&q, &fb, rdram, fb.depth_addr), "no depth pending");
	q.depth_write_pending = true;
	CHECK(rdp_queue_upload_reads_pending(&q, &fb, rdram, fb.depth_addr + 6398), "depth: the last pixel");
	CHECK(!rdp_queue_upload_reads_pending(&q, &fb, rdram, fb.depth_addr + 6400), "depth: past it");
	/* RDRAM wraps */
	fb.depth_addr = rdram - 100;
	CHECK(rdp_queue_upload_reads_pending(&q, &fb, rdram, 50), "around the end of RDRAM");
}

/* ---- tile loads ---- */
static unsigned flushes;

static void flush_queue(void *ctx)
{
	flushes++;
	rdp_queue_reset((rdp_queue_t *)ctx);
}

/* A 16-bit RGBA load of @width x @height texels */
static rdp_load_tile_info_t load_of(unsigned width, unsigned height, unsigned size)
{
	rdp_load_tile_info_t info;
	memset(&info, 0, sizeof(info));
	info.tex_addr = 0x10000;
	info.tex_width = width;
	info.slo = 0;
	info.tlo = 0;
	info.shi = (uint16_t)((width - 1) << 2);
	info.thi = (uint16_t)((height - 1) << 2);
	info.fmt = RDP_TEXTURE_FORMAT_RGBA;
	info.size = (uint8_t)size;
	info.mode = RDP_UPLOAD_TILE;
	return info;
}

static void test_load_tile(void)
{
	rdp_load_tile_info_t info;

	fresh();
	tiles[2].meta.fmt = RDP_TEXTURE_FORMAT_RGBA;
	tiles[2].meta.size = RDP_TEXTURE_SIZE_16;
	tiles[2].meta.stride = 64;
	tiles[2].meta.offset = 0x100;

	/* 32x16, 1 KiB: in one */
	info = load_of(32, 16, RDP_TEXTURE_SIZE_16);
	CHECK(rdp_queue_load_tile(&q, tiles, 2, &info, flush_queue, &q) == 0, "no crash");
	CHECK(q.num_uploads == 1 && q.uploads[0].height == 16 && q.uploads[0].tmem_offset == 0x100, "one load");
	CHECK(tiles[2].size.thi == info.thi && tiles[2].size.shi == info.shi, "the tile's size");

	/* 64x64, 8 KiB: two parts of 32 lines, TMEM wrapping each */
	fresh();
	tiles[2].meta.fmt = RDP_TEXTURE_FORMAT_RGBA;
	tiles[2].meta.size = RDP_TEXTURE_SIZE_16;
	tiles[2].meta.stride = 64;
	info = load_of(64, 64, RDP_TEXTURE_SIZE_16);
	info.tlo = 4 << 2;
	info.thi = (uint16_t)(info.tlo + (63 << 2));
	CHECK(rdp_queue_load_tile(&q, tiles, 2, &info, flush_queue, &q) == 0, "no crash");
	CHECK(q.num_uploads == 2, "two parts");
	CHECK(q.uploads[0].height == 32 && q.uploads[1].height == 32, "32 lines each");
	CHECK(q.uploads[0].tmem_offset == 0 && q.uploads[1].tmem_offset == 32 * 64, "the second part on in TMEM");
	CHECK(q.uploads[0].vram_addr == 0x10000 + 4 * 64 * 2 && q.uploads[1].vram_addr == 0x10000 + 36 * 64 * 2,
	      "each part from its own lines");
	CHECK(tiles[2].size.tlo == info.tlo && tiles[2].size.thi == info.thi, "the tile's size is the whole load's");

	/* 4-bit: the RDP crashes, nothing loaded */
	fresh();
	tiles[1].meta.size = RDP_TEXTURE_SIZE_16;
	tiles[1].meta.stride = 64;
	info = load_of(32, 16, 0);
	CHECK(rdp_queue_load_tile(&q, tiles, 1, &info, flush_queue, &q) == 1 && q.num_uploads == 0, "a crash");

	/* a line wider than TMEM: nothing */
	fresh();
	tiles[1].meta.size = RDP_TEXTURE_SIZE_16;
	tiles[1].meta.stride = 64;
	info = load_of(3000, 4, RDP_TEXTURE_SIZE_16);
	CHECK(rdp_queue_load_tile(&q, tiles, 1, &info, flush_queue, &q) == 0 && q.num_uploads == 0, "too wide");
	CHECK(tiles[1].size.shi == 0, "the tile as it was");

	/* the loads one flush holds: it goes */
	fresh();
	tiles[2].meta.size = RDP_TEXTURE_SIZE_16;
	tiles[2].meta.stride = 64;
	flushes = 0;
	q.num_uploads = RDP_QUEUE_MAX_UPLOADS - 3;
	info = load_of(32, 16, RDP_TEXTURE_SIZE_16);
	rdp_queue_load_tile(&q, tiles, 2, &info, flush_queue, &q);
	CHECK(flushes == 0 && q.num_uploads == RDP_QUEUE_MAX_UPLOADS - 2, "room for one more");
	rdp_queue_load_tile(&q, tiles, 2, &info, flush_queue, &q);
	CHECK(flushes == 1 && q.num_uploads == 0, "flushed");
}

int main(void)
{
	test_spans();
	test_attributes();
	test_state_caches();
	test_full();
	test_upload_reads_pending();
	test_load_tile();
	if (failures)
	{
		fprintf(stderr, "test_rdp_queue: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_rdp_queue: all checks passed\n");
	return 0;
}
