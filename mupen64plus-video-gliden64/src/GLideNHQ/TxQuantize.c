/*
 * Texture Filtering
 * Version:  1.0
 *
 * Copyright (C) 2007  Hiroshi Morii   All Rights Reserved.
 * Email koolsmoky(at)users.sourceforge.net
 * Web   http://www.3dfxzone.it/koolsmoky
 *
 * this is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * this is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Make; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* C89: converting between 32-bit RGBA and the 16-bit formats. */

#include <stdlib.h>

#include "TxQuantize.h"

static const unsigned char One2Eight[2] =
{
	0, /* 0 = 00000000 */
	255, /* 1 = 11111111 */
};

static const unsigned char Five2Eight[32] =
{
	0, /* 00000 = 00000000 */
	8, /* 00001 = 00001000 */
	16, /* 00010 = 00010000 */
	25, /* 00011 = 00011001 */
	33, /* 00100 = 00100001 */
	41, /* 00101 = 00101001 */
	49, /* 00110 = 00110001 */
	58, /* 00111 = 00111010 */
	66, /* 01000 = 01000010 */
	74, /* 01001 = 01001010 */
	82, /* 01010 = 01010010 */
	90, /* 01011 = 01011010 */
	99, /* 01100 = 01100011 */
	107, /* 01101 = 01101011 */
	115, /* 01110 = 01110011 */
	123, /* 01111 = 01111011 */
	132, /* 10000 = 10000100 */
	140, /* 10001 = 10001100 */
	148, /* 10010 = 10010100 */
	156, /* 10011 = 10011100 */
	165, /* 10100 = 10100101 */
	173, /* 10101 = 10101101 */
	181, /* 10110 = 10110101 */
	189, /* 10111 = 10111101 */
	197, /* 11000 = 11000101 */
	206, /* 11001 = 11001110 */
	214, /* 11010 = 11010110 */
	222, /* 11011 = 11011110 */
	230, /* 11100 = 11100110 */
	239, /* 11101 = 11101111 */
	247, /* 11110 = 11110111 */
	255 /* 11111 = 11111111 */
};

static void
ARGB1555_ARGB8888(uint32* src, uint32* dest, int width, int height)
{
	int i;
	const int siz = (width * height) >> 1;
	uint8 r, g, b, a;
	uint32 color;
	for (i = 0; i < siz; ++i) {
		color = (*src) & 0xffff;
		r = Five2Eight[color >> 11];
		g = Five2Eight[(color >> 6) & 0x001f];
		b = Five2Eight[(color >> 1) & 0x001f];
		a = One2Eight [(color     ) & 0x0001];
		*dest = ((uint32)a << 24) | ((uint32)b << 16) | ((uint32)g << 8) | r;
		++dest;
		color = (*src) >> 16;
		r = Five2Eight[color >> 11];
		g = Five2Eight[(color >> 6) & 0x001f];
		b = Five2Eight[(color >> 1) & 0x001f];
		a = One2Eight [(color     ) & 0x0001];
		*dest = ((uint32)a << 24) | ((uint32)b << 16) | ((uint32)g << 8) | r;
		++dest;
		++src;
	}
	/* an odd count's last texel, the low half of a pair */
	if ((width * height) & 1) {
		color = *(const uint16 *)src;
		r = Five2Eight[color >> 11];
		g = Five2Eight[(color >> 6) & 0x001f];
		b = Five2Eight[(color >> 1) & 0x001f];
		a = One2Eight [(color     ) & 0x0001];
		*dest = ((uint32)a << 24) | ((uint32)b << 16) | ((uint32)g << 8) | r;
	}
}

