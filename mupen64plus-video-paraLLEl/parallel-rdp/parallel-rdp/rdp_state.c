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

#include <string.h>
#include "rdp_state.h"

#define SUBPIXELS_Y 4
#define TILE_WIDTH 8
#define TILE_HEIGHT 8

/* ---- derived setup ---- */
static int normalize_dzpix(int dz)
{
	unsigned bit = 0;
	if (dz >= 0x8000)
		return 0x8000;
	else if (dz == 0)
		return 1;

	while ((dz >> bit) > 1)
		bit++;
	return 1 << (bit + 1);
}

static uint8_t dz_compress(int dz)
{
	int val = 0;
	if (dz & 0xff00)
		val |= 8;
	if (dz & 0xf0f0)
		val |= 4;
	if (dz & 0xcccc)
		val |= 2;
	if (dz & 0xaaaa)
		val |= 1;
	return (uint8_t)val;
}

static void encode_rgb(uint8_t *rgba, uint32_t color)
{
	rgba[0] = (uint8_t)(color >> 24);
	rgba[1] = (uint8_t)(color >> 16);
	rgba[2] = (uint8_t)(color >> 8);
}

static void encode_alpha(uint8_t *rgba, uint32_t color)
{
	rgba[3] = (uint8_t)color;
}

/* an alpha input's constant: primitive or environment alpha */
static void encode_alpha_addsub(uint8_t *rgba, unsigned input, const rdp_constants_t *c)
{
	if (input == RDP_ALPHA_ADDSUB_PRIMITIVE_ALPHA)
		encode_alpha(rgba, c->primitive_color);
	else if (input == RDP_ALPHA_ADDSUB_ENV_ALPHA)
		encode_alpha(rgba, c->env_color);
}

static void combiner_constants(const rdp_constants_t *c, const rdp_combiner_inputs_t *comb,
		rdp_constant_combiner_inputs_t *output)
{
	switch (comb->rgb.muladd)
	{
	case RDP_RGB_MULADD_ENV:
		encode_rgb(output->muladd, c->env_color);
		break;

	case RDP_RGB_MULADD_PRIMITIVE:
		encode_rgb(output->muladd, c->primitive_color);
		break;

	default:
		break;
	}

	switch (comb->rgb.mulsub)
	{
	case RDP_RGB_MULSUB_ENV:
		encode_rgb(output->mulsub, c->env_color);
		break;

	case RDP_RGB_MULSUB_PRIMITIVE:
		encode_rgb(output->mulsub, c->primitive_color);
		break;

	case RDP_RGB_MULSUB_CONVERT_K4:
		/* Need to decode this specially since it's a 9-bit value. */
		encode_rgb(output->mulsub, (uint32_t)c->convert[4] << 8);
		break;

	case RDP_RGB_MULSUB_KEY_CENTER:
		output->mulsub[0] = c->key_center[0];
		output->mulsub[1] = c->key_center[1];
		output->mulsub[2] = c->key_center[2];
		break;

	default:
		break;
	}

	switch (comb->rgb.mul)
	{
	case RDP_RGB_MUL_PRIMITIVE:
		encode_rgb(output->mul, c->primitive_color);
		break;

	case RDP_RGB_MUL_ENV:
		encode_rgb(output->mul, c->env_color);
		break;

	case RDP_RGB_MUL_PRIMITIVE_ALPHA:
		encode_rgb(output->mul, 0x01010101u * (c->primitive_color & 0xff));
		break;

	case RDP_RGB_MUL_ENV_ALPHA:
		encode_rgb(output->mul, 0x01010101u * (c->env_color & 0xff));
		break;

	case RDP_RGB_MUL_PRIM_LOD_FRAC:
		encode_rgb(output->mul, 0x01010101u * c->prim_lod_frac);
		break;

	case RDP_RGB_MUL_CONVERT_K5:
		/* Need to decode this specially since it's a 9-bit value. */
		encode_rgb(output->mul, (uint32_t)c->convert[5] << 8);
		break;

	case RDP_RGB_MUL_KEY_SCALE:
		output->mul[0] = c->key_scale[0];
		output->mul[1] = c->key_scale[1];
		output->mul[2] = c->key_scale[2];
		break;

	default:
		break;
	}

	switch (comb->rgb.add)
	{
	case RDP_RGB_ADD_PRIMITIVE:
		encode_rgb(output->add, c->primitive_color);
		break;

	case RDP_RGB_ADD_ENV:
		encode_rgb(output->add, c->env_color);
		break;

	default:
		break;
	}

	encode_alpha_addsub(output->muladd, comb->alpha.muladd, c);
	encode_alpha_addsub(output->mulsub, comb->alpha.mulsub, c);
	encode_alpha_addsub(output->add, comb->alpha.add, c);

	switch (comb->alpha.mul)
	{
	case RDP_ALPHA_MUL_PRIMITIVE_ALPHA:
		encode_alpha(output->mul, c->primitive_color);
		break;

	case RDP_ALPHA_MUL_ENV_ALPHA:
		encode_alpha(output->mul, c->env_color);
		break;

	case RDP_ALPHA_MUL_PRIM_LOD_FRAC:
		encode_alpha(output->mul, c->prim_lod_frac);
		break;

	default:
		break;
	}
}

