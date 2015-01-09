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

uint32_t *blitter_buf;
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
    if (blitter_buf)
       free(blitter_buf);

    SaveLoaded = 1;
    command_counter = 0;
}

static m64p_handle l_ConfigAngrylion;
 
EXPORT int CALL angrylionRomOpen (void)
{
   screen_width = SCREEN_WIDTH;
   screen_height = SCREEN_HEIGHT;
   blitter_buf = (uint32_t*)calloc(
         PRESCALE_WIDTH * PRESCALE_HEIGHT, sizeof(uint32_t)
         );
   pitchindwords = PRESCALE_WIDTH / 1; /* sizeof(DWORD) == sizeof(pixel) == 4 */
   screen_pitch = PRESCALE_WIDTH << 2;

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
