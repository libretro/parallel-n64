/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus-rsp-hle - main.c                                          *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
 *   Copyright (C) 2012 Bobby Smiles                                       *
 *   Copyright (C) 2009 Richard Goedeken                                   *
 *   Copyright (C) 2002 Hacktarux                                          *
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

#include <stdbool.h>
#include <stdint.h>

#ifdef ENABLE_TASK_DUMP
#include <stdio.h>
#endif

#include "hle_memory.h"
#include "hle_plugin.h"

#include "alist.h"
#include "cicx105.h"
#include "jpeg.h"
#include "musyx.h"

#define min(a,b) (((a) < (b)) ? (a) : (b))

/* some rsp status flags */
#define SP_STATUS_HALT             0x1
#define SP_STATUS_BROKE            0x2
#define SP_STATUS_INTR_ON_BREAK    0x40
#define SP_STATUS_TASKDONE         0x200

/* some rdp status flags */
#define DP_STATUS_FREEZE            0x2

/* some mips interface interrupt flags */
#define MI_INTR_SP                  0x1

/* helper functions prototypes */
static bool is_task(void);
static void rsp_break(unsigned int setbits);
static void forward_gfx_task(void);
static void forward_audio_task(void);
static void show_cfb(void);
static bool try_fast_audio_dispatching(void);
static bool try_fast_task_dispatching(void);
static void normal_task_dispatching(void);
static void non_task_dispatching(void);

static uint32_t sum_bytes(const uint8_t *bytes, uint32_t size);
#ifdef ENABLE_TASK_DUMP
static void dump_binary(const int8_t * const filename, const uint8_t* const bytes,
                        uint32_t size);
static void dump_task(const char * const filename);

static void dump_unknown_task(uint32_t sum);
static void dump_unknown_non_task(uint32_t sum);
#endif

/* local variables */
static const bool FORWARD_AUDIO = false, FORWARD_GFX = true;

/* Global functions */

void hle_execute(void)
{
   if (is_task()) {
      if (!try_fast_task_dispatching())
         normal_task_dispatching();
      rsp_break(SP_STATUS_TASKDONE);
   } else {
      non_task_dispatching();
      rsp_break(0);
   }
}

/* local functions */
static uint32_t sum_bytes(const uint8_t *bytes, uint32_t size)
{
    uint32_t sum = 0;
    const uint8_t * const bytes_end = bytes + size;

    while (bytes != bytes_end)
        sum += *bytes++;

    return sum;
}

/**
 * Try to figure if the RSP was launched using osSpTask* functions
 * and not run directly (in which case DMEM[0xfc0-0xfff] is meaningless).
 *
 * Previously, the ucode_size field was used to determine this,
 * but it is not robust enough (hi Pokemon Stadium !) because games could write anything
 * in this field : most ucode_boot discard the value and just use 0xf7f anyway.
 *
 * Using ucode_boot_size should be more robust in this regard.
 **/
static inline bool is_task(void)
{
   return (*dmem_u32(TASK_UCODE_BOOT_SIZE) <= 0x1000);
}

static void rsp_break(uint32_t setbits)
{
    *q_RspInfo.SP_STATUS_REG |= setbits | SP_STATUS_BROKE | SP_STATUS_HALT;

    if ((*q_RspInfo.SP_STATUS_REG & SP_STATUS_INTR_ON_BREAK))
    {
        *q_RspInfo.MI_INTR_REG |= MI_INTR_SP;
        q_RspInfo.CheckInterrupts();
    }
}

static void forward_gfx_task(void)
{
   if (q_RspInfo.ProcessDlistList)
   {
      q_RspInfo.ProcessDlistList();
      *q_RspInfo.DPC_STATUS_REG &= ~DP_STATUS_FREEZE;
   }
}

static void forward_audio_task(void)
{
   if (q_RspInfo.ProcessAlistList)
      q_RspInfo.ProcessAlistList();
}

static void show_cfb(void)
{
   if (q_RspInfo.ShowCFB)
      q_RspInfo.ShowCFB();
}