void rdp_derived_setup(const rdp_constants_t *c, const rdp_static_state_t *state,
		const rdp_attribute_setup_t *attr, rdp_derived_setup_t *setup)
{
	unsigned i;

	memset(setup, 0, sizeof(*setup));
	if (c->use_prim_depth)
	{
		setup->dz = c->prim_dz;
		setup->dz_compressed = dz_compress(setup->dz);
	}
	else
	{
		/* (the integer halves, in their sign's magnitude, one less negative) */
		const int dzdx = attr->dzdx / 65536 - (attr->dzdx < 0 && (attr->dzdx & 0xffff) ? 1 : 0);
		const int dzdy = attr->dzdy / 65536 - (attr->dzdy < 0 && (attr->dzdy & 0xffff) ? 1 : 0);
		const int dzpix = normalize_dzpix((dzdx < 0 ? (~dzdx & 0x7fff) : dzdx) + (dzdy < 0 ? (~dzdy & 0x7fff) : dzdy));
		setup->dz = (uint16_t)dzpix;
		setup->dz_compressed = dz_compress(dzpix);
	}

	combiner_constants(c, &state->combiner[0], &setup->constants[0]);
	combiner_constants(c, &state->combiner[1], &setup->constants[1]);

	setup->fog_color[0] = (uint8_t)(c->fog_color >> 24);
	setup->fog_color[1] = (uint8_t)(c->fog_color >> 16);
	setup->fog_color[2] = (uint8_t)(c->fog_color >> 8);
	setup->fog_color[3] = (uint8_t)(c->fog_color >> 0);

	setup->blend_color[0] = (uint8_t)(c->blend_color >> 24);
	setup->blend_color[1] = (uint8_t)(c->blend_color >> 16);
	setup->blend_color[2] = (uint8_t)(c->blend_color >> 8);
	setup->blend_color[3] = (uint8_t)(c->blend_color >> 0);

	setup->fill_color = c->fill_color;
	setup->min_lod = c->min_level;

	for (i = 0; i < 4; i++)
		setup->convert_factors[i] = (int16_t)c->convert[i];
}

/* ---- the tiles a primitive covers ---- */
static int32_t clamp_int32(int64_t v)
{
	if (v < INT32_MIN)
		return INT32_MIN;
	else if (v > INT32_MAX)
		return INT32_MAX;
	else
		return (int32_t)v;
}

/* @v / 2^15, rounding down */
static int64_t shift15(int64_t v)
{
	return (v - (v & 0x7fff)) / 32768;
}

typedef struct x_span
{
	int32_t first, second;
} x_span_t;

