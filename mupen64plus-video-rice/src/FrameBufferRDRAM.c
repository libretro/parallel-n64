/*
Copyright (C) 2003 Rice1964

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

/* Texels copied into the N64's colour image in RDRAM, where a game draws
   one into it (Yoshi's Story, Ogre Battle 64's backgrounds). C89. */
#include <retro_miscellaneous.h>

#include "ConvertImage.h"
#include "FrameBuffer.h"
#include "RenderBase.h"
#include "Video.h"

uint16_t ConvertRGBATo555(uint32_t color32)
{
    return (uint16_t)((((color32>>19)&0x1F)<<RGBA5551_RedShift) | (((color32>>11)&0x1F)<<RGBA5551_GreenShift) | (((color32>>3)&0x1F)<<RGBA5551_BlueShift) | ((color32>>31)));
}

void TexRectToFrameBuffer_8b(uint32_t dwXL, uint32_t dwYL, uint32_t dwXH, uint32_t dwYH, float t0u0, float t0v0, float t0u1, float t0v1, uint32_t dwTile)
{
    /* Copy the framebuffer texture into the N64 framebuffer memory */
    /* Used in Yoshi */

    uint32_t maxW = g_pRenderTextureInfo->N64Width;
    uint32_t maxH = g_pRenderTextureInfo->N64Height;

    uint32_t maxOff = maxW*maxH;

    const TMEMLoadMapInfo *info = &g_tmemLoadAddrMap[gDP.tiles[dwTile].tmem];
    uint32_t dwWidth = dwXH-dwXL;
    uint32_t dwHeight = dwYH-dwYL;

    float xScale = (t0u1-t0u0)/dwWidth;
    float yScale = (t0v1-t0v0)/dwHeight;

    uint8_t *rdram_u8 = (uint8_t*)gfx_info.RDRAM;
    uint8_t* dwSrc = rdram_u8 + info->dwLoadAddress;
    uint8_t* dwDst = rdram_u8 + g_pRenderTextureInfo->CI_Info.dwAddr;

    uint32_t dwSrcPitch = gRDP.tilesinfo[dwTile].dwPitch;
    uint32_t dwDstPitch = g_pRenderTextureInfo->CI_Info.dwWidth;

    uint32_t dwSrcOffX = gRDP.tilesinfo[dwTile].hilite_sl;
    uint32_t dwSrcOffY = gRDP.tilesinfo[dwTile].hilite_tl;

    uint32_t dwLeft = dwXL;
    uint32_t dwTop = dwYL;
    uint32_t y;

    dwWidth = MIN(dwWidth, maxW-dwLeft);
    dwHeight = MIN(dwHeight, maxH-dwTop);

    if (maxH <= dwTop)
        return;

    for (y = 0; y < dwHeight; y++)
    {
        uint32_t dwByteOffset = (uint32_t)(((y*yScale+dwSrcOffY) * dwSrcPitch) + dwSrcOffX);
        uint32_t x;

        for (x = 0; x < dwWidth; x++)
        {
            /* no further than the image (its last byte maxOff - 1) */
            if ((((y+dwTop)*dwDstPitch+x+dwLeft)^0x3) >= maxOff)
                continue;
            dwDst[((y+dwTop)*dwDstPitch+x+dwLeft)^0x3] = dwSrc[(uint32_t)(dwByteOffset+x*xScale) ^ 0x3];
        }
    }
}

void TexRectToN64FrameBuffer_16b(uint32_t x0, uint32_t y0, uint32_t width, uint32_t height, uint32_t dwTile)
{
    /* Copy the framebuffer texture into the N64 RDRAM framebuffer memory structure */

    DrawInfo srcInfo;
    uint32_t n64CIaddr;
    uint32_t n64CIwidth;
    uint32_t y;
    /* the texture's pixels read, not changed: no update ended, nothing
       put on the GPU again */
    if (CTexture_StartUpdate(g_textures[dwTile].m_pCTexture, &srcInfo) == false)
        return;

    n64CIaddr = g_CI.dwAddr;
    n64CIwidth = g_CI.dwWidth;

    for (y = 0; y < height; y++)
    {
       uint8_t *rdram_u8 = (uint8_t*)gfx_info.RDRAM;
        uint32_t* pSrc = (uint32_t*)((uint8_t*)srcInfo.lpSurface + y * srcInfo.lPitch);
        uint16_t* pN64Buffer = (uint16_t*)(rdram_u8 + (n64CIaddr&(g_dwRamSize-1)))+(y+y0)*n64CIwidth;
        uint32_t x;

        for (x = 0; x < width; x++)
        {
            pN64Buffer[x+x0] = ConvertRGBATo555(pSrc[x]);
        }
    }
}
