/* What parallel-RDP works out of its state for each primitive
 * (parallel-rdp/rdp_state.c).
 *
 * - The derived setup: the constant registers each combiner input names
 *   (primitive, environment, their alphas, the LOD fraction, the convert
 *   and key constants), fog and blend colour, the fill colour, the depth
 *   slope as a power of two from the primitive's own dz/dx and dz/dy or
 *   the primitive depth register, and its compressed form.
 * - How many 8x8 tiles a primitive can cover, inside the scissor and at
 *   the scaling factor, assuming the whole scissor when its X overflows
 *   the rasterizer, including at the int32 extremes; a primitive whose
 *   top is above the screen is counted from where it is.
 * - The setup's fixes: an YM above the first line, interlace, fill and
 *   copy.
 * - Which texels the combiner uses in one and two cycle mode, and a
 *   static texture format when all tiles sampled agree.
 * - Noise from dither, the combiner and alpha dither.
 * - The state normalized: fill keeps nothing, copy a few flags, the
 *   combiner's constant inputs read as zero. */
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <boolean.h>

#include "rdp_state.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_rdp_state: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

static void combiner(rdp_combiner_inputs_t *c, unsigned muladd, unsigned mulsub, unsigned mul, unsigned add,
		unsigned amuladd, unsigned amulsub, unsigned amul, unsigned aadd)
{
	c->rgb.muladd = (uint8_t)muladd;
	c->rgb.mulsub = (uint8_t)mulsub;
	c->rgb.mul = (uint8_t)mul;
	c->rgb.add = (uint8_t)add;
	c->alpha.muladd = (uint8_t)amuladd;
	c->alpha.mulsub = (uint8_t)amulsub;
	c->alpha.mul = (uint8_t)amul;
	c->alpha.add = (uint8_t)aadd;
}

static bool rgba_is(const uint8_t *v, unsigned r, unsigned g, unsigned b, unsigned a)
{
	return v[0] == r && v[1] == g && v[2] == b && v[3] == a;
}

