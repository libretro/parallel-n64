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
#include "primitive_decode.h"

/* The low @bits of @v as a signed number */
static int32_t sext(uint32_t v, unsigned bits)
{
	const uint32_t sign = (uint32_t)1 << (bits - 1);
	v &= (sign << 1) - 1;
	return (int32_t)(v ^ sign) - (int32_t)sign;
}

/* @v halved, rounding down */
static int32_t half(int32_t v)
{
	return (v - (v & 1)) / 2;
}

static void decode_edges(rdp_triangle_setup_t *setup, const uint32_t *words, unsigned flags)
{
	const bool flip = (words[0] & 0x800000u) != 0;
	const bool sign_dxhdy = (words[5] & 0x80000000u) != 0;

	setup->flags = 0;
	if (flip)
		setup->flags |= TRIANGLE_SETUP_FLIP_BIT;
	if (flip == sign_dxhdy)
		setup->flags |= TRIANGLE_SETUP_DO_OFFSET_BIT;
	if (flags & RDP_DECODE_COPY_CYCLE)
		setup->flags |= TRIANGLE_SETUP_SKIP_XFRAC_BIT;
	if (flags & RDP_DECODE_NATIVE_TEXTURE_LOD)
		setup->flags |= TRIANGLE_SETUP_NATIVE_LOD_BIT;

	setup->tile = (words[0] >> 16) & 63;

	setup->yl = (int16_t)sext(words[0], 14);
	setup->ym = (int16_t)sext(words[1] >> 16, 14);
	setup->yh = (int16_t)sext(words[1], 14);

	/* The lower bit is ignored, so shift here to obtain an extra bit of
	 * subpixel precision. This is very useful for upscaling, since we can
	 * obtain 8x before we overflow instead of 4x. */
	setup->xl = half(sext(words[2], 28));
	setup->xh = half(sext(words[4], 28));
	setup->xm = half(sext(words[6], 28));
	setup->dxldy = half(sext(words[3] >> 2, 28));
	setup->dxhdy = half(sext(words[5] >> 2, 28));
	setup->dxmdy = half(sext(words[7] >> 2, 28));
}

/* A coefficient's integer half in the high 16 bits of one word and its
 * fraction in the high 16 bits of another */
static int32_t high(uint32_t integer, uint32_t fraction)
{
	return (int32_t)((integer & 0xffff0000u) | ((fraction >> 16) & 0x0000ffffu));
}

/* The same, both in the low 16 bits */
static int32_t low(uint32_t integer, uint32_t fraction)
{
	return (int32_t)(((integer << 16) & 0xffff0000u) | (fraction & 0x0000ffffu));
}

static void decode_tex(rdp_attribute_setup_t *attr, const uint32_t *words)
{
	attr->s = high(words[0], words[4]);
	attr->t = low(words[0], words[4]);
	attr->w = high(words[1], words[5]);

	attr->dsdx = high(words[2], words[6]);
	attr->dtdx = low(words[2], words[6]);
	attr->dwdx = high(words[3], words[7]);

	attr->dsde = high(words[8], words[12]);
	attr->dtde = low(words[8], words[12]);
	attr->dwde = high(words[9], words[13]);

	attr->dsdy = high(words[10], words[14]);
	attr->dtdy = low(words[10], words[14]);
	attr->dwdy = high(words[11], words[15]);
}

static void decode_rgba(rdp_attribute_setup_t *attr, const uint32_t *words)
{
	attr->r = high(words[0], words[4]);
	attr->g = low(words[0], words[4]);
	attr->b = high(words[1], words[5]);
	attr->a = low(words[1], words[5]);

	attr->drdx = high(words[2], words[6]);
	attr->dgdx = low(words[2], words[6]);
	attr->dbdx = high(words[3], words[7]);
	attr->dadx = low(words[3], words[7]);

	attr->drde = high(words[8], words[12]);
	attr->dgde = low(words[8], words[12]);
	attr->dbde = high(words[9], words[13]);
	attr->dade = low(words[9], words[13]);

	attr->drdy = high(words[10], words[14]);
	attr->dgdy = low(words[10], words[14]);
	attr->dbdy = high(words[11], words[15]);
	attr->dady = low(words[11], words[15]);
}

unsigned rdp_triangle_words(unsigned opcode)
{
	return 8 + ((opcode & 4) ? 16 : 0) + ((opcode & 2) ? 16 : 0) + ((opcode & 1) ? 4 : 0);
}