static void
ARGB4444_ARGB8888(uint32* src, uint32* dest, int width, int height)
{
	int i;
	const int siz = (width * height) >> 1;
	for (i = 0; i < siz; ++i) {
		*dest = ((*src & 0x0000f000) >> 8 ) |
				((*src & 0x00000f00) << 4 ) |
				((*src & 0x000000f0) << 16) |
				((*src & 0x0000000f) << 28);
		*dest |= (*dest >> 4);
		dest++;
		*dest =  ((*src & 0xf0000000) >> 24) |
				 ((*src & 0x0f000000) >> 12) |
				 ((*src & 0x00f00000) >> 0 ) |
				 ((*src & 0x000f0000) << 12);
		*dest |= (*dest >> 4);
		dest++;
		src++;
	}
	if ((width * height) & 1) {
		const uint32 s = *(const uint16 *)src;
		*dest = ((s & 0x0000f000) >> 8 ) |
				((s & 0x00000f00) << 4 ) |
				((s & 0x000000f0) << 16) |
				((s & 0x0000000f) << 28);
		*dest |= (*dest >> 4);
	}
}

static void
RGB565_ARGB8888(uint32* src, uint32* dest, int width, int height)
{
	int siz = (width * height) >> 1;
	int i;
	for (i = 0; i < siz; i++) {
		*dest = (0xff000000 |
				 ((*src & 0x0000f800) << 8) | ((*src & 0x0000e000) << 3) |
				 ((*src & 0x000007e0) << 5) | ((*src & 0x00000600) >> 1) |
				 ((*src & 0x0000001f) << 3) | ((*src & 0x0000001c) >> 2));
		dest++;
		*dest = (0xff000000 |
				 ((*src & 0xf8000000) >>  8) | ((*src & 0xe0000000) >> 13) |
				 ((*src & 0x07e00000) >> 11) | ((*src & 0x06000000) >> 17) |
				 ((*src & 0x001f0000) >> 13) | ((*src & 0x001c0000) >> 18));
		dest++;
		src++;
	}
	if ((width * height) & 1) {
		const uint32 s = *(const uint16 *)src;
		*dest = (0xff000000 |
				 ((s & 0x0000f800) << 8) | ((s & 0x0000e000) << 3) |
				 ((s & 0x000007e0) << 5) | ((s & 0x00000600) >> 1) |
				 ((s & 0x0000001f) << 3) | ((s & 0x0000001c) >> 2));
	}
}

static void
ARGB8888_ARGB1555(uint32* src, uint32* dest, int width, int height)
{
	int i;
	const int siz = (width * height) >> 1;
	uint32 color;
	uint32 r, g, b;
	for (i = 0; i < siz; i++) {
		color = *src;
		*dest = ((color & 0xff000000) ? 0x0001 : 0x0000);
		r = (color & 0x000000FF) >> 3;
		g = (color & 0x0000FF00) >> 11;
		b = (color & 0x00FF0000) >> 19;
		*dest |= (r<<11)|(g<<6)|(b<<1);
		src++;
		color = *src;
		*dest |= ((color & 0xff000000) ? 0x00010000 : 0x0000);
		r = (color & 0x000000FF) >> 3;
		g = (color & 0x0000FF00) >> 11;
		b = (color & 0x00FF0000) >> 19;
		*dest |= (r<<27)|(g<<22)|(b<<17);
		src++;
		dest++;
	}
	if ((width * height) & 1) {
		color = *src;
		r = (color & 0x000000FF) >> 3;
		g = (color & 0x0000FF00) >> 11;
		b = (color & 0x00FF0000) >> 19;
		*(uint16 *)dest = (uint16)(((color & 0xff000000) ? 0x0001 : 0x0000) | (r<<11)|(g<<6)|(b<<1));
	}
}

static void
ARGB8888_ARGB4444(uint32* src, uint32* dest, int width, int height)
{
	int i;
	const int siz = (width * height) >> 1;
	for (i = 0; i < siz; ++i) {
		*dest = (((*src & 0xf0000000) >> 28) |
				 ((*src & 0x00f00000) >> 16) |
				 ((*src & 0x0000f000) >> 4)  |
				 ((*src & 0x000000f0) << 8));
		src++;
		*dest |= (((*src & 0xf0000000) >> 12) |
				  ((*src & 0x00f00000) << 0)  |
				  ((*src & 0x0000f000) << 12) |
				  ((*src & 0x000000f0) << 24));
		src++;
		dest++;
	}
	if ((width * height) & 1)
		*(uint16 *)dest = (uint16)(((*src & 0xf0000000) >> 28) |
				 ((*src & 0x00f00000) >> 16) |
				 ((*src & 0x0000f000) >> 4)  |
				 ((*src & 0x000000f0) << 8));
}