static void test_derived(void)
{
	rdp_constants_t c;
	rdp_static_state_t state;
	rdp_attribute_setup_t attr;
	rdp_derived_setup_t d;

	CHECK(sizeof(rdp_derived_setup_t) == 56 && sizeof(rdp_static_state_t) == 32
			&& sizeof(rdp_depth_blend_state_t) == 16 && sizeof(rdp_tile_info_t) == 32,
			"the structs are the sizes the shaders read");

	memset(&c, 0, sizeof(c));
	c.primitive_color = 0x11223344u;
	c.env_color = 0x55667788u;
	c.fog_color = 0x99aabbccu;
	c.blend_color = 0xddeeff01u;
	c.fill_color = 0x12345678u;
	c.min_level = 9;
	c.prim_lod_frac = 0x3c;
	c.convert[0] = 0x1ff;
	c.convert[1] = 2;
	c.convert[2] = 3;
	c.convert[3] = 0x100;
	c.convert[4] = 0x1a5;
	c.convert[5] = 0x0b7;
	c.key_center[0] = 1; c.key_center[1] = 2; c.key_center[2] = 3;
	c.key_scale[0] = 4; c.key_scale[1] = 5; c.key_scale[2] = 6;

	memset(&state, 0, sizeof(state));
	memset(&attr, 0, sizeof(attr));
	/* cycle 0: env, prim, prim alpha, env; cycle 1: prim, env, env alpha, prim */
	combiner(&state.combiner[0], RDP_RGB_MULADD_ENV, RDP_RGB_MULSUB_PRIMITIVE, RDP_RGB_MUL_PRIMITIVE_ALPHA,
			RDP_RGB_ADD_ENV, RDP_ALPHA_ADDSUB_PRIMITIVE_ALPHA, RDP_ALPHA_ADDSUB_ENV_ALPHA,
			RDP_ALPHA_MUL_PRIM_LOD_FRAC, RDP_ALPHA_ADDSUB_ENV_ALPHA);
	combiner(&state.combiner[1], RDP_RGB_MULADD_PRIMITIVE, RDP_RGB_MULSUB_ENV, RDP_RGB_MUL_ENV_ALPHA,
			RDP_RGB_ADD_PRIMITIVE, RDP_ALPHA_ADDSUB_ENV_ALPHA, RDP_ALPHA_ADDSUB_PRIMITIVE_ALPHA,
			RDP_ALPHA_MUL_ENV_ALPHA, RDP_ALPHA_ADDSUB_PRIMITIVE_ALPHA);
	memset(&d, 0x55, sizeof(d));
	rdp_derived_setup(&c, &state, &attr, &d);
	CHECK(rgba_is(d.constants[0].muladd, 0x55, 0x66, 0x77, 0x44) && rgba_is(d.constants[0].mulsub, 0x11, 0x22, 0x33, 0x88)
			&& rgba_is(d.constants[0].mul, 0x44, 0x44, 0x44, 0x3c) && rgba_is(d.constants[0].add, 0x55, 0x66, 0x77, 0x88),
			"cycle 0's constant inputs");
	CHECK(rgba_is(d.constants[1].muladd, 0x11, 0x22, 0x33, 0x88) && rgba_is(d.constants[1].mulsub, 0x55, 0x66, 0x77, 0x44)
			&& rgba_is(d.constants[1].mul, 0x88, 0x88, 0x88, 0x88) && rgba_is(d.constants[1].add, 0x11, 0x22, 0x33, 0x44),
			"cycle 1's");
	CHECK(rgba_is(d.fog_color, 0x99, 0xaa, 0xbb, 0xcc) && rgba_is(d.blend_color, 0xdd, 0xee, 0xff, 0x01)
			&& d.fill_color == 0x12345678u && d.min_lod == 9, "fog, blend and fill colour and the minimum level");
	CHECK(d.convert_factors[0] == 0x1ff && d.convert_factors[1] == 2 && d.convert_factors[2] == 3
			&& d.convert_factors[3] == 0x100, "the first four convert factors");

	/* the special ones */
	combiner(&state.combiner[0], RDP_RGB_MULADD_SHADE, RDP_RGB_MULSUB_CONVERT_K4, RDP_RGB_MUL_CONVERT_K5,
			RDP_RGB_ADD_SHADE, RDP_ALPHA_ADDSUB_SHADE_ALPHA, RDP_ALPHA_ADDSUB_ZERO, RDP_ALPHA_MUL_PRIMITIVE_ALPHA,
			RDP_ALPHA_ADDSUB_ONE);
	combiner(&state.combiner[1], RDP_RGB_MULADD_ZERO, RDP_RGB_MULSUB_KEY_CENTER, RDP_RGB_MUL_KEY_SCALE,
			RDP_RGB_ADD_ZERO, RDP_ALPHA_ADDSUB_ZERO, RDP_ALPHA_ADDSUB_ZERO, RDP_ALPHA_MUL_ZERO, RDP_ALPHA_ADDSUB_ZERO);
	memset(&d, 0x55, sizeof(d));
	rdp_derived_setup(&c, &state, &attr, &d);
	CHECK(rgba_is(d.constants[0].mulsub, 0x00, 0x01, 0xa5, 0) && rgba_is(d.constants[0].mul, 0x00, 0x00, 0xb7, 0x44),
			"K4 and K5 are 9 bits: their ninth in the second byte, the rest in the third");
	CHECK(rgba_is(d.constants[0].muladd, 0, 0, 0, 0) && rgba_is(d.constants[0].add, 0, 0, 0, 0),
			"a non-constant input leaves its constant zero");
	CHECK(rgba_is(d.constants[1].mulsub, 1, 2, 3, 0) && rgba_is(d.constants[1].mul, 4, 5, 6, 0),
			"the key's centre and scale");
	combiner(&state.combiner[1], RDP_RGB_MULADD_ZERO, RDP_RGB_MULSUB_ZERO, RDP_RGB_MUL_PRIM_LOD_FRAC,
			RDP_RGB_ADD_ZERO, RDP_ALPHA_ADDSUB_ZERO, RDP_ALPHA_ADDSUB_ZERO, RDP_ALPHA_MUL_ZERO, RDP_ALPHA_ADDSUB_ZERO);
	rdp_derived_setup(&c, &state, &attr, &d);
	CHECK(rgba_is(d.constants[1].mul, 0x3c, 0x3c, 0x3c, 0), "the primitive LOD fraction in colour");

	/* ---- the depth slope ---- */
	attr.dzdx = 3 << 16;
	attr.dzdy = 4 << 16;
	rdp_derived_setup(&c, &state, &attr, &d);
	CHECK(d.dz == 8 && d.dz_compressed == 3, "dz is the slopes' sum, rounded up to a power of two");
	attr.dzdx = -(3 << 16);
	attr.dzdy = (int32_t)0xffff8000u; /* -0.5: its integer half is -1 */
	rdp_derived_setup(&c, &state, &attr, &d);
	CHECK(d.dz == 4 && d.dz_compressed == 2, "a negative slope counts as its complement");
	attr.dzdx = 0;
	attr.dzdy = (int32_t)0xfffe8000u; /* -1.5: its integer half is -2, which counts as 1 */
	rdp_derived_setup(&c, &state, &attr, &d);
	CHECK(d.dz == 2, "a slope's integer half rounds down");
	attr.dzdy = -(4 << 16);
	rdp_derived_setup(&c, &state, &attr, &d);
	CHECK(d.dz == 4, "-4 counts as 3, not 4");
	attr.dzdx = 0;
	attr.dzdy = 0;
	rdp_derived_setup(&c, &state, &attr, &d);
	CHECK(d.dz == 1 && d.dz_compressed == 0, "no slope is one");
	attr.dzdx = 0x4000 << 16;
	attr.dzdy = 0x4000 << 16;
	rdp_derived_setup(&c, &state, &attr, &d);
	CHECK(d.dz == 0x8000 && d.dz_compressed == 15, "at most 0x8000");
	attr.dzdx = 0x3fff << 16;
	attr.dzdy = 0;
	rdp_derived_setup(&c, &state, &attr, &d);
	CHECK(d.dz == 0x4000 && d.dz_compressed == 14, "0x3fff rounds up to 0x4000");
	attr.dzdx = 0x4000 << 16;
	rdp_derived_setup(&c, &state, &attr, &d);
	CHECK(d.dz == 0x8000, "0x4000 to 0x8000");

	c.use_prim_depth = true;
	c.prim_dz = 0x0123;
	rdp_derived_setup(&c, &state, &attr, &d);
	CHECK(d.dz == 0x0123 && d.dz_compressed == 0xd, "the primitive depth register's dz is taken as it is");
	c.prim_dz = 0x0400;
	rdp_derived_setup(&c, &state, &attr, &d);
	CHECK(d.dz == 0x0400 && d.dz_compressed == 0xa, "and compressed");
}

