/* parallel-rdp's state commands (parallel-rdp/rdp_command_decode.c),
 * against their bit layout in the RDP's command reference.
 *
 * Each field is taken from where the command puts it and nowhere else:
 * every case sets one field to all ones and checks that only it comes
 * out set. Set Other Modes changes only the flags it owns, Set Tile
 * clamps masks of 0 and limits them to 10, and Set Convert's K2 spans
 * the two words. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rdp_command_decode.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_rdp_command_decode: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

static void test_color_image(void)
{
	uint32_t w[2], addr, width, fmt;
	unsigned size;

	/* RGBA, 16-bit, 320 wide, at 0x123456 (the top byte ignored) */
	w[0] = (0x3fu << 24) | (0u << 21) | (2u << 19) | 319u;
	w[1] = 0xff123456u;
	rdp_decode_color_image(w, &addr, &width, &fmt, &size);
	CHECK(addr == 0x123456 && width == 320 && fmt == 2 && size == 2, "RGBA5551");
	w[0] = (3u << 21) | (2u << 19) | 1023u;
	rdp_decode_color_image(w, &addr, &width, &fmt, &size);
	CHECK(width == 1024 && fmt == 3, "IA88: any format but RGBA");
	w[0] = (2u << 19) | (1u << 21);
	rdp_decode_color_image(w, &addr, &width, &fmt, &size);
	CHECK(fmt == 3 && width == 1, "16-bit, format 1: IA88");
	w[0] = 3u << 19;
	rdp_decode_color_image(w, &addr, &width, &fmt, &size);
	CHECK(fmt == 4 && size == 3, "RGBA8888");
	w[0] = 1u << 19;
	rdp_decode_color_image(w, &addr, &width, &fmt, &size);
	CHECK(fmt == 1 && size == 1, "I8");
	w[0] = 0;
	rdp_decode_color_image(w, &addr, &width, &fmt, &size);
	CHECK(fmt == 0 && size == 0, "I4");
}

static void test_scissor(void)
{
	rdp_scissor_state_t s;
	uint32_t w[2], flags = ~0u;

	w[0] = (0xabcu << 12) | 0x123u;
	w[1] = (0xdefu << 12) | 0x456u;
	rdp_decode_scissor(w, &s, &flags);
	CHECK(s.xlo == 0xabc && s.ylo == 0x123 && s.xhi == 0xdef && s.yhi == 0x456, "the box");
	CHECK(!(flags & RASTERIZATION_INTERLACE_FIELD_BIT) && !(flags & RASTERIZATION_INTERLACE_KEEP_ODD_BIT) &&
	      (flags | RASTERIZATION_INTERLACE_FIELD_BIT | RASTERIZATION_INTERLACE_KEEP_ODD_BIT) == ~0u,
	      "the interlace bits off, the rest kept");
	w[1] |= 1u << 25;
	rdp_decode_scissor(w, &s, &flags);
	CHECK((flags & RASTERIZATION_INTERLACE_FIELD_BIT) && !(flags & RASTERIZATION_INTERLACE_KEEP_ODD_BIT), "field");
	w[1] = 1u << 24;
	rdp_decode_scissor(w, &s, &flags);
	CHECK(!(flags & RASTERIZATION_INTERLACE_FIELD_BIT) && (flags & RASTERIZATION_INTERLACE_KEEP_ODD_BIT), "keep odd");
}

