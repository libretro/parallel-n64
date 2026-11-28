/*
Copyright (C) 2002 Rice1964

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include <stdint.h>

#include "osal_opengl.h"

#include "Config.h"
#include "Debugger.h"
#include "FrameBuffer.h"
#include "OGLBlender.h"
#include "RenderBase.h"
#include "RiceCombiner.h"
#include "RiceGLDraw.h"
#include "RiceGLState.h"
#include "RiceGLTex.h"
#include "RiceRender.h"
#include "RSP_Parser.h"
#include "RSP_S2DEX.h"
#include "TextureManager.h"
#include "Video.h"

/* the sprites and backgrounds: each its texture loaded, then drawn */

/* a draw into the N64's render texture */
static void RiceRender_RenderTextureDrawn(void)
{
    if( status.bHandleN64RenderTexture )
    {
        g_pRenderTextureInfo->maxUsedHeight = g_pRenderTextureInfo->N64Height;
        if( !status.bDirectWriteIntoRDRAM )
        {
            status.bFrameBufferIsDrawn = true;
            status.bFrameBufferDrawnByTriangles = true;
        }
    }
}

/* a sprite drawn into the colour image */
static void RiceRender_SpriteDrawn(void)
{
    if( !status.bCIBufferIsRendered )
        RiceFB_ActiveTextureBuffer();
    RiceRender_RenderTextureDrawn();
}

/* the texture, with RDRAM from its address on */
static TxtrCacheEntry *RiceRender_LoadRDRAM(TxtrInfo *gti)
{
    gti->HeightToLoad = gti->HeightToCreate;
    gti->WidthToLoad = gti->WidthToCreate;
    gti->pPhysicalAddress = (uint8_t*)gfx_info.RDRAM + gti->Address;
    gti->tileNo = -1;
    return TextureManager_GetTexture(gti, false, true, false);
}

void RiceRender_DrawFrameBufferVI(void)
{
    uint32_t VIwidth = *gfx_info.VI_WIDTH_REG;
    TxtrCacheEntry *pEntry;
    CTexture *pTexture;
    TxtrInfo gti;

    gti.clampS = gti.clampT = 0;
    gti.maskS = gti.maskT = gti.mirrorS = gti.mirrorT = 0;
    gti.TLutFmt = TLUT_FMT_RGBA16;
    gti.bSwapped    = false;
    gti.Palette = 0;
    gti.PalAddress = (uint8_t *) &g_wRDPTlut[0];
    gti.LeftToLoad  = 0;
    gti.TopToLoad   = 0;

    if( *gfx_info.VI_ORIGIN_REG > VIwidth*2 )
    {
        gti.Format  = 0;
        gti.Size    = 2;

        gti.Address = (*gfx_info.VI_ORIGIN_REG & (g_dwRamSize-1) ) - VIwidth*2;

        gti.WidthToCreate   = windowSetting.uViWidth;
        gti.HeightToCreate  = windowSetting.uViHeight;

        if( gti.WidthToCreate == 0 || gti.HeightToCreate == 0 )
            goto draw;

        gti.Pitch   = VIwidth << gti.Size >> 1;
    }
    else
    {
        gti.Format  = g_CI.dwFormat;
        gti.Size    = g_CI.dwSize;

        gti.Address = RSPSegmentAddr(g_CI.dwAddr);

        gti.WidthToCreate       = g_CI.dwWidth;
        gti.HeightToCreate      = g_CI.dwWidth*3/4;

        if( gti.Size == G_IM_SIZ_4b )
            gti.Pitch = g_CI.dwWidth >> 1;
        else
            gti.Pitch = g_CI.dwWidth << (gti.Size-1);
    }

    if( gti.Address + gti.Pitch*gti.HeightToCreate <= g_dwRamSize )
    {
        pEntry = RiceRender_LoadRDRAM(&gti);
        if( pEntry )
            RiceRender_SetCurrentTexture( 0, pEntry->pTexture, pEntry->ti.WidthToCreate, pEntry->ti.HeightToCreate, pEntry);
    }

draw:
    RiceCombiner_InitSimpleTextureDraw(0);

    RiceGL_ZBufferEnable(false);
    RiceGL_SetZUpdate(false);
    rice_combiner.alphaTest = false;

    rice_render.blender->ops->Disable(rice_render.blender);

    /* the whole frame buffer */
    pTexture = g_textures[0].m_pCTexture;
    if( pTexture )
        RiceRender_DrawSimple2DTexture(0, 0, windowSetting.uViWidth, windowSetting.uViHeight,
            0, 0, 1/pTexture->m_fXScale, 1/pTexture->m_fYScale, 0xFFFFFFFF, 0xFFFFFFFF, 0, 1);

    TXTRBUF_OR_CI_DUMP(TRACE0("Draw Frame Buffer Img"));
}

