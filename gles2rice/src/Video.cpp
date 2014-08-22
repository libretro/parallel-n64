/*
Copyright (C) 2002 Rice1964
Copyright (C) 2009-2011 Richard Goedeken

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

#include <vector>

#include <stdarg.h>
#include <stdlib.h>
#include <SDL.h>

#include "osal_opengl.h"

#define M64P_PLUGIN_PROTOTYPES 1
#include "m64p_types.h"
#include "m64p_common.h"
#include "m64p_plugin.h"

#include "Config.h"
#include "Debugger.h"
#include "DeviceBuilder.h"
#include "FrameBuffer.h"
#include "GraphicsContext.h"
#include "Render.h"
#include "RSP_Parser.h"
#include "TextureManager.h"
#include "Video.h"
#include "version.h"

//=======================================================
// local variables

static void (*l_DebugCallback)(void *, int, const char *) = NULL;
static void *l_DebugCallContext = NULL;
static int l_PluginInit = 0;

//=======================================================
// global variables

PluginStatus  status;

unsigned int   g_dwRamSize = 0x400000;

M64P_RECT frameWriteByCPURect;
std::vector<M64P_RECT> frameWriteByCPURects;
M64P_RECT frameWriteByCPURectArray[20][20];
bool frameWriteByCPURectFlag[20][20];
std::vector<uint32_t> frameWriteRecord;

void (*renderCallback)(int) = NULL;

#ifdef __LIBRETRO__ // Prefix API
#define VIDEO_TAG(X) rice##X

#define ReadScreen2 VIDEO_TAG(ReadScreen2)
#define PluginStartup VIDEO_TAG(PluginStartup)
#define PluginShutdown VIDEO_TAG(PluginShutdown)
#define PluginGetVersion VIDEO_TAG(PluginGetVersion)
#define CaptureScreen VIDEO_TAG(CaptureScreen)
#define ChangeWindow VIDEO_TAG(ChangeWindow)
#define CloseDLL VIDEO_TAG(CloseDLL)
#define DllTest VIDEO_TAG(DllTest)
#define DrawScreen VIDEO_TAG(DrawScreen)
#define GetDllInfo VIDEO_TAG(GetDllInfo)
#define InitiateGFX VIDEO_TAG(InitiateGFX)
#define MoveScreen VIDEO_TAG(MoveScreen)
#define RomClosed VIDEO_TAG(RomClosed)
#define RomOpen VIDEO_TAG(RomOpen)
#define ShowCFB VIDEO_TAG(ShowCFB)
#define SetRenderingCallback VIDEO_TAG(SetRenderingCallback)
#define UpdateScreen VIDEO_TAG(UpdateScreen)
#define ViStatusChanged VIDEO_TAG(ViStatusChanged)
#define ViWidthChanged VIDEO_TAG(ViWidthChanged)
#define ReadScreen VIDEO_TAG(ReadScreen)
#define FBGetFrameBufferInfo VIDEO_TAG(FBGetFrameBufferInfo)
#define FBRead VIDEO_TAG(FBRead)
#define FBWrite VIDEO_TAG(FBWrite)
#define ProcessDList VIDEO_TAG(ProcessDList)
#define ProcessRDPList VIDEO_TAG(ProcessRDPList)
#define ResizeVideoOutput VIDEO_TAG(ResizeVideoOutput)
#endif

//---------------------------------------------------------------------------------------
// Forward function declarations

extern "C" EXPORT void CALL RomClosed(void);

//---------------------------------------------------------------------------------------

static void ResizeStep2(void)
{
    // Delete all OpenGL textures
    gTextureManager.CleanUp();
    RDP_Cleanup();
    // delete our opengl renderer
    CDeviceBuilder::GetBuilder()->DeleteRender();

    // call video extension function with updated width, height (this creates a new OpenGL context)
    windowSetting.uDisplayWidth = status.gNewResizeWidth;
    windowSetting.uDisplayHeight = status.gNewResizeHeight;
    //CoreVideo_ResizeWindow(windowSetting.uDisplayWidth, windowSetting.uDisplayHeight);

    // re-initialize our OpenGL graphics context state
    bool res = CGraphicsContext::Get()->ResizeInitialize(windowSetting.uDisplayWidth, windowSetting.uDisplayHeight);
    if (res)
    {
        // re-create the OpenGL renderer
        CDeviceBuilder::GetBuilder()->CreateRender();
        CRender::GetRender()->Initialize();
        DLParser_Init();
    }

    status.ToResize = false;
}

static void UpdateScreenStep2 (void)
{
    status.bVIOriginIsUpdated = false;

    if (status.ToResize && status.gDlistCount > 0)
    {
        ResizeStep2();
        return;
    }

    if( status.bHandleN64RenderTexture )
        g_pFrameBufferManager->CloseRenderTexture(true);
    
    g_pFrameBufferManager->SetAddrBeDisplayed(*gfx_info.VI_ORIGIN_REG);

    if( status.gDlistCount == 0 )
    {
        // CPU frame buffer update
        uint32_t width = *gfx_info.VI_WIDTH_REG;
        if( (*gfx_info.VI_ORIGIN_REG & (g_dwRamSize-1) ) > width*2 && *gfx_info.VI_H_START_REG != 0 && width != 0 )
        {
            SetVIScales();
            CRender::GetRender()->DrawFrameBuffer(true, 0, 0, 0, 0);
            CGraphicsContext::Get()->UpdateFrame(false);
        }
        return;
    }

    TXTRBUF_DETAIL_DUMP(TRACE1("VI ORIG is updated to %08X", *gfx_info.VI_ORIGIN_REG));

    if( currentRomOptions.screenUpdateSetting == SCREEN_UPDATE_AT_VI_UPDATE )
    {
        CGraphicsContext::Get()->UpdateFrame(false);

        DEBUGGER_IF_DUMP( pauseAtNext, TRACE1("Update Screen: VIORIG=%08X", *gfx_info.VI_ORIGIN_REG));
        DEBUGGER_PAUSE_COUNT_N_WITHOUT_UPDATE(NEXT_FRAME);
        DEBUGGER_PAUSE_COUNT_N_WITHOUT_UPDATE(NEXT_SET_CIMG);
        return;
    }

    TXTRBUF_DETAIL_DUMP(TRACE1("VI ORIG is updated to %08X", *gfx_info.VI_ORIGIN_REG));

    if( currentRomOptions.screenUpdateSetting == SCREEN_UPDATE_AT_VI_UPDATE_AND_DRAWN )
    {
        if( status.bScreenIsDrawn )
        {
            CGraphicsContext::Get()->UpdateFrame(false);
            DEBUGGER_IF_DUMP( pauseAtNext, TRACE1("Update Screen: VIORIG=%08X", *gfx_info.VI_ORIGIN_REG));
        }
        else
        {
            DEBUGGER_IF_DUMP( pauseAtNext, TRACE1("Skip Screen Update: VIORIG=%08X", *gfx_info.VI_ORIGIN_REG));
        }

        DEBUGGER_PAUSE_COUNT_N_WITHOUT_UPDATE(NEXT_FRAME);
        DEBUGGER_PAUSE_COUNT_N_WITHOUT_UPDATE(NEXT_SET_CIMG);
        return;
    }

    if( currentRomOptions.screenUpdateSetting==SCREEN_UPDATE_AT_VI_CHANGE )
    {

        if( *gfx_info.VI_ORIGIN_REG != status.curVIOriginReg )
        {
            if( *gfx_info.VI_ORIGIN_REG < status.curDisplayBuffer || *gfx_info.VI_ORIGIN_REG > status.curDisplayBuffer+0x2000  )
            {
                status.curDisplayBuffer = *gfx_info.VI_ORIGIN_REG;
                status.curVIOriginReg = status.curDisplayBuffer;
                //status.curRenderBuffer = NULL;

                CGraphicsContext::Get()->UpdateFrame(false);
                DEBUGGER_IF_DUMP( pauseAtNext, TRACE1("Update Screen: VIORIG=%08X", *gfx_info.VI_ORIGIN_REG));
                DEBUGGER_PAUSE_COUNT_N_WITHOUT_UPDATE(NEXT_FRAME);
                DEBUGGER_PAUSE_COUNT_N_WITHOUT_UPDATE(NEXT_SET_CIMG);
            }
            else
            {
                status.curDisplayBuffer = *gfx_info.VI_ORIGIN_REG;
                status.curVIOriginReg = status.curDisplayBuffer;
                DEBUGGER_PAUSE_AND_DUMP_NO_UPDATE(NEXT_FRAME, {DebuggerAppendMsg("Skip Screen Update, closed to the display buffer, VIORIG=%08X", *gfx_info.VI_ORIGIN_REG);});
            }
        }
        else
        {
            DEBUGGER_PAUSE_AND_DUMP_NO_UPDATE(NEXT_FRAME, {DebuggerAppendMsg("Skip Screen Update, the same VIORIG=%08X", *gfx_info.VI_ORIGIN_REG);});
        }

        return;
    }

    if( currentRomOptions.screenUpdateSetting >= SCREEN_UPDATE_AT_1ST_CI_CHANGE )
    {
        status.bVIOriginIsUpdated=true;
        DEBUGGER_PAUSE_AND_DUMP_NO_UPDATE(NEXT_FRAME, {DebuggerAppendMsg("VI ORIG is updated to %08X", *gfx_info.VI_ORIGIN_REG);});
        return;
    }

    DEBUGGER_IF_DUMP( pauseAtNext, TRACE1("VI is updated, No screen update: VIORIG=%08X", *gfx_info.VI_ORIGIN_REG));
    DEBUGGER_PAUSE_COUNT_N_WITHOUT_UPDATE(NEXT_FRAME);
    DEBUGGER_PAUSE_COUNT_N_WITHOUT_UPDATE(NEXT_SET_CIMG);
}

static void ProcessDListStep2(void)
{
    if( status.toShowCFB )
    {
        CRender::GetRender()->DrawFrameBuffer(true, 0, 0, 0, 0);
        status.toShowCFB = false;
    }

    DLParser_Process((OSTask *)(gfx_info.DMEM + 0x0FC0));
}   

static bool StartVideo(void)
{
    windowSetting.dps = windowSetting.fps = -1;
    windowSetting.lastSecDlistCount = windowSetting.lastSecFrameCount = 0xFFFFFFFF;

    memcpy(&g_curRomInfo.romheader, gfx_info.HEADER, sizeof(ROMHeader));
    unsigned char *puc = (unsigned char *) &g_curRomInfo.romheader;
    unsigned int i;
    unsigned char temp;
    for (i = 0; i < sizeof(ROMHeader); i += 4) /* byte-swap the ROM header */
    {
        temp     = puc[i];
        puc[i]   = puc[i+3];
        puc[i+3] = temp;
        temp     = puc[i+1];
        puc[i+1] = puc[i+2];
        puc[i+2] = temp;
    }

    ROM_GetRomNameFromHeader(g_curRomInfo.szGameName, &g_curRomInfo.romheader);
    Ini_GetRomOptions(&g_curRomInfo);
    char *p = (char *) g_curRomInfo.szGameName + (strlen((char *) g_curRomInfo.szGameName) -1);     // -1 to skip null
    while (p >= (char *) g_curRomInfo.szGameName)
    {
        if( *p == ':' || *p == '\\' || *p == '/' )
            *p = '-';
        p--;
    }

    GenerateCurrentRomOptions();
    status.dwTvSystem = CountryCodeToTVSystem(g_curRomInfo.romheader.nCountryID);
    if( status.dwTvSystem == TV_SYSTEM_NTSC )
        status.fRatio = 0.75f;
    else
        status.fRatio = 9/11.0f;;
    
    CDeviceBuilder::GetBuilder()->CreateGraphicsContext();
    CGraphicsContext::InitWindowInfo();

    bool res = CGraphicsContext::Get()->Initialize(windowSetting.uDisplayWidth, windowSetting.uDisplayHeight);
    if (!res)
    {
       return false;
    }
    CDeviceBuilder::GetBuilder()->CreateRender();
    CRender::GetRender()->Initialize();
    DLParser_Init();
    status.bGameIsRunning = true;
   
    return true;
}

