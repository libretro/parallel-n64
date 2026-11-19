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


#ifndef __TEXTUREFILTERS_H__
#define __TEXTUREFILTERS_H__

/* 16bpp filters are somewhat buggy and output image is not clean.
 * Since there's not much time, we'll just convert them to ARGB8888
 * and use 32bpp filters until fixed.
 * (1:enable hack, 0:disable hack) */
#define _16BPP_HACK 1

#include "Ext_TxFilter.h"

#ifdef __cplusplus
extern "C" {
#endif

/* enhancers */
void hq4x_8888(unsigned char * pIn, unsigned char * pOut, int Xres, int Yres, int SrcPPL, int BpL);

void hq2x_32(uint8 *srcPtr, uint32 srcPitch, uint8 *dstPtr, uint32 dstPitch, int width, int height);
void hq2xS_32(uint8 *srcPtr, uint32 srcPitch, uint8 *dstPtr, uint32 dstPitch, int width, int height);

void lq2x_32(uint8 *srcPtr, uint32 srcPitch, uint8 *dstPtr, uint32 dstPitch, int width, int height);
void lq2xS_32(uint8 *srcPtr, uint32 srcPitch, uint8 *dstPtr, uint32 dstPitch, int width, int height);

void Super2xSaI_8888(uint32 *srcPtr, uint32 *destPtr, uint32 width, uint32 height, uint32 pitch);

void Texture2x_32(uint8 *srcPtr, uint32 srcPitch, uint8 *dstPtr, uint32 dstPitch, int width, int height);

/* filters */
void SharpFilter_8888(uint32 *src, uint32 srcwidth, uint32 srcheight, uint32 *dest, uint32 filter);

void SmoothFilter_8888(uint32 *src, uint32 srcwidth, uint32 srcheight, uint32 *dest, uint32 filter);

/* xBRZ (TextureFilters_xbrz.c): its tables made, and @factor (2 to 6)
 * times the texture, its texels ABGR */
void xbrz_init(void);
void xbrz_scale_abgr(unsigned factor, const uint32 *src, uint32 *dst, int width, int height);

/* @filter's enhancement, then its sharp or smooth filter, @src to @dest;
 * with DEPOSTERIZE, @src deposterized first, @scratch0 and @scratch1 each
 * holding srcwidth x srcheight texels for it */
void filter_8888(uint32 *src, uint32 srcwidth, uint32 srcheight, uint32 *dest, uint32 filter,
      uint32 *scratch0, uint32 *scratch1);


#ifdef __cplusplus
}
#endif

#endif /* __TEXTUREFILTERS_H__ */
