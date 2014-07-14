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
    m_bSupportSeparateSpecularColor(false),
    m_bSupportSecondColor(false),
    m_bSupportTextureObject(false),
    m_bSupportRescaleNormal(false),
    m_bSupportLODBias(false),
    m_bSupportTextureLOD(false),
    m_bSupportNVRegisterCombiner(false),
    m_bSupportBlendColor(false),
    m_bSupportBlendSubtract(false),
    m_bSupportNVTextureEnvCombine4(false),
    m_pVendorStr(NULL),
    m_pRenderStr(NULL),
    m_pExtensionStr(NULL),
    m_pVersionStr(NULL)
{
}


COGLGraphicsContext::~COGLGraphicsContext()
{
}

bool COGLGraphicsContext::Initialize(uint32_t dwWidth, uint32_t dwHeight, bool bWindowed)
{
    DebugMessage(M64MSG_INFO, "Initializing OpenGL Device Context.");
    CGraphicsContext::Initialize(dwWidth, dwHeight, bWindowed);

    if (bWindowed)
    {
        windowSetting.statusBarHeightToUse = windowSetting.statusBarHeight;
        windowSetting.toolbarHeightToUse = windowSetting.toolbarHeight;
    }
    else
    {
        windowSetting.statusBarHeightToUse = 0;
        windowSetting.toolbarHeightToUse = 0;
    }

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
    
    m_bReady = true;
    status.isVertexShaderEnabled = false;

    return true;
}

bool COGLGraphicsContext::ResizeInitialize(uint32_t dwWidth, uint32_t dwHeight, bool bWindowed)
{
    CGraphicsContext::Initialize(dwWidth, dwHeight, bWindowed);

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
    m_bSupportAnisotropicFiltering = true;

    // Compute maxAnisotropicFiltering
    m_maxAnisotropicFiltering = 0;

    if (m_bSupportAnisotropicFiltering
    && (options.anisotropicFiltering == 2
        || options.anisotropicFiltering == 4
        || options.anisotropicFiltering == 8
        || options.anisotropicFiltering == 16))
    {
        //Get the max value of anisotropy that the graphic card support
        glGetIntegerv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &m_maxAnisotropicFiltering);
        OPENGL_CHECK_ERRORS;

        // If the user wants more anisotropy than the hardware is capable of
        if (options.anisotropicFiltering > (uint32_t) m_maxAnisotropicFiltering)
        {
            DebugMessage(M64MSG_INFO, "A value of '%i' is set for AnisotropicFiltering option but the hardware has a maximum value of '%i' so this will be used", options.anisotropicFiltering, m_maxAnisotropicFiltering);
        }

        // Check if the user wants less anisotropy than the hardware is capable of
        if ((uint32_t) m_maxAnisotropicFiltering > options.anisotropicFiltering)
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
    m_bReady = false;
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

extern "C" int retro_return(bool just_flipping);

void COGLGraphicsContext::UpdateFrame(bool swapOnly)
{
    status.gFrameCount++;

    glFlush();
    OPENGL_CHECK_ERRORS;
    //glFinish();
    //wglSwapIntervalEXT(0);

    /*
    if (debuggerPauseCount == countToPause)
    {
        static int iShotNum = 0;
        // get width, height, allocate buffer to store image
        int width = windowSetting.uDisplayWidth;
        int height = windowSetting.uDisplayHeight;
        printf("Saving debug images: width=%i  height=%i\n", width, height);
        short *buffer = (short *) malloc(((width+3)&~3)*(height+1)*4);
        glReadBuffer( GL_FRONT );
        // set up a BMGImage struct
        struct BMGImageStruct img;
        memset(&img, 0, sizeof(BMGImageStruct));
        InitBMGImage(&img);
        img.bits = (unsigned char *) buffer;
        img.bits_per_pixel = 32;
        img.height = height;
        img.width = width;
        img.scan_width = width * 4;
        // store the RGB color image
        char chFilename[64];
        sprintf(chFilename, "dbg_rgb_%03i.png", iShotNum);
        glReadPixels(0,0,width,height, GL_BGRA, GL_UNSIGNED_BYTE, buffer);
        WritePNG(chFilename, img);
        // store the Z buffer
        sprintf(chFilename, "dbg_Z_%03i.png", iShotNum);
        glReadPixels(0,0,width,height, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, buffer);
        //img.bits_per_pixel = 16;
        //img.scan_width = width * 2;
        WritePNG(chFilename, img);
        // dump a subset of the Z data
        for (int y = 0; y < 480; y += 16)
        {
            for (int x = 0; x < 640; x+= 16)
                printf("%4hx ", buffer[y*640 + x]);
            printf("\n");
        }
        printf("\n");
        // free memory and get out of here
        free(buffer);
        iShotNum++;
    }
    */

    // if emulator defined a render callback function, call it before buffer swap
    if (renderCallback)
        (*renderCallback)(status.bScreenIsDrawn);

   retro_return(true);
   
   /*if (options.bShowFPS)
    {
        static unsigned int lastTick=0;
        static int frames=0;
        unsigned int nowTick = SDL_GetTicks();
        frames++;
        if (lastTick + 5000 <= nowTick)
        {
            char caption[200];
            sprintf(caption, "%s v%i.%i.%i - %.3f VI/S", PLUGIN_NAME, VERSION_PRINTF_SPLIT(PLUGIN_VERSION), frames/5.0);
            CoreVideo_SetCaption(caption);
            frames = 0;
            lastTick = nowTick;
        }
    }*/

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

bool COGLGraphicsContext::SetFullscreenMode()
{
    windowSetting.statusBarHeightToUse = 0;
    windowSetting.toolbarHeightToUse = 0;
    return true;
}

bool COGLGraphicsContext::SetWindowMode()
{
    windowSetting.statusBarHeightToUse = windowSetting.statusBarHeight;
    windowSetting.toolbarHeightToUse = windowSetting.toolbarHeight;
    return true;
}
int COGLGraphicsContext::ToggleFullscreen()
{
    return m_bWindowed?0:1;
}

// This is a static function, will be called when the plugin DLL is initialized
void COGLGraphicsContext::InitDeviceParameters()
{
    status.isVertexShaderEnabled = false;   // Disable it for now
}

// Get methods
bool COGLGraphicsContext::IsSupportAnisotropicFiltering()
{
    return m_bSupportAnisotropicFiltering;
}

int COGLGraphicsContext::getMaxAnisotropicFiltering()
{
    return m_maxAnisotropicFiltering;
}
