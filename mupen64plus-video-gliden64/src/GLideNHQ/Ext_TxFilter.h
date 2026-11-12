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

#ifndef __EXT_TXFILTER_H__
#define __EXT_TXFILTER_H__

#include <stddef.h>

#ifdef __MSC__
typedef __int64 int64;
typedef unsigned __int64 uint64;
#else
typedef long long int64;
typedef unsigned long long uint64;
typedef unsigned char boolean;
#endif

#define NO_OPTIONS          0x00000000

#define FILTER_MASK         0x000000ff
#define NO_FILTER           0x00000000
#define SMOOTH_FILTER_MASK  0x0000000f
#define NO_SMOOTH_FILTER    0x00000000
#define SMOOTH_FILTER_1     0x00000001
#define SMOOTH_FILTER_2     0x00000002
#define SMOOTH_FILTER_3     0x00000003
#define SMOOTH_FILTER_4     0x00000004
#define SHARP_FILTER_MASK   0x000000f0
#define NO_SHARP_FILTER     0x00000000
#define SHARP_FILTER_1      0x00000010
#define SHARP_FILTER_2      0x00000020

#define ENHANCEMENT_MASK    0x00000f00
#define NO_ENHANCEMENT      0x00000000
#define X2_ENHANCEMENT      0x00000100
#define X2SAI_ENHANCEMENT   0x00000200
#define HQ2X_ENHANCEMENT    0x00000300
#define LQ2X_ENHANCEMENT    0x00000400
#define HQ4X_ENHANCEMENT    0x00000500
#define HQ2XS_ENHANCEMENT   0x00000600
#define LQ2XS_ENHANCEMENT   0x00000700
#define BRZ2X_ENHANCEMENT   0x00000800
#define BRZ3X_ENHANCEMENT   0x00000900
#define BRZ4X_ENHANCEMENT   0x00000a00
#define BRZ5X_ENHANCEMENT   0x00000b00
#define BRZ6X_ENHANCEMENT   0x00000c00

#define DEPOSTERIZE         0x00001000

#define HIRESTEXTURES_MASK  0x000f0000
#define NO_HIRESTEXTURES    0x00000000
#define GHQ_HIRESTEXTURES   0x00010000
#define RICE_HIRESTEXTURES  0x00020000
#define JABO_HIRESTEXTURES  0x00030000

/* COMPRESS_TEX 0x00100000 and COMPRESS_HIRESTEX 0x00200000 are not used anymore */
#define GZ_TEXCACHE         0x00400000
#define GZ_HIRESTEXCACHE    0x00800000
#define DUMP_TEXCACHE       0x01000000
#define DUMP_HIRESTEXCACHE  0x02000000
#define TILE_HIRESTEX       0x04000000
#define UNDEFINED_0         0x08000000
#define FORCE16BPP_HIRESTEX 0x10000000
#define FORCE16BPP_TEX      0x20000000
#define LET_TEXARTISTS_FLY  0x40000000 /* a little freedom for texture artists */
#define DUMP_TEX            0x80000000

typedef struct GHQTexInfo GHQTexInfo;
struct GHQTexInfo {
  unsigned char *data;
  int width;
  int height;
  unsigned int format;
  unsigned short texture_format;
  unsigned short pixel_type;
  unsigned char is_hires_tex;

#ifdef __cplusplus
  GHQTexInfo() :
	  data(NULL), width(0), height(0), format(0),
	  texture_format(0), pixel_type(0), is_hires_tex(0)
  {}
#endif
};

/* dll exports */
/* Use TXFilter as a library. Define exported functions. */
#ifdef OS_WINDOWS
#ifdef TXFILTER_LIB
#define TAPI __declspec(dllexport)
#define TAPIENTRY
#else
#define TAPI
#define TAPIENTRY
#endif
#else /* OS_WINDOWS */
#ifdef TXFILTER_LIB
#define TAPI __attribute__((visibility("default")))
#define TAPIENTRY
#else
#define TAPI
#define TAPIENTRY
#endif
#endif /* OS_WINDOWS */

typedef unsigned char  uint8;
typedef unsigned short uint16;
typedef unsigned int  uint32;

#ifdef __cplusplus
extern "C"{
#endif

TAPI boolean TAPIENTRY
txfilter_init(int maxwidth, int maxheight, int maxbpp, int options, int cachesize,
	const char *txCachePath, const char *txDumpPath, const char *texPackPath,
	const char *ident); /* UTF-8 paths and ROM name */

TAPI void TAPIENTRY
txfilter_shutdown(void);

TAPI boolean TAPIENTRY
txfilter_filter(uint8 *src, int srcwidth, int srcheight, uint16 srcformat,
		 uint64 g64crc, GHQTexInfo *info);

TAPI boolean TAPIENTRY
txfilter_hirestex(uint64 g64crc, uint64 r_crc64, uint16 *palette, GHQTexInfo *info);

TAPI uint64 TAPIENTRY
txfilter_checksum(uint8 *src, int width, int height, int size, int rowStride, uint8 *palette);

TAPI boolean TAPIENTRY
txfilter_dmptx(uint8 *src, int width, int height, int rowStridePixel, uint16 gfmt, uint16 n64fmt, uint64 r_crc64);

TAPI boolean TAPIENTRY
txfilter_reloadhirestex(void);

TAPI void TAPIENTRY
txfilter_dumpcache(void);

#ifdef __cplusplus
}
#endif

#endif /* __EXT_TXFILTER_H__ */
