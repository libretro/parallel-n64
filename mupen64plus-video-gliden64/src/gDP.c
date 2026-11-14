#include <assert.h>
#include <string.h>
#include "GLideN64.h"
#include "N64.h"
#include "GBI.h"
#include "RSP.h"
#include "RDP.h"
#include "gDP.h"
#include "gSP.h"
#include "Types.h"
#include "convert.h"
#include "CRC.h"
#include "CombinerMux.h"
#include "Config.h"
#include "TextureFilterHandler.h"
#include "gli_drawer.h"
#include "gli_lle_tri.h"
#include "gli_framebuffer.h"

/* b only if it is strictly less/greater, as std::min and std::max choose */
static u32 gdp_min(u32 _a, u32 _b)
{
	return _b < _a ? _b : _a;
}

static u32 gdp_max(u32 _a, u32 _b)
{
	return _a < _b ? _b : _a;
}

static void gdp_clear_rect_color(void)
{
	gDP.rectColor.r = 0.0f;
	gDP.rectColor.g = 0.0f;
	gDP.rectColor.b = 0.0f;
	gDP.rectColor.a = 0.0f;
}

gDPInfo gDP;

bool gli_isCurrentColorImageDepthImage(void)
{
	return (gDP.colorImage.address == gDP.depthImageAddress) ||
		(gDP.fillColor.color == DepthClearColor && gDP.otherMode.cycleType == G_CYC_FILL);
}

bool gli_isDepthCompareEnabled(void)
{
	return gDP.otherMode.cycleType <= G_CYC_2CYCLE &&
		gDP.otherMode.depthCompare != 0 &&
		((gSP.geometryMode & G_ZBUFFER) || gDP.otherMode.depthSource == G_ZS_PRIM);
}

void gli_gDPSetOtherMode( u32 mode0, u32 mode1 )
{
	gDP.otherMode.h = mode0;
	gDP.otherMode.l = mode1;

	gDP.changed |= CHANGED_RENDERMODE | CHANGED_CYCLETYPE | CHANGED_ALPHACOMPARE;

}

void gli_gDPSetPrimDepth( u16 z, u16 dz )
{
	gDP.primDepth.z = _FIXED2FLOAT(_SHIFTR(z, 0, 15), 15);
	gDP.primDepth.deltaZ = _FIXED2FLOAT(_SHIFTR(dz, 0, 15), 15);
}

void gli_gDPSetTexturePersp( u32 enable )
{
	gDP.otherMode.texturePersp = enable & 1;

}

void gli_gDPSetTextureLUT( u32 mode )
{
	gDP.otherMode.textureLUT = mode & 3;

}

void gli_gDPSetCombine( s32 muxs0, s32 muxs1 )
{
	gDP.combine.muxs0 = muxs0;
	gDP.combine.muxs1 = muxs1;

	gDP.changed |= CHANGED_COMBINE;

}

void gli_gDPSetColorImage( u32 format, u32 size, u32 width, u32 address )
{
	address = RSP_SegmentToPhysical( address );

	gDP.colorImage.format = format;
	gDP.colorImage.size = size;
	gDP.colorImage.width = width;
	gDP.colorImage.height = 0;
	gDP.colorImage.address = address;

	gli_fb_set_color_image(address, format, size, width);

}

void gli_gDPSetTextureImage(u32 format, u32 size, u32 width, u32 address)
{
	gDP.textureImage.format = format;
	gDP.textureImage.size = size;
	gDP.textureImage.width = width;
	gDP.textureImage.address = RSP_SegmentToPhysical(address);
	gDP.textureImage.bpl = gDP.textureImage.width << gDP.textureImage.size >> 1;
	if (gSP.DMAOffsets.tex_offset != 0) {
		if (format == G_IM_FMT_RGBA) {
			u16 * t = (u16*)(RDRAM + gSP.DMAOffsets.tex_offset);
			gSP.DMAOffsets.tex_shift = t[gSP.DMAOffsets.tex_count ^ 1];
			gDP.textureImage.address += gSP.DMAOffsets.tex_shift;
		} else {
			gSP.DMAOffsets.tex_offset = 0;
			gSP.DMAOffsets.tex_shift = 0;
			gSP.DMAOffsets.tex_count = 0;
		}
	}
}

void gli_gDPSetDepthImage( u32 address )
{
	address = RSP_SegmentToPhysical( address );
	gDP.depthImageAddress = address;
	gli_depth_set_image(address);

}

void gli_gDPSetEnvColor( u32 r, u32 g, u32 b, u32 a )
{
	gDP.envColor.r = _FIXED2FLOATCOLOR( r, 8 );
	gDP.envColor.g = _FIXED2FLOATCOLOR( g, 8 );
	gDP.envColor.b = _FIXED2FLOATCOLOR( b, 8 );
	gDP.envColor.a = _FIXED2FLOATCOLOR( a, 8 );

}

void gli_gDPSetBlendColor( u32 r, u32 g, u32 b, u32 a )
{
	gDP.blendColor.r = _FIXED2FLOATCOLOR( r, 8 );
	gDP.blendColor.g = _FIXED2FLOATCOLOR( g, 8 );
	gDP.blendColor.b = _FIXED2FLOATCOLOR( b, 8 );
	gDP.blendColor.a = _FIXED2FLOATCOLOR( a, 8 );

	gDP.changed |= CHANGED_BLENDCOLOR;

}

void gli_gDPSetFogColor( u32 r, u32 g, u32 b, u32 a )
{
	gDP.fogColor.r = _FIXED2FLOATCOLOR( r, 8 );
	gDP.fogColor.g = _FIXED2FLOATCOLOR( g, 8 );
	gDP.fogColor.b = _FIXED2FLOATCOLOR( b, 8 );
	gDP.fogColor.a = _FIXED2FLOATCOLOR( a, 8 );

	gDP.changed |= CHANGED_FOGCOLOR;

}

void gli_gDPSetFillColor( u32 c )
{
	gDP.fillColor.color = c;
	gDP.fillColor.z = (f32)_SHIFTR( c,  2, 14 );
	gDP.fillColor.dz = (f32)_SHIFTR( c, 0, 2 );

}