static void test_other_modes(void)
{
	static const struct
	{
		unsigned word, bit;
		uint32_t static_flag, depth_flag;
	} bits[] = {
		{ 0, 19, RASTERIZATION_PERSPECTIVE_CORRECT_BIT, 0 },
		{ 0, 18, RASTERIZATION_DETAIL_LOD_ENABLE_BIT, 0 },
		{ 0, 17, RASTERIZATION_SHARPEN_LOD_ENABLE_BIT, 0 },
		{ 0, 16, RASTERIZATION_TEX_LOD_ENABLE_BIT, 0 },
		{ 0, 15, RASTERIZATION_TLUT_BIT, 0 },
		{ 0, 14, RASTERIZATION_TLUT_TYPE_BIT, 0 },
		{ 0, 13, RASTERIZATION_SAMPLE_MODE_BIT, 0 },
		{ 0, 12, RASTERIZATION_SAMPLE_MID_TEXEL_BIT, 0 },
		{ 0, 11, RASTERIZATION_BILERP_0_BIT, 0 },
		{ 0, 10, RASTERIZATION_BILERP_1_BIT, 0 },
		{ 0, 9, RASTERIZATION_CONVERT_ONE_BIT, 0 },
		{ 1, 14, 0, DEPTH_BLEND_FORCE_BLEND_BIT },
		{ 1, 13, RASTERIZATION_ALPHA_CVG_SELECT_BIT, 0 },
		{ 1, 12, RASTERIZATION_CVG_TIMES_ALPHA_BIT, 0 },
		{ 1, 7, 0, DEPTH_BLEND_COLOR_ON_COVERAGE_BIT },
		{ 1, 6, 0, DEPTH_BLEND_IMAGE_READ_ENABLE_BIT },
		{ 1, 5, 0, DEPTH_BLEND_DEPTH_UPDATE_BIT },
		{ 1, 4, 0, DEPTH_BLEND_DEPTH_TEST_BIT },
		{ 1, 3, RASTERIZATION_AA_BIT, DEPTH_BLEND_AA_BIT },
		{ 1, 1, RASTERIZATION_ALPHA_TEST_DITHER_BIT, 0 },
		{ 1, 0, RASTERIZATION_ALPHA_TEST_BIT, 0 },
	};
	rdp_static_state_t s;
	rdp_depth_blend_state_t d;
	uint32_t w[2];
	unsigned i;

	for (i = 0; i < sizeof(bits) / sizeof(bits[0]); i++)
	{
		memset(&s, 0, sizeof(s));
		memset(&d, 0, sizeof(d));
		w[0] = 0;
		/* dither off: no dither flag */
		w[0] = (uint32_t)RDP_RGB_DITHER_OFF << 6;
		w[1] = 0;
		w[bits[i].word] |= 1u << bits[i].bit;
		CHECK(!rdp_decode_other_modes(w, &s, &d), "no primitive depth");
		CHECK(s.flags == bits[i].static_flag, "only its static flag");
		CHECK(d.flags == bits[i].depth_flag, "only its depth blend flag");
	}

	/* the flags it does not own are kept */
	memset(&s, 0, sizeof(s));
	memset(&d, 0, sizeof(d));
	s.flags = RASTERIZATION_INTERLACE_FIELD_BIT;
	w[0] = w[1] = 0;
	rdp_decode_other_modes(w, &s, &d);
	CHECK(s.flags == RASTERIZATION_INTERLACE_FIELD_BIT, "the interlace flag kept");

	/* cycle types */
	w[0] = (uint32_t)RDP_CYCLE_2 << 20;
	rdp_decode_other_modes(w, &s, &d);
	CHECK((s.flags & RASTERIZATION_MULTI_CYCLE_BIT) && (d.flags & DEPTH_BLEND_MULTI_CYCLE_BIT), "2-cycle");
	w[0] = (uint32_t)RDP_CYCLE_FILL << 20;
	rdp_decode_other_modes(w, &s, &d);
	CHECK((s.flags & RASTERIZATION_FILL_BIT) && !(s.flags & RASTERIZATION_MULTI_CYCLE_BIT) &&
	      !(d.flags & DEPTH_BLEND_MULTI_CYCLE_BIT), "fill, the 2-cycle bits gone");
	w[0] = (uint32_t)RDP_CYCLE_COPY << 20;
	rdp_decode_other_modes(w, &s, &d);
	CHECK((s.flags & RASTERIZATION_COPY_BIT) && !(s.flags & RASTERIZATION_FILL_BIT), "copy");
	w[0] = 0;
	rdp_decode_other_modes(w, &s, &d);
	CHECK(!(s.flags & (RASTERIZATION_COPY_BIT | RASTERIZATION_FILL_BIT | RASTERIZATION_MULTI_CYCLE_BIT)), "1-cycle");

	/* dither, coverage and z modes, the blender, primitive depth */
	w[0] = 0xfu << 4;
	w[1] = (3u << 8) | (3u << 10) | (1u << 2);
	CHECK(rdp_decode_other_modes(w, &s, &d), "primitive depth");
	CHECK(s.dither == 0xf && !(d.flags & DEPTH_BLEND_DITHER_ENABLE_BIT), "RGB dither off");
	CHECK(d.coverage_mode == 3 && d.z_mode == 3, "coverage and z modes");
	w[0] = 0;
	w[1] = 0;
	rdp_decode_other_modes(w, &s, &d);
	CHECK(s.dither == 0 && (d.flags & DEPTH_BLEND_DITHER_ENABLE_BIT) && d.coverage_mode == 0, "dither on");

	w[1] = 3u << 30;
	rdp_decode_other_modes(w, &s, &d);
	CHECK(d.blend_cycles[0].blend_1a == 3 && d.blend_cycles[1].blend_1a == 0 && d.blend_cycles[0].blend_2b == 0,
	      "cycle 0's 1a");
	w[1] = 3u << 16;
	rdp_decode_other_modes(w, &s, &d);
	CHECK(d.blend_cycles[1].blend_2b == 3 && d.blend_cycles[0].blend_1a == 0, "cycle 1's 2b");
	w[1] = (1u << 28) | (2u << 26) | (3u << 24) | (1u << 22) | (2u << 20) | (3u << 18);
	rdp_decode_other_modes(w, &s, &d);
	CHECK(d.blend_cycles[1].blend_1a == 1 && d.blend_cycles[0].blend_1b == 2 && d.blend_cycles[1].blend_1b == 3 &&
	      d.blend_cycles[0].blend_2a == 1 && d.blend_cycles[1].blend_2a == 2 && d.blend_cycles[0].blend_2b == 3,
	      "every input");
}