static x_span_t interpolate_x(const rdp_triangle_setup_t *setup, int y, bool flip, int scaling)
{
	/* Interpolate in 64-bit so we are guaranteed to catch any overflow scenario. */
	const int64_t yh_interpolation_base = (int64_t)(setup->yh & ~(SUBPIXELS_Y - 1)) * scaling;
	const int64_t ym_interpolation_base = (int64_t)setup->ym * scaling;
	const int64_t xh = (int64_t)scaling * setup->xh + (y - yh_interpolation_base) * setup->dxhdy;
	const int64_t xm = (int64_t)scaling * setup->xm + (y - yh_interpolation_base) * setup->dxmdy;
	int64_t xl = (int64_t)scaling * setup->xl + (y - ym_interpolation_base) * setup->dxldy;
	x_span_t span;

	if (y < scaling * setup->ym)
		xl = xm;

	if (flip)
	{
		span.first = clamp_int32(shift15(xh));
		span.second = clamp_int32(shift15(xl));
	}
	else
	{
		span.first = clamp_int32(shift15(xl));
		span.second = clamp_int32(shift15(xh));
	}
	return span;
}

static int32_t min32(int32_t a, int32_t b)
{
	return b < a ? b : a;
}

static int32_t max32(int32_t a, int32_t b)
{
	return a < b ? b : a;
}

bool rdp_x_range_exceeds(int32_t start_x, int32_t end_x, int32_t limit)
{
	const int64_t a = start_x < 0 ? -(int64_t)start_x : (int64_t)start_x;
	const int64_t b = end_x < 0 ? -(int64_t)end_x : (int64_t)end_x;
	return (a > b ? a : b) > (int64_t)limit;
}

unsigned rdp_conservative_tile_count(const rdp_triangle_setup_t *setup, const rdp_scissor_state_t *scissor,
		unsigned scaling_)
{
	const int scaling = (int)scaling_;
	const bool flip = (setup->flags & TRIANGLE_SETUP_FLIP_BIT) != 0;
	int start_y, end_y, ym;
	int32_t start_x, end_x;
	x_span_t upper, lower, mid, mid1;

	if (setup->yl <= setup->yh)
		return 0;

	start_y = max32((int)scissor->ylo, setup->yh & ~(SUBPIXELS_Y - 1));
	end_y = min32((int)scissor->yhi - 1, (setup->yl - 1) | (SUBPIXELS_Y - 1));
	start_y *= scaling;
	end_y *= scaling;

	/* Y is clipped out, exit early. */
	if (end_y < start_y)
		return 0;

	upper = interpolate_x(setup, start_y, flip, scaling);
	lower = interpolate_x(setup, end_y, flip, scaling);
	mid = upper;
	mid1 = upper;

	ym = scaling * setup->ym;
	if (ym > start_y && ym < end_y)
	{
		mid = interpolate_x(setup, ym, flip, scaling);
		mid1 = interpolate_x(setup, ym - 1, flip, scaling);
	}

	/* Robustness, check if we overflow the X rasterizer precision. After
	 * shifting down, we should have 12 bits signed. If we detect any
	 * overflow here we need to assume X range is scissor rect. This really
	 * should never happen, but it's possible to write tests that
	 * intentionally trigger weird overflow behavior that needs to be
	 * specially handled. There might be freak scenarios where we cannot
	 * detect overflow since we're only sampling at 4 scanlines and we have
	 * ~32 overflows happening at once, so we need to interpolate in 64-bit
	 * to make this work. */
	start_x = min32(min32(upper.first, lower.first), min32(mid.first, mid1.first));
	end_x = max32(max32(upper.second, lower.second), max32(mid.second, mid1.second));

	/* Effective X range is [-2048, 2047], but just make it [-2047, 2047]
	 * to match binning shader. If we interpolate X to something outside
	 * that range, we must assume the entire X range will be covered. */
	if (rdp_x_range_exceeds(start_x, end_x, 2047 * scaling))
	{
		start_x = 0;
		end_x = 0x7fffffff;
	}

	start_x = max32(start_x, scaling * ((int)scissor->xlo >> 2));
	end_x = min32(end_x, scaling * (((int)scissor->xhi + 3) >> 2) - 1);

	if (end_x < start_x)
		return 0;

	start_x /= TILE_WIDTH;
	end_x /= TILE_WIDTH;
	start_y /= SUBPIXELS_Y * TILE_HEIGHT;
	end_y /= SUBPIXELS_Y * TILE_HEIGHT;

	return (unsigned)((end_x - start_x + 1) * (end_y - start_y + 1));
}

