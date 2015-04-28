#include <stdlib.h>
#include <math.h>

#include "Common.h"
#include "gles2N64.h"
#include "N64.h"
#include "GBI.h"
#include "RSP.h"
#include "gDP.h"
#include "gSP.h"
#include "Types.h"
#include "Debug.h"
#include "convert.h"
#include "OpenGL.h"
#include "CRC.h"
#include "DepthBuffer.h"
#include "VI.h"
#include "Config.h"

gDPInfo gDP;

void gDPSetOtherMode( u32 mode0, u32 mode1 )
{
   gDP.otherMode.h = mode0;
   gDP.otherMode.l = mode1;

   gDP.changed |= CHANGED_RENDERMODE | CHANGED_CYCLETYPE | CHANGED_ALPHACOMPARE;

#ifdef DEBUG
   DebugMsg( DEBUG_HIGH | DEBUG_HANDLED, "gDPSetOtherMode( %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s, %s | %s | %s%s%s%s%s | %s | %s%s%s );\n",
         AlphaDitherText[gDP.otherMode.alphaDither],
         ColorDitherText[gDP.otherMode.colorDither],
         CombineKeyText[gDP.otherMode.combineKey],
         TextureConvertText[gDP.otherMode.textureConvert],
         TextureFilterText[gDP.otherMode.textureFilter],
         TextureLUTText[gDP.otherMode.textureLUT],
         TextureLODText[gDP.otherMode.textureLOD],
         TextureDetailText[gDP.otherMode.textureDetail],
         TexturePerspText[gDP.otherMode.texturePersp],
         CycleTypeText[gDP.otherMode.cycleType],
         PipelineModeText[gDP.otherMode.pipelineMode],
         AlphaCompareText[gDP.otherMode.alphaCompare],
         DepthSourceText[gDP.otherMode.depthSource],
         gDP.otherMode.AAEnable ? "AA_EN | " : "",
         gDP.otherMode.depthCompare ? "Z_CMP | " : "",
         gDP.otherMode.depthUpdate ? "Z_UPD | " : "",
         gDP.otherMode.imageRead ? "IM_RD | " : "",
         CvgDestText[gDP.otherMode.cvgDest],
         DepthModeText[gDP.otherMode.depthMode],
         gDP.otherMode.cvgXAlpha ? "CVG_X_ALPHA | " : "",
         gDP.otherMode.alphaCvgSel ? "ALPHA_CVG_SEL | " : "",
         gDP.otherMode.forceBlender ? "FORCE_BL" : "" );
#endif
}

void gDPSetPrimDepth( u16 z, u16 dz )
{
	if (gSP.viewport.vscale[2] == 0)
		gDP.primDepth.z = _FIXED2FLOAT(_SHIFTR(z, 0, 15), 15);
	else
		gDP.primDepth.z = min(1.0f, max(-1.0f, (_FIXED2FLOAT(_SHIFTR(z, 0, 15), 15) - gSP.viewport.vtrans[2]) / gSP.viewport.vscale[2]));
	gDP.primDepth.deltaZ = _FIXED2FLOAT(_SHIFTR(dz, 0, 15), 15);

#ifdef DEBUG
   DebugMsg( DEBUG_HIGH | DEBUG_HANDLED, "gDPSetPrimDepth( %f, %f );\n",
         gDP.primDepth.z,
         gDP.primDepth.deltaZ);
#endif
}

void gDPPipelineMode( u32 mode )
{
   gDP.otherMode.pipelineMode = mode;

#ifdef DEBUG
   DebugMsg( DEBUG_HIGH | DEBUG_HANDLED, "gDPPipelineMode( %s );\n",
         PipelineModeText[gDP.otherMode.pipelineMode] );
#endif
}

void gDPSetCycleType( u32 type )
{
   gDP.otherMode.cycleType = type;
   gDP.changed |= CHANGED_CYCLETYPE;

#ifdef DEBUG
   DebugMsg( DEBUG_HIGH | DEBUG_HANDLED, "gDPSetCycleType( %s );\n",
         CycleTypeText[gDP.otherMode.cycleType] );
#endif
}

void gDPSetTexturePersp( u32 enable )
{
   gDP.otherMode.texturePersp = enable & 1;

#ifdef DEBUG
   DebugMsg( DEBUG_HIGH | DEBUG_HANDLED | DEBUG_TEXTURE, "gDPSetTexturePersp( %s );\n",
         TexturePerspText[gDP.otherMode.texturePersp] );
#endif
}

void gDPSetTextureDetail( u32 type )
{
   gDP.otherMode.textureDetail = type;

#ifdef DEBUG
   DebugMsg( DEBUG_HIGH | DEBUG_HANDLED | DEBUG_TEXTURE, "gDPSetTextureDetail( %s );\n",
         TextureDetailText[gDP.otherMode.textureDetail] );
#endif
}

void gDPSetTextureLOD( u32 mode )
{
   gDP.otherMode.textureLOD = mode;

#ifdef DEBUG
   DebugMsg( DEBUG_HIGH | DEBUG_HANDLED | DEBUG_TEXTURE, "gDPSetTextureLOD( %s );\n",
         TextureLODText[gDP.otherMode.textureLOD] );
#endif
}

void gDPSetTextureLUT( u32 mode )
{
   gDP.otherMode.textureLUT = mode & 3;

#ifdef DEBUG
   DebugMsg( DEBUG_HIGH | DEBUG_HANDLED | DEBUG_TEXTURE, "gDPSetTextureLUT( %s );\n",
         TextureLUTText[gDP.otherMode.textureLUT] );
#endif
}

void gDPSetTextureFilter( u32 type )
{
   gDP.otherMode.textureFilter = type;

#ifdef DEBUG
   DebugMsg( DEBUG_HIGH | DEBUG_HANDLED | DEBUG_TEXTURE, "gDPSetTextureFilter( %s );\n",
         TextureFilterText[gDP.otherMode.textureFilter] );
#endif
}