void RiceRender_DrawSprite2D(Sprite2DInfo *info)
{
    TxtrInfo gti;
    uint8_t *rdram_u8 = (uint8_t*)gfx_info.RDRAM;
    int x0, y0, x1, y1;
    float t0, s0, t1, s1;
    float depth;

    RiceRender_SpriteDrawn();

    gti.Format  = info->spritePtr->SourceImageType;
    gti.Size    = info->spritePtr->SourceImageBitSize;

    gti.Address = RSPSegmentAddr(info->spritePtr->SourceImagePointer);
    gti.Palette = 0;
    gti.PalAddress = (uint8_t *) (rdram_u8 + RSPSegmentAddr(info->spritePtr->TlutPointer));

    if( options.enableHackForGames == HACK_FOR_NITRO )
    {
        gti.WidthToCreate   = (uint32_t)(info->spritePtr->SubImageWidth/info->scaleX);
        gti.HeightToCreate  = (uint32_t)(info->spritePtr->SubImageHeight/info->scaleY);
        gti.LeftToLoad      = (uint32_t)(info->spritePtr->SourceImageOffsetS/info->scaleX);
        gti.TopToLoad       = (uint32_t)(info->spritePtr->SourceImageOffsetT/info->scaleY);
        gti.Pitch   = info->spritePtr->Stride << gti.Size >> 1;
        gti.Pitch   = (uint32_t)(gti.Pitch*info->scaleY);
    }
    else
    {
        gti.WidthToCreate   = info->spritePtr->SubImageWidth;
        gti.HeightToCreate  = info->spritePtr->SubImageHeight;
        gti.LeftToLoad      = info->spritePtr->SourceImageOffsetS;
        gti.TopToLoad       = info->spritePtr->SourceImageOffsetT;
        gti.Pitch   = info->spritePtr->Stride << gti.Size >> 1;
    }

    /* SubImageWidth/Height are signed shorts and, in the HACK_FOR_NITRO
     * path, are divided by a scale factor before being cast to unsigned.
     * A degenerate sprite (e.g. Wipeout 64's exploding-car effect, which
     * emits a SubImageWidth that works out to -2) would wrap to a huge
     * width/height here and then be walked as a buffer extent by
     * CalculateRDRAMCRC/CalculateMaxCI/the decompressor, reading far off
     * the end of RDRAM and crashing. Reject non-positive dimensions. */
    if( (int)gti.WidthToCreate <= 0 || (int)gti.HeightToCreate <= 0 )
    {
        TRACE0("Skip Sprite image, degenerate dimensions");
    }
    else if( gti.Address + gti.Pitch*gti.HeightToCreate > g_dwRamSize )
    {
        TRACE0("Skip Sprite image decompress, memory out of bound");
    }
    else
    {
        gti.TLutFmt     = TLUT_FMT_RGBA16;
        gti.bSwapped    = false;
        RiceRender_SetCurrentTextureEntry(0, RiceRender_LoadRDRAM(&gti));
    }

    info->scaleX = 1/info->scaleX;
    info->scaleY = 1/info->scaleY;

    if( info->flipX )
    {
        x0 = info->px + (int)(info->spritePtr->SubImageWidth*info->scaleX);
        x1 = info->px;
    }
    else
    {
        x0 = info->px;
        x1 = info->px + (int)(info->spritePtr->SubImageWidth*info->scaleX);
    }

    if( info->flipY )
    {
        y0 = info->py + (int)(info->spritePtr->SubImageHeight*info->scaleY);
        y1 = info->py;
    }
    else
    {
        y0 = info->py;
        y1 = info->py + (int)(info->spritePtr->SubImageHeight*info->scaleY);
    }

    t0 = s0 = 0;
    if( options.enableHackForGames == HACK_FOR_NITRO )
    {
        t1 = info->spritePtr->SubImageWidth*info->scaleX/g_textures[0].m_fTexWidth;
        s1 = info->spritePtr->SubImageHeight*info->scaleY/g_textures[0].m_fTexHeight;
    }
    else
    {
        t1 = info->spritePtr->SubImageWidth/g_textures[0].m_fTexWidth;
        s1 = info->spritePtr->SubImageHeight/g_textures[0].m_fTexHeight;
    }

    RiceRender_SetCombinerAndBlender();
    RiceGLTex_SetUFlag(TEXTURE_UV_FLAG_CLAMP, 0);
    RiceGLTex_SetVFlag(TEXTURE_UV_FLAG_CLAMP, 0);

    depth = ( gRDP.otherMode.depth_source == 1 ) ? gRDP.fPrimitiveDepth : 0;
    RiceRender_DrawSimple2DTexture((float)x0, (float)y0, (float)x1, (float)y1, t0, s0, t1, s1, 0, 0xffffffff, depth, 1.0f);
}

