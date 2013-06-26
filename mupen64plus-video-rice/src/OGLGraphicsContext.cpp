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

COGLGraphicsContext::COGLGraphicsContext() :
    m_bSupportFogCoord(false),
    m_pVendorStr(NULL),
    m_pRenderStr(NULL),
    m_pExtensionStr(NULL),
    m_pVersionStr(NULL)
{
}


COGLGraphicsContext::~COGLGraphicsContext()
{
}

bool COGLGraphicsContext::Initialize(uint32 dwWidth, uint32 dwHeight)
{
    DebugMessage(M64MSG_INFO, "Initializing OpenGL Device Context.");
    Lock();

    CGraphicsContext::Initialize(dwWidth, dwHeight);

    InitState();
    InitOGLExtension();
    sprintf(m_strDeviceStats, "%.60s - %.128s : %.60s", m_pVendorStr, m_pRenderStr, m_pVersionStr);
    TRACE0(m_strDeviceStats);
    DebugMessage(M64MSG_INFO, "Using OpenGL: %s", m_strDeviceStats);

    Unlock();

    Clear(CLEAR_COLOR_AND_DEPTH_BUFFER);    // Clear buffers
    UpdateFrame();
    Clear(CLEAR_COLOR_AND_DEPTH_BUFFER);
    UpdateFrame();
    
    m_bReady = true;
    status.isVertexShaderEnabled = false;

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

    // __LIBRETRO__: Change front face winding as everything is draw upside-down
    glFrontFace(GL_CW);
    OPENGL_CHECK_ERRORS;
    glDisable(GL_CULL_FACE);
    OPENGL_CHECK_ERRORS;

    glDepthFunc(GL_LEQUAL);
    OPENGL_CHECK_ERRORS;
    glEnable(GL_DEPTH_TEST);
    OPENGL_CHECK_ERRORS;

    glEnable(GL_BLEND);
    OPENGL_CHECK_ERRORS;

    glDepthRangef(0.0f, 1.0f);

    OPENGL_CHECK_ERRORS;
}

void COGLGraphicsContext::InitOGLExtension(void)
{
    // important extension features, it is very bad not to have these feature
    m_bSupportFogCoord = IsExtensionSupported("GL_EXT_fog_coord");

    // Compute maxAnisotropicFiltering
    m_maxAnisotropicFiltering = 0;

    if( IsExtensionSupported("GL_EXT_texture_filter_anisotropic")
    && (options.anisotropicFiltering == 2
        || options.anisotropicFiltering == 4
        || options.anisotropicFiltering == 8
        || options.anisotropicFiltering == 16))
    {
        //Get the max value of aniso that the graphic card support
        glGetIntegerv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &m_maxAnisotropicFiltering);
        OPENGL_CHECK_ERRORS;

        // If user want more aniso than hardware can do
        if(options.anisotropicFiltering > (uint32) m_maxAnisotropicFiltering)
        {
            DebugMessage(M64MSG_INFO, "A value of '%i' is set for AnisotropicFiltering option but the hardware has a maximum value of '%i' so this will be used", options.anisotropicFiltering, m_maxAnisotropicFiltering);
        }

        //check if user want less anisotropy than hardware can do
        if((uint32) m_maxAnisotropicFiltering > options.anisotropicFiltering)
        m_maxAnisotropicFiltering = options.anisotropicFiltering;
    }
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

void COGLGraphicsContext::CleanUp()
{
    m_bReady = false;
}


void COGLGraphicsContext::Clear(ClearFlag dwFlags, uint32 color, float depth)
{
    uint32 flag=0;
    if( dwFlags&CLEAR_COLOR_BUFFER )    flag |= GL_COLOR_BUFFER_BIT;
    if( dwFlags&CLEAR_DEPTH_BUFFER )    flag |= GL_DEPTH_BUFFER_BIT;

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

void COGLGraphicsContext::UpdateFrame(bool swaponly)
{
    status.gFrameCount++;

    glFlush();
    OPENGL_CHECK_ERRORS;
   
   // if emulator defined a render callback function, call it before buffer swap
   if(renderCallback)
       (*renderCallback)(status.bScreenIsDrawn);
   
    glDepthMask(GL_TRUE);
    OPENGL_CHECK_ERRORS;
    glClearDepth(1.0f);
    OPENGL_CHECK_ERRORS;
    if( !g_curRomInfo.bForceScreenClear )
    {
        glClear(GL_DEPTH_BUFFER_BIT);
        OPENGL_CHECK_ERRORS;
    }
    else
        needCleanScene = true;

    status.bScreenIsDrawn = false;
}

// This is a static function, will be called when the plugin DLL is initialized
void COGLGraphicsContext::InitDeviceParameters()
{
    status.isVertexShaderEnabled = false;   // Disable it for now
}

// Get methods
int COGLGraphicsContext::getMaxAnisotropicFiltering()
{
    return m_maxAnisotropicFiltering;
}