static void test_texture_image(void)
{
	rdp_texture_image_t t;
	uint32_t w[2];
	w[0] = (4u << 21) | (3u << 19) | 0x3ffu;
	w[1] = 0xab654321u;
	rdp_decode_texture_image(w, &t);
	CHECK(t.fmt == 4 && t.size == 3 && t.width == 1024 && t.addr == 0x654321, "texture image");
}

static void test_tile(void)
{
	rdp_tile_meta_t m;
	rdp_tile_size_t z;
	uint32_t w[2];

	w[0] = (2u << 21) | (1u << 19) | (0x1ffu << 9) | 0x101u;
	w[1] = (5u << 24) | (9u << 20) | (7u << 14) | (3u << 10) | (6u << 4) | 2u;
	CHECK(rdp_decode_tile(w, &m) == 5, "tile 5");
	CHECK(m.fmt == 2 && m.size == 1 && m.stride == 0x1ff * 8 && m.offset == 0x101 * 8, "format, size, stride, offset");
	CHECK(m.palette == 9 && m.mask_t == 7 && m.shift_t == 3 && m.mask_s == 6 && m.shift_s == 2, "palette, masks, shifts");
	CHECK(m.flags == 0, "no flags");

	w[1] = (1u << 8) | (1u << 9) | (1u << 18) | (1u << 19) | (15u << 14) | (12u << 4);
	rdp_decode_tile(w, &m);
	CHECK(m.flags == (TILE_INFO_MIRROR_S_BIT | TILE_INFO_CLAMP_S_BIT | TILE_INFO_MIRROR_T_BIT | TILE_INFO_CLAMP_T_BIT),
	      "mirror and clamp");
	CHECK(m.mask_s == 10 && m.mask_t == 10, "masks to 10");
	w[1] = 0;
	rdp_decode_tile(w, &m);
	CHECK(m.flags == (TILE_INFO_CLAMP_S_BIT | TILE_INFO_CLAMP_T_BIT) && m.mask_s == 0, "mask 0 clamps");
	w[1] = 1u << 4;
	rdp_decode_tile(w, &m);
	CHECK(m.flags == TILE_INFO_CLAMP_T_BIT, "only T clamps");

	w[0] = (0xabcu << 12) | 0x123u;
	w[1] = (7u << 24) | (0xdefu << 12) | 0x456u;
	CHECK(rdp_decode_tile_size(w, &z) == 7, "tile 7");
	CHECK(z.slo == 0xabc && z.tlo == 0x123 && z.shi == 0xdef && z.thi == 0x456, "tile size");
}

