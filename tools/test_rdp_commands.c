/* test_rdp_commands -- parallel-RDP's commands run (parallel-rdp/rdp_commands.c),
 * into the renderer on a stand-in device.
 *
 * Each command's decoding (rdp_command_decode.c, primitive_decode.c,
 * checked on their own) reaches the renderer's state it sets, and the
 * temporal upscaler's view of it: the color and mask images, other
 * modes, the combiner, the scissor, tiles and tile sizes, the loads from
 * the texture image, the colors, the primitive depth, convert and the
 * keys; the primitives are queued, decoded by the cycle type and the
 * quirks (only the two there are); sync full submits what is queued;
 * the processor's own opcodes (1 to 7), the syncs that order nothing
 * here and the opcodes the RDP does not have change nothing.
 *
 * Built by `make check-core` (top-level Makefile). Exit status 0 on pass.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <boolean.h>

#include "rdp_commands.h"
#include "primitive_decode.h"
#include "tmem_load.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_rdp_commands: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

/* (32 slots of stream buffers, and the rest) */
#define MAX_BUFFERS 1024
#include "vk_test_device.h"

/* video_interface.c's: none here */
void rdp_vi_queue_known_pipelines(rdp_vi_t *vi, unsigned upscaling, bool with_upscaler,
		rdp_known_pipeline_fn add, void *ctx)
{
	(void)vi; (void)upscaling; (void)with_upscaler; (void)add; (void)ctx;
}

#define RDRAM_SIZE (1u << 20)

static int no_defines(void *user, const char *name, const char *define)
{
	(void)user; (void)name; (void)define;
	return 0;
}

static rdp_shader_bank_t bank;
static rdp_timeline_t timeline;
static granite_buffer_t *rdram, *hidden, *tmem;
static rdp_renderer_t r;
static rdp_commands_t c;

static granite_buffer_t *buffer(VkDeviceSize size)
{
	granite_buffer_info_t info;
	info.domain = GRANITE_BUFFER_DOMAIN_DEVICE;
	info.size = size;
	info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	info.misc = 0;
	return granite_device_create_buffer(&dev, &info, NULL);
}

static void open_renderer(void)
{
	rdp_renderer_options_t options;

	fill_context();
	context.gpu_props.limits.maxStorageBufferRange = 0xffffffffu;
	context.ext.storage_16bit_features.storageBuffer16BitAccess = VK_TRUE;
	context.ext.storage_8bit_features.storageBuffer8BitAccess = VK_TRUE;
	context.ext.subgroup_properties.subgroupSize = 32;
	granite_device_init(&dev);
	granite_device_set_context(&dev, &context);
	rdp_shader_bank_init(&bank, &dev, no_defines, NULL);
	CHECK(rdp_timeline_init(&timeline, &dev), "the timeline worker starts");
	rdram = buffer(RDRAM_SIZE);
	hidden = buffer(RDRAM_SIZE / 2);
	tmem = buffer(0x1000);

	rdp_renderer_init(&r, &timeline);
	rdp_renderer_set_device(&r, &dev);
	rdp_renderer_set_shader_bank(&r, &bank);
	rdp_renderer_set_rdram(&r, rdram, NULL, 0, RDRAM_SIZE, true);
	rdp_renderer_set_hidden_rdram(&r, hidden);
	rdp_renderer_set_tmem(&r, tmem);
	memset(&options, 0, sizeof(options));
	options.upscaling_factor = 1;
	CHECK(rdp_renderer_init_renderer(&r, &options), "the renderer");
}

static void close_renderer(void)
{
	unsigned i;
	rdp_renderer_flush_and_signal(&r);
	rdp_renderer_free(&r);
	rdp_timeline_free(&timeline);
	granite_buffer_unref(rdram);
	granite_buffer_unref(hidden);
	granite_buffer_unref(tmem);
	granite_device_free(&dev);
	for (i = 0; i < num_memory; i++)
		free(memory_data[i]);
}

static void run2(uint32_t w0, uint32_t w1)
{
	uint32_t words[2];
	words[0] = w0;
	words[1] = w1;
	rdp_commands_run(&c, words);
}