static bool try_fast_audio_dispatching(void)
{
    /* identify audio ucode by using the content of ucode_data */
    uint32_t ucode_data = *dmem_u32(TASK_UCODE_DATA);
    uint32_t v;

    if (*dram_u32(ucode_data) == 0x00000001)
    {
       if (*dram_u32(ucode_data + 0x30) == 0xf0000f00)
       {
          v = *dram_u32(ucode_data + 0x28);
          switch(v)
          {
             case 0x1e24138c: /* audio ABI (most common) */
                alist_process_audio(); return true;
             case 0x1dc8138c: /* GoldenEye */
                alist_process_audio_ge(); return true;
             case 0x1e3c1390: /* BlastCorp, DiddyKongRacing */
                alist_process_audio_bc(); return true;
             default:
                DebugMessage(M64MSG_WARNING, "ABI1 identification regression: v=%08x", v);
          }
       }
       else
       {
          v = *dram_u32(ucode_data + 0x10);
          switch(v)
          {
             case 0x11181350: /* MarioKart, WaveRace (E) */
                alist_process_mk(); return true;
             case 0x111812e0: /* StarFox (J) */
                alist_process_sfj(); return true;
             case 0x110412ac: /* WaveRace (J RevB) */
                alist_process_wrjb(); return true;
             case 0x110412cc: /* StarFox/LylatWars (except J) */
                alist_process_sf(); return true;
             case 0x1cd01250: /* FZeroX */
                alist_process_fz(); return true;
             case 0x1f08122c: /* YoshisStory */
                alist_process_ys(); return true;
             case 0x1f38122c: /* 1080° Snowboarding */
                alist_process_1080(); return true;
             case 0x1f681230: /* Zelda OoT / Zelda MM (J, J RevA) */
                alist_process_oot(); return true;
             case 0x1f801250: /* Zelda MM (except J, J RevA, E Beta), PokemonStadium 2 */
                alist_process_mm(); return true;
             case 0x109411f8: /* Zelda MM (E Beta) */
                alist_process_mmb(); return true;
             case 0x1eac11b8: /* AnimalCrossing */
                alist_process_ac(); return true;
             case 0x00010010: /* MusyX v2 (IndianaJones, BattleForNaboo) */
                musyx_v2_task(); return true;

             default:
                DebugMessage(M64MSG_WARNING, "ABI2 identification regression: v=%08x", v);
          }
       }
    }
    else
    {
       v = *dram_u32(ucode_data + 0x10);
       switch(v)
       {
          case 0x00000001: /* MusyX v1
                              RogueSquadron, ResidentEvil2, PolarisSnoCross,
                              TheWorldIsNotEnough, RugratsInParis, NBAShowTime,
                              HydroThunder, Tarzan, GauntletLegend, Rush2049 */
             musyx_v1_task(); return true;
          case 0x0000127c: /* naudio (many games) */
             alist_process_naudio(); return true;
          case 0x00001280: /* BanjoKazooie */
             alist_process_naudio_bk(); return true;
          case 0x1c58126c: /* DonkeyKong */
             alist_process_naudio_dk(); return true;
          case 0x1ae8143c: /* BanjoTooie, JetForceGemini, MickeySpeedWayUSA, PerfectDark */
             alist_process_naudio_mp3(); return true;
          case 0x1ab0140c: /* ConkerBadFurDay */
             alist_process_naudio_cbfd(); return true;

          default:
             DebugMessage(M64MSG_WARNING, "ABI3 identification regression: v=%08x", v);
       }
    }
    
    return false;
}

static bool try_fast_task_dispatching(void)
{
    /* identify task ucode by its type */

    switch (*dmem_u32(TASK_TYPE))
    {
       case 1:
          if (FORWARD_GFX)
          {
             forward_gfx_task();
             return true;
          }
          break;
       case 2:
          if (FORWARD_AUDIO)
          {
             forward_audio_task();
             return true;
          }
          else if (try_fast_audio_dispatching())
             return true;
          break;
       case 7:
          show_cfb();
          return true;
    }

    return false;
}

static void normal_task_dispatching(void)
{
    const uint32_t sum = sum_bytes((void*)dram_u32(*dmem_u32(TASK_UCODE)), min(*dmem_u32(TASK_UCODE_SIZE), 0xf80) >> 1);

    switch (sum)
    {
        /* StoreVe12: found in Zelda Ocarina of Time [misleading task->type == 4] */
        case 0x278: /* Nothing to emulate */ return;

        /* GFX: Twintris [misleading task->type == 0] */                                         
        case 0x212ee:
            if (FORWARD_GFX) { forward_gfx_task(); return; }
            break;

        /* JPEG: found in Pokemon Stadium J */ 
        case 0x2c85a: jpeg_decode_PS0(); return;

        /* JPEG: found in Zelda Ocarina of Time, Pokemon Stadium 1, Pokemon Stadium 2 */
        case 0x2caa6: jpeg_decode_PS(); return;

        /* JPEG: found in Ogre Battle, Bottom of the 9th */
        case 0x130de:
        case 0x278b0:
                      jpeg_decode_OB(); return;
    }

#ifdef ENABLE_TASK_DUMP
    dump_unknown_task(sum);
#endif
}

static void non_task_dispatching(void)
{
    const uint32_t sum = sum_bytes(q_RspInfo.IMEM, 0x1000 >> 1);

    switch(sum)
    {
        /* CIC x105 ucode (used during boot of CIC x105 games) */
        case 0x9e2: /* CIC 6105 */
        case 0x9f2: /* CIC 7105 */
            cicx105_ucode(); return;
    }

#ifdef ENABLE_TASK_DUMP
    dump_unknown_non_task(sum);
#endif
}


/* DLL-exported functions */


EXPORT void CALL hleInitiateRSP(RSP_INFO Rsp_Info, uint32_t *CycleCount)
{
    q_RspInfo = Rsp_Info;
}

