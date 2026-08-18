/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - plugin.c                                                *
 *   Mupen64Plus homepage: https://mupen64plus.org/                        *
 *   Copyright (C) 2002 Hacktarux                                          *
 *   Copyright (C) 2009 Richard Goedeken                                   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.          *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "api/callbacks.h"
#include "api/m64p_common.h"
#include "api/m64p_plugin.h"
#include "api/m64p_types.h"
#include "device/device.h"
#include "device/r4300/interrupt.h"
#include "device/memory/m64p_memory.h"
#include "device/r4300/r4300_core.h"
#include "device/rcp/ai/ai_controller.h"
#include "device/rcp/mi/mi_controller.h"
#include "device/rcp/rdp/rdp_core.h"
#include "device/rcp/rsp/rsp_core.h"
#include "device/rcp/vi/vi_controller.h"
#include "main/main.h"
#include "main/rom.h"
#include "main/version.h"
#include "osal/dynamiclib.h"
#include "plugin.h"
#include "mupen64plus-next_common.h"

#include <stdio.h>

CONTROL Controls[4];
void ResizeVideoOutput(int width, int height){

}
/* local data structures and functions */
#define DEFINE_GFX(X) \
    EXPORT m64p_error CALL X##PluginGetVersion(m64p_plugin_type *, int *, int *, const char **, int *); \
    EXPORT void CALL X##ChangeWindow(void); \
    EXPORT int  CALL X##InitiateGFX(GFX_INFO Gfx_Info); \
    EXPORT void CALL X##MoveScreen(int x, int y); \
    EXPORT void CALL X##ProcessDList(void); \
    EXPORT void CALL X##ProcessRDPList(void); \
    EXPORT void CALL X##RomClosed(void); \
    EXPORT int  CALL X##RomOpen(void); \
    EXPORT void CALL X##ShowCFB(void); \
    EXPORT void CALL X##UpdateScreen(void); \
    EXPORT void CALL X##ViStatusChanged(void); \
    EXPORT void CALL X##ViWidthChanged(void); \
    EXPORT void CALL X##ReadScreen2(void *dest, int *width, int *height, int front); \
    EXPORT void CALL X##SetRenderingCallback(void (*callback)(int)); \
    EXPORT void CALL X##ResizeVideoOutput(int width, int height); \
    EXPORT void CALL X##FBRead(unsigned int addr); \
    EXPORT void CALL X##FBWrite(unsigned int addr, unsigned int size); \
    EXPORT void CALL X##FBGetFrameBufferInfo(void *p); \
    \
    gfx_plugin_functions gfx_##X = { \
        X##PluginGetVersion, \
        X##ChangeWindow, \
        X##InitiateGFX, \
        X##MoveScreen, \
        X##ProcessDList, \
        X##ProcessRDPList, \
        X##RomClosed, \
        X##RomOpen, \
        X##ShowCFB, \
        X##UpdateScreen, \
        X##ViStatusChanged, \
        X##ViWidthChanged, \
        X##ReadScreen2, \
        X##SetRenderingCallback, \
        ResizeVideoOutput, \
        X##FBRead, \
        X##FBWrite, \
        X##FBGetFrameBufferInfo \
    }

#if defined(HAVE_GLN64)
DEFINE_GFX(gln64);
#endif
#if defined(HAVE_THR_AL)
DEFINE_GFX(angrylion);
#endif
#if defined(HAVE_PARALLEL)
DEFINE_GFX(parallel);
#endif
#if defined(HAVE_RICE)
DEFINE_GFX(rice);
#endif
#if defined(HAVE_GLIDEN64)
DEFINE_GFX(gliden64);
#endif
#if defined(HAVE_GLIDE64)
DEFINE_GFX(glide64);
#endif

gfx_plugin_functions gfx;
GFX_INFO gfx_info;
rsp_plugin_functions rsp;
/* Plugin used to run audio (type 2) RSP tasks. Normally the same as the
 * active RSP, but the Audio Processing option can pin it either way: audio on
 * the HLE RSP while graphics stay accurate, or audio on an LLE RSP while
 * graphics run high-level. */
rsp_plugin_functions rsp_audio;
RSP_INFO rsp_info;

/* Which RSP runs audio (type 2) tasks, from the libretro "Audio Processing"
 * core option.  Independent of the RSP that runs graphics: the two were tied
 * together before, and only one of the two useful mixed combinations could be
 * expressed. */
