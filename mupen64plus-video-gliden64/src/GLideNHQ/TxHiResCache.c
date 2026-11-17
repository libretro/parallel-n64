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

/* C89: GLideNHQ's hi-res textures, read from a Rice texture pack. */

/* use power of 2 texture size
 * (0:disable, 1:enable, 2:3dfx) */
#define POW2_TEXTURES 0

/* use aggressive format assumption for quantization
 * (0:disable, 1:enable, 2:extreme) */
#define AGGRESSIVE_QUANTIZATION 1

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <retro_dirent.h>
#include <file/file_path.h>

#include "TxHiResCache.h"
#include "TxUtil.h"
#include "TxQuantize.h"
#include "TxImage.h"
#include "TxReSample.h"

enum { RES_OK, RES_NOT_FOUND, RES_ERROR };

/* the longest file name read, with room for the longest suffix swapped in */
#define FNAME_MAX 512

static char *str_dup(const char *s)
{
	const size_t n = strlen(s) + 1;
	char *d = (char *)malloc(n);
	if (d)
		memcpy(d, s, n);
	return d;
}

/* @a "/" @b, malloc'd */
static char *path_join(const char *a, const char *b)
{
	const size_t na = strlen(a), nb = strlen(b);
	char *p = (char *)malloc(na + nb + 2);
	if (!p)
		return NULL;
	memcpy(p, a, na);
	p[na] = '/';
	memcpy(p + na + 1, b, nb + 1);
	return p;
}

/* <ROM>_HIRESTEXTURES.htc, a ':' of the name a '-'; malloc'd */
static char *file_name(const txcache *c)
{
	static const char suffix[] = "_HIRESTEXTURES." TEXCACHE_EXT;
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
	return c->options & (HIRESTEXTURES_MASK | TILE_HIRESTEX | FORCE16BPP_HIRESTEX | GZ_HIRESTEXCACHE | LET_TEXARTISTS_FLY);
}

static boolean save(txhirescache *h)
{
	boolean res = 0;
	char *name = file_name(h->cache);
	if (name) {
		res = txcache_save(h->cache, h->cache->cache_path, name, config(h->cache));
		free(name);
	}
	return res;
}

static int load_dir(txhirescache *h, const char *dir_path, const char *ident, boolean replace);

txhirescache *txhirescache_new(int maxwidth, int maxheight, int maxbpp, int options,
		const char *cachePath, const char *texPackPath, const char *ident)
{
	txhirescache *h = (txhirescache *)calloc(1, sizeof(*h));
	if (!h)
		return NULL;
	h->numcore = txutil_cores();
	h->tex_pack_path = str_dup(texPackPath ? texPackPath : "");
	if (!h->tex_pack_path) {
		free(h);
		return NULL;
	}
	h->cache = txcache_new(options & ~GZ_TEXCACHE, 0, cachePath, ident);
	if (!h->cache)
		return h;

	h->maxwidth  = maxwidth;
	h->maxheight = maxheight;
	h->maxbpp    = maxbpp;

	if (!h->cache->cache_path[0] || !h->cache->ident[0]) {
		h->cache->options &= ~DUMP_HIRESTEXCACHE;
		return h;
	}

	/* read in hires texture cache */
	if (h->cache->options & DUMP_HIRESTEXCACHE) {
		/* find it on disk: read whatever the pack, if there is no pack */
		char *name = file_name(h->cache);
		char *pack = path_join(h->tex_pack_path, h->cache->ident);
		if (name && pack)
			h->dumped = txcache_load(h->cache, h->cache->cache_path, name, config(h->cache), !path_is_valid(pack));
		free(name);
		free(pack);
	}

	/* read in hires textures */
	if (!h->dumped) {
		if (txhirescache_load(h, 0) && (h->cache->options & DUMP_HIRESTEXCACHE))
			h->dumped = save(h);
	}
	return h;
}

void txhirescache_free(txhirescache *h)
{
	if (!h)
		return;
	txcache_free(h->cache);
	free(h->tex_pack_path);
	free(h);
}

boolean txhirescache_empty(const txhirescache *h)
{
	return !h || !h->cache || txcache_empty(h->cache);
}

boolean txhirescache_get(txhirescache *h, uint64 checksum, GHQTexInfo *info)
{
	return h && h->cache ? txcache_get(h->cache, checksum, info) : 0;
}

boolean txhirescache_add(txhirescache *h, uint64 checksum, GHQTexInfo *info, boolean owned)
{
	return h && h->cache ? txcache_add(h->cache, checksum, info, 0, owned) : 0;
}