static void
ARGB8888_RGB565(uint32* src, uint32* dest, int width, int height)
{
	int siz = (width * height) >> 1;
	int i;
	for (i = 0; i < siz; i++) {
		*dest = (((*src & 0x000000f8) >> 3) |
				 ((*src & 0x0000fc00) >> 5) |
				 ((*src & 0x00f80000) >> 8));
		src++;
		*dest |= (((*src & 0x000000f8) << 13) |
				  ((*src & 0x0000fc00) << 11) |
				  ((*src & 0x00f80000) << 8));
		src++;
		dest++;
	}
	if ((width * height) & 1)
		*(uint16 *)dest = (uint16)(((*src & 0x000000f8) >> 3) |
				 ((*src & 0x0000fc00) >> 5) |
				 ((*src & 0x00f80000) >> 8));
}

static void
ARGB8888_RGB565_ErrD(uint32* src, uint32* dst, int width, int height)
{
	/* Floyd-Steinberg error-diffusion halftoning */

	int x, y;
	int qr, qg, qb; /* quantized incoming values */
	int ir, ig, ib; /* incoming values */
	int t;
	/* the errors carried to the next row: one block, its three rows */
	int *errR = (int *)calloc(3 * (size_t)width, sizeof(int));
	int *errG, *errB;

	uint16 *dest = (uint16 *)dst;

	if (!errR)
		return;
	errG = errR + width;
	errB = errG + width;

	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			/* incoming pixel values */
			ir = ((*src >> 16) & 0xFF) * 10000;
			ig = ((*src >>  8) & 0xFF) * 10000;
			ib = ((*src      ) & 0xFF) * 10000;

			if (x == 0) qr = qg = qb = 0;

			/* quantize pixel values.
	   * qr * 0.4375 is the error from the pixel to the left,
	   * errR is the error from the pixel to the top, top left, and top right */
			/* qr * 0.4375 is the error distribution to the EAST in
	   * the previous loop */
			ir += errR[x] + qr * 4375 / 10000;
			ig += errG[x] + qg * 4375 / 10000;
			ib += errB[x] + qb * 4375 / 10000;

			/* error distribution to the SOUTH-EAST in the previous loop
	   * can't calculate in the previous loop because it steps on
	   * the above quantization */
			errR[x] = qr * 625 / 10000;
			errG[x] = qg * 625 / 10000;
			errB[x] = qb * 625 / 10000;

			qr = ir;
			qg = ig;
			qb = ib;

			/* clamp */
			if (qr < 0) qr = 0; else if (qr > 2550000) qr = 2550000;
			if (qg < 0) qg = 0; else if (qg > 2550000) qg = 2550000;
			if (qb < 0) qb = 0; else if (qb > 2550000) qb = 2550000;

			/* convert to RGB565 */
			qr = qr * 0x1F / 2550000;
			qg = qg * 0x3F / 2550000;
			qb = qb * 0x1F / 2550000;

			/* this is the dithered pixel */
			t  = (qr << 11) | (qg << 5) | qb;

			/* compute the errors */
			qr = ((qr << 3) | (qr >> 2)) * 10000;
			qg = ((qg << 2) | (qg >> 4)) * 10000;
			qb = ((qb << 3) | (qb >> 2)) * 10000;
			qr = ir - qr;
			qg = ig - qg;
			qb = ib - qb;

			/* compute the error distributions */
			/* Floyd-Steinberg filter
	   * 7/16 (=0.4375) to the EAST
	   * 5/16 (=0.3125) to the SOUTH
	   * 1/16 (=0.0625) to the SOUTH-EAST
	   * 3/16 (=0.1875) to the SOUTH-WEST
	   *
	   *         x    7/16
	   *  3/16  5/16  1/16
	   */
			/* SOUTH-WEST */
			if (x > 1) {
				errR[x - 1] += qr * 1875 / 10000;
				errG[x - 1] += qg * 1875 / 10000;
				errB[x - 1] += qb * 1875 / 10000;
			}

			/* SOUTH */
			errR[x] += qr * 3125 / 10000;
			errG[x] += qg * 3125 / 10000;
			errB[x] += qb * 3125 / 10000;

			*dest = (t & 0xFFFF);

			dest++;
			src++;
		}
	}

	free(errR);
}