/* a triangle from 10.2 Y and 16.16 X, as the decoder leaves them */
static void tri(rdp_triangle_setup_t *t, int yh, int ym, int yl, double xh, double xm, double xl, double dxhdy,
		double dxmdy, double dxldy, bool flip)
{
	memset(t, 0, sizeof(*t));
	t->yh = (int16_t)yh;
	t->ym = (int16_t)ym;
	t->yl = (int16_t)yl;
	t->xh = (int32_t)(xh * 32768.0);
	t->xm = (int32_t)(xm * 32768.0);
	t->xl = (int32_t)(xl * 32768.0);
	t->dxhdy = (int32_t)(dxhdy * 32768.0);
	t->dxmdy = (int32_t)(dxmdy * 32768.0);
	t->dxldy = (int32_t)(dxldy * 32768.0);
	t->flags = flip ? TRIANGLE_SETUP_FLIP_BIT : 0;
}

static void test_tiles(void)
{
	rdp_scissor_state_t sc;
	rdp_triangle_setup_t t;

	sc.xlo = 0;
	sc.ylo = 0;
	sc.xhi = 320 << 2;
	sc.yhi = 240 << 2;

	/* a 64x64 square at (64, 64), left major (xh on the left): 8x8 tiles,
	 * plus the column the right edge's pixel is in */
	tri(&t, 64 << 2, 128 << 2, 128 << 2, 64, 128, 128, 0, 0, 0, true);
	CHECK(rdp_conservative_tile_count(&t, &sc, 1) == 9 * 8, "a square's tiles");
	CHECK(rdp_conservative_tile_count(&t, &sc, 2) == 17 * 16, "at 2x");
	CHECK(rdp_conservative_tile_count(&t, &sc, 4) == 33 * 32, "at 4x");
	tri(&t, 64 << 2, 128 << 2, 128 << 2, 128, 64, 64, 0, 0, 0, false);
	CHECK(rdp_conservative_tile_count(&t, &sc, 1) == 9 * 8, "right major (xh on the right)");

	/* none */
	tri(&t, 64 << 2, 64 << 2, 64 << 2, 64, 128, 128, 0, 0, 0, true);
	CHECK(rdp_conservative_tile_count(&t, &sc, 1) == 0, "no height, no tiles");
	tri(&t, 64 << 2, 128 << 2, 128 << 2, 128, 64, 64, 0, 0, 0, true);
	CHECK(rdp_conservative_tile_count(&t, &sc, 1) == 0, "nor edges the wrong way round");
	tri(&t, 300 << 2, 320 << 2, 320 << 2, 64, 128, 128, 0, 0, 0, true);
	CHECK(rdp_conservative_tile_count(&t, &sc, 1) == 0, "below the scissor");
	tri(&t, 64 << 2, 128 << 2, 128 << 2, 330, 400, 400, 0, 0, 0, true);
	CHECK(rdp_conservative_tile_count(&t, &sc, 1) == 0, "right of it");

	/* the scissor */
	sc.xlo = 80 << 2;
	sc.ylo = 72 << 2;
	tri(&t, 64 << 2, 128 << 2, 128 << 2, 64, 128, 128, 0, 0, 0, true);
	CHECK(rdp_conservative_tile_count(&t, &sc, 1) == 7 * 7, "inside the scissor");
	sc.xlo = 0;
	sc.ylo = 0;

	/* a triangle (slopes per sub-line) whose widest line is its middle
	 * one, at 160: neither its top nor its bottom line has it */
	tri(&t, 0, 64 << 2, 128 << 2, 32, 32, 160, 0, 0.25, -0.25, true);
	CHECK(rdp_conservative_tile_count(&t, &sc, 1) == 17 * 16, "a triangle's middle vertex is sampled");

	/* its top above the screen: counted from where it is */
	tri(&t, -6, 120 << 2, 128 << 2, 64, 128, 128, 0, 0, 0, true);
	CHECK(rdp_conservative_tile_count(&t, &sc, 1) == 9 * 16, "a primitive starting above the screen");
	/* one sloping out from 20 lines above: at the top of the screen its
	 * right edge is at 84, just above its middle vertex at 203 */
	tri(&t, -20 << 2, 120 << 2, 128 << 2, 64, 64, 128, 0, 0.25, 0, true);
	CHECK(rdp_conservative_tile_count(&t, &sc, 1) == 18 * 16, "and one sloping out from above it");

	/* entirely left of the screen, its right edge half a pixel left of 0 */
	tri(&t, 0, 64 << 2, 64 << 2, -10, -0.5, -0.5, 0, 0, 0, true);
	CHECK(rdp_conservative_tile_count(&t, &sc, 1) == 0, "left of the screen, X rounds down");
	/* the scissor's edges: its right edge rounds up, its bottom one is not drawn */
	sc.xhi = (128 << 2) + 1;
	sc.yhi = 96 << 2;
	tri(&t, 64 << 2, 128 << 2, 128 << 2, 0, 300, 300, 0, 0, 0, true);
	CHECK(rdp_conservative_tile_count(&t, &sc, 1) == 17 * 4, "a scissor's fractional right edge takes in the pixel");
	sc.xhi = 320 << 2;
	sc.yhi = 240 << 2;
	tri(&t, 257, 257, 257, 0, 300, 300, 0, 0, 0, true);
	CHECK(rdp_conservative_tile_count(&t, &sc, 1) == 0, "no height inside a line is no tiles");

	/* overflow: the whole width */
	tri(&t, 0, 64 << 2, 64 << 2, -3000, 100, 100, 0, 0, 0, true);
	CHECK(rdp_conservative_tile_count(&t, &sc, 1) == 40 * 8, "X past the range on the left covers the whole width too");
	tri(&t, 0, 64 << 2, 64 << 2, 0, 2100, 2100, 0, 0, 0, true);
	CHECK(rdp_conservative_tile_count(&t, &sc, 1) == 40 * 8, "X past the rasterizer's range covers the scissor's width");
	tri(&t, 0, 64 << 2, 64 << 2, 0, 2000, 2000, 0, 0, 0, true);
	CHECK(rdp_conservative_tile_count(&t, &sc, 1) == 40 * 8, "(as an X inside the range but past the scissor)");
	t.xm = INT32_MAX;
	t.dxmdy = INT32_MAX;
	CHECK(rdp_conservative_tile_count(&t, &sc, 8) == 320 * 64, "at the int32 extremes, at 8x");

	/* the X range test, as the binning shader has it */
	CHECK(!rdp_x_range_exceeds(0, 2047, 2047) && !rdp_x_range_exceeds(-2047, 2047, 2047), "inside");
	CHECK(rdp_x_range_exceeds(-2048, 0, 2047) && rdp_x_range_exceeds(0, 2048, 2047), "just past either end");
	CHECK(rdp_x_range_exceeds(INT_MIN, 10, 2047) && rdp_x_range_exceeds(10, INT_MIN, 2047), "INT_MIN either side");
	CHECK(rdp_x_range_exceeds(INT_MIN, INT_MAX, 2047 * 8), "both extremes");
	CHECK(!rdp_x_range_exceeds(-16376, 16376, 2047 * 8) && rdp_x_range_exceeds(-16377, 0, 2047 * 8), "at 8x");
}