void gli_gDPGetFillColor(f32 _fillColor[4])
{
	const u32 c = gDP.fillColor.color;
	if (gDP.colorImage.size < 3) {
		_fillColor[0] = _FIXED2FLOATCOLOR( _SHIFTR( c, 11, 5 ), 5 );
		_fillColor[1] = _FIXED2FLOATCOLOR( _SHIFTR( c,  6, 5 ), 5 );
		_fillColor[2] = _FIXED2FLOATCOLOR( _SHIFTR( c,  1, 5 ), 5 );
		_fillColor[3] = (f32)_SHIFTR( c,  0, 1 );
	} else {
		_fillColor[0] = _FIXED2FLOATCOLOR( _SHIFTR( c, 24, 8 ), 8 );
		_fillColor[1] = _FIXED2FLOATCOLOR( _SHIFTR( c, 16, 8 ), 8 );
		_fillColor[2] = _FIXED2FLOATCOLOR( _SHIFTR( c,  8, 8 ), 8 );
		_fillColor[3] = _FIXED2FLOATCOLOR( _SHIFTR( c,  0, 8 ), 8 );
	}
}

void gli_gDPSetPrimColor( u32 m, u32 l, u32 r, u32 g, u32 b, u32 a )
{
	gDP.primColor.m = _FIXED2FLOAT( m, 5 );
	gDP.primColor.l = _FIXED2FLOATCOLOR( l, 8 );
	gDP.primColor.r = _FIXED2FLOATCOLOR( r, 8 );
	gDP.primColor.g = _FIXED2FLOATCOLOR( g, 8 );
	gDP.primColor.b = _FIXED2FLOATCOLOR( b, 8 );
	gDP.primColor.a = _FIXED2FLOATCOLOR( a, 8 );

}

void gli_gDPSetTile( u32 format, u32 size, u32 line, u32 tmem, u32 tile, u32 palette, u32 cmt, u32 cms, u32 maskt, u32 masks, u32 shiftt, u32 shifts )
{
	gDP.tiles[tile].format = format;
	gDP.tiles[tile].size = size;
	gDP.tiles[tile].line = line;
	gDP.tiles[tile].tmem = tmem;
	gDP.tiles[tile].palette = palette;
	gDP.tiles[tile].cmt = cmt;
	gDP.tiles[tile].cms = cms;
	gDP.tiles[tile].maskt = gDP.tiles[tile].originalMaskT = maskt;
	gDP.tiles[tile].masks = gDP.tiles[tile].originalMaskS = masks;
	gDP.tiles[tile].shiftt = shiftt;
	gDP.tiles[tile].shifts = shifts;

	if (!gDP.tiles[tile].masks) gDP.tiles[tile].clamps = 1;
	if (!gDP.tiles[tile].maskt) gDP.tiles[tile].clampt = 1;

	if (tile == gSP.texture.tile || tile == gSP.texture.tile + 1) {
		u32 nTile = gDP.loadTileIdx;
		while(gDP.tiles[nTile].tmem != tmem && nTile > gSP.texture.tile + 1)
			--nTile;
		if (nTile > gSP.texture.tile + 1) {
			gDP.tiles[tile].textureMode = gDP.tiles[nTile].textureMode;
			gDP.tiles[tile].loadType = gDP.tiles[nTile].loadType;
			gDP.tiles[tile].frameBufferAddress = gDP.tiles[nTile].frameBufferAddress;
			gDP.tiles[tile].imageAddress = gDP.tiles[nTile].imageAddress;
		}
	}

	gDP.changed |= CHANGED_TILE;

}

void gli_gDPSetTileSize( u32 tile, u32 uls, u32 ult, u32 lrs, u32 lrt )
{
	gDP.tiles[tile].uls = _SHIFTR( uls, 2, 10 );
	gDP.tiles[tile].ult = _SHIFTR( ult, 2, 10 );
	gDP.tiles[tile].lrs = _SHIFTR( lrs, 2, 10 );
	gDP.tiles[tile].lrt = _SHIFTR( lrt, 2, 10 );

	gDP.tiles[tile].fuls = _FIXED2FLOAT( uls, 2 );
	gDP.tiles[tile].fult = _FIXED2FLOAT( ult, 2 );
	gDP.tiles[tile].flrs = _FIXED2FLOAT( lrs, 2 );
	gDP.tiles[tile].flrt = _FIXED2FLOAT( lrt, 2 );

	if (gli_legacy_sm64_tools())
	{
		/* Force maskT and maskS for given sizes if we go too far */
		/* Get normal sizes for lrs/lrt */
		u32 lrsizes = gDP.tiles[tile].lrs + 1;
		u32 lrsizet = gDP.tiles[tile].lrt + 1;

		if (lrsizes == 16 && lrsizet == 2)
		{
			/* Get normal sizes for masks/maskt */
			u32 masksizes = 1 << gDP.tiles[tile].masks;
			u32 masksizet = 1 << gDP.tiles[tile].maskt;

			/* Do validity check */
			/* TODO: Not sure if this actually works */
			if (masksizes > lrsizes)
			{
				gDP.tiles[tile].masks = 4; /* log2 of lrsizes */
				gDP.tiles[tile].originalMaskS = 4;
			}

			if (masksizet > lrsizet)
			{
				gDP.tiles[tile].maskt = 1; /* log2 of lrsizet */
				gDP.tiles[tile].originalMaskT = 1;
			}
		}
	}

	gDP.changed |= CHANGED_TILE;

}

/****************/
/* LoadTile for 32bit RGBA texture */
/* Based on sources of angrylion's software plugin. */

