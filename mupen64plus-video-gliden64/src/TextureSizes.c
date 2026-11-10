#include <stdint.h>

#include "Types.h"
#include "N64.h"
#include "GBI.h"
#include "RDP.h"
#include "gDP.h"
#include "gSP.h"
#include "CRC.h"
#include "Config.h"
#include "gli_texdecode.h"

static u32 pow2(u32 dim)
{
	u32 i = 1;
	while (i < dim)
		i <<= 1;
	return i;
}

static u32 powof(u32 dim)
{
	u32 num = 1;
	u32 i = 0;
	while (num < dim) {
		num <<= 1;
		i++;
	}
	return i;
}

void gli_tex_tile_sizes(u32 _t, gli_tile_sizes *_sizes, gDPTile *_pLoadTile)
{
	gDPTile *pTile = _t < 2 ? gSP.textureTile[_t] : &gDP.tiles[_t];
	const gli_tex_format *loadParams;
	u32 maxTexels, tileWidth, tileHeight, lineWidth, lineHeight;
	u32 maskWidth, maskHeight, width, height, tMemMask;
	gDPLoadTileInfo *info;

	pTile->masks = pTile->originalMaskS;
	pTile->maskt = pTile->originalMaskT;

	loadParams = gli_tex_format_get(gDP.otherMode.textureLUT, pTile->size, pTile->format);
	maxTexels = loadParams->maxTexels;
	tileWidth = ((pTile->lrs - pTile->uls) & 0x03FF) + 1;
	tileHeight = ((pTile->lrt - pTile->ult) & 0x03FF) + 1;
	if (tileWidth == 1 && tileHeight == 1 &&
		gDP.otherMode.cycleType == G_CYC_COPY &&
		_pLoadTile != NULL &&
		_pLoadTile->loadType == LOADTYPE_BLOCK) {
		const u32 ulx = _SHIFTR(RDP.w1, 14, 10);
		const u32 uly = _SHIFTR(RDP.w1, 2, 10);
		const u32 lrx = _SHIFTR(RDP.w0, 14, 10);
		const u32 lry = _SHIFTR(RDP.w0, 2, 10);
		tileWidth = lrx - ulx + 1;
		tileHeight = lry - uly + 1;
	}

	lineWidth = pTile->line << loadParams->lineShift;
	lineHeight = 0;
	if (lineWidth != 0) {
		lineHeight = maxTexels / lineWidth;
		if (lineHeight > tileHeight)
			lineHeight = tileHeight;
	}

	maskWidth = 1 << pTile->masks;
	maskHeight = 1 << pTile->maskt;

	tMemMask = gDP.otherMode.textureLUT == G_TT_NONE ? 0x1FF : 0xFF;
	info = &gDP.loadInfo[pTile->tmem & tMemMask];
	if (pTile->tmem == gDP.loadTile->tmem) {
		if (gDP.loadTile->loadWidth != 0 && gDP.loadTile->masks == 0)
			info->width = gDP.loadTile->loadWidth;
		if (gDP.loadTile->loadHeight != 0 && gDP.loadTile->maskt == 0) {
			info->height = gDP.loadTile->loadHeight;
			info->bytes = info->height * (gDP.loadTile->line << 3);
			/* A 32-bit texture fills both halves of TMEM */
			if (gDP.loadTile->size == G_IM_SIZ_32b)
				info->bytes *= 2;
		}
		gDP.loadTile->loadWidth = gDP.loadTile->loadHeight = 0;
	}
	_sizes->bytes = info->bytes;
	if (info->loadType == LOADTYPE_TILE) {
		if (pTile->masks && ((maskWidth * maskHeight) <= maxTexels))
			width = maskWidth;
		else {
			width = info->width;
			if (info->size > pTile->size)
				width <<= info->size - pTile->size;
		}
		if (pTile->maskt && ((maskWidth * maskHeight) <= maxTexels))
			height = maskHeight;
		else
			height = info->height;
	} else {
		/* the mask if set and valid, else the tile, else the line */
		if (pTile->masks && ((maskWidth * maskHeight) <= maxTexels))
			width = maskWidth;
		else if ((tileWidth * tileHeight) <= maxTexels)
			width = tileWidth;
		else
			width = lineWidth;

		if (pTile->maskt && ((maskWidth * maskHeight) <= maxTexels))
			height = maskHeight;
		else if ((tileWidth * tileHeight) <= maxTexels)
			height = tileHeight;
		else
			height = lineHeight;
	}

	_sizes->clampWidth = (pTile->clamps && gDP.otherMode.cycleType != G_CYC_COPY) ? tileWidth : width;
	_sizes->clampHeight = (pTile->clampt && gDP.otherMode.cycleType != G_CYC_COPY) ? tileHeight : height;

	/* Keep the masks inside the texture */
	if (maskWidth > width) {
		pTile->masks = powof(width);
		maskWidth = 1 << pTile->masks;
	}
	if (maskHeight > height) {
		pTile->maskt = powof(height);
		maskHeight = 1 << pTile->maskt;
	}

	_sizes->maskWidth = maskWidth;
	_sizes->maskHeight = maskHeight;
	_sizes->width = width;
	_sizes->height = height;

	if (pTile->clamps != 0)
		_sizes->realWidth = _sizes->clampWidth;
	else if (pTile->masks != 0)
		_sizes->realWidth = _sizes->maskWidth;
	else
		_sizes->realWidth = _sizes->width;

	if (pTile->clampt != 0)
		_sizes->realHeight = _sizes->clampHeight;
	else if (pTile->maskt != 0)
		_sizes->realHeight = _sizes->maskHeight;
	else
		_sizes->realHeight = _sizes->height;

	if (gSP.texture.level > 0) {
		_sizes->realWidth = pow2(_sizes->realWidth);
		_sizes->realHeight = pow2(_sizes->realHeight);
	}
}