/* with rotation */
void RiceRender_DrawSpriteR(uObjTxSprite *sprite, bool initCombiner, uint32_t tile,
      uint32_t left, uint32_t top, uint32_t width, uint32_t height)
{
    float scaleX, scaleY, depth, x0, y0, x1, y1;
    RenderTexture *tex = &g_textures[tile];
    int i;

    (void)initCombiner;

    RiceRender_SpriteDrawn();
    RiceRender_SetCombinerAndBlender();

    scaleX = sprite->sprite.scaleW/1024.0f;
    scaleY = sprite->sprite.scaleH/1024.0f;

    if( width == 0 || height == 0 )
    {
        width = tex->m_dwTileWidth;
        height = tex->m_dwTileHeight;
    }

    depth = 0.0;
    if (gRDP.otherMode.depth_source==1)
        depth = gRDP.fPrimitiveDepth;

    x0 = sprite->sprite.objX/4.0f;
    y0 = sprite->sprite.objY/4.0f;
    x1 = sprite->sprite.imageW / 32.0f / scaleX + x0;
    y1 = sprite->sprite.imageH / 32.0f / scaleY + y0;

    if( sprite->sprite.imageFlags&1    ) { float temp = x0; x0 = x1; x1 = temp; } /* flip X */
    if( sprite->sprite.imageFlags&0x10 ) { float temp = y0; y0 = y1; y1 = temp; } /* flip Y */

    g_texRectTVtx[0].x = (gObjMtxReal.A*x0 + gObjMtxReal.B*y0 + gObjMtxReal.X)*windowSetting.fMultX;
    g_texRectTVtx[0].y = (gObjMtxReal.C*x0 + gObjMtxReal.D*y0 + gObjMtxReal.Y)*windowSetting.fMultY;
    g_texRectTVtx[1].x = (gObjMtxReal.A*x1 + gObjMtxReal.B*y0 + gObjMtxReal.X)*windowSetting.fMultX;
    g_texRectTVtx[1].y = (gObjMtxReal.C*x1 + gObjMtxReal.D*y0 + gObjMtxReal.Y)*windowSetting.fMultY;
    g_texRectTVtx[2].x = (gObjMtxReal.A*x1 + gObjMtxReal.B*y1 + gObjMtxReal.X)*windowSetting.fMultX;
    g_texRectTVtx[2].y = (gObjMtxReal.C*x1 + gObjMtxReal.D*y1 + gObjMtxReal.Y)*windowSetting.fMultY;
    g_texRectTVtx[3].x = (gObjMtxReal.A*x0 + gObjMtxReal.B*y1 + gObjMtxReal.X)*windowSetting.fMultX;
    g_texRectTVtx[3].y = (gObjMtxReal.C*x0 + gObjMtxReal.D*y1 + gObjMtxReal.Y)*windowSetting.fMultY;

    g_texRectTVtx[0].tcord[0].u = left/tex->m_fTexWidth;
    g_texRectTVtx[0].tcord[0].v = top/tex->m_fTexHeight;
    g_texRectTVtx[1].tcord[0].u = (left+width)/tex->m_fTexWidth;
    g_texRectTVtx[1].tcord[0].v = top/tex->m_fTexHeight;
    g_texRectTVtx[2].tcord[0].u = (left+width)/tex->m_fTexWidth;
    g_texRectTVtx[2].tcord[0].v = (top+height)/tex->m_fTexHeight;
    g_texRectTVtx[3].tcord[0].u = left/tex->m_fTexWidth;
    g_texRectTVtx[3].tcord[0].v = (top+height)/tex->m_fTexHeight;

    for (i = 0; i < 4; i++)
    {
        g_texRectTVtx[i].z = depth;
        g_texRectTVtx[i].rhw = 1;
        g_texRectTVtx[i].dcDiffuse = 0xffffffff;
        g_texRectTVtx[i].dcSpecular = 0xffffffff;
    }

    RiceGL_DrawSpriteR(gRDP.fvPrimitiveColor);
}