static void gDPLoadTile32b(u32 uls, u32 ult, u32 lrs, u32 lrt)
{
	u32 ramWords;
	u32 j;
	const u32 width = lrs - uls + 1;
	const u32 height = lrt - ult + 1;
	const u32 line = gDP.loadTile->line << 2;
	const u32 tbase = gDP.loadTile->tmem << 2;
	const u32 addr = gDP.textureImage.address >> 2;
	const u32 * src = (const u32*)RDRAM;
	u16 * tmem16 = (u16*)TMEM;
	u32 c, ptr, tline, s, xorval;
	/* RDRAMSize is a byte mask (size - 1); src is indexed in 32-bit
	 * words. The source index addr + s + i is built from guest tile
	 * geometry and is otherwise unbounded, unlike the non-32b path
	 * below which clamps each row to RDRAMSize. Skip any word that
	 * would read past RDRAM. The TMEM destination is already safe
	 * (ptr is masked to 0x3ff). */
	ramWords = (RDRAMSize >> 2) + 1;

	for (j = 0; j < height; ++j) {
		u32 i;
		tline = tbase + line * j;
		s = ((j + ult) * gDP.textureImage.width) + uls;
		xorval = (j & 1) ? 3 : 1;
		for (i = 0; i < width; ++i) {
			const u32 srcIdx = addr + s + i;
			if (srcIdx >= ramWords)
				continue;
			c = src[srcIdx];
			ptr = ((tline + i) ^ xorval) & 0x3ff;
			tmem16[ptr] = c >> 16;
			tmem16[ptr | 0x400] = c & 0xffff;
		}
	}
}

void gli_gDPLoadTile(u32 tile, u32 uls, u32 ult, u32 lrs, u32 lrt)
{
	gDPLoadTileInfo *info;
	u32 width;
	u32 height;
	u32 bpl;
	u32 alignedWidth;
	u32 wmask;
	u32 bpr;
	u32 address;
	u32 bpl2;
	u32 height2;
	gli_gDPSetTileSize( tile, uls, ult, lrs, lrt );
	gDP.loadTileIdx = tile;
	gDP.loadTile = &gDP.tiles[tile];
	gDP.loadTile->loadType = LOADTYPE_TILE;
	gDP.loadTile->imageAddress = gDP.textureImage.address;

	if (gDP.loadTile->lrs < gDP.loadTile->uls || gDP.loadTile->lrt < gDP.loadTile->ult)
		return;

	width = (gDP.loadTile->lrs - gDP.loadTile->uls + 1) & 0x03FF;
	height = (gDP.loadTile->lrt - gDP.loadTile->ult + 1) & 0x03FF;
	bpl = gDP.loadTile->line << 3;

	alignedWidth = width;
	wmask = 0;
	switch (gDP.textureImage.size) {
	case G_IM_SIZ_8b:
		wmask = 7;
		break;
	case G_IM_SIZ_16b:
		wmask = 3;
		break;
	case G_IM_SIZ_32b:
		wmask = 1;
		break;
	}
	if ((width & wmask) != 0)
		alignedWidth = (width & (~wmask)) + wmask + 1;
	bpr = alignedWidth << gDP.loadTile->size >> 1;

	info = &gDP.loadInfo[gDP.loadTile->tmem];
	info->texAddress = gDP.loadTile->imageAddress;
	info->uls = gDP.loadTile->uls;
	info->ult = gDP.loadTile->ult;
	info->lrs = gDP.loadTile->lrs;
	info->lrt = gDP.loadTile->lrt;
	info->width = gDP.loadTile->masks != 0 ? (u16)gdp_min(width, 1U << gDP.loadTile->masks) : (u16)width;
	info->height = gDP.loadTile->maskt != 0 ? (u16)gdp_min(height, 1U << gDP.loadTile->maskt) : (u16)height;
	info->texWidth = gDP.textureImage.width;
	info->size = gDP.textureImage.size;
	info->loadType = LOADTYPE_TILE;
	info->bytes = bpl * height;
	if (gDP.loadTile->size == G_IM_SIZ_32b)
		/* 32 bit texture loaded into lower and upper half of TMEM, thus actual bytes doubled. */
		info->bytes *= 2;

	if (gDP.loadTile->line == 0)
		return;

	if (gDP.loadTile->masks == 0)
		gDP.loadTile->loadWidth = gdp_max(gDP.loadTile->loadWidth, info->width);
	if (gDP.loadTile->maskt == 0 && gDP.loadTile->tmem % gDP.loadTile->line == 0) {
		const u16 theight = info->height + gDP.loadTile->tmem / gDP.loadTile->line;
		gDP.loadTile->loadHeight = gdp_max(gDP.loadTile->loadHeight, theight);
	}

	address = gDP.textureImage.address + gDP.loadTile->ult * gDP.textureImage.bpl + (gDP.loadTile->uls << gDP.textureImage.size >> 1);
	bpl2 = bpl;
	if (gDP.loadTile->lrs > gDP.textureImage.width)
		bpl2 = (gDP.textureImage.width - gDP.loadTile->uls);
	height2 = height;
	if (gDP.loadTile->lrt > gDP.scissor.lry)
		height2 = (u32)gDP.scissor.lry - gDP.loadTile->ult;

	if (gli_fb_check_texture(address, info->width, bpl2*height2))
		return;

	if (gDP.loadTile->size == G_IM_SIZ_32b)
		gDPLoadTile32b(gDP.loadTile->uls, gDP.loadTile->ult, gDP.loadTile->lrs, gDP.loadTile->lrt);
	else if (address < RDRAMSize) {
		u32 y;
		u32 tmemAddr = gDP.loadTile->tmem;
		const u32 line = gDP.loadTile->line;
		for (y = 0; y < height; ++y) {
			u32 room;
			u32 rowBytes;
			/* A row that runs past the end of TMEM stops there instead of */
			/* wrapping round onto the rows loaded first.  Vivid Dolls blits its */
			/* background in 6-line strips and asks for one line more than the */
			/* 4KB holds: wrapping laid that line over line 0 and left a corrupt */
			/* line at every strip boundary.  The RDP-accurate renderers show no */
			/* such line, so dropping the overflow is what matches them. */
			const u32 tmemByte = tmemAddr << 3;
			if (tmemByte >= 0x1000)
				break;
			room = 0x1000 - tmemByte;
			rowBytes = gdp_min(bpr, room);
			if (address + bpl > RDRAMSize)
				gli_UnswapCopyWrap(RDRAM, address, (u8*)TMEM, tmemByte, 0xFFF, gdp_min(RDRAMSize - address, room));
			else
				gli_UnswapCopyWrap(RDRAM, address, (u8*)TMEM, tmemByte, 0xFFF, rowBytes);
			if (y & 1)
				gli_DWordInterleaveWrap((u32*)TMEM, tmemAddr << 1, 0x3FF, rowBytes >> 3);

			address += gDP.textureImage.bpl;
			if (address >= RDRAMSize)
				break;
			tmemAddr += line;
		}
	}

}