void gDPSetTextureConvert( u32 type )
{
   gDP.otherMode.textureConvert = type;

#ifdef DEBUG
   DebugMsg( DEBUG_HIGH | DEBUG_HANDLED | DEBUG_TEXTURE, "gDPSetTextureConvert( %s );\n",
         TextureConvertText[gDP.otherMode.textureConvert] );
#endif
}

void gDPSetCombineKey( u32 type )
{
   gDP.otherMode.combineKey = type;

#ifdef DEBUG
   DebugMsg( DEBUG_HIGH | DEBUG_HANDLED | DEBUG_COMBINE, "gDPSetCombineKey( %s );\n",
         CombineKeyText[gDP.otherMode.combineKey] );
#endif
}

void gDPSetColorDither( u32 type )
{
   gDP.otherMode.colorDither = type;

#ifdef DEBUG
   DebugMsg( DEBUG_HIGH | DEBUG_HANDLED, "gDPSetColorDither( %s );\n",
         ColorDitherText[gDP.otherMode.colorDither] );
#endif
}

void gDPSetAlphaDither( u32 type )
{
   gDP.otherMode.alphaDither = type;

#ifdef DEBUG
   DebugMsg( DEBUG_HIGH | DEBUG_HANDLED, "gDPSetAlphaDither( %s );\n",
         AlphaDitherText[gDP.otherMode.alphaDither] );
#endif
}

void gDPSetAlphaCompare( u32 mode )
{
   gDP.otherMode.alphaCompare = mode;
   gDP.changed |= CHANGED_ALPHACOMPARE;

#ifdef DEBUG
   DebugMsg( DEBUG_HIGH | DEBUG_HANDLED, "gDPSetAlphaCompare( %s );\n",
         AlphaCompareText[gDP.otherMode.alphaCompare] );
#endif
}

void gDPSetDepthSource( u32 source )
{
   gDP.otherMode.depthSource = source;
   gDP.changed |= CHANGED_DEPTHSOURCE;

#ifdef DEBUG
   DebugMsg( DEBUG_HIGH | DEBUG_HANDLED, "gDPSetDepthSource( %s );\n",
         DepthSourceText[gDP.otherMode.depthSource] );
#endif
}

void gDPSetRenderMode( u32 mode1, u32 mode2 )
{
   gDP.otherMode.l &= 0x00000007;
   gDP.otherMode.l |= mode1 | mode2;
   gDP.changed |= CHANGED_RENDERMODE;

#ifdef DEBUG
   // THIS IS INCOMPLETE!!!
   DebugMsg( DEBUG_HIGH | DEBUG_HANDLED, "gDPSetRenderMode( %s%s%s%s%s | %s | %s%s%s );\n",
         gDP.otherMode.AAEnable ? "AA_EN | " : "",
         gDP.otherMode.depthCompare ? "Z_CMP | " : "",
         gDP.otherMode.depthUpdate ? "Z_UPD | " : "",
         gDP.otherMode.imageRead ? "IM_RD | " : "",
         CvgDestText[gDP.otherMode.cvgDest],
         DepthModeText[gDP.otherMode.depthMode],
         gDP.otherMode.cvgXAlpha ? "CVG_X_ALPHA | " : "",
         gDP.otherMode.alphaCvgSel ? "ALPHA_CVG_SEL | " : "",
         gDP.otherMode.forceBlender ? "FORCE_BL" : "" );
#endif
}

void gDPSetCombine( s32 muxs0, s32 muxs1 )
{
   gDP.combine.muxs0 = muxs0;
   gDP.combine.muxs1 = muxs1;

   gDP.changed |= CHANGED_COMBINE;

#ifdef DEBUG
   DebugMsg( DEBUG_HIGH | DEBUG_HANDLED | DEBUG_COMBINE, "gDPSetCombine( %s, %s, %s, %s, %s, %s, %s, %s,\n",
         saRGBText[gDP.combine.saRGB0],
         sbRGBText[gDP.combine.sbRGB0],
         mRGBText[gDP.combine.mRGB0],
         aRGBText[gDP.combine.aRGB0],
         saAText[gDP.combine.saA0],
         sbAText[gDP.combine.sbA0],
         mAText[gDP.combine.mA0],
         aAText[gDP.combine.aA0] );

   DebugMsg( DEBUG_HIGH | DEBUG_HANDLED | DEBUG_COMBINE, "               %s, %s, %s, %s, %s, %s, %s, %s );\n",
         saRGBText[gDP.combine.saRGB1],
         sbRGBText[gDP.combine.sbRGB1],
         mRGBText[gDP.combine.mRGB1],
         aRGBText[gDP.combine.aRGB1],
         saAText[gDP.combine.saA1],
         sbAText[gDP.combine.sbA1],
         mAText[gDP.combine.mA1],
         aAText[gDP.combine.aA1] );

#endif
}

/* FIXME/TODO - needs to be updated */
void gDPSetColorImage( u32 format, u32 size, u32 width, u32 address )
{
   u32 addr = RSP_SegmentToPhysical( address );

	if (gDP.colorImage.address != address || gDP.colorImage.width != width || gDP.colorImage.size != size)
   {
		u32 height = 1;

      gDP.colorImage.changed = FALSE;

		if (width == VI.width)
			height = VI.height;
		else if (width == gDP.scissor.lrx && width == gSP.viewport.width)
      {
			height = max(gDP.scissor.lry, gSP.viewport.height);
			height = min(height, VI.height);
		} else if (width == gDP.scissor.lrx)
			height = gDP.scissor.lry;
		else if (width <= 64)
			height = width;
		else if (gSP.viewport.height > 0)
			height = gSP.viewport.height;
		else
			height = gDP.scissor.lry;

      gDP.colorImage.height = height;
   }

   gDP.colorImage.format  = format;
   gDP.colorImage.size    = size;
   gDP.colorImage.width   = width;
   gDP.colorImage.address = addr;

   OGL.renderingToTexture = false;

#ifdef DEBUG
   DebugMsg( DEBUG_HIGH | DEBUG_HANDLED, "gDPSetColorImage( %s, %s, %i, 0x%08X );\n",
         ImageFormatText[gDP.colorImage.format],
         ImageSizeText[gDP.colorImage.size],
         gDP.colorImage.width,
         gDP.colorImage.address );
#endif
}