void txhirescache_dump(txhirescache *h)
{
	if (!txhirescache_empty(h) && (h->cache->options & DUMP_HIRESTEXCACHE) && !h->dumped) {
		/* dump cache to disk */
		h->dumped = save(h);
	}
}

boolean txhirescache_load(txhirescache *h, boolean replace) /* 0 : reload, 1 : replace partial */
{
	if (!h || !h->cache || !h->tex_pack_path[0] || !h->cache->ident[0])
		return 0;

	if (!replace) txcache_clear(h->cache);

	switch (h->cache->options & HIRESTEXTURES_MASK) {
	case RICE_HIRESTEXTURES:
	{
		/* XXX case sensitivity fiasco!
		 * files must use _a, _rgb, _all, _allciByRGBA, _ciByRGBA, _ci
		 * and file extensions must be in lower case letters! */
		char *ident = str_dup(h->cache->ident);
		char *dir_path = path_join(h->tex_pack_path, h->cache->ident);
		int res = RES_ERROR;
		if (ident && dir_path) {
#ifdef OS_WINDOWS
			size_t i;
			for (i = 0; ident[i]; i++) ident[i] = (char)tolower((unsigned char)ident[i]);
#endif
			res = load_dir(h, dir_path, ident, replace);
		}
		free(ident);
		free(dir_path);
		/* what was loaded before the error is given up, freed */
		if (res == RES_ERROR)
			txcache_clear(h->cache);
		return res == RES_OK ? 1 : 0;
	}
	}
	return 0;
}

/* the format of what TxImage read (TXI_*) */
static uint32 txi_format(int txi)
{
	if (txi == TXI_RGBA8) return TXFMT_RGBA8;
	if (txi == TXI_COLOR_INDEX8) return TXFMT_COLOR_INDEX8;
	return 0;
}

/* the format as TxQuantize has it (TXQ_*) */
static int txq_format(uint32 format)
{
	if (format == TXFMT_RGB5_A1) return TXQ_RGB5_A1;
	if (format == TXFMT_RGBA4)   return TXQ_RGBA4;
	if (format == TXFMT_RGB8)    return TXQ_RGB565;
	return TXQ_RGBA8;
}

/* @path's texels: a PNG, else a BMP, the name at @suffix swapped for
 * @name ".png" and @name ".bmp" */
static uint8 *read_pair(char *path, char *suffix, const char *name, int *width, int *height, int *txi)
{
	uint8 *tex;
	strcpy(suffix, name);
	strcat(suffix, ".png");
	tex = tximage_read_png(path, width, height, txi);
	if (!tex) {
		strcpy(suffix, name);
		strcat(suffix, ".bmp");
		tex = tximage_read_bmp(path, width, height, txi);
	}
	return tex;
}

/* Rice's fmt 0 siz 2 and fmt 2 textures: alpha rounded, and each texel not
 * opaque given the average colour of the opaque ones around it */
static void round_alpha(uint32 *tex, int width, int height)
{
	int i, j;
	/* round A comp */
	for (i = 0; i < height * width; i++) {
		uint32 texel = tex[i];
		tex[i] = ((texel & 0xff000000) == 0xff000000 ? 0xff000000 : 0) | (texel & 0x00ffffff);
	}
	/* Substitute texel color with the average of the surrounding
	 * opaque texels. This removes borders regardless of hardware
	 * texture filtering (bilinear, etc). */
	for (i = 0; i < height; i++) {
		for (j = 0; j < width; j++) {
			uint32 texel = tex[i * width + j];
			if ((texel & 0xff000000) != 0xff000000) {
				uint32 tmptexel[8];
				uint32 k, numtexel, r, g, b;
				numtexel = r = g = b = 0;
				memset(&tmptexel, 0, sizeof(tmptexel));
				if (i > 0) {
					tmptexel[0] = tex[(i - 1) * width + j];                        /* north */
					if (j > 0)         tmptexel[1] = tex[(i - 1) * width + j - 1]; /* north-west */
					if (j < width - 1) tmptexel[2] = tex[(i - 1) * width + j + 1]; /* north-east */
				}
				if (i < height - 1) {
					tmptexel[3] = tex[(i + 1) * width + j];                        /* south */
					if (j > 0)         tmptexel[4] = tex[(i + 1) * width + j - 1]; /* south-west */
					if (j < width - 1) tmptexel[5] = tex[(i + 1) * width + j + 1]; /* south-east */
				}
				if (j > 0)         tmptexel[6] = tex[i * width + j - 1]; /* west */
				if (j < width - 1) tmptexel[7] = tex[i * width + j + 1]; /* east */
				for (k = 0; k < 8; k++) {
					if ((tmptexel[k] & 0xff000000) == 0xff000000) {
						b += ((tmptexel[k] & 0x00ff0000) >> 16);
						g += ((tmptexel[k] & 0x0000ff00) >>  8);
						r += ((tmptexel[k] & 0x000000ff)      );
						numtexel++;
					}
				}
				if (numtexel) {
					tex[i * width + j] = ((b / numtexel) << 16) |
					                     ((g / numtexel) <<  8) |
					                     ((r / numtexel)      );
				} else {
					tex[i * width + j] = texel & 0x00ffffff;
				}
			}
		}
	}
}

