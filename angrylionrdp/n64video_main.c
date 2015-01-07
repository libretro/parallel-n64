#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "z64.h"
#include "Gfx #1.3.h"
#include "vi.h"
#include "rdp.h"
#include "m64p_types.h"
#include "m64p_config.h"

extern unsigned int screen_width, screen_height;
extern uint32_t screen_pitch;

#ifdef HAVE_DIRECTDRAW
LPDIRECTDRAW7 lpdd = 0;
LPDIRECTDRAWSURFACE7 lpddsprimary; 
LPDIRECTDRAWSURFACE7 lpddsback;
DDSURFACEDESC2 ddsd;
#else
uint32_t *blitter_buf;
#endif
int res;
RECT __dst, __src;
INT32 pitchindwords;

FILE* zeldainfo = 0;
int ProcessDListShown = 0;
extern int SaveLoaded;
extern UINT32 command_counter;

int retro_return(bool just_flipping);

EXPORT void CALL CaptureScreen ( char * Directory )
{
    return;
}

EXPORT void CALL angrylionChangeWindow (void)
{
}

EXPORT void CALL CloseDLL (void)
{
    return;
}

EXPORT void CALL angrylionReadScreen2(void *dest, int *width, int *height, int front)
{
}

 
EXPORT void CALL angrylionDrawScreen (void)
{
}

EXPORT void CALL angrylionGetDllInfo(PLUGIN_INFO* PluginInfo)
{
    PluginInfo -> Version = 0x0103;
    PluginInfo -> Type  = 2;
    strcpy(
#if (SCREEN_WIDTH == 320 && SCREEN_HEIGHT == 240)
    PluginInfo -> Name, "angrylion's RDP (320x240)"
#else
    PluginInfo -> Name, "angrylion's RDP"
#endif
    );
    PluginInfo -> NormalMemory = true;
    PluginInfo -> MemoryBswaped = true;
}

EXPORT void CALL angrylionSetRenderingCallback(void (*callback)(int))
{
}

EXPORT int CALL angrylionInitiateGFX (GFX_INFO Gfx_Info)
{
   return true;
}

 
EXPORT void CALL angrylionMoveScreen (int xpos, int ypos)
{
#ifdef HAVE_DIRECTDRAW
    RECT statusrect;
    POINT p;

    p.x = p.y = 0;
    GetClientRect(gfx.hWnd, &__dst);
    ClientToScreen(gfx.hWnd, &p);
    OffsetRect(&__dst, p.x, p.y);
    GetClientRect(gfx.hStatusBar, &statusrect);
    __dst.bottom -= statusrect.bottom;
#endif
}

 
EXPORT void CALL angrylionProcessDList(void)
{
    if (!ProcessDListShown)
    {
        DisplayError("ProcessDList");
        ProcessDListShown = 1;
    }
}

EXPORT void CALL angrylionProcessRDPList(void)
{
    process_RDP_list();
    return;
}

EXPORT void CALL angrylionRomClosed (void)
{
    rdp_close();
#ifdef HAVE_DIRECTDRAW
    if (lpddsback)
    {
        IDirectDrawSurface_Release(lpddsback);
        lpddsback = 0;
    }
    if (lpddsprimary)
    {
        IDirectDrawSurface_Release(lpddsprimary);
        lpddsprimary = 0;
    }
    if (lpdd)
    {
        IDirectDraw_Release(lpdd);
        lpdd = 0;
    }
#else
    if (blitter_buf)
       free(blitter_buf);
#endif

    SaveLoaded = 1;
    command_counter = 0;
}

static m64p_handle l_ConfigAngrylion;
 
