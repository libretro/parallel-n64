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

#include <stdlib.h>
#include "osal_opengl.h"

#include "DeviceBuilder.h"
#include "FrameBuffer.h"
#include "OGLBlender.h"
#include "OGLExtRender.h"
#include "GraphicsContext.h"
#include "OGLTexture.h"
#include "OGLES2FragmentShaders.h"

//========================================================================
CDeviceBuilder* CDeviceBuilder::m_pInstance=NULL;
SupportedDeviceType CDeviceBuilder::m_deviceType = DIRECTX_DEVICE;
SupportedDeviceType CDeviceBuilder::m_deviceGeneralType = DIRECTX_DEVICE;

CDeviceBuilder* CDeviceBuilder::GetBuilder(void)
{
    if( m_pInstance == NULL )
        CreateBuilder(m_deviceType);
    
    return m_pInstance;
}

void CDeviceBuilder::SelectDeviceType(SupportedDeviceType type)
{
    if( type != m_deviceType && m_pInstance != NULL )
        DeleteBuilder();

    CDeviceBuilder::m_deviceType        = type;
    CDeviceBuilder::m_deviceGeneralType = OGL_DEVICE;
}

CDeviceBuilder* CDeviceBuilder::CreateBuilder(SupportedDeviceType type)
{
    if( m_pInstance == NULL )
       m_pInstance = new OGLDeviceBuilder();

    return m_pInstance;
}

void CDeviceBuilder::DeleteBuilder(void)
{
    delete m_pInstance;
    m_pInstance = NULL;
}

CDeviceBuilder::CDeviceBuilder() :
    m_pRender(NULL),
    m_bGraphicsContext(false),
    m_pColorCombiner(NULL),
    m_pAlphaBlender(NULL)
{
}

CDeviceBuilder::~CDeviceBuilder()
{
    DeleteGraphicsContext();
    DeleteRender();
    DeleteColorCombiner();
    DeleteAlphaBlender();
}

void CDeviceBuilder::DeleteGraphicsContext(void)
{
    if( m_bGraphicsContext )
    {
        g_pFrameBufferManager->CloseUp();
        RiceGC_Release();
        m_bGraphicsContext = false;
    }

    delete g_pFrameBufferManager;
    g_pFrameBufferManager = NULL;
}

void CDeviceBuilder::DeleteRender(void)
{
    if( m_pRender != NULL )
    {
        delete m_pRender;
        CRender::g_pRender = m_pRender = NULL;
        CRender::gRenderReferenceCount = 0;
    }
}

void CDeviceBuilder::DeleteColorCombiner(void)
{
    if( m_pColorCombiner != NULL )
    {
        delete m_pColorCombiner;
        m_pColorCombiner = NULL;
    }
}

void CDeviceBuilder::DeleteAlphaBlender(void)
{
    m_pAlphaBlender = NULL;
}


//========================================================================

void CDeviceBuilder::CreateGraphicsContext(void)
{
    m_bGraphicsContext = true;
    g_pFrameBufferManager = new FrameBufferManager;
}

CRender * OGLDeviceBuilder::CreateRender(void)
{
    if( m_pRender == NULL )
    {
        m_pRender = new COGLExtRender();

        CRender::g_pRender = m_pRender;
    }

    return m_pRender;
}

CTexture * OGLDeviceBuilder::CreateTexture(uint32_t dwWidth, uint32_t dwHeight, TextureUsage usage)
{
    CTexture *txtr = COGLTexture_New(dwWidth, dwHeight, usage);
    if( txtr == NULL )
        TRACE0("Cannot create new texture, out of video memory");
    return txtr;
}

CColorCombiner * OGLDeviceBuilder::CreateColorCombiner(CRender *pRender)
{
    if( m_pColorCombiner == NULL )
    {
       m_deviceType = (SupportedDeviceType)options.OpenglRenderSetting;

       m_pColorCombiner = new COGL_FragmentProgramCombiner(pRender);
       DebugMessage(M64MSG_VERBOSE, "OpenGL Combiner: Fragment Program");
    }

    return m_pColorCombiner;
}

CBlender * OGLDeviceBuilder::CreateAlphaBlender(CRender *pRender)
{
    (void)pRender;
    m_pAlphaBlender = OGLBlender_Get();

    return m_pAlphaBlender;
}

/* TextureManager.c's: a texture of the renderer's */
extern "C" CTexture *RiceDevice_CreateTexture(uint32_t width, uint32_t height)
{
    return CDeviceBuilder::GetBuilder()->CreateTexture(width, height);
}
