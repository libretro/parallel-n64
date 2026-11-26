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

#ifndef _FRAME_BUFFER_H_
#define _FRAME_BUFFER_H_

#include "typedefs.h"
#include "RenderTexture.h"
#include "TextureManager.h"

#ifdef __cplusplus
extern "C" {
#endif

/* FrameBufferRDRAM.c's: texels copied into the N64's colour image in
   RDRAM, 8 bits from the tile's load, 16 bits from the tile's texture */
void TexRectToN64FrameBuffer_16b(uint32_t x0, uint32_t y0, uint32_t width, uint32_t height, uint32_t dwTile);
void TexRectToFrameBuffer_8b(uint32_t dwXL, uint32_t dwYL, uint32_t dwXH, uint32_t dwYH, float t0u0, float t0v0, float t0u1, float t0v1, uint32_t dwTile);
/* an A8R8G8B8 pixel as RGBA 5551 */
uint16_t ConvertRGBATo555(uint32_t color32);

extern RenderTextureInfo gRenderTextureInfos[];
extern RenderTextureInfo *g_pRenderTextureInfo;
extern RenderTextureInfo newRenderTextureInfo;
extern int numOfTxtBufInfos;

extern RecentCIInfo g_RecentCIInfo[];
extern RecentViOriginInfo g_RecentVIOriginInfo[];
extern RecentCIInfo *g_uRecentCIInfoPtrs[5];

/* FrameBuffer.c's: the N64 colour images seen and the textures they are
   drawn into. Set up for a new context, and their textures deleted */
void RiceFB_Initialize(void);
void RiceFB_CloseUp(void);
/* the colour image set; the render texture drawn into made active, on
   its first draw */
void RiceFB_SetCIAddr(const SetImgInfo *newCI);
void RiceFB_ActiveTextureBuffer(void);
/* the VI showing addr */
void RiceFB_SetAddrBeDisplayed(uint32_t addr);
/* the CPU reading a colour image at addr */
void RiceFB_FrameBufferReadByCPU(uint32_t addr);
/* the render textures RDRAM no longer holds dropped */
void RiceFB_CheckRenderTextureCRCInRDRAM(void);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern RenderTextureInfo g_ZI_saves[2];
#endif

#endif