/* the RGBA8 texture analysed: the format it is best kept in */
static uint32 analyse(txhirescache *h, uint32 *tex, int width, int height, unsigned fmt, unsigned siz)
{
	const boolean force16 = h->maxbpp < 32 || (h->cache->options & FORCE16BPP_HIRESTEX);
	int i;
	int alphabits = 0;
	boolean intensity = 1;
	uint32 destformat;

	/* HACK ALERT! */
	/* Account for Rice's weirdness with fmt:0 siz:2 textures.
	 * Although the conditions are relaxed with other formats,
	 * the D3D RGBA5551 surface is used for this format in certain
	 * cases. See Nintemod's SuperMario64 life gauge and power
	 * meter. The same goes for fmt:2 textures. See Mollymutt's
	 * PaperMario text. */
	if (!(h->cache->options & LET_TEXARTISTS_FLY) && ((fmt == 0 && siz == 2) || fmt == 2))
		round_alpha(tex, width, height);

	/* simple analysis of texture */
	for (i = 0; i < height * width; i++) {
		uint32 texel = tex[i];
		if (alphabits != 8) {
#if AGGRESSIVE_QUANTIZATION
			if ((texel & 0xff000000) < 0x00000003) {
				alphabits = 1;
			} else if ((texel & 0xff000000) < 0xfe000000) {
				alphabits = 8;
			}
#else
			if ((texel & 0xff000000) == 0x00000000) {
				alphabits = 1;
			} else if ((texel & 0xff000000) != 0xff000000) {
				alphabits = 8;
			}
#endif
		}
		if (intensity) {
			int rcomp = (texel >> 16) & 0xff;
			int gcomp = (texel >>  8) & 0xff;
			int bcomp = (texel      ) & 0xff;
#if AGGRESSIVE_QUANTIZATION
			if (abs(rcomp - gcomp) > 8 || abs(rcomp - bcomp) > 8 || abs(gcomp - bcomp) > 8) intensity = 0;
#else
			if (rcomp != gcomp || rcomp != bcomp || gcomp != bcomp) intensity = 0;
#endif
		}
		if (!intensity && alphabits == 8) break;
	}

	/* preparations based on above analysis */
	if (force16) {
		if      (alphabits == 0) destformat = TXFMT_RGB8;
		else if (alphabits == 1) destformat = TXFMT_RGB5_A1;
		else                     destformat = TXFMT_RGBA8;
	} else {
		destformat = TXFMT_RGBA8;
	}
	if (fmt == 4 && alphabits == 0) {
		destformat = TXFMT_RGBA8;
		/* Rice I format; I = (R + G + B) / 3 */
		for (i = 0; i < height * width; i++) {
			uint32 texel = tex[i];
			uint32 icomp = (((texel >> 16) & 0xff) +
			                ((texel >>  8) & 0xff) +
			                ((texel      ) & 0xff)) / 3;
			tex[i] = (icomp << 24) | (texel & 0x00ffffff);
		}
	}
	/* 16 bits at most: 4 of alpha */
	if (force16 && destformat == TXFMT_RGBA8)
		destformat = TXFMT_RGBA4;
	return destformat;
}

/* @dir_path's textures, its sub-directories' too, into @h's cache:
 * names start with @ident */