void gDPSetTextureImage( u32 format, u32 size, u32 width, u32 address )
{
   gDP.textureImage.format  = format;
   gDP.textureImage.size    = size;
   gDP.textureImage.width   = width;
   gDP.textureImage.address = RSP_SegmentToPhysical( address );
   gDP.textureImage.bpl     = gDP.textureImage.width << gDP.textureImage.size >> 1;

	if (gSP.DMAOffsets.tex_offset != 0)
   {
      if (format == G_IM_FMT_RGBA)
      {
         u16 * t = (u16*)(gfx_info.RDRAM + gSP.DMAOffsets.tex_offset);
         gSP.DMAOffsets.tex_shift = t[gSP.DMAOffsets.tex_count ^ 1];
         gDP.textureImage.address += gSP.DMAOffsets.tex_shift;
      }
      else
      {
         gSP.DMAOffsets.tex_offset = 0;
         gSP.DMAOffsets.tex_shift = 0;
         gSP.DMAOffsets.tex_count = 0;
      }
   }

#ifdef DEBUG
   DebugMsg( DEBUG_HIGH | DEBUG_HANDLED | DEBUG_TEXTURE, "gDPSetTextureImage( %s, %s, %i, 0x%08X );\n",
         ImageFormatText[gDP.textureImage.format],
         ImageSizeText[gDP.textureImage.size],
         gDP.textureImage.width,
         gDP.textureImage.address );
#endif
}

/* TODO/FIXME - update */
void gDPSetDepthImage( u32 address )
{
   address = RSP_SegmentToPhysical(address);
   gDP.depthImageAddress = address;
#if 0
	depthBufferList().saveBuffer(address);
#else
   DepthBuffer_SetBuffer(address);

   if (depthBuffer.current->cleared)
      OGL_ClearDepthBuffer();
#endif


#ifdef DEBUG
   DebugMsg( DEBUG_HIGH | DEBUG_HANDLED, "gDPSetDepthImage( 0x%08X );\n", gDP.depthImageAddress );
#endif
}

/* TODO/FIXME - update */
void gDPSetEnvColor( u32 r, u32 g, u32 b, u32 a )
{
   gDP.envColor.r = r * 0.0039215689f;
   gDP.envColor.g = g * 0.0039215689f;
   gDP.envColor.b = b * 0.0039215689f;
   gDP.envColor.a = a * 0.0039215689f;

#if 0
	CombinerInfo::get().updateEnvColor();
#else
   gDP.changed |= CHANGED_ENV_COLOR;
#endif

#ifdef DEBUG
   DebugMsg( DEBUG_HIGH | DEBUG_HANDLED | DEBUG_COMBINE, "gDPSetEnvColor( %u, %u, %u, %u );\n",
         r, g, b, a );
#endif
}

/* TODO/FIXME - update */
void gDPSetBlendColor( u32 r, u32 g, u32 b, u32 a )
{
   gDP.blendColor.r = r * 0.0039215689f;
   gDP.blendColor.g = g * 0.0039215689f;
   gDP.blendColor.b = b * 0.0039215689f;
   gDP.blendColor.a = a * 0.0039215689f;

#if 0
	CombinerInfo::get().updateBlendColor();
#endif

   gDP.changed |= CHANGED_BLENDCOLOR;

#ifdef DEBUG
   DebugMsg( DEBUG_HIGH | DEBUG_HANDLED, "gDPSetBlendColor( %u, %u, %u, %u );\n",
         r, g, b, a );
#endif
}

void gDPSetFogColor( u32 r, u32 g, u32 b, u32 a )
{
   gDP.fogColor.r = r * 0.0039215689f;
   gDP.fogColor.g = g * 0.0039215689f;
   gDP.fogColor.b = b * 0.0039215689f;
   gDP.fogColor.a = a * 0.0039215689f;

#if 0
	CombinerInfo::get().updateFogColor();
#endif

   gDP.changed |= CHANGED_FOGCOLOR;

#ifdef DEBUG
   DebugMsg( DEBUG_HIGH | DEBUG_HANDLED, "gDPSetFogColor( %u, %u, %u, %u );\n",
         r, g, b, a );
#endif
}

void gDPSetFillColor( u32 c )
{
	gDP.fillColor.color = c;
   gDP.fillColor.z     = (f32)_SHIFTR( c,  2, 14 );
   gDP.fillColor.dz    = (f32)_SHIFTR( c,  0,  2 );

#ifdef DEBUG
   DebugMsg( DEBUG_HIGH | DEBUG_HANDLED, "gDPSetFillColor( 0x%08X );\n", c );
#endif
}

void gDPGetFillColor(f32 _fillColor[4])
{
	const u32 c = gDP.fillColor.color;

	if (gDP.colorImage.size < 3) {
		_fillColor[0] = _SHIFTR( c, 11, 5 ) * 0.032258064f;
		_fillColor[1] = _SHIFTR( c,  6, 5 ) * 0.032258064f;
		_fillColor[2] = _SHIFTR( c,  1, 5 ) * 0.032258064f;
		_fillColor[3] = (f32)_SHIFTR( c,  0, 1 );
	} else {
		_fillColor[0] = _SHIFTR( c, 24, 8 ) * 0.0039215686f;
		_fillColor[1] = _SHIFTR( c, 16, 8 ) * 0.0039215686f;
		_fillColor[2] = _SHIFTR( c,  8, 8 ) * 0.0039215686f;
		_fillColor[3] = _SHIFTR( c,  0, 8 ) * 0.0039215686f;
	}
}

