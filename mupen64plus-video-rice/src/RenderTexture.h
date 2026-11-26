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

/* A texture Rice draws the N64's colour image into, or saves a back buffer
   in, and what it knows of it. C89, for C and C++ alike. */
#ifndef _TEXTURE_BUFFER_H_
#define _TEXTURE_BUFFER_H_

#include "typedefs.h"
#include "TextureManager.h"

#ifdef __cplusplus
extern "C" {
#endif

struct CRenderTexture;

typedef struct {
    struct CRenderTexture *pRenderTexture;
    SetImgInfo  CI_Info;

    uint32_t      bufferWidth;
    uint32_t      bufferHeight;
    uint32_t      N64Width;
    uint32_t      N64Height;
    float       scaleX;
    float       scaleY;

    int         maxUsedHeight;
    uint32_t      updateAtFrame;
    uint32_t      updateAtUcodeCount;

    bool        isUsed;
    uint32_t      knownHeight;

    uint32_t      crcInRDRAM;
    uint32_t      crcCheckedAtFrame;

    TxtrCacheEntry txtEntry;
} RenderTextureInfo;

/* the renderer's part: drawn into, read from, written back */
typedef struct CRenderTextureOps
{
    bool (*set_as_render_target)(struct CRenderTexture *rt, bool enable);
    void (*load_texture)(struct CRenderTexture *rt, TxtrCacheEntry *pEntry);
    void (*store_to_rdram)(struct CRenderTexture *rt, int infoIdx);
} CRenderTextureOps;

typedef struct CRenderTexture
{
    const CRenderTextureOps *ops;
    int                m_width;
    int                m_height;
    bool               m_beingRendered;
    TextureUsage       m_usage;
    CTexture          *m_pTexture;   /* a back buffer saved's; none drawn into */
    RenderTextureInfo *m_pInfo;
} CRenderTexture;

/* GL's (drawing into one is not done: only a back buffer saved has a
   texture) */
CRenderTexture *CRenderTexture_New(int width, int height, RenderTextureInfo *pInfo, TextureUsage usage);
void CRenderTexture_Delete(CRenderTexture *rt);

#define CRenderTexture_IsBeingRendered(rt)            ((rt)->m_beingRendered)
#define CRenderTexture_SetAsRenderTarget(rt, enable)  ((rt)->ops->set_as_render_target((rt), (enable)))
#define CRenderTexture_LoadTexture(rt, entry)         ((rt)->ops->load_texture((rt), (entry)))
#define CRenderTexture_StoreToRDRAM(rt, idx)          ((rt)->ops->store_to_rdram((rt), (idx)))

/* FrameBuffer.c's: the texture being drawn into closed */
void RiceFB_CloseRenderTexture(bool toSave);

#ifdef __cplusplus
}
#endif

#endif
