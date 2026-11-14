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

#ifndef RDP_PRIMITIVE_DECODE_H
#define RDP_PRIMITIVE_DECODE_H

#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>

RETRO_BEGIN_DECLS

enum TriangleSetupFlagBits
{
	TRIANGLE_SETUP_FLIP_BIT = 1 << 0,
	TRIANGLE_SETUP_DO_OFFSET_BIT = 1 << 1,
	TRIANGLE_SETUP_SKIP_XFRAC_BIT = 1 << 2,
	TRIANGLE_SETUP_INTERLACE_FIELD_BIT = 1 << 3,
	TRIANGLE_SETUP_INTERLACE_KEEP_ODD_BIT = 1 << 4,
	TRIANGLE_SETUP_DISABLE_UPSCALING_BIT = 1 << 5,
	TRIANGLE_SETUP_NATIVE_LOD_BIT = 1 << 6,
	TRIANGLE_SETUP_FILL_COPY_RASTER_BIT = 1 << 7
};

/* A primitive's edges, as the shaders read them: 32 bytes */
typedef struct rdp_triangle_setup
{
	int32_t xh, xm, xl;
	int16_t yh, ym;

	int32_t dxhdy, dxmdy, dxldy;
	int16_t yl;
	/* TRIANGLE_SETUP_* */
	uint8_t flags;
	uint8_t tile;
} rdp_triangle_setup_t;

/* A primitive's shade, texture and depth coefficients: 128 bytes */
typedef struct rdp_attribute_setup
{
	int32_t r, g, b, a;
	int32_t drdx, dgdx, dbdx, dadx;
	int32_t drde, dgde, dbde, dade;
	int32_t drdy, dgdy, dbdy, dady;

	int32_t s, t, z, w;
	int32_t dsdx, dtdx, dzdx, dwdx;
	int32_t dsde, dtde, dzde, dwde;
	int32_t dsdy, dtdy, dzdy, dwdy;
} rdp_attribute_setup_t;

/* What of the RDP's state a primitive's decoding depends on */
enum rdp_decode_flags
{
	/* the cycle type is copy */
	RDP_DECODE_COPY_CYCLE = 1 << 0,
	/* the cycle type is fill */
	RDP_DECODE_FILL_CYCLE = 1 << 1,
	/* quirk: texture LOD as at native resolution */
	RDP_DECODE_NATIVE_TEXTURE_LOD = 1 << 2,
	/* quirk: texture rectangles are not upscaled */
	RDP_DECODE_NATIVE_TEX_RECT = 1 << 3
};

/* A triangle command, 0x08 to 0x0f: bit 2 of the opcode says it has a
 * shade block, bit 1 a texture block, bit 0 a depth block, in that order
 * after the 8 words of edges. A block the command does not have reads
 * as hardware latches it: from the command's first two words. */
void rdp_decode_triangle(const uint32_t *words, unsigned flags, rdp_triangle_setup_t *setup,
		rdp_attribute_setup_t *attr);

/* How many words a triangle command of @opcode is */
unsigned rdp_triangle_words(unsigned opcode);

/* Fill rectangle, 0x36: 2 words */
void rdp_decode_fill_rectangle(const uint32_t *words, unsigned flags, rdp_triangle_setup_t *setup);

/* Texture rectangle, 0x24, and with s and t swapped (@flip), 0x25: 4 words */
void rdp_decode_texture_rectangle(const uint32_t *words, bool flip, unsigned flags, rdp_triangle_setup_t *setup,
		rdp_attribute_setup_t *attr);

RETRO_END_DECLS

#endif
