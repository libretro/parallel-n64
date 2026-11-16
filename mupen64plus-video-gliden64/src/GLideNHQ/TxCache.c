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

/* C89: GLideNHQ's texture cache, a hash of its textures by checksum and,
 * with a size, a list of them by use. */

#include <stdlib.h>
#include <string.h>

#include <file/file_path.h>
#include <encodings/deflate.h>

#include "TxCache.h"
#include "TxGz.h"

struct txcache_entry
{
	uint64 checksum;
	int size;              /* the bytes kept: compressed, if it is */
	GHQTexInfo info;
	txcache_entry *chain;  /* the next of its bucket */
	txcache_entry *prev;   /* by use, with a size */
	txcache_entry *next;
};

/* Whole-buffer deflate/inflate over libretro-common's rdeflate/rinflate,
 * standing in for zlib's compress2()/uncompress().  window_bits 15 selects
 * the RFC 1950 zlib wrapper, which is the container compress2() produced,
 * so texture caches written by earlier builds stay readable and caches
 * written here stay readable by them.
 *
 * Both return 0 on success and -1 on failure, and write the produced length
 * back through destLen, matching the zlib calls they replace. */
static int txDeflate(uint8 *dest, uint32 *destLen,
                     const uint8 *src, uint32 srcLen, int level)
{
	void *stream = rdeflate_new(level, 15);
	size_t produced = 0;
	int ret = -1;

	if (!stream)
		return -1;

	rdeflate_set_in(stream, src, srcLen);
	rdeflate_set_out(stream, dest, *destLen);
	rdeflate_finish(stream);

	for (;;) {
		size_t rd = 0, wn = 0;
		const int st = rdeflate_process(stream, &rd, &wn);

		produced += wn;

		if (st == RDEFLATE_PROCESS_END) {
			ret = 0;
			break;
		}
		/* No progress means the output buffer is full: the compressed form
		 * would be larger than the destination, so the caller keeps the
		 * texture uncompressed. */
		if (st == RDEFLATE_PROCESS_ERROR || (rd == 0 && wn == 0))
			break;
	}

	rdeflate_free(stream);
	*destLen = (uint32)produced;
	return ret;
}

static int txInflate(uint8 *dest, uint32 *destLen,
                     const uint8 *src, uint32 srcLen)
{
	void *stream = rinflate_new(15);
	size_t produced = 0;
	int ret = -1;

	if (!stream)
		return -1;

	rinflate_set_in(stream, src, srcLen);
	rinflate_set_out(stream, dest, *destLen);

	for (;;) {
		size_t rd = 0, wn = 0;
		const int st = rinflate_process(stream, &rd, &wn);

		produced += wn;

		if (st == RDEFLATE_PROCESS_END) {
			ret = 0;
			break;
		}
		if (st == RDEFLATE_PROCESS_ERROR || (rd == 0 && wn == 0))
			break;
	}

	rinflate_free(stream);
	*destLen = (uint32)produced;
	return ret;
}

static char *copy_string(const char *s)
{
	const size_t n = s ? strlen(s) : 0;
	char *d = (char *)malloc(n + 1);
	if (d) {
		if (n)
			memcpy(d, s, n);
		d[n] = '\0';
	}
	return d;
}

static unsigned bucket_of(const txcache *c, uint64 checksum)
{
	uint64 x = checksum;
	x ^= x >> 33;
	x *= 0xff51afd7ed558ccdull;
	x ^= x >> 33;
	return (unsigned)x & (c->nbuckets - 1);
}

static txcache_entry *find(const txcache *c, uint64 checksum)
{
	txcache_entry *e = c->buckets[bucket_of(c, checksum)];
	while (e && e->checksum != checksum)
		e = e->chain;
	return e;
}

/* twice the buckets once there are twice as many textures */
static void grow(txcache *c)
{
	const unsigned old = c->nbuckets;
	txcache_entry **buckets = (txcache_entry **)calloc(old * 2, sizeof(*buckets));
	unsigned i;
	if (!buckets)
		return;
	{
		txcache_entry **from = c->buckets;
		c->buckets = buckets;
		c->nbuckets = old * 2;
		for (i = 0; i < old; i++) {
			txcache_entry *e = from[i];
			while (e) {
				txcache_entry *next = e->chain;
				const unsigned b = bucket_of(c, e->checksum);
				e->chain = buckets[b];
				buckets[b] = e;
				e = next;
			}
		}
		free(from);
	}
}

