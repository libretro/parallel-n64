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

void gli_texrect_depth_row(const gli_texrect_params *_p)
{
	const u32 width = (u32)(_p->lrx - _p->ulx);
	const u32 ulx = (u32)_p->ulx;
	const u16 *pSrc = ((const u16 *)TMEM) + _p->s / 32;
	u16 *pDst = (u16 *)(RDRAM + gDP.colorImage.address);
	u32 x;
	for (x = 0; x < width; ++x)
		pDst[(ulx + x) ^ 1] = swapword(pSrc[x]);
}

int gli_texrect_bg_copy(const gli_texrect_params *_p)
{
	f32 flry = _p->lry;
	u32 width, tex_width, uly, lry, y;
	const u8 *texaddr;
	u8 *fbaddr;

	if (gDP.colorImage.size > G_IM_SIZ_8b)
		return 0;

	if (flry > gDP.scissor.lry)
		flry = gDP.scissor.lry;

	width = (u32)(_p->lrx - _p->ulx);
	tex_width = gSP.textureTile[0]->line << 3;
	uly = (u32)_p->uly;
	lry = (u32)flry;

	texaddr = RDRAM + gDP.loadInfo[gSP.textureTile[0]->tmem].texAddress + tex_width * _p->t / 32 + _p->s / 32;
	fbaddr = RDRAM + gDP.colorImage.address + (u32)_p->ulx;

	for (y = uly; y < lry; ++y)
		memcpy(fbaddr + y * gDP.colorImage.width, texaddr + (y - uly) * tex_width, width);
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
	u32 i;

	if (gDP.scissor.lrx != 16 || gDP.scissor.lry != 1 || _p->lrx != 16 || _p->lry != 1)
		return 0;
	env16 = color16(gDP.envColor.r, gDP.envColor.g, gDP.envColor.b);
	prim16 = color16(gDP.primColor.r, gDP.primColor.g, gDP.primColor.b);
	src = (const u16 *)&TMEM[256];
	dst = (u16 *)(RDRAM + gDP.colorImage.address);
	for (i = 0; i < 16; ++i)
		dst[i ^ 1] = (src[i << 2] & 0x100) ? prim16 : env16;
	return 1;
}