/* TODO/FIXME - update */
void gDPSetPrimColor( u32 m, u32 l, u32 r, u32 g, u32 b, u32 a )
{
	gDP.primColor.m = m * 0.0039215689f;
   gDP.primColor.l = l * 0.0039215689f;
   gDP.primColor.r = r * 0.0039215689f;
   gDP.primColor.g = g * 0.0039215689f;
   gDP.primColor.b = b * 0.0039215689f;
   gDP.primColor.a = a * 0.0039215689f;

#if 0
	CombinerInfo::get().updatePrimColor();
#else
   gDP.changed |= CHANGED_PRIM_COLOR;
#endif

#ifdef DEBUG
   DebugMsg( DEBUG_HIGH | DEBUG_HANDLED | DEBUG_COMBINE, "gDPSetPrimColor( %u, %u, %u, %u, %u, %u );\n",
         m, l, r, g, b, a );
#endif
}

void gDPSetTile( u32 format, u32 size, u32 line, u32 tmem, u32 tile, u32 palette, u32 cmt, u32 cms, u32 maskt, u32 masks, u32 shiftt, u32 shifts )
{
   gDP.tiles[tile].format = format;
   gDP.tiles[tile].size = size;
   gDP.tiles[tile].line = line;
   gDP.tiles[tile].tmem = tmem;
   gDP.tiles[tile].palette = palette;
   gDP.tiles[tile].cmt = cmt;
   gDP.tiles[tile].cms = cms;
   gDP.tiles[tile].maskt = maskt;
   gDP.tiles[tile].masks = masks;
   gDP.tiles[tile].shiftt = shiftt;
   gDP.tiles[tile].shifts = shifts;

   if (!gDP.tiles[tile].masks) gDP.tiles[tile].clamps = 1;
   if (!gDP.tiles[tile].maskt) gDP.tiles[tile].clampt = 1;

   if (tile == gSP.texture.tile || tile == gSP.texture.tile + 1)
   {
      u32 nTile = 7;
      while(gDP.tiles[nTile].tmem != tmem && nTile > gSP.texture.tile + 1)
         --nTile;
      if (nTile > gSP.texture.tile + 1)
      {
         gDP.tiles[tile].textureMode = gDP.tiles[nTile].textureMode;
         gDP.tiles[tile].loadType = gDP.tiles[nTile].loadType;
#ifdef NEW
         gDP.tiles[tile].frameBuffer = gDP.tiles[nTile].frameBuffer;
#endif
         gDP.tiles[tile].imageAddress = gDP.tiles[nTile].imageAddress;
      }
   }

#ifdef DEBUG
   DebugMsg( DEBUG_HIGH | DEBUG_HANDLED | DEBUG_TEXTURE, "gDPSetTile( %s, %s, %i, %i, %i, %i, %s%s, %s%s, %i, %i, %i, %i );\n",
         ImageFormatText[format],
         ImageSizeText[size],
         line,
         tmem,
         tile,
         palette,
         cmt & G_TX_MIRROR ? "G_TX_MIRROR" : "G_TX_NOMIRROR",
         cmt & G_TX_CLAMP ? " | G_TX_CLAMP" : "",
         cms & G_TX_MIRROR ? "G_TX_MIRROR" : "G_TX_NOMIRROR",
         cms & G_TX_CLAMP ? " | G_TX_CLAMP" : "",
         maskt,
         masks,
         shiftt,
         shifts );
#endif
}

void gDPSetTileSize( u32 tile, u32 uls, u32 ult, u32 lrs, u32 lrt )
{
   gDP.tiles[tile].uls = _SHIFTR( uls, 2, 10 );
   gDP.tiles[tile].ult = _SHIFTR( ult, 2, 10 );
   gDP.tiles[tile].lrs = _SHIFTR( lrs, 2, 10 );
   gDP.tiles[tile].lrt = _SHIFTR( lrt, 2, 10 );

   gDP.tiles[tile].fuls = _FIXED2FLOAT( uls, 2 );
   gDP.tiles[tile].fult = _FIXED2FLOAT( ult, 2 );
   gDP.tiles[tile].flrs = _FIXED2FLOAT( lrs, 2 );
   gDP.tiles[tile].flrt = _FIXED2FLOAT( lrt, 2 );

   gDP.changed |= CHANGED_TILE;

#ifdef DEBUG
   DebugMsg( DEBUG_HIGH | DEBUG_HANDLED | DEBUG_TEXTURE, "gDPSetTileSize( %u, %.2f, %.2f, %.2f, %.2f );\n",
         tile,
         gDP.tiles[tile].fuls,
         gDP.tiles[tile].fult,
         gDP.tiles[tile].flrs,
         gDP.tiles[tile].flrt );
#endif
}

