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

#ifndef __TXTEXCACHE_H__
#define __TXTEXCACHE_H__

/* GLideNHQ's cache of the textures it filtered, kept to a size, saved as
 * <ROM>_MEMORYCACHE.htc with DUMP_TEXCACHE. C89. */

#include "TxCache.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct txtexcache
{
	txcache *cache;
	boolean dumped;    /* what is on disk is what it holds */
} txtexcache;

txtexcache *txtexcache_new(int options, int cachesize, const char *cachePath, const char *ident);
void txtexcache_free(txtexcache *t);
/* @owned as txcache_add has it; 0 with no size to keep textures in */
boolean txtexcache_add(txtexcache *t, uint64 checksum, GHQTexInfo *info, boolean owned);
#define txtexcache_get(t, checksum, info) txcache_get((t)->cache, (checksum), (info))
/* saved, with DUMP_TEXCACHE, if it changed since */
void txtexcache_dump(txtexcache *t);

#ifdef __cplusplus
}
#endif

#endif /* __TXTEXCACHE_H__ */
