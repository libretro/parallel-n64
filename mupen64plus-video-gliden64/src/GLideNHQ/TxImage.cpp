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

/* use power of 2 texture size
 * (0:disable, 1:enable, 2:3dfx) */
#define POW2_TEXTURES 0

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <formats/image.h>
#include <formats/rpng.h>
#include <streams/file_stream.h>

#include "TxImage.h"
#include "TxReSample.h"

/* Decode a PNG into a 32-bit RGBA8888 surface.
 *
 * rpng handles internally everything the libpng transform stack was
 * configured for here - 16-bit strip to 8, palette expansion, grey and
 * grey+alpha promotion to RGB, tRNS to alpha, filler alpha for RGB, and
 * Adam7 interlacing - and always yields 8-bit RGBA, so the per-colour-type
 * branching is gone.
 *
 * supports_rgba selects R,G,B,A byte order, which is what this function has
 * always returned (note the disabled png_set_bgr call in the original: "OpenGL
 * does not need it") and what the texture upload path expects. */
uint8*
TxImage::readPNG(const char *path, int* width, int* height, ColorFormat *format)
{
	void     *filebuf = nullptr;
	int64_t   filelen = 0;
	rpng_t   *rpng    = nullptr;
	uint32   *data    = nullptr;
	unsigned  o_width = 0, o_height = 0;
	int       ret     = 0;

	/* initialize */
	*width  = 0;
	*height = 0;
	*format = graphics::internalcolorFormat::NOCOLOR;

	/* rpng decodes from a resident buffer, so the file is pulled in with
	 * one sized read. Texture packs are tens of thousands of small PNGs
	 * and this is the hot path when one is first scanned. */
	if (!path || !filestream_read_file(path, &filebuf, &filelen) || filelen <= 0) {
		free(filebuf);
		return nullptr;
	}

	if (!(rpng = rpng_alloc())) {
		free(filebuf);
		return nullptr;
	}

	if (!rpng_set_buf_ptr(rpng, filebuf, (size_t)filelen) || !rpng_start(rpng)) {
		rpng_free(rpng);
		free(filebuf);
		return nullptr;
	}

	while (rpng_iterate_image(rpng))
		;

	do {
		ret = rpng_process_image(rpng, (void**)&data, (size_t)filelen, &o_width, &o_height, true);
	} while (ret == IMAGE_PROCESS_NEXT);

	rpng_free(rpng);
	free(filebuf);

	if (ret != IMAGE_PROCESS_END || !data) {
		if (data)
			free(data);
		return nullptr;
	}


	*width  = (int)o_width;
	*height = (int)o_height;
	*format = graphics::internalcolorFormat::RGBA8;

#if POW2_TEXTURES
	/* next power of 2 size conversions */
	{
		TxReSample *txReSample = new TxReSample;
#if (POW2_TEXTURES == 2)
		if (!txReSample->nextPow2((uint8**)&data, width, height, 32, 1)) {
#else
		if (!txReSample->nextPow2((uint8**)&data, width, height, 32, 0)) {
#endif
			if (data) {
				free(data);
				data = nullptr;
			}
			*width = 0;
			*height = 0;
			*format = graphics::internalcolorFormat::NOCOLOR;
		}
		delete txReSample;
	}
#endif /* POW2_TEXTURES */

	return (uint8*)data;
}

/* Encode a 32-bit RGBA8888 surface as a PNG.
 *
 * The source is RGBA in memory order, which is PNG's own colour-type-6
 * channel order, so rpng_save_image_rgba() writes it with no swizzle at all.
 * rpng_save_image_argb() would have required byte-swapping red and blue
 * across a frame-sized scratch buffer first, only for the encoder to swap
 * them back a line later.
 *
 * Takes a path rather than a FILE*: rpng opens the file itself through the
 * VFS, so the caller no longer needs its own fopen/fclose pair. */
boolean
TxImage::writePNG(uint8* src, const char *path, int width, int height, int rowStride, ColorFormat format)
{
	assert(format == graphics::internalcolorFormat::RGBA8);

	if (!src || !path)
		return 0;

	return rpng_save_image_rgba(path, src, (unsigned)width, (unsigned)height,
	                            (unsigned)rowStride) ? 1 : 0;
}

static uint32 bmp_u32(const uint8 *p)
{
	return (uint32)p[0] | ((uint32)p[1] << 8) | ((uint32)p[2] << 16) | ((uint32)p[3] << 24);
}

static uint16 bmp_u16(const uint8 *p)
{
	return (uint16)(p[0] | (p[1] << 8));
}

/* Decode an uncompressed Windows BMP held in memory.
 *
 * The header is read field by field in little-endian order, and every row
 * is bounds-checked against the file. It used to be fread into structs of
 * unsigned long, which on LP64 targets left the high four bytes of each
 * 32-bit field uninitialised, and short reads of the rows went unnoticed.
 *
 * NOTE: returned image format;
 *       4, 8bit palette bmp -> COLOR_INDEX8
 *       24, 32bit bmp -> RGBA8
 */
static uint8 *decodeBMP(const uint8 *file, size_t filelen, int *width, int *height, ColorFormat *format)
{
	/* BITMAPFILEHEADER is 14 bytes; BITMAPINFOHEADER follows */
	enum { FILE_HEADER = 14, INFO_HEADER = 40, MAX_SIDE = 16384 };
	uint8 *image;
	uint32 offBits, bitCount, compression, rows, x, y;
	int biWidth, biHeight;
	size_t row_bytes;

	if (filelen < FILE_HEADER + INFO_HEADER || memcmp(file, "BM", 2) != 0)
		return nullptr;
	/* is this a Windows BMP? */
	if (bmp_u32(file + 14) != INFO_HEADER)
		return nullptr;
	offBits = bmp_u32(file + 10);
	biWidth = (int)bmp_u32(file + 18);
	biHeight = (int)bmp_u32(file + 22);
	bitCount = bmp_u16(file + 28);
	compression = bmp_u32(file + 30);

	/* Rice hi-res textures */
	if (!(bitCount == 8 || bitCount == 4 || bitCount == 32 || bitCount == 24) || compression != 0)
		return nullptr;
	/* bottom-up rows only; a top-down (negative height) bitmap never loaded */
	if (biWidth <= 0 || biHeight <= 0 || biWidth > MAX_SIDE || biHeight > MAX_SIDE)
		return nullptr;

	/* rowStride in bytes, aligned to 4 bytes */
	row_bytes = (((size_t)biWidth * bitCount >> 3) + 3) & ~(size_t)3;
	rows = (uint32)biHeight;
	if (offBits > filelen || (filelen - offBits) / row_bytes < rows)
		return nullptr;

	switch (bitCount) {
	case 8:
	case 32:
		/* 8 bit, 32 bit bitmap */
		image = (uint8*)malloc(row_bytes * rows);
		if (image)
			for (y = 0; y < rows; y++)
				memcpy(image + row_bytes * y, file + offBits + row_bytes * (rows - 1 - y), row_bytes);
		break;
	case 4:
		/* 4bit bitmap: stuff 4bit values into 8bit comps */
		image = (uint8*)malloc((row_bytes * rows) << 1);
		if (image)
			for (y = 0; y < rows; y++) {
				const uint8 *src = file + offBits + row_bytes * (rows - 1 - y);
				uint8 *dst = image + (row_bytes << 1) * y;
				for (x = 0; x < row_bytes; x++) {
					dst[x << 1] = src[x] & 0x0f;
					dst[(x << 1) + 1] = (src[x] & 0xf0) >> 4;
				}
			}
		break;
	default:
		/* 24 bit bitmap to 32bpp */
		image = (uint8*)malloc(((size_t)biWidth * rows) << 2);
		if (image)
			for (y = 0; y < rows; y++) {
				const uint8 *src = file + offBits + row_bytes * (rows - 1 - y);
				uint8 *dst = image + ((size_t)biWidth << 2) * y;
				for (x = 0; x < (uint32)biWidth; x++) {
					dst[(x << 2)]     = src[x * 3];
					dst[(x << 2) + 1] = src[x * 3 + 1];
					dst[(x << 2) + 2] = src[x * 3 + 2];
					dst[(x << 2) + 3] = 0xFF;
				}
			}
		break;
	}

	if (image) {
		*width = (int)((row_bytes << 3) / bitCount);
		*height = biHeight;
		*format = (bitCount <= 8) ? graphics::internalcolorFormat::COLOR_INDEX8 : graphics::internalcolorFormat::RGBA8;
	}
	return image;
}

uint8*
TxImage::readBMP(const char *path, int* width, int* height, ColorFormat *format)
{
	void *file = nullptr;
	int64_t filelen = 0;
	uint8 *image = nullptr;

	/* initialize */
	*width  = 0;
	*height = 0;
	*format = graphics::internalcolorFormat::NOCOLOR;

	if (path && filestream_read_file(path, &file, &filelen) && filelen > 0)
		image = decodeBMP((const uint8*)file, (size_t)filelen, width, height, format);
	free(file);

#if POW2_TEXTURES
	/* next power of 2 size conversions */
	/* NOTE: I can do this in the above loop for faster operations, but some
	 * texture packs require a workaround. see HACKALERT in nextPow2().
	 */
	if (image) {
		TxReSample *txReSample = new TxReSample; // XXX: temporary. move to a better place.

#if (POW2_TEXTURES == 2)
		if (!txReSample->nextPow2(&image, width, height, 8, 1)) {
#else
		if (!txReSample->nextPow2(&image, width, height, 8, 0)) {
#endif
			if (image) {
				free(image);
				image = nullptr;
			}
			*width = 0;
			*height = 0;
			*format = graphics::internalcolorFormat::NOCOLOR;
		}

		delete txReSample;
	}
#endif /* POW2_TEXTURES */

	return image;
}
