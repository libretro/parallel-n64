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
#include "rdp_command_decode.h"

/* FBFormat */
enum
{
	FB_I4 = 0,
	FB_I8,
	FB_RGBA5551,
	FB_IA88,
	FB_RGBA8888
};

static void set_flag(uint32_t *flags, bool on, uint32_t mask)
{
	*flags &= ~mask;
	if (on)
		*flags |= mask;
}

void rdp_decode_color_image(const uint32_t *words, uint32_t *addr, uint32_t *width, uint32_t *fb_fmt,
		unsigned *size)
{
	const unsigned fmt = (words[0] >> 21) & 7;
	*size = (words[0] >> 19) & 3;
	*width = (words[0] & 1023) + 1;
	*addr = words[1] & 0xffffff;
	switch (*size)
	{
		case 0:
			*fb_fmt = FB_I4;
			break;
		case 1:
			*fb_fmt = FB_I8;
			break;
		case 2:
			*fb_fmt = fmt ? FB_IA88 : FB_RGBA5551;
			break;
		default:
			*fb_fmt = FB_RGBA8888;
			break;
	}
}

void rdp_decode_scissor(const uint32_t *words, rdp_scissor_state_t *scissor, uint32_t *static_flags)
{
	scissor->xlo = (words[0] >> 12) & 0xfff;
	scissor->xhi = (words[1] >> 12) & 0xfff;
	scissor->ylo = (words[0] >> 0) & 0xfff;
	scissor->yhi = (words[1] >> 0) & 0xfff;
	set_flag(static_flags, (words[1] & (1u << 25)) != 0, RASTERIZATION_INTERLACE_FIELD_BIT);
	set_flag(static_flags, (words[1] & (1u << 24)) != 0, RASTERIZATION_INTERLACE_KEEP_ODD_BIT);
}

