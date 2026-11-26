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

#include "osal_opengl.h"

#include "Render.h"
#include "GraphicsContext.h"
#include "OGLTexture.h"
#include "TextureManager.h"

// FIXME: Use OGL internal L/T and matrix stack
// FIXME: Use OGL lookupAt function
// FIXME: Use OGL DisplayList

//===================================================================
bool CRender::InitDeviceObjects()
{
    // enable Z-buffer by default
    ZBufferEnable(true);
    return true;
}

bool CRender::ClearDeviceObjects()
{
    return true;
}

void CRender::Initialize(void)
{
    RiceGL_ViewportDisplay();
    RiceGL_InitVertexArrays();
    RiceGLTex_Init();
}
//===================================================================
void CRender::SetZBias(int bias)
{
    // set member variable and apply the setting in opengl
    m_dwZBias = bias;
    ApplyZBias(bias);
}

void CRender::SetAlphaRef(uint32_t dwAlpha)
{
    if (m_dwAlpha != dwAlpha)
    {
        m_dwAlpha = dwAlpha;
    }
}

void CRender::ForceAlphaRef(uint32_t dwAlpha)
{
    m_dwAlpha = dwAlpha;
}

bool CRender::SetCurrentTexture(int tile, CTexture *handler,uint32_t dwTileWidth, uint32_t dwTileHeight, TxtrCacheEntry *pTextureEntry)
{
    RenderTexture &texture = g_textures[tile];
    texture.pTextureEntry = pTextureEntry;

    if( handler!= NULL  && texture.m_lpsTexturePtr != CTexture_GetTexture(handler) )
    {
        texture.m_pCTexture = handler;
        texture.m_lpsTexturePtr = CTexture_GetTexture(handler);

        texture.m_dwTileWidth = dwTileWidth;
        texture.m_dwTileHeight = dwTileHeight;

        if( handler->m_bIsEnhancedTexture )
        {
            texture.m_fTexWidth = (float)pTextureEntry->pTexture->m_dwCreatedTextureWidth;
            texture.m_fTexHeight = (float)pTextureEntry->pTexture->m_dwCreatedTextureHeight;
        }
        else
        {
            texture.m_fTexWidth = (float)handler->m_dwCreatedTextureWidth;
            texture.m_fTexHeight = (float)handler->m_dwCreatedTextureHeight;
        }
    }
    
    return true;
}

bool CRender::SetCurrentTexture(int tile, TxtrCacheEntry *pEntry)
{
    if (pEntry != NULL && pEntry->pTexture != NULL)
    {   
        SetCurrentTexture( tile, pEntry->pTexture,  pEntry->ti.WidthToCreate, pEntry->ti.HeightToCreate, pEntry);
        return true;
    }
    else
    {
        SetCurrentTexture( tile, NULL, 64, 64, NULL );
        return false;
    }
    return true;
}

// Basic render drawing functions

bool CRender::RenderLine3D()
{
    return true;
}

void CRender::DrawSimple2DTexture(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, COLOR dif, COLOR spe, float z, float rhw)
{
    if( status.bVIOriginIsUpdated == true && currentRomOptions.screenUpdateSetting==SCREEN_UPDATE_AT_1ST_PRIMITIVE )
    {
        status.bVIOriginIsUpdated=false;
        RiceGC_UpdateFrame();
    }

    StartDrawSimple2DTexture(x0, y0, x1, y1, u0, v0, u1, v1, dif, spe, z, rhw);

    RiceGL_DrawSimple2DTexture();
}

#if 0
void CRender::InitCombinerBlenderForSimpleRectDraw(uint32_t tile)
{
    //glEnable(GL_CULL_FACE);
    EnableTexUnit(0, false);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    //glEnable(GL_ALPHA_TEST);
}
#endif

void CRender::RenderReset()
{
    UpdateClipRectangle();
    ResetMatrices();
    SetZBias(0);
    gRSP.numVertices = 0;
    gRSP.maxVertexID = 0;
    gRSP.curTile = 0;
    gRSP.fTexScaleX = 1/32.0f;;
    gRSP.fTexScaleY = 1/32.0f;
}

void CRender::UpdateScissor()
{
    if (!RiceGL_RE2Scissor())
        UpdateScissorWithClipRatio();
}

void CRender::SetFogEnable(bool bEnable)
{
    gRSP.bFogEnabled = bEnable;
    
    // If force fog
    if(options.fogMethod == 2)
        gRSP.bFogEnabled = true;

    rice_combiner.fog = gRSP.bFogEnabled;
}

void CRender::EndRendering(void)
{
    if( CRender::gRenderReferenceCount > 0 ) 
        CRender::gRenderReferenceCount--;
}