static void StopVideo()
{
    status.bGameIsRunning = false;

    // Kill all textures?
    gTextureManager.RecycleAllTextures();
    gTextureManager.CleanUp();
    RDP_Cleanup();

    CDeviceBuilder::GetBuilder()->DeleteRender();
    CGraphicsContext::Get()->CleanUp();
    CDeviceBuilder::GetBuilder()->DeleteGraphicsContext();

    windowSetting.dps = windowSetting.fps = -1;
    windowSetting.lastSecDlistCount = windowSetting.lastSecFrameCount = 0xFFFFFFFF;
    status.gDlistCount = status.gFrameCount = 0;

}

//---------------------------------------------------------------------------------------
// Global functions, for use by other source files in this plugin

void SetVIScales()
{
    if( g_curRomInfo.VIHeight>0 && g_curRomInfo.VIWidth>0 )
    {
        windowSetting.fViWidth = windowSetting.uViWidth = g_curRomInfo.VIWidth;
        windowSetting.fViHeight = windowSetting.uViHeight = g_curRomInfo.VIHeight;
    }
    else if( g_curRomInfo.UseCIWidthAndRatio && g_CI.dwWidth )
    {
        windowSetting.fViWidth = windowSetting.uViWidth = g_CI.dwWidth;
        windowSetting.fViHeight = windowSetting.uViHeight = 
            g_curRomInfo.UseCIWidthAndRatio == USE_CI_WIDTH_AND_RATIO_FOR_NTSC ? g_CI.dwWidth/4*3 : g_CI.dwWidth/11*9;
    }
    else
    {
        float xscale, yscale;
        uint32_t val = *gfx_info.VI_X_SCALE_REG & 0xFFF;
        xscale = (float)val / (1<<10);
        uint32_t start = *gfx_info.VI_H_START_REG >> 16;
        uint32_t end = *gfx_info.VI_H_START_REG&0xFFFF;
        uint32_t width = *gfx_info.VI_WIDTH_REG;
        windowSetting.fViWidth = (end-start)*xscale;
        if( abs((int)(windowSetting.fViWidth - width) ) < 8 ) 
        {
            windowSetting.fViWidth = (float)width;
        }
        else
        {
            DebuggerAppendMsg("fViWidth = %f, Width Reg=%d", windowSetting.fViWidth, width);
        }

        val = (*gfx_info.VI_Y_SCALE_REG & 0xFFF);// - ((*gfx_info.VI_Y_SCALE_REG>>16) & 0xFFF);
        if( val == 0x3FF )  val = 0x400;
        yscale = (float)val / (1<<10);
        start = *gfx_info.VI_V_START_REG >> 16;
        end = *gfx_info.VI_V_START_REG&0xFFFF;
        windowSetting.fViHeight = (end-start)/2*yscale;

        if( yscale == 0 )
        {
            windowSetting.fViHeight = windowSetting.fViWidth*status.fRatio;
        }
        else
        {
            if( *gfx_info.VI_WIDTH_REG > 0x300 ) 
                windowSetting.fViHeight *= 2;

            if( windowSetting.fViWidth*status.fRatio > windowSetting.fViHeight && (*gfx_info.VI_X_SCALE_REG & 0xFF) != 0 )
            {
                if( abs(int(windowSetting.fViWidth*status.fRatio - windowSetting.fViHeight)) < 8 )
                {
                    windowSetting.fViHeight = windowSetting.fViWidth*status.fRatio;
                }
                /*
                else
                {
                    if( abs(windowSetting.fViWidth*status.fRatio-windowSetting.fViHeight) > windowSetting.fViWidth*0.1f )
                    {
                        if( status.fRatio > 0.8 )
                            windowSetting.fViHeight = windowSetting.fViWidth*3/4;
                        //windowSetting.fViHeight = (*gfx_info.VI_V_SYNC_REG - 0x2C)/2;
                    }
                }
                */
            }
            
            if( windowSetting.fViHeight<100 || windowSetting.fViWidth<100 )
            {
                //At sometime, value in VI_H_START_REG or VI_V_START_REG are 0
                windowSetting.fViWidth = (float)*gfx_info.VI_WIDTH_REG;
                windowSetting.fViHeight = windowSetting.fViWidth*status.fRatio;
            }
        }

        windowSetting.uViWidth = (unsigned short)(windowSetting.fViWidth/4);
        windowSetting.fViWidth = windowSetting.uViWidth *= 4;

        windowSetting.uViHeight = (unsigned short)(windowSetting.fViHeight/4);
        windowSetting.fViHeight = windowSetting.uViHeight *= 4;
        uint16_t optimizeHeight = (uint16_t)(windowSetting.uViWidth*status.fRatio);
        optimizeHeight &= ~3;

        uint16_t optimizeHeight2 = (uint16_t)(windowSetting.uViWidth*3/4);
        optimizeHeight2 &= ~3;

        if( windowSetting.uViHeight != optimizeHeight && windowSetting.uViHeight != optimizeHeight2 )
        {
            if( abs(windowSetting.uViHeight-optimizeHeight) <= 8 )
                windowSetting.fViHeight = windowSetting.uViHeight = optimizeHeight;
            else if( abs(windowSetting.uViHeight-optimizeHeight2) <= 8 )
                windowSetting.fViHeight = windowSetting.uViHeight = optimizeHeight2;
        }


        if( gRDP.scissor.left == 0 && gRDP.scissor.top == 0 && gRDP.scissor.right != 0 )
        {
            if( (*gfx_info.VI_X_SCALE_REG & 0xFF) != 0x0 && gRDP.scissor.right == windowSetting.uViWidth )
            {
                // Mario Tennis
                if( abs(int( windowSetting.fViHeight - gRDP.scissor.bottom )) < 8 )
                {
                    windowSetting.fViHeight = windowSetting.uViHeight = gRDP.scissor.bottom;
                }
                else if( windowSetting.fViHeight < gRDP.scissor.bottom )
                {
                    windowSetting.fViHeight = windowSetting.uViHeight = gRDP.scissor.bottom;
                }
                windowSetting.fViHeight = windowSetting.uViHeight = gRDP.scissor.bottom;
            }
            else if( gRDP.scissor.right == windowSetting.uViWidth - 1 && gRDP.scissor.bottom != 0 )
            {
                if( windowSetting.uViHeight != optimizeHeight && windowSetting.uViHeight != optimizeHeight2 )
                {
                    if( status.fRatio != 0.75 && windowSetting.fViHeight > optimizeHeight/2 )
                    {
                        windowSetting.fViHeight = windowSetting.uViHeight = gRDP.scissor.bottom + gRDP.scissor.top + 1;
                    }
                }
            }
            else if( gRDP.scissor.right == windowSetting.uViWidth && gRDP.scissor.bottom != 0  && status.fRatio != 0.75 )
            {
                if( windowSetting.uViHeight != optimizeHeight && windowSetting.uViHeight != optimizeHeight2 )
                {
                    if( status.fRatio != 0.75 && windowSetting.fViHeight > optimizeHeight/2 )
                    {
                        windowSetting.fViHeight = windowSetting.uViHeight = gRDP.scissor.bottom + gRDP.scissor.top + 1;
                    }
                }
            }
        }
    }
    SetScreenMult(windowSetting.uDisplayWidth/windowSetting.fViWidth, windowSetting.uDisplayHeight/windowSetting.fViHeight);
}