extern enum audio_rsp_mode_t audio_rsp_mode;




static CONTROL_INFO control_info;
static int l_RspAttached = 0;
static int l_InputAttached = 0;
static int l_AudioAttached = 0;
static int l_GfxAttached = 0;


/* local functions */
static void EmptyFunc(void)
{
}

/* RSP plugins raise MI interrupts by setting the bit in MI_INTR_REG
 * through their pointer into the register file and then calling
 * CheckInterrupts to have the core propagate it. With an EmptyFunc
 * here the write never reaches the CP0 cause logic: the BOSS Game
 * Studios microcode's WAITSIGNAL handshake (World Driver Championship,
 * Stunt Racer 64) raises SIG3 plus the SP interrupt from the HLE
 * walker and then suspends until the CPU clears the signal -- with the
 * interrupt undelivered the game's handler never runs and every thread
 * ends up parked in osRecvMesg while the idle loop spins. Evaluate the
 * MI interrupt lines against the mask exactly the way
 * signal_rcp_interrupt does after its own register write. */
static void rsp_plugin_check_interrupts(void)
{
    struct mi_controller* mi = &g_dev.mi;
    r4300_check_interrupt(mi->r4300, CP0_CAUSE_IP2,
                          mi->regs[MI_INTR_REG] & mi->regs[MI_INTR_MASK_REG]);
}
/* Angrylion raises the DP interrupt from rdp_sync_full by setting the bit
 * through its MI_INTR_REG pointer and calling CheckInterrupts.  Where the
 * list came from an RSP task, the task-end path in rsp_core picks that bit
 * up and re-delivers it deferred, so there is nothing to do here and the
 * flag says so.
 *
 * Where it did not - libdragon's rspq is a persistent task that never ends
 * that way, so the block never runs for it - nothing consumed the bit at
 * all.  It stayed set until something unrelated evaluated the interrupt
 * lines, which in practice was a guest MTC0 to Status inside its own VI
 * handler, and the DP interrupt nested there.  libdragon keeps one global
 * FP save slot rather than a stack, so the inner handler's exit freed the
 * slot the outer one was using and its next FPU access died with nowhere
 * to save.  Defer it here in that case, the same way the task-end path
 * would have.  Delivering immediately is not an option: rdp_sync_full runs
 * in the middle of list processing. */
static void gfx_plugin_check_interrupts(void)
{
    extern int g_rsp_task_consumes_dp;
    struct mi_controller* mi = &g_dev.mi;

    if (g_rsp_task_consumes_dp)
        return;

    if (!(mi->regs[MI_INTR_REG] & MI_INTR_DP))
        return;

    mi->regs[MI_INTR_REG] &= ~MI_INTR_DP;
    cp0_update_count(mi->r4300);
    add_interrupt_event(&mi->r4300->cp0, DP_INT, 4000);
}

/* RSP */
#define DEFINE_RSP(X) \
    EXPORT m64p_error CALL X##PluginGetVersion(m64p_plugin_type *, int *, int *, const char **, int *); \
    EXPORT unsigned int CALL X##DoRspCycles(unsigned int Cycles); \
    EXPORT void CALL X##InitiateRSP(RSP_INFO Rsp_Info, unsigned int *CycleCount); \
    EXPORT void CALL X##RomClosed(void); \
    \
    const rsp_plugin_functions rsp_##X = { \
        X##PluginGetVersion, \
        X##DoRspCycles, \
        X##InitiateRSP, \
        X##RomClosed \
    }

// Define RSP Interfaces
DEFINE_RSP(hle);

#ifdef HAVE_PARALLEL_RSP
DEFINE_RSP(parallelRSP);
#endif // HAVE_PARALLEL_RSP

#if HAVE_LLE
DEFINE_RSP(cxd4);
#endif // HAVE_LLE