#if 0
static
bool CheckForFrameBufferTexture(u32 _address, u32 _bytes)
{
	gDP.loadTile->textureMode = TEXTUREMODE_NORMAL;
	gDP.loadTile->frameBuffer = NULL;
	gDP.changed |= CHANGED_TMEM;
	if (!config.frameBufferEmulation.enable)
		return false;

	FrameBuffer *pBuffer = frameBufferList().findBuffer(_address);
	bool bRes = pBuffer != NULL;
	if (bRes) {
		if ((config.generalEmulation.hacks & hack_blurPauseScreen) != 0) {
			if (gDP.colorImage.address == gDP.depthImageAddress && pBuffer->m_RdramCrc != 0) {
				memcpy(RDRAM + gDP.depthImageAddress, RDRAM + pBuffer->m_startAddress, (pBuffer->m_width*pBuffer->m_height) << pBuffer->m_size >> 1);
				pBuffer->m_RdramCrc = 0;
				frameBufferList().getCurrent()->m_isPauseScreen = true;
			}
			if (pBuffer->m_isPauseScreen)
				bRes = false;
		}

		if (pBuffer->m_cfb) {
			frameBufferList().removeBuffer(pBuffer->m_startAddress);
			bRes = false;
		}

		if ((config.generalEmulation.hacks & hack_noDepthFrameBuffers) != 0 && pBuffer->m_isDepthBuffer) {
			frameBufferList().removeBuffer(pBuffer->m_startAddress);
			bRes = false;
		}

		const u32 texEndAddress = _address + _bytes - 1;
		if (_address > pBuffer->m_startAddress && texEndAddress > (pBuffer->m_endAddress + (pBuffer->m_width << pBuffer->m_size >> 1))) {
			//frameBufferList().removeBuffer(pBuffer->m_startAddress);
			bRes = false;
		}

		if (bRes && gDP.loadTile->loadType == LOADTYPE_TILE && gDP.textureImage.width != pBuffer->m_width && gDP.textureImage.size != pBuffer->m_size) {
			//frameBufferList().removeBuffer(pBuffer->m_startAddress); // Does not work with Zelda MM
			bRes = false;
		}

		if (bRes && pBuffer->m_validityChecked != RSP.DList) {
			if (pBuffer->m_cleared) {
				const u32 color = pBuffer->m_fillcolor & 0xFFFEFFFE;
				u32 wrongPixels = 0;
				for (u32 i = pBuffer->m_startAddress + 4; i < pBuffer->m_endAddress; i += 4) {
					if (((*(u32*)&RDRAM[i]) & 0xFFFEFFFE) != color)
						++wrongPixels;
				}
				bRes = wrongPixels < (pBuffer->m_endAddress - pBuffer->m_startAddress)/100; // treshold level 1%
				if (bRes)
					pBuffer->m_validityChecked = RSP.DList;
				else
					frameBufferList().removeBuffer(pBuffer->m_startAddress);
			} else if (pBuffer->m_RdramCrc != 0) {
				const u32 crc = textureCRC(RDRAM + pBuffer->m_startAddress, pBuffer->m_height, pBuffer->m_width << pBuffer->m_size >> 1);
				bRes = (pBuffer->m_RdramCrc == crc);
				if (bRes)
					pBuffer->m_validityChecked = RSP.DList;
				else
					frameBufferList().removeBuffer(pBuffer->m_startAddress);
			}
		}

		if (bRes) {
			pBuffer->m_pLoadTile = gDP.loadTile;
			gDP.loadTile->frameBuffer = pBuffer;
			gDP.loadTile->textureMode = TEXTUREMODE_FRAMEBUFFER;
		}
	}

	for (int nTile = gSP.texture.tile; nTile < 6; ++nTile) {
		if (gDP.tiles[nTile].tmem == gDP.loadTile->tmem) {
			gDPTile & curTile = gDP.tiles[nTile];
			curTile.textureMode = gDP.loadTile->textureMode;
			curTile.loadType = gDP.loadTile->loadType;
			curTile.frameBuffer = gDP.loadTile->frameBuffer;
			curTile.imageAddress = gDP.loadTile->imageAddress;
		}
	}
	return bRes;
}
#endif

void gDPLoadTile( u32 tile, u32 uls, u32 ult, u32 lrs, u32 lrt )
{
   void (*Interleave)( void *mem, u32 numDWords );

   u32 address, height, bpl, line, y;
   u64 *dest;
   u8 *src;

   gDPSetTileSize( tile, uls, ult, lrs, lrt );
   gDP.loadTile = &gDP.tiles[tile];

   if (gDP.loadTile->line == 0)
      return;

   address = gDP.textureImage.address + gDP.loadTile->ult * gDP.textureImage.bpl + (gDP.loadTile->uls << gDP.textureImage.size >> 1);
   dest = &TMEM[gDP.loadTile->tmem];
   bpl = (gDP.loadTile->lrs - gDP.loadTile->uls + 1) << gDP.loadTile->size >> 1;
   height = gDP.loadTile->lrt - gDP.loadTile->ult + 1;
   src = &gfx_info.RDRAM[address];

   if (((address + height * bpl) > RDRAMSize) ||
         (((gDP.loadTile->tmem << 3) + bpl * height) > 4096)) // Stay within TMEM
   {
#ifdef DEBUG
      DebugMsg( DEBUG_HIGH | DEBUG_ERROR | DEBUG_TEXTURE, "// Attempting to load texture tile out of range\n" );
      DebugMsg( DEBUG_HIGH | DEBUG_HANDLED | DEBUG_TEXTURE, "gDPLoadTile( %u, %i, %i, %i, %i );\n",
            tile, gDP.loadTile->uls, gDP.loadTile->ult, gDP.loadTile->lrs, gDP.loadTile->lrt );
#endif
      return;
   }

   // Line given for 32-bit is half what it seems it should since they split the
   // high and low words. I'm cheating by putting them together.
   if (gDP.loadTile->size == G_IM_SIZ_32b)
   {
      line = gDP.loadTile->line << 1;
      Interleave = QWordInterleave;
   }
   else
   {
      line = gDP.loadTile->line;
      Interleave = DWordInterleave;
   }

   for (y = 0; y < height; y++)
   {
      UnswapCopy( src, dest, bpl );
      if (y & 1) Interleave( dest, line );

      src += gDP.textureImage.bpl;
      dest += line;
   }

   gDP.loadTile->textureMode = TEXTUREMODE_NORMAL;
   gDP.loadTile->loadType = LOADTYPE_TILE;
   gDP.changed |= CHANGED_TMEM;

#ifdef DEBUG
   DebugMsg( DEBUG_HIGH | DEBUG_HANDLED | DEBUG_TEXTURE, "gDPLoadTile( %u, %i, %i, %i, %i );\n",
         tile, gDP.loadTile->uls, gDP.loadTile->ult, gDP.loadTile->lrs, gDP.loadTile->lrt );
#endif
}

