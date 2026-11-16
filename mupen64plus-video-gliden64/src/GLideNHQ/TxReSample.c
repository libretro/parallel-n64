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

#include "TxReSample.h"
#include <stdlib.h>
#include <string.h>

#define _USE_MATH_DEFINES
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

int txresample_pow2(int num)
{
	num = num - 1;
	num = num | (num >> 1);
	num = num | (num >> 2);
	num = num | (num >> 4);
	num = num | (num >> 8);
	num = num | (num >> 16);
	num = num + 1;

	return num;
}

boolean txresample_next_pow2(uint8 **image, int *width, int *height, int bpp, boolean use_3dfx)
{
	/* NOTE: bpp must be one of the follwing: 8, 16, 24, 32 bits per pixel */
	int row_bytes, o_row_bytes, o_width, n_width, o_height, n_height;
	uint8 *pow2image;

	if (!*image || !*width || !*height || !bpp)
		return 0;

	row_bytes = ((*width * bpp) >> 3);
	o_row_bytes = row_bytes;
	o_width = *width;
	n_width = *width;
	o_height = *height;
	n_height = *height;

	/* HACKALERT: I have explicitly subtracted (n) from width/height to
	 * adjust textures that have (n) pixel larger width/height than
	 * power of 2 size. This is a dirty hack for textures that have
	 * munged aspect ratio by (n) pixel to the original.
	 */
	if      (n_width  > 64) n_width  -= 4;
	else if (n_width  > 16) n_width  -= 2;
	else if (n_width  >  4) n_width  -= 1;

	if      (n_height > 64) n_height -= 4;
	else if (n_height > 16) n_height -= 2;
	else if (n_height >  4) n_height -= 1;

	n_width = txresample_pow2(n_width);
	n_height = txresample_pow2(n_height);
	row_bytes = (n_width * bpp) >> 3;

	/* 3dfx Glide3 format, W:H aspect ratio range (8:1 - 1:8) */
	if (use_3dfx) {
		if (n_width > n_height) {
			if (n_width > (n_height << 3))
				n_height = n_width >> 3;
		} else {
			if (n_height > (n_width << 3)) {
				n_width = n_height >> 3;
				row_bytes = (n_width * bpp) >> 3;
			}
		}
	}

	/* do we really need to do this ? */
	if (o_width == n_width && o_height == n_height)
		return 1; /* nope */

	if (o_width > n_width)
		o_width = n_width;

	if (o_height > n_height)
		o_height = n_height;

	/* allocate memory to read in image */
	pow2image = (uint8*)malloc(row_bytes * n_height);

	/* read in image */
	if (pow2image) {
		int i, j;
		uint8 *tmpimage = *image, *tmppow2image = pow2image;

		for (i = 0; i < o_height; i++) {
			/* copy row */
			memcpy(tmppow2image, tmpimage, ((o_width * bpp) >> 3));

			/* expand to pow2 size by replication */
			for (j = ((o_width * bpp) >> 3); j < row_bytes; j++)
				tmppow2image[j] = tmppow2image[j - (bpp >> 3)];

			tmppow2image += row_bytes;
			tmpimage += o_row_bytes;
		}
		/* expand to pow2 size by replication */
		for (i = o_height; i < n_height; i++)
			memcpy(&pow2image[row_bytes * i], &pow2image[row_bytes * (i - 1)], row_bytes);

		free(*image);

		*image = pow2image;
		*height = n_height;
		*width = n_width;

		return 1;
	}

	return 0;
}

/* Ken Turkowski
 * Filters for Common Resampling Tasks
 * Apple Computer 1990
 */
static double sinc(double x)
{
	if (x == 0) return 1.0;
	x *= M_PI;
	return (sin(x) / x);
}

/* J. F. Kaiser and W. A. Reed
 * Data smoothing using low-pass digital filters
 * Rev. Sci. instrum. 48 (11), pp1447-1457, 1977
 */
static double besselI0(double x)
{
	/* zero-order modified bessel function of the first kind */
	const double eps_coeff = 1E-16; /* small enough */
	double xh, sum, pw, ds;
	int k = 0;
	xh = 0.5 * x;
	sum = 1.0;
	pw = 1.0;
	ds = 1.0;
	while (ds > sum * eps_coeff) {
		k++;
		pw *= (xh / k);
		ds = pw * pw;
		sum = sum + ds;
	}
	return sum;
}

static double kaiser(double x)
{
	const double alpha = 4.0;
	const double half_window = 5.0;
	const double ratio = x / half_window;
	return sinc(x) * besselI0(alpha * sqrt(1 - ratio * ratio)) / besselI0(alpha);
}