m64p_error plugin_start_gfx(void)
{
    printf("plugin_start_gfx\n");

    uint8_t media = *((uint8_t*)mem_base_u32(g_mem_base, MM_CART_ROM) + (0x3b ^ S8));

    /* Here we feed 64DD IPL ROM header to GFX plugin if 64DD is present.
     * We use g_media_loader.get_dd_rom to detect 64DD presence
     * instead of g_dev because the latter is not yet initialized at plugin_start time */
    /* XXX: Not sure it is the best way to convey which game is being played to the GFX plugin
     * as 64DD IPL is the same for all 64DD games... */
    char* dd_ipl_rom_filename = (g_media_loader.get_dd_rom == NULL)
        ? NULL
        : g_media_loader.get_dd_rom(g_media_loader.cb_data);

    uint32_t rom_base = (g_rom_size == 0 || (dd_ipl_rom_filename != NULL && strlen(dd_ipl_rom_filename) != 0 && media != 'C'))
        ? MM_DD_ROM
        : MM_CART_ROM;

    free(dd_ipl_rom_filename);

    /* fill in the GFX_INFO data structure */
    gfx_info.HEADER = (unsigned char *)mem_base_u32(g_mem_base, rom_base);
    gfx_info.RDRAM = (unsigned char *)mem_base_u32(g_mem_base, MM_RDRAM_DRAM);
    gfx_info.DMEM = (unsigned char *)mem_base_u32(g_mem_base, MM_RSP_MEM);
    gfx_info.IMEM = (unsigned char *)mem_base_u32(g_mem_base, MM_RSP_MEM + 0x1000);
    gfx_info.MI_INTR_REG = &(g_dev.mi.regs[MI_INTR_REG]);
    gfx_info.DPC_START_REG = &(g_dev.dp.dpc_regs[DPC_START_REG]);
    gfx_info.DPC_END_REG = &(g_dev.dp.dpc_regs[DPC_END_REG]);
    gfx_info.DPC_CURRENT_REG = &(g_dev.dp.dpc_regs[DPC_CURRENT_REG]);
    gfx_info.DPC_STATUS_REG = &(g_dev.dp.dpc_regs[DPC_STATUS_REG]);
    gfx_info.DPC_CLOCK_REG = &(g_dev.dp.dpc_regs[DPC_CLOCK_REG]);
    gfx_info.DPC_BUFBUSY_REG = &(g_dev.dp.dpc_regs[DPC_BUFBUSY_REG]);
    gfx_info.DPC_PIPEBUSY_REG = &(g_dev.dp.dpc_regs[DPC_PIPEBUSY_REG]);
    gfx_info.DPC_TMEM_REG = &(g_dev.dp.dpc_regs[DPC_TMEM_REG]);
    gfx_info.VI_STATUS_REG = &(g_dev.vi.regs[VI_STATUS_REG]);
    gfx_info.VI_ORIGIN_REG = &(g_dev.vi.regs[VI_ORIGIN_REG]);
    gfx_info.VI_WIDTH_REG = &(g_dev.vi.regs[VI_WIDTH_REG]);
    gfx_info.VI_INTR_REG = &(g_dev.vi.regs[VI_V_INTR_REG]);
    gfx_info.VI_V_CURRENT_LINE_REG = &(g_dev.vi.regs[VI_CURRENT_REG]);
    gfx_info.VI_TIMING_REG = &(g_dev.vi.regs[VI_BURST_REG]);
    gfx_info.VI_V_SYNC_REG = &(g_dev.vi.regs[VI_V_SYNC_REG]);
    gfx_info.VI_H_SYNC_REG = &(g_dev.vi.regs[VI_H_SYNC_REG]);
    gfx_info.VI_LEAP_REG = &(g_dev.vi.regs[VI_LEAP_REG]);
    gfx_info.VI_H_START_REG = &(g_dev.vi.regs[VI_H_START_REG]);
    gfx_info.VI_V_START_REG = &(g_dev.vi.regs[VI_V_START_REG]);
    gfx_info.VI_V_BURST_REG = &(g_dev.vi.regs[VI_V_BURST_REG]);
    gfx_info.VI_X_SCALE_REG = &(g_dev.vi.regs[VI_X_SCALE_REG]);
    gfx_info.VI_Y_SCALE_REG = &(g_dev.vi.regs[VI_Y_SCALE_REG]);
    gfx_info.CheckInterrupts = gfx_plugin_check_interrupts;
    
    gfx_info.version = 2; //Version 2 added SP_STATUS_REG and RDRAM_SIZE
    gfx_info.SP_STATUS_REG = &g_dev.sp.regs[SP_STATUS_REG];
    gfx_info.RDRAM_SIZE = (unsigned int*) &g_dev.rdram.dram_size;

    /* call the audio plugin */
    if (!gfx.initiateGFX(gfx_info))
        return M64ERR_PLUGIN_FAIL;

    return M64ERR_SUCCESS;
}