static void test_nothing(void)
{
	static rdp_renderer_t before;
	static rdp_commands_t c_before;
	static const unsigned ops[] = {
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x10, 0x17, 0x1f, 0x20, 0x23, 0x26, 0x27, 0x28, 0x31
	};
	unsigned i;

	memcpy(&before, &r, sizeof(r));
	memcpy(&c_before, &c, sizeof(c));
	for (i = 0; i < sizeof(ops) / sizeof(ops[0]); i++)
		run2((ops[i] << 24) | 0xffffff, 0xffffffffu);
	CHECK(!memcmp(&before, &r, sizeof(r)) && !memcmp(&c_before, &c, sizeof(c)),
			"the processor's own opcodes, the syncs that order nothing here and those the RDP has not change nothing");
}

static void test_state(void)
{
	uint32_t words[2], addr, width, fmt;
	unsigned size, tile;
	rdp_static_state_t st;
	rdp_depth_blend_state_t db;
	rdp_scissor_state_t sc;
	rdp_tile_meta_t meta;
	rdp_tile_size_t ts;
	uint16_t k[6];
	bool prim_depth;

	/* the color image */
	words[0] = 0x3f000000u | (0u << 21) | (2u << 19) | 319;
	words[1] = 0x00123400u;
	rdp_commands_run(&c, words);
	rdp_decode_color_image(words, &addr, &width, &fmt, &size);
	CHECK(r.fb.addr == addr && r.fb.width == width && r.fb.fmt == fmt && width == 320 && fmt == RDP_FB_RGBA5551,
			"set color image");
	run2(0x3e000000u, 0xff234560u);
	CHECK(r.fb.depth_addr == 0x234560, "set mask image, 24 bits of address");

	/* other modes, the combiner */
	words[0] = 0x2f000000u | (1u << 20) | 0x00080000u | 0x3c;
	words[1] = 0x0f0a4000u | (1u << 4) | (1u << 5) | (1u << 2);
	memset(&st, 0, sizeof(st));
	memset(&db, 0, sizeof(db));
	rdp_commands_run(&c, words);
	prim_depth = rdp_decode_other_modes(words, &st, &db);
	CHECK(r.queue.static_state.flags == st.flags && r.queue.static_state.dither == st.dither
			&& !memcmp(&r.queue.depth_blend, &db, sizeof(db)), "set other modes");
	CHECK(prim_depth && r.constants.use_prim_depth && c.upscaler_view.prim_depth, "with the primitive depth");
	CHECK(c.upscaler_view.other_modes[0] == words[0] && c.upscaler_view.other_modes[1] == words[1],
			"the upscaler keeps its words");
	words[0] = 0x3c121824u;
	words[1] = 0xff33ffffu;
	rdp_commands_run(&c, words);
	rdp_decode_combine(words, &st);
	CHECK(!memcmp(r.queue.static_state.combiner, st.combiner, sizeof(st.combiner)) && st.flags == r.queue.static_state.flags,
			"set combine, over the other modes");
	CHECK(c.upscaler_view.combine[0] == words[0] && c.upscaler_view.combine[1] == words[1], "kept too");

	/* the scissor, its interlace bits in the static state */
	words[0] = 0x2d000000u | (8u << 12) | 4u;
	words[1] = (1u << 25) | (1u << 24) | (1280u << 12) | 960u;
	rdp_commands_run(&c, words);
	rdp_decode_scissor(words, &sc, &st.flags);
	CHECK(!memcmp(&r.queue.scissor, &sc, sizeof(sc)) && r.queue.static_state.flags == st.flags, "set scissor");

	/* tiles */
	words[0] = 0x35000000u | (2u << 21) | (2u << 19) | (8u << 9) | 0x40;
	words[1] = (5u << 24) | (1u << 20) | 0x12345u;
	rdp_commands_run(&c, words);
	memset(&meta, 0, sizeof(meta));
	tile = rdp_decode_tile(words, &meta);
	CHECK(tile == 5 && !memcmp(&r.tiles[5].meta, &meta, sizeof(meta)), "set tile");
	CHECK(c.upscaler_view.tile[5][0] == words[0] && c.upscaler_view.tile[5][1] == words[1], "kept by its index");
	words[0] = 0x32000000u | (4u << 12) | 8u;
	words[1] = (5u << 24) | (124u << 12) | 60u;
	rdp_commands_run(&c, words);
	tile = rdp_decode_tile_size(words, &ts);
	CHECK(tile == 5 && r.tiles[5].size.slo == ts.slo && r.tiles[5].size.tlo == ts.tlo && r.tiles[5].size.shi == ts.shi
			&& r.tiles[5].size.thi == ts.thi && ts.shi == 124, "set tile size");

	/* a load from the texture image */
	run2(0x3d000000u | (2u << 19) | 31u, 0x00200000u);
	CHECK(c.texture_image.addr == 0x200000 && c.texture_image.width == 32, "set texture image");
	run2(0x34000000u, (5u << 24) | (31u << 2 << 12) | (7u << 2));
	CHECK(r.queue.num_uploads >= 1 && c.upscaler_view.load_addr == 0x200000
			&& r.queue.uploads[r.queue.num_uploads - 1].mode == RDP_UPLOAD_TILE
			&& r.queue.uploads[r.queue.num_uploads - 1].vram_addr == 0x200000, "load tile, from the texture image");
	run2(0x3d000000u | (2u << 19) | 31u, 0x00300000u);
	run2(0x33000000u, (5u << 24) | (63u << 12) | 0x800u);
	CHECK(r.queue.num_uploads >= 2 && c.upscaler_view.load_addr == 0x300000
			&& r.queue.uploads[r.queue.num_uploads - 1].mode == RDP_UPLOAD_BLOCK, "load block");
	run2(0x3d000000u | (2u << 19) | 0u, 0x00310000u);
	run2(0x30000000u, (5u << 24) | (15u << 2 << 12));
	CHECK(r.queue.num_uploads >= 3 && c.upscaler_view.load_addr == 0x310000
			&& r.queue.uploads[r.queue.num_uploads - 1].mode == RDP_UPLOAD_TLUT, "load TLUT");

	/* the colors, the primitive depth, convert and the keys */
	run2(0x37000000u, 0x11111111u);
	run2(0x38000000u, 0x22222222u);
	run2(0x39000000u, 0x33333333u);
	run2(0x3b000000u, 0x44444444u);
	run2(0x3a000000u | (3u << 8) | 0x80u, 0x55555555u);
	CHECK(r.constants.fill_color == 0x11111111u && r.constants.fog_color == 0x22222222u
			&& r.constants.blend_color == 0x33333333u && r.constants.env_color == 0x44444444u
			&& r.constants.primitive_color == 0x55555555u && r.constants.min_level == 3
			&& r.constants.prim_lod_frac == 0x80, "the colors");
	run2(0x2e000000u, 0x80011234u);
	CHECK(r.constants.prim_depth == 0x10000 && r.constants.prim_dz == 0x1234 && c.upscaler_view.prim_z == 0x10000,
			"set primitive depth, for the renderer and the upscaler alike");
	words[0] = 0x2c000000u | (0x1ffu << 13) | (0x0ffu << 4) | 0x1;
	words[1] = 0x23456789u;
	rdp_commands_run(&c, words);
	rdp_decode_convert(words, k);
	{
		rdp_renderer_t expect;
		rdp_renderer_init(&expect, &timeline);
		rdp_renderer_set_convert(&expect, k[0], k[1], k[2], k[3], k[4], k[5]);
		CHECK(!memcmp(r.constants.convert, expect.constants.convert, sizeof(expect.constants.convert)), "set convert");
		rdp_renderer_free(&expect);
	}
	run2(0x2a000000u | (0x123u << 12) | 0x456u, 0x11223344u);
	run2(0x2b000000u | 0x789u, 0x00005566u);
	{
		rdp_color_key_t kg, kb, kr;
		words[0] = 0x2a000000u | (0x123u << 12) | 0x456u;
		words[1] = 0x11223344u;
		rdp_decode_key_gb(words, &kg, &kb);
		words[0] = 0x2b000000u | 0x789u;
		words[1] = 0x00005566u;
		rdp_decode_key_r(words, &kr);
		CHECK(r.constants.key_width[0] == kr.width && r.constants.key_center[0] == kr.center
				&& r.constants.key_scale[0] == kr.scale && r.constants.key_width[1] == kg.width
				&& r.constants.key_center[1] == kg.center && r.constants.key_width[2] == kb.width
				&& r.constants.key_scale[2] == kb.scale, "set key R and GB, each component in its place");
	}
}

