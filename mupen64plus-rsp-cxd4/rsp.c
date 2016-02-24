/******************************************************************************\
* Authors:  Iconoclast                                                         *
* Release:  2013.12.12                                                         *
* License:  CC0 Public Domain Dedication                                       *
*                                                                              *
* To the extent possible under law, the author(s) have dedicated all copyright *
* and related and neighboring rights to this software to the public domain     *
* worldwide. This software is distributed without any warranty.                *
*                                                                              *
* You should have received a copy of the CC0 Public Domain Dedication along    *
* with this software.                                                          *
* If not, see <http://creativecommons.org/publicdomain/zero/1.0/>.             *
\******************************************************************************/

const char DLL_name[100] = "Static Interpreter";

unsigned char conf[32];

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "Rsp_#1.1.h"
#include "rsp.h"
#include "bench.h"
#include "m64p_plugin.h"


#define RSP_CXD4_VERSION 0x0101

#include <stdarg.h>

#define RSP_PLUGIN_API_VERSION 0x020000
#define CONFIG_API_VERSION       0x020100
#define CONFIG_PARAM_VERSION     1.00

static void (*l_DebugCallback)(void *, int, const char *) = NULL;
static void *l_DebugCallContext = NULL;
static int l_PluginInit = 0;
static m64p_handle l_ConfigRsp;
extern RSP_INFO rsp_info;

#define VERSION_PRINTF_SPLIT(x) (((x) >> 16) & 0xffff), (((x) >> 8) & 0xff), ((x) & 0xff)

NOINLINE void update_conf(const char* source)
{
    memset(conf, 0, sizeof(conf));

    CFG_HLE_GFX = ConfigGetParamBool(l_ConfigRsp, "DisplayListToGraphicsPlugin");
    CFG_HLE_AUD = ConfigGetParamBool(l_ConfigRsp, "AudioListToAudioPlugin");
    CFG_WAIT_FOR_CPU_HOST = ConfigGetParamBool(l_ConfigRsp, "WaitForCPUHost");
    CFG_MEND_SEMAPHORE_LOCK = ConfigGetParamBool(l_ConfigRsp, "SupportCPUSemaphoreLock");
}

static void DebugMessage(int level, const char *message, ...)
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

EXPORT m64p_error CALL PluginStartup(m64p_dynlib_handle CoreLibHandle, void *Context,
                                     void (*DebugCallback)(void *, int, const char *))
{
    float fConfigParamsVersion = 0.0f;

    if (l_PluginInit)
        return M64ERR_ALREADY_INIT;

    /* first thing is to set the callback function for debug info */
    l_DebugCallback = DebugCallback;
    l_DebugCallContext = Context;

    /* set the default values for this plugin */
    ConfigSetDefaultFloat(l_ConfigRsp, "Version", CONFIG_PARAM_VERSION,  "Mupen64Plus cxd4 RSP Plugin config parameter version number");
    ConfigSetDefaultBool(l_ConfigRsp, "DisplayListToGraphicsPlugin", 0, "Send display lists to the graphics plugin");
    ConfigSetDefaultBool(l_ConfigRsp, "AudioListToAudioPlugin", 0, "Send audio lists to the audio plugin");
    ConfigSetDefaultBool(l_ConfigRsp, "WaitForCPUHost", 0, "Force CPU-RSP signals synchronization");
    ConfigSetDefaultBool(l_ConfigRsp, "SupportCPUSemaphoreLock", 0, "Support CPU-RSP semaphore lock");

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
        *Capabilities = 0;

    return M64ERR_SUCCESS;
}

EXPORT int CALL RomOpen(void)
{
    if (!l_PluginInit)
        return 0;

    update_conf(CFG_FILE);
    return 1;
}