EXPORT int CALL angrylionRomOpen (void)
{
   printf("Gets here?\n");
#ifndef HAVE_DIRECTDRAW
   screen_width = SCREEN_WIDTH;
   screen_height = SCREEN_HEIGHT;
   blitter_buf = (uint32_t*)calloc(
      PRESCALE_WIDTH * PRESCALE_HEIGHT, sizeof(uint32_t)
   );
   pitchindwords = PRESCALE_WIDTH / 1; /* sizeof(DWORD) == sizeof(pixel) == 4 */
   screen_pitch = PRESCALE_WIDTH << 2;
#else
    DDPIXELFORMAT ftpixel;
    LPDIRECTDRAWCLIPPER lpddcl;
    RECT bigrect, smallrect, statusrect;
    POINT p;
    int rightdiff;
    int bottomdiff;
    GetWindowRect(gfx.hWnd,&bigrect);
    GetClientRect(gfx.hWnd,&smallrect);
    rightdiff = screen_width - smallrect.right;
    bottomdiff = screen_height - smallrect.bottom;

    if (gfx.hStatusBar)
    {
        GetClientRect(gfx.hStatusBar, &statusrect);
        bottomdiff += statusrect.bottom;
    }
    MoveWindow(gfx.hWnd, bigrect.left, bigrect.top, bigrect.right - bigrect.left + rightdiff, bigrect.bottom - bigrect.top + bottomdiff, true);

    res = DirectDrawCreateEx(0, (LPVOID*)&lpdd, &IID_IDirectDraw7, 0);
    if (res != DD_OK)
    {
        DisplayError("Couldn't create a DirectDraw object.");
        return; /* to-do:  move to InitiateGFX? */
    }
    res = IDirectDraw_SetCooperativeLevel(lpdd, gfx.hWnd, DDSCL_NORMAL);
    if (res != DD_OK)
    {
        DisplayError("Couldn't set a cooperative level.");
        return; /* to-do:  move to InitiateGFX? */
    }

    zerobuf(&ddsd, sizeof(ddsd));
    ddsd.dwSize = sizeof(ddsd);
    ddsd.dwFlags = DDSD_CAPS;
    ddsd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    res = IDirectDraw_CreateSurface(lpdd, &ddsd, &lpddsprimary, 0);
    if (res != DD_OK)
    {
        DisplayError("CreateSurface for a primary surface failed.");
        return; /* to-do:  move to InitiateGFX? */
    }

    ddsd.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT;
    ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
    ddsd.dwWidth = PRESCALE_WIDTH;
    ddsd.dwHeight = PRESCALE_HEIGHT;
    zerobuf(&ftpixel, sizeof(ftpixel));
    ftpixel.dwSize = sizeof(ftpixel);
    ftpixel.dwFlags = DDPF_RGB;
    ftpixel.dwRGBBitCount = 32;
    ftpixel.dwRBitMask = 0xff0000;
    ftpixel.dwGBitMask = 0xff00;
    ftpixel.dwBBitMask = 0xff;
    ddsd.ddpfPixelFormat = ftpixel;
    res = IDirectDraw_CreateSurface(lpdd, &ddsd, &lpddsback, 0);
    if (res == DDERR_INVALIDPIXELFORMAT)
    {
        DisplayError(
            "ARGB8888 is not supported. You can try changing desktop color "\
            "depth to 32-bit, but most likely that won't help.");
        return; /* InitiateGFX fails. */
    }
    else if (res != DD_OK)
    {
        DisplayError("CreateSurface for a secondary surface failed.");
        return; /* InitiateGFX should fail. */
    }

    res = IDirectDrawSurface_GetSurfaceDesc(lpddsback, &ddsd);
    if (res != DD_OK)
    {
        DisplayError("GetSurfaceDesc failed.");
        return; /* InitiateGFX should fail. */
    }
    if ((ddsd.lPitch & 3) || ddsd.lPitch < (PRESCALE_WIDTH << 2))
    {
        DisplayError(
            "Pitch of a secondary surface is either not 32 bit aligned or "\
            "too small.");
        return; /* InitiateGFX should fail. */
    }
    pitchindwords = ddsd.lPitch >> 2;

    res = IDirectDraw_CreateClipper(lpdd, 0, &lpddcl, 0);
    if (res != DD_OK)
    {
        DisplayError("Couldn't create a clipper.");
        return; /* InitiateGFX should fail. */
    }
    res = IDirectDrawClipper_SetHWnd(lpddcl, 0, gfx.hWnd);
    if (res != DD_OK)
    {
        DisplayError("Couldn't register a windows handle as a clipper.");
        return; /* InitiateGFX should fail. */
    }
    res = IDirectDrawSurface_SetClipper(lpddsprimary, lpddcl);
    if (res != DD_OK)
    {
        DisplayError("Couldn't attach a clipper to a surface.");
        return; /* InitiateGFX should fail. */
    }

    __src.top = __src.left = 0; 
    __src.bottom = 0;
#if SCREEN_WIDTH < PRESCALE_WIDTH
    __src.right = PRESCALE_WIDTH - 1; /* fix for undefined video card behavior */
#else
    __src.right = PRESCALE_WIDTH;
#endif
    p.x = p.y = 0;
    GetClientRect(gfx.hWnd, &__dst);
    ClientToScreen(gfx.hWnd, &p);
    OffsetRect(&__dst, p.x, p.y);
    GetClientRect(gfx.hStatusBar, &statusrect);
    __dst.bottom -= statusrect.bottom;
#endif

    rdp_init();
    overlay = ConfigGetParamBool(l_ConfigAngrylion, "VIOverlay");
    return 1;
}

EXPORT void CALL angrylionUpdateScreen(void)
{
    static int counter;

#ifdef HAVE_FRAMESKIP
    if (counter++ < skip)
        return;
    counter = 0;
#endif
    rdp_update();
    retro_return(true);
#if 0
    if (step != 0)
        MessageBox(NULL, "Updated screen.\nPaused.", "Frame Step", MB_OK);
#endif
    return;
}

EXPORT void CALL angrylionShowCFB (void)
{
    //MessageBox(NULL, "ShowCFB", NULL, MB_ICONWARNING);
    angrylionUpdateScreen();
    return;
}


EXPORT void CALL angrylionViStatusChanged (void)
{
}

EXPORT void CALL angrylionViWidthChanged (void)
{
}

EXPORT void CALL angrylionFBWrite(unsigned int addr, unsigned int size)
{
}

EXPORT void CALL angrylionFBRead(unsigned int addr)
{
}

EXPORT void CALL angrylionFBGetFrameBufferInfo(void *pinfo)
{
}

EXPORT m64p_error CALL angrylionPluginGetVersion(m64p_plugin_type *PluginType, int *PluginVersion, int *APIVersion, const char **PluginNamePtr, int *Capabilities)
{
   /* set version info */
   if (PluginType != NULL)
      *PluginType = M64PLUGIN_GFX;

   if (PluginVersion != NULL)
      *PluginVersion = 0x016304;

   if (APIVersion != NULL)
      *APIVersion = 0x020100;

   if (PluginNamePtr != NULL)
      *PluginNamePtr = "MAME/Angrylion/HatCat video Plugin";

   if (Capabilities != NULL)
      *Capabilities = 0;

   return M64ERR_SUCCESS;
}
