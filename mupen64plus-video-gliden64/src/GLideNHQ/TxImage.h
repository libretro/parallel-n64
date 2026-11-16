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

#ifndef __TXIMAGE_H__
#define __TXIMAGE_H__

/* GLideNHQ's hi-res texture files, PNG and BMP. C89. Paths are UTF-8,
 * read and written through the VFS. */

#include "Ext_TxFilter.h"

#ifdef __cplusplus
extern "C" {
#endif

/* the formats an image is read in: none (not read), RGBA8 (R, G, B, A in
 * memory), 8-bit palette indices */
enum
{
	TXI_NONE = 0,
	TXI_RGBA8,
	TXI_COLOR_INDEX8
};

/* The image, malloc'd, its size and format set; NULL (TXI_NONE, 0 x 0)
 * if it cannot be read. A PNG is always RGBA8; a BMP of 4 or 8 bits a
 * pixel indices (4 bits stuffed into 8), of 24 or 32 RGBA8. */
uint8 *tximage_read_png(const char *path, int *width, int *height, int *format);
uint8 *tximage_read_bmp(const char *path, int *width, int *height, int *format);
/* @src, RGBA8 rows @rowStride bytes apart, as a PNG */
boolean tximage_write_png(const uint8 *src, const char *path, int width, int height, int rowStride);

#ifdef __cplusplus
}
#endif

#endif /* __TXIMAGE_H__ */