void RiceRender_ObjBGCopy(uObjBg *info)
{
    TxtrInfo gti;
    uint32_t frameH, frameW, imageH, imageW;
    float x0, y0, x1, y1, s0, t0, texwidth, texheight;
    float u0, v0, maxu, maxv, x2, y2, u1, v1, depth;
    float texW, texH;
    COLOR speColor = 0;
    COLOR difColor = 0xffffffff;

    /* the texture */
    gti.Format      = info->imageFmt;
    gti.Size        = info->imageSiz;
    gti.Address     = RSPSegmentAddr(info->imagePtr);
    gti.LeftToLoad  = 0;
    gti.TopToLoad   = 0;
    gti.Palette     = info->imagePal;

    gti.PalAddress  = (uint8_t *) &g_wRDPTlut[0];
    gti.bSwapped    = false;
    gti.TLutFmt     = TLUT_FMT_RGBA16;

    gti.WidthToCreate   = info->imageW/4;
    gti.HeightToCreate  = info->imageH/4;

    if( options.bEnableHacks )
    {
        if( g_CI.dwWidth == 0x200 && gti.Format == g_CI.dwFormat && gti.Size == g_CI.dwSize &&
            gti.WidthToCreate == 0x200 )
        {
            /* Hack for RE2 */
            uint32_t w = *gfx_info.VI_WIDTH_REG & 0xFFF;
            gti.HeightToCreate = (gti.WidthToCreate*gti.HeightToCreate)/w;
            gti.WidthToCreate = w;
        }
    }

    gti.Pitch   = gti.WidthToCreate << gti.Size >> 1;
    gti.Pitch   = (gti.Pitch>>3)<<3;    /* Align to 8 bytes */

    if( gti.Address + gti.Pitch*gti.HeightToCreate > g_dwRamSize )
    {
        TRACE0("Skip BG copy loading, memory out of bound");
    }
    else
        RiceRender_SetCurrentTextureEntry(0, RiceRender_LoadRDRAM(&gti));

    /* the draw */
    RiceRender_SpriteDrawn();
    RiceRender_SetCombinerAndBlender();

    frameH = info->frameH;
    frameW = info->frameW;
    imageH = info->imageH;
    imageW = info->imageW;

    if( options.bEnableHacks )
    {
        if( g_CI.dwWidth == 0x200 && info->imageFmt == g_CI.dwFormat && info->imageSiz == g_CI.dwSize &&
            frameW == 0x800 )
        {
            /* Hack for RE2 */
            uint32_t width = *gfx_info.VI_WIDTH_REG & 0xFFF;
            imageH = frameH = (frameW/4*frameH/4)/width*4;
            imageW = frameW = width*4;
        }
    }

    texW = g_textures[0].m_fTexWidth;
    texH = g_textures[0].m_fTexHeight;

    x0 = info->frameX / 4.0f;
    y0 = info->frameY / 4.0f;
    x1 = frameW / 4.0f + x0;
    y1 = frameH / 4.0f + y0;

    s0 = info->imageX / 32.0f;
    t0 = info->imageY / 32.0f;

    texwidth = imageW/4.0f;
    texheight = imageH/4.0f;

    u0 = s0/texW;
    v0 = t0/texH;
    maxu = texwidth/texW;
    maxv = texheight/texH;

    x2 = x0 + (texwidth-s0);
    y2 = y0 + (texheight-t0);
    u1 = (x1-x2)/texW;
    v1 = (y1-y2)/texH;

    depth = (gRDP.otherMode.depth_source == 1 ? gRDP.fPrimitiveDepth : 0.0f);

    if( options.enableHackForGames == HACK_FOR_COMMANDCONQUER )
    {
        float s1 = (x1-x0) + s0;
        float t1 = (y1-y0) + t0;
        RiceRender_DrawSimple2DTexture(x0, y0, x1, y1, u0, v0,
            s1/texW, t1/texH, difColor, speColor, depth, 1);
    }
    else if( x2 >= x1 )
    {
        float s1 = (x1-x0) + s0;
        if( y2 >= y1 )
        {
            float t1 = (y1-y0) + t0;
            RiceRender_DrawSimple2DTexture(x0, y0, x1, y1, u0, v0,
                s1/texW, t1/texH, difColor, speColor, depth, 1);
        }
        else
        {
            RiceRender_DrawSimple2DTexture(x0, y0, x1, y2, u0, v0,
                s1/texW, maxv, difColor, speColor, depth, 1);
            RiceRender_DrawSimple2DTexture(x0, y2, x1, y1, u0, 0,
                s1/texW, v1, difColor, speColor, depth, 1);
        }
    }
    else
    {
        if( y2 >= y1 )
        {
            float t1 = (y1-y0) + t0;
            RiceRender_DrawSimple2DTexture(x0, y0, x2, y1, u0, v0,
                maxu, t1/texH, difColor, speColor, depth, 1);
            RiceRender_DrawSimple2DTexture(x2, y0, x1, y1, 0, v0,
                u1, t1/texH, difColor, speColor, depth, 1);
        }
        else
        {
            RiceRender_DrawSimple2DTexture(x0, y0, x2, y2, u0, v0, maxu, maxv, difColor, speColor, depth, 1);
            RiceRender_DrawSimple2DTexture(x2, y0, x1, y2, 0, v0, u1, maxv, difColor, speColor, depth, 1);
            RiceRender_DrawSimple2DTexture(x0, y2, x2, y1, u0, 0, maxu, v1, difColor, speColor, depth, 1);
            RiceRender_DrawSimple2DTexture(x2, y2, x1, y1, 0, 0, u1, v1, difColor, speColor, depth, 1);
        }
    }
}

