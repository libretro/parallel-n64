/*
* Glide64 - Glide video plugin for Nintendo 64 emulators.
* Copyright (c) 2002  Dave2001
* Copyright (c) 2003-2009  Sergey 'Gonetz' Lipski
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef TEXLOADBLOCK_H
#define TEXLOADBLOCK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* @cnt 64-bit words from @off bytes into @src, byte-swapped, into @dst.
 * @dxt: the LoadBlock command's texture coordinate step, which says
 * where a line ends: the words of every other line have their halves
 * swapped. */
void loadBlock(uint32_t *src, uint32_t *dst, uint32_t off, int dxt, int cnt);

/* @height lines of @width 64-bit words, @line bytes apart in @src from
 * @off, into @dst, no further than @end; every other line with the
 * halves of its words swapped */
void loadTile(uint32_t *src, uint32_t *dst, int width, int height, int line, int off, uint32_t *end);

#ifdef __cplusplus
}
#endif

#endif
