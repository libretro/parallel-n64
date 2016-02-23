/******************************************************************************\
* Project:  Module Subsystem Interface to SP Interpreter Core                  *
* Authors:  Iconoclast                                                         *
* Release:  2015.01.30                                                         *
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef WIN32
#include <windows.h>
#endif

#include "module.h"
#include "su.h"

RSP_INFO RSP_INFO_NAME;

#define RSP_CXD4_VERSION 0x0101

#include <stdarg.h>

#define RSP_PLUGIN_API_VERSION 0x020000
#define CONFIG_API_VERSION       0x020100
#define CONFIG_PARAM_VERSION     1.00

static int l_PluginInit = 0;
static m64p_handle l_ConfigRsp;

#define VERSION_PRINTF_SPLIT(x) (((x) >> 16) & 0xffff), (((x) >> 8) & 0xff), ((x) & 0xff)

NOINLINE void update_conf(const char* source)
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
    ptr_CoreGetAPIVersions CoreAPIVersionFunc;

    int ConfigAPIVersion, DebugAPIVersion, VidextAPIVersion, bSaveConfig;
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

    if (Capabilities != NULL)
        *Capabilities = 0;

    return M64ERR_SUCCESS;
}

EXPORT int CALL RomOpen(void)
{
    if (!l_PluginInit)
        return 0;

    return 1;
}


EXPORT unsigned int CALL cxd4DoRspCycles(unsigned int cycles)
{
    if (GET_RCP_REG(SP_STATUS_REG) & 0x00000003)
    {
        message("SP_STATUS_HALT");
        return 0x00000000;
    }

    switch (*(pi32)(DMEM + 0xFC0))
    { /* Simulation barrier to redirect processing externally. */
#ifdef EXTERN_COMMAND_LIST_GBI
        case 0x00000001:
            if (CFG_HLE_GFX == 0)
                break;

            if (*(pi32)(DMEM + 0xFF0) == 0x00000000)
                break; /* Resident Evil 2, null task pointers */
            if (GET_RSP_INFO(ProcessDlistList) == NULL)
                { /* branch */ }
            else
                GET_RSP_INFO(ProcessDlistList)();
            GET_RCP_REG(SP_STATUS_REG) |= 0x00000203;
            if (GET_RCP_REG(SP_STATUS_REG) & 0x00000040)
            { /* SP_STATUS_INTR_BREAK */
                GET_RCP_REG(MI_INTR_REG) |= 0x00000001; /* R4300 SP interrupt */
                GET_RSP_INFO(CheckInterrupts)();
            }
            if (GET_RCP_REG(DPC_STATUS_REG) & 0x00000002)
            { /* DPC_STATUS_FREEZE */
                message("DPC_CLR_FREEZE");
                GET_RCP_REG(DPC_STATUS_REG) &= ~0x00000002;
            }
            return 0;
#endif
#ifdef EXTERN_COMMAND_LIST_ABI
        case 0x00000002: /* OSTask.type == M_AUDTASK */
            if (CFG_HLE_AUD == 0)
                break;

            if (GET_RSP_INFO(ProcessAlistList) == NULL)
                { /* branch */ }
            else
                GET_RSP_INFO(ProcessAlistList)();
            GET_RCP_REG(SP_STATUS_REG) |= 0x00000203;
            if (GET_RCP_REG(SP_STATUS_REG) & 0x00000040)
            { /* SP_STATUS_INTR_BREAK */
                GET_RCP_REG(MI_INTR_REG) |= 0x00000001; /* R4300 SP interrupt */
                GET_RSP_INFO(CheckInterrupts)();
            }
            return 0;
#endif
    }
    run_task();
    return (cycles);
}

EXPORT void CALL GetDllInfo(PLUGIN_INFO *PluginInfo)
{
    PluginInfo -> Version = PLUGIN_API_VERSION;
    PluginInfo -> Type = PLUGIN_TYPE_RSP;
    my_strcpy(PluginInfo -> Name, "Static Interpreter");
    PluginInfo -> NormalMemory = 0;
    PluginInfo -> MemoryBswaped = 1;
    return;
}