static m64p_error plugin_start_input(void)
{
    int i;

    /* fill in the CONTROL_INFO data structure */
    control_info.Controls = Controls;
    for (i=0; i<4; i++)
      {
         Controls[i].Present = 0;
         Controls[i].RawData = 0;
         Controls[i].Plugin = PLUGIN_NONE;
         Controls[i].Type = CONT_TYPE_STANDARD;
      }

    /* call the input plugin */
    inputInitiateControllers(control_info);

    return M64ERR_SUCCESS;
}

static m64p_error plugin_start_rsp(void)
{
    /* fill in the RSP_INFO data structure */
    rsp_info.RDRAM = (unsigned char *)mem_base_u32(g_mem_base, MM_RDRAM_DRAM);
    rsp_info.DMEM = (unsigned char *)mem_base_u32(g_mem_base, MM_RSP_MEM);
    rsp_info.IMEM = (unsigned char *)mem_base_u32(g_mem_base, MM_RSP_MEM + 0x1000);
    rsp_info.MI_INTR_REG = &g_dev.mi.regs[MI_INTR_REG];
    rsp_info.SP_MEM_ADDR_REG = &g_dev.sp.regs[SP_MEM_ADDR_REG];
    rsp_info.SP_DRAM_ADDR_REG = &g_dev.sp.regs[SP_DRAM_ADDR_REG];
    rsp_info.SP_RD_LEN_REG = &g_dev.sp.regs[SP_RD_LEN_REG];
    rsp_info.SP_WR_LEN_REG = &g_dev.sp.regs[SP_WR_LEN_REG];
    rsp_info.SP_STATUS_REG = &g_dev.sp.regs[SP_STATUS_REG];
    rsp_info.SP_DMA_FULL_REG = &g_dev.sp.regs[SP_DMA_FULL_REG];
    rsp_info.SP_DMA_BUSY_REG = &g_dev.sp.regs[SP_DMA_BUSY_REG];
    rsp_info.SP_PC_REG = &g_dev.sp.regs2[SP_PC_REG];
    rsp_info.SP_SEMAPHORE_REG = &g_dev.sp.regs[SP_SEMAPHORE_REG];
    rsp_info.DPC_START_REG = &g_dev.dp.dpc_regs[DPC_START_REG];
    rsp_info.DPC_END_REG = &g_dev.dp.dpc_regs[DPC_END_REG];
    rsp_info.DPC_CURRENT_REG = &g_dev.dp.dpc_regs[DPC_CURRENT_REG];
    rsp_info.DPC_STATUS_REG = &g_dev.dp.dpc_regs[DPC_STATUS_REG];
    rsp_info.DPC_CLOCK_REG = &g_dev.dp.dpc_regs[DPC_CLOCK_REG];
    rsp_info.DPC_BUFBUSY_REG = &g_dev.dp.dpc_regs[DPC_BUFBUSY_REG];
    rsp_info.DPC_PIPEBUSY_REG = &g_dev.dp.dpc_regs[DPC_PIPEBUSY_REG];
    rsp_info.DPC_TMEM_REG = &g_dev.dp.dpc_regs[DPC_TMEM_REG];
    rsp_info.CheckInterrupts = rsp_plugin_check_interrupts;
    rsp_info.ProcessDlistList = gfx.processDList;
    rsp_info.ProcessAlistList = NULL; /* no audio-plugin AList handler; audio type-2 tasks run on the HLE RSP (rsp_audio) */
    rsp_info.ProcessRdpList = gfx.processRDPList;
    rsp_info.ShowCFB = gfx.showCFB;

    /* call the RSP plugin  */
    rsp.initiateRSP(rsp_info, NULL);

    /* Decide which plugin processes audio (type 2) tasks. The accurate LLE RSP
     * plugins (cxd4, parallel-rsp) do not emulate the audio microcode, so when
     * one of them is active and the user asked for HLE audio, route audio lists
     * to the HLE RSP. It must be initialised with the same RSP_INFO so its
     * DMEM/RDRAM pointers are valid. When the active RSP already is HLE, or the
     * option is off, audio stays on the active RSP. */
    switch (audio_rsp_mode)
    {
    case AUDIO_RSP_HLE:
        /* Audio on the HLE RSP whatever runs graphics. */
        if (current_rsp_type != RSP_PLUGIN_HLE)
            rsp_hle.initiateRSP(rsp_info, NULL);
        rsp_audio = rsp_hle;
        break;

    case AUDIO_RSP_ACCURATE:
        /* Audio on an LLE RSP whatever runs graphics.  When graphics are on
         * the HLE RSP there is no LLE plugin started yet, so start one: this
         * is the combination the old boolean could not express, and it is the
         * one that matters, because the HLE audio microcode is what glitches
         * while HLE graphics are what people actually want for speed. */
        if (current_rsp_type == RSP_PLUGIN_HLE)
        {
#if defined(HAVE_PARALLEL_RSP)
            rsp_parallelRSP.initiateRSP(rsp_info, NULL);
            rsp_audio = rsp_parallelRSP;
#elif defined(HAVE_LLE)
            rsp_cxd4.initiateRSP(rsp_info, NULL);
            rsp_audio = rsp_cxd4;
#else
            /* No LLE RSP in this build: the request cannot be honoured, and
             * silently running audio on the HLE RSP is better than no audio. */
            rsp_audio = rsp;
#endif
        }
        else
            rsp_audio = rsp;
        break;

    case AUDIO_RSP_FOLLOW:
    default:
        rsp_audio = rsp;
        break;
    }

    return M64ERR_SUCCESS;
}

