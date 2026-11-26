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

#include "OGLES2FragmentShaders.h"
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
    glViewportWrapper(0, 0, windowSetting.uDisplayWidth, windowSetting.uDisplayHeight, true);


    glVertexAttribPointer(VS_POSITION,4,GL_FLOAT,GL_FALSE,sizeof(float)*5,&(g_vtxProjected5[0][0]));


    glVertexAttribPointer(VS_TEXCOORD0,2,GL_FLOAT,GL_FALSE, sizeof( TLITVERTEX ), &(g_vtxBuffer[0].tcord[0].u));
    glVertexAttribPointer(VS_TEXCOORD1,2,GL_FLOAT,GL_FALSE, sizeof( TLITVERTEX ), &(g_vtxBuffer[0].tcord[1].u));

    glVertexAttribPointer(VS_FOG,1,GL_FLOAT,GL_FALSE,sizeof(float)*5,&(g_vtxProjected5[0][4]));

    glVertexAttribPointer(VS_COLOR, 4, GL_UNSIGNED_BYTE,GL_TRUE, sizeof(uint8_t)*4, &(g_oglVtxColors[0][0]) );

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

bool CRender::RenderTexRect()
{
    glViewportWrapper(0, 0, windowSetting.uDisplayWidth, windowSetting.uDisplayHeight, true);

    GLboolean cullface = glIsEnabled(GL_CULL_FACE);
    glDisable(GL_CULL_FACE);

    float depth = -(g_texRectTVtx[3].z*2-1);

    GLfloat colour[] = {
            (GLfloat)g_texRectTVtx[3].r, (GLfloat)g_texRectTVtx[3].g, (GLfloat)g_texRectTVtx[3].b, (GLfloat)g_texRectTVtx[3].a,
            (GLfloat)g_texRectTVtx[2].r, (GLfloat)g_texRectTVtx[2].g, (GLfloat)g_texRectTVtx[2].b, (GLfloat)g_texRectTVtx[2].a,
            (GLfloat)g_texRectTVtx[1].r, (GLfloat)g_texRectTVtx[1].g, (GLfloat)g_texRectTVtx[1].b, (GLfloat)g_texRectTVtx[1].a,
            (GLfloat)g_texRectTVtx[0].r, (GLfloat)g_texRectTVtx[0].g, (GLfloat)g_texRectTVtx[0].b, (GLfloat)g_texRectTVtx[0].a
    };

    GLfloat tex[] = {
            g_texRectTVtx[3].tcord[0].u,g_texRectTVtx[3].tcord[0].v,
            g_texRectTVtx[2].tcord[0].u,g_texRectTVtx[2].tcord[0].v,
            g_texRectTVtx[1].tcord[0].u,g_texRectTVtx[1].tcord[0].v,
            g_texRectTVtx[0].tcord[0].u,g_texRectTVtx[0].tcord[0].v
    };

    GLfloat tex2[] = {
       g_texRectTVtx[3].tcord[1].u,g_texRectTVtx[3].tcord[1].v,
       g_texRectTVtx[2].tcord[1].u,g_texRectTVtx[2].tcord[1].v,
       g_texRectTVtx[1].tcord[1].u,g_texRectTVtx[1].tcord[1].v,
       g_texRectTVtx[0].tcord[1].u,g_texRectTVtx[0].tcord[1].v

    };

    float w = windowSetting.uDisplayWidth / 2.0f, h = windowSetting.uDisplayHeight / 2.0f, inv = 1.0f;

    GLfloat vertices[] = {
            -inv + g_texRectTVtx[3].x / w, inv - g_texRectTVtx[3].y / h, depth, 1,
            -inv + g_texRectTVtx[2].x / w, inv - g_texRectTVtx[2].y / h, depth, 1,
            -inv + g_texRectTVtx[1].x / w, inv - g_texRectTVtx[1].y / h, depth, 1,
            -inv + g_texRectTVtx[0].x / w, inv - g_texRectTVtx[0].y / h, depth, 1
    };

    glEnableVertexAttribArray(VS_COLOR);
    glVertexAttribPointer(VS_COLOR, 4, GL_FLOAT,GL_TRUE, 0, &colour );
    glEnableVertexAttribArray(VS_POSITION);
    glVertexAttribPointer(VS_POSITION,4,GL_FLOAT,GL_FALSE,0,&vertices);
    glEnableVertexAttribArray(VS_TEXCOORD0);
    glVertexAttribPointer(VS_TEXCOORD0,2,GL_FLOAT,GL_FALSE, 0, &tex);
    glEnableVertexAttribArray(VS_TEXCOORD1);
    glVertexAttribPointer(VS_TEXCOORD1,2,GL_FLOAT,GL_FALSE, 0, &tex2);
    glDrawArrays(GL_TRIANGLE_FAN,0,4);

    //Restore old pointers
    glVertexAttribPointer(VS_COLOR, 4, GL_UNSIGNED_BYTE,GL_TRUE, sizeof(uint8_t)*4, &(g_oglVtxColors[0][0]) );
    glVertexAttribPointer(VS_POSITION,4,GL_FLOAT,GL_FALSE,sizeof(float)*5,&(g_vtxProjected5[0][0]));
    glVertexAttribPointer(VS_TEXCOORD0,2,GL_FLOAT,GL_FALSE, sizeof( TLITVERTEX ), &(g_vtxBuffer[0].tcord[0].u));
    glVertexAttribPointer(VS_TEXCOORD1,2,GL_FLOAT,GL_FALSE, sizeof( TLITVERTEX ), &(g_vtxBuffer[0].tcord[1].u));

    if( cullface ) glEnable(GL_CULL_FACE);

    return true;
}