void rdp_fixup_triangle_setup(rdp_triangle_setup_t *setup, uint32_t static_flags)
{
	/* If YM is lower than the first sub-scanline we rasterize, we will
	 * never observe YM at all. To account for this, fixup here so that YM
	 * is out of range. No known content actually triggers this, but some
	 * public tests triggered it. */
	const int start_y = setup->yh & ~(SUBPIXELS_Y - 1);
	if (setup->ym < start_y)
		setup->ym = INT16_MAX;

	if (static_flags & RASTERIZATION_INTERLACE_FIELD_BIT)
	{
		setup->flags |= TRIANGLE_SETUP_INTERLACE_FIELD_BIT;
		if (static_flags & RASTERIZATION_INTERLACE_KEEP_ODD_BIT)
			setup->flags |= TRIANGLE_SETUP_INTERLACE_KEEP_ODD_BIT;
	}

	/* Span size is inclusive, not exclusive. Rasterization is based on X
	 * range directly. */
	if (static_flags & (RASTERIZATION_COPY_BIT | RASTERIZATION_FILL_BIT))
		setup->flags |= TRIANGLE_SETUP_FILL_COPY_RASTER_BIT;
}

/* ---- what the combiner reads ---- */
static bool accesses_texel0(const rdp_combiner_inputs_t *inputs)
{
	return inputs->rgb.muladd == RDP_RGB_MULADD_TEXEL0 ||
	       inputs->rgb.mulsub == RDP_RGB_MULSUB_TEXEL0 ||
	       inputs->rgb.mul == RDP_RGB_MUL_TEXEL0 ||
	       inputs->rgb.add == RDP_RGB_ADD_TEXEL0 ||
	       inputs->rgb.mul == RDP_RGB_MUL_TEXEL0_ALPHA ||
	       inputs->alpha.muladd == RDP_ALPHA_ADDSUB_TEXEL0_ALPHA ||
	       inputs->alpha.mulsub == RDP_ALPHA_ADDSUB_TEXEL0_ALPHA ||
	       inputs->alpha.mul == RDP_ALPHA_MUL_TEXEL0_ALPHA ||
	       inputs->alpha.add == RDP_ALPHA_ADDSUB_TEXEL0_ALPHA;
}

static bool accesses_lod_frac(const rdp_combiner_inputs_t *inputs)
{
	return inputs->rgb.mul == RDP_RGB_MUL_LOD_FRAC || inputs->alpha.mul == RDP_ALPHA_MUL_LOD_FRAC;
}

static bool accesses_texel1(const rdp_combiner_inputs_t *inputs)
{
	return inputs->rgb.muladd == RDP_RGB_MULADD_TEXEL1 ||
	       inputs->rgb.mulsub == RDP_RGB_MULSUB_TEXEL1 ||
	       inputs->rgb.mul == RDP_RGB_MUL_TEXEL1 ||
	       inputs->rgb.add == RDP_RGB_ADD_TEXEL1 ||
	       inputs->rgb.mul == RDP_RGB_MUL_TEXEL1_ALPHA ||
	       inputs->alpha.muladd == RDP_ALPHA_ADDSUB_TEXEL1_ALPHA ||
	       inputs->alpha.mulsub == RDP_ALPHA_ADDSUB_TEXEL1_ALPHA ||
	       inputs->alpha.mul == RDP_ALPHA_MUL_TEXEL1_ALPHA ||
	       inputs->alpha.add == RDP_ALPHA_ADDSUB_TEXEL1_ALPHA;
}

