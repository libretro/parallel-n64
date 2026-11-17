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

/* C89: GLideNHQ, what GLideN64 calls (Ext_TxFilter.h): textures filtered
 * and cached, hi-res textures looked up, textures dumped. One filter at
 * a time, made by txfilter_init and freed by txfilter_shutdown. */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <file/file_path.h>
#include <encodings/crc32.h>

#include "Ext_TxFilter.h"
#include "TextureFilters.h"
#include "TxQuantize.h"
#include "TxHiResCache.h"
#include "TxTexCache.h"
#include "TxUtil.h"
#include "TxImage.h"
#include "txpool.h"

/* GL_RGBA: a texture format, never an internal format GLideN64 hands in */
#define TX_GL_RGBA 0x1908

typedef struct txfilter
{
	unsigned numcore;
	uint8 *tex1;
	uint8 *tex2;
	int maxwidth;
	int maxheight;
	int maxbpp;
	int options;
	int cacheSize;
	char *ident;    /* UTF-8, "" for none */
	/* <dump>/<ROM>/GLideNHQ, and that "/<ROM>" with room for a dumped
	 * texture's name after it; NULL with no dump path or ROM name */
	char *dumpDir;
	char *dumpFile;
	size_t dumpName; /* where the name goes in dumpFile */
	txtexcache *texCache;
	txhirescache *hiresCache;
	boolean initialized;
} txfilter;

static txfilter s_filter;
static boolean s_live = 0;

/* the longest name a texture is dumped under, its NUL too */
#define DUMP_NAME_MAX 64

/* One block of rows per lane; the last lane takes the remainder. */
typedef struct FilterJob
{
	uint8 *src;
	uint8 *dst;
	uint32 srcwidth;
	uint32 srcheight;
	uint32 blkheight;
	uint32 numcore;
	uint32 filter;
	uint32 srcStride;
	uint32 destStride;
} FilterJob;

static void filter_lane(void *ctx, unsigned lane)
{
	const FilterJob *job = (const FilterJob *)ctx;
	const uint32 height = (lane == job->numcore - 1)
		? job->srcheight - job->blkheight * lane
		: job->blkheight;
	/* the lane's rows of deposterize's scratch: where its rows lie in the
	 * texture (srcStride bytes of texels a lane) */
	const size_t at = (size_t)lane * (job->srcStride >> 2);
	filter_8888((uint32 *)(job->src + lane * job->srcStride), job->srcwidth, height,
				(uint32 *)(job->dst + lane * job->destStride), job->filter,
				txmembuf_scratch(0) + at, txmembuf_scratch(1) + at);
}

/* the format as TxQuantize has it (TXQ_*), -1 for one it has not */
static int txq_format(uint32 format)
{
	switch (format) {
	case TXFMT_RGBA8:   return TXQ_RGBA8;
	case TXFMT_RGB5_A1: return TXQ_RGB5_A1;
	case TXFMT_RGBA4:   return TXQ_RGBA4;
	case TXFMT_RGB8:    return TXQ_RGB565;
	}
	return -1;
}

static char *str_dup(const char *s)
{
	const size_t n = strlen(s) + 1;
	char *d = (char *)malloc(n);
	if (d)
		memcpy(d, s, n);
	return d;
}

/* where textures are dumped, made once: <dump>/<ROM>/GLideNHQ, and the
 * start of each file's path, <dump>/<ROM>/GLideNHQ/<ROM> */
static void dump_paths(txfilter *f, const char *dumpPath)
{
	static const char sub[] = "/GLideNHQ";
	const size_t nd = strlen(dumpPath), ni = strlen(f->ident);
	const size_t ndir = nd + 1 + ni + sizeof(sub) - 1;
	if (!nd || !ni)
		return;
	f->dumpDir = (char *)malloc(ndir + 1);
	f->dumpFile = (char *)malloc(ndir + 1 + ni + DUMP_NAME_MAX);
	if (!f->dumpDir || !f->dumpFile) {
		free(f->dumpDir);
		free(f->dumpFile);
		f->dumpDir = f->dumpFile = NULL;
		return;
	}
	memcpy(f->dumpDir, dumpPath, nd);
	f->dumpDir[nd] = '/';
	memcpy(f->dumpDir + nd + 1, f->ident, ni);
	memcpy(f->dumpDir + nd + 1 + ni, sub, sizeof(sub));
	memcpy(f->dumpFile, f->dumpDir, ndir);
	f->dumpFile[ndir] = '/';
	memcpy(f->dumpFile + ndir + 1, f->ident, ni);
	f->dumpName = ndir + 1 + ni;
}