bool CRender::RenderFillRect(uint32_t dwColor, float depth)
{
    float a = (dwColor>>24)/255.0f;
    float r = ((dwColor>>16)&0xFF)/255.0f;
    float g = ((dwColor>>8)&0xFF)/255.0f;
    float b = (dwColor&0xFF)/255.0f;
    glViewportWrapper(0, 0, windowSetting.uDisplayWidth, windowSetting.uDisplayHeight, true);

    GLboolean cullface = glIsEnabled(GL_CULL_FACE);
    glDisable(GL_CULL_FACE);

    GLfloat colour[] = {
            r,g,b,a,
            r,g,b,a,
            r,g,b,a,
            r,g,b,a};

    float w = windowSetting.uDisplayWidth / 2.0f, h = windowSetting.uDisplayHeight / 2.0f, inv = 1.0f;

    GLfloat vertices[] = {
            -inv + m_fillRectVtx[0].x / w, inv - m_fillRectVtx[1].y / h, depth, 1,
            -inv + m_fillRectVtx[1].x / w, inv - m_fillRectVtx[1].y / h, depth, 1,
            -inv + m_fillRectVtx[1].x / w, inv - m_fillRectVtx[0].y / h, depth, 1,
            -inv + m_fillRectVtx[0].x / w, inv - m_fillRectVtx[0].y / h, depth, 1
    };

    glEnableVertexAttribArray(VS_COLOR);
    glVertexAttribPointer(VS_COLOR, 4, GL_FLOAT,GL_FALSE, 0, &colour );
    glEnableVertexAttribArray(VS_POSITION);
    glVertexAttribPointer(VS_POSITION,4,GL_FLOAT,GL_FALSE,0,&vertices);
    glDisableVertexAttribArray(VS_TEXCOORD0);
    glDisableVertexAttribArray(VS_TEXCOORD1);

    glDrawArrays(GL_TRIANGLE_FAN,0,4);

    //Restore old pointers
    glVertexAttribPointer(VS_COLOR, 4, GL_UNSIGNED_BYTE,GL_TRUE, sizeof(uint8_t)*4, &(g_oglVtxColors[0][0]) );
    glVertexAttribPointer(VS_POSITION,4,GL_FLOAT,GL_FALSE,sizeof(float)*5,&(g_vtxProjected5[0][0]));
    glEnableVertexAttribArray(VS_TEXCOORD0);
    glEnableVertexAttribArray(VS_TEXCOORD1);

    if( cullface ) glEnable(GL_CULL_FACE);

    return true;
}

bool CRender::RenderLine3D()
{
    return true;
}

extern FiddledVtx * g_pVtxBase;

