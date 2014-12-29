/*
 * z64
 *
 * Copyright (C) 2007  ziggy
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
**/

#include "rsp.h"
#include <assert.h>
#include <stdarg.h>

#define RSP_Z64_VERSION        0x020000
#define RSP_PLUGIN_API_VERSION 0x020000

#if defined(VIDEO_HLE_ALLOWED) && defined(AUDIO_HLE_ALLOWED)
#define L_NAME "Z64 RSP Plugin (HLE)"
#elif defined(VIDEO_HLE_ALLOWED)
#define L_NAME "Z64 RSP Plugin (MLE)"
#elif defined(AUDIO_HLE_ALLOWED)
#define L_NAME "Z64 RSP Plugin (LLE)"
#else
#define L_NAME "Z64 RSP Plugin"
#endif

static void (*l_DebugCallback)(void *, int, const char *) = NULL;
static void *l_DebugCallContext = NULL;
static bool l_PluginInit = false;

#if 0
static void dump()
{
    FILE * fp = fopen("rsp.dump", "w");
    assert(fp);
    fwrite(rdram, 8*1024, 1024, fp);
    fwrite(rsp_dmem, 0x2000, 1, fp);
    fwrite(rsp.ext.MI_INTR_REG, 4, 1, fp);

    fwrite(rsp.ext.SP_MEM_ADDR_REG, 4, 1, fp);
    fwrite(rsp.ext.SP_DRAM_ADDR_REG, 4, 1, fp);
    fwrite(rsp.ext.SP_RD_LEN_REG, 4, 1, fp);
    fwrite(rsp.ext.SP_WR_LEN_REG, 4, 1, fp);
    fwrite(rsp.ext.SP_STATUS_REG, 4, 1, fp);
    fwrite(rsp.ext.SP_DMA_FULL_REG, 4, 1, fp);
    fwrite(rsp.ext.SP_DMA_BUSY_REG, 4, 1, fp);
    fwrite(rsp.ext.SP_PC_REG, 4, 1, fp);
    fwrite(rsp.ext.SP_SEMAPHORE_REG, 4, 1, fp);

    fwrite(rsp.ext.DPC_START_REG, 4, 1, fp);
    fwrite(rsp.ext.DPC_END_REG, 4, 1, fp);
    fwrite(rsp.ext.DPC_CURRENT_REG, 4, 1, fp);
    fwrite(rsp.ext.DPC_STATUS_REG, 4, 1, fp);
    fwrite(rsp.ext.DPC_CLOCK_REG, 4, 1, fp);
    fwrite(rsp.ext.DPC_BUFBUSY_REG, 4, 1, fp);
    fwrite(rsp.ext.DPC_PIPEBUSY_REG, 4, 1, fp);
    fwrite(rsp.ext.DPC_TMEM_REG, 4, 1, fp);
    fclose(fp);
}
#endif

void log(m64p_msg_level level, const char *msg, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, msg);
    vsnprintf(buf, 1023, msg, args);
    buf[1023]='\0';
    va_end(args);
    if (l_DebugCallback)
    {
        l_DebugCallback(l_DebugCallContext, level, buf);
    }
}

#ifdef __cplusplus
extern "C" {
#endif

    /* DLL-exported functions */
    EXPORT m64p_error CALL PluginStartup(m64p_dynlib_handle CoreLibHandle, void *Context,
        void (*DebugCallback)(void *, int, const char *))
    {

        if (l_PluginInit)
            return M64ERR_ALREADY_INIT;

        ///* first thing is to set the callback function for debug info */
        l_DebugCallback = DebugCallback;
        l_DebugCallContext = Context;

        ///* this plugin doesn't use any Core library functions (ex for Configuration), so no need to keep the CoreLibHandle */

        l_PluginInit = true;
        return M64ERR_SUCCESS;
    }

    EXPORT m64p_error CALL PluginShutdown(void)
    {
        if (!l_PluginInit)
            return M64ERR_NOT_INIT;

        ///* reset some local variable */
        l_DebugCallback = NULL;
        l_DebugCallContext = NULL;

        l_PluginInit = 0;
        return M64ERR_SUCCESS;
    }

    EXPORT m64p_error CALL PluginGetVersion(m64p_plugin_type *PluginType, int *PluginVersion, int *APIVersion, const char **PluginNamePtr, int *Capabilities)
    {
        /* set version info */
        if (PluginType != NULL)
            *PluginType = M64PLUGIN_RSP;

        if (PluginVersion != NULL)
            *PluginVersion = RSP_Z64_VERSION;

        if (APIVersion != NULL)
            *APIVersion = RSP_PLUGIN_API_VERSION;

        if (PluginNamePtr != NULL)
            *PluginNamePtr = L_NAME;

        if (Capabilities != NULL)
        {
            *Capabilities = 0;
        }

        return M64ERR_SUCCESS;
    }

    EXPORT unsigned int CALL DoRspCycles(unsigned int Cycles)
    {
        //#define VIDEO_HLE_ALLOWED
        //#define AUDIO_HLE_ALLOWED

#if defined (AUDIO_HLE_ALLOWED) || defined (VIDEO_HLE_ALLOWED)
        unsigned int TaskType = *(unsigned int *)(z64_rspinfo.DMEM + 0xFC0);
#endif

#ifdef VIDEO_HLE_ALLOWED
#if 0
        if (TaskType == 1) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                switch (event.type) {
                    case SDL_KEYDOWN:
                        switch (event.key.keysym.sym) {
                            case 'd':
                                printf("Dumping !\n");
                                dump();
                                break;
                        }
                        break;
                }
            }
        }