static void lru_unlink(txcache *c, txcache_entry *e)
{
	if (e->prev) e->prev->next = e->next; else c->lru_head = e->next;
	if (e->next) e->next->prev = e->prev; else c->lru_tail = e->prev;
	e->prev = e->next = NULL;
}

static void lru_push_back(txcache *c, txcache_entry *e)
{
	e->prev = c->lru_tail;
	e->next = NULL;
	if (c->lru_tail) c->lru_tail->next = e; else c->lru_head = e;
	c->lru_tail = e;
}

/* out of the hash and the list, its texels freed */
static void remove_entry(txcache *c, txcache_entry *e)
{
	txcache_entry **p = &c->buckets[bucket_of(c, e->checksum)];
	while (*p != e)
		p = &(*p)->chain;
	*p = e->chain;
	if (c->cache_size > 0)
		lru_unlink(c, e);
	c->total_size -= e->size;
	c->count--;
	free(e->info.data);
	free(e);
}

txcache *txcache_new(int options, int cachesize, const char *cachePath, const char *ident)
{
	txcache *c = (txcache *)calloc(1, sizeof(*c));
	if (!c)
		return NULL;
	c->options = options;
	c->cache_size = cachesize;
	c->ident = copy_string(ident);
	c->cache_path = copy_string(cachePath);
	c->nbuckets = 1024;
	c->buckets = (txcache_entry **)calloc(c->nbuckets, sizeof(*c->buckets));
	if (!c->ident || !c->cache_path || !c->buckets) {
		txcache_free(c);
		return NULL;
	}

	/* zlib memory buffers to (de)compress hires textures */
	if (c->options & (GZ_TEXCACHE|GZ_HIRESTEXCACHE)) {
		c->gzdest0   = txmembuf_get(0);
		c->gzdest1   = txmembuf_get(1);
		c->gzdest_len = (txmembuf_size(0) < txmembuf_size(1)) ? txmembuf_size(0) : txmembuf_size(1);

		if (!c->gzdest0 || !c->gzdest1 || !c->gzdest_len) {
			c->options &= ~(GZ_TEXCACHE|GZ_HIRESTEXCACHE);
			c->gzdest0 = NULL;
			c->gzdest1 = NULL;
			c->gzdest_len = 0;
		}
	}
	return c;
}

void txcache_free(txcache *c)
{
	if (!c)
		return;
	if (c->buckets)
		txcache_clear(c);
	free(c->buckets);
	free(c->ident);
	free(c->cache_path);
	free(c);
}

