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

/* C89: GLideNHQ's cache of the textures it filtered. */

#include <stdlib.h>
#include <string.h>

#include "TxTexCache.h"

/* <ROM>_MEMORYCACHE.htc, a ':' of the name a '-'; malloc'd */
static char *file_name(const txcache *c)
{
	static const char suffix[] = "_MEMORYCACHE." TEXCACHE_EXT;
	const size_t n = strlen(c->ident);
	char *name = (char *)malloc(n + sizeof(suffix));
	size_t i;
	if (!name)
		return NULL;
	memcpy(name, c->ident, n);
	memcpy(name + n, suffix, sizeof(suffix));
	for (i = 0; name[i]; i++)
		if (name[i] == ':')
			name[i] = '-';
	return name;
}

/* what a saved cache must have been made with */
static int config(const txcache *c)
{
	return c->options & (FILTER_MASK | ENHANCEMENT_MASK | FORCE16BPP_TEX | GZ_TEXCACHE);
}

txtexcache *txtexcache_new(int options, int cachesize, const char *cachePath, const char *ident)
{
	txtexcache *t = (txtexcache *)calloc(1, sizeof(*t));
	if (!t)
		return NULL;
	t->cache = txcache_new(options & ~GZ_HIRESTEXCACHE, cachesize, cachePath, ident);
	if (!t->cache) {
		free(t);
		return NULL;
	}

	/* assert local options */
	if (!t->cache->cache_path[0] || !t->cache->ident[0] || !t->cache->cache_size)
		t->cache->options &= ~DUMP_TEXCACHE;

	if (t->cache->options & DUMP_TEXCACHE) {
		/* find it on disk */
		char *name = file_name(t->cache);
		if (name) {
			t->dumped = txcache_load(t->cache, t->cache->cache_path, name, config(t->cache), 0);
			free(name);
		}
	}
	return t;
}

void txtexcache_free(txtexcache *t)
{
	if (!t)
		return;
	txcache_free(t->cache);
	free(t);
}

boolean txtexcache_add(txtexcache *t, uint64 checksum, GHQTexInfo *info, boolean owned)
{
	boolean res;
	if (t->cache->cache_size <= 0)
		return 0;

	res = txcache_add(t->cache, checksum, info, 0, owned);
	if (res)
		t->dumped = 0;
	return res;
}

void txtexcache_dump(txtexcache *t)
{
	if ((t->cache->options & DUMP_TEXCACHE) && !t->dumped) {
		/* dump cache to disk */
		char *name = file_name(t->cache);
		if (name) {
			t->dumped = txcache_save(t->cache, t->cache->cache_path, name, config(t->cache));
			free(name);
		}
	}
}
