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

#ifndef __TXQUANTIZE_H__
#define __TXQUANTIZE_H__

/* Converting textures between 32-bit RGBA and the 16-bit formats. C89. */

#include "Ext_TxFilter.h"
#include "txpool.h"

#ifdef __cplusplus
extern "C" {
#endif

/* the formats: 8888 RGBA (in memory R, G, B, A), and 1555, 4444 and 565
 * as 16-bit words */
enum
{
	TXQ_RGBA8 = 0,
	TXQ_RGB5_A1,
	TXQ_RGBA4,
	TXQ_RGB565
};

/* @src in @srcformat to @dest in @destformat, one of the two RGBA8 and the
 * other 16-bit; 0 for any other pair. To 16 bits @fast rounds each texel,
 * else the errors diffuse (Floyd-Steinberg). The rows are split into
 * @lanes blocks run on @pool (on the caller with none), each block
 * diffusing on its own; width * height must be at least 4. */
boolean txquantize(uint8 *src, uint8 *dest, int width, int height, int srcformat, int destformat,
		boolean fast, txpool_t *pool, unsigned lanes);

/* 8-bit indices to 16-bit texels: each through @palette, RGBA5551 words,
 * turned to ARGB1555 */
void txquantize_p8_16bpp(const uint8 *src, uint16 *dest, int width, int height, const uint16 *palette);

#ifdef __cplusplus
}
#endif

#endif /* __TXQUANTIZE_H__ */
