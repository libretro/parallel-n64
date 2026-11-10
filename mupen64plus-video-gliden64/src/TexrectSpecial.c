#include <string.h>

#include "Types.h"
#include "N64.h"
#include "GBI.h"
#include "gDP.h"
#include "gSP.h"
#include "convert.h"
#include "gli_drawer.h"

int gli_texrect_is_depth_copy(void)
{
	const gDPTile *pTile = gSP.textureTile[0];
	return pTile->loadType == LOADTYPE_BLOCK && gDP.textureImage.size == 2 &&
		gDP.textureImage.address >= gDP.depthImageAddress &&
		gDP.textureImage.address < (gDP.depthImageAddress + gDP.colorImage.width * gDP.scissor.lry * 2);
}

/* The 16-bit words from RDRAM byte @_address to the end of RDRAM */
static u32 rdram_words_from(u32 _address)
{
	return _address <= RDRAMSize ? (RDRAMSize + 1 - _address) >> 1 : 0;
}

/* A rect with no width, or one starting left of the image, copies nothing */
static int rect_empty(const gli_texrect_params *_p)
{
	return !(_p->ulx >= 0.0f && _p->lrx > _p->ulx);
}

void gli_texrect_depth_row(const gli_texrect_params *_p)
{
	u32 width, ulx, src, words, x;
	const u16 *pSrc = (const u16 *)TMEM;
	u16 *pDst = (u16 *)(RDRAM + gDP.colorImage.address);

	if (rect_empty(_p))
		return;
	width = (u32)(_p->lrx - _p->ulx);
	ulx = (u32)_p->ulx;
	/* TMEM is 2048 words and its addresses wrap */
	src = (u32)(_p->s / 32);
	/* past word @words of the image nothing lands in RDRAM */
	words = rdram_words_from(gDP.colorImage.address);
	if (ulx > words)
		return;
	if (width > words + 1 - ulx)
		width = words + 1 - ulx;
	for (x = 0; x < width; ++x) {
		const u32 dst = (ulx + x) ^ 1;
		if (dst < words)
			pDst[dst] = swapword(pSrc[(src + x) & 0x7FF]);
	}
}

int gli_texrect_bg_copy(const gli_texrect_params *_p)
{
	f32 flry = _p->lry;
	u32 width, tex_width, uly, lry, y, src, dst;

	if (gDP.colorImage.size > G_IM_SIZ_8b)
		return 0;
	if (rect_empty(_p))
		return 1;

	if (flry > gDP.scissor.lry)
		flry = gDP.scissor.lry;

	width = (u32)(_p->lrx - _p->ulx);
	tex_width = gSP.textureTile[0]->line << 3;
	uly = (u32)_p->uly;
	lry = (u32)flry;

	src = gDP.loadInfo[gSP.textureTile[0]->tmem].texAddress + tex_width * _p->t / 32 + _p->s / 32;
	dst = gDP.colorImage.address + (u32)_p->ulx;

	/* each row clipped to the end of RDRAM, at either end of the copy */
	for (y = uly; y < lry; ++y) {
		const u32 from = src + (y - uly) * tex_width;
		const u32 to = dst + y * gDP.colorImage.width;
		u32 n = width;
		if (from > RDRAMSize || to > RDRAMSize)
			continue;
		if (n > RDRAMSize + 1 - from)
			n = RDRAMSize + 1 - from;
		if (n > RDRAMSize + 1 - to)
			n = RDRAMSize + 1 - to;
		/* the texture and the image may share RDRAM */
		memmove(RDRAM + to, RDRAM + from, n);
	}
	return 1;
}

/* An RGBA5551 word from a color's r, g, b */
static u16 color16(f32 _r, f32 _g, f32 _b)
{
	const u8 r = (u8)(_r * 31.0f);
	const u8 g = (u8)(_g * 31.0f);
	const u8 b = (u8)(_b * 31.0f);
	return (u16)((r << 11) | (g << 6) | (b << 1) | 1);
}

int gli_texrect_palette_mod(const gli_texrect_params *_p)
{
	u16 env16, prim16;
	const u16 *src;
	u16 *dst;
	u32 i, words;

	if (gDP.scissor.lrx != 16 || gDP.scissor.lry != 1 || _p->lrx != 16 || _p->lry != 1)
		return 0;
	env16 = color16(gDP.envColor.r, gDP.envColor.g, gDP.envColor.b);
	prim16 = color16(gDP.primColor.r, gDP.primColor.g, gDP.primColor.b);
	src = (const u16 *)&TMEM[256];
	dst = (u16 *)(RDRAM + gDP.colorImage.address);
	words = rdram_words_from(gDP.colorImage.address);
	for (i = 0; i < 16; ++i) {
		if ((i ^ 1) < words)
			dst[i ^ 1] = (src[i << 2] & 0x100) ? prim16 : env16;
	}
	return 1;
}