/****************/
/* LoadBlock for 32bit RGBA texture */
/* Based on sources of angrylion's software plugin. */

static void gDPLoadBlock32(u32 uls,u32 lrs, u32 dxt)
{
	u32 ramWords;
	const u32 * src = (const u32*)RDRAM;
	const u32 tb = gDP.loadTile->tmem << 2;
	const u32 line = gDP.loadTile->line << 2;

	u16 *tmem16 = (u16*)TMEM;
	u32 addr = gDP.loadTile->imageAddress >> 2;
	u32 width = (lrs - uls + 1) << 2;
	/* As in gDPLoadTile32b: src is indexed in 32-bit words from a
	 * guest-derived base/width and is otherwise unbounded. Skip any
	 * word past RDRAM; the TMEM destination is masked to 0x3ff. */
	ramWords = (RDRAMSize >> 2) + 1;
	if (lrs < uls)
		return;
	if (width == 4) /* lr_s == 0, 1x1 texture */
		width = 1;
	else if (width & 7)
		width = (width & (~7)) + 8;

	if (dxt != 0) {
		u32 i;
		u32 j = 0;
		u32 t = 0;
		u32 oldt = 0;
		u32 ptr;

		u32 c = 0;
		for (i = 0; i < width; i += 2) {
			oldt = t;
			t = ((j >> 11) & 1) ? 3 : 1;
			if (t != oldt)
				i += line;
			if ((addr + i) < ramWords) {
				ptr = ((tb + i) ^ t) & 0x3ff;
				c = src[addr + i];
				tmem16[ptr] = c >> 16;
				tmem16[ptr | 0x400] = c & 0xffff;
			}
			if ((addr + i + 1) < ramWords) {
				ptr = ((tb + i + 1) ^ t) & 0x3ff;
				c = src[addr + i + 1];
				tmem16[ptr] = c >> 16;
				tmem16[ptr | 0x400] = c & 0xffff;
			}
			j += dxt;
		}
	} else {
		u32 i;
		u32 c, ptr;
		for (i = 0; i < width; i++) {
			if ((addr + i) >= ramWords)
				continue;
			ptr = ((tb + i) ^ 1) & 0x3ff;
			c = src[addr + i];
			tmem16[ptr] = c >> 16;
			tmem16[ptr | 0x400] = c & 0xffff;
		}
	}
}

void gli_gDPLoadBlock(u32 tile, u32 uls, u32 ult, u32 lrs, u32 dxt)
{
	gDPLoadTileInfo *info;
	u32 width;
	u32 bytes;
	u32 address;
	gli_gDPSetTileSize( tile, uls, ult, lrs, dxt );
	gDP.loadTileIdx = tile;
	gDP.loadTile = &gDP.tiles[tile];
	gDP.loadTile->loadType = LOADTYPE_BLOCK;

	if (gSP.DMAOffsets.tex_offset != 0) {
		if (gSP.DMAOffsets.tex_shift % (((lrs>>2) + 1) << 3)) {
			gDP.textureImage.address -= gSP.DMAOffsets.tex_shift;
			gSP.DMAOffsets.tex_offset = 0;
			gSP.DMAOffsets.tex_shift = 0;
			gSP.DMAOffsets.tex_count = 0;
		} else
			++gSP.DMAOffsets.tex_count;
	}
	gDP.loadTile->imageAddress = gDP.textureImage.address;

	info = &gDP.loadInfo[gDP.loadTile->tmem];
	info->texAddress = gDP.loadTile->imageAddress;
	info->uls = gDP.loadTile->uls;
	info->ult = gDP.loadTile->ult;
	info->lrs = gDP.loadTile->lrs;
	info->lrt = gDP.loadTile->lrt;
	info->width = gDP.loadTile->lrs;
	info->dxt = dxt;
	info->size = gDP.textureImage.size;
	info->loadType = LOADTYPE_BLOCK;

	width = (lrs - uls + 1) & 0x0FFF;
	bytes = width << gDP.loadTile->size >> 1;
	if ((bytes & 7) != 0)
		bytes = (bytes & (~7)) + 8;

	info->bytes = bytes;
	address = gDP.textureImage.address + ult * gDP.textureImage.bpl + (uls << gDP.textureImage.size >> 1);

	if (bytes == 0 || (address + bytes) > RDRAMSize) {
		return;
	}

	gDP.loadTile->frameBufferAddress = 0;
	gli_fb_check_texture(address, info->width, bytes); /* Load data to TMEM even if FB texture is found. See comment to texturedRectDepthBufferCopy */

	if (gDP.loadTile->size == G_IM_SIZ_32b)
		gDPLoadBlock32(gDP.loadTile->uls, gDP.loadTile->lrs, dxt);
	else if (gDP.loadTile->format == G_IM_FMT_YUV)
		memcpy(TMEM, &RDRAM[address], gdp_min(bytes, (u32)sizeof(TMEM))); /* HACK! */
	else {
		u32 tmemAddr = gDP.loadTile->tmem;
		gli_UnswapCopyWrap(RDRAM, address, (u8*)TMEM, tmemAddr << 3, 0xFFF, bytes);
		if (dxt != 0) {
			u32 dxtCounter = 0;
			u32 qwords = (bytes >> 3);
			u32 line = 0;
			for (;;) {
				do {
					++tmemAddr;
					--qwords;
					if (qwords == 0)
						goto end_dxt_test;
					dxtCounter += dxt;
				} while ((dxtCounter & 0x800) == 0);
				do {
					++line;
					--qwords;
					if (qwords == 0)
						goto end_dxt_test;
					dxtCounter += dxt;
				} while ((dxtCounter & 0x800) != 0);
				gli_DWordInterleaveWrap((u32*)TMEM, tmemAddr << 1, 0x3FF, line);
				tmemAddr += line;
				line = 0;
			}
			end_dxt_test:
				gli_DWordInterleaveWrap((u32*)TMEM, tmemAddr << 1, 0x3FF, line);
		}
	}

}