boolean txcache_add(txcache *c, uint64 checksum, GHQTexInfo *info, int dataSize, boolean owned)
{
	/* NOTE: dataSize must be provided if info->data is zlib compressed. */
	uint8 *dest = info->data;
	uint32 format = info->format;
	uint8 *tmpdata;
	txcache_entry *e;

	if (!checksum || !info->data || find(c, checksum))
		return 0;

	if (dataSize == 0) {
		dataSize = txutil_texture_bytes(info->width, info->height, info->format);

		if (!dataSize)
			return 0;

		if (c->options & (GZ_TEXCACHE|GZ_HIRESTEXCACHE)) {
		/* deflate it. compression level:1 (best speed)
		 *
		 * This runs on the gameplay path - TxFilter calls add() as
		 * textures are filtered - so the level is chosen for time, not
		 * size, which is what the original "best speed" comment meant.
		 * Measured over 4 MiB RGBA8 payloads, mean of 3, against the
		 * zlib compress2(level 1) this replaced:
		 *
		 *                    zlib l1        rdeflate l1     rdeflate l4
		 *   upscaled art   76086 / 10.1ms  82669 / 7.9ms   75705 / 30.1ms
		 *   noisy detail  3635149 /  147ms 3629668 / 122ms 3629638 / 134ms
		 *   gradient/UI   1450155 / 67.7ms 1451446 / 69.1ms 1451446 / 173ms
		 *
		 * rdeflate at level 1 is the faster of the two on the first two
		 * shapes and within noise on the third.  It gives up 8.6% on flat
		 * upscaled content, which is the shape a texture cache sees most,
		 * and level 4 buys that back and then beats zlib - but at 3.8x the
		 * time on that shape and 2.5x on a gradient, which on this path is
		 * a hitch while a texture is filtered.  Levels 1-3 are identical in
		 * output here and 4-6 are identical to each other, so the only
		 * decision is which side of that cliff to sit on; speed wins on a
		 * runtime path.  Revisit only with a measurement taken while a real
		 * texture pack is loading, not against synthetic payloads. */
		uint32 destLen = c->gzdest_len;
			dest = (dest == c->gzdest0) ? c->gzdest1 : c->gzdest0;
			if (txDeflate(dest, &destLen, info->data, dataSize, 1) != 0) {
				dest = info->data;
			} else {
				dataSize = destLen;
				format |= TXFMT_GZ;
			}
		}
	}

	/* if cache size exceeds limit, remove old cache: the least used first */
	if (c->cache_size > 0) {
		c->total_size += dataSize;
		while (c->total_size > c->cache_size && c->lru_head)
			remove_entry(c, c->lru_head);
		c->total_size -= dataSize;
	}

	/* cache it: a buffer given it as it is, else a copy (of what was
	 * compressed, if it was) */
	if (owned && dest == info->data) {
		tmpdata = info->data;
	} else {
		tmpdata = (uint8 *)malloc(dataSize);
		if (tmpdata == NULL)
			return 0;
		memcpy(tmpdata, dest, dataSize);
	}
	e = (txcache_entry *)malloc(sizeof(*e));
	if (!e) {
		if (tmpdata != info->data)
			free(tmpdata);
		return 0;
	}
	if (owned && tmpdata != info->data)
		free(info->data);

	e->checksum = checksum;
	e->size = dataSize;
	e->info = *info;
	e->info.data = tmpdata;
	e->info.format = format;
	e->prev = e->next = NULL;
	{
		const unsigned b = bucket_of(c, checksum);
		e->chain = c->buckets[b];
		c->buckets[b] = e;
	}
	if (c->cache_size > 0)
		lru_push_back(c, e);
	c->count++;
	if (c->count > c->nbuckets * 2)
		grow(c);

	/* total cache size */
	c->total_size += dataSize;

	return 1;
}

boolean txcache_get(txcache *c, uint64 checksum, GHQTexInfo *info)
{
	txcache_entry *e;

	if (!checksum || !c->count)
		return 0;

	/* find a match in cache */
	e = find(c, checksum);
	if (!e)
		return 0;

	/* yep, we've got it. */
	*info = e->info;

	/* the last used now */
	if (c->cache_size > 0) {
		lru_unlink(c, e);
		lru_push_back(c, e);
	}

	/* decompress it */
	if (info->format & TXFMT_GZ) {
		uint32 destLen = c->gzdest_len;
		uint8 *dest = (c->gzdest0 == info->data) ? c->gzdest1 : c->gzdest0;
		if (txInflate(dest, &destLen, info->data, e->size) != 0)
			return 0;
		info->data = dest;
		info->format &= ~TXFMT_GZ;
	}

	return 1;
}

static int by_checksum(const void *a, const void *b)
{
	const uint64 x = (*(txcache_entry *const *)a)->checksum, y = (*(txcache_entry *const *)b)->checksum;
	return x < y ? -1 : x > y;
}

