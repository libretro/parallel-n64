/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - OGLCombiner.cpp                                         *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
 *   Copyright (C) 2003 Rice1964                                           *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.          *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "osal_opengl.h"

#include "OGLCombiner.h"
#include "OGLDebug.h"
#include "OGLRender.h"
#include "OGLGraphicsContext.h"
#include "OGLDecodedMux.h"
#include "Texture.h"

//========================================================================
COGLColorCombiner::COGLColorCombiner(CRender *pRender) :
    CColorCombiner(pRender),
    m_pOGLRender((OGLRender*)pRender),
    m_bSupportAdd(false), m_bSupportSubtract(false)
{
    m_pDecodedMux = new COGLDecodedMux;
    m_pDecodedMux->m_maxConstants = 0;
    m_pDecodedMux->m_maxTextures = 1;
}

COGLColorCombiner::~COGLColorCombiner()
{
    delete m_pDecodedMux;
    m_pDecodedMux = NULL;
}

bool COGLColorCombiner::Initialize(void)
{
    m_bSupportAdd = false;
    m_bSupportSubtract = false;
    m_supportedStages = 1;
    m_bSupportMultiTexture = false;

    COGLGraphicsContext *pcontext = (COGLGraphicsContext *)(CGraphicsContext::g_pGraphicsContext);
    if( pcontext->IsExtensionSupported(OSAL_GL_ARB_TEXTURE_ENV_ADD) || pcontext->IsExtensionSupported("GL_EXT_texture_env_add") )
    {
        m_bSupportAdd = true;
    }

    if( pcontext->IsExtensionSupported("GL_EXT_blend_subtract") )
    {
        m_bSupportSubtract = true;
    }

    return true;
}

void COGLColorCombiner::DisableCombiner(void)
{
    m_pOGLRender->DisableMultiTexture();
    glEnable(GL_BLEND);
    OPENGL_CHECK_ERRORS;
    glBlendFunc(GL_ONE, GL_ZERO);
    OPENGL_CHECK_ERRORS;
    
    if( m_bTexelsEnable )
    {
        CTexture* pTexture = g_textures[gRSP.curTile].m_pCTexture;
        if( pTexture ) 
        {
            m_pOGLRender->EnableTexUnit(0,TRUE);
            m_pOGLRender->BindTexture(pTexture->m_dwTextureName, 0);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
            OPENGL_CHECK_ERRORS;
            m_pOGLRender->SetAllTexelRepeatFlag();
        }
#ifdef DEBUGGER
        else
        {
            DebuggerAppendMsg("Check me, texture is NULL but it is enabled");
        }
#endif
    }
    else
    {
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        OPENGL_CHECK_ERRORS;
        m_pOGLRender->EnableTexUnit(0,FALSE);
    }
}

void COGLColorCombiner::InitCombinerCycleCopy(void)
{
    m_pOGLRender->DisableMultiTexture();
    m_pOGLRender->EnableTexUnit(0,TRUE);
    CTexture* pTexture = g_textures[gRSP.curTile].m_pCTexture;
    if( pTexture )
    {
        m_pOGLRender->BindTexture(pTexture->m_dwTextureName, 0);
        m_pOGLRender->SetTexelRepeatFlags(gRSP.curTile);
    }
#ifdef DEBUGGER
    else
    {
        DebuggerAppendMsg("Check me, texture is NULL");
    }
#endif

    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    OPENGL_CHECK_ERRORS;
}

void COGLColorCombiner::InitCombinerCycleFill(void)
{
    m_pOGLRender->DisableMultiTexture();
    m_pOGLRender->EnableTexUnit(0,FALSE);
}


void COGLColorCombiner::InitCombinerCycle12(void)
{
    m_pOGLRender->DisableMultiTexture();
    if( !m_bTexelsEnable )
    {
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        OPENGL_CHECK_ERRORS;
        m_pOGLRender->EnableTexUnit(0,FALSE);
        return;
    }
}

void COGLColorCombiner::InitCombinerBlenderForSimpleTextureDraw(uint32 tile)
{
    m_pOGLRender->DisableMultiTexture();
    if( g_textures[tile].m_pCTexture )
    {
        m_pOGLRender->EnableTexUnit(0,TRUE);
        glBindTexture(GL_TEXTURE_2D, ((CTexture*)(g_textures[tile].m_pCTexture))->m_dwTextureName);
        OPENGL_CHECK_ERRORS;
    }
    m_pOGLRender->SetAllTexelRepeatFlag();

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    OPENGL_CHECK_ERRORS;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    OPENGL_CHECK_ERRORS;
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR); // Linear Filtering
    OPENGL_CHECK_ERRORS;
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR); // Linear Filtering
    OPENGL_CHECK_ERRORS;

    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    OPENGL_CHECK_ERRORS;
    m_pOGLRender->SetAlphaTestEnable(FALSE);
}

#ifdef DEBUGGER
extern const char *translatedCombTypes[];
void COGLColorCombiner::DisplaySimpleMuxString(void)
{
    TRACE0("\nSimplified Mux\n");
    m_pDecodedMux->DisplaySimpliedMuxString("Used");
}
#endif