m64p_error plugin_start(m64p_plugin_type type)
{
    switch(type)
    {
        case M64PLUGIN_RSP:
            return plugin_start_rsp();
        case M64PLUGIN_GFX:
            return plugin_start_gfx();
        case M64PLUGIN_INPUT:
            return plugin_start_input();
        default:
            return M64ERR_INPUT_INVALID;
    }

    return M64ERR_INTERNAL;
}

m64p_error plugin_check(void)
{
    if (!l_GfxAttached)
        DebugMessage(M64MSG_WARNING, "No video plugin attached.  There will be no video output.");
    if (!l_RspAttached)
        DebugMessage(M64MSG_WARNING, "No RSP plugin attached.  The video output will be corrupted.");
    if (!l_AudioAttached)
        DebugMessage(M64MSG_WARNING, "No audio plugin attached.  There will be no sound output.");
    if (!l_InputAttached)
        DebugMessage(M64MSG_WARNING, "No input plugin attached.  You won't be able to control the game.");

    return M64ERR_SUCCESS;
}

enum rdp_plugin_type current_rdp_type = RDP_PLUGIN_NONE;
enum rsp_plugin_type current_rsp_type = RSP_PLUGIN_NONE;

/* global functions */
void plugin_connect_all()
{
    switch (current_rdp_type)
    {
       case RDP_PLUGIN_ANGRYLION:
#ifdef HAVE_THR_AL
          gfx = gfx_angrylion;
#endif
          break;
       case RDP_PLUGIN_PARALLEL:
#ifdef HAVE_PARALLEL
          gfx = gfx_parallel;
#endif
          break;
       case RDP_PLUGIN_RICE:
#ifdef HAVE_RICE
          gfx = gfx_rice;
#endif
          break;
       case RDP_PLUGIN_GLN64:
#ifdef HAVE_GLN64
          gfx = gfx_gln64;
#endif
          break;
       case RDP_PLUGIN_GLIDE64:
#ifdef HAVE_GLIDE64
          gfx = gfx_glide64;
#endif
          break;
       case RDP_PLUGIN_GLIDEN64:
#ifdef HAVE_GLIDEN64
          gfx = gfx_gliden64;
#elif defined(HAVE_GLN64)
          gfx = gfx_gln64;
#endif
          break;
      case RDP_PLUGIN_NONE:
      default:
         break;
    }

    l_GfxAttached = 1;
    plugin_start_gfx();

    switch (current_rsp_type)
    {
      case RSP_PLUGIN_HLE:
         rsp = rsp_hle;
         break;
      case RSP_PLUGIN_CXD4:
#ifdef HAVE_LLE
         rsp = rsp_cxd4;
#endif // HAVE_LLE
         break;
      case RSP_PLUGIN_PARALLEL:
#ifdef HAVE_PARALLEL_RSP
         rsp = rsp_parallelRSP;
#endif // HAVE_PARALLEL_RSP
         break;
      case RSP_PLUGIN_NONE:
      default:
         break;
    }

    l_RspAttached = 1;
    plugin_start_rsp();

    l_AudioAttached = 1;
    l_InputAttached = 1;
    plugin_start_input();
}

