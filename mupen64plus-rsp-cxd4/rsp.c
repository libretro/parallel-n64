#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Rsp_#1.1.h"
#include "rsp.h"
#include "bench.h"

// TODO: What's the REAL new version other than 0x0101 + 1?
#define RSP_CXD4_VERSION 0x0102

#if defined(M64P_PLUGIN_API)
#define RSP_PLUGIN_API_VERSION 0x020000
static int l_PluginInit = 0;

EXPORT m64p_error CALL PluginStartup(m64p_dynlib_handle CoreLibHandle, void *Context,
                                     void (*DebugCallback)(void *, int, const char *))
{
    if (l_PluginInit)
        return M64ERR_ALREADY_INIT;

    l_PluginInit = 1;
    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginShutdown(void)
{
    if (!l_PluginInit)
        return M64ERR_NOT_INIT;

    l_PluginInit = 0;
    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL cxd4PluginGetVersion(m64p_plugin_type *PluginType, int *PluginVersion, int *APIVersion, const char **PluginNamePtr, int *Capabilities)
{
    /* set version info */
    if (PluginType != NULL)
        *PluginType = M64PLUGIN_RSP;

    if (PluginVersion != NULL)
        *PluginVersion = RSP_CXD4_VERSION;

    if (APIVersion != NULL)
        *APIVersion = RSP_PLUGIN_API_VERSION;

    if (PluginNamePtr != NULL)
        *PluginNamePtr = DLL_name;

    if (Capabilities != NULL)
    {
        *Capabilities = 0;
    }

    return M64ERR_SUCCESS;
}
#endif

EXPORT void CALL CloseDLL(void)
{
    RSP.RDRAM = NULL; /* so DllTest benchmark doesn't think ROM is still open */
    return;
}
static const char DLL_about[] =
    "RSP Interpreter by Iconoclast&&ECHO."\
    "&&ECHO "\
    "Thanks for test RDP:  Jabo, ziggy, and angrylion\n"\
    "RSP driver examples:  bpoint, zilmar, and Ville Linde\n"\
    "Helpful shenanigans:  MarathonMan, dsx, and mudlord";
EXPORT void CALL DllAbout(HWND hParent)
{
    hParent = NULL;
    message(DLL_about, 3);
    return;
}
EXPORT void CALL DllConfig(HWND hParent)
{
    hParent = NULL;
    return;
}
EXPORT unsigned int CALL cxd4DoRspCycles(unsigned int cycles)
{
    if (*RSP.SP_STATUS_REG & 0x00000003)
    {
        message("SP_STATUS_HALT", 3);
        return 0x00000000;
    }
    switch (*(unsigned int *)(RSP.DMEM + 0xFC0))
    { /* Simulation barrier to redirect processing externally. */
#ifdef EXTERN_COMMAND_LIST_GBI
        case 0x00000001:
            if (*(unsigned int *)(RSP.DMEM + 0xFF0) == 0x00000000)
                break; /* Resident Evil 2 */
            if (RSP.ProcessDlistList == NULL) {/*branch next*/} else
                RSP.ProcessDlistList();
            *RSP.SP_STATUS_REG |= 0x00000203;
            if (*RSP.SP_STATUS_REG & 0x00000040) /* SP_STATUS_INTR_BREAK */
            {
                *RSP.MI_INTR_REG |= 0x00000001; /* VR4300 SP interrupt */
                RSP.CheckInterrupts();
            }
            if (*RSP.DPC_STATUS_REG & 0x00000002) /* DPC_STATUS_FREEZE */
            {
                message("DPC_CLR_FREEZE", 2);
                *RSP.DPC_STATUS_REG &= ~0x00000002;
            }
            return 0;
#endif
#ifdef EXTERN_COMMAND_LIST_ABI
        case 0x00000002: /* OSTask.type == M_AUDTASK */
            if (RSP.ProcessAlistList == 0) {} else
                RSP.ProcessAlistList();
            *RSP.SP_STATUS_REG |= 0x00000203;
            if (*RSP.SP_STATUS_REG & 0x00000040) /* SP_STATUS_INTR_BREAK */
            {
                *RSP.MI_INTR_REG |= 0x00000001; /* VR4300 SP interrupt */
                RSP.CheckInterrupts();
            }
            return 0;
#endif
    }
    run_task();
    return (cycles);
}
EXPORT void CALL GetDllInfo(PLUGIN_INFO *PluginInfo)
{
    PluginInfo -> Version = 0x0101; /* zilmar #1.1 (only standard RSP spec) */
    PluginInfo -> Type = PLUGIN_TYPE_RSP;
strcpy(
    PluginInfo -> Name, DLL_name);
    PluginInfo -> NormalMemory = 0;
    PluginInfo -> MemoryBswaped = 1;
    return;
}
EXPORT void CALL cxd4InitiateRSP(RSP_INFO Rsp_Info, unsigned int *CycleCount)
{
    if (CycleCount != NULL) /* cycle-accuracy not doable with today's hosts */
        *CycleCount = 0x00000000;

    RSP.DMEM = Rsp_Info.DMEM;
    RSP.IMEM = Rsp_Info.IMEM;
    if (RSP.DMEM == RSP.IMEM) /* usually dummy RSP data, not to start ROM */
        return; /* DMA is not executed just because plugin initiates. */
    while (RSP.IMEM != RSP.DMEM + 4096)
        message("Virtual host map noncontiguity.", 3);

    RSP = Rsp_Info;
    *RSP.SP_PC_REG = 0x04001000 & 0x00000FFF; /* task init bug on Mupen64 */
    return;
}
EXPORT void CALL InitiateRSPDebugger(DEBUG_INFO DebugInfo)
{
    DebugInfo.UpdateMemory();
    return;
}
EXPORT void CALL cxd4RomClosed(void)
{
    *RSP.SP_PC_REG = 0x00000000;
    return;
}
