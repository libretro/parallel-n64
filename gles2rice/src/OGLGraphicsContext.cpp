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

#define M64P_PLUGIN_PROTOTYPES 1
#include "m64p_plugin.h"
#include "Config.h"
#include "Debugger.h"
#include "OGLDebug.h"
#include "OGLGraphicsContext.h"
#include "TextureManager.h"
#include "Video.h"
#include "version.h"
#include "SDL.h"

COGLGraphicsContext::COGLGraphicsContext() :
    m_bSupportLODBias(false),
    m_bSupportTextureLOD(false),
    m_pVendorStr(NULL),
    m_pRenderStr(NULL),
    m_pExtensionStr(NULL),
    m_pVersionStr(NULL)
{
}


COGLGraphicsContext::~COGLGraphicsContext()
{
}

bool COGLGraphicsContext::Initialize(uint32_t dwWidth, uint32_t dwHeight)
{
    DebugMessage(M64MSG_INFO, "Initializing OpenGL Device Context.");
    CGraphicsContext::Initialize(dwWidth, dwHeight);

    int depthBufferDepth = options.OpenglDepthBufferSetting;
    int colorBufferDepth = 32;
    int bVerticalSync = windowSetting.bVerticalSync;

    if (options.colorQuality == TEXTURE_FMT_A4R4G4B4)
        colorBufferDepth = 16;

    InitState();
    InitOGLExtension();
    sprintf(m_strDeviceStats, "%.60s - %.128s : %.60s", m_pVendorStr, m_pRenderStr, m_pVersionStr);
    TRACE0(m_strDeviceStats);
    DebugMessage(M64MSG_INFO, "Using OpenGL: %s", m_strDeviceStats);

    Clear(CLEAR_COLOR_AND_DEPTH_BUFFER, 0xFF000000, 1.0f);    // Clear buffers
    UpdateFrame(false);
    Clear(CLEAR_COLOR_AND_DEPTH_BUFFER, 0xFF000000, 1.0f);
    UpdateFrame(false);
    
    return true;
}

bool COGLGraphicsContext::ResizeInitialize(uint32_t dwWidth, uint32_t dwHeight)
{
    CGraphicsContext::Initialize(dwWidth, dwHeight);

    int depthBufferDepth = options.OpenglDepthBufferSetting;
    int colorBufferDepth = 32;
    int bVerticalSync = windowSetting.bVerticalSync;

    if (options.colorQuality == TEXTURE_FMT_A4R4G4B4)
        colorBufferDepth = 16;

    /* Hard-coded attribute values */
    const int iDOUBLEBUFFER = 1;

    InitState();

    Clear(CLEAR_COLOR_AND_DEPTH_BUFFER, 0xFF000000, 1.0f);    // Clear buffers
    UpdateFrame(false);
    Clear(CLEAR_COLOR_AND_DEPTH_BUFFER, 0xFF000000, 1.0f);
    UpdateFrame(false);
    
    return true;
}

void COGLGraphicsContext::InitState(void)
{
    m_pRenderStr = glGetString(GL_RENDERER);
    m_pExtensionStr = glGetString(GL_EXTENSIONS);
    m_pVersionStr = glGetString(GL_VERSION);
    m_pVendorStr = glGetString(GL_VENDOR);
    glMatrixMode(GL_PROJECTION);
    OPENGL_CHECK_ERRORS;
    glLoadIdentity();
    OPENGL_CHECK_ERRORS;

    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    OPENGL_CHECK_ERRORS;
    glClearDepth(1.0f);
    OPENGL_CHECK_ERRORS;

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    OPENGL_CHECK_ERRORS;
    glDisable(GL_BLEND);
    OPENGL_CHECK_ERRORS;

    glFrontFace(GL_CCW);
    OPENGL_CHECK_ERRORS;
    glDisable(GL_CULL_FACE);
    OPENGL_CHECK_ERRORS;

    glDepthFunc(GL_LEQUAL);
    OPENGL_CHECK_ERRORS;
    glEnable(GL_DEPTH_TEST);
    OPENGL_CHECK_ERRORS;

    glEnable(GL_BLEND);
    OPENGL_CHECK_ERRORS;
    glDepthRange(-1.0f, 1.0f);
    OPENGL_CHECK_ERRORS;
}

void COGLGraphicsContext::InitOGLExtension(void)
{
}

bool COGLGraphicsContext::IsExtensionSupported(const char* pExtName)
{
    if (strstr((const char*)m_pExtensionStr, pExtName) != NULL)
    {
        DebugMessage(M64MSG_VERBOSE, "OpenGL Extension '%s' is supported.", pExtName);
        return true;
    }
    else
    {
        DebugMessage(M64MSG_VERBOSE, "OpenGL Extension '%s' is NOT supported.", pExtName);
        return false;
    }
}

bool COGLGraphicsContext::IsWglExtensionSupported(const char* pExtName)
{
    if (m_pWglExtensionStr == NULL)
        return false;

    if (strstr((const char*)m_pWglExtensionStr, pExtName) != NULL)
        return true;
    else
        return false;
}


void COGLGraphicsContext::CleanUp()
{
}


void COGLGraphicsContext::Clear(ClearFlag dwFlags, uint32_t color, float depth)
{
    uint32_t flag=0;
    if (dwFlags & CLEAR_COLOR_BUFFER) flag |= GL_COLOR_BUFFER_BIT;
    if (dwFlags & CLEAR_DEPTH_BUFFER) flag |= GL_DEPTH_BUFFER_BIT;

    float r = ((color>>16)&0xFF)/255.0f;
    float g = ((color>> 8)&0xFF)/255.0f;
    float b = ((color    )&0xFF)/255.0f;
    float a = ((color>>24)&0xFF)/255.0f;
    glClearColor(r, g, b, a);
    OPENGL_CHECK_ERRORS;
    glClearDepth(depth);
    OPENGL_CHECK_ERRORS;
    glClear(flag);  //Clear color buffer and depth buffer
    OPENGL_CHECK_ERRORS;
}

void COGLGraphicsContext::UpdateFrame(bool swapOnly)
{
    status.gFrameCount++;

    glFlush();
    OPENGL_CHECK_ERRORS;

    // if emulator defined a render callback function, call it before buffer swap
    if (renderCallback)
        (*renderCallback)(status.bScreenIsDrawn);

   retro_return(true);
   
    glDepthMask(GL_TRUE);
    OPENGL_CHECK_ERRORS;
    glClearDepth(1.0f);
    OPENGL_CHECK_ERRORS;
    if (!g_curRomInfo.bForceScreenClear)
    {
        glClear(GL_DEPTH_BUFFER_BIT);
        OPENGL_CHECK_ERRORS;
    }
    else
    {
        needCleanScene = true;
    }

    status.bScreenIsDrawn = false;
}

// This is a static function, will be called when the plugin DLL is initialized
void COGLGraphicsContext::InitDeviceParameters()
{
}