void gli_gDPLoadTLUT( u32 tile, u32 uls, u32 ult, u32 lrs, u32 lrt )
{
	u16 count;
	u32 address;
	u16 pal;
	u16 * dest;
	u32 destIdx;
	int i;
	gli_gDPSetTileSize( tile, uls, ult, lrs, lrt );
	if (gDP.tiles[tile].tmem < 256) {
		return;
	}
	count = (u16)((gDP.tiles[tile].lrs - gDP.tiles[tile].uls + 1) * (gDP.tiles[tile].lrt - gDP.tiles[tile].ult + 1));
	address = gDP.textureImage.address + gDP.tiles[tile].ult * gDP.textureImage.bpl + (gDP.tiles[tile].uls << gDP.textureImage.size >> 1);
	pal = (u16)((gDP.tiles[tile].tmem - 256) >> 4);
	dest = (u16*)TMEM;
	destIdx = gDP.tiles[tile].tmem << 2;

	i = 0;
	while (i < count) {
		u16 j;
		for (j = 0; (j < 16) && (i < count); ++j, ++i) {
			/* address is guest-derived and advances per entry; stop if
			 * the 2-byte read would leave RDRAM. The TMEM destination is
			 * already masked to 0x07FF. */
			if ((address + 2) > RDRAMSize) {
				i = count;
				break;
			}
			dest[(destIdx | 0x0400) & 0x07FF] = swapword(*(u16*)(RDRAM + (address ^ 2)));
			address += 2;
			destIdx += 4;
		}

		gDP.paletteCRC16[pal] = CRC_CalculatePalette((u64)-1, &TMEM[256 + (pal << 4)], 16);
		pal = (pal + 1) & 0x0F;
	}

	gDP.paletteCRC256 = CRC_Calculate((u64)-1, gDP.paletteCRC16, sizeof(u64) * 16);

	if (gli_TFH_is_inited()) {
		u32 words;
		const u16 start = gDP.tiles[tile].tmem - 256; /* starting location in the palettes */
		u16 *spal = (u16*)(RDRAM + gDP.textureImage.address);
		/* Clamp the copy to the TexFilterPalette[512] destination and to
		 * RDRAM: both 'count' and the source address come from the tile
		 * descriptor. */
		words = count;
		if (start >= 512)
			words = 0;
		else if (start + words > 512)
			words = 512 - start;
		if (gDP.textureImage.address + (words << 1) > RDRAMSize)
			words = (gDP.textureImage.address < RDRAMSize)
				? (RDRAMSize - gDP.textureImage.address) >> 1 : 0;
		if (words != 0)
			memcpy((u8*)(gDP.TexFilterPalette + start), spal, words << 1);
	}

	gDP.changed |= CHANGED_TMEM;

}

void gli_gDPSetScissor(u32 mode, s16 xh, s16 yh, s16 xl, s16 yl)
{
	gDP.scissor.mode = mode;
	gDP.scissor.xh = xh;
	gDP.scissor.yh = yh;
	gDP.scissor.xl = xl;
	gDP.scissor.yl = yl;
	gDP.scissor.ulx = _FIXED2FLOAT(xh, 2);
	gDP.scissor.uly = _FIXED2FLOAT(yh, 2);
	gDP.scissor.lrx = _FIXED2FLOAT(xl, 2);
	gDP.scissor.lry = _FIXED2FLOAT(yl, 2);

	gDP.changed |= CHANGED_SCISSOR | CHANGED_REJECT_BOX;

}

void gli_gDPFillRectangle( s32 ulx, s32 uly, s32 lrx, s32 lry )
{
	enum {
		dbNone,
		dbFound,
		dbCleared
	} depthBuffer = dbNone;
	if (gDP.otherMode.cycleType == G_CYC_FILL) {
		++lrx;
		++lry;
	} else if (lry == uly)
		++lry;

	if (gDP.depthImageAddress == gDP.colorImage.address) {
		/* Game may use depth texture as auxilary color texture. Example: Mario Tennis */
		/* If color is not depth clear color, that is most likely the case */
		if (gDP.fillColor.color == DepthClearColor) {
			depthBuffer = dbFound;
			if (config.generalEmulation.enableFragmentDepthWrite == 0) {
				gli_drawer_clear_depth();
				depthBuffer = dbCleared;
			} else
				gli_depth_set_cleared(true);
		}
	} else if (gDP.fillColor.color == DepthClearColor && gDP.otherMode.cycleType == G_CYC_FILL) {
		depthBuffer = dbFound;
		gli_depth_set_image(gDP.colorImage.address);
		if (config.generalEmulation.enableFragmentDepthWrite == 0 ||
			(config.generalEmulation.hacks & hack_Snap) != 0) {
			gli_drawer_clear_depth();
			depthBuffer = dbCleared;
		} else
			gli_depth_set_cleared(true);
	}

	if (depthBuffer != dbCleared) {
		if (gDP.otherMode.cycleType == G_CYC_FILL) {
			f32 fillColor[4];
			gli_gDPGetFillColor(fillColor);
			gDP.rectColor.r = fillColor[0];
			gDP.rectColor.g = fillColor[1];
			gDP.rectColor.b = fillColor[2];
			gDP.rectColor.a = fillColor[3];
		} else {
			gdp_clear_rect_color();
		}
		gli_drawer_draw_rect(ulx, uly, lrx, lry);
	}

	if (gDP.otherMode.cycleType == G_CYC_FILL)
		gli_fb_fill_rdram(ulx, uly, lrx, lry);

	gli_fb_set_changed((f32)lry);

	gSP.tri_num += 2;
}

