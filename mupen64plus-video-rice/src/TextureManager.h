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

/* Rice's texture cache: a texture as the RDP loads it, converted once and
   kept while its texels in RDRAM (and palette) are the same. C89, for C
   and C++ alike. */
#ifndef __TEXTUREHANDLER_H__
#define __TEXTUREHANDLER_H__

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <boolean.h>

#include "../../Graphics/RDP/gDP_state.h"
#include "typedefs.h"
#include "Texture.h"
#include "TxtrInfo.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TxtrCacheEntry
{
    struct TxtrCacheEntry *pNext;   /* the next in its hash bucket */

    TxtrInfo  ti;
    uint32_t  dwCRC;                /* of its texels in RDRAM */
    uint32_t  dwPalCRC;             /* of its palette's entries it uses */
    int       maxCI;                /* the highest index it uses, -1 not yet known */

    uint32_t  dwTimeLastUsed;       /* status.gRDPTime when last used */
    uint32_t  FrameLastUsed;        /* status.gDlistCount when last used */

    CTexture *pTexture;
    int       txtrBufIdx;
} TxtrCacheEntry;

/* the texture for @pgti: from the cache if its texels (and palette) are
   the same, converted otherwise; NULL where none could be made */
TxtrCacheEntry *TextureManager_GetTexture(TxtrInfo *pgti, bool fromTMEM, bool doCRCCheck, bool AutoExtendTexture);
/* 4x4 textures of a colour: black, and the primitive, environment, LOD
   fraction or primitive LOD fraction colour (MUX_PRIM, MUX_ENV,
   MUX_LODFRAC, MUX_PRIMLODFRAC) */
TxtrCacheEntry *TextureManager_GetBlackTexture(void);
TxtrCacheEntry *TextureManager_GetConstantColorTexture(uint32_t constant);
/* textures unused five seconds and not bound let go */
void TextureManager_PurgeOldTextures(void);
/* every cached texture let go; and the colour textures too */
void TextureManager_RecycleAllTextures(void);
bool TextureManager_CleanUp(void);

/* a CRC of texels in RDRAM, and the highest palette index among them; 0,
   and every index, where they are not all in RDRAM */
uint32_t CalculateRDRAMCRC(void *pPhysicalAddress, uint32_t left, uint32_t top, uint32_t width, uint32_t height,
      uint32_t size, uint32_t pitchInBytes);
unsigned char CalculateMaxCI(void *pPhysicalAddress, uint32_t left, uint32_t top, uint32_t width, uint32_t height,
      uint32_t size, uint32_t pitchInBytes);

/* what the cache asks of the rest of Rice (FrameBuffer.cpp,
   DeviceBuilder.cpp) */
int  RiceFB_CheckAddrInRenderTextures(uint32_t addr);
int  RiceFB_CheckAddrInBackBuffers(uint32_t addr, uint32_t memsize);
/* render texture @idx's entry, @ti its texture's, if it is of @ti's format
   and size; NULL otherwise */
TxtrCacheEntry *RiceFB_RenderTextureEntry(int idx, const TxtrInfo *ti);
uint32_t RiceFB_RenderTextureCRC(int idx);
uint32_t RiceFB_RenderTextureUpdatedAt(int idx);
void RiceFB_LoadTextureFromRenderTexture(TxtrCacheEntry *pEntry, int idx);
/* the format of the colour image being drawn to */
uint32_t RiceFB_CIFormat(void);
CTexture *RiceDevice_CreateTexture(uint32_t width, uint32_t height);

#ifdef __cplusplus
}
#endif

#endif
