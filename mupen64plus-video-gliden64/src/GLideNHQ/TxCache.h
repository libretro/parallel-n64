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

#ifndef __TXCACHE_H__
#define __TXCACHE_H__

/* GLideNHQ's texture cache: textures by their 64-bit checksum, kept
 * whole or compressed, saved to and loaded from disk. C89. */

#include "Ext_TxFilter.h"
#include "TxUtil.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct txcache_entry txcache_entry;

typedef struct txcache
{
	int options;          /* GLideNHQ's options; GZ_* compress what is kept */
	int cache_size;       /* the bytes it may hold, the least used given up
	                       * past it; 0 keeps all */
	int total_size;       /* the bytes it holds */
	char *ident;          /* the ROM's name, UTF-8, "" for none */
	char *cache_path;     /* UTF-8, "" for none */
	/* where a compressed texture is inflated for a get, the two in turn */
	uint8 *gzdest0;
	uint8 *gzdest1;
	uint32 gzdest_len;
	/* the textures, chained by checksum; and, with a size, the order they
	 * were last used in, the least first */
	txcache_entry **buckets;
	unsigned nbuckets;
	unsigned count;
	txcache_entry *lru_head;
	txcache_entry *lru_tail;
} txcache;

txcache *txcache_new(int options, int cachesize, const char *cachePath, const char *ident);
void txcache_free(txcache *c);

/* @info's texels under @checksum (hi: palette, low: texture); @dataSize
 * given when they are compressed already. With @owned, info->data is a
 * malloc'd buffer the cache takes on success, kept as it is (or freed once
 * a compressed copy is kept); on failure it is still the caller's. Else
 * the texels are copied. 0 for a checksum it holds already. */
boolean txcache_add(txcache *c, uint64 checksum, GHQTexInfo *info, int dataSize, boolean owned);
/* the texture into @info, inflated if it is kept compressed (into a buffer
 * of the cache's, valid until the get after next) */
boolean txcache_get(txcache *c, uint64 checksum, GHQTexInfo *info);
boolean txcache_del(txcache *c, uint64 checksum);
boolean txcache_is_cached(const txcache *c, uint64 checksum);
void txcache_clear(txcache *c);
#define txcache_empty(c) ((c)->count == 0)

/* to and from path/filename, under @config (what a load must match,
 * unless @force) */
boolean txcache_save(txcache *c, const char *path, const char *filename, int config);
boolean txcache_load(txcache *c, const char *path, const char *filename, int config, boolean force);

#ifdef __cplusplus
}
#endif

#endif /* __TXCACHE_H__ */