static bool uses_texel0(const rdp_static_state_t *state)
{
	/* Texel0 can be safely used in cycle0 of CYCLE2 mode, or in cycle1
	 * (only cycle) of CYCLE1 mode. In second cycle, Texel0 and Texel1
	 * swap around ... */
	if (state->flags & RASTERIZATION_MULTI_CYCLE_BIT)
		return accesses_texel0(&state->combiner[0]) || accesses_texel1(&state->combiner[1]);
	return accesses_texel0(&state->combiner[1]);
}

static bool uses_texel1(const rdp_static_state_t *state)
{
	/* Texel1 can be safely used in cycle0 of CYCLE2 mode, and never in
	 * cycle1 mode. Texel0 can be safely accessed in cycle1, which is an
	 * alias due to pipelining. */
	if (state->flags & RASTERIZATION_MULTI_CYCLE_BIT)
		return accesses_texel1(&state->combiner[0]) || accesses_texel0(&state->combiner[1]);
	return false;
}

static bool uses_pipelined_texel1(const rdp_static_state_t *state)
{
	/* If you access Texel1 in cycle1 mode, you end up reading the next
	 * pixel's color for whatever reason. */
	if (!(state->flags & RASTERIZATION_MULTI_CYCLE_BIT))
		return accesses_texel1(&state->combiner[1]);
	return false;
}

static bool uses_lod_frac(const rdp_static_state_t *state)
{
	if (state->flags & RASTERIZATION_MULTI_CYCLE_BIT)
		return accesses_lod_frac(&state->combiner[0]) || accesses_lod_frac(&state->combiner[1]);
	return false;
}

void rdp_deduce_noise_state(rdp_static_state_t *state)
{
	const bool multi = (state->flags & RASTERIZATION_MULTI_CYCLE_BIT) != 0;

	state->flags &= ~(uint32_t)(RASTERIZATION_NEED_NOISE_BIT | RASTERIZATION_NEED_NOISE_DUAL_BIT);

	/* Figure out if we need to seed noise variable for this primitive. */
	if ((state->dither & 3) == 2 || ((state->dither >> 2) & 3) == 2)
	{
		state->flags |= RASTERIZATION_NEED_NOISE_BIT;
		return;
	}

	if (state->flags & (RASTERIZATION_COPY_BIT | RASTERIZATION_FILL_BIT))
		return;

	if (multi && state->combiner[0].rgb.muladd == RDP_RGB_MULADD_NOISE)
		state->flags |= RASTERIZATION_NEED_NOISE_BIT;

	if (state->combiner[1].rgb.muladd == RDP_RGB_MULADD_NOISE)
		state->flags |= RASTERIZATION_NEED_NOISE_BIT;

	/* If both cycles use noise, they need to observe different values. */
	if (multi && state->combiner[0].rgb.muladd == RDP_RGB_MULADD_NOISE &&
	    state->combiner[1].rgb.muladd == RDP_RGB_MULADD_NOISE)
		state->flags |= RASTERIZATION_NEED_NOISE_DUAL_BIT;

	if ((state->flags & (RASTERIZATION_ALPHA_TEST_BIT | RASTERIZATION_ALPHA_TEST_DITHER_BIT)) ==
	    (RASTERIZATION_ALPHA_TEST_BIT | RASTERIZATION_ALPHA_TEST_DITHER_BIT))
		state->flags |= RASTERIZATION_NEED_NOISE_BIT;
}

