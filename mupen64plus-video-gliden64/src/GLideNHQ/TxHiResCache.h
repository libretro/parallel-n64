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

#ifndef __TXHIRESCACHE_H__
#define __TXHIRESCACHE_H__

/* GLideNHQ's hi-res textures, read from a Rice texture pack
 * (<pack>/<ROM>/..., sub-directories too) and kept whole; saved as
 * <ROM>_HIRESTEXTURES.htc with DUMP_HIRESTEXCACHE, and read from there
 * the next time. C89. */

/* support hires textures
 *   0: disable
 *   1: enable
 */
#define HIRES_TEXTURE 1

#include "TxCache.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct txhirescache
{
	txcache *cache;      /* the textures, kept whole (no size); NULL if not made */
	char *tex_pack_path; /* UTF-8, "" for none */
	int maxwidth;
	int maxheight;
	int maxbpp;
	boolean dumped;      /* what is on disk is what it holds */
	unsigned numcore;    /* the quantizer's lanes */
} txhirescache;

/* the textures read in, or read from the file saved before; NULL only
 * out of memory */
txhirescache *txhirescache_new(int maxwidth, int maxheight, int maxbpp, int options,
		const char *cachePath, const char *texPackPath, const char *ident);
void txhirescache_free(txhirescache *h);
boolean txhirescache_empty(const txhirescache *h);
boolean txhirescache_get(txhirescache *h, uint64 checksum, GHQTexInfo *info);
/* a copy of @info's texture */
boolean txhirescache_add(txhirescache *h, uint64 checksum, GHQTexInfo *info);
/* the pack read in again (@replace 0: the textures held before given up;
 * 1: those of the same checksum replaced); 0 if none could be */
boolean txhirescache_load(txhirescache *h, boolean replace);
/* saved, with DUMP_HIRESTEXCACHE, if not saved since */
void txhirescache_dump(txhirescache *h);

#ifdef __cplusplus
}
#endif

#endif /* __TXHIRESCACHE_H__ */
