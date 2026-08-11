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
#include "TxDbg.h"

/* Read the whole of an already-open stream into one allocation.
 *
 * rpng decodes from a resident buffer, so the file is pulled in with a
 * single sized read instead of the incremental fread-per-chunk walk libpng
 * drove through png_init_io.  Texture packs are tens of thousands of small
 * PNGs and this is the hot path when one is first scanned. */
static uint8 *txReadWholeFile(FILE *fp, size_t *len_out)
{
	long  size;
	uint8 *buf;
	size_t got;

	*len_out = 0;

	if (fseek(fp, 0, SEEK_END) != 0)
		return nullptr;
	if ((size = ftell(fp)) <= 0) {
		return nullptr;
	}
	if (fseek(fp, 0, SEEK_SET) != 0)
		return nullptr;

	if (!(buf = (uint8*)malloc((size_t)size)))
		return nullptr;

	got = fread(buf, 1, (size_t)size, fp);
	if (got != (size_t)size) {
		free(buf);
		return nullptr;
	}

	*len_out = got;
	return buf;
}

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
TxImage::readPNG(FILE* fp, int* width, int* height, ColorFormat *format)
{
	uint8    *filebuf = nullptr;
	size_t    filelen = 0;
	rpng_t   *rpng    = nullptr;
	uint32   *data    = nullptr;
	unsigned  o_width = 0, o_height = 0;
	int       ret     = 0;

	/* initialize */
	*width  = 0;
	*height = 0;
	*format = graphics::internalcolorFormat::NOCOLOR;

	if (!fp)
		return nullptr;

	if (!(filebuf = txReadWholeFile(fp, &filelen))) {
		INFO(80, wst("error reading png file!\n"));
		return nullptr;
	}

	if (!(rpng = rpng_alloc())) {
		free(filebuf);
		return nullptr;
	}

	if (!rpng_set_buf_ptr(rpng, filebuf, filelen) || !rpng_start(rpng)) {
		INFO(80, wst("error reading png file! png image is corrupt.\n"));
		rpng_free(rpng);
		free(filebuf);
		return nullptr;
	}

	while (rpng_iterate_image(rpng))
		;

	do {
		ret = rpng_process_image(rpng, (void**)&data, filelen, &o_width, &o_height, true);
	} while (ret == IMAGE_PROCESS_NEXT);

	rpng_free(rpng);
	free(filebuf);

	if (ret != IMAGE_PROCESS_END || !data) {
		if (data)
			free(data);
		DBG_INFO(80, wst("Error: failed to load png image!\n"));
		return nullptr;
	}

	DBG_INFO(80, wst("png format %d x %d\n"), o_width, o_height);

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

boolean
TxImage::getBMPInfo(FILE* fp, BITMAPFILEHEADER* bmp_fhdr, BITMAPINFOHEADER* bmp_ihdr)
{
	/*
   * read in BITMAPFILEHEADER
   */

	/* is this a BMP file? */
	if (fread(&bmp_fhdr->bfType, 2, 1, fp) != 1)
		return 0;

	if (memcmp(&bmp_fhdr->bfType, "BM", 2) != 0)
		return 0;

	/* get file size */
	if (fread(&bmp_fhdr->bfSize, 4, 1, fp) != 1)
		return 0;

	/* reserved 1 */
	if (fread(&bmp_fhdr->bfReserved1, 2, 1, fp) != 1)
		return 0;

	/* reserved 2 */
	if (fread(&bmp_fhdr->bfReserved2, 2, 1, fp) != 1)
		return 0;

	/* offset to the image data */
	if (fread(&bmp_fhdr->bfOffBits, 4, 1, fp) != 1)
		return 0;

	/*
   * read in BITMAPINFOHEADER
   */

	/* size of BITMAPINFOHEADER */
	if (fread(&bmp_ihdr->biSize, 4, 1, fp) != 1)
		return 0;

	/* is this a Windows BMP? */
	if (bmp_ihdr->biSize != 40)
		return 0;

	/* width of the bitmap in pixels */
	if (fread(&bmp_ihdr->biWidth, 4, 1, fp) != 1)
		return 0;

	/* height of the bitmap in pixels */
	if (fread(&bmp_ihdr->biHeight, 4, 1, fp) != 1)
		return 0;

	/* number of planes (always 1) */
	if (fread(&bmp_ihdr->biPlanes, 2, 1, fp) != 1)
		return 0;

	/* number of bits-per-pixel. (1, 4, 8, 16, 24, 32) */
	if (fread(&bmp_ihdr->biBitCount, 2, 1, fp) != 1)
		return 0;

	/* compression for a compressed bottom-up bitmap
   *   0 : uncompressed format
   *   1 : run-length encoded 4 bpp format
   *   2 : run-length encoded 8 bpp format
   *   3 : bitfield
   */
	if (fread(&bmp_ihdr->biCompression, 4, 1, fp) != 1)
		return 0;

	/* size of the image in bytes */
	if (fread(&bmp_ihdr->biSizeImage, 4, 1, fp) != 1)
		return 0;

	/* horizontal resolution in pixels-per-meter */
	if (fread(&bmp_ihdr->biXPelsPerMeter, 4, 1, fp) != 1)
		return 0;

	/* vertical resolution in pixels-per-meter */
	if (fread(&bmp_ihdr->biYPelsPerMeter, 4, 1, fp) != 1)
		return 0;

	/* number of color indexes in the color table that are actually used */
	if (fread(&bmp_ihdr->biClrUsed, 4, 1, fp) != 1)
		return 0;

	/*  the number of color indexes that are required for displaying */
	if (fread(&bmp_ihdr->biClrImportant, 4, 1, fp) != 1)
		return 0;

	return 1;
}

uint8*
TxImage::readBMP(FILE* fp, int* width, int* height, ColorFormat *format)
{
	/* NOTE: returned image format;
   *       4, 8bit palette bmp -> COLOR_INDEX8
   *       24, 32bit bmp -> RGBA8
   */

	uint8 *image = nullptr;
	uint8 *image_row = nullptr;
	uint8 *tmpimage = nullptr;
	int row_bytes, pos, i, j;
	/* Windows Bitmap */
	BITMAPFILEHEADER bmp_fhdr;
	BITMAPINFOHEADER bmp_ihdr;

	/* initialize */
	*width  = 0;
	*height = 0;
	*format = graphics::internalcolorFormat::NOCOLOR;

	/* check if we have a valid bmp file */
	if (!fp)
		return nullptr;

	if (!getBMPInfo(fp, &bmp_fhdr, &bmp_ihdr)) {
		INFO(80, wst("error reading bitmap file! bitmap image is corrupt.\n"));
		return nullptr;
	}

	DBG_INFO(80, wst("bmp format %d x %d bitdepth:%d compression:%x offset:%d\n"),
			 bmp_ihdr.biWidth, bmp_ihdr.biHeight, bmp_ihdr.biBitCount,
			 bmp_ihdr.biCompression, bmp_fhdr.bfOffBits);

	/* rowStride in bytes */
	row_bytes = (bmp_ihdr.biWidth * bmp_ihdr.biBitCount) >> 3;
	/* align to 4bytes boundary */
	row_bytes = (row_bytes + 3) & ~3;

	/* Rice hi-res textures */
	if (!(bmp_ihdr.biBitCount == 8 || bmp_ihdr.biBitCount == 4 || bmp_ihdr.biBitCount == 32 || bmp_ihdr.biBitCount == 24) ||
			bmp_ihdr.biCompression != 0) {
		DBG_INFO(80, wst("Error: incompatible bitmap format!\n"));
		return nullptr;
	}

	switch (bmp_ihdr.biBitCount) {
	case 8:
	case 32:
		/* 8 bit, 32 bit bitmap */
		image = (uint8*)malloc(row_bytes * bmp_ihdr.biHeight);
		if (image) {
			tmpimage = image;
			pos = bmp_fhdr.bfOffBits + row_bytes * (bmp_ihdr.biHeight - 1);
			for (i = 0; i < bmp_ihdr.biHeight; i++) {
				/* read in image */
				fseek(fp, pos, SEEK_SET);
				fread(tmpimage, row_bytes, 1, fp);
				tmpimage += row_bytes;
				pos -= row_bytes;
			}
		}
	break;
	case 4:
		/* 4bit bitmap */
		image = (uint8*)malloc((row_bytes * bmp_ihdr.biHeight) << 1);
		image_row = (uint8*)malloc(row_bytes);
		if (image && image_row) {
			tmpimage = image;
			pos = bmp_fhdr.bfOffBits + row_bytes * (bmp_ihdr.biHeight - 1);
			for (i = 0; i < bmp_ihdr.biHeight; i++) {
				/* read in image */
				fseek(fp, pos, SEEK_SET);
				fread(image_row, row_bytes, 1, fp);
				/* expand 4bpp to 8bpp. stuff 4bit values into 8bit comps. */
				for (j = 0; j < row_bytes; j++) {
					tmpimage[j << 1] = image_row[j] & 0x0f;
					tmpimage[(j << 1) + 1] = (image_row[j] & 0xf0) >> 4;
				}
				tmpimage += (row_bytes << 1);
				pos -= row_bytes;
			}
			free(image_row);
		} else {
			if (image_row) free(image_row);
			if (image) free(image);
			image = nullptr;
		}
	break;
	case 24:
		/* 24 bit bitmap */
		image = (uint8*)malloc((bmp_ihdr.biWidth * bmp_ihdr.biHeight) << 2);
		image_row = (uint8*)malloc(row_bytes);
		if (image && image_row) {
			tmpimage = image;
			pos = bmp_fhdr.bfOffBits + row_bytes * (bmp_ihdr.biHeight - 1);
			for (i = 0; i < bmp_ihdr.biHeight; i++) {
				/* read in image */
				fseek(fp, pos, SEEK_SET);
				fread(image_row, row_bytes, 1, fp);
				/* convert 24bpp to 32bpp. */
				for (j = 0; j < bmp_ihdr.biWidth; j++) {
					tmpimage[(j << 2)]     = image_row[j * 3];
					tmpimage[(j << 2) + 1] = image_row[j * 3 + 1];
					tmpimage[(j << 2) + 2] = image_row[j * 3 + 2];
					tmpimage[(j << 2) + 3] = 0xFF;
				}
				tmpimage += (bmp_ihdr.biWidth << 2);
				pos -= row_bytes;
			}
			free(image_row);
		} else {
			if (image_row) free(image_row);
			if (image) free(image);
			image = nullptr;
		}
	}

	if (image) {
		*width = (row_bytes << 3) / bmp_ihdr.biBitCount;
		*height = bmp_ihdr.biHeight;

		switch (bmp_ihdr.biBitCount) {
		case 8:
		case 4:
			*format = graphics::internalcolorFormat::COLOR_INDEX8;
		break;
		case 32:
		case 24:
			*format = graphics::internalcolorFormat::RGBA8;
		}

#if POW2_TEXTURES
		/* next power of 2 size conversions */
		/* NOTE: I can do this in the above loop for faster operations, but some
	 * texture packs require a workaround. see HACKALERT in nextPow2().
	 */

		TxReSample txReSample = new TxReSample; // XXX: temporary. move to a better place.

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

#endif /* POW2_TEXTURES */
	}

#ifdef DEBUG
	if (!image) {
		DBG_INFO(80, wst("Error: failed to load bmp image!\n"));
	}
#endif

	return image;
}