void gli_gDPSetConvert( s32 k0, s32 k1, s32 k2, s32 k3, s32 k4, s32 k5 )
{
/* angrylion's macro */
#define SIGN(x, numb)	(((x) & ((1 << numb) - 1)) | -((x) & (1 << (numb - 1))))

	gDP.convert.k0 = (SIGN(k0, 9) << 1) + 1;
	gDP.convert.k1 = (SIGN(k1, 9) << 1) + 1;
	gDP.convert.k2 = (SIGN(k2, 9) << 1) + 1;
	gDP.convert.k3 = (SIGN(k3, 9) << 1) + 1;
	gDP.convert.k4 = k4;
	gDP.convert.k5 = k5;

}

void gli_gDPSetKeyR( u32 cR, u32 sR, u32 wR )
{
	gDP.key.center.r = 	_FIXED2FLOATCOLOR( cR, 8 );
	gDP.key.scale.r = 	_FIXED2FLOATCOLOR( sR, 8 );
	gDP.key.width.r = 	_FIXED2FLOATCOLOR( wR, 8 );
}

void gli_gDPSetKeyGB(u32 cG, u32 sG, u32 wG, u32 cB, u32 sB, u32 wB )
{
	gDP.key.center.g = 	_FIXED2FLOATCOLOR( cG, 8 );
	gDP.key.scale.g = 	_FIXED2FLOATCOLOR( sG, 8 );
	gDP.key.width.g = 	_FIXED2FLOATCOLOR( wG, 8 );
	gDP.key.center.b = 	_FIXED2FLOATCOLOR( cB, 8 );
	gDP.key.scale.b = 	_FIXED2FLOATCOLOR( sB, 8 );
	gDP.key.width.b = 	_FIXED2FLOATCOLOR( wB, 8 );
}

void gli_gDPTextureRectangle(f32 ulx, f32 uly, f32 lrx, f32 lry, s32 tile, s16 s, s16 t, f32 dsdx, f32 dtdy , bool flip)
{
	gDPTile *textureTileOrg[2];
	if (gDP.otherMode.cycleType == G_CYC_COPY) {
		dsdx /= 4.0f;
		lrx += 1.0f;
		lry += 1.0f;
	}
	if (lry < uly + 1.0f)
		lry = uly + 1.0f;

	textureTileOrg[0] = gSP.textureTile[0];
	textureTileOrg[1] = gSP.textureTile[1];
	gSP.textureTile[0] = &gDP.tiles[tile];
	gSP.textureTile[1] = &gDP.tiles[(tile + 1) & 7];

	/* HACK ALERT! */
	if (s == 0x4000 && (gDP.colorImage.width + gSP.textureTile[0]->uls < 512))
		s = 0;

	gdp_clear_rect_color();
	if (gDP.otherMode.cycleType < G_CYC_COPY) {
		if ((config.generalEmulation.hacks & hack_texrect_shade_alpha) != 0 &&
			gDP.combine.mA0 == G_ACMUX_0 && gDP.combine.aA0 == G_ACMUX_SHADE)
			gDP.rectColor.a = 1.0f;
	}

	gli_drawer_textured_rect(ulx, uly, lrx, lry, dsdx, dtdy, s, t, flip);

	gSP.textureTile[0] = textureTileOrg[0];
	gSP.textureTile[1] = textureTileOrg[1];

	gli_fb_set_changed(lry);

	gSP.tri_num += 2;
}

void gli_gDPFullSync(void)
{
	if (RSP.stereoPass == RSP_STEREO_LEFT) {
		/* The right pass hands the task over to the CPU */
		gli_fb_stereo_sync();
		return;
	}

	gli_fb_full_sync(RSP.LLE);

	*REG.MI_INTR |= MI_INTR_DP;

	CheckInterrupts();

}

void gli_gDPTileSync(void)
{
}

void gli_gDPPipeSync(void)
{
}

void gli_gDPLoadSync(void)
{
}

void gli_gDPNoOp(void)
{
}

/*******************************************
 *          Low level triangle             *
 *******************************************
 *    based on sources of ziggy's z64      *
 *******************************************/