static void
ARGB8888_ARGB1555_ErrD(uint32* src, uint32* dst, int width, int height)
{
	/* Floyd-Steinberg error-diffusion halftoning */

	int x, y;
	int qr, qg, qb; /* quantized incoming values */
	int ir, ig, ib; /* incoming values */
	int t;
	/* the errors carried to the next row: one block, its three rows */
	int *errR = (int *)calloc(3 * (size_t)width, sizeof(int));
	int *errG, *errB;

	uint16 *dest = (uint16 *)dst;

	if (!errR)
		return;
	errG = errR + width;
	errB = errG + width;

	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			/* incoming pixel values */
			ir = ((*src >> 16) & 0xFF) * 10000;
			ig = ((*src >>  8) & 0xFF) * 10000;
			ib = ((*src      ) & 0xFF) * 10000;

			if (x == 0) qr = qg = qb = 0;

			/* quantize pixel values.
	   * qr * 0.4375 is the error from the pixel to the left,
	   * errR is the error from the pixel to the top, top left, and top right */
			/* qr * 0.4375 is the error distribution to the EAST in
	   * the previous loop */
			ir += errR[x] + qr * 4375 / 10000;
			ig += errG[x] + qg * 4375 / 10000;
			ib += errB[x] + qb * 4375 / 10000;

			/* error distribution to the SOUTH-EAST of the previous loop.
	   * cannot calculate in the previous loop because it steps on
	   * the above quantization */
			errR[x] = qr * 625 / 10000;
			errG[x] = qg * 625 / 10000;
			errB[x] = qb * 625 / 10000;

			qr = ir;
			qg = ig;
			qb = ib;

			/* clamp */
			if (qr < 0) qr = 0; else if (qr > 2550000) qr = 2550000;
			if (qg < 0) qg = 0; else if (qg > 2550000) qg = 2550000;
			if (qb < 0) qb = 0; else if (qb > 2550000) qb = 2550000;

			/* convert to RGB555 */
			qr = qr * 0x1F / 2550000;
			qg = qg * 0x1F / 2550000;
			qb = qb * 0x1F / 2550000;

			/* this is the dithered pixel */
			t  = (qr << 10) | (qg << 5) | qb;
			t |= ((*src >> 24) ? 0x8000 : 0);

			/* compute the errors */
			qr = ((qr << 3) | (qr >> 2)) * 10000;
			qg = ((qg << 3) | (qg >> 2)) * 10000;
			qb = ((qb << 3) | (qb >> 2)) * 10000;
			qr = ir - qr;
			qg = ig - qg;
			qb = ib - qb;

			/* compute the error distributions */
			/* Floyd-Steinberg filter
	   * 7/16 (=0.4375) to the EAST
	   * 5/16 (=0.3125) to the SOUTH
	   * 1/16 (=0.0625) to the SOUTH-EAST
	   * 3/16 (=0.1875) to the SOUTH-WEST
	   *
	   *         x    7/16
	   *  3/16  5/16  1/16
	   */
			/* SOUTH-WEST */
			if (x > 1) {
				errR[x - 1] += qr * 1875 / 10000;
				errG[x - 1] += qg * 1875 / 10000;
				errB[x - 1] += qb * 1875 / 10000;
			}

			/* SOUTH */
			errR[x] += qr * 3125 / 10000;
			errG[x] += qg * 3125 / 10000;
			errB[x] += qb * 3125 / 10000;

			*dest = (t & 0xFFFF);

			dest++;
			src++;
		}
	}

	free(errR);
}

