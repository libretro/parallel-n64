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
#include "m64p_plugin.h"

#define RSP_CXD4_VERSION 0x0101

#if defined(M64P_PLUGIN_API)

#include <stdarg.h>

#define RSP_PLUGIN_API_VERSION 0x020000
#define CONFIG_API_VERSION       0x020100
#define CONFIG_PARAM_VERSION     1.00

static int l_PluginInit = 0;
static m64p_handle l_ConfigRsp;
extern RSP_INFO rsp_info;

#define VERSION_PRINTF_SPLIT(x) (((x) >> 16) & 0xffff), (((x) >> 8) & 0xff), ((x) & 0xff)

NOINLINE void update_conf(void)
{
    memset(conf, 0, sizeof(conf));

    CFG_HLE_GFX = ConfigGetParamBool(l_ConfigRsp, "DisplayListToGraphicsPlugin");
    CFG_HLE_AUD = ConfigGetParamBool(l_ConfigRsp, "AudioListToAudioPlugin");
    CFG_WAIT_FOR_CPU_HOST = ConfigGetParamBool(l_ConfigRsp, "WaitForCPUHost");
    CFG_MEND_SEMAPHORE_LOCK = ConfigGetParamBool(l_ConfigRsp, "SupportCPUSemaphoreLock");
}

EXPORT m64p_error CALL PluginStartup(m64p_dynlib_handle CoreLibHandle, void *Context,
                                     void (*DebugCallback)(void *, int, const char *))
{
    float fConfigParamsVersion = 0.0f;

    if (l_PluginInit)
        return M64ERR_ALREADY_INIT;

    /* first thing is to set the callback function for debug info */

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

    update_conf();
    return 1;
}
#else

#endif

EXPORT unsigned int CALL cxd4DoRspCycles(unsigned int cycles)
{
   OSTask_type task_type;

   if (*RSP.SP_STATUS_REG & 0x00000003) /* SP_STATUS_HALT */
      return 0x00000000;

   task_type = 0x00000000
#ifdef MSB_FIRST
      | (uint32_t)RSP.DMEM[0xFC0] << 24
      | (uint32_t)RSP.DMEM[0xFC1] << 16
      | (uint32_t)RSP.DMEM[0xFC2] <<  8
      | (uint32_t)RSP.DMEM[0xFC3] <<  0;
#else
      | *((int32_t*)(RSP.DMEM + 0x000FC0U));
#endif

   switch (task_type)
   {
      /* Simulation barrier to redirect processing externally. */
#ifdef EXTERN_COMMAND_LIST_GBI
      case M_GFXTASK:
         if (CFG_HLE_GFX == 0)
            break;

         if (*(int32_t*)(RSP.DMEM + 0xFF0) == 0x00000000)
            break; /* Resident Evil 2, null task pointers */

         if (rsp_info.ProcessDlistList != NULL)
            rsp_info.ProcessDlistList();

         *RSP.SP_STATUS_REG |= 0x00000203;

         if (*RSP.SP_STATUS_REG & 0x00000040) /* SP_STATUS_INTR_BREAK */
         {
            *RSP.MI_INTR_REG |= 0x00000001; /* VR4300 SP interrupt */
            rsp_info.CheckInterrupts();
         }
         if (*RSP.DPC_STATUS_REG & 0x00000002) /* DPC_STATUS_FREEZE */
         {
            /* DPC_CLR_FREEZE */
            *RSP.DPC_STATUS_REG &= ~0x00000002;
         }
         return 0;
#endif
#ifdef EXTERN_COMMAND_LIST_ABI
      case M_AUDTASK:
         if (CFG_HLE_AUD == 0)
            break;

         if (rsp_info.ProcessAlistList != 0)
            rsp_info.ProcessAlistList();

         *RSP.SP_STATUS_REG |= 0x00000203;

         if (*RSP.SP_STATUS_REG & 0x00000040) /* SP_STATUS_INTR_BREAK */
         {
            *RSP.MI_INTR_REG |= 0x00000001; /* VR4300 SP interrupt */
            rsp_info.CheckInterrupts();
         }
         return 0;
#endif
      case M_VIDTASK:
         /* stub */
         break;
      case M_NJPEGTASK:
         /* Zelda, Pokemon, others */
         break;
      case M_NULTASK:
         break;
      case M_HVQMTASK:
         if (rsp_info.ShowCFB != 0)
            rsp_info.ShowCFB(); /* forced FB refresh in case gfx plugin skip */
         break;
   }

   run_task();

   /*
    * An optional EMMS when compiling with Intel SIMD or MMX support.
    *
    * Whether or not MMX has been executed in this emulator, here is a good time
    * to finally empty the MM state, at the end of a long interpreter loop.
    */
#ifdef ARCH_MIN_SSE2
      _mm_empty();
#endif
   return (cycles);
}

RCPREG* CR[16];

EXPORT void CALL cxd4InitiateRSP(RSP_INFO Rsp_Info, unsigned int *CycleCount)
{
    if (CycleCount != NULL) /* cycle-accuracy not doable with today's hosts */
        *CycleCount = 0x00000000;

    if (Rsp_Info.DMEM == Rsp_Info.IMEM) /* usually dummy RSP data for testing */
        return; /* DMA is not executed just because plugin initiates. */

#if 0
    while (Rsp_Info.IMEM != Rsp_Info.DMEM + 4096)
        message("Virtual host map noncontiguity.", 3);
#endif

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
}

EXPORT void CALL cxd4RomClosed(void)
{
    *RSP.SP_PC_REG = 0x00000000;
    return;
}
