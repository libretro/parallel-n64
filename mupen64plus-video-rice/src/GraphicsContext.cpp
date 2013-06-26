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

#define M64P_PLUGIN_PROTOTYPES 1
#include "m64p_plugin.h"
#include "m64p_vidext.h"

#include "Config.h"
#include "FrameBuffer.h"
#include "GraphicsContext.h"
#include "Video.h"

CGraphicsContext* CGraphicsContext::g_pGraphicsContext = NULL;
bool CGraphicsContext::needCleanScene = false;

CGraphicsContext * CGraphicsContext::Get(void)
{   
    return CGraphicsContext::g_pGraphicsContext;
}
    
CGraphicsContext::CGraphicsContext() :
    m_bReady(false),
    m_bSupportMultiTexture(true),
    m_bSupportFogCoord(false)
{
}

CGraphicsContext::~CGraphicsContext()
{
    g_pFrameBufferManager->CloseUp();
}

bool CGraphicsContext::Initialize(uint32 dwWidth, uint32 dwHeight)
{
    DebugMessage(M64MSG_INFO, "Initializing OpenGL Device Context.");
    Lock();

    g_pFrameBufferManager->Initialize();

    ResetOpenGL();

    Unlock();

    Clear(CLEAR_COLOR_AND_DEPTH_BUFFER);    // Clear buffers
    UpdateFrame();
    Clear(CLEAR_COLOR_AND_DEPTH_BUFFER);
    UpdateFrame();
    
    m_bReady = true;
    status.isVertexShaderEnabled = false;

    return true;
}

void CGraphicsContext::CleanUp()
{
    m_bReady = false;
}

void CGraphicsContext::ResetOpenGL(void)
{
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClearDepth(1.0f);
    glDepthRange(0.0f, 1.0f);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_DEPTH_TEST);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_BLEND);

    glFrontFace(GL_CW); // __LIBRETRO__: Change front face winding as everything is draw upside-down
    glDisable(GL_CULL_FACE);
}


void CGraphicsContext::Clear(ClearFlag dwFlags, uint32 color, float depth)
{
    uint32 flag=0;
    if( dwFlags&CLEAR_COLOR_BUFFER )    flag |= GL_COLOR_BUFFER_BIT;
    if( dwFlags&CLEAR_DEPTH_BUFFER )    flag |= GL_DEPTH_BUFFER_BIT;

    float r = ((color>>16)&0xFF)/255.0f;
    float g = ((color>> 8)&0xFF)/255.0f;
    float b = ((color    )&0xFF)/255.0f;
    float a = ((color>>24)&0xFF)/255.0f;
    glClearColor(r, g, b, a);
    glClearDepth(depth);
    glClear(flag);  //Clear color buffer and depth buffer
}

void CGraphicsContext::UpdateFrame(bool swaponly)
{
    status.gFrameCount++;

    glFlush();
   
   // if emulator defined a render callback function, call it before buffer swap
   if(renderCallback)
       (*renderCallback)(status.bScreenIsDrawn);
   
    glDepthMask(GL_TRUE);
    glClearDepth(1.0f);

    if( !g_curRomInfo.bForceScreenClear )
        glClear(GL_DEPTH_BUFFER_BIT);
    else
        needCleanScene = true;

    status.bScreenIsDrawn = false;
}

void CGraphicsContext::InitDeviceParameters(void)
{
    status.isVertexShaderEnabled = false;
}