/* Hashed after the texels; the layout is part of the cache key */
typedef struct gli_tex_key_params
{
	u16 width;
	u16 height;
	u32 flags;
} gli_tex_key_params;

u64 gli_tex_crc(u32 _t, const gli_tile_sizes *_sizes)
{
	const gDPTile *pTile = gSP.textureTile[_t];
	const int rgba32 = pTile->size == G_IM_SIZ_32b;
	u32 bytes = _sizes->bytes;
	u32 tMemMask;
	u64 crc = UINT64_MAX;
	gli_tex_key_params params;

	params.flags = pTile->masks |
		(pTile->maskt << 4) |
		(pTile->mirrors << 8) |
		(pTile->mirrort << 9) |
		(pTile->clamps << 10) |
		(pTile->clampt << 11) |
		(pTile->size << 12) |
		(pTile->format << 14) |
		(gDP.otherMode.textureLUT << 17);
	params.width = (u16)_sizes->realWidth;
	params.height = (u16)_sizes->realHeight;

	if (bytes == 0)
		bytes = params.height * (pTile->line << 3);
	if (rgba32)
		bytes >>= 1;
	tMemMask = (gDP.otherMode.textureLUT == G_TT_NONE && !rgba32) ? 0x1FF : 0xFF;
	crc = CRC_Calculate(crc, &TMEM[pTile->tmem & tMemMask], bytes);

	if (rgba32)
		crc = CRC_Calculate(crc, &TMEM[pTile->tmem + 256], bytes);

	if (gDP.otherMode.textureLUT != G_TT_NONE || pTile->format == G_IM_FMT_CI) {
		if (pTile->size == G_IM_SIZ_4b)
			crc = CRC_Calculate(crc, &gDP.paletteCRC16[pTile->palette], sizeof(u64));
		else if (pTile->size == G_IM_SIZ_8b)
			crc = CRC_Calculate(crc, &gDP.paletteCRC256, sizeof(u64));
	}

	if (config.generalEmulation.enableLOD != 0 && gSP.texture.level > 1 && _t > 0)
		crc = CRC_Calculate(crc, &gSP.texture.level, 4);

	return CRC_Calculate(crc, &params, sizeof(params));
}

/* From RiceVideo */
static u32 CalculateDXT(u32 txl2words)
{
	if (txl2words == 0)
		return 1;
	return (2048 + txl2words - 1) / txl2words;
}

static u32 Txl2Words(u32 width, u32 size)
{
	static const u32 sizeBytes[4] = { 0, 1, 2, 4 };
	u32 words = size == 0 ? width / 16 : width * sizeBytes[size] / 8;
	return words > 1 ? words : 1;
}

u32 gli_tex_reverse_dxt(u32 val, u32 lrs, u32 width, u32 size)
{
	int low, high, i;
	(void)lrs;
	if (val == 0x800)
		return 1;

	low = 2047 / val;
	if (CalculateDXT(low) > val)
		low++;
	high = 2047 / (val - 1);

	if (low == high)
		return low;

	for (i = low; i <= high; i++) {
		if (Txl2Words(width, size) == (u32)i)
			return i;
	}

	return (low + high) / 2;
}