static void txfilter_free(txfilter *f)
{
	/* clear hires texture cache */
	txhirescache_free(f->hiresCache);

	/* clear texture cache */
	txtexcache_free(f->texCache);

	/* free memory */
	txmembuf_shutdown();

	free(f->ident);
	free(f->dumpDir);
	free(f->dumpFile);
	memset(f, 0, sizeof(*f));
	txutil_pool_release();
}

static void txfilter_make(txfilter *f, int maxwidth, int maxheight, int maxbpp, int options, int cachesize,
		const char *texCachePath, const char *texDumpPath, const char *texPackPath, const char *ident)
{
	memset(f, 0, sizeof(*f));
	txutil_pool_acquire();
	if (texCachePath == NULL || texDumpPath == NULL || texPackPath == NULL)
		return;

	f->options = options;

	/* get number of CPU cores. */
	f->numcore = txutil_cores();

	f->maxwidth  = maxwidth  > 4096 ? 4096 : maxwidth;
	f->maxheight = maxheight > 4096 ? 4096 : maxheight;
	f->maxbpp    = maxbpp;

	f->cacheSize = cachesize;

	/* save ROM name */
	f->ident = str_dup(ident && strcmp(ident, "DEFAULT") != 0 ? ident : "");
	if (!f->ident)
		return;

	/* save pathes */
	dump_paths(f, texDumpPath);

	if (txmembuf_init(f->maxwidth, f->maxheight)) {
		f->tex1 = txmembuf_get(0);
		f->tex2 = txmembuf_get(1);
	}

	/* initialize texture cache in bytes. 128Mb will do nicely in most cases */
	f->texCache = txtexcache_new(f->options, f->cacheSize, texCachePath, f->ident);

	/* hires texture */
#if HIRES_TEXTURE
	f->hiresCache = txhirescache_new(f->maxwidth, f->maxheight, f->maxbpp, f->options, texCachePath, texPackPath, f->ident);

	if (txhirescache_empty(f->hiresCache))
		f->options &= ~HIRESTEXTURES_MASK;
#endif

	if (f->tex1 && f->tex2)
		f->initialized = 1;
}

/* @f's enhancement for a @srcwidth x @srcheight texture, and the scale it
 * gives (1 for none) */
static uint32 enhancement(const txfilter *f, int srcwidth, int srcheight, int *scale)
{
	const uint32 e = (f->options & ENHANCEMENT_MASK);
	const boolean half = srcwidth <= (f->maxwidth >> 1) && srcheight <= (f->maxheight >> 1);
	*scale = 1;
	switch (e) {
	case NO_ENHANCEMENT:
		return 0;
	case HQ4X_ENHANCEMENT:
		if (srcwidth <= (f->maxwidth >> 2) && srcheight <= (f->maxheight >> 2)) {
			*scale = 4;
			return HQ4X_ENHANCEMENT;
		}
		if (half) {
			*scale = 2;
			return HQ2X_ENHANCEMENT;
		}
		return 0;
	case BRZ3X_ENHANCEMENT:
	case BRZ4X_ENHANCEMENT:
	case BRZ5X_ENHANCEMENT:
	case BRZ6X_ENHANCEMENT:
	{
		const int n = e == BRZ3X_ENHANCEMENT ? 3 : e == BRZ4X_ENHANCEMENT ? 4 : e == BRZ5X_ENHANCEMENT ? 5 : 6;
		xbrz_init();
		if (srcwidth <= (f->maxwidth / n) && srcheight <= (f->maxheight / n)) {
			*scale = n;
			return e;
		}
		if (half) {
			*scale = 2;
			return BRZ2X_ENHANCEMENT;
		}
		return 0;
	}
	default:
		if (half) {
			*scale = 2;
			return e;
		}
		return 0;
	}
}