void TriggerDPInterrupt(void)
{
    *(gfx_info.MI_INTR_REG) |= MI_INTR_DP;
    gfx_info.CheckInterrupts();
}

void TriggerSPInterrupt(void)
{
    *(gfx_info.MI_INTR_REG) |= MI_INTR_SP;
    gfx_info.CheckInterrupts();
}

void _VIDEO_DisplayTemporaryMessage(const char *Message)
{
}

void DebugMessage(int level, const char *message, ...)
{
  char msgbuf[1024];
  va_list args;

  if (l_DebugCallback == NULL)
      return;

  va_start(args, message);
  vsprintf(msgbuf, message, args);

  (*l_DebugCallback)(l_DebugCallContext, level, msgbuf);

  va_end(args);
}

//---------------------------------------------------------------------------------------
// Global functions, exported for use by the core library

// since these functions are exported, they need to have C-style names
#ifdef __cplusplus
extern "C" {
#endif

/* Mupen64Plus plugin functions */
EXPORT m64p_error CALL PluginStartup(m64p_dynlib_handle CoreLibHandle, void *Context,
                                   void (*DebugCallback)(void *, int, const char *))
{
    if (l_PluginInit)
        return M64ERR_ALREADY_INIT;

    /* first thing is to set the callback function for debug info */
    l_DebugCallback = DebugCallback;
    l_DebugCallContext = Context;

    /* open config section handles and set parameter default values */
    if (!InitConfiguration())
        return M64ERR_INTERNAL;

    l_PluginInit = 1;
    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginShutdown(void)
{
    if (!l_PluginInit)
        return M64ERR_NOT_INIT;

    if( status.bGameIsRunning )
    {
        RomClosed();
    }
#if 0
    if (bIniIsChanged)
    {
        WriteIniFile();
        TRACE0("Write back INI file");
    }
#endif

    /* reset some local variables */
    l_DebugCallback = NULL;
    l_DebugCallContext = NULL;

    l_PluginInit = 0;
    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginGetVersion(m64p_plugin_type *PluginType, int *PluginVersion, int *APIVersion, const char **PluginNamePtr, int *Capabilities)
{
    /* set version info */
    if (PluginType != NULL)
        *PluginType = M64PLUGIN_GFX;

    if (PluginVersion != NULL)
        *PluginVersion = PLUGIN_VERSION;

    if (APIVersion != NULL)
        *APIVersion = VIDEO_PLUGIN_API_VERSION;
    
    if (PluginNamePtr != NULL)
        *PluginNamePtr = PLUGIN_NAME;

    if (Capabilities != NULL)
    {
        *Capabilities = 0;
    }
                    
    return M64ERR_SUCCESS;
}

//-------------------------------------------------------------------------------------


EXPORT void CALL ChangeWindow (void)
{
}

//---------------------------------------------------------------------------------------

EXPORT void CALL MoveScreen (int xpos, int ypos)
{ 
}

//---------------------------------------------------------------------------------------
EXPORT void CALL RomClosed(void)
{
    TRACE0("To stop video");
    Ini_StoreRomOptions(&g_curRomInfo);
    StopVideo();
    TRACE0("Video is stopped");
}

EXPORT int CALL RomOpen(void)
{
    /* Read RiceVideoLinux.ini file, set up internal variables by reading values from core configuration API */
    LoadConfiguration();

    status.bDisableFPS=false;

   g_dwRamSize = 0x800000;
    
#ifdef DEBUGGER
    if( debuggerPause )
    {
        debuggerPause = FALSE;
        usleep(100 * 1000);
    }
#endif

    if (!StartVideo())
        return 0;

    return 1;
}


//---------------------------------------------------------------------------------------
EXPORT void CALL UpdateScreen(void)
{
    UpdateScreenStep2();
}

//---------------------------------------------------------------------------------------

EXPORT void CALL ViStatusChanged(void)
{
    SetVIScales();
    CRender::g_pRender->UpdateClipRectangle();
}

//---------------------------------------------------------------------------------------
EXPORT void CALL ViWidthChanged(void)
{
    SetVIScales();
    CRender::g_pRender->UpdateClipRectangle();
}

EXPORT int CALL InitiateGFX(GFX_INFO Gfx_Info)
{
    memset(&status, 0, sizeof(status));

    windowSetting.fViWidth = 320;
    windowSetting.fViHeight = 240;
    status.ToResize = false;
    status.bDisableFPS=false;

    if (!InitConfiguration())
    {
        DebugMessage(M64MSG_ERROR, "Failed to read configuration data");
        return FALSE;
    }

    CGraphicsContext::InitWindowInfo();
    CGraphicsContext::InitDeviceParameters();

    return(TRUE);
}

EXPORT void CALL ResizeVideoOutput(int width, int height)
{
    // save the new window resolution.  actual resizing operation is asynchronous (it happens later)
    status.gNewResizeWidth = width;
    status.gNewResizeHeight = height;
    status.ToResize = true;
}

//---------------------------------------------------------------------------------------

EXPORT void CALL ProcessRDPList(void)
{
   RDP_DLParser_Process();
}   

EXPORT void CALL ProcessDList(void)
{
    ProcessDListStep2();
}   

//---------------------------------------------------------------------------------------

/******************************************************************
  Function: FrameBufferRead
  Purpose:  This function is called to notify the dll that the
            frame buffer memory is beening read at the given address.
            DLL should copy content from its render buffer to the frame buffer
            in N64 RDRAM
            DLL is responsible to maintain its own frame buffer memory addr list
            DLL should copy 4KB block content back to RDRAM frame buffer.
            Emulator should not call this function again if other memory
            is read within the same 4KB range

            Since depth buffer is also being watched, the reported addr
            may belong to depth buffer
  input:    addr        rdram address
            val         val
            size        1 = uint8_t, 2 = uint16_t, 4 = uint32_t
  output:   none
*******************************************************************/ 

EXPORT void CALL FBRead(uint32_t addr)
{
    g_pFrameBufferManager->FrameBufferReadByCPU(addr);
}


/******************************************************************
  Function: FrameBufferWrite
  Purpose:  This function is called to notify the dll that the
            frame buffer has been modified by CPU at the given address.

            Since depth buffer is also being watched, the reported addr
            may belong to depth buffer

  input:    addr        rdram address
            val         val
            size        1 = uint8_t, 2 = uint16_t, 4 = uint32_t
  output:   none
*******************************************************************/ 

EXPORT void CALL FBWrite(uint32_t addr, uint32_t size)
{
    g_pFrameBufferManager->FrameBufferWriteByCPU(addr, size);
}

/************************************************************************
Function: FBGetFrameBufferInfo
Purpose:  This function is called by the emulator core to retrieve frame
          buffer information from the video plugin in order to be able
          to notify the video plugin about CPU frame buffer read/write
          operations

          size:
            = 1     byte
            = 2     word (16 bit) <-- this is N64 default depth buffer format
            = 4     dword (32 bit)

          when frame buffer information is not available yet, set all values
          in the FrameBufferInfo structure to 0

input:    FrameBufferInfo pinfo[6]
          pinfo is pointed to a FrameBufferInfo structure which to be
          filled in by this function
output:   Values are return in the FrameBufferInfo structure
          Plugin can return up to 6 frame buffer info
 ************************************************************************/

EXPORT void CALL FBGetFrameBufferInfo(void *p)
{
    FrameBufferInfo * pinfo = (FrameBufferInfo *)p;
    memset(pinfo,0,sizeof(FrameBufferInfo)*6);

    //if( g_ZI.dwAddr == 0 )
    //{
    //  memset(pinfo,0,sizeof(FrameBufferInfo)*6);
    //}
    //else
    {
        for (int i=0; i<5; i++ )
        {
            if( status.gDlistCount-g_RecentCIInfo[i].lastUsedFrame > 30 || g_RecentCIInfo[i].lastUsedFrame == 0 )
            {
                //memset(&pinfo[i],0,sizeof(FrameBufferInfo));
            }
            else
            {
                pinfo[i].addr = g_RecentCIInfo[i].dwAddr;
                pinfo[i].size = 2;
                pinfo[i].width = g_RecentCIInfo[i].dwWidth;
                pinfo[i].height = g_RecentCIInfo[i].dwHeight;
                TXTRBUF_DETAIL_DUMP(TRACE3("Protect 0x%08X (%d,%d)", g_RecentCIInfo[i].dwAddr, g_RecentCIInfo[i].dwWidth, g_RecentCIInfo[i].dwHeight));
                pinfo[5].width = g_RecentCIInfo[i].dwWidth;
                pinfo[5].height = g_RecentCIInfo[i].dwHeight;
            }
        }

        pinfo[5].addr = g_ZI.dwAddr;
        //pinfo->size = g_RecentCIInfo[5].dwSize;
        pinfo[5].size = 2;
        TXTRBUF_DETAIL_DUMP(TRACE3("Protect 0x%08X (%d,%d)", pinfo[5].addr, pinfo[5].width, pinfo[5].height));
    }
}

// Plugin spec 1.3 functions
EXPORT void CALL ShowCFB(void)
{
    status.toShowCFB = true;
}

//void ReadScreen2( void *dest, int *width, int *height, int bFront )
EXPORT void CALL ReadScreen2(void *dest, int *width, int *height, int bFront)
{
    if (width == NULL || height == NULL)
        return;

    *width = windowSetting.uDisplayWidth;
    *height = windowSetting.uDisplayHeight;

    if (dest == NULL)
        return;
}
    

EXPORT void CALL SetRenderingCallback(void (*callback)(int))
{
    renderCallback = callback;
}

#ifdef __cplusplus
}
#endif

