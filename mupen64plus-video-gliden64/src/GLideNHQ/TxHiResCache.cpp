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

#ifdef __MSC__
#pragma warning(disable: 4786)
#endif

/* use power of 2 texture size
 * (0:disable, 1:enable, 2:3dfx) */
#define POW2_TEXTURES 0

/* use aggressive format assumption for quantization
 * (0:disable, 1:enable, 2:extreme) */
#define AGGRESSIVE_QUANTIZATION 1

#include "TxHiResCache.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <retro_dirent.h>
#include <file/file_path.h>

TxHiResCache::~TxHiResCache()
{
  delete _txImage;
  delete _txQuantize;
  delete _txReSample;
}

TxHiResCache::TxHiResCache(int maxwidth,
						   int maxheight,
						   int maxbpp,
						   int options,
						   const char *cachePath,
						   const char *texPackPath,
						   const char *ident)
	: TxCache((options & ~GZ_TEXCACHE), 0, cachePath, ident)
{
  _txImage = new TxImage();
  _txQuantize  = new TxQuantize();
  _txReSample = new TxReSample();

  _maxwidth  = maxwidth;
  _maxheight = maxheight;
  _maxbpp    = maxbpp;
  _cacheDumped = 0;

  if (texPackPath)
	  _texPackPath.assign(texPackPath);

  if (_cachePath.empty() || _ident.empty()) {
	_options &= ~DUMP_HIRESTEXCACHE;
	return;
  }

  /* read in hires texture cache */
  if (_options & DUMP_HIRESTEXCACHE) {
	/* find it on disk */
	_cacheDumped = TxCache::load(_cachePath.c_str(), _getFileName().c_str(), _getConfig(), !_HiResTexPackPathExists());
  }

/* read in hires textures */
  if (!_cacheDumped) {
	  if (TxHiResCache::load(0) && (_options & DUMP_HIRESTEXCACHE) != 0)
		  _cacheDumped = TxCache::save(_cachePath.c_str(), _getFileName().c_str(), _getConfig());
  }
}

void TxHiResCache::dump()
{
	if ((_options & DUMP_HIRESTEXCACHE) && !_cacheDumped && !empty()) {
	  /* dump cache to disk */
	  _cacheDumped = TxCache::save(_cachePath.c_str(), _getFileName().c_str(), _getConfig());
	}
}

std::string TxHiResCache::_getFileName() const
{
	std::string filename = _ident + "_HIRESTEXTURES." TEXCACHE_EXT;
	std::replace(filename.begin(), filename.end(), ':', '-');
	return filename;
}

int TxHiResCache::_getConfig() const
{
	return _options & (HIRESTEXTURES_MASK | TILE_HIRESTEX | FORCE16BPP_HIRESTEX | GZ_HIRESTEXCACHE | LET_TEXARTISTS_FLY);
}

boolean TxHiResCache::_HiResTexPackPathExists() const
{
	const std::string dir_path = _texPackPath + "/" + _ident;
	return path_is_valid(dir_path.c_str());
}

boolean TxHiResCache::empty()
{
  return _cache.empty();
}

boolean TxHiResCache::load(boolean replace) /* 0 : reload, 1 : replace partial */
{
	if (_texPackPath.empty() || _ident.empty())
		return 0;

	if (!replace) TxCache::clear();

	switch (_options & HIRESTEXTURES_MASK) {
	case RICE_HIRESTEXTURES:
	{
		const LoadResult res = loadHiResTextures(_texPackPath + "/" + _ident, replace);
		if (res == resError)
			_cache.clear();
		return res == resOk ? 1 : 0;
	}
	}
	return 0;
}