static void test_combine(void)
{
	rdp_static_state_t s;
	uint32_t w[2];

	memset(&s, 0, sizeof(s));
	w[0] = 0xfu << 20;
	w[1] = 0;
	rdp_decode_combine(w, &s);
	CHECK(s.combiner[0].rgb.muladd == 0xf && s.combiner[0].rgb.mul == 0 && s.combiner[1].rgb.muladd == 0, "0 a");
	w[0] = (0x1fu << 15) | (7u << 12) | (7u << 9) | (0xfu << 5) | 0x1fu;
	w[1] = 0;
	rdp_decode_combine(w, &s);
	CHECK(s.combiner[0].rgb.muladd == 0 && s.combiner[0].rgb.mul == 0x1f && s.combiner[0].alpha.muladd == 7 &&
	      s.combiner[0].alpha.mul == 7 && s.combiner[1].rgb.muladd == 0xf && s.combiner[1].rgb.mul == 0x1f,
	      "the first word");
	w[0] = 0;
	w[1] = (0xfu << 28) | (0xfu << 24) | (7u << 21) | (7u << 18) | (7u << 15) | (7u << 12) | (7u << 9) |
	       (7u << 6) | (7u << 3) | 7u;
	rdp_decode_combine(w, &s);
	CHECK(s.combiner[0].rgb.mulsub == 0xf && s.combiner[1].rgb.mulsub == 0xf && s.combiner[1].alpha.muladd == 7 &&
	      s.combiner[1].alpha.mul == 7 && s.combiner[0].rgb.add == 7 && s.combiner[0].alpha.mulsub == 7 &&
	      s.combiner[0].alpha.add == 7 && s.combiner[1].rgb.add == 7 && s.combiner[1].alpha.mulsub == 7 &&
	      s.combiner[1].alpha.add == 7, "the second word");
	CHECK(s.combiner[0].rgb.muladd == 0 && s.combiner[0].rgb.mul == 0, "the first word's gone");
	w[1] = 7u << 15;
	rdp_decode_combine(w, &s);
	CHECK(s.combiner[0].rgb.add == 7 && s.combiner[1].rgb.add == 0 && s.combiner[0].alpha.mulsub == 0, "0 rgb add");
}

static void test_convert_keys_prim(void)
{
	rdp_color_key_t r, g, b;
	uint16_t k[6];
	uint32_t w[2];
	uint8_t level, frac;
	uint32_t color;
	unsigned i;

	/* K0..K5 = 0x101, 0x102, ... as one 64-bit value */
	{
		uint64_t m = 0;
		for (i = 0; i < 6; i++)
			m |= (uint64_t)(0x101 + i) << (45 - 9 * i);
		w[0] = (uint32_t)(m >> 32);
		w[1] = (uint32_t)m;
	}
	rdp_decode_convert(w, k);
	for (i = 0; i < 6; i++)
		CHECK(k[i] == 0x101 + i, "K0 to K5");
	/* K2 alone: its 9 bits across both words */
	w[0] = 0xfu;
	w[1] = 0x1fu << 27;
	rdp_decode_convert(w, k);
	CHECK(k[2] == 0x1ff && k[1] == 0 && k[3] == 0, "K2 across the words");

	w[0] = (0xabcu << 12) | 0xdefu;
	w[1] = 0x11223344u;
	rdp_decode_key_gb(w, &g, &b);
	CHECK(g.width == 0xabc && g.center == 0x11 && g.scale == 0x22, "green");
	CHECK(b.width == 0xdef && b.center == 0x33 && b.scale == 0x44, "blue");
	w[1] = (0xfedu << 16) | 0x5566u;
	rdp_decode_key_r(w, &r);
	CHECK(r.width == 0xfed && r.center == 0x55 && r.scale == 0x66, "red");

	w[0] = (0x1fu << 8) | 0xabu;
	w[1] = 0xdeadbeefu;
	rdp_decode_prim_color(w, &level, &frac, &color);
	CHECK(level == 0x1f && frac == 0xab && color == 0xdeadbeefu, "primitive colour");
}

int main(void)
{
	test_color_image();
	test_scissor();
	test_other_modes();
	test_texture_image();
	test_tile();
	test_combine();
	test_convert_keys_prim();
	if (failures)
	{
		fprintf(stderr, "test_rdp_command_decode: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_rdp_command_decode: all checks passed\n");
	return 0;
}