boolean txcache_save(txcache *c, const char *path, const char *filename, int config)
{
	char *fullPath;
	TxGzFile *gzfp;
	txcache_entry **all;
	unsigned i, n = 0;

	if (!c->count)
		return 0;

	/* dump cache to disk */
	path_mkdir(path);

	fullPath = (char *)malloc(strlen(path) + strlen(filename) + 2);
	all = (txcache_entry **)malloc(c->count * sizeof(*all));
	if (!fullPath || !all) {
		free(fullPath);
		free(all);
		return 0;
	}
	strcpy(fullPath, path);
	strcat(fullPath, "/");
	strcat(fullPath, filename);

	/* in the order of their checksums, as the files have always had them */
	for (i = 0; i < c->nbuckets; i++) {
		txcache_entry *e;
		for (e = c->buckets[i]; e; e = e->chain)
			all[n++] = e;
	}
	qsort(all, n, sizeof(*all), by_checksum);

	gzfp = txgz_open(fullPath, "wb1");
	if (gzfp) {
		/* write header to determine config match */
		txgz_write(gzfp, &config, 4);

		for (i = 0; i < n; i++) {
			const txcache_entry *e = all[i];
			/* the texels as the cache keeps them, compressed or not: a
			 * cache made with the other GZ_* option is rebuilt */
			uint8 *dest = e->info.data;
			uint32 destLen = (uint32)e->size;
			uint32 format = e->info.format;

			if (dest && destLen) {
				/* texture checksum */
				txgz_write(gzfp, &e->checksum, 8);

				/* other texture info */
				txgz_write(gzfp, &e->info.width, 4);
				txgz_write(gzfp, &e->info.height, 4);
				txgz_write(gzfp, &format, 4);
				txgz_write(gzfp, &e->info.texture_format, 2);
				txgz_write(gzfp, &e->info.pixel_type, 2);
				txgz_write(gzfp, &e->info.is_hires_tex, 1);

				txgz_write(gzfp, &destLen, 4);
				txgz_write(gzfp, dest, destLen);
			}
		}
		txgz_close(gzfp);
	}
	free(all);
	free(fullPath);

	return c->count ? 1 : 0;
}

boolean txcache_load(txcache *c, const char *path, const char *filename, int config, boolean force)
{
	/* find it on disk */
	char *fullPath = (char *)malloc(strlen(path) + strlen(filename) + 2);
	TxGzFile *gzfp;

	if (!fullPath)
		return 0;
	strcpy(fullPath, path);
	strcat(fullPath, "/");
	strcat(fullPath, filename);
	gzfp = txgz_open(fullPath, "rb");
	free(fullPath);
	if (gzfp) {
		/* yep, we have it. load it into memory cache. */
		int dataSize;
		uint64 checksum;
		int tmpconfig;
		/* read header to determine config match */
		if (txgz_read(gzfp, &tmpconfig, 4) == 4 && (tmpconfig == config || force)) {
			do {
				GHQTexInfo tmpInfo;

				/* a short record ends the file: the cache was cut off */
				if (txgz_read(gzfp, &checksum, 8) != 8 ||
						txgz_read(gzfp, &tmpInfo.width, 4) != 4 ||
						txgz_read(gzfp, &tmpInfo.height, 4) != 4 ||
						txgz_read(gzfp, &tmpInfo.format, 4) != 4 ||
						txgz_read(gzfp, &tmpInfo.texture_format, 2) != 2 ||
						txgz_read(gzfp, &tmpInfo.pixel_type, 2) != 2 ||
						txgz_read(gzfp, &tmpInfo.is_hires_tex, 1) != 1 ||
						txgz_read(gzfp, &dataSize, 4) != 4 ||
						dataSize <= 0)
					break;

				tmpInfo.data = (uint8 *)malloc(dataSize);
				if (tmpInfo.data) {
					if (txgz_read(gzfp, tmpInfo.data, dataSize) != dataSize) {
						free(tmpInfo.data);
						break;
					}

					/* into the memory cache, which takes the buffer read */
					if (!txcache_add(c, checksum, &tmpInfo, (tmpInfo.format & TXFMT_GZ) ? dataSize : 0, 1))
						free(tmpInfo.data);
				} else {
					txgz_skip(gzfp, dataSize);
				}

			} while (!txgz_eof(gzfp));
		}
		txgz_close(gzfp);
	}

	return c->count ? 1 : 0;
}

boolean txcache_del(txcache *c, uint64 checksum)
{
	txcache_entry *e;
	if (!checksum || !c->count)
		return 0;
	e = find(c, checksum);
	if (!e)
		return 0;
	remove_entry(c, e);
	return 1;
}

boolean txcache_is_cached(const txcache *c, uint64 checksum)
{
	return find(c, checksum) ? 1 : 0;
}

void txcache_clear(txcache *c)
{
	unsigned i;
	for (i = 0; i < c->nbuckets; i++) {
		txcache_entry *e = c->buckets[i];
		while (e) {
			txcache_entry *next = e->chain;
			free(e->info.data);
			free(e);
			e = next;
		}
		c->buckets[i] = NULL;
	}
	c->count = 0;
	c->lru_head = c->lru_tail = NULL;
	c->total_size = 0;
}