/* without rotation */
void RiceRender_ObjBG1CYC(uObjScaleBg *bg)
{
    TxtrInfo gti;
    uint32_t imageWidth = bg->imageW/4;
    uint32_t imageHeight = bg->imageH/4;
    float depth, x0, y0, x1, y1, s0, t0, scaleX, scaleY, texwidth, texheight;
    float u0, v0, maxu, maxv, x2, y2, u1, v1, texW, texH;
    COLOR speColor = 0;
    COLOR difColor = 0xffffffff;

    /* the texture */
    gti.Format = bg->imageFmt;
    gti.Size   = bg->imageSiz;

    gti.Address = RSPSegmentAddr(bg->imagePtr);

    gti.LeftToLoad     = 0;
    gti.TopToLoad      = 0;

    gti.WidthToCreate  = imageWidth;
    gti.HeightToCreate = imageHeight;

    gti.clampS = gti.clampT = 1;
    gti.maskS = gti.maskT = 0;

    gti.Palette     = bg->imagePal;
    gti.PalAddress  = (uint8_t *) &g_wRDPTlut[0];

    gti.Pitch   = imageWidth << gti.Size >> 1;
    gti.Pitch   = (gti.Pitch>>3)<<3;    /* Align to 8 bytes */

    if( gti.Address + gti.Pitch*gti.HeightToCreate > g_dwRamSize )
    {
        TRACE0("Skip BG 1CYC loading, memory out of bound");
    }
    else
    {
        gti.TLutFmt = TLUT_FMT_RGBA16;
        gti.bSwapped = false;
        RiceRender_SetCurrentTextureEntry(0, RiceRender_LoadRDRAM(&gti));
    }

    /* the draw */
    if( !status.bCIBufferIsRendered )
        RiceFB_ActiveTextureBuffer();

    if( g_curRomInfo.bDisableObjBG )
        return;

    RiceRender_RenderTextureDrawn();
    RiceRender_SetCombinerAndBlender();

    depth = (gRDP.otherMode.depth_source == 1 ? gRDP.fPrimitiveDepth : 0.0f);

    texW = g_textures[0].m_fTexWidth;
    texH = g_textures[0].m_fTexHeight;

    x0 = bg->frameX / 4.0f;
    y0 = bg->frameY / 4.0f;
    x1 = bg->frameW / 4.0f + x0;
    y1 = bg->frameH / 4.0f + y0;

    s0 = bg->imageX / 32.0f;
    t0 = bg->imageY / 32.0f;

    scaleX = bg->scaleW/1024.0f;
    scaleY = bg->scaleH/1024.0f;

    texwidth = bg->imageW/4.0f;
    texheight = bg->imageH/4.0f;

    u0 = s0/texW;
    v0 = t0/texH;
    maxu = texwidth/texW;
    maxv = texheight/texH;

    x2 = x0 + (texwidth-s0)/scaleX;
    y2 = y0 + (texheight-t0)/scaleY;
    u1 = (x1-x2)*scaleX/texW;
    v1 = (y1-y2)*scaleY/texH;

    rice_combiner.alphaTest = false;

    if( options.enableHackForGames != HACK_FOR_YOSHI )
    {
        float s1 = (x1-x0)*scaleX + s0;
        float t1 = (y1-y0)*scaleY + t0;
        RiceRender_DrawSimple2DTexture(x0, y0, x1, y1, u0, v0,
            s1/texW, t1/texH, difColor, speColor, depth, 1);
    }
    else if( x2 >= x1 )
    {
        float s1 = (x1-x0)*scaleX + s0;
        if( y2 >= y1 )
        {
            float t1 = (y1-y0)*scaleY + t0;
            RiceRender_DrawSimple2DTexture(x0, y0, x1, y1, u0, v0,
                s1/texW, t1/texH, difColor, speColor, depth, 1);
        }
        else
        {
            RiceRender_DrawSimple2DTexture(x0, y0, x1, y2, u0, v0,
                s1/texW, maxv, difColor, speColor, depth, 1);
            RiceRender_DrawSimple2DTexture(x0, y2, x1, y1, u0, 0,
                s1/texW, v1, difColor, speColor, depth, 1);
        }
    }
    else
    {
        if( y2 >= y1 )
        {
            float t1 = (y1-y0)*scaleY + t0;
            RiceRender_DrawSimple2DTexture(x0, y0, x2, y1, u0, v0,
                maxu, t1/texH, difColor, speColor, depth, 1);
            RiceRender_DrawSimple2DTexture(x2, y0, x1, y1, 0, v0,
                u1, t1/texH, difColor, speColor, depth, 1);
        }
        else
        {
            RiceRender_DrawSimple2DTexture(x0, y0, x2, y2, u0, v0, maxu, maxv, difColor, speColor, depth, 1);
            RiceRender_DrawSimple2DTexture(x2, y0, x1, y2, 0, v0, u1, maxv, difColor, speColor, depth, 1);
            RiceRender_DrawSimple2DTexture(x0, y2, x2, y1, u0, 0, maxu, v1, difColor, speColor, depth, 1);
            RiceRender_DrawSimple2DTexture(x2, y2, x1, y1, 0, 0, u1, v1, difColor, speColor, depth, 1);
        }
    }
}