static boolean txfilter_do(txfilter *f, uint8 *src, int srcwidth, int srcheight, uint32 srcformat, uint64 g64crc, GHQTexInfo *info)
{
	uint8 *texture = src;
	uint8 *tmptex = f->tex1;
	uint32 destformat = srcformat;
	/* The last step writes into a buffer the texture cache keeps as it is
	 * (it copied each filtered texture into one of its own before): when
	 * there is a cache, and it keeps textures uncompressed. */
	const boolean adopt = f->cacheSize && !(f->options & (GZ_TEXCACHE|GZ_HIRESTEXCACHE));
	const boolean to16 = f->maxbpp < 32 || (f->options & FORCE16BPP_TEX);
	uint8 *owned = NULL;
	size_t owned_size = 0;

	assert(srcformat != TX_GL_RGBA);

	/* We need to be initialized first! */
	if (!f->initialized) return 0;

	/* find cached textures */
	if (f->cacheSize) {

		/* calculate checksum of source texture */
		if (!g64crc) {
			const int bytes = txutil_texture_bytes(srcwidth, srcheight, srcformat);
			g64crc = bytes ? (uint64)encoding_crc32(0, texture, (size_t)bytes) : 0;
		}

		/* check if we have it in cache */
		if ((g64crc >> 32) == 0 && /* we reach here only when there is no hires texture for this crc */
				f->texCache && txtexcache_get(f->texCache, g64crc, info)) {
			return 1; /* yep, we've got it */
		}
	}

	/* Leave small textures alone because filtering makes little difference.
	 * Moreover, some filters require at least 4 * 4 to work.
	 * Bypass options to do ARGB8888->16bpp if maxbpp=16 or forced color reduction.
	 * (The 16-bit filters, under _16BPP_HACK 0, are not built.)
	 */
	if ((srcwidth >= 4 && srcheight >= 4) &&
			((f->options & (FILTER_MASK|ENHANCEMENT_MASK)) ||
			 (srcformat == TXFMT_RGBA8 && to16))) {

		if (srcformat != TXFMT_RGBA8) {
			if (!txquantize(texture, tmptex, srcwidth, srcheight, txq_format(srcformat), TXQ_RGBA8, 1, txutil_pool(), f->numcore)) {
				return 0;
			}
			texture = tmptex;
			destformat = TXFMT_RGBA8;
		}

		if (destformat == TXFMT_RGBA8) {
			/*
			 * prepare texture enhancements (x2, x4 scalers)
			 */
			int scale, num_filters, steps;
			uint32 filter = enhancement(f, srcwidth, srcheight, &scale);
			num_filters = filter ? 1 : 0;

			/*
			 * prepare texture filters
			 */
			if (f->options & (SMOOTH_FILTER_MASK|SHARP_FILTER_MASK)) {
				filter |= (f->options & (SMOOTH_FILTER_MASK|SHARP_FILTER_MASK));
				num_filters++;
			}

			filter |= f->options & DEPOSTERIZE;

			/* the steps left: the filters, and a conversion to 16 bits */
			steps = num_filters + (to16 ? 1 : 0);

			/*
			 * execute texture enhancements and filters
			 */
			while (num_filters > 0) {
				unsigned int numcore = f->numcore;
				unsigned int blkrow = 0;

				tmptex = (texture == f->tex1) ? f->tex2 : f->tex1;
				if (adopt && steps == 1) {
					owned_size = ((size_t)srcwidth * scale) * ((size_t)srcheight * scale) * 4;
					owned = (uint8 *)malloc(owned_size);
					if (owned)
						tmptex = owned;
				}
				steps--;

				while (numcore > 1 && blkrow == 0) {
					blkrow = (srcheight >> 2) / numcore;
					numcore--;
				}
				if (blkrow > 0 && numcore > 1) {
					FilterJob job;
					job.src        = texture;
					job.dst        = tmptex;
					job.srcwidth   = srcwidth;
					job.srcheight  = srcheight;
					job.blkheight  = blkrow << 2;
					job.numcore    = numcore;
					job.filter     = filter;
					job.srcStride  = (srcwidth * job.blkheight) << 2;
					job.destStride = job.srcStride * scale * scale;
					txpool_run(txutil_pool(), filter_lane, &job, numcore);
				} else {
					filter_8888((uint32 *)texture, srcwidth, srcheight, (uint32 *)tmptex, filter,
								txmembuf_scratch(0), txmembuf_scratch(1));
				}

				if (filter & ENHANCEMENT_MASK) {
					srcwidth  *= scale;
					srcheight *= scale;
					filter &= ~ENHANCEMENT_MASK;
					scale = 1;
				}

				texture = tmptex;
				num_filters--;
			}

			/*
			 * texture (re)conversions
			 */
			if (to16) {
				if (srcformat == TXFMT_RGBA8)
					srcformat = TXFMT_RGBA4;
				tmptex = (texture == f->tex1) ? f->tex2 : f->tex1;
				if (adopt && steps == 1) {
					owned_size = (size_t)srcwidth * srcheight * 2;
					owned = (uint8 *)malloc(owned_size);
					if (owned)
						tmptex = owned;
				}
				if (!txquantize(texture, tmptex, srcwidth, srcheight, TXQ_RGBA8, txq_format(srcformat), 1, txutil_pool(), f->numcore)) {
					free(owned);
					return 0;
				}
				texture = tmptex;
				destformat = srcformat;
			}
		}
	}

	/* fill in the texture info. */
	info->data = texture;
	info->width  = srcwidth;
	info->height = srcheight;
	info->is_hires_tex = 0;
	txutil_set_format(info, destformat);

	/* cache the texture: the buffer the last step wrote is the cache's;
	 * if it does not take it, the texels go to one of ours for the caller */
	if (owned && texture == owned) {
		if (!f->texCache || !txtexcache_add(f->texCache, g64crc, info, 1)) {
			memcpy(f->tex1, owned, owned_size);
			free(owned);
			info->data = f->tex1;
		}
	} else if (f->cacheSize && f->texCache)
		txtexcache_add(f->texCache, g64crc, info, 0);

	return 1;
}