void gDPLoadBlock( u32 tile, u32 uls, u32 ult, u32 lrs, u32 dxt )
{
   u64 *src, *dest;
   u32 bytes, address;
   unsigned y;
   gDPSetTileSize( tile, uls, ult, lrs, dxt );
   gDP.loadTile = &gDP.tiles[tile];

   bytes = (lrs + 1) << gDP.loadTile->size >> 1;
   address = gDP.textureImage.address + ult * gDP.textureImage.bpl + (uls << gDP.textureImage.size >> 1);

   if ((bytes == 0) ||
         ((address + bytes) > RDRAMSize) ||
         (((gDP.loadTile->tmem << 3) + bytes) > 4096))
   {
#ifdef DEBUG
      DebugMsg( DEBUG_HIGH | DEBUG_ERROR | DEBUG_TEXTURE, "// Attempting to load texture block out of range\n" );
      DebugMsg( DEBUG_HIGH | DEBUG_HANDLED | DEBUG_TEXTURE, "gDPLoadBlock( %u, %u, %u, %u, %u );\n",
            tile, uls, ult, lrs, dxt );
#endif
      //      bytes = min( bytes, min( RDRAMSize - gDP.textureImage.address, 4096 - (gDP.loadTile->tmem << 3) ) );
      return;
   }

   src = (u64*)&gfx_info.RDRAM[address];
   dest = (u64*)&TMEM[gDP.loadTile->tmem];

   if (dxt > 0)
   {
      u32 line = (2047 + dxt) / dxt;
      u32 bpl = line << 3;
      u32 height = bytes / bpl;

      if (gDP.loadTile->size == G_IM_SIZ_32b)
      {
         for (y = 0; y < height; y++)
         {
            UnswapCopy( src, dest, bpl );
            if (y & 1) QWordInterleave( dest, line );
            src += line;
            dest += line;
         }
      }
      else
      {
         for (y = 0; y < height; y++)
         {
            UnswapCopy( src, dest, bpl );
            if (y & 1) DWordInterleave( dest, line );
            src += line;
            dest += line;
         }

      }

   }
   else
      UnswapCopy( src, dest, bytes );

   gDP.loadTile->textureMode = TEXTUREMODE_NORMAL;
   gDP.loadTile->loadType = LOADTYPE_BLOCK;
   gDP.changed |= CHANGED_TMEM;

#ifdef DEBUG
   DebugMsg( DEBUG_HIGH | DEBUG_HANDLED | DEBUG_TEXTURE, "gDPLoadBlock( %u, %u, %u, %u, %u );\n",
         tile, uls, ult, lrs, dxt );
#endif
}

void gDPLoadTLUT( u32 tile, u32 uls, u32 ult, u32 lrs, u32 lrt )
{
   u16 j, count, *dest, *src, pal;
   u32 address;
   int i;
   gDPSetTileSize( tile, uls, ult, lrs, lrt );

   count = (gDP.tiles[tile].lrs - gDP.tiles[tile].uls + 1) * (gDP.tiles[tile].lrt - gDP.tiles[tile].ult + 1);
   address = gDP.textureImage.address + gDP.tiles[tile].ult * gDP.textureImage.bpl + (gDP.tiles[tile].uls << gDP.textureImage.size >> 1);

   dest = (u16*)&TMEM[gDP.tiles[tile].tmem];
   src = (u16*)&gfx_info.RDRAM[address];

   pal = (gDP.tiles[tile].tmem - 256) >> 4;

   i = 0;
   while (i < count)
   {
      for (j = 0; (j < 16) && (i < count); j++, i++)
      {
         u16 color = swapword( src[i^1] );

         *dest = color;
         //dest[1] = color;
         //dest[2] = color;
         //dest[3] = color;

         dest += 4;
      }

      gDP.paletteCRC16[pal] = Hash_CalculatePalette(&TMEM[256 + (pal << 4)], 16 );
      pal++;
   }

   gDP.paletteCRC256 = Hash_Calculate( 0xFFFFFFFF, gDP.paletteCRC16, 64 );

   gDP.changed |= CHANGED_TMEM;

#ifdef DEBUG
   DebugMsg( DEBUG_HIGH | DEBUG_HANDLED | DEBUG_TEXTURE, "gDPLoadTLUT( %u, %i, %i, %i, %i );\n",
         tile, gDP.tiles[tile].uls, gDP.tiles[tile].ult, gDP.tiles[tile].lrs, gDP.tiles[tile].lrt );
#endif
}

void gDPSetScissor( u32 mode, f32 ulx, f32 uly, f32 lrx, f32 lry )
{
   gDP.scissor.mode = mode;
   gDP.scissor.ulx = ulx;
   gDP.scissor.uly = uly;
   gDP.scissor.lrx = lrx;
   gDP.scissor.lry = lry;
   gDP.changed |= CHANGED_SCISSOR;

   /* TODO/FIXME - update */
#if 0
	frameBufferList().correctHeight();
#endif

#ifdef DEBUG
   DebugMsg( DEBUG_HIGH | DEBUG_IGNORED, "gDPSetScissor( %s, %.2f, %.2f, %.2f, %.2f );\n",
         ScissorModeText[gDP.scissor.mode],
         gDP.scissor.ulx,
         gDP.scissor.uly,
         gDP.scissor.lrx,
         gDP.scissor.lry );
#endif
}