void rdp_decode_triangle(const uint32_t *words, unsigned flags, rdp_triangle_setup_t *setup,
		rdp_attribute_setup_t *attr)
{
	const unsigned opcode = (words[0] >> 24) & 63;
	const uint32_t *block = words + 8;

	/* (the edges fill the setup: it has no padding) */
	memset(attr, 0, sizeof(*attr));
	decode_edges(setup, words, flags);

	if (opcode & 4)
	{
		decode_rgba(attr, block);
		block += 16;
	}
	else
	{
		/* A triangle without a shade block still feeds the shade
		 * registers to the combiner, and the loader latches the header
		 * doubleword into all eight slots of the block. Hardware verified,
		 * snapper64 "RDP Undefined Shade 1C", 64 of 64 hardware snapshots. */
		uint32_t header[16];
		unsigned i;
		for (i = 0; i < 16; i += 2)
		{
			header[i] = words[0];
			header[i + 1] = words[1];
		}
		decode_rgba(attr, header);
	}

	if (opcode & 2)
	{
		decode_tex(attr, block);
		block += 16;
	}

	if (opcode & 1)
	{
		attr->z = (int32_t)block[0];
		attr->dzdx = (int32_t)block[1];
		attr->dzde = (int32_t)block[2];
		attr->dzdy = (int32_t)block[3];
	}
	else
	{
		/* A triangle without a Z block still runs the Z pipe if the other
		 * modes enable it. Hardware neither zeroes the Z coefficients nor
		 * keeps the previous primitive's, it latches the triangle's own
		 * header doubleword into both Z block slots. Hardware verified,
		 * diagnostic cartridge case 9:21 (carmiker/n64docs
		 * RDP_TESTCART_HARDWARE.txt, section 3.2). */
		attr->z = (int32_t)words[0];
		attr->dzdx = (int32_t)words[1];
		attr->dzde = (int32_t)words[0];
		attr->dzdy = (int32_t)words[1];
	}
}

/* A rectangle's edges: two vertical ones, 10.2 corners */
static void decode_rectangle(rdp_triangle_setup_t *setup, const uint32_t *words, unsigned flags)
{
	const uint32_t xl = (words[0] >> 12) & 0xfff;
	uint32_t yl = (words[0] >> 0) & 0xfff;
	const uint32_t xh = (words[1] >> 12) & 0xfff;
	const uint32_t yh = (words[1] >> 0) & 0xfff;

	/* (copy and fill draw the last line whole) */
	if (flags & (RDP_DECODE_COPY_CYCLE | RDP_DECODE_FILL_CYCLE))
		yl |= 3;

	memset(setup, 0, sizeof(*setup));
	setup->xh = (int32_t)(xh << 13);
	setup->xl = (int32_t)(xl << 13);
	setup->xm = (int32_t)(xl << 13);
	setup->ym = (int16_t)yl;
	setup->yl = (int16_t)yl;
	setup->yh = (int16_t)yh;
}

void rdp_decode_fill_rectangle(const uint32_t *words, unsigned flags, rdp_triangle_setup_t *setup)
{
	decode_rectangle(setup, words, flags);
	setup->flags = TRIANGLE_SETUP_FLIP_BIT | TRIANGLE_SETUP_DISABLE_UPSCALING_BIT;
}

void rdp_decode_texture_rectangle(const uint32_t *words, bool flip, unsigned flags, rdp_triangle_setup_t *setup,
		rdp_attribute_setup_t *attr)
{
	const uint32_t s = (words[2] >> 16) & 0xffff;
	const uint32_t t = (words[2] >> 0) & 0xffff;
	/* s5.10 steps, as s10.21 */
	const int32_t dsdx = sext(words[3] >> 16, 16) * 2048;
	const int32_t dtdy = sext(words[3] >> 0, 16) * 2048;

	decode_rectangle(setup, words, flags);
	setup->flags = TRIANGLE_SETUP_FLIP_BIT;
	/* (a flipped one is never upscaled) */
	if (flip || (flags & RDP_DECODE_NATIVE_TEX_RECT))
		setup->flags |= TRIANGLE_SETUP_DISABLE_UPSCALING_BIT;
	if (flags & RDP_DECODE_NATIVE_TEXTURE_LOD)
		setup->flags |= TRIANGLE_SETUP_NATIVE_LOD_BIT;
	if (flags & RDP_DECODE_COPY_CYCLE)
		setup->flags |= TRIANGLE_SETUP_SKIP_XFRAC_BIT;
	setup->tile = (words[1] >> 24) & 0x7;

	memset(attr, 0, sizeof(*attr));
	attr->s = (int32_t)(s << 16);
	attr->t = (int32_t)(t << 16);
	if (flip)
	{
		attr->dtdx = dtdy;
		attr->dsde = dsdx;
		attr->dsdy = dsdx;
	}
	else
	{
		attr->dsdx = dsdx;
		attr->dtde = dtdy;
		attr->dtdy = dtdy;
	}
}