static boolean txfilter_hires(txfilter *f, uint64 g64crc, uint64 r_crc64, uint16 *palette, GHQTexInfo *info)
{
	/* NOTE: Rice CRC32 sometimes return the same value for different textures.
	 * As a workaround, Glide64 CRC32 is used for the key for NON-hires
	 * texture cache.
	 *
	 * r_crc64 = hi:palette low:texture
	 *           (separate crc. doesn't necessary have to be rice crc)
	 * g64crc  = texture + palette glide64 crc32
	 *           (can be any other crc if robust)
	 */

#if HIRES_TEXTURE
	/* check if we have it in hires memory cache. */
	if ((f->options & HIRESTEXTURES_MASK) && r_crc64) {
		if (txhirescache_get(f->hiresCache, r_crc64, info))
			return 1; /* yep, got it */
		if (txhirescache_get(f->hiresCache, (r_crc64 & 0xffffffff), info)) {

			/* for true CI textures, we use the passed in palette to convert to
			 * ARGB1555 and add it to memory cache.
			 *
			 * NOTE: we do this AFTER all other texture cache searches because
			 * only a few texture packs actually use true CI textures.
			 *
			 * NOTE: the pre-converted palette from Glide64 is in RGBA5551 format.
			 * A comp comes before RGB comp.
			 */
			if (palette && info->format == TXFMT_COLOR_INDEX8) {
				const int width = info->width;
				const int height = info->height;
				/* XXX: avoid collision with zlib compression buffer in the hi-res cache's get */
				uint8 *texture = info->data;
				uint8 *tmptex = (texture == f->tex1) ? f->tex2 : f->tex1;
				/* with the cache uncompressed, converted into the buffer it
				 * keeps (it copied ours before) */
				uint8 *owned = (f->options & GZ_HIRESTEXCACHE) ? NULL : (uint8 *)malloc((size_t)width * height * 2);

				/* use palette and convert to 16bit format */
				txquantize_p8_16bpp(texture, (uint16 *)(owned ? owned : tmptex), width, height, palette);

				/* fill in the required info to return */
				info->data = owned ? owned : tmptex;
				info->width = width;
				info->height = height;
				info->is_hires_tex = 1;
				txutil_set_format(info, TXFMT_RGB5_A1);

				/* XXX: add to hires texture cache!!! */
				if (!txhirescache_add(f->hiresCache, r_crc64, info, owned != NULL) && owned) {
					memcpy(tmptex, owned, (size_t)width * height * 2);
					free(owned);
					info->data = tmptex;
				}
			}

			return 1;
		}
	}
#endif

	/* check if we have it in memory cache */
	if (f->cacheSize && g64crc) {
		if (f->texCache && txtexcache_get(f->texCache, g64crc, info)) {
			return 1; /* yep, we've got it */
		}
	}

	return 0;
}