static void
ARGB8888_ARGB4444_ErrD(uint32* src, uint32* dst, int width, int height)
{
	/* Floyd-Steinberg error-diffusion halftoning */

	int x, y;
	int qr, qg, qb, qa; /* quantized incoming values */
	int ir, ig, ib, ia; /* incoming values */
	int t;
	/* the errors carried to the next row: one block, its four rows */
	int *errR = (int *)calloc(4 * (size_t)width, sizeof(int));
	int *errG, *errB, *errA;

	uint16 *dest = (uint16 *)dst;

	if (!errR)
		return;
	errG = errR + width;
	errB = errG + width;
	errA = errB + width;

	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			/* incoming pixel values */
			ir = ((*src >> 16) & 0xFF) * 10000;
			ig = ((*src >>  8) & 0xFF) * 10000;
			ib = ((*src      ) & 0xFF) * 10000;
			ia = ((*src >> 24) & 0xFF) * 10000;

			if (x == 0) qr = qg = qb = qa = 0;

			/* quantize pixel values.
	   * qr * 0.4375 is the error from the pixel to the left,
	   * errR is the error from the pixel to the top, top left, and top right */
			/* qr * 0.4375 is the error distribution to the EAST in
	   * the previous loop */
			ir += errR[x] + qr * 4375 / 10000;
			ig += errG[x] + qg * 4375 / 10000;
			ib += errB[x] + qb * 4375 / 10000;
			ia += errA[x] + qa * 4375 / 10000;

			/* error distribution to the SOUTH-EAST of the previous loop.
	   * cannot calculate in the previous loop because it steps on
	   * the above quantization */
			errR[x] = qr * 625 / 10000;
			errG[x] = qg * 625 / 10000;
			errB[x] = qb * 625 / 10000;
			errA[x] = qa * 625 / 10000;

			qr = ir;
			qg = ig;
			qb = ib;
			qa = ia;

			/* clamp */
			if (qr < 0) qr = 0; else if (qr > 2550000) qr = 2550000;
			if (qg < 0) qg = 0; else if (qg > 2550000) qg = 2550000;
			if (qb < 0) qb = 0; else if (qb > 2550000) qb = 2550000;
			if (qa < 0) qa = 0; else if (qa > 2550000) qa = 2550000;

			/* convert to RGB444 */
			qr = qr * 0xF / 2550000;
			qg = qg * 0xF / 2550000;
			qb = qb * 0xF / 2550000;
			qa = qa * 0xF / 2550000;

			t = (qr <<  8) | (qg << 4) | qb;
			t |= (*src >> 16) & 0xF000;

			/* compute the errors */
			qr = ((qr << 4) | qr) * 10000;
			qg = ((qg << 4) | qg) * 10000;
			qb = ((qb << 4) | qb) * 10000;
			qa = ((qa << 4) | qa) * 10000;
			qr = ir - qr;
			qg = ig - qg;
			qb = ib - qb;
			qa = ia - qa;

			/* compute the error distributions */
			/* Floyd-Steinberg filter
	   * 7/16 (=0.4375) to the EAST
	   * 5/16 (=0.3125) to the SOUTH
	   * 1/16 (=0.0625) to the SOUTH-EAST
	   * 3/16 (=0.1875) to the SOUTH-WEST
	   *
	   *         x    7/16
	   *  3/16  5/16  1/16
	   */
			/* SOUTH-WEST */
			if (x > 1) {
				errR[x - 1] += qr * 1875 / 10000;
				errG[x - 1] += qg * 1875 / 10000;
				errB[x - 1] += qb * 1875 / 10000;
				errA[x - 1] += qa * 1875 / 10000;
			}

			/* SOUTH */
			errR[x] += qr * 3125 / 10000;
			errG[x] += qg * 3125 / 10000;
			errB[x] += qb * 3125 / 10000;
			errA[x] += qa * 3125 / 10000;

			*dest = (t & 0xFFFF);

			dest++;
			src++;
		}
	}

	free(errR);
}


