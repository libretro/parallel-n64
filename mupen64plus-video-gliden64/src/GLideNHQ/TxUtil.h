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

#ifndef __TXUTIL_H__
#define __TXUTIL_H__

/* GLideNHQ's checksums, its worker pool and its texture buffers. C89. */

#include "Ext_TxFilter.h"
#include "txpool.h"

/* maximum number of CPU cores allowed */
#define MAX_NUMCORE 8

/* extension for cache files */
#define TEXCACHE_EXT "htc"

#ifdef __cplusplus
extern "C" {
#endif

/* Rice's CRC32 of a texture, as hi-res texture packs are named by it:
 * @size the N64's texel size (0 4-bit .. 3 32-bit) in its low byte */
uint32 txutil_checksum(const uint8 *src, int width, int height, int size, int rowStride);
/* the same, with @palette for a 4- or 8-bit texture: the palette's CRC32,
 * as far as the indices reach, in the high word */
uint64 txutil_checksum64(const uint8 *src, int width, int height, int size, int rowStride, const uint8 *palette);

/* GHQTexInfo's formats: graphics::internalcolorFormat's values
 * (opengl_Parameters.cpp, for GL and Vulkan alike), and the flag of a
 * texture the cache keeps compressed */
#define TXFMT_RGB8         0x8051 /* 565 */
#define TXFMT_RGBA4        0x8056
#define TXFMT_RGB5_A1      0x8057
#define TXFMT_RGBA8        0x8058
#define TXFMT_COLOR_INDEX8 0x80E5
#define TXFMT_GZ           0x80000000u

/* the bytes a texture of @format takes, 0 for a format GLideNHQ has not */
int txutil_texture_bytes(int width, int height, uint32 format);

/* the physical cores, at most MAX_NUMCORE */
uint32 txutil_cores(void);

/* The worker pool the filters and quantizers split textures across
 * (txpool.c), NULL until acquired. TxFilter holds it for its lifetime;
 * acquire and release are called on the drawing thread only. */
txpool_t *txutil_pool(void);
void txutil_pool_acquire(void);
void txutil_pool_release(void);

/* Two buffers of maxwidth x maxheight RGBA8 texels for textures on their
 * way through the filters, and two of deposterize's scratch: made by
 * init, freed by shutdown. */
boolean txmembuf_init(int maxwidth, int maxheight);
void txmembuf_shutdown(void);
uint8 *txmembuf_get(uint32 num);
uint32 txmembuf_size(uint32 num);
uint32 *txmembuf_scratch(uint32 num);

#ifdef __cplusplus
}
#endif

#endif /* __TXUTIL_H__ */