#endif

        if (TaskType == 1) {
            if (z64_rspinfo.ProcessDlistList != NULL) {
                z64_rspinfo.ProcessDlistList();
            }
            *z64_rspinfo.SP_STATUS_REG |= (0x0203);
            if ((*z64_rspinfo.SP_STATUS_REG & SP_STATUS_INTR_BREAK) != 0 ) {
                *z64_rspinfo.MI_INTR_REG |= R4300i_SP_Intr;
                z64_rspinfo.CheckInterrupts();
            }

            *z64_rspinfo.DPC_STATUS_REG &= ~0x0002;
            return Cycles;
        }
#endif

#ifdef AUDIO_HLE_ALLOWED
        if (TaskType == 2) {
            if (z64_rspinfo.ProcessAlistList != NULL) {
                z64_rspinfo.ProcessAlistList();
            }
            *z64_rspinfo.SP_STATUS_REG |= (0x0203);
            if ((*z64_rspinfo.SP_STATUS_REG & SP_STATUS_INTR_BREAK) != 0 ) {
                *z64_rspinfo.MI_INTR_REG |= R4300i_SP_Intr;
                z64_rspinfo.CheckInterrupts();
            }
            return Cycles;
        }  
#endif

        if (z64_rspinfo.CheckInterrupts==NULL)
            log(M64MSG_WARNING, "Emulator doesn't provide CheckInterrupts routine");
        return rsp_execute(0x100000);
        //return Cycles;
    }

    EXPORT void CALL InitiateRSP(RSP_INFO Rsp_Info, unsigned int *CycleCount)
    {
        log(M64MSG_STATUS, "INITIATE RSP");
        rsp_init(Rsp_Info);
        memset(((UINT32*)z64_rspinfo.DMEM), 0, 0x2000);
        //*CycleCount = 0; //Causes segfault, doesn't seem to be used anyway
    }

    EXPORT void CALL RomClosed(void)
    {
        extern int rsp_gen_cache_hit;
        extern int rsp_gen_cache_miss;
        log(M64MSG_STATUS, "cache hit %d miss %d %g%%", rsp_gen_cache_hit, rsp_gen_cache_miss,
            rsp_gen_cache_miss*100.0f/rsp_gen_cache_hit);
        rsp_gen_cache_hit = rsp_gen_cache_miss = 0;

#ifdef RSPTIMING
        int i,j;
        UINT32 op, op2;

        for(i=0; i<0x140;i++) {
            if (i>=0x100)
                op = (0x12<<26) | (0x10 << 21) | (i&0x3f);
            else if (i>=0xc0)
                op = (0x3a<<26) | ((i&0x1f)<<11);
            else if (i>=0xa0)
                op = (0x32<<26) | ((i&0x1f)<<11);
            else if (i>=0x80)
                op = (0x12<<26) | ((i&0x1f)<<21);
            else if (i>=0x40)
                op = (0<<26) | (i&0x3f);
            else
                op = (i&0x3f)<<26;

            char s[128], s2[128];
            rsp_dasm_one(s, 0x800, op);
            //rsp_dasm_one(s2, 0x800, op2);
            if (rsptimings[i])
                printf("%10g %10g %7d\t%30s\n"
                /*"%10g %10g %7d\t%30s\n"*/,
                rsptimings[i]/(rspcounts[i]*1.0f), rsptimings[i]*(1.0f), rspcounts[i], s//,
                //timings[k]/1.0f/counts[k], counts[k], s2
                );
        }
#endif

        //rsp_init(z64_rspinfo);
    }
#ifdef __cplusplus
}
#endif