static void test_fixup(void)
{
	rdp_triangle_setup_t t;

	tri(&t, 13, 12, 40, 0, 0, 0, 0, 0, 0, true);
	rdp_fixup_triangle_setup(&t, 0);
	CHECK(t.ym == 12 && t.flags == TRIANGLE_SETUP_FLIP_BIT,
			"YM on or below the first sub-line rasterized (YH's line, 12 for 13) is kept");
	tri(&t, 13, 11, 40, 0, 0, 0, 0, 0, 0, true);
	rdp_fixup_triangle_setup(&t, 0);
	CHECK(t.ym == INT16_MAX, "above it, it is never reached");
	tri(&t, -6, -9, 40, 0, 0, 0, 0, 0, 0, false);
	rdp_fixup_triangle_setup(&t, 0);
	CHECK(t.ym == INT16_MAX && t.flags == 0, "and above the screen");
	tri(&t, -6, -8, 40, 0, 0, 0, 0, 0, 0, false);
	rdp_fixup_triangle_setup(&t, 0);
	CHECK(t.ym == -8, "(-6 starts on line -8)");

	tri(&t, 0, 10, 40, 0, 0, 0, 0, 0, 0, false);
	rdp_fixup_triangle_setup(&t, RASTERIZATION_INTERLACE_FIELD_BIT);
	CHECK(t.flags == TRIANGLE_SETUP_INTERLACE_FIELD_BIT, "interlaced, one field");
	t.flags = 0;
	rdp_fixup_triangle_setup(&t, RASTERIZATION_INTERLACE_FIELD_BIT | RASTERIZATION_INTERLACE_KEEP_ODD_BIT);
	CHECK(t.flags == (TRIANGLE_SETUP_INTERLACE_FIELD_BIT | TRIANGLE_SETUP_INTERLACE_KEEP_ODD_BIT), "the odd one");
	t.flags = 0;
	rdp_fixup_triangle_setup(&t, RASTERIZATION_INTERLACE_KEEP_ODD_BIT);
	CHECK(t.flags == 0, "not interlaced, the odd field means nothing");
	rdp_fixup_triangle_setup(&t, RASTERIZATION_FILL_BIT);
	CHECK(t.flags == TRIANGLE_SETUP_FILL_COPY_RASTER_BIT, "fill rasterizes as it is");
	t.flags = 0;
	rdp_fixup_triangle_setup(&t, RASTERIZATION_COPY_BIT);
	CHECK(t.flags == TRIANGLE_SETUP_FILL_COPY_RASTER_BIT, "and copy");
}

static uint32_t deduced(rdp_static_state_t *state, const rdp_tile_info_t *tiles, unsigned tile, unsigned max_lod)
{
	rdp_deduce_static_texture_state(state, tiles, tile, max_lod);
	return state->flags & (RASTERIZATION_USES_TEXEL0_BIT | RASTERIZATION_USES_TEXEL1_BIT |
			RASTERIZATION_USES_PIPELINED_TEXEL1_BIT | RASTERIZATION_USES_LOD_BIT |
			RASTERIZATION_USE_STATIC_TEXTURE_SIZE_FORMAT_BIT);
}