void gDPFillRDRAM(u32 address, s32 ulx, s32 uly, s32 lrx, s32 lry, u32 width, u32 size, u32 color, bool scissor)
{
   /* stub right now */
}

void gDPFillRectangle( s32 ulx, s32 uly, s32 lrx, s32 lry )
{
   f32 fillColor[4];
   DepthBuffer *buffer = NULL;
   
   if (gDP.otherMode.cycleType == G_CYC_FILL)
   {
      ++lrx;
      ++lry;
   }
   else if (lry == uly)
      ++lry;

   buffer = (DepthBuffer*)DepthBuffer_FindBuffer( gDP.colorImage.address );
   if (buffer)
      buffer->cleared = TRUE;

   if (gDP.depthImageAddress == gDP.colorImage.address)
   {
      gDPFillRDRAM(gDP.colorImage.address, ulx, uly, lrx, lry,
            gDP.colorImage.width, gDP.colorImage.size, gDP.fillColor.color, true);
      OGL_ClearDepthBuffer();
      return;
   }

   gDPGetFillColor(fillColor);

   if (gDP.otherMode.cycleType == G_CYC_FILL)
   {
      if ((ulx == 0) && (uly == 0) && (lrx == gDP.scissor.lrx) && (lry == gDP.scissor.lry))
      {
			gDPFillRDRAM(gDP.colorImage.address, ulx, uly, lrx, lry,
               gDP.colorImage.width, gDP.colorImage.size, gDP.fillColor.color, true);

			if ((*gfx_info.VI_STATUS_REG & 8) != 0)
         {
				fillColor[0] = sqrtf(fillColor[0]);
				fillColor[1] = sqrtf(fillColor[1]);
				fillColor[2] = sqrtf(fillColor[2]);
			}
         OGL_ClearColorBuffer(&fillColor[0]);
         return;
      }
   }

   OGL_DrawRect( ulx, uly, lrx, lry, fillColor);
   if (depthBuffer.current)
      depthBuffer.current->cleared = FALSE;
   gDP.colorImage.changed = TRUE;

   if (gDP.otherMode.cycleType == G_CYC_FILL)
   {
      if (lry > (u32)gDP.scissor.lry)
         gDP.colorImage.height = (u32)max( gDP.colorImage.height, (unsigned int)gDP.scissor.lry );
      else
         gDP.colorImage.height = (u32)max((s32)gDP.colorImage.height, lry);
   }
   else
      gDP.colorImage.height = max( gDP.colorImage.height, (unsigned int)lry );

#ifdef DEBUG
   DebugMsg( DEBUG_HIGH | DEBUG_HANDLED, "gDPFillRectangle( %i, %i, %i, %i );\n",
         ulx, uly, lrx, lry );
#endif
}

void gDPSetConvert( s32 k0, s32 k1, s32 k2, s32 k3, s32 k4, s32 k5 )
{
// angrylion's macro
#define SRA(exp, sa)    ((signed)(exp) >> (sa))
#define SIGN(i, b)      SRA((i) << (32 - (b)), (32 - (b)))

	gDP.convert.k0 = SIGN(k0, 9);
	gDP.convert.k1 = SIGN(k1, 9);
	gDP.convert.k2 = SIGN(k2, 9);
	gDP.convert.k3 = SIGN(k3, 9);
	gDP.convert.k4 = SIGN(k4, 9);
	gDP.convert.k5 = SIGN(k5, 9);

   /* TODO/FIXME - update */
#if 1
   gDP.changed |= CHANGED_CONVERT;
#else
	CombinerInfo::get().updateConvertColor();
#endif
}

void gDPSetKeyR( u32 cR, u32 sR, u32 wR )
{
   gDP.key.center.r = cR * 0.0039215689f;;
   gDP.key.scale.r = sR * 0.0039215689f;;
   gDP.key.width.r = wR * 0.0039215689f;;

}

void gDPSetKeyGB(u32 cG, u32 sG, u32 wG, u32 cB, u32 sB, u32 wB )
{
   gDP.key.center.g = cG * 0.0039215689f;;
   gDP.key.scale.g = sG * 0.0039215689f;;
   gDP.key.width.g = wG * 0.0039215689f;;
   gDP.key.center.b = cB * 0.0039215689f;;
   gDP.key.scale.b = sB * 0.0039215689f;;
   gDP.key.width.b = wB * 0.0039215689f;;

   /* TODO/FIXME - update */
#if 0
	CombinerInfo::get().updateKeyColor();
#endif
}