static void gDPLLETriangle(u32 _w1, u32 _w2, int _shade, int _texture, int _zbuffer, const u32 * _pRdpCmd)
{
	u32 tile;
	gDPTile *textureTileOrg[2];
	/* an RDP triangle is at most eight corners as a strip */
	gli_lle_corner corners[8];
	gli_lle_corner * corner;
	u32 numCorners = 0;
	gli_lle_mode mode;
	SPVertex * vtx;
	u32 numVtx;
	int list;
	int j;
	int xleft;
	int xright;
	int xleft_inc;
	int xright_inc;
	int r;
	int g;
	int b;
	int a;
	int z;
	int s;
	int t;
	int w;
	int drdx;
	int dgdx;
	int dbdx;
	int dadx;
	int dzdx;
	int dsdx;
	int dtdx;
	int dwdx;
	int drde;
	int dgde;
	int dbde;
	int dade;
	int dzde;
	int dsde;
	int dtde;
	int dwde;
	int flip;
	s32 yl;
	s32 ym;
	s32 yh;
	s32 xl;
	s32 xm;
	s32 xh;
	s32 dxldy;
	s32 dxhdy;
	s32 dxmdy;
	u32 w3;
	u32 w4;
	u32 w5;
	u32 w6;
	u32 w7;
	u32 w8;
	const u32 * shade_base;
	const u32 * texture_base;
	const u32 * zbuffer_base;
	gSP.texture.level = _SHIFTR(_w1, 19, 3);
	tile = _SHIFTR(_w1, 16, 3);
	textureTileOrg[0] = gSP.textureTile[0];
	textureTileOrg[1] = gSP.textureTile[1];
	gSP.textureTile[0] = &gDP.tiles[tile];
	gSP.textureTile[1] = &gDP.tiles[(tile + 1) & 7];

	drdx = 0;
	dgdx = 0;
	dbdx = 0;
	dadx = 0;
	dzdx = 0;
	dsdx = 0;
	dtdx = 0;
	dwdx = 0;
	drde = 0;
	dgde = 0;
	dbde = 0;
	dade = 0;
	dzde = 0;
	dsde = 0;
	dtde = 0;
	dwde = 0;
	flip = (_w1 & 0x800000) ? 1 : 0;


	shade_base = _pRdpCmd + 8;
	texture_base = _pRdpCmd + 8;
	zbuffer_base = _pRdpCmd + 8;

	if (_shade != 0) {
		texture_base += 16;
		zbuffer_base += 16;
	}
	if (_texture != 0) {
		zbuffer_base += 16;
	}

	w3 = _pRdpCmd[2];
	w4 = _pRdpCmd[3];
	w5 = _pRdpCmd[4];
	w6 = _pRdpCmd[5];
	w7 = _pRdpCmd[6];
	w8 = _pRdpCmd[7];

	yl = (_w1 & 0x3fff);
	ym = ((_w2 >> 16) & 0x3fff);
	yh = ((_w2 >>  0) & 0x3fff);
	xl = (s32)(w3);
	xh = (s32)(w5);
	xm = (s32)(w7);
	dxldy = (s32)(w4);
	dxhdy = (s32)(w6);
	dxmdy = (s32)(w8);

	if (yl & (0x800<<2)) yl |= 0xfffff000<<2;
	if (ym & (0x800<<2)) ym |= 0xfffff000<<2;
	if (yh & (0x800<<2)) yh |= 0xfffff000<<2;

	yh &= ~3;

	r = 0xff; g = 0xff; b = 0xff; a = 0xff; z = 0xffff0000; s = 0;  t = 0;  w = 0x30000;

	if (_shade != 0) {
		r    = (shade_base[0] & 0xffff0000) | ((shade_base[+4 ] >> 16) & 0x0000ffff);
		g    = ((shade_base[0 ] << 16) & 0xffff0000) | (shade_base[4 ] & 0x0000ffff);
		b    = (shade_base[1 ] & 0xffff0000) | ((shade_base[5 ] >> 16) & 0x0000ffff);
		a    = ((shade_base[1 ] << 16) & 0xffff0000) | (shade_base[5 ] & 0x0000ffff);
		drdx = (shade_base[2 ] & 0xffff0000) | ((shade_base[6 ] >> 16) & 0x0000ffff);
		dgdx = ((shade_base[2 ] << 16) & 0xffff0000) | (shade_base[6 ] & 0x0000ffff);
		dbdx = (shade_base[3 ] & 0xffff0000) | ((shade_base[7 ] >> 16) & 0x0000ffff);
		dadx = ((shade_base[3 ] << 16) & 0xffff0000) | (shade_base[7 ] & 0x0000ffff);
		drde = (shade_base[8 ] & 0xffff0000) | ((shade_base[12] >> 16) & 0x0000ffff);
		dgde = ((shade_base[8 ] << 16) & 0xffff0000) | (shade_base[12] & 0x0000ffff);
		dbde = (shade_base[9 ] & 0xffff0000) | ((shade_base[13] >> 16) & 0x0000ffff);
		dade = ((shade_base[9 ] << 16) & 0xffff0000) | (shade_base[13] & 0x0000ffff);
	}
	if (_texture != 0) {
		s    = (texture_base[0 ] & 0xffff0000) | ((texture_base[4 ] >> 16) & 0x0000ffff);
		t    = ((texture_base[0 ] << 16) & 0xffff0000)      | (texture_base[4 ] & 0x0000ffff);
		w    = (texture_base[1 ] & 0xffff0000) | ((texture_base[5 ] >> 16) & 0x0000ffff);
		/* w = abs(w); */
		dsdx = (texture_base[2 ] & 0xffff0000) | ((texture_base[6 ] >> 16) & 0x0000ffff);
		dtdx = ((texture_base[2 ] << 16) & 0xffff0000)      | (texture_base[6 ] & 0x0000ffff);
		dwdx = (texture_base[3 ] & 0xffff0000) | ((texture_base[7 ] >> 16) & 0x0000ffff);
		dsde = (texture_base[8 ] & 0xffff0000) | ((texture_base[12] >> 16) & 0x0000ffff);
		dtde = ((texture_base[8 ] << 16) & 0xffff0000)      | (texture_base[12] & 0x0000ffff);
		dwde = (texture_base[9 ] & 0xffff0000) | ((texture_base[13] >> 16) & 0x0000ffff);
	}
	if (_zbuffer != 0) {
		z    = zbuffer_base[0];
		dzdx = zbuffer_base[1];
		dzde = zbuffer_base[2];
	}

	xh <<= 2;  xm <<= 2;  xl <<= 2;
	r <<= 2;  g <<= 2;  b <<= 2;  a <<= 2;
	dsde >>= 2;  dtde >>= 2;  dsdx >>= 2;  dtdx >>= 2;
	dzdx >>= 2;  dzde >>= 2;
	dwdx >>= 2;  dwde >>= 2;

#define XSCALE(x) ((float)(x)/(1<<18))
#define YSCALE(y) ((float)(y)/(1<<2))
#define PERSP_EN (gDP.otherMode.texturePersp != 0)

	xleft = xm;
	xright = xh;
	xleft_inc = dxmdy;
	xright_inc = dxhdy;

	while (yh<ym &&
		!((!flip && xleft < xright+0x10000) ||
		 (flip && xleft > xright-0x10000))) {
		xleft += xleft_inc;
		xright += xright_inc;
		s += dsde;    t += dtde;    w += dwde;
		r += drde;    g += dgde;    b += dbde;    a += dade;
		z += dzde;
		yh++;
	}

	j = ym-yh;
	if (j > 0) {
		int dx = (xleft-xright)>>16;
		if ((!flip && xleft < xright) || (flip/* && xleft > xright*/))
		{
			corner = &corners[numCorners++];
			corner->r = r+drdx*dx;
			corner->g = g+dgdx*dx;
			corner->b = b+dbdx*dx;
			corner->a = a+dadx*dx;
			corner->s = s+dsdx*dx;
			corner->t = t+dtdx*dx;
			corner->w = w+dwdx*dx;
			corner->x = XSCALE(xleft);
			corner->y = YSCALE(yh);
			corner->z = (u32)(z+dzdx*dx);
		}
		if ((!flip/* && xleft < xright*/) || (/*flip &&*/ xleft > xright))
		{
			corner = &corners[numCorners++];
			corner->r = r;
			corner->g = g;
			corner->b = b;
			corner->a = a;
			corner->s = s;
			corner->t = t;
			corner->w = w;
			corner->x = XSCALE(xright);
			corner->y = YSCALE(yh);
			corner->z = (u32)(z);
		}
		xleft += xleft_inc*j;  xright += xright_inc*j;
		s += dsde*j;  t += dtde*j;
		if (w + dwde*j != 0) w += dwde*j;
		else w += dwde*(j-1);
		r += drde*j;  g += dgde*j;  b += dbde*j;  a += dade*j;
		z += dzde*j;
		/* render ... */
	}

	if (xl != xh)
		xleft = xl;

	/* if (yl-ym > 0) */
	{
		int dx = (xleft-xright)>>16;
		if ((!flip && xleft <= xright) ||
				(flip/* && xleft >= xright*/))
		{
			corner = &corners[numCorners++];
			corner->r = r+drdx*dx;
			corner->g = g+dgdx*dx;
			corner->b = b+dbdx*dx;
			corner->a = a+dadx*dx;
			corner->s = s+dsdx*dx;
			corner->t = t+dtdx*dx;
			corner->w = w+dwdx*dx;
			corner->x = XSCALE(xleft);
			corner->y = YSCALE(ym);
			corner->z = (u32)(z+dzdx*dx);
		}
		if ((!flip/* && xleft <= xright*/) ||
				(/*flip && */xleft >= xright))
		{
			corner = &corners[numCorners++];
			corner->r = r;
			corner->g = g;
			corner->b = b;
			corner->a = a;
			corner->s = s;
			corner->t = t;
			corner->w = w;
			corner->x = XSCALE(xright);
			corner->y = YSCALE(ym);
			corner->z = (u32)(z);
		}
	}
	xleft_inc = dxldy;
	xright_inc = dxhdy;

	j = yl-ym;
	/* j--; ? */
	xleft += xleft_inc*j;  xright += xright_inc*j;
	s += dsde*j;  t += dtde*j;  w += dwde*j;
	r += drde*j;  g += dgde*j;  b += dbde*j;  a += dade*j;
	z += dzde*j;

	while (yl>ym &&
		   !((!flip && xleft < xright+0x10000) ||
			 (flip && xleft > xright-0x10000))) {
		xleft -= xleft_inc;    xright -= xright_inc;
		s -= dsde;    t -= dtde;    w -= dwde;
		r -= drde;    g -= dgde;    b -= dbde;    a -= dade;
		z -= dzde;
		--j;
		--yl;
	}

	/* render ... */
	if (j >= 0) {
		int dx = (xleft-xright)>>16;
		if ((!flip && xleft <= xright) ||
				(flip/* && xleft >= xright*/))
		{
			corner = &corners[numCorners++];
			corner->r = r+drdx*dx;
			corner->g = g+dgdx*dx;
			corner->b = b+dbdx*dx;
			corner->a = a+dadx*dx;
			corner->s = s+dsdx*dx;
			corner->t = t+dtdx*dx;
			corner->w = w+dwdx*dx;
			corner->x = XSCALE(xleft);
			corner->y = YSCALE(yl);
			corner->z = (u32)(z+dzdx*dx);
		}
		if ((!flip/* && xleft <= xright*/) ||
				(/*flip &&*/ xleft >= xright))
		{
			corner = &corners[numCorners++];
			corner->r = r;
			corner->g = g;
			corner->b = b;
			corner->a = a;
			corner->s = s;
			corner->t = t;
			corner->w = w;
			corner->x = XSCALE(xright);
			corner->y = YSCALE(yl);
			corner->z = (u32)(z);
		}
	}

	if (_texture != 0)
		gDP.changed |= CHANGED_TILE;
	if (_zbuffer != 0)
		gSP.geometryMode |= G_ZBUFFER;

	mode.shade = _shade != 0;
	mode.texture = _texture != 0;
	mode.persp = PERSP_EN;
	mode.zprim = gDP.otherMode.depthSource == G_ZS_PRIM;
	mode.primZ = gDP.primDepth.z;
	vtx = gli_drawer_dma_vertices(GLI_LLE_MAX_VERTICES(8));
	numVtx = gli_lle_vertices(corners, numCorners, &mode, vtx, &list);
	if (list)
		gli_drawer_draw_screen_triangles(numVtx);
	else
		gli_drawer_draw_screen_strip(numVtx);
	gSP.textureTile[0] = textureTileOrg[0];
	gSP.textureTile[1] = textureTileOrg[1];

}