static void test_textures(void)
{
	rdp_static_state_t s;
	rdp_tile_info_t tiles[8];
	unsigned i;

	memset(tiles, 0, sizeof(tiles));
	for (i = 0; i < 8; i++)
	{
		tiles[i].meta.fmt = RDP_TEXTURE_FORMAT_CI;
		tiles[i].meta.size = RDP_TEXTURE_SIZE_8;
	}
	memset(&s, 0, sizeof(s));
	combiner(&s.combiner[0], RDP_RGB_MULADD_ZERO, RDP_RGB_MULSUB_ZERO, RDP_RGB_MUL_ZERO, RDP_RGB_ADD_ZERO,
			RDP_ALPHA_ADDSUB_ZERO, RDP_ALPHA_ADDSUB_ZERO, RDP_ALPHA_MUL_ZERO, RDP_ALPHA_ADDSUB_ZERO);
	s.combiner[1] = s.combiner[0];

	CHECK(deduced(&s, tiles, 0, 0) == 0 && s.texture_fmt == 0 && s.texture_size == 0,
			"a combiner that reads no texel uses none, and has no static format");

	/* one cycle */
	s.combiner[1].rgb.add = RDP_RGB_ADD_TEXEL0;
	CHECK(deduced(&s, tiles, 0, 0) == (RASTERIZATION_USES_TEXEL0_BIT | RASTERIZATION_USE_STATIC_TEXTURE_SIZE_FORMAT_BIT)
			&& s.texture_fmt == RDP_TEXTURE_FORMAT_CI && s.texture_size == RDP_TEXTURE_SIZE_8,
			"one cycle reading texel 0, with its tile's format and size");
	s.combiner[1].rgb.add = RDP_RGB_ADD_ZERO;
	s.combiner[1].alpha.mul = RDP_ALPHA_MUL_TEXEL1_ALPHA;
	CHECK(deduced(&s, tiles, 0, 0) == (RASTERIZATION_USES_PIPELINED_TEXEL1_BIT | RASTERIZATION_USE_STATIC_TEXTURE_SIZE_FORMAT_BIT),
			"texel 1 in one cycle is the next pixel's");
	s.combiner[1].alpha.mul = RDP_ALPHA_MUL_LOD_FRAC;
	CHECK(deduced(&s, tiles, 0, 0) == 0, "the LOD fraction in one cycle is not read");
	s.combiner[0].rgb.mul = RDP_RGB_MUL_TEXEL0;
	CHECK(deduced(&s, tiles, 0, 0) == 0, "nor the first cycle's inputs");
	s.combiner[0].rgb.mul = RDP_RGB_MUL_ZERO;
	s.combiner[1].alpha.mul = RDP_ALPHA_MUL_ZERO;

	/* two cycles */
	s.flags = RASTERIZATION_MULTI_CYCLE_BIT;
	s.combiner[0].rgb.mulsub = RDP_RGB_MULSUB_TEXEL0;
	CHECK(deduced(&s, tiles, 0, 0) == (RASTERIZATION_USES_TEXEL0_BIT | RASTERIZATION_USE_STATIC_TEXTURE_SIZE_FORMAT_BIT),
			"two cycles, texel 0 in the first");
	s.combiner[0].rgb.mulsub = RDP_RGB_MULSUB_ZERO;
	s.combiner[1].rgb.muladd = RDP_RGB_MULADD_TEXEL1;
	CHECK(deduced(&s, tiles, 0, 0) == (RASTERIZATION_USES_TEXEL0_BIT | RASTERIZATION_USE_STATIC_TEXTURE_SIZE_FORMAT_BIT),
			"texel 1 in the second cycle is texel 0");
	s.combiner[1].rgb.muladd = RDP_RGB_MULADD_ZERO;
	s.combiner[1].alpha.add = RDP_ALPHA_ADDSUB_TEXEL0_ALPHA;
	CHECK(deduced(&s, tiles, 0, 0) == (RASTERIZATION_USES_TEXEL1_BIT | RASTERIZATION_USE_STATIC_TEXTURE_SIZE_FORMAT_BIT),
			"and texel 0 is texel 1");
	s.combiner[1].alpha.add = RDP_ALPHA_ADDSUB_ZERO;
	s.combiner[0].rgb.mul = RDP_RGB_MUL_TEXEL1_ALPHA;
	s.combiner[1].rgb.mul = RDP_RGB_MUL_LOD_FRAC;
	CHECK(deduced(&s, tiles, 0, 0) == (RASTERIZATION_USES_TEXEL1_BIT | RASTERIZATION_USES_LOD_BIT |
				RASTERIZATION_USE_STATIC_TEXTURE_SIZE_FORMAT_BIT), "the LOD fraction in two cycles is read");
	s.combiner[1].rgb.mul = RDP_RGB_MUL_ZERO;
	s.combiner[1].alpha.mul = RDP_ALPHA_MUL_LOD_FRAC;
	CHECK(deduced(&s, tiles, 0, 0) == (RASTERIZATION_USES_TEXEL1_BIT | RASTERIZATION_USES_LOD_BIT |
				RASTERIZATION_USE_STATIC_TEXTURE_SIZE_FORMAT_BIT), "the alpha LOD fraction too");
	s.combiner[1].alpha.mul = RDP_ALPHA_MUL_ZERO;

	/* texel 1 alone: the next tile must agree */
	tiles[1].meta.size = RDP_TEXTURE_SIZE_16;
	CHECK(deduced(&s, tiles, 0, 0) == RASTERIZATION_USES_TEXEL1_BIT && !s.texture_fmt && !s.texture_size,
			"texel 1 reads the next tile, which must have the same format and size");
	tiles[1].meta.size = RDP_TEXTURE_SIZE_8;
	tiles[1].meta.fmt = RDP_TEXTURE_FORMAT_IA;
	CHECK(deduced(&s, tiles, 0, 0) == RASTERIZATION_USES_TEXEL1_BIT, "(the format)");
	CHECK(deduced(&s, tiles, 1, 0) == RASTERIZATION_USES_TEXEL1_BIT, "from tile 1 the next is tile 2, which differs too");
	tiles[0].meta.fmt = RDP_TEXTURE_FORMAT_IA;
	tiles[2].meta.fmt = RDP_TEXTURE_FORMAT_IA;
	CHECK(deduced(&s, tiles, 1, 0) == (RASTERIZATION_USES_TEXEL1_BIT | RASTERIZATION_USE_STATIC_TEXTURE_SIZE_FORMAT_BIT)
			&& s.texture_fmt == RDP_TEXTURE_FORMAT_IA && s.texture_size == RDP_TEXTURE_SIZE_8,
			"tiles 1 and 2 alike, tile 0 is not read");
	tiles[0].meta.fmt = RDP_TEXTURE_FORMAT_CI;
	tiles[2].meta.fmt = RDP_TEXTURE_FORMAT_IA;
	tiles[1].meta.fmt = RDP_TEXTURE_FORMAT_CI;
	CHECK(deduced(&s, tiles, 1, 0) == RASTERIZATION_USES_TEXEL1_BIT, "(tile 2 against tile 1)");
	tiles[2].meta.fmt = RDP_TEXTURE_FORMAT_CI;

	/* convert one: texel 1 is texel 0 converted, without its own tile unless bilerped */
	s.flags |= RASTERIZATION_CONVERT_ONE_BIT;
	tiles[1].meta.size = RDP_TEXTURE_SIZE_16;
	CHECK(deduced(&s, tiles, 0, 0) == (RASTERIZATION_USES_TEXEL0_BIT | RASTERIZATION_USES_TEXEL1_BIT |
				RASTERIZATION_USE_STATIC_TEXTURE_SIZE_FORMAT_BIT), "texel 1 converted from texel 0 needs no tile of its own");
	s.flags |= RASTERIZATION_BILERP_1_BIT;
	CHECK(deduced(&s, tiles, 0, 0) == (RASTERIZATION_USES_TEXEL0_BIT | RASTERIZATION_USES_TEXEL1_BIT),
			"unless it is bilerped");
	s.flags &= ~(uint32_t)(RASTERIZATION_CONVERT_ONE_BIT | RASTERIZATION_BILERP_1_BIT);
	tiles[1].meta.size = RDP_TEXTURE_SIZE_8;

	/* LOD: every level's tile, and detail one more */
	s.combiner[0].rgb.mul = RDP_RGB_MUL_TEXEL0;
	s.flags |= RASTERIZATION_TEX_LOD_ENABLE_BIT;
	tiles[4].meta.size = RDP_TEXTURE_SIZE_32;
	CHECK(deduced(&s, tiles, 0, 3) == (RASTERIZATION_USES_TEXEL0_BIT | RASTERIZATION_USES_LOD_BIT |
				RASTERIZATION_USE_STATIC_TEXTURE_SIZE_FORMAT_BIT), "with LOD, the tiles of the levels used");
	CHECK(deduced(&s, tiles, 0, 4) == (RASTERIZATION_USES_TEXEL0_BIT | RASTERIZATION_USES_LOD_BIT),
			"(one more level reaches tile 4)");
	s.flags |= RASTERIZATION_DETAIL_LOD_ENABLE_BIT;
	CHECK(deduced(&s, tiles, 0, 3) == (RASTERIZATION_USES_TEXEL0_BIT | RASTERIZATION_USES_LOD_BIT),
			"detail adds a level");
	tiles[4].meta.size = RDP_TEXTURE_SIZE_8;
	tiles[7].meta.size = RDP_TEXTURE_SIZE_32;
	CHECK(deduced(&s, tiles, 0, 7) == (RASTERIZATION_USES_TEXEL0_BIT | RASTERIZATION_USES_LOD_BIT),
			"seven levels and detail reach every tile");
	CHECK(deduced(&s, tiles, 0, 5) == (RASTERIZATION_USES_TEXEL0_BIT | RASTERIZATION_USES_LOD_BIT |
				RASTERIZATION_USE_STATIC_TEXTURE_SIZE_FORMAT_BIT), "five and detail stop at tile 6");
	CHECK(deduced(&s, tiles, 5, 0) == (RASTERIZATION_USES_TEXEL0_BIT | RASTERIZATION_USES_LOD_BIT |
				RASTERIZATION_USE_STATIC_TEXTURE_SIZE_FORMAT_BIT), "from tile 5, level 0 and detail read tiles 5 and 6");
	tiles[7].meta.size = RDP_TEXTURE_SIZE_8;
	tiles[6].meta.size = RDP_TEXTURE_SIZE_32;
	CHECK(deduced(&s, tiles, 5, 0) == (RASTERIZATION_USES_TEXEL0_BIT | RASTERIZATION_USES_LOD_BIT),
			"(tile 6 differs)");
	tiles[6].meta.size = RDP_TEXTURE_SIZE_8;
	tiles[0].meta.size = RDP_TEXTURE_SIZE_32;
	CHECK(deduced(&s, tiles, 7, 0) == (RASTERIZATION_USES_TEXEL0_BIT | RASTERIZATION_USES_LOD_BIT),
			"from tile 7 the next is tile 0");
	tiles[0].meta.size = RDP_TEXTURE_SIZE_8;

	/* fill and copy */
	s.flags = RASTERIZATION_FILL_BIT | RASTERIZATION_USE_STATIC_TEXTURE_SIZE_FORMAT_BIT;
	CHECK(deduced(&s, tiles, 0, 0) == 0 && !s.texture_fmt && !s.texture_size, "fill samples nothing");
	s.flags = RASTERIZATION_COPY_BIT | RASTERIZATION_USES_TEXEL0_BIT;
	tiles[1].meta.size = RDP_TEXTURE_SIZE_32;
	CHECK(deduced(&s, tiles, 0, 0) == (RASTERIZATION_USES_TEXEL0_BIT | RASTERIZATION_USE_STATIC_TEXTURE_SIZE_FORMAT_BIT)
			&& s.texture_size == RDP_TEXTURE_SIZE_8, "copy samples its tile alone, and leaves the flags it had");
}