EXPORT void CALL hleRomClosed(void)
{
    memset(q_RspInfo.DMEM, 0, 0x1000);
    memset(q_RspInfo.IMEM, 0, 0x1000);
}

#ifdef ENABLE_TASK_DUMP
static void dump_unknown_task(uint32_t sum)
{
    char filename[256];
    uint32_t ucode = *dmem_u32(TASK_UCODE);
    uint32_t ucode_data = *dmem_u32(TASK_UCODE_DATA);
    uint32_t data_ptr = *dmem_u32(TASK_DATA_PTR);

    RSP_DEBUG_MESSAGE(M64MSG_WARNING, "unknown OSTask: sum %x PC:%x", sum, *q_RspInfo.SP_PC_REG);

    sprintf(&filename[0], "task_%x.log", sum);
    dump_task(filename);
    
    // dump ucode_boot
    sprintf(&filename[0], "ucode_boot_%x.bin", sum);
    dump_binary(filename, (void*)dram_u32(*dmem_u32(TASK_UCODE_BOOT)), *dmem_u32(TASK_UCODE_BOOT_SIZE));

    // dump ucode
    if (ucode != 0)
    {
        sprintf(&filename[0], "ucode_%x.bin", sum);
        dump_binary(filename, (void*)dram_u32(ucode), 0xf80);
    }

    // dump ucode_data
    if (ucode_data != 0)
    {
        sprintf(&filename[0], "ucode_data_%x.bin", sum);
        dump_binary(filename, (void*)dram_u32(ucode_data), *dmem_u32(TASK_UCODE_DATA_SIZE));
    }

    // dump data
    if (data_ptr != 0)
    {
        sprintf(&filename[0], "data_%x.bin", sum);
        dump_binary(filename, (void*)dram_u32(data_ptr), *dmem_u32(TASK_DATA_SIZE));
    }
}

static void dump_unknown_non_task(uint32_t sum)
{
    char filename[256];

    RSP_DEBUG_MESSAGE(M64MSG_WARNING, "unknown RSP code: sum: %x PC:%x", sum, *q_RspInfo.SP_PC_REG);

    // dump IMEM & DMEM for further analysis
    sprintf(&filename[0], "imem_%x.bin", sum);
    dump_binary(filename, q_RspInfo.IMEM, 0x1000);

    sprintf(&filename[0], "dmem_%x.bin", sum);
    dump_binary(filename, q_RspInfo.DMEM, 0x1000);
}

static void dump_binary(const int8_t * const filename, const uint8_t * const bytes,
                        uint32_t size)
{
    FILE *f;

    // if file already exists, do nothing
    f = fopen(filename, "r");
    if (f == NULL)
    {
        // else we write bytes to the file
        f= fopen(filename, "wb");
        if (f != NULL) {
            if (fwrite(bytes, 1, size, f) != size)
            {
                RSP_DEBUG_MESSAGE(M64MSG_ERROR, "Writing error on %s", filename);
            }
            fclose(f);
        }
#ifndef DEBUG
        else
        {
            RSP_DEBUG_MESSAGE(M64MSG_ERROR, "Couldn't open %s for writing !", filename);
        }
#endif
    }
    else
    {
        fclose(f);
    }
}

static void dump_task(const char * const filename)
{
    FILE *f;

    f = fopen(filename, "r");
    if (f == NULL)
    {
        f = fopen(filename, "w");
        fprintf(f,
            "type = %d\n"
            "flags = %d\n"
            "ucode_boot  = %#08x size  = %#x\n"
            "ucode       = %#08x size  = %#x\n"
            "ucode_data  = %#08x size  = %#x\n"
            "dram_stack  = %#08x size  = %#x\n"
            "output_buff = %#08x *size = %#x\n"
            "data        = %#08x size  = %#x\n"
            "yield_data  = %#08x size  = %#x\n",
            *dmem_u32(TASK_TYPE),
            *dmem_u32(TASK_FLAGS),
            *dmem_u32(TASK_UCODE_BOOT),     *dmem_u32(TASK_UCODE_BOOT_SIZE),
            *dmem_u32(TASK_UCODE),          *dmem_u32(TASK_UCODE_SIZE),
            *dmem_u32(TASK_UCODE_DATA),     *dmem_u32(TASK_UCODE_DATA_SIZE),
            *dmem_u32(TASK_DRAM_STACK),     *dmem_u32(TASK_DRAM_STACK_SIZE),
            *dmem_u32(TASK_OUTPUT_BUFF),    *dmem_u32(TASK_OUTPUT_BUFF_SIZE),
            *dmem_u32(TASK_DATA_PTR),       *dmem_u32(TASK_DATA_SIZE),
            *dmem_u32(TASK_YIELD_DATA_PTR), *dmem_u32(TASK_YIELD_DATA_SIZE));
        fclose(f);
    }
    else
    {
        fclose(f);
    }
}
#endif