void gDPTextureRectangle( f32 ulx, f32 uly, f32 lrx, f32 lry, s32 tile, f32 s, f32 t, f32 dsdx, f32 dtdy )
{
   f32 lrs, lrt;
   float tmp;

   if (gDP.otherMode.cycleType == G_CYC_COPY)
   {
      dsdx = 1.0f;
      lrx += 1.0f;
      lry += 1.0f;
   }
	lry = max(lry, uly + 1.0f);

	gDPTile *textureTileOrg[2];
	textureTileOrg[0] = gSP.textureTile[0];
	textureTileOrg[1] = gSP.textureTile[1];
   gSP.textureTile[0] = &gDP.tiles[tile];
   gSP.textureTile[1] = &gDP.tiles[(tile + 1) & 7];

   if (gDP.loadTile->textureMode == TEXTUREMODE_NORMAL)
      gDP.loadTile->textureMode = TEXTUREMODE_TEXRECT;
	if (gSP.textureTile[1]->textureMode == TEXTUREMODE_NORMAL)
		gSP.textureTile[1]->textureMode = TEXTUREMODE_TEXRECT;

	// HACK ALERT!
	if (((int)(s) == 512) && (gDP.colorImage.width < 512))
		s = 0.0f;

   if (__RSP.cmd == G_TEXRECTFLIP)
   {
      lrs = s + (lry - uly - 1) * dtdy;
      lrt = t + (lrx - ulx - 1) * dsdx;
   }
   else
   {
      lrs = s + (lrx - ulx - 1) * dsdx;
      lrt = t + (lry - uly - 1) * dtdy;
   }

   gDP.texRect.width = (unsigned int)(max( lrs, s ) + dsdx);
   gDP.texRect.height = (unsigned int)(max( lrt, t ) + dtdy);

   if (lrs < s)
   {
      tmp = ulx;
      ulx = lrx;
      lrx = tmp;
      tmp = s;
      s = lrs;
      lrs = tmp;
   }
   if (lrt < t)
   {
      tmp = uly;
      uly = lry;
      lry = tmp;
      tmp = t;
      t = lrt;
      lrt = tmp;
   }

   OGL_DrawTexturedRect( ulx, uly, lrx, lry, s, t, lrs, lrt, (__RSP.cmd == G_TEXRECTFLIP));

	gSP.textureTile[0] = textureTileOrg[0];
	gSP.textureTile[1] = textureTileOrg[1];

   if (depthBuffer.current)
      depthBuffer.current->cleared = FALSE;
   gDP.colorImage.changed = TRUE;

   if (gDP.colorImage.width < 64)
		gDP.colorImage.height = (u32)max( (f32)gDP.colorImage.height, lry );
   else
      gDP.colorImage.height = (unsigned int)(max( gDP.colorImage.height, gDP.scissor.lry ));

#ifdef DEBUG
   DebugMsg( DEBUG_HIGH | DEBUG_HANDLED, "gDPTextureRectangle( %f, %f, %f, %f, %i, %f, %f, %f, %f );\n",
         ulx, uly, lrx, lry, tile, s, t, dsdx, dtdy );
#endif
}

void gDPTextureRectangleFlip( f32 ulx, f32 uly, f32 lrx, f32 lry, s32 tile, f32 s, f32 t, f32 dsdx, f32 dtdy )
{
   gDPTextureRectangle( ulx, uly, lrx, lry, tile, s, t, dsdx, dtdy );
#ifdef DEBUG
   DebugMsg( DEBUG_HIGH | DEBUG_HANDLED, "gDPTextureRectangleFlip( %f, %f, %f, %f, %i, %f, %f, %f, %f);\n",
         ulx, uly, lrx, lry, tile, s, t, dsdx, dtdy );
#endif
}

void gDPFullSync(void)
{
   *gfx_info.MI_INTR_REG |= MI_INTR_DP;

   if (gfx_info.CheckInterrupts)
      gfx_info.CheckInterrupts();

#ifdef DEBUG
   DebugMsg( DEBUG_HIGH | DEBUG_HANDLED, "gDPFullSync();\n" );
#endif
}

void gDPTileSync(void)
{
#ifdef DEBUG
   DebugMsg( DEBUG_HIGH | DEBUG_IGNORED | DEBUG_TEXTURE, "gDPTileSync();\n" );
#endif
}

void gDPPipeSync(void)
{
#ifdef DEBUG
   DebugMsg( DEBUG_HIGH | DEBUG_IGNORED, "gDPPipeSync();\n" );
#endif
}

void gDPLoadSync(void)
{
#ifdef DEBUG
   DebugMsg( DEBUG_HIGH | DEBUG_IGNORED, "gDPLoadSync();\n" );
#endif
}

void gDPNoOp(void)
{
#ifdef DEBUG
   DebugMsg( DEBUG_HIGH | DEBUG_IGNORED, "gDPNoOp();\n" );
#endif
}

static void gDPTriangle(u32 _w1, u32 _w2, int shade, int texture, int zbuffer)
{
	//gDPLLETriangle(_w1, _w2, shade, texture, zbuffer, RDP.cmd_data + RDP.cmd_cur);
}

void gDPTriFill(u32 w0, u32 w1)
{
	gDPTriangle(w0, w1, 0, 0, 0);
#if 0
	DebugMsg( DEBUG_HIGH | DEBUG_HANDLED, "trifill\n");
#endif
}

void gDPTriShade(u32 w0, u32 w1)
{
	gDPTriangle(w0, w1, 1, 0, 0);
#if 0
	DebugMsg( DEBUG_HIGH | DEBUG_HANDLED, "trishade\n");
#endif
}

void gDPTriTxtr(u32 w0, u32 w1)
{
	gDPTriangle(w0, w1, 0, 1, 0);
#if 0
	DebugMsg( DEBUG_HIGH | DEBUG_HANDLED, "tritxtr\n");
#endif
}

void gDPTriShadeTxtr(u32 w0, u32 w1)
{
	gDPTriangle(w0, w1, 1, 1, 0);
#if 0
	DebugMsg( DEBUG_HIGH | DEBUG_HANDLED, "trishadetxtr\n");
#endif
}

void gDPTriFillZ(u32 w0, u32 w1)
{
	gDPTriangle(w0, w1, 0, 0, 1);
#if 0
	DebugMsg( DEBUG_HIGH | DEBUG_HANDLED, "trifillz\n");
#endif
}

void gDPTriShadeZ(u32 w0, u32 w1)
{
	gDPTriangle(w0, w1, 1, 0, 1);
#if 0
	DebugMsg( DEBUG_HIGH | DEBUG_HANDLED, "trishadez\n");
#endif
}

void gDPTriTxtrZ(u32 w0, u32 w1)
{
	gDPTriangle(w0, w1, 0, 1, 1);
#if 0
	DebugMsg( DEBUG_HIGH | DEBUG_HANDLED, "tritxtrz\n");
#endif
}

void gDPTriShadeTxtrZ(u32 w0, u32 w1)
{
	gDPTriangle(w0, w1, 1, 1, 1);
#if 0
	DebugMsg( DEBUG_HIGH | DEBUG_HANDLED, "trishadetxtrz\n");
#endif
}