static void test_noise(void)
{
	rdp_static_state_t s;

	memset(&s, 0, sizeof(s));
	combiner(&s.combiner[0], RDP_RGB_MULADD_ZERO, 0, 0, 0, 0, 0, 0, 0);
	s.combiner[1] = s.combiner[0];
	s.dither = 3 << 2 | 3;
	s.flags = RASTERIZATION_NEED_NOISE_BIT | RASTERIZATION_NEED_NOISE_DUAL_BIT;
	rdp_deduce_noise_state(&s);
	CHECK(!(s.flags & (RASTERIZATION_NEED_NOISE_BIT | RASTERIZATION_NEED_NOISE_DUAL_BIT)), "no noise");
	s.dither = 2 << 2 | 3;
	rdp_deduce_noise_state(&s);
	CHECK(s.flags == RASTERIZATION_NEED_NOISE_BIT, "noise RGB dither");
	s.dither = 3 << 2 | 2;
	rdp_deduce_noise_state(&s);
	CHECK(s.flags == RASTERIZATION_NEED_NOISE_BIT, "noise alpha dither");
	s.dither = 1 << 2 | 1;
	s.combiner[1].rgb.muladd = RDP_RGB_MULADD_NOISE;
	rdp_deduce_noise_state(&s);
	CHECK(s.flags == RASTERIZATION_NEED_NOISE_BIT, "the combiner's noise");
	s.flags = RASTERIZATION_COPY_BIT;
	rdp_deduce_noise_state(&s);
	CHECK(s.flags == RASTERIZATION_COPY_BIT, "not in copy");
	s.flags = RASTERIZATION_FILL_BIT;
	rdp_deduce_noise_state(&s);
	CHECK(s.flags == RASTERIZATION_FILL_BIT, "nor fill");
	s.combiner[1].rgb.muladd = RDP_RGB_MULADD_ZERO;
	s.combiner[0].rgb.muladd = RDP_RGB_MULADD_NOISE;
	s.flags = 0;
	rdp_deduce_noise_state(&s);
	CHECK(s.flags == 0, "the first cycle's noise is not read in one cycle");
	s.flags = RASTERIZATION_MULTI_CYCLE_BIT;
	rdp_deduce_noise_state(&s);
	CHECK(s.flags == (RASTERIZATION_MULTI_CYCLE_BIT | RASTERIZATION_NEED_NOISE_BIT), "it is in two");
	s.combiner[1].rgb.muladd = RDP_RGB_MULADD_NOISE;
	rdp_deduce_noise_state(&s);
	CHECK(s.flags == (RASTERIZATION_MULTI_CYCLE_BIT | RASTERIZATION_NEED_NOISE_BIT | RASTERIZATION_NEED_NOISE_DUAL_BIT),
			"noise in both cycles is two of it");
	s.flags = 0;
	rdp_deduce_noise_state(&s);
	CHECK(s.flags == RASTERIZATION_NEED_NOISE_BIT, "(one cycle: one)");
	s.combiner[0].rgb.muladd = RDP_RGB_MULADD_ZERO;
	s.combiner[1].rgb.muladd = RDP_RGB_MULADD_ZERO;
	s.flags = RASTERIZATION_ALPHA_TEST_BIT;
	rdp_deduce_noise_state(&s);
	CHECK(s.flags == RASTERIZATION_ALPHA_TEST_BIT, "alpha test alone needs no noise");
	s.flags = RASTERIZATION_ALPHA_TEST_DITHER_BIT;
	rdp_deduce_noise_state(&s);
	CHECK(s.flags == RASTERIZATION_ALPHA_TEST_DITHER_BIT, "nor its dither alone");
	s.flags = RASTERIZATION_ALPHA_TEST_BIT | RASTERIZATION_ALPHA_TEST_DITHER_BIT;
	rdp_deduce_noise_state(&s);
	CHECK(s.flags == (RASTERIZATION_ALPHA_TEST_BIT | RASTERIZATION_ALPHA_TEST_DITHER_BIT | RASTERIZATION_NEED_NOISE_BIT),
			"dithered alpha test does");
}

