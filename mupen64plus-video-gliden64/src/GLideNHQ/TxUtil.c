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

/* C89: GLideNHQ's checksums, its worker pool and its texture buffers. */

#include <stdlib.h>
#include <string.h>

#include <features/features_cpu.h>

#include "TxUtil.h"

/*
 * Utilities
 ******************************************************************************/

static uint32 rice_crc32(const uint8 *src, int width, int height, int size, int rowStride);
static boolean rice_crc32_ci4(const uint8 *src, int width, int height, int rowStride, uint32 *crc32, uint32 *cimax);
static boolean rice_crc32_ci8(const uint8 *src, int width, int height, int rowStride, uint32 *crc32, uint32 *cimax);

uint32 txutil_checksum(const uint8 *src, int width, int height, int size, int rowStride)
{
	/* Rice CRC32 for now. We can switch this to Jabo MD5 or
   * any other custom checksum.
   * TODO: use *_HIRESTEXTURE option. */

	if (!src) return 0;

	return rice_crc32(src, width, height, size, rowStride);
}

uint64 txutil_checksum64(const uint8 *src, int width, int height, int size, int rowStride, const uint8 *palette)
{
	/* Rice CRC32 for now. We can switch this to Jabo MD5 or
   * any other custom checksum.
   * TODO: use *_HIRESTEXTURE option. */
	/* Returned value is 64bits: hi=palette crc32 low=texture crc32 */
	uint64 crc64Ret = 0;

	if (!src) return 0;

	if (palette) {
		uint32 crc32 = 0, cimax = 0;
		switch (size & 0xff) {
		case 1:
			if (rice_crc32_ci8(src, width, height, rowStride, &crc32, &cimax)) {
				crc64Ret = (uint64)rice_crc32(palette, cimax + 1, 1, 2, 512);
				crc64Ret <<= 32;
				crc64Ret |= (uint64)crc32;
			}
		break;
		case 0:
			if (rice_crc32_ci4(src, width, height, rowStride, &crc32, &cimax)) {
				crc64Ret = (uint64)rice_crc32(palette, cimax + 1, 1, 2, 32);
				crc64Ret <<= 32;
				crc64Ret |= (uint64)crc32;
			}
		}
	}
	if (!crc64Ret) {
		crc64Ret = (uint64)rice_crc32(src, width, height, size, rowStride);
	}

	return crc64Ret;
}

/* Rice CRC32 for hires texture packs */
/* NOTE: The following is used in Glide64 to calculate the CRC32
 * for Rice hires texture packs.
 *
 * BYTE* addr = (BYTE*)(gfx.RDRAM +
 *                     rdp.addr[rdp.tiles[tile].t_mem] +
 *                     (rdp.tiles[tile].ul_t * bpl) +
 *                     (((rdp.tiles[tile].ul_s<<rdp.tiles[tile].size)+1)>>1));
 * rice_crc32(addr,
 *          rdp.tiles[tile].width,
 *          rdp.tiles[tile].height,
 *          (unsigned short)(rdp.tiles[tile].format << 8 | rdp.tiles[tile].size),
 *          bpl);
 */
static uint32 rice_crc32(const uint8* src, int width, int height, int size, int rowStride)
{
	/* NOTE: bytes_per_width must be equal or larger than 4 */

	uint32 crc32Ret = 0;
	/* the texel size in the low byte, the N64 format above it. Shifted by
	 * all of it, x86 took the count's low five bits and ARM its low byte:
	 * the texel size either way, as here */
	const uint32 bytesPerLine = (uint32)width << (size & 0xff) >> 1;

	{
		int y = height - 1;
		while (y >= 0)
		{
			uint32 esi = 0;
			int x = bytesPerLine - 4;
			while (x >= 0)
			{
				/* rows start anywhere: read as the CPU holds the word */
				memcpy(&esi, src + x, 4);
				esi ^= x;

				crc32Ret = (crc32Ret << 4) + ((crc32Ret >> 28) & 15);
				crc32Ret += esi;
				x -= 4;
			}
			esi ^= y;
			crc32Ret += esi;
			src += rowStride;
			--y;
		}
	}

	return crc32Ret;
}

static uint8 calculate_max_ci8b(const uint8* src, uint32 width, uint32 height, uint32 rowStride)
{
	uint8 val = 0;
	uint32 x, y;
	for (y = 0; y < height; ++y) {
		const uint8 * buf = src + rowStride * y;
		for (x = 0; x<width; ++x) {
			if (buf[x] > val)
				val = buf[x];
			if (val == 0xFF)
				return 0xFF;
		}
	}
	return val;
}

static uint8 calculate_max_ci4b(const uint8* src, uint32 width, uint32 height, uint32 rowStride)
{
	uint8 val = 0;
	uint8 val1, val2;
	uint32 x, y;
	width >>= 1;
	for (y = 0; y < height; ++y) {
		const uint8 * buf = src + rowStride * y;
		for (x = 0; x<width; ++x) {
			val1 = buf[x] >> 4;
			val2 = buf[x] & 0xF;
			if (val1 > val) val = val1;
			if (val2 > val) val = val2;
			if (val == 0xF)
				return 0xF;
		}
	}
	return val;
}