/* the last primitive queued */
static const rdp_triangle_setup_t *last(void)
{
	return &r.queue.triangles[r.queue.num_primitives - 1];
}

static void texture_rectangle(unsigned op)
{
	uint32_t words[4];
	words[0] = (op << 24) | (100u << 2 << 12) | (100u << 2);
	words[1] = (2u << 24) | (10u << 2 << 12) | (10u << 2);
	words[2] = 0;
	words[3] = 0x04000400u;
	rdp_commands_run(&c, words);
}

static void test_primitives(void)
{
	unsigned before, submits;
	uint32_t tri[8];

	/* (a 1-cycle mode, the whole screen) */
	run2(0x2f000000u, 0);
	run2(0x2d000000u, (320u << 2 << 12) | (240u << 2));
	run2(0x3f000000u | (2u << 19) | 319, 0x100000u);

	rdp_commands_set_quirks(&c, 0);
	before = r.queue.num_primitives;
	texture_rectangle(0x24);
	CHECK(r.queue.num_primitives == before + 1 && !(last()->flags & TRIANGLE_SETUP_DISABLE_UPSCALING_BIT)
			&& !(last()->flags & TRIANGLE_SETUP_NATIVE_LOD_BIT) && last()->tile == 2,
			"a texture rectangle is queued, upscaled without the quirk");
	texture_rectangle(0x25);
	CHECK(r.queue.num_primitives == before + 2 && (last()->flags & TRIANGLE_SETUP_DISABLE_UPSCALING_BIT),
			"a flipped one is never upscaled");
	rdp_commands_set_quirks(&c, ~0u);
	CHECK(c.quirks == (RDP_DECODE_NATIVE_TEX_RECT | RDP_DECODE_NATIVE_TEXTURE_LOD), "only the two quirks are taken");
	texture_rectangle(0x24);
	CHECK((last()->flags & TRIANGLE_SETUP_DISABLE_UPSCALING_BIT) && (last()->flags & TRIANGLE_SETUP_NATIVE_LOD_BIT),
			"with the quirks, at native resolution and LOD");
	rdp_commands_set_quirks(&c, RDP_DECODE_NATIVE_TEX_RECT);

	/* the cycle type as the primitive is decoded by it */
	run2(0x2f000000u | (2u << 20), 0);
	texture_rectangle(0x24);
	CHECK((last()->flags & TRIANGLE_SETUP_SKIP_XFRAC_BIT) && (last()->yl & 3) == 3, "in copy mode");
	run2(0x2f000000u | (3u << 20), 0);
	before = r.queue.num_primitives;
	run2(0x36000000u | (50u << 2 << 12) | (50u << 2), (10u << 2 << 12) | (10u << 2));
	CHECK(r.queue.num_primitives == before + 1 && (last()->yl & 3) == 3, "a fill rectangle, in fill mode");

	/* a shaded triangle, 0x0c: 8 words of edges, 16 of shade */
	run2(0x2f000000u, 0);
	memset(tri, 0, sizeof(tri));
	{
		uint32_t words[24];
		memset(words, 0, sizeof(words));
		/* yl, then ym and yh */
		words[0] = 0x0c000000u | (140u << 2);
		words[1] = (90u << 2 << 16) | (40u << 2);
		words[2] = 60u << 16;
		words[4] = 100u << 16;
		words[6] = 100u << 16;
		before = r.queue.num_primitives;
		rdp_commands_run(&c, words);
		CHECK(r.queue.num_primitives == before + 1 && last()->yh == 40 * 4 && last()->yl == 140 * 4, "a triangle");
	}

	/* sync full */
	submits = submits_made;
	run2(0x29000000u, 0);
	CHECK(r.queue.num_primitives == 0 && submits_made == submits + 1, "sync full submits what is queued");
}

int main(void)
{
	unsetenv("PARALLEL_RDP_UBERSHADER");
	setenv("PARALLEL_RDP_NO_KNOWN_PIPELINES", "1", 1);
	open_renderer();
	rdp_commands_init(&c, &r, NULL);
	CHECK(c.quirks == RDP_DECODE_NATIVE_TEX_RECT && c.renderer == &r, "texture rectangles at native resolution to start");
	test_nothing();
	test_state();
	test_nothing();
	test_primitives();
	close_renderer();

	if (failures)
	{
		fprintf(stderr, "test_rdp_commands: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_rdp_commands: ok\n");
	return 0;
}