TxHiResCache::LoadResult
TxHiResCache::loadHiResTextures(const std::string & dir_path, boolean replace)
{
  /* find it on disk */
  if (!path_is_valid(dir_path.c_str()))
	return resNotFound;

  LoadResult result = resOk;

  /* XXX case sensitivity fiasco!
   * files must use _a, _rgb, _all, _allciByRGBA, _ciByRGBA, _ci
   * and file extensions must be in lower case letters! */
  std::string ident(_ident);
#ifdef OS_WINDOWS
  for (size_t i = 0; i < ident.size(); i++) ident[i] = (char)tolower((unsigned char)ident[i]);
#endif

  /* files are opened by full UTF-8 path; the working directory is the
   * frontend's and is left alone */
  const std::string dir_prefix = dir_path + "/";
  struct RDIR *dir = retro_opendir(dir_path.c_str());
  if (dir == nullptr)
	return result;

  while (retro_readdir(dir)) {
	const char *foundfilename = retro_dirent_get_name(dir);
	// The current file is a hidden one, or "." or ".."
	if (foundfilename == nullptr || foundfilename[0] == '.')
		// These files we don't need
		continue;

	/* recursive read into sub-directory */
	if (retro_dirent_is_dir(dir, nullptr)) {
		result = loadHiResTextures(dir_prefix + foundfilename, replace);
		if (result == resOk)
			continue;
		else
			break;
	}


	int width = 0, height = 0;
	ColorFormat format = graphics::internalcolorFormat::NOCOLOR;
	uint8 *tex = nullptr;
	int tmpwidth = 0, tmpheight = 0;
	ColorFormat tmpformat = graphics::internalcolorFormat::NOCOLOR;
	uint8 *tmptex= nullptr;
	ColorFormat destformat = graphics::internalcolorFormat::NOCOLOR;

	/* Rice hi-res textures: begin
	 */
	uint32 chksum = 0, fmt = 0, siz = 0, palchksum = 0;
	/* room for the longest suffix swapped in below */
	char *pfname = nullptr, fname[512];
	std::string path;

	if (strlen(foundfilename) < 4 || strlen(foundfilename) + 16 > sizeof(fname))
	  continue;

	/* read in Rice's file naming convention */
#define CRCFMTSIZ_LEN 13
#define PALCRC_LEN 9
	strcpy(fname, foundfilename);
#ifdef OS_WINDOWS
	{
	  unsigned int i;
	  for (i = 0; i < strlen(fname); i++) fname[i] = (char)tolower((unsigned char)fname[i]);
	}
#endif
	pfname = fname + strlen(fname) - 4;
	if (!(pfname == strstr(fname, ".png") ||
		  pfname == strstr(fname, ".bmp") ||
		  pfname == strstr(fname, ".dds"))) {
#if !DEBUG
#endif
	  continue;
	}
	pfname = strstr(fname, ident.c_str());
	if (pfname != fname) pfname = 0;
	if (pfname) {
	  if (sscanf(pfname + ident.size(), "#%08X#%01X#%01X#%08X", &chksum, &fmt, &siz, &palchksum) == 4)
		pfname += (ident.size() + CRCFMTSIZ_LEN + PALCRC_LEN);
	  else if (sscanf(pfname + ident.size(), "#%08X#%01X#%01X", &chksum, &fmt, &siz) == 3)
		pfname += (ident.size() + CRCFMTSIZ_LEN);
	  else
		pfname = 0;
	}
	if (!pfname) {
#if !DEBUG
#endif
	  continue;
	}
	if (!chksum) {
#if !DEBUG
#endif
	  continue;
	}

	/* check if we already have it in hires texture cache */
	if (!replace) {
	  uint64 chksum64 = (uint64)palchksum;
	  chksum64 <<= 32;
	  chksum64 |= (uint64)chksum;
	  if (TxCache::is_cached(chksum64)) {
#if !DEBUG
#endif
		continue;
	  }
	}


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
	  /* _rgb.* is loaded first so that the open doubles as the existence
	   * check.  This used to osal_path_existsA() both _rgb.png and _rgb.bmp
	   * purely to decide whether to bail, then open the same paths a few
	   * lines later - a second full path resolution per entry, for an
	   * answer the open already gives.  A texture pack is tens of thousands
	   * of files and this runs for every _rgb and _a entry in it.
	   *
	   * Bailing here also skips the _a.* load when there is no usable
	   * _rgb.*, which the old order paid for before discovering the pair
	   * was unusable. */
	  strcpy(pfname, "_rgb.png");
	  tex = _txImage->readPNG((dir_prefix + fname).c_str(), &width, &height, &format);
	  if (!tex) {
		/* _rgb.bmp */
		strcpy(pfname, "_rgb.bmp");
		tex = _txImage->readBMP((dir_prefix + fname).c_str(), &width, &height, &format);
	  }
	  if (!tex) {
#if !DEBUG
#endif
		continue;
	  }
	  /* _a.png */
	  strcpy(pfname, "_a.png");
	  tmptex = _txImage->readPNG((dir_prefix + fname).c_str(), &tmpwidth, &tmpheight, &tmpformat);
	  if (!tmptex) {
		/* _a.bmp */
		strcpy(pfname, "_a.bmp");
		tmptex = _txImage->readBMP((dir_prefix + fname).c_str(), &tmpwidth, &tmpheight, &tmpformat);
	  }
	  if (tmptex) {
		/* check if _rgb.* and _a.* have matching size and format. */
		if (!tex || width != tmpwidth || height != tmpheight ||
			format != graphics::internalcolorFormat::RGBA8 || tmpformat != graphics::internalcolorFormat::RGBA8) {
		  if (tex) free(tex);
		  free(tmptex);
		  tex = nullptr;
		  tmptex = nullptr;
		  continue;
		}
	  }
	  /* make adjustments */
	  if (tex) {
		if (tmptex) {
		  /* merge (A)RGB and A comp */
		  int i;
		  for (i = 0; i < height * width; i++) {
#if 1
			/* use R comp for alpha. this is what Rice uses. sigh... */
			((uint32*)tex)[i] &= 0x00ffffff;
			((uint32*)tex)[i] |= ((((uint32*)tmptex)[i] & 0xff) << 24);
#endif
#if 0
			/* use libpng style grayscale conversion */
			uint32 texel = ((uint32*)tmptex)[i];
			uint32 acomp = (((texel >> 16) & 0xff) * 6969 +
							((texel >>  8) & 0xff) * 23434 +
							((texel      ) & 0xff) * 2365) / 32768;
			((uint32*)tex)[i] = (acomp << 24) | (((uint32*)tex)[i] & 0x00ffffff);
#endif
#if 0
			/* use the standard NTSC gray scale conversion */
			uint32 texel = ((uint32*)tmptex)[i];
			uint32 acomp = (((texel >> 16) & 0xff) * 299 +
							((texel >>  8) & 0xff) * 587 +
							((texel      ) & 0xff) * 114) / 1000;
			((uint32*)tex)[i] = (acomp << 24) | (((uint32*)tex)[i] & 0x00ffffff);
#endif
		  }
		  free(tmptex);
		  tmptex = nullptr;
		} else {
		  /* clobber A comp. never a question of alpha. only RGB used. */
#if !DEBUG
#endif
		  int i;
		  for (i = 0; i < height * width; i++) {
			((uint32*)tex)[i] |= 0xff000000;
		  }
		}
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
	  path = dir_prefix + fname;
	  if      (strstr(fname, ".png")) tex = _txImage->readPNG(path.c_str(), &width, &height, &format);
	  else                            tex = _txImage->readBMP(path.c_str(), &width, &height, &format);
	}

	/* if we do not have a texture at this point we are screwed */
	if (!tex) {
#if !DEBUG
#endif
	  continue;
	}

	/* check if size and format are OK */
	if (!(format == graphics::internalcolorFormat::RGBA8 || format == graphics::internalcolorFormat::COLOR_INDEX8) ||
		(width * height) < 4) { /* TxQuantize requirement: width * height must be 4 or larger. */
	  free(tex);
	  tex = nullptr;
#if !DEBUG
#endif
	  continue;
	}

	/* analyze and determine best format to quantize */
	if (format == graphics::internalcolorFormat::RGBA8) {
	  int i;
	  int alphabits = 0;
	  int fullalpha = 0;
	  boolean intensity = 1;

	  if (!(_options & LET_TEXARTISTS_FLY)) {
		/* HACK ALERT! */
		/* Account for Rice's weirdness with fmt:0 siz:2 textures.
		 * Although the conditions are relaxed with other formats,
		 * the D3D RGBA5551 surface is used for this format in certain
		 * cases. See Nintemod's SuperMario64 life gauge and power
		 * meter. The same goes for fmt:2 textures. See Mollymutt's
		 * PaperMario text. */
		if ((fmt == 0 && siz == 2) || fmt == 2) {
		  /* round A comp */
		  for (i = 0; i < height * width; i++) {
			uint32 texel = ((uint32*)tex)[i];
			((uint32*)tex)[i] = ((texel & 0xff000000) == 0xff000000 ? 0xff000000 : 0) |
								(texel & 0x00ffffff);
		  }
		  /* Substitute texel color with the average of the surrounding
		   * opaque texels. This removes borders regardless of hardware
		   * texture filtering (bilinear, etc). */
		  int j;
		  for (i = 0; i < height; i++) {
			for (j = 0; j < width; j++) {
			  uint32 texel = ((uint32*)tex)[i * width + j];
			  if ((texel & 0xff000000) != 0xff000000) {
				uint32 tmptexel[8];
				uint32 k, numtexel, r, g, b;
				numtexel = r = g = b = 0;
				memset(&tmptexel, 0, sizeof(tmptexel));
				if (i > 0) {
				  tmptexel[0] = ((uint32*)tex)[(i - 1) * width + j];                        /* north */
				  if (j > 0)         tmptexel[1] = ((uint32*)tex)[(i - 1) * width + j - 1]; /* north-west */
				  if (j < width - 1) tmptexel[2] = ((uint32*)tex)[(i - 1) * width + j + 1]; /* north-east */
				}
				if (i < height - 1) {
				  tmptexel[3] = ((uint32*)tex)[(i + 1) * width + j];                        /* south */
				  if (j > 0)         tmptexel[4] = ((uint32*)tex)[(i + 1) * width + j - 1]; /* south-west */
				  if (j < width - 1) tmptexel[5] = ((uint32*)tex)[(i + 1) * width + j + 1]; /* south-east */
				}
				if (j > 0)         tmptexel[6] = ((uint32*)tex)[i * width + j - 1]; /* west */
				if (j < width - 1) tmptexel[7] = ((uint32*)tex)[i * width + j + 1]; /* east */
				for (k = 0; k < 8; k++) {
				  if ((tmptexel[k] & 0xff000000) == 0xff000000) {
					b += ((tmptexel[k] & 0x00ff0000) >> 16);
					g += ((tmptexel[k] & 0x0000ff00) >>  8);
					r += ((tmptexel[k] & 0x000000ff)      );
					numtexel++;
				  }
				}
				if (numtexel) {
				  ((uint32*)tex)[i * width + j] = ((b / numtexel) << 16) |
												  ((g / numtexel) <<  8) |
												  ((r / numtexel)      );
				} else {
				  ((uint32*)tex)[i * width + j] = texel & 0x00ffffff;
				}
			  }
			}
		  }
		}
	  }

	  /* simple analysis of texture */
	  for (i = 0; i < height * width; i++) {
		uint32 texel = ((uint32*)tex)[i];
		if (alphabits != 8) {
#if AGGRESSIVE_QUANTIZATION
		  if ((texel & 0xff000000) < 0x00000003) {
			alphabits = 1;
			fullalpha++;
		  } else if ((texel & 0xff000000) < 0xfe000000) {
			alphabits = 8;
		  }
#else
		  if ((texel & 0xff000000) == 0x00000000) {
			alphabits = 1;
			fullalpha++;
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
	  if (_maxbpp < 32 || _options & FORCE16BPP_HIRESTEX) {
		if      (alphabits == 0) destformat = graphics::internalcolorFormat::RGB8;
		else if (alphabits == 1) destformat = graphics::internalcolorFormat::RGB5_A1;
		else                     destformat = graphics::internalcolorFormat::RGBA8;
	  } else {
		destformat = graphics::internalcolorFormat::RGBA8;
	  }
	  if (fmt == 4 && alphabits == 0) {
		destformat = graphics::internalcolorFormat::RGBA8;
		/* Rice I format; I = (R + G + B) / 3 */
		for (i = 0; i < height * width; i++) {
		  uint32 texel = ((uint32*)tex)[i];
		  uint32 icomp = (((texel >> 16) & 0xff) +
						  ((texel >>  8) & 0xff) +
						  ((texel      ) & 0xff)) / 3;
		  ((uint32*)tex)[i] = (icomp << 24) | (texel & 0x00ffffff);
		}
	  }

	}
	/*
	 * Rice hi-res textures: end */


	/* XXX: only RGBA8888 for now. comeback to this later... */
	if (format == graphics::internalcolorFormat::RGBA8) {

	  /* minification */
	  if (width > _maxwidth || height > _maxheight) {
		int ratio = 1;
		if (width / _maxwidth > height / _maxheight) {
		  ratio = (int)ceil((double)width / _maxwidth);
		} else {
		  ratio = (int)ceil((double)height / _maxheight);
		}
		if (!_txReSample->minify(&tex, &width, &height, ratio)) {
		  free(tex);
		  tex = nullptr;
		  continue;
		}
	  }

#if POW2_TEXTURES
#if (POW2_TEXTURES == 2)
		/* 3dfx Glide3x aspect ratio (8:1 - 1:8) */
		if (!_txReSample->nextPow2(&tex, &width , &height, 32, 1)) {
#else
		/* normal pow2 expansion */
		if (!_txReSample->nextPow2(&tex, &width , &height, 32, 0)) {
#endif
		  free(tex);
		  tex = nullptr;
		  continue;
		}
#endif

	  /* quantize */
	  {
		tmptex = (uint8 *)malloc(TxUtil::sizeofTx(width, height, destformat));
		if (tmptex == nullptr) {
			free(tex);
			tex = nullptr;
			result = resError;
			break;
		}
		if (destformat == graphics::internalcolorFormat::RGBA8 ||
			destformat == graphics::internalcolorFormat::RGBA4) {
			if (_maxbpp < 32 || _options & FORCE16BPP_HIRESTEX)
				destformat = graphics::internalcolorFormat::RGBA4;
		} else if (destformat == graphics::internalcolorFormat::RGB5_A1) {
			if (_maxbpp < 32 || _options & FORCE16BPP_HIRESTEX)
				destformat = graphics::internalcolorFormat::RGB5_A1;
		}
		if (_txQuantize->quantize(tex, tmptex, width, height, graphics::internalcolorFormat::RGBA8, destformat, 0)) {
			format = destformat;
			free(tex);
			tex = tmptex;
		} else
			free(tmptex);
		tmptex = nullptr;
	  }
	}


	/* last minute validations */
	if (!tex || !chksum || !width || !height || format == graphics::internalcolorFormat::NOCOLOR || width > _maxwidth || height > _maxheight) {
#if !DEBUG
#endif
	  if (tex) {
		free(tex);
		tex = nullptr;
	  }
	  continue;
	}

	/* load it into hires texture cache. */
	{
	  uint64 chksum64 = (uint64)palchksum;
	  chksum64 <<= 32;
	  chksum64 |= (uint64)chksum;

	  GHQTexInfo tmpInfo;
	  tmpInfo.data = tex;
	  tmpInfo.width = width;
	  tmpInfo.height = height;
	  tmpInfo.is_hires_tex = 1;
	  setTextureFormat(format, &tmpInfo);

	  /* remove redundant in cache */
	  if (replace)
		TxCache::del(chksum64);

	  /* add to cache */
	  const boolean added = TxCache::add(chksum64, &tmpInfo);
	  free(tex);
	  if (!added) {
		  result = resError;
		  break;
	  }
	}

  }
  retro_closedir(dir);

  return result;
}