static boolean rice_crc32_ci4(const uint8* src, int width, int height, int rowStride,
					  uint32* crc32, uint32* cimax)
{
	/* NOTE: bytes_per_width must be equal or larger than 4 */

	uint32 crc32Ret = 0;
	uint32 cimaxRet = 0;

	/* 4bit CI */
	{
		crc32Ret = rice_crc32(src, width, height, 0, rowStride);
		cimaxRet = calculate_max_ci4b(src, width, height, rowStride);
	}

	*crc32 = crc32Ret;
	*cimax = cimaxRet;

	return 1;
}

static boolean rice_crc32_ci8(const uint8* src, int width, int height, int rowStride,
					  uint32* crc32, uint32* cimax)
{
	/* NOTE: bytes_per_width must be equal or larger than 4 */

	uint32 crc32Ret = 0;
	uint32 cimaxRet = 0;

	/* 8bit CI */
	{
		crc32Ret = rice_crc32(src, width, height, 1, rowStride);
		cimaxRet = calculate_max_ci8b(src, width, height, rowStride);
	}

	*crc32 = crc32Ret;
	*cimax = cimaxRet;

	return 1;
}

int txutil_texture_bytes(int width, int height, uint32 format)
{
	switch (format) {
	case TXFMT_COLOR_INDEX8:
		return width * height;
	case TXFMT_RGBA4:
	case TXFMT_RGB5_A1:
	case TXFMT_RGB8:
		return (width * height) << 1;
	case TXFMT_RGBA8:
		return (width * height) << 2;
	}
	return 0;
}

void txutil_set_format(GHQTexInfo *info, uint32 format)
{
	info->format = format;
	info->texture_format = format == TXFMT_RGB8 ? GLI_CF_RED_GREEN_BLUE : GLI_CF_RGBA;
	switch (format) {
	case TXFMT_RGB8:    info->pixel_type = GLI_DT_UNSIGNED_SHORT_5_6_5; break;
	case TXFMT_RGBA4:   info->pixel_type = GLI_DT_UNSIGNED_SHORT_4_4_4_4; break;
	case TXFMT_RGB5_A1: info->pixel_type = GLI_DT_UNSIGNED_SHORT_5_5_5_1; break;
	default:            info->pixel_type = GLI_DT_UNSIGNED_BYTE; break; /* RGBA8, and what GLideNHQ has not */
	}
}

uint32 txutil_cores(void)
{
	/* Physical cores, not logical: an SMT sibling is a poor lane next to
	 * the emulator and the frontend, and these filters are not the main
	 * work on the machine. */
	uint32 numcore = cpu_features_get_core_amount_physical();
	if (numcore < 1) numcore = 1;
	if (numcore > MAX_NUMCORE) numcore = MAX_NUMCORE;
	return numcore;
}

static txpool_t *s_pool = NULL;
static unsigned s_poolRefs = 0;

txpool_t *txutil_pool(void)
{
	return s_pool;
}

void txutil_pool_acquire(void)
{
	if (s_poolRefs++ == 0)
		s_pool = txpool_new(txutil_cores());
}

void txutil_pool_release(void)
{
	if (s_poolRefs > 0 && --s_poolRefs == 0) {
		txpool_free(s_pool);
		s_pool = NULL;
	}
}

/*
 * Memory buffers for texture manipulations
 ******************************************************************************/

/* two textures' worth, and deposterize's scratch, a texture's texels each:
 * the threads filtering a texture each use their own rows of it. Made at
 * init, freed at shutdown; nothing else frees them. */
static uint8 *s_tex[2];
static uint32 s_size[2];
static uint32 *s_scratch[2];

boolean txmembuf_init(int maxwidth, int maxheight)
{
	uint32 i;
	for (i = 0; i < 2; i++) {
		if (s_tex[i] == NULL) {
			s_tex[i] = (uint8 *)malloc((size_t)maxwidth * maxheight * 4);
			s_size[i] = (uint32)maxwidth * maxheight * 4;
		}
		if (s_tex[i] == NULL) {
			txmembuf_shutdown();
			return 0;
		}
		if (s_scratch[i] == NULL)
			s_scratch[i] = (uint32 *)malloc((size_t)maxwidth * maxheight * sizeof(uint32));
		if (s_scratch[i] == NULL) {
			txmembuf_shutdown();
			return 0;
		}
	}
	return 1;
}

void txmembuf_shutdown(void)
{
	uint32 i;
	for (i = 0; i < 2; i++) {
		free(s_tex[i]);
		s_tex[i] = NULL;
		s_size[i] = 0;
		free(s_scratch[i]);
		s_scratch[i] = NULL;
	}
}

uint8 *txmembuf_get(uint32 num)
{
	return num < 2 ? s_tex[num] : NULL;
}

uint32 txmembuf_size(uint32 num)
{
	return num < 2 ? s_size[num] : 0;
}

uint32 *txmembuf_scratch(uint32 num)
{
	return num < 2 ? s_scratch[num] : NULL;
}