bool rdp_decode_other_modes(const uint32_t *words, rdp_static_state_t *state, rdp_depth_blend_state_t *depth_blend)
{
	static const struct
	{
		uint8_t word, bit;
		uint32_t flag;
	} static_bits[] = {
		{ 0, 19, RASTERIZATION_PERSPECTIVE_CORRECT_BIT },
		{ 0, 18, RASTERIZATION_DETAIL_LOD_ENABLE_BIT },
		{ 0, 17, RASTERIZATION_SHARPEN_LOD_ENABLE_BIT },
		{ 0, 16, RASTERIZATION_TEX_LOD_ENABLE_BIT },
		{ 0, 15, RASTERIZATION_TLUT_BIT },
		{ 0, 14, RASTERIZATION_TLUT_TYPE_BIT },
		{ 0, 13, RASTERIZATION_SAMPLE_MODE_BIT },
		{ 0, 12, RASTERIZATION_SAMPLE_MID_TEXEL_BIT },
		{ 0, 11, RASTERIZATION_BILERP_0_BIT },
		{ 0, 10, RASTERIZATION_BILERP_1_BIT },
		{ 0, 9, RASTERIZATION_CONVERT_ONE_BIT },
		{ 1, 13, RASTERIZATION_ALPHA_CVG_SELECT_BIT },
		{ 1, 12, RASTERIZATION_CVG_TIMES_ALPHA_BIT },
		{ 1, 3, RASTERIZATION_AA_BIT },
		{ 1, 1, RASTERIZATION_ALPHA_TEST_DITHER_BIT },
		{ 1, 0, RASTERIZATION_ALPHA_TEST_BIT },
	}, depth_bits[] = {
		{ 1, 14, DEPTH_BLEND_FORCE_BLEND_BIT },
		{ 1, 7, DEPTH_BLEND_COLOR_ON_COVERAGE_BIT },
		{ 1, 6, DEPTH_BLEND_IMAGE_READ_ENABLE_BIT },
		{ 1, 5, DEPTH_BLEND_DEPTH_UPDATE_BIT },
		{ 1, 4, DEPTH_BLEND_DEPTH_TEST_BIT },
		{ 1, 3, DEPTH_BLEND_AA_BIT },
	};
	const uint32_t w1 = words[1];
	unsigned i;

	for (i = 0; i < sizeof(static_bits) / sizeof(static_bits[0]); i++)
		set_flag(&state->flags, (words[static_bits[i].word] & (1u << static_bits[i].bit)) != 0, static_bits[i].flag);
	for (i = 0; i < sizeof(depth_bits) / sizeof(depth_bits[0]); i++)
		set_flag(&depth_blend->flags, (words[depth_bits[i].word] & (1u << depth_bits[i].bit)) != 0,
				depth_bits[i].flag);

	state->dither = (words[0] >> 4) & 0x0f;
	set_flag(&depth_blend->flags, (state->dither >> 2) != RDP_RGB_DITHER_OFF, DEPTH_BLEND_DITHER_ENABLE_BIT);
	depth_blend->coverage_mode = (uint8_t)((w1 >> 8) & 3);
	depth_blend->z_mode = (uint8_t)((w1 >> 10) & 3);

	state->flags &= ~(uint32_t)(RASTERIZATION_MULTI_CYCLE_BIT | RASTERIZATION_FILL_BIT | RASTERIZATION_COPY_BIT);
	depth_blend->flags &= ~(uint32_t)DEPTH_BLEND_MULTI_CYCLE_BIT;
	switch ((words[0] >> 20) & 3)
	{
		case RDP_CYCLE_2:
			state->flags |= RASTERIZATION_MULTI_CYCLE_BIT;
			depth_blend->flags |= DEPTH_BLEND_MULTI_CYCLE_BIT;
			break;
		case RDP_CYCLE_FILL:
			state->flags |= RASTERIZATION_FILL_BIT;
			break;
		case RDP_CYCLE_COPY:
			state->flags |= RASTERIZATION_COPY_BIT;
			break;
		default:
			break;
	}

	depth_blend->blend_cycles[0].blend_1a = (uint8_t)((w1 >> 30) & 3);
	depth_blend->blend_cycles[1].blend_1a = (uint8_t)((w1 >> 28) & 3);
	depth_blend->blend_cycles[0].blend_1b = (uint8_t)((w1 >> 26) & 3);
	depth_blend->blend_cycles[1].blend_1b = (uint8_t)((w1 >> 24) & 3);
	depth_blend->blend_cycles[0].blend_2a = (uint8_t)((w1 >> 22) & 3);
	depth_blend->blend_cycles[1].blend_2a = (uint8_t)((w1 >> 20) & 3);
	depth_blend->blend_cycles[0].blend_2b = (uint8_t)((w1 >> 18) & 3);
	depth_blend->blend_cycles[1].blend_2b = (uint8_t)((w1 >> 16) & 3);

	return (w1 & (1u << 2)) != 0;
}

void rdp_decode_texture_image(const uint32_t *words, rdp_texture_image_t *image)
{
	image->fmt = (uint8_t)((words[0] >> 21) & 7);
	image->size = (uint8_t)((words[0] >> 19) & 3);
	image->width = (words[0] & 0x3ff) + 1;
	image->addr = words[1] & 0x00ffffffu;
}

unsigned rdp_decode_tile(const uint32_t *words, rdp_tile_meta_t *meta)
{
	memset(meta, 0, sizeof(*meta));
	meta->offset = ((words[0] >> 0) & 511) << 3;
	meta->stride = ((words[0] >> 9) & 511) << 3;
	meta->size = (uint8_t)((words[0] >> 19) & 3);
	meta->fmt = (uint8_t)((words[0] >> 21) & 7);
	meta->palette = (uint8_t)((words[1] >> 20) & 15);
	meta->shift_s = (uint8_t)((words[1] >> 0) & 15);
	meta->mask_s = (uint8_t)((words[1] >> 4) & 15);
	meta->shift_t = (uint8_t)((words[1] >> 10) & 15);
	meta->mask_t = (uint8_t)((words[1] >> 14) & 15);

	if (words[1] & (1u << 8))
		meta->flags |= TILE_INFO_MIRROR_S_BIT;
	if (words[1] & (1u << 9))
		meta->flags |= TILE_INFO_CLAMP_S_BIT;
	if (words[1] & (1u << 18))
		meta->flags |= TILE_INFO_MIRROR_T_BIT;
	if (words[1] & (1u << 19))
		meta->flags |= TILE_INFO_CLAMP_T_BIT;

	if (meta->mask_s > 10)
		meta->mask_s = 10;
	else if (meta->mask_s == 0)
		meta->flags |= TILE_INFO_CLAMP_S_BIT;
	if (meta->mask_t > 10)
		meta->mask_t = 10;
	else if (meta->mask_t == 0)
		meta->flags |= TILE_INFO_CLAMP_T_BIT;

	return (words[1] >> 24) & 7;
}