void rdp_deduce_static_texture_state(rdp_static_state_t *state, const rdp_tile_info_t *tiles, unsigned tile,
		unsigned max_lod_level)
{
	const unsigned fmt = tiles[tile].meta.fmt;
	const unsigned siz = tiles[tile].meta.size;

	state->flags &= ~(uint32_t)RASTERIZATION_USE_STATIC_TEXTURE_SIZE_FORMAT_BIT;
	state->texture_size = 0;
	state->texture_fmt = 0;

	if (state->flags & RASTERIZATION_FILL_BIT)
		return;

	if (!(state->flags & RASTERIZATION_COPY_BIT))
	{
		/* If all tiles we sample have the same fmt and size (common case),
		 * we can use a static variant. */
		bool texel0 = uses_texel0(state);
		const bool texel1 = uses_texel1(state);
		const bool pipelined_texel1 = uses_pipelined_texel1(state);
		const bool lod_frac = uses_lod_frac(state);
		bool use_lod, use_detail, physical_texel1;
		unsigned i;

		if (texel1 && (state->flags & RASTERIZATION_CONVERT_ONE_BIT))
			texel0 = true;

		state->flags &= ~(uint32_t)(RASTERIZATION_USES_TEXEL0_BIT | RASTERIZATION_USES_TEXEL1_BIT |
				RASTERIZATION_USES_PIPELINED_TEXEL1_BIT | RASTERIZATION_USES_LOD_BIT);
		if (texel0)
			state->flags |= RASTERIZATION_USES_TEXEL0_BIT;
		if (texel1)
			state->flags |= RASTERIZATION_USES_TEXEL1_BIT;
		if (pipelined_texel1)
			state->flags |= RASTERIZATION_USES_PIPELINED_TEXEL1_BIT;
		if (lod_frac || (state->flags & RASTERIZATION_TEX_LOD_ENABLE_BIT))
			state->flags |= RASTERIZATION_USES_LOD_BIT;

		if (!texel0 && !texel1 && !pipelined_texel1)
			return;

		use_lod = (state->flags & RASTERIZATION_TEX_LOD_ENABLE_BIT) != 0;
		use_detail = (state->flags & RASTERIZATION_DETAIL_LOD_ENABLE_BIT) != 0;
		physical_texel1 = texel1 && (!(state->flags & RASTERIZATION_CONVERT_ONE_BIT) ||
				(state->flags & RASTERIZATION_BILERP_1_BIT));

		if (!use_lod)
			max_lod_level = physical_texel1 ? 1 : 0;
		if (use_detail)
			max_lod_level++;
		if (max_lod_level > 7)
			max_lod_level = 7;

		for (i = 1; i <= max_lod_level; i++)
		{
			const rdp_tile_meta_t *t = &tiles[(tile + i) & 7].meta;
			if (t->fmt != fmt || t->size != siz)
				return;
		}
	}

	/* We have a static format. */
	state->flags |= RASTERIZATION_USE_STATIC_TEXTURE_SIZE_FORMAT_BIT;
	state->texture_fmt = fmt;
	state->texture_size = siz;
}

/* ---- normalizing ---- */
static uint8_t normalize_rgb_muladd(uint8_t v)
{
	switch (v)
	{
	case RDP_RGB_MULADD_NOISE:
	case RDP_RGB_MULADD_TEXEL0:
	case RDP_RGB_MULADD_TEXEL1:
	case RDP_RGB_MULADD_COMBINED:
	case RDP_RGB_MULADD_ONE:
	case RDP_RGB_MULADD_SHADE:
		return v;
	default:
		return RDP_RGB_MULADD_ZERO;
	}
}

static uint8_t normalize_rgb_mulsub(uint8_t v)
{
	switch (v)
	{
	case RDP_RGB_MULSUB_COMBINED:
	case RDP_RGB_MULSUB_TEXEL0:
	case RDP_RGB_MULSUB_TEXEL1:
	case RDP_RGB_MULSUB_SHADE:
	case RDP_RGB_MULSUB_CONVERT_K4:
		return v;
	default:
		return RDP_RGB_MULSUB_ZERO;
	}
}

static uint8_t normalize_rgb_mul(uint8_t v)
{
	switch (v)
	{
	case RDP_RGB_MUL_COMBINED:
	case RDP_RGB_MUL_COMBINED_ALPHA:
	case RDP_RGB_MUL_TEXEL0:
	case RDP_RGB_MUL_TEXEL1:
	case RDP_RGB_MUL_TEXEL0_ALPHA:
	case RDP_RGB_MUL_TEXEL1_ALPHA:
	case RDP_RGB_MUL_SHADE:
	case RDP_RGB_MUL_SHADE_ALPHA:
	case RDP_RGB_MUL_LOD_FRAC:
	case RDP_RGB_MUL_CONVERT_K5:
		return v;
	default:
		return RDP_RGB_MUL_ZERO;
	}
}