static boolean txfilter_dump(txfilter *f, uint8 *src, int width, int height, int rowStridePixel, uint32 gfmt, uint16 n64fmt, uint64 r_crc64)
{
	assert(gfmt != TX_GL_RGBA);
	if (!f->initialized)
		return 0;

	if (!(f->options & DUMP_TEX))
		return 0;

	if (gfmt != TXFMT_RGBA8) {
		if (!txquantize(src, f->tex1, rowStridePixel, height, txq_format(gfmt), TXQ_RGBA8, 1, txutil_pool(), f->numcore))
			return 0;
		src = f->tex1;
	}

	if (f->dumpFile) {
		/* dump it to disk: create directories */
		char *name = f->dumpFile + f->dumpName;
		if (!path_mkdir(f->dumpDir))
			return 0;

		if ((n64fmt >> 8) == 0x2)
			sprintf(name, "#%08X#%01X#%01X#%08X_ciByRGBA.png", (uint32)(r_crc64 & 0xffffffff), (n64fmt >> 8), (n64fmt & 0xf), (uint32)(r_crc64 >> 32));
		else
			sprintf(name, "#%08X#%01X#%01X_all.png", (uint32)(r_crc64 & 0xffffffff), (n64fmt >> 8), (n64fmt & 0xf));

		/* rpng opens the UTF-8 path itself through the VFS */
		tximage_write_png(src, f->dumpFile, width, height, (rowStridePixel << 2));
		return 1;
	}

	return 0;
}

TAPI boolean TAPIENTRY
txfilter_init(int maxwidth, int maxheight, int maxbpp, int options, int cachesize,
	const char *txCachePath, const char *txDumpPath, const char *texPackPath, const char *ident)
{
	if (s_live) return 0;

	txfilter_make(&s_filter, maxwidth, maxheight, maxbpp, options, cachesize,
		txCachePath, txDumpPath, texPackPath, ident);
	s_live = 1;

	return 1;
}

TAPI void TAPIENTRY
txfilter_shutdown(void)
{
	if (s_live)
		txfilter_free(&s_filter);
	s_live = 0;
}

TAPI boolean TAPIENTRY
txfilter_filter(uint8 *src, int srcwidth, int srcheight, uint16 srcformat,
		 uint64 g64crc, GHQTexInfo *info)
{
	return s_live ? txfilter_do(&s_filter, src, srcwidth, srcheight, srcformat, g64crc, info) : 0;
}

TAPI boolean TAPIENTRY
txfilter_hirestex(uint64 g64crc, uint64 r_crc64, uint16 *palette, GHQTexInfo *info)
{
	return s_live ? txfilter_hires(&s_filter, g64crc, r_crc64, palette, info) : 0;
}

TAPI uint64 TAPIENTRY
txfilter_checksum(uint8 *src, int width, int height, int size, int rowStride, uint8 *palette)
{
	if (s_live && (s_filter.options & (HIRESTEXTURES_MASK|DUMP_TEX)))
		return txutil_checksum64(src, width, height, size, rowStride, palette);

	return 0;
}

TAPI boolean TAPIENTRY
txfilter_dmptx(uint8 *src, int width, int height, int rowStridePixel, uint16 gfmt, uint16 n64fmt, uint64 r_crc64)
{
	return s_live ? txfilter_dump(&s_filter, src, width, height, rowStridePixel, gfmt, n64fmt, r_crc64) : 0;
}

TAPI boolean TAPIENTRY
txfilter_reloadhirestex(void)
{
	if (!s_live)
		return 0;

	if (txhirescache_load(s_filter.hiresCache, 0) && !txhirescache_empty(s_filter.hiresCache)) {
		s_filter.options |= HIRESTEXTURES_MASK;
		return 1;
	}

	s_filter.options &= ~HIRESTEXTURES_MASK;
	return 0;
}

TAPI void TAPIENTRY
txfilter_dumpcache(void)
{
	if (!s_live)
		return;

	if (s_filter.texCache)
		txtexcache_dump(s_filter.texCache);

	/* hires texture */
#if HIRES_TEXTURE
	txhirescache_dump(s_filter.hiresCache);
#endif
}