/* without rotation */
void RiceRender_DrawSprite(uObjTxSprite *sprite, bool rectR)
{
    float objX, objY, width, high, scaleW, scaleH;
    float x0, y0, x1, y1, depth;
    GLint iClampS, iClampT;
    CTexture *pTexture;

    RiceRender_SpriteDrawn();
    RiceRender_SetCombinerAndBlender();

    objX = sprite->sprite.objX/4.0f;
    objY = sprite->sprite.objY/4.0f;
    width = sprite->sprite.imageW / 32.0f;
    high = sprite->sprite.imageH / 32.0f;
    scaleW = sprite->sprite.scaleW/1024.0f;
    scaleH = sprite->sprite.scaleH/1024.0f;

    if( g_curRomInfo.bIncTexRectEdge )
    {
        width++;
        high++;
    }

    if( rectR )
    {
        /* Upper-left coordinate
         * ( X + objX / BaseScaleX, Y+objY/BaseScaleY )
         * Lower-right coordinate
         * ( X + (objX + imageW / scaleW) / BaseScaleX - 1, Y + (objY + imageH / scaleH) / BaseScaleY - 1 ) */
        x0 = gObjMtxReal.X + objX/gObjMtxReal.BaseScaleX;
        y0 = gObjMtxReal.Y + objY/gObjMtxReal.BaseScaleY;
        x1 = gObjMtxReal.X + (objX + width / scaleW) / gObjMtxReal.BaseScaleX;
        y1 = gObjMtxReal.Y + (objY + high / scaleH) / gObjMtxReal.BaseScaleY;
    }
    else
    {
        /* (objX, objY) - ( objX+imageW/scaleW-1, objY+imageH/scaleH-1) */
        x0 = objX;
        y0 = objY;
        x1 = objX + width / scaleW;
        y1 = objY + high / scaleH;

        if( (sprite->sprite.imageFlags&1) ) /* flipX */
        {
            float temp = x0;
            x0 = x1;
            x1 = temp;
        }

        if( (sprite->sprite.imageFlags&0x10) ) /* flipY */
        {
            float temp = y0;
            y0 = y1;
            y1 = temp;
        }
    }

    /* save the current clamp type */
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &iClampS);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, &iClampT);
    /* force clamp type to CLAMP_EDGE (experiments show sometimes this is set to hex 0x2901 - invalid value) */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    /* draw the 2D sprite as 2 triangles */
    depth = (gRDP.otherMode.depth_source == 1 ? gRDP.fPrimitiveDepth : 0.0f);
    pTexture = g_textures[0].m_pCTexture;
    RiceRender_DrawSimple2DTexture(x0, y0, x1, y1, 0, 0, 1/pTexture->m_fXScale, 1/pTexture->m_fYScale,
        0xffffffff, 0, depth, 1);
    /* return clamp type to original setting */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, iClampS);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, iClampT);
}