static void gDPTriangle(u32 _w1, u32 _w2, int shade, int texture, int zbuffer)
{
	gDPLLETriangle(_w1, _w2, shade, texture, zbuffer, RDP.cmd_lend);
}

void gli_gDPTriFill(u32 w0, u32 w1)
{
	gDPTriangle(w0, w1, 0, 0, 0);
}

void gli_gDPTriShade(u32 w0, u32 w1)
{
	gDPTriangle(w0, w1, 1, 0, 0);
}

void gli_gDPTriTxtr(u32 w0, u32 w1)
{
	gDPTriangle(w0, w1, 0, 1, 0);
}

void gli_gDPTriShadeTxtr(u32 w0, u32 w1)
{
	gDPTriangle(w0, w1, 1, 1, 0);
}

void gli_gDPTriFillZ(u32 w0, u32 w1)
{
	gDPTriangle(w0, w1, 0, 0, 1);
}

void gli_gDPTriShadeZ(u32 w0, u32 w1)
{
	gDPTriangle(w0, w1, 1, 0, 1);
}

void gli_gDPTriTxtrZ(u32 w0, u32 w1)
{
	gDPTriangle(w0, w1, 0, 1, 1);
}

void gli_gDPTriShadeTxtrZ(u32 w0, u32 w1)
{
	gDPTriangle(w0, w1, 1, 1, 1);
}