EXPORT unsigned int CALL cxd4DoRspCycles(unsigned int cycles)
{
   unsigned int i;
   if (*RSP.SP_STATUS_REG & 0x00000003)
   {
      message("SP_STATUS_HALT", 3);
      return 0x00000000;
   }
   switch (*(unsigned int *)(RSP.DMEM + 0xFC0))
   { /* Simulation barrier to redirect processing externally. */
#ifdef EXTERN_COMMAND_LIST_GBI
      case 0x00000001:
         if (CFG_HLE_GFX == 0)
            break;
         if (*(unsigned int *)(RSP.DMEM + 0xFF0) == 0x00000000)
            break; /* Resident Evil 2 */
         if (rsp_info.ProcessDlistList == NULL) {/*branch next*/} else
            rsp_info.ProcessDlistList();
         *RSP.SP_STATUS_REG |= 0x00000203;
         if (*RSP.SP_STATUS_REG & 0x00000040) /* SP_STATUS_INTR_BREAK */
         {
            *RSP.MI_INTR_REG |= 0x00000001; /* VR4300 SP interrupt */
            rsp_info.CheckInterrupts();
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
         if (CFG_HLE_AUD == 0)
            break;
         if (rsp_info.ProcessAlistList == 0) {} else
            rsp_info.ProcessAlistList();
         *RSP.SP_STATUS_REG |= 0x00000203;
         if (*RSP.SP_STATUS_REG & 0x00000040) /* SP_STATUS_INTR_BREAK */
         {
            *RSP.MI_INTR_REG |= 0x00000001; /* VR4300 SP interrupt */
            rsp_info.CheckInterrupts();
         }
         return 0;
#endif
   }

   for (i = 0; i < 32; i++)
      MFC0_count[i] = 0;
   run_task();

   if (*RSP.SP_STATUS_REG & SP_STATUS_BROKE) /* normal exit, from executing BREAK */
      return (cycles);
   else if (*RSP.MI_INTR_REG & 0x00000001) /* interrupt set by MTC0 to break */
      RSP.CheckInterrupts();
   else if (*RSP.SP_SEMAPHORE_REG != 0x00000000) /* semaphore lock fixes */
   {}
#ifdef WAIT_FOR_CPU_HOST
   else if (stale_signals != 0) /* too many iterations of MFC0:  timed out */
      MF_SP_STATUS_TIMEOUT = 16384; /* This is slow:  Make 16 if it works. */
#else
   else /* ??? unknown, possibly external intervention from CPU memory map */
   {
      message("SP_SET_HALT", 3);
      return (cycles);
   }
#endif
   *RSP.SP_STATUS_REG &= ~SP_STATUS_HALT; /* CPU restarts with the correct SIGs. */

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

RCPREG* CR[16];
int MF_SP_STATUS_TIMEOUT;
int stale_signals;
EXPORT void CALL cxd4InitiateRSP(RSP_INFO Rsp_Info, unsigned int *CycleCount)
{
    if (CycleCount != NULL) /* cycle-accuracy not doable with today's hosts */
        *CycleCount = 0x00000000;

    if (Rsp_Info.DMEM == Rsp_Info.IMEM) /* usually dummy RSP data for testing */
        return; /* DMA is not executed just because plugin initiates. */
    while (Rsp_Info.IMEM != Rsp_Info.DMEM + 4096)
        message("Virtual host map noncontiguity.", 3);

    RSP = Rsp_Info;
    *RSP.SP_PC_REG = 0x04001000 & 0x00000FFF; /* task init bug on Mupen64 */
    CR[0x0] = RSP.SP_MEM_ADDR_REG;
    CR[0x1] = RSP.SP_DRAM_ADDR_REG;
    CR[0x2] = RSP.SP_RD_LEN_REG;
    CR[0x3] = RSP.SP_WR_LEN_REG;
    CR[0x4] = RSP.SP_STATUS_REG;
    CR[0x5] = RSP.SP_DMA_FULL_REG;
    CR[0x6] = RSP.SP_DMA_BUSY_REG;
    CR[0x7] = RSP.SP_SEMAPHORE_REG;
    CR[0x8] = RSP.DPC_START_REG;
    CR[0x9] = RSP.DPC_END_REG;
    CR[0xA] = RSP.DPC_CURRENT_REG;
    CR[0xB] = RSP.DPC_STATUS_REG;
    CR[0xC] = RSP.DPC_CLOCK_REG;
    CR[0xD] = RSP.DPC_BUFBUSY_REG;
    CR[0xE] = RSP.DPC_PIPEBUSY_REG;
    CR[0xF] = RSP.DPC_TMEM_REG;
    MF_SP_STATUS_TIMEOUT = 16384;
    stale_signals = 0;
    return;
}
EXPORT void CALL cxd4RomClosed(void)
{
    *RSP.SP_PC_REG = 0x00000000;
    return;
}