unsigned rdp_decode_tile_size(const uint32_t *words, rdp_tile_size_t *size)
{
	size->slo = (words[0] >> 12) & 0xfff;
	size->shi = (words[1] >> 12) & 0xfff;
	size->tlo = (words[0] >> 0) & 0xfff;
	size->thi = (words[1] >> 0) & 0xfff;
	return (words[1] >> 24) & 7;
}

void rdp_decode_combine(const uint32_t *words, rdp_static_state_t *state)
{
	rdp_combiner_inputs_t *c = state->combiner;
	c[0].rgb.muladd = (uint8_t)((words[0] >> 20) & 0xf);
	c[0].rgb.mul = (uint8_t)((words[0] >> 15) & 0x1f);
	c[0].rgb.mulsub = (uint8_t)((words[1] >> 28) & 0xf);
	c[0].rgb.add = (uint8_t)((words[1] >> 15) & 0x7);
	c[0].alpha.muladd = (uint8_t)((words[0] >> 12) & 0x7);
	c[0].alpha.mulsub = (uint8_t)((words[1] >> 12) & 0x7);
	c[0].alpha.mul = (uint8_t)((words[0] >> 9) & 0x7);
	c[0].alpha.add = (uint8_t)((words[1] >> 9) & 0x7);

	c[1].rgb.muladd = (uint8_t)((words[0] >> 5) & 0xf);
	c[1].rgb.mul = (uint8_t)((words[0] >> 0) & 0x1f);
	c[1].rgb.mulsub = (uint8_t)((words[1] >> 24) & 0xf);
	c[1].rgb.add = (uint8_t)((words[1] >> 6) & 0x7);
	c[1].alpha.muladd = (uint8_t)((words[1] >> 21) & 0x7);
	c[1].alpha.mulsub = (uint8_t)((words[1] >> 3) & 0x7);
	c[1].alpha.mul = (uint8_t)((words[1] >> 18) & 0x7);
	c[1].alpha.add = (uint8_t)((words[1] >> 0) & 0x7);
}

void rdp_decode_convert(const uint32_t *words, uint16_t k[6])
{
	/* the two words as one 64-bit value: K0 at bit 45, K5 at bit 0 */
	k[5] = (uint16_t)(words[1] & 0x1ff);
	k[4] = (uint16_t)((words[1] >> 9) & 0x1ff);
	k[3] = (uint16_t)((words[1] >> 18) & 0x1ff);
	k[2] = (uint16_t)(((words[1] >> 27) | (words[0] << 5)) & 0x1ff);
	k[1] = (uint16_t)((words[0] >> 4) & 0x1ff);
	k[0] = (uint16_t)((words[0] >> 13) & 0x1ff);
}

void rdp_decode_key_gb(const uint32_t *words, rdp_color_key_t *g, rdp_color_key_t *b)
{
	g->width = (words[0] >> 12) & 0xfff;
	b->width = (words[0] >> 0) & 0xfff;
	g->center = (words[1] >> 24) & 0xff;
	g->scale = (words[1] >> 16) & 0xff;
	b->center = (words[1] >> 8) & 0xff;
	b->scale = (words[1] >> 0) & 0xff;
}

void rdp_decode_key_r(const uint32_t *words, rdp_color_key_t *r)
{
	r->width = (words[1] >> 16) & 0xfff;
	r->center = (words[1] >> 8) & 0xff;
	r->scale = (words[1] >> 0) & 0xff;
}

void rdp_decode_prim_color(const uint32_t *words, uint8_t *min_level, uint8_t *lod_frac, uint32_t *color)
{
	*min_level = (uint8_t)((words[0] >> 8) & 31);
	*lod_frac = (uint8_t)(words[0] & 0xff);
	*color = words[1];
}
