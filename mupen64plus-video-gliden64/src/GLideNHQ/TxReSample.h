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

#ifndef __TXRESAMPLE_H__
#define __TXRESAMPLE_H__

#include "Ext_TxFilter.h"

#ifdef __cplusplus
extern "C" {
#endif

/* the power of two at or above @num */
int txresample_pow2(int num);
/* @image (@bpp bits a texel) padded out to powers of two by replicating
 * its last column and row, (with @use_3dfx) its aspect kept within 8:1;
 * a new allocation in place of the old, *image, *width and *height set */
boolean txresample_next_pow2(uint8 **image, int *width, int *height, int bpp, boolean use_3dfx);
/* ARGB8888 @src shrunk by @ratio (2 or more) with a Kaiser-Bessel window,
 * a new allocation in place of the old */
boolean txresample_minify(uint8 **src, int *width, int *height, int ratio);

#ifdef __cplusplus
}
#endif

#endif /* __TXRESAMPLE_H__ */