static int load_dir(txhirescache *h, const char *dir_path, const char *ident, boolean replace)
{
	const size_t identlen = strlen(ident);
	const size_t dirlen = strlen(dir_path);
	struct RDIR *dir;
	/* each file's full path: the directory written once, the name after it */
	char *path, *fname;
	int result = RES_OK;

	/* find it on disk */
	if (!path_is_valid(dir_path))
		return RES_NOT_FOUND;

	/* files are opened by full UTF-8 path; the working directory is the
	 * frontend's and is left alone */
	dir = retro_opendir(dir_path);
	if (dir == NULL)
		return result;
	path = (char *)malloc(dirlen + 1 + FNAME_MAX);
	if (!path) {
		retro_closedir(dir);
		return RES_ERROR;
	}
	memcpy(path, dir_path, dirlen);
	path[dirlen] = '/';
	fname = path + dirlen + 1;

	while (retro_readdir(dir)) {
		const char *foundfilename = retro_dirent_get_name(dir);
		size_t namelen;
		int width = 0, height = 0;
		uint32 format = 0, destformat = 0;
		uint8 *tex = NULL;
		int tmpwidth = 0, tmpheight = 0;
		int txi = TXI_NONE; /* the format TxImage read in */
		uint8 *tmptex = NULL;
		unsigned int chksum = 0, fmt = 0, siz = 0, palchksum = 0;
		char *pfname;
		uint64 chksum64;

		/* The current file is a hidden one, or "." or ".." */
		if (foundfilename == NULL || foundfilename[0] == '.')
			continue;

		/* recursive read into sub-directory */
		if (retro_dirent_is_dir(dir, NULL)) {
			char *sub = path_join(dir_path, foundfilename);
			result = sub ? load_dir(h, sub, ident, replace) : RES_ERROR;
			free(sub);
			if (result == RES_OK)
				continue;
			else
				break;
		}

		/* Rice hi-res textures: begin
		 */
		namelen = strlen(foundfilename);
		if (namelen < 4 || namelen + 16 > FNAME_MAX)
			continue;

		/* read in Rice's file naming convention */
#define CRCFMTSIZ_LEN 13
#define PALCRC_LEN 9
		memcpy(fname, foundfilename, namelen + 1);
#ifdef OS_WINDOWS
		{
			size_t i;
			for (i = 0; i < namelen; i++) fname[i] = (char)tolower((unsigned char)fname[i]);
		}
#endif
		pfname = fname + namelen - 4;
		if (!(pfname == strstr(fname, ".png") ||
		      pfname == strstr(fname, ".bmp") ||
		      pfname == strstr(fname, ".dds")))
			continue;
		if (strncmp(fname, ident, identlen) != 0)
			continue;
		if (sscanf(fname + identlen, "#%08X#%01X#%01X#%08X", &chksum, &fmt, &siz, &palchksum) == 4)
			pfname = fname + identlen + CRCFMTSIZ_LEN + PALCRC_LEN;
		else if (sscanf(fname + identlen, "#%08X#%01X#%01X", &chksum, &fmt, &siz) == 3)
			pfname = fname + identlen + CRCFMTSIZ_LEN;
		else
			continue;
		if (!chksum)
			continue;
		chksum64 = ((uint64)palchksum << 32) | (uint64)chksum;

		/* check if we already have it in hires texture cache */
		if (!replace && txcache_is_cached(h->cache, chksum64))
			continue;

		/* Deal with the wackiness some texture packs utilize Rice format.
		 * Read in the following order: _a.* + _rgb.*, _all.png _ciByRGBA.png,
		 * _allciByRGBA.png, and _ci.bmp. PNG are prefered over BMP.
		 *
		 * For some reason there are texture packs that include them all. Some
		 * even have RGB textures named as _all.* and ARGB textures named as
		 * _rgb.*... Someone pleeeez write a GOOD guideline for the texture
		 * designers!!!
		 *
		 * We allow hires textures to have higher bpp than the N64 originals.
		 */
		/* N64 formats
		 * Format: 0 - RGBA, 1 - YUV, 2 - CI, 3 - IA, 4 - I
		 * Size:   0 - 4bit, 1 - 8bit, 2 - 16bit, 3 - 32 bit
		 */

		/*
		 * read in _rgb.* and _a.*
		 */
		if (pfname == strstr(fname, "_rgb.") || pfname == strstr(fname, "_a.")) {
			/* _rgb.* is loaded first so that the open doubles as the
			 * existence check, and _a.* is not read without it. */
			tex = read_pair(path, pfname, "_rgb", &width, &height, &txi);
			format = txi_format(txi);
			if (!tex)
				continue;
			tmptex = read_pair(path, pfname, "_a", &tmpwidth, &tmpheight, &txi);
			if (tmptex) {
				/* check if _rgb.* and _a.* have matching size and format. */
				if (width != tmpwidth || height != tmpheight ||
				    format != TXFMT_RGBA8 || txi_format(txi) != TXFMT_RGBA8) {
					free(tex);
					free(tmptex);
					continue;
				}
			}
			/* make adjustments */
			if (tmptex) {
				/* merge (A)RGB and A comp: the R comp for alpha, as Rice
				 * has it */
				int i;
				for (i = 0; i < height * width; i++) {
					((uint32 *)tex)[i] &= 0x00ffffff;
					((uint32 *)tex)[i] |= ((((uint32 *)tmptex)[i] & 0xff) << 24);
				}
				free(tmptex);
				tmptex = NULL;
			} else {
				/* clobber A comp. never a question of alpha. only RGB used. */
				int i;
				for (i = 0; i < height * width; i++)
					((uint32 *)tex)[i] |= 0xff000000;
			}
		} else

		/*
		 * read in _all.png, _all.dds, _allciByRGBA.png, _allciByRGBA.dds
		 * _ciByRGBA.png, _ciByRGBA.dds, _ci.bmp
		 */
		if (pfname == strstr(fname, "_all.png") ||
		    pfname == strstr(fname, "_all.dds") ||
#ifdef OS_WINDOWS
		    pfname == strstr(fname, "_allcibyrgba.png") ||
		    pfname == strstr(fname, "_allcibyrgba.dds") ||
		    pfname == strstr(fname, "_cibyrgba.png") ||
		    pfname == strstr(fname, "_cibyrgba.dds") ||
#else
		    pfname == strstr(fname, "_allciByRGBA.png") ||
		    pfname == strstr(fname, "_allciByRGBA.dds") ||
		    pfname == strstr(fname, "_ciByRGBA.png") ||
		    pfname == strstr(fname, "_ciByRGBA.dds") ||
#endif
		    pfname == strstr(fname, "_ci.bmp")) {
			if (strstr(fname, ".png")) tex = tximage_read_png(path, &width, &height, &txi);
			else                       tex = tximage_read_bmp(path, &width, &height, &txi);
			format = txi_format(txi);
		}

		/* if we do not have a texture at this point we are screwed */
		if (!tex)
			continue;

		/* check if size and format are OK */
		if (!(format == TXFMT_RGBA8 || format == TXFMT_COLOR_INDEX8) ||
		    (width * height) < 4) { /* TxQuantize requirement: width * height must be 4 or larger. */
			free(tex);
			continue;
		}

		/* XXX: only RGBA8888 for now. comeback to this later... */
		if (format == TXFMT_RGBA8) {
			/* analyze and determine best format to quantize */
			destformat = analyse(h, (uint32 *)tex, width, height, fmt, siz);
			/*
			 * Rice hi-res textures: end */

			/* minification */
			if (width > h->maxwidth || height > h->maxheight) {
				int ratio = 1;
				if (width / h->maxwidth > height / h->maxheight) {
					ratio = (int)ceil((double)width / h->maxwidth);
				} else {
					ratio = (int)ceil((double)height / h->maxheight);
				}
				if (!txresample_minify(&tex, &width, &height, ratio)) {
					free(tex);
					continue;
				}
			}

#if POW2_TEXTURES
#if (POW2_TEXTURES == 2)
			/* 3dfx Glide3x aspect ratio (8:1 - 1:8) */
			if (!txresample_next_pow2(&tex, &width , &height, 32, 1)) {
#else
			/* normal pow2 expansion */
			if (!txresample_next_pow2(&tex, &width , &height, 32, 0)) {
#endif
				free(tex);
				continue;
			}
#endif

			/* quantize: kept as read when it stays RGBA8 */
			if (destformat != TXFMT_RGBA8) {
				tmptex = (uint8 *)malloc(txutil_texture_bytes(width, height, destformat));
				if (tmptex == NULL) {
					free(tex);
					result = RES_ERROR;
					break;
				}
				if (txquantize(tex, tmptex, width, height, TXQ_RGBA8, txq_format(destformat), 0, txutil_pool(), h->numcore)) {
					format = destformat;
					free(tex);
					tex = tmptex;
				} else
					free(tmptex);
				tmptex = NULL;
			}
		}

		/* last minute validations */
		if (!width || !height || width > h->maxwidth || height > h->maxheight) {
			free(tex);
			continue;
		}

		/* load it into hires texture cache. */
		{
			GHQTexInfo tmpInfo;
			memset(&tmpInfo, 0, sizeof(tmpInfo));
			tmpInfo.data = tex;
			tmpInfo.width = width;
			tmpInfo.height = height;
			tmpInfo.is_hires_tex = 1;
			txutil_set_format(&tmpInfo, format);

			/* remove redundant in cache */
			if (replace)
				txcache_del(h->cache, chksum64);

			/* add to cache: it takes the texture as it is */
			if (!txcache_add(h->cache, chksum64, &tmpInfo, 0, 1)) {
				free(tex);
				result = RES_ERROR;
				break;
			}
		}
	}
	free(path);
	retro_closedir(dir);

	return result;
}