static uint8_t normalize_rgb_add(uint8_t v)
{
	switch (v)
	{
	case RDP_RGB_ADD_TEXEL0:
	case RDP_RGB_ADD_TEXEL1:
	case RDP_RGB_ADD_COMBINED:
	case RDP_RGB_ADD_ONE:
	case RDP_RGB_ADD_SHADE:
		return v;
	default:
		return RDP_RGB_ADD_ZERO;
	}
}

static uint8_t normalize_alpha_addsub(uint8_t v)
{
	switch (v)
	{
	case RDP_ALPHA_ADDSUB_COMBINED_ALPHA:
	case RDP_ALPHA_ADDSUB_TEXEL0_ALPHA:
	case RDP_ALPHA_ADDSUB_TEXEL1_ALPHA:
	case RDP_ALPHA_ADDSUB_SHADE_ALPHA:
	case RDP_ALPHA_ADDSUB_ONE:
		return v;
	default:
		return RDP_ALPHA_ADDSUB_ZERO;
	}
}

static uint8_t normalize_alpha_mul(uint8_t v)
{
	switch (v)
	{
	case RDP_ALPHA_MUL_LOD_FRAC:
	case RDP_ALPHA_MUL_TEXEL0_ALPHA:
	case RDP_ALPHA_MUL_TEXEL1_ALPHA:
	case RDP_ALPHA_MUL_SHADE_ALPHA:
		return v;
	default:
		return RDP_ALPHA_MUL_ZERO;
	}
}

/* (the constant inputs come from the derived setup: the shaders read
 * them all as one) */
static void normalize_combiner(rdp_combiner_inputs_t *comb)
{
	comb->rgb.muladd = normalize_rgb_muladd(comb->rgb.muladd);
	comb->rgb.mulsub = normalize_rgb_mulsub(comb->rgb.mulsub);
	comb->rgb.mul = normalize_rgb_mul(comb->rgb.mul);
	comb->rgb.add = normalize_rgb_add(comb->rgb.add);
	comb->alpha.muladd = normalize_alpha_addsub(comb->alpha.muladd);
	comb->alpha.mulsub = normalize_alpha_addsub(comb->alpha.mulsub);
	comb->alpha.mul = normalize_alpha_mul(comb->alpha.mul);
	comb->alpha.add = normalize_alpha_addsub(comb->alpha.add);
}

void rdp_normalize_static_state(rdp_static_state_t *state)
{
	if (state->flags & RASTERIZATION_FILL_BIT)
	{
		memset(state, 0, sizeof(*state));
		state->flags = RASTERIZATION_FILL_BIT;
		return;
	}

	if (state->flags & RASTERIZATION_COPY_BIT)
	{
		const uint32_t flags = state->flags & (RASTERIZATION_COPY_BIT | RASTERIZATION_TLUT_BIT |
				RASTERIZATION_TLUT_TYPE_BIT | RASTERIZATION_USES_TEXEL0_BIT |
				RASTERIZATION_USE_STATIC_TEXTURE_SIZE_FORMAT_BIT | RASTERIZATION_TEX_LOD_ENABLE_BIT |
				RASTERIZATION_DETAIL_LOD_ENABLE_BIT | RASTERIZATION_PERSPECTIVE_CORRECT_BIT |
				RASTERIZATION_ALPHA_TEST_BIT);
		const uint32_t fmt = state->texture_fmt;
		const uint32_t siz = state->texture_size;
		memset(state, 0, sizeof(*state));
		state->flags = flags;
		state->texture_fmt = fmt;
		state->texture_size = siz;
		return;
	}

	if (!(state->flags & (RASTERIZATION_MULTI_CYCLE_BIT | RASTERIZATION_USES_PIPELINED_TEXEL1_BIT)))
		state->flags &= ~(uint32_t)(RASTERIZATION_BILERP_1_BIT | RASTERIZATION_CONVERT_ONE_BIT);

	normalize_combiner(&state->combiner[0]);
	normalize_combiner(&state->combiner[1]);
}