typedef void (*txq_fn)(uint32 *src, uint32 *dest, int width, int height);

/* One block of rows per lane; the last lane takes the remainder. */
typedef struct
{
	txq_fn fn;
	uint8 *src;
	uint8 *dest;
	int width;
	int height;
	int blkheight;
	unsigned lanes;
	unsigned srcStride;
	unsigned destStride;
} txq_job;

static void quantize_lane(void *ctx, unsigned lane)
{
	const txq_job *job = (const txq_job *)ctx;
	const int height = (lane == job->lanes - 1)
		? job->height - job->blkheight * (int)lane
		: job->blkheight;
	job->fn((uint32 *)(job->src + lane * job->srcStride),
			(uint32 *)(job->dest + lane * job->destStride),
			job->width, height);
}

/* @fn over the rows, split into blocks of a multiple of four rows when
 * there are lanes for them; @src_bpp, @dest_bpp bytes a texel */
static void run(txq_fn fn, uint8 *src, uint8 *dest, int width, int height, unsigned src_bpp, unsigned dest_bpp,
		txpool_t *pool, unsigned lanes)
{
	unsigned blkrow = 0;
	while (lanes > 1 && blkrow == 0) {
		blkrow = (unsigned)(height >> 2) / lanes;
		lanes--;
	}
	if (blkrow > 0 && lanes > 1) {
		txq_job job;
		job.fn         = fn;
		job.src        = src;
		job.dest       = dest;
		job.width      = width;
		job.height     = height;
		job.blkheight  = (int)(blkrow << 2);
		job.lanes      = lanes;
		job.srcStride  = (unsigned)width * (unsigned)job.blkheight * src_bpp;
		job.destStride = (unsigned)width * (unsigned)job.blkheight * dest_bpp;
		txpool_run(pool, quantize_lane, &job, lanes);
	} else {
		fn((uint32 *)src, (uint32 *)dest, width, height);
	}
}

void txquantize_p8_16bpp(const uint8 *src, uint16 *dest, int width, int height, const uint16 *palette)
{
	/* passed in palette is RGBA5551 format */
	const int size = width * height;
	int i;
	for (i = 0; i < size; i++) {
		const uint16 c = palette[src[i]];
		dest[i] = (uint16)((c << 15) | (c >> 1));
	}
}

boolean txquantize(uint8 *src, uint8 *dest, int width, int height, int srcformat, int destformat,
		boolean fast, txpool_t *pool, unsigned lanes)
{
	txq_fn quantizer;

	if (destformat == TXQ_RGBA8) {
		if (srcformat == TXQ_RGB5_A1)
			quantizer = ARGB1555_ARGB8888;
		else if (srcformat == TXQ_RGBA4)
			quantizer = ARGB4444_ARGB8888;
		else if (srcformat == TXQ_RGB565)
			quantizer = RGB565_ARGB8888;
		else
			return 0;
		run(quantizer, src, dest, width, height, 2, 4, pool, lanes);
	} else if (srcformat == TXQ_RGBA8) {
		if (destformat == TXQ_RGB5_A1)
			quantizer = fast ? ARGB8888_ARGB1555 : ARGB8888_ARGB1555_ErrD;
		else if (destformat == TXQ_RGBA4)
			quantizer = fast ? ARGB8888_ARGB4444 : ARGB8888_ARGB4444_ErrD;
		else if (destformat == TXQ_RGB565)
			quantizer = fast ? ARGB8888_RGB565 : ARGB8888_RGB565_ErrD;
		else
			return 0;
		run(quantizer, src, dest, width, height, 4, 2, pool, lanes);
	} else {
		return 0;
	}

	return 1;
}
