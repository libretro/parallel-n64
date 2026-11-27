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

/* The renderer's state and what it asks of GL for it (RiceRender.h).
   C89. */
#include <stdint.h>
#include <string.h>

#include "osal_opengl.h"

#include "m64p_plugin.h"
#include "Config.h"
#include "FrameBuffer.h"
#include "GraphicsContext.h"
#include "RenderBase.h"
#include "RiceCombiner.h"
#include "RiceGLDraw.h"
#include "RiceGLState.h"
#include "RiceGLTex.h"
#include "RiceRender.h"
#include "RSP_Parser.h"
#include "Video.h"

RiceRenderState rice_render;

void RiceRender_Reset(void)
{
    rice_render.zBias = 0;
    rice_render.alpha = 0xFF;
}

void RiceRender_Initialize(void)
{
    RiceGL_ViewportDisplay();
    RiceGL_InitVertexArrays();
    RiceGLTex_Init();
}

void RiceRender_SetZBias(int bias)
{
    rice_render.zBias = bias;
    RiceGL_ApplyZBias(bias);
}

void RiceRender_SetAlphaRef(uint32_t alpha)
{
    rice_render.alpha = alpha;
}

void RiceRender_SetFogEnable(bool enable)
{
    /* the ROM's fog, or fog forced on */
    gRSP.bFogEnabled = enable || options.fogMethod == 2;
    rice_combiner.fog = gRSP.bFogEnabled;
}

void RiceRender_SetCurrentTexture(int tile, CTexture *handler,
      uint32_t width, uint32_t height, TxtrCacheEntry *entry)
{
    RenderTexture *texture = &g_textures[tile];
    texture->pTextureEntry = entry;

    if (handler != NULL)
    {
        LPRICETEXTURE tex = CTexture_GetTexture(handler);
        if (texture->m_lpsTexturePtr != tex)
        {
            /* an enhanced texture is drawn at the size of the entry's */
            const CTexture *sized = handler->m_bIsEnhancedTexture
                  ? entry->pTexture : handler;

            texture->m_pCTexture     = handler;
            texture->m_lpsTexturePtr = tex;
            texture->m_dwTileWidth   = width;
            texture->m_dwTileHeight  = height;
            texture->m_fTexWidth     = (float)sized->m_dwCreatedTextureWidth;
            texture->m_fTexHeight    = (float)sized->m_dwCreatedTextureHeight;
        }
    }
}

void RiceRender_SetCurrentTextureEntry(int tile, TxtrCacheEntry *entry)
{
    if (entry != NULL && entry->pTexture != NULL)
        RiceRender_SetCurrentTexture(tile, entry->pTexture,
              entry->ti.WidthToCreate, entry->ti.HeightToCreate, entry);
    else
        RiceRender_SetCurrentTexture(tile, NULL, 64, 64, NULL);
}

/* both matrix stacks down to the identity */
static void rr_reset_matrices(void)
{
    XMATRIX mat;

    memset(&mat, 0, sizeof(mat));
    mat.m[0][0] = mat.m[1][1] = mat.m[2][2] = mat.m[3][3] = 1.0f;

    gRSP.projectionMtxTop  = 0;
    gRSP.modelViewMtxTop   = 0;
    gRSP.projectionMtxs[0] = mat;
    gRSP.modelviewMtxs[0]  = mat;

    gRSP.bMatrixIsUpdated      = true;
    gRSP.bWorldMatrixIsUpdated = true;
    UpdateCombinedMatrix();
}

void RiceRender_RenderReset(void)
{
    RiceGL_UpdateClipRectangle();
    rr_reset_matrices();
    RiceRender_SetZBias(0);
    gRSP.numVertices = 0;
    gRSP.maxVertexID = 0;
    gRSP.curTile     = 0;
    gRSP.fTexScaleX  = 1/32.0f;
    gRSP.fTexScaleY  = 1/32.0f;
}

void RiceRender_UpdateScissor(void)
{
    if (!RiceGL_RE2Scissor())
        RiceGL_UpdateScissorWithClipRatio();
}

/* a corner of the simple rectangle, in window pixels */
static void rr_corner(TLITVERTEX *v, float x, float y, float u, float tv,
      COLOR dif, COLOR spe, float z, float rhw)
{
    v->x          = x * windowSetting.fMultX;
    v->y          = y * windowSetting.fMultY;
    v->z          = z;
    v->rhw        = rhw;
    v->dcDiffuse  = dif;
    v->dcSpecular = spe;
    v->tcord[0].u = u;
    v->tcord[0].v = tv;
}

void RiceRender_DrawSimple2DTexture(float x0, float y0, float x1, float y1,
      float u0, float v0, float u1, float v1, COLOR dif, COLOR spe,
      float z, float rhw)
{
    const TxtrCacheEntry *entry = g_textures[0].pTextureEntry;

    if (status.bVIOriginIsUpdated == true
          && currentRomOptions.screenUpdateSetting == SCREEN_UPDATE_AT_1ST_PRIMITIVE)
    {
        status.bVIOriginIsUpdated = false;
        RiceGC_UpdateFrame();
    }

    /* a render texture's coordinates go by its scale */
    if (entry && entry->txtrBufIdx > 0)
    {
        const RenderTextureInfo *info = &gRenderTextureInfos[entry->txtrBufIdx - 1];
        u0 *= info->scaleX;
        u1 *= info->scaleX;
        v0 *= info->scaleY;
        v1 *= info->scaleY;
    }

    /* clockwise from (x0, y0) */
    rr_corner(&g_texRectTVtx[0], x0, y0, u0, v0, dif, spe, z, rhw);
    rr_corner(&g_texRectTVtx[1], x1, y0, u1, v0, dif, spe, z, rhw);
    rr_corner(&g_texRectTVtx[2], x1, y1, u1, v1, dif, spe, z, rhw);
    rr_corner(&g_texRectTVtx[3], x0, y1, u0, v1, dif, spe, z, rhw);

    RiceGL_DrawSimple2DTexture();
}