void RiceRender_LoadObjSprite(uObjTxSprite *sprite, bool useTIAddr)
{
    TxtrInfo gti;

    (void)useTIAddr;

    gti.Format  = sprite->sprite.imageFmt;
    gti.Size    = sprite->sprite.imageSiz;

    gti.Address = RSPSegmentAddr(sprite->txtr.block.image);
    gti.Address += sprite->sprite.imageAdrs<<3;
    gti.LeftToLoad      = 0;
    gti.TopToLoad       = 0;
    gti.Palette = sprite->sprite.imagePal;
    gti.PalAddress = (uint8_t *) &g_wRDPTlut[0];

    if( sprite->txtr.block.type == S2DEX_OBJLT_TXTRBLOCK )
    {
        gti.WidthToCreate       = sprite->sprite.imageW/32;
        if( sprite->sprite.imageW >= 0x8000 )
        {
            gti.WidthToCreate = (0x10000-sprite->sprite.imageW)/32;
        }
        gti.HeightToCreate  = sprite->sprite.imageH/32;
        if( sprite->sprite.imageH >= 0x8000 )
        {
            gti.HeightToCreate  = (0x10000-sprite->sprite.imageH)/32;
        }
        gti.Pitch       = (2047/(sprite->txtr.block.tline-1)) << 3;
    }
    else if( sprite->txtr.block.type == S2DEX_OBJLT_TXTRTILE )
    {
        gti.WidthToCreate       = ((sprite->txtr.tile.twidth+1)>>2)<<(4-gti.Size);
        gti.HeightToCreate  = (sprite->txtr.tile.theight+1)>>2;

        if( gti.Size == G_IM_SIZ_4b )
            gti.Pitch = gti.WidthToCreate >> 1;
        else
            gti.Pitch = gti.WidthToCreate << (gti.Size-1);
    }

    if( gti.Address + gti.Pitch*gti.HeightToCreate > g_dwRamSize )
    {
        TRACE0("Skip Obj sprite loading, memory out of bound");
        return;
    }

    gti.TLutFmt = TLUT_FMT_RGBA16;
    gti.bSwapped    = false;

    RiceRender_SetCurrentTextureEntry(0, RiceRender_LoadRDRAM(&gti));
}