static void test_normalize(void)
{
	rdp_static_state_t s, n;
	unsigned v;
	static const uint8_t rgb_muladd_kept[] = { 0, 1, 2, 4, 6, 7 }, rgb_mulsub_kept[] = { 0, 1, 2, 4, 7 };
	static const uint8_t rgb_mul_kept[] = { 0, 1, 2, 4, 7, 8, 9, 11, 13, 15 }, rgb_add_kept[] = { 0, 1, 2, 4, 6 };
	static const uint8_t alpha_addsub_kept[] = { 0, 1, 2, 4, 6 }, alpha_mul_kept[] = { 0, 1, 2, 4 };

	memset(&s, 0x5a, sizeof(s));
	s.flags = RASTERIZATION_FILL_BIT | RASTERIZATION_AA_BIT;
	n = s;
	rdp_normalize_static_state(&n);
	CHECK(n.flags == RASTERIZATION_FILL_BIT && !n.dither && !n.texture_fmt && !n.texture_size
			&& !n.combiner[0].rgb.mul && !n.combiner[1].alpha.add, "fill keeps nothing but itself");

	s.flags = 0xffffffffu & ~(uint32_t)RASTERIZATION_FILL_BIT;
	n = s;
	rdp_normalize_static_state(&n);
	CHECK(n.flags == (RASTERIZATION_COPY_BIT | RASTERIZATION_TLUT_BIT | RASTERIZATION_TLUT_TYPE_BIT |
				RASTERIZATION_USES_TEXEL0_BIT | RASTERIZATION_USE_STATIC_TEXTURE_SIZE_FORMAT_BIT |
				RASTERIZATION_TEX_LOD_ENABLE_BIT | RASTERIZATION_DETAIL_LOD_ENABLE_BIT |
				RASTERIZATION_PERSPECTIVE_CORRECT_BIT | RASTERIZATION_ALPHA_TEST_BIT)
			&& n.texture_fmt == 0x5a5a5a5au && n.texture_size == 0x5a5a5a5au && !n.dither && !n.combiner[0].rgb.muladd,
			"copy keeps a few flags and the texture format and size");

	/* the combiner: the constant inputs come from the derived setup */
	memset(&s, 0, sizeof(s));
	s.flags = RASTERIZATION_MULTI_CYCLE_BIT | RASTERIZATION_BILERP_1_BIT | RASTERIZATION_CONVERT_ONE_BIT;
	for (v = 0; v < 32; v++)
	{
		unsigned i;
		bool kept;
		s.combiner[0].rgb.muladd = s.combiner[0].rgb.mulsub = s.combiner[0].rgb.mul = s.combiner[0].rgb.add = (uint8_t)v;
		s.combiner[1].alpha.muladd = s.combiner[1].alpha.mulsub = s.combiner[1].alpha.mul = s.combiner[1].alpha.add = (uint8_t)v;
		n = s;
		rdp_normalize_static_state(&n);
		for (kept = false, i = 0; i < sizeof(rgb_muladd_kept); i++)
			kept |= rgb_muladd_kept[i] == v;
		CHECK(n.combiner[0].rgb.muladd == (kept ? v : RDP_RGB_MULADD_ZERO), "RGB muladd");
		for (kept = false, i = 0; i < sizeof(rgb_mulsub_kept); i++)
			kept |= rgb_mulsub_kept[i] == v;
		CHECK(n.combiner[0].rgb.mulsub == (kept ? v : RDP_RGB_MULSUB_ZERO), "RGB mulsub");
		for (kept = false, i = 0; i < sizeof(rgb_mul_kept); i++)
			kept |= rgb_mul_kept[i] == v;
		CHECK(n.combiner[0].rgb.mul == (kept ? v : RDP_RGB_MUL_ZERO), "RGB mul");
		for (kept = false, i = 0; i < sizeof(rgb_add_kept); i++)
			kept |= rgb_add_kept[i] == v;
		CHECK(n.combiner[0].rgb.add == (kept ? v : RDP_RGB_ADD_ZERO), "RGB add");
		for (kept = false, i = 0; i < sizeof(alpha_addsub_kept); i++)
			kept |= alpha_addsub_kept[i] == v;
		CHECK(n.combiner[1].alpha.muladd == (kept ? v : RDP_ALPHA_ADDSUB_ZERO)
				&& n.combiner[1].alpha.mulsub == n.combiner[1].alpha.muladd
				&& n.combiner[1].alpha.add == n.combiner[1].alpha.muladd, "alpha add and sub");
		for (kept = false, i = 0; i < sizeof(alpha_mul_kept); i++)
			kept |= alpha_mul_kept[i] == v;
		CHECK(n.combiner[1].alpha.mul == (kept ? v : RDP_ALPHA_MUL_ZERO), "alpha mul");
		CHECK(n.flags == s.flags, "two cycles keep the second texel's bilerp and convert");
	}
	s.flags = RASTERIZATION_BILERP_1_BIT | RASTERIZATION_CONVERT_ONE_BIT | RASTERIZATION_AA_BIT;
	n = s;
	rdp_normalize_static_state(&n);
	CHECK(n.flags == RASTERIZATION_AA_BIT, "one cycle drops them");
	s.flags |= RASTERIZATION_USES_PIPELINED_TEXEL1_BIT;
	n = s;
	rdp_normalize_static_state(&n);
	CHECK(n.flags == s.flags, "unless it reads the pipelined texel 1");
}

int main(void)
{
	test_derived();
	test_tiles();
	test_fixup();
	test_textures();
	test_noise();
	test_normalize();

	if (failures)
	{
		fprintf(stderr, "test_rdp_state: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_rdp_state: ok\n");
	return 0;
}