EXPORT void CALL cxd4InitiateRSP(RSP_INFO Rsp_Info, pu32 CycleCount)
{
    if (CycleCount != NULL) /* cycle-accuracy not doable with today's hosts */
        *CycleCount = 0x00000000;

    if (Rsp_Info.DMEM == Rsp_Info.IMEM) /* usually dummy RSP data for testing */
        return; /* DMA is not executed just because plugin initiates. */

    RSP_INFO_NAME = Rsp_Info;
    DRAM = GET_RSP_INFO(RDRAM);
    DMEM = GET_RSP_INFO(DMEM);
    IMEM = GET_RSP_INFO(IMEM);

    CR[0x0] = &GET_RCP_REG(SP_MEM_ADDR_REG);
    CR[0x1] = &GET_RCP_REG(SP_DRAM_ADDR_REG);
    CR[0x2] = &GET_RCP_REG(SP_RD_LEN_REG);
    CR[0x3] = &GET_RCP_REG(SP_WR_LEN_REG);
    CR[0x4] = &GET_RCP_REG(SP_STATUS_REG);
    CR[0x5] = &GET_RCP_REG(SP_DMA_FULL_REG);
    CR[0x6] = &GET_RCP_REG(SP_DMA_BUSY_REG);
    CR[0x7] = &GET_RCP_REG(SP_SEMAPHORE_REG);
    GET_RCP_REG(SP_PC_REG) = 0x04001000 & 0xFFF; /* task init bug on Mupen64 */
    CR[0x8] = &GET_RCP_REG(DPC_START_REG);
    CR[0x9] = &GET_RCP_REG(DPC_END_REG);
    CR[0xA] = &GET_RCP_REG(DPC_CURRENT_REG);
    CR[0xB] = &GET_RCP_REG(DPC_STATUS_REG);
    CR[0xC] = &GET_RCP_REG(DPC_CLOCK_REG);
    CR[0xD] = &GET_RCP_REG(DPC_BUFBUSY_REG);
    CR[0xE] = &GET_RCP_REG(DPC_PIPEBUSY_REG);
    CR[0xF] = &GET_RCP_REG(DPC_TMEM_REG);

    MF_SP_STATUS_TIMEOUT = 32767;
    return;
}

EXPORT void CALL cxd4RomClosed(void)
{
    GET_RCP_REG(SP_PC_REG) = 0x04001000;
}

NOINLINE void message(const char* body)
{
    printf("%s\n", body);
}

#ifdef SP_EXECUTE_LOG
void step_SP_commands(uint32_t inst)
{
    if (output_log)
    {
        const char digits[16] = {
            '0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'
        };
        char text[256];
        char offset[4] = "";
        char code[9] = "";
        unsigned char endian_swap[4];

        endian_swap[00] = (unsigned char)(inst >> 24);
        endian_swap[01] = (unsigned char)(inst >> 16);
        endian_swap[02] = (unsigned char)(inst >>  8);
        endian_swap[03] = (unsigned char)inst;
        offset[00] = digits[(*RSP.SP_PC_REG & 0xF00) >> 8];
        offset[01] = digits[(*RSP.SP_PC_REG & 0x0F0) >> 4];
        offset[02] = digits[(*RSP.SP_PC_REG & 0x00F) >> 0];
        code[00] = digits[(inst & 0xF0000000) >> 28];
        code[01] = digits[(inst & 0x0F000000) >> 24];
        code[02] = digits[(inst & 0x00F00000) >> 20];
        code[03] = digits[(inst & 0x000F0000) >> 16];
        code[04] = digits[(inst & 0x0000F000) >> 12];
        code[05] = digits[(inst & 0x00000F00) >>  8];
        code[06] = digits[(inst & 0x000000F0) >>  4];
        code[07] = digits[(inst & 0x0000000F) >>  0];
        strcpy(text, offset);
        my_strcat(text, "\n");
        my_strcat(text, code);
        message(text); /* PC offset, MIPS hex. */
        if (output_log == NULL) {} else /* Global pointer not updated?? */
            my_fwrite(endian_swap, 4, 1, output_log);
    }
}
#endif

/*
 * low-level recreations of the C standard library functions for operating
 * systems that define a C run-time or dependency on top of fixed OS calls
 *
 * Currently, this only addresses Microsoft Windows.
 *
 * None of these are meant to out-perform the original functions, by the way
 * (especially with better intrinsic compiler support for stuff like memcpy),
 * just to cut down on I-cache use for performance-irrelevant code sections
 * and to avoid std. lib run-time dependencies on certain operating systems.
 */

NOINLINE p_void my_calloc(size_t count, size_t size)
{
    return calloc(count, size);
}

NOINLINE void my_free(p_void ptr)
{
    free(ptr);
}

NOINLINE size_t my_strlen(const char* str)
{
    size_t ret_slot;

    for (ret_slot = 0; *str != '\0'; ret_slot++, str++);
    return (ret_slot);
}

NOINLINE char* my_strcpy(char* destination, const char* source)
{
    register size_t i;
    const size_t length = my_strlen(source) + 1; /* including null terminator */

    for (i = 0; i < length; i++)
        destination[i] = source[i];
    return (destination);
}

NOINLINE char* my_strcat(char* destination, const char* source)
{
    const size_t length = my_strlen(destination);

    my_strcpy(destination + length, source);
    return (destination);
}