// This is so weird that I can not do vertex transform by myself. I have to use
// OpenGL internal transform
bool CRender::RenderFlushTris()
{
   if( !gRDP.bFogEnableInBlender && gRSP.bFogEnabled )
   {
      TurnFogOnOff(false);
   }

    ApplyZBias(m_dwZBias);  // set the bias factors

    glViewportWrapper(windowSetting.vpLeftW, windowSetting.uDisplayHeight - windowSetting.vpTopW - windowSetting.vpHeightW, windowSetting.vpWidthW, windowSetting.vpHeightW, false);

    /* Enable and point the vertex attribute arrays for the geometry draw. They
     * are otherwise only set up on a shader-combiner change, but the libretro
     * frontend resets GL state each frame, so on a frame where the combiner did
     * not change they would be left disabled and the draw rejected. */
    glEnableVertexAttribArray(VS_POSITION);
    glVertexAttribPointer(VS_POSITION,4,GL_FLOAT,GL_FALSE,sizeof(float)*5,&(g_vtxProjected5[0][0]));
    glEnableVertexAttribArray(VS_COLOR);
    glVertexAttribPointer(VS_COLOR,4,GL_UNSIGNED_BYTE,GL_TRUE,sizeof(uint8_t)*4,&(g_oglVtxColors[0][0]));
    glEnableVertexAttribArray(VS_TEXCOORD0);
    glVertexAttribPointer(VS_TEXCOORD0,2,GL_FLOAT,GL_FALSE,sizeof(TLITVERTEX),&(g_vtxBuffer[0].tcord[0].u));
    glEnableVertexAttribArray(VS_TEXCOORD1);
    glVertexAttribPointer(VS_TEXCOORD1,2,GL_FLOAT,GL_FALSE,sizeof(TLITVERTEX),&(g_vtxBuffer[0].tcord[1].u));
    glEnableVertexAttribArray(VS_FOG);
    glVertexAttribPointer(VS_FOG,1,GL_FLOAT,GL_FALSE,sizeof(float)*5,&(g_vtxProjected5[0][4]));

    //if options.bOGLVertexClipper == false )
    {
        glDrawElements( GL_TRIANGLES, gRSP.numVertices, GL_UNSIGNED_SHORT, g_vtxIndex );
    }

    /* Fog is only used by this geometry draw. Disable it here so it is not left
     * enabled -- still pointing at g_vtxProjected5 with a 5-float stride -- for
     * the rect/blit draws (DrawSimple2DTexture, the TRIANGLE_FANs), which use a
     * different vertex layout and would read fog out of bounds and crash. */
    glDisableVertexAttribArray(VS_FOG);

    if( !gRDP.bFogEnableInBlender && gRSP.bFogEnabled )
    {
       TurnFogOnOff(true);
    }
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

    GLboolean cullface = glIsEnabled(GL_CULL_FACE);
    glDisable(GL_CULL_FACE);

    glViewportWrapper(0, 0, windowSetting.uDisplayWidth, windowSetting.uDisplayHeight, true);

    float a = (g_texRectTVtx[0].dcDiffuse >>24)/255.0f;
    float r = ((g_texRectTVtx[0].dcDiffuse>>16)&0xFF)/255.0f;
    float g = ((g_texRectTVtx[0].dcDiffuse>>8)&0xFF)/255.0f;
    float b = (g_texRectTVtx[0].dcDiffuse&0xFF)/255.0f;

    GLfloat colour[] = {
            r,g,b,a,
            r,g,b,a,
            r,g,b,a,
            r,g,b,a,
            r,g,b,a,
            r,g,b,a
    };

    GLfloat tex[] = {
            g_texRectTVtx[0].tcord[0].u,g_texRectTVtx[0].tcord[0].v,
            g_texRectTVtx[1].tcord[0].u,g_texRectTVtx[1].tcord[0].v,
            g_texRectTVtx[2].tcord[0].u,g_texRectTVtx[2].tcord[0].v,

            g_texRectTVtx[0].tcord[0].u,g_texRectTVtx[0].tcord[0].v,
            g_texRectTVtx[2].tcord[0].u,g_texRectTVtx[2].tcord[0].v,
            g_texRectTVtx[3].tcord[0].u,g_texRectTVtx[3].tcord[0].v,
    };

    GLfloat tex2[] = {
       g_texRectTVtx[0].tcord[1].u,g_texRectTVtx[0].tcord[1].v,
       g_texRectTVtx[1].tcord[1].u,g_texRectTVtx[1].tcord[1].v,
       g_texRectTVtx[2].tcord[1].u,g_texRectTVtx[2].tcord[1].v,

       g_texRectTVtx[0].tcord[1].u,g_texRectTVtx[0].tcord[1].v,
       g_texRectTVtx[2].tcord[1].u,g_texRectTVtx[2].tcord[1].v,
       g_texRectTVtx[3].tcord[1].u,g_texRectTVtx[3].tcord[1].v,

    };

     float w = windowSetting.uDisplayWidth / 2.0f, h = windowSetting.uDisplayHeight / 2.0f, inv = 1.0f;

    GLfloat vertices[] = {
            -inv + g_texRectTVtx[0].x/ w, inv - g_texRectTVtx[0].y/ h, -g_texRectTVtx[0].z,1,
            -inv + g_texRectTVtx[1].x/ w, inv - g_texRectTVtx[1].y/ h, -g_texRectTVtx[1].z,1,
            -inv + g_texRectTVtx[2].x/ w, inv - g_texRectTVtx[2].y/ h, -g_texRectTVtx[2].z,1,

            -inv + g_texRectTVtx[0].x/ w, inv - g_texRectTVtx[0].y/ h, -g_texRectTVtx[0].z,1,
            -inv + g_texRectTVtx[2].x/ w, inv - g_texRectTVtx[2].y/ h, -g_texRectTVtx[2].z,1,
            -inv + g_texRectTVtx[3].x/ w, inv - g_texRectTVtx[3].y/ h, -g_texRectTVtx[3].z,1
    };

    glEnableVertexAttribArray(VS_COLOR);
    glVertexAttribPointer(VS_COLOR, 4, GL_FLOAT,GL_FALSE, 0, &colour );
    glEnableVertexAttribArray(VS_POSITION);
    glVertexAttribPointer(VS_POSITION,4,GL_FLOAT,GL_FALSE,0,&vertices);
    glDisableVertexAttribArray(VS_FOG);
    glEnableVertexAttribArray(VS_TEXCOORD0);
    glVertexAttribPointer(VS_TEXCOORD0,2,GL_FLOAT,GL_FALSE, 0, &tex);
    glEnableVertexAttribArray(VS_TEXCOORD1);
    glVertexAttribPointer(VS_TEXCOORD1,2,GL_FLOAT,GL_FALSE, 0, &tex2);
    glDrawArrays(GL_TRIANGLES,0,6);

    //Restore old pointers
    glVertexAttribPointer(VS_COLOR, 4, GL_UNSIGNED_BYTE,GL_TRUE, sizeof(uint8_t)*4, &(g_oglVtxColors[0][0]) );
    glVertexAttribPointer(VS_POSITION,4,GL_FLOAT,GL_FALSE,sizeof(float)*5,&(g_vtxProjected5[0][0]));
    glVertexAttribPointer(VS_TEXCOORD0,2,GL_FLOAT,GL_FALSE, sizeof( TLITVERTEX ), &(g_vtxBuffer[0].tcord[0].u));
    glVertexAttribPointer(VS_TEXCOORD1,2,GL_FLOAT,GL_FALSE, sizeof( TLITVERTEX ), &(g_vtxBuffer[0].tcord[1].u));

    if( cullface ) glEnable(GL_CULL_FACE);
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

void CRender::SetViewportRender()
{
    glViewportWrapper(windowSetting.vpLeftW, windowSetting.uDisplayHeight - windowSetting.vpTopW - windowSetting.vpHeightW, windowSetting.vpWidthW, windowSetting.vpHeightW, true);
}

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

void CRender::SetAlphaTestEnable(bool bAlphaTestEnable)
{
        COGL_FragmentProgramCombiner* frag = (COGL_FragmentProgramCombiner*)m_pColorCombiner;
        frag->SetAlphaTestState(bAlphaTestEnable);
}

void CRender::UpdateScissor()
{
    if (!RiceGL_RE2Scissor())
        UpdateScissorWithClipRatio();
}

void CRender::TurnFogOnOff(bool flag)
{
    ((COGL_FragmentProgramCombiner*)m_pColorCombiner)->SetFogState(flag);
}

void CRender::SetFogEnable(bool bEnable)
{
    gRSP.bFogEnabled = bEnable;
    
    // If force fog
    if(options.fogMethod == 2)
        gRSP.bFogEnabled = true;

    ((COGL_FragmentProgramCombiner*)m_pColorCombiner)->SetFogState(gRSP.bFogEnabled);
}

void CRender::EndRendering(void)
{
    if( CRender::gRenderReferenceCount > 0 ) 
        CRender::gRenderReferenceCount--;
}

void CRender::glViewportWrapper(GLint x, GLint y, GLsizei width, GLsizei height, bool flag)
{
    /* Always set the viewport. Caching it here is unsafe: glsm re-applies its
     * own tracked viewport every frame, so a stale local cache would let rice
     * skip a needed glViewport and render into the wrong region. The glsm
     * wrapper still avoids a redundant GL call when nothing actually changed. */
    glViewport(x, y, width, height);
}