boolean txresample_minify(uint8 **src, int *width, int *height, int ratio)
{
	/* NOTE: src must be ARGB8888, ratio is the inverse representation */

	/* half width of filter window, the Kaiser-Bessel window's: 5 */
	const double half_window = 5.0;

	int x, y, x2, y2, z;
	double A, R, G, B;
	uint32 texel;
	int tmpwidth, tmpheight;
	uint8 *tmptex, *workbuf;
	double *weight;

	if (!*src || ratio < 2) return 0;

	tmpwidth = *width / ratio;
	tmpheight = *height / ratio;

	/* resampled destination */
	tmptex = (uint8*)malloc((tmpwidth * tmpheight) << 2);
	if (!tmptex) return 0;

	/* work buffer. single row */
	workbuf = (uint8*)malloc(*width << 2);
	if (!workbuf) {
		free(tmptex);
		return 0;
	}

	/* prepare filter lookup table. only half width required for symetric filters. */
	weight = (double*)malloc((int)((half_window * ratio) * sizeof(double)));
	if (!weight) {
		free(tmptex);
		free(workbuf);
		return 0;
	}
	for (x = 0; x < half_window * ratio; x++)
		weight[x] = kaiser((double)x / ratio) / ratio;

	/* linear convolution */
	for (y = 0; y < tmpheight; y++) {
		for (x = 0; x < *width; x++) {
			texel = ((uint32*)*src)[y * ratio * *width + x];
			A = (double)(texel >> 24) * weight[0];
			R = (double)((texel >> 16) & 0xff) * weight[0];
			G = (double)((texel >>  8) & 0xff) * weight[0];
			B = (double)((texel      ) & 0xff) * weight[0];
			for (y2 = 1; y2 < half_window * ratio; y2++) {
				z = y * ratio + y2;
				if (z >= *height) z = *height - 1;
				texel = ((uint32*)*src)[z * *width + x];
				A += (double)(texel >> 24) * weight[y2];
				R += (double)((texel >> 16) & 0xff) * weight[y2];
				G += (double)((texel >>  8) & 0xff) * weight[y2];
				B += (double)((texel      ) & 0xff) * weight[y2];
				z = y * ratio - y2;
				if (z < 0) z = 0;
				texel = ((uint32*)*src)[z * *width + x];
				A += (double)(texel >> 24) * weight[y2];
				R += (double)((texel >> 16) & 0xff) * weight[y2];
				G += (double)((texel >>  8) & 0xff) * weight[y2];
				B += (double)((texel      ) & 0xff) * weight[y2];
			}
			if (A < 0) A = 0; else if (A > 255) A = 255;
			if (R < 0) R = 0; else if (R > 255) R = 255;
			if (G < 0) G = 0; else if (G > 255) G = 255;
			if (B < 0) B = 0; else if (B > 255) B = 255;
			((uint32*)workbuf)[x] = (((uint32)A << 24) | ((uint32)R << 16) | ((uint32)G << 8) | (uint32)B);
		}
		for (x = 0; x < tmpwidth; x++) {
			texel = ((uint32*)workbuf)[x * ratio];
			A = (double)(texel >> 24) * weight[0];
			R = (double)((texel >> 16) & 0xff) * weight[0];
			G = (double)((texel >>  8) & 0xff) * weight[0];
			B = (double)((texel      ) & 0xff) * weight[0];
			for (x2 = 1; x2 < half_window * ratio; x2++) {
				z = x * ratio + x2;
				if (z >= *width) z = *width - 1;
				texel = ((uint32*)workbuf)[z];
				A += (double)(texel >> 24) * weight[x2];
				R += (double)((texel >> 16) & 0xff) * weight[x2];
				G += (double)((texel >>  8) & 0xff) * weight[x2];
				B += (double)((texel      ) & 0xff) * weight[x2];
				z = x * ratio - x2;
				if (z < 0) z = 0;
				texel = ((uint32*)workbuf)[z];
				A += (double)(texel >> 24) * weight[x2];
				R += (double)((texel >> 16) & 0xff) * weight[x2];
				G += (double)((texel >>  8) & 0xff) * weight[x2];
				B += (double)((texel      ) & 0xff) * weight[x2];
			}
			if (A < 0) A = 0; else if (A > 255) A = 255;
			if (R < 0) R = 0; else if (R > 255) R = 255;
			if (G < 0) G = 0; else if (G > 255) G = 255;
			if (B < 0) B = 0; else if (B > 255) B = 255;
			((uint32*)tmptex)[y * tmpwidth + x] = (((uint32)A << 24) | ((uint32)R << 16) | ((uint32)G << 8) | (uint32)B);
		}
	}

	free(*src);
	*src = tmptex;
	free(weight);
	free(workbuf);
	*width = tmpwidth;
	*height = tmpheight;


	return 1;
}
