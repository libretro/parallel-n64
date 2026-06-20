/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - savestates.c                                            *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
 *   Copyright (C) 2012 CasualJames                                        *
 *   Copyright (C) 2009 Olejl Tillin9                                      *
 *   Copyright (C) 2008 Richard42 Tillin9                                  *
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

#include <stdlib.h>
#include <string.h>
#include <time.h>

#define M64P_CORE_PROTOTYPES 1
#include "../api/m64p_types.h"
#include "../api/callbacks.h"
#include "../api/m64p_config.h"
#include "../api/core_config.h"

#include "savestates.h"
#include "../device/device.h"
#include "main.h"
#include "rom.h"
#include "util.h"

#include "../device/rcp/ai/ai_controller.h"
#include "../device/memory/m64p_memory.h"
#include "../device/r4300/cp1.h"
#include "../device/rcp/pi/pi_controller.h"
#include "../plugin/core_plugin.h"
#include "../device/r4300/r4300_core.h"
#include "../device/rcp/rdp/rdp_core.h"
#include "../device/rcp/ri/ri_controller.h"
#include "../device/rcp/rsp/rsp_core.h"
#include "../device/rcp/si/si_controller.h"
#include "../device/rcp/vi/vi_controller.h"
#include "../device/cart/af_rtc.h"
#include "../device/pif/pif.h"
#include "../device/controllers/game_controller.h"
#include "../device/controllers/paks/transferpak.h"
#include "../device/gb/gb_cart.h"
#include "../device/gb/mbc3_rtc.h"
#include "../osal/preproc.h"

extern uint32_t RollbackRtcOnLoadState;

static const char* savestate_magic = "M64+SAVE";
static const int savestate_latest_version = 0x00010004;  /* 1.4 */

#define GETARRAY(buff, type, count) \
    (to_little_endian_buffer(buff, sizeof(type),count), \
     buff += count*sizeof(type), \
     (type *)(buff-count*sizeof(type)))
#define COPYARRAY(dst, buff, type, count) \
    memcpy(dst, GETARRAY(buff, type, count), sizeof(type)*count)
#define GETDATA(buff, type) *GETARRAY(buff, type, 1)

#define PUTARRAY(src, buff, type, count) \
    memcpy(buff, src, sizeof(type)*count); \
    to_little_endian_buffer(buff, sizeof(type), count); \
    buff += count*sizeof(type);

#define PUTDATA(buff, type, value) \
    do { type x = value; PUTARRAY(&x, buff, type, 1); } while(0)

int savestates_load_m64p(const unsigned char *data, size_t size)
{
   char queue[1024];
   int version;
   int i;
   uint32_t FCR31;
   uint32_t* cp0_regs = r4300_cp0_regs(&g_dev.r4300.cp0);
   unsigned char *curr = (unsigned char*)data; // < HACK

   /* Read and check Mupen64Plus magic number. */
   if(strncmp((char *)curr, savestate_magic, 8)!=0)
      return 0;

   curr += 8;

   version = *curr++;
   version = (version << 8) | *curr++;
   version = (version << 8) | *curr++;
   version = (version << 8) | *curr++;

   if(version != 0x00010000 && version != 0x00010001 && version != 0x00010002 && version != 0x00010003 && version != 0x00010004)
      return 0;

   /* Identity check.  New states carry the "M64H"-prefixed header
    * identity written by open_rom; reject those only on a real mismatch
    * (a state from a different game or build).  States written before
    * the MD5 removal carry an MD5 of the image here, which can no
    * longer be verified without hashing the ROM, so they are accepted
    * as-is. */
   if(!memcmp((char *)curr, "M64H", 4) && memcmp((char *)curr, ROM_SETTINGS.MD5, 32))
      return 0;

   curr += 32;

   /* Parse savestate */
   g_dev.rdram.regs[0][RDRAM_CONFIG_REG] = GETDATA(curr, uint32_t);
   g_dev.rdram.regs[0][RDRAM_DEVICE_ID_REG] = GETDATA(curr, uint32_t);
   g_dev.rdram.regs[0][RDRAM_DELAY_REG] = GETDATA(curr, uint32_t);
   g_dev.rdram.regs[0][RDRAM_MODE_REG] = GETDATA(curr, uint32_t);
   g_dev.rdram.regs[0][RDRAM_REF_INTERVAL_REG] = GETDATA(curr, uint32_t);
   g_dev.rdram.regs[0][RDRAM_REF_ROW_REG] = GETDATA(curr, uint32_t);
   g_dev.rdram.regs[0][RDRAM_RAS_INTERVAL_REG] = GETDATA(curr, uint32_t);
   g_dev.rdram.regs[0][RDRAM_MIN_INTERVAL_REG] = GETDATA(curr, uint32_t);
   g_dev.rdram.regs[0][RDRAM_ADDR_SELECT_REG] = GETDATA(curr, uint32_t);
   g_dev.rdram.regs[0][RDRAM_DEVICE_MANUF_REG] = GETDATA(curr, uint32_t);

   curr += 4; /* Padding from old implementation */
   g_dev.mi.regs[MI_INIT_MODE_REG] = GETDATA(curr, uint32_t);
   curr += 4; // Duplicate MI init mode flags from old implementation
   g_dev.mi.regs[MI_VERSION_REG] = GETDATA(curr, uint32_t);
   g_dev.mi.regs[MI_INTR_REG] = GETDATA(curr, uint32_t);
   g_dev.mi.regs[MI_INTR_MASK_REG] = GETDATA(curr, uint32_t);
   curr += 4; /* Padding from old implementation. */
   curr += 8; // Duplicated MI intr flags and padding from old implementation

   g_dev.pi.regs[PI_DRAM_ADDR_REG] = GETDATA(curr, uint32_t);
   g_dev.pi.regs[PI_CART_ADDR_REG] = GETDATA(curr, uint32_t);
   g_dev.pi.regs[PI_RD_LEN_REG] = GETDATA(curr, uint32_t);
   g_dev.pi.regs[PI_WR_LEN_REG] = GETDATA(curr, uint32_t);
   g_dev.pi.regs[PI_STATUS_REG] = GETDATA(curr, uint32_t);
   g_dev.pi.regs[PI_BSD_DOM1_LAT_REG] = GETDATA(curr, uint32_t);
   g_dev.pi.regs[PI_BSD_DOM1_PWD_REG] = GETDATA(curr, uint32_t);
   g_dev.pi.regs[PI_BSD_DOM1_PGS_REG] = GETDATA(curr, uint32_t);
   g_dev.pi.regs[PI_BSD_DOM1_RLS_REG] = GETDATA(curr, uint32_t);
   g_dev.pi.regs[PI_BSD_DOM2_LAT_REG] = GETDATA(curr, uint32_t);
   g_dev.pi.regs[PI_BSD_DOM2_PWD_REG] = GETDATA(curr, uint32_t);
   g_dev.pi.regs[PI_BSD_DOM2_PGS_REG] = GETDATA(curr, uint32_t);
   g_dev.pi.regs[PI_BSD_DOM2_RLS_REG] = GETDATA(curr, uint32_t);

   g_dev.sp.regs[SP_MEM_ADDR_REG] = GETDATA(curr, uint32_t);
   g_dev.sp.regs[SP_DRAM_ADDR_REG] = GETDATA(curr, uint32_t);
   g_dev.sp.regs[SP_RD_LEN_REG] = GETDATA(curr, uint32_t);
   g_dev.sp.regs[SP_WR_LEN_REG] = GETDATA(curr, uint32_t);
   curr += 4; /* Padding from old implementation. */
   g_dev.sp.regs[SP_STATUS_REG] = GETDATA(curr, uint32_t);
   curr += 16; // Duplicated SP flags and padding from old implementation
   g_dev.sp.regs[SP_DMA_FULL_REG] = GETDATA(curr, uint32_t);
   g_dev.sp.regs[SP_DMA_BUSY_REG] = GETDATA(curr, uint32_t);
   g_dev.sp.regs[SP_SEMAPHORE_REG] = GETDATA(curr, uint32_t);

   g_dev.sp.regs2[SP_PC_REG] = GETDATA(curr, uint32_t);
   g_dev.sp.regs2[SP_IBIST_REG] = GETDATA(curr, uint32_t);

   g_dev.si.regs[SI_DRAM_ADDR_REG]      = GETDATA(curr, uint32_t);
   g_dev.si.regs[SI_PIF_ADDR_RD64B_REG] = GETDATA(curr, uint32_t);
   g_dev.si.regs[SI_PIF_ADDR_WR64B_REG] = GETDATA(curr, uint32_t);
   g_dev.si.regs[SI_STATUS_REG]         = GETDATA(curr, uint32_t);

   g_dev.vi.regs[VI_STATUS_REG] = GETDATA(curr, uint32_t);
   g_dev.vi.regs[VI_ORIGIN_REG] = GETDATA(curr, uint32_t);
   g_dev.vi.regs[VI_WIDTH_REG] = GETDATA(curr, uint32_t);
   g_dev.vi.regs[VI_V_INTR_REG] = GETDATA(curr, uint32_t);
   g_dev.vi.regs[VI_CURRENT_REG] = GETDATA(curr, uint32_t);
   g_dev.vi.regs[VI_BURST_REG] = GETDATA(curr, uint32_t);
   g_dev.vi.regs[VI_V_SYNC_REG] = GETDATA(curr, uint32_t);
   g_dev.vi.regs[VI_H_SYNC_REG] = GETDATA(curr, uint32_t);
   g_dev.vi.regs[VI_LEAP_REG] = GETDATA(curr, uint32_t);
   g_dev.vi.regs[VI_H_START_REG] = GETDATA(curr, uint32_t);
   g_dev.vi.regs[VI_V_START_REG] = GETDATA(curr, uint32_t);
   g_dev.vi.regs[VI_V_BURST_REG] = GETDATA(curr, uint32_t);
   g_dev.vi.regs[VI_X_SCALE_REG] = GETDATA(curr, uint32_t);
   g_dev.vi.regs[VI_Y_SCALE_REG] = GETDATA(curr, uint32_t);
   g_dev.vi.delay = GETDATA(curr, unsigned int);

   gfx.viStatusChanged();
   gfx.viWidthChanged();

   g_dev.ri.regs[RI_MODE_REG]         = GETDATA(curr, uint32_t);
   g_dev.ri.regs[RI_CONFIG_REG]       = GETDATA(curr, uint32_t);
   g_dev.ri.regs[RI_CURRENT_LOAD_REG] = GETDATA(curr, uint32_t);
   g_dev.ri.regs[RI_SELECT_REG]       = GETDATA(curr, uint32_t);
   g_dev.ri.regs[RI_REFRESH_REG]      = GETDATA(curr, uint32_t);
   g_dev.ri.regs[RI_LATENCY_REG]      = GETDATA(curr, uint32_t);
   g_dev.ri.regs[RI_ERROR_REG]        = GETDATA(curr, uint32_t);
   g_dev.ri.regs[RI_WERROR_REG]       = GETDATA(curr, uint32_t);

   g_dev.ai.regs[AI_DRAM_ADDR_REG] = GETDATA(curr, uint32_t);
   g_dev.ai.regs[AI_LEN_REG] = GETDATA(curr, uint32_t);
   g_dev.ai.regs[AI_CONTROL_REG] = GETDATA(curr, uint32_t);
   g_dev.ai.regs[AI_STATUS_REG] = GETDATA(curr, uint32_t);
   g_dev.ai.regs[AI_DACRATE_REG] = GETDATA(curr, uint32_t);
   g_dev.ai.regs[AI_BITRATE_REG] = GETDATA(curr, uint32_t);
   g_dev.ai.fifo[1].duration     = GETDATA(curr, unsigned int);
   g_dev.ai.fifo[1].length       = GETDATA(curr, uint32_t);
   g_dev.ai.fifo[0].duration     = GETDATA(curr, unsigned int);
   g_dev.ai.fifo[0].length       = GETDATA(curr, uint32_t);
   g_dev.ai.last_read       = GETDATA(curr, uint32_t);

   /* best effort initialization of fifo addresses...
    * You might get a small sound "pop" because address might be wrong.
    * Proper initialization requires changes to savestate format
    */
   g_dev.ai.fifo[0].address = g_dev.ai.regs[AI_DRAM_ADDR_REG];
   g_dev.ai.fifo[1].address = g_dev.ai.regs[AI_DRAM_ADDR_REG];
   g_dev.ai.samples_format_changed = 1;

   g_dev.dp.dpc_regs[DPC_START_REG] = GETDATA(curr, uint32_t);
   g_dev.dp.dpc_regs[DPC_END_REG]   = GETDATA(curr, uint32_t);
   g_dev.dp.dpc_regs[DPC_CURRENT_REG] = GETDATA(curr, uint32_t);
   curr += 4; /* Padding from old implementation. */
   g_dev.dp.dpc_regs[DPC_STATUS_REG] = GETDATA(curr, uint32_t);
   curr += 12; // Duplicated DPC flags and padding from old implementation
   g_dev.dp.dpc_regs[DPC_CLOCK_REG] = GETDATA(curr, uint32_t);
   g_dev.dp.dpc_regs[DPC_BUFBUSY_REG] = GETDATA(curr, uint32_t);
   g_dev.dp.dpc_regs[DPC_PIPEBUSY_REG] = GETDATA(curr, uint32_t);
   g_dev.dp.dpc_regs[DPC_TMEM_REG] = GETDATA(curr, uint32_t);

   g_dev.dp.dps_regs[DPS_TBIST_REG] = GETDATA(curr, uint32_t);
   g_dev.dp.dps_regs[DPS_TEST_MODE_REG] = GETDATA(curr, uint32_t);
   g_dev.dp.dps_regs[DPS_BUFTEST_ADDR_REG] = GETDATA(curr, uint32_t);
   g_dev.dp.dps_regs[DPS_BUFTEST_DATA_REG] = GETDATA(curr, uint32_t);

   COPYARRAY(g_dev.rdram.dram, curr, uint32_t, RDRAM_MAX_SIZE/4);
   COPYARRAY(g_dev.sp.mem, curr, uint32_t, SP_MEM_SIZE/4);
   COPYARRAY(g_dev.pif.ram, curr, uint8_t, PIF_RAM_SIZE);

   /* extra rsp handshake state (since 1.2) - companion to the
    * parallel-rsp accuracy backport; upstream mupen64plus-core#1153
    * persists rsp_status/first_run/rsp_wait, whose fork-equivalent
    * here is the rsp_task_locked flag. */
   if (version >= 0x00010002)
      g_dev.sp.rsp_task_locked = GETDATA(curr, uint32_t);

   g_dev.cart.use_flashram = GETDATA(curr, int);
   g_dev.cart.flashram.mode = GETDATA(curr, int);
   g_dev.cart.flashram.status = GETDATA(curr, unsigned long long);

   COPYARRAY(g_dev.r4300.cp0.tlb.LUT_r, curr, unsigned int, 0x100000);
   COPYARRAY(g_dev.r4300.cp0.tlb.LUT_w, curr, unsigned int, 0x100000);

   *r4300_llbit(&g_dev.r4300) = GETDATA(curr, unsigned int);
   COPYARRAY(r4300_regs(&g_dev.r4300), curr, int64_t, 32);
   COPYARRAY(cp0_regs, curr, uint32_t, 32);
   set_fpr_pointers(&g_dev.r4300.cp1, cp0_regs[CP0_STATUS_REG]);
   *r4300_mult_lo(&g_dev.r4300) = GETDATA(curr, int64_t);
   *r4300_mult_hi(&g_dev.r4300) = GETDATA(curr, int64_t);
   COPYARRAY(r4300_cp1_regs(&g_dev.r4300.cp1), curr, int64_t, 32);

   /* 32-bit FPR mode requires data shuffling because 
    * 64-bit layout is always stored in savestate file */
   if ((cp0_regs[CP0_STATUS_REG] & UINT32_C(0x04000000)) == 0)  
      set_fpr_pointers(&g_dev.r4300.cp1, cp0_regs[CP0_STATUS_REG]);
   *r4300_cp1_fcr0(&g_dev.r4300.cp1) = GETDATA(curr, uint32_t);
   FCR31 = GETDATA(curr, uint32_t);
   *r4300_cp1_fcr31(&g_dev.r4300.cp1) = FCR31;
   update_x86_rounding_mode(&g_dev.r4300.cp1);

   for (i = 0; i < 32; i++)
   {
      g_dev.r4300.cp0.tlb.entries[i].mask       = GETDATA(curr, short);
      curr += 2;

      g_dev.r4300.cp0.tlb.entries[i].vpn2       = GETDATA(curr, unsigned int);
      g_dev.r4300.cp0.tlb.entries[i].g          = GETDATA(curr, char);
      g_dev.r4300.cp0.tlb.entries[i].asid       = GETDATA(curr, unsigned char);
      curr += 2;

      g_dev.r4300.cp0.tlb.entries[i].pfn_even   = GETDATA(curr, unsigned int);
      g_dev.r4300.cp0.tlb.entries[i].c_even     = GETDATA(curr, char);
      g_dev.r4300.cp0.tlb.entries[i].d_even     = GETDATA(curr, char);
      g_dev.r4300.cp0.tlb.entries[i].v_even     = GETDATA(curr, char);
      curr++;

      g_dev.r4300.cp0.tlb.entries[i].pfn_odd    = GETDATA(curr, unsigned int);
      g_dev.r4300.cp0.tlb.entries[i].c_odd      = GETDATA(curr, char);
      g_dev.r4300.cp0.tlb.entries[i].d_odd      = GETDATA(curr, char);
      g_dev.r4300.cp0.tlb.entries[i].v_odd      = GETDATA(curr, char);
      g_dev.r4300.cp0.tlb.entries[i].r          = GETDATA(curr, char);

      g_dev.r4300.cp0.tlb.entries[i].start_even = GETDATA(curr, unsigned int);
      g_dev.r4300.cp0.tlb.entries[i].end_even   = GETDATA(curr, unsigned int);
      g_dev.r4300.cp0.tlb.entries[i].phys_even  = GETDATA(curr, unsigned int);
      g_dev.r4300.cp0.tlb.entries[i].start_odd  = GETDATA(curr, unsigned int);
      g_dev.r4300.cp0.tlb.entries[i].end_odd    = GETDATA(curr, unsigned int);
      g_dev.r4300.cp0.tlb.entries[i].phys_odd   = GETDATA(curr, unsigned int);
   }

   savestates_load_set_pc(&g_dev.r4300, GETDATA(curr, uint32_t));

   *r4300_cp0_next_interrupt(&g_dev.r4300.cp0) = GETDATA(curr, unsigned int);
   g_dev.vi.delay  = GETDATA(curr, unsigned int);
   g_dev.vi.field    = GETDATA(curr, unsigned int);

   memcpy(queue, curr, sizeof(queue));
   to_little_endian_buffer(queue, 4, 256);
   load_eventqueue_infos(&g_dev.r4300.cp0, queue);
   curr += sizeof(queue);

   *r4300_cp0_last_addr(&g_dev.r4300.cp0) = *r4300_pc(&g_dev.r4300);
   
   if( RollbackRtcOnLoadState && version >= 0x00010001 ) {
      struct tm timestamp;
      /* af_rtc: next stores control/now/last_update_rtc (not tm) */
      COPYARRAY( ((void*)&timestamp), curr, int, 9 );
      g_dev.cart.af_rtc.control = (uint16_t)timestamp.tm_year;
   }

   /* Transfer Pak / GB cart volatile state (since 1.3); see the matching
    * block in savestates_save_m64p for the rationale and layout.  Older
    * states simply lack this block, so the GB cart keeps the state it was
    * initialised with at load_game time (the prior behaviour). */
   if (version >= 0x00010003) {
      for (i = 0; i < GAME_CONTROLLERS_COUNT; ++i)
      {
         struct transferpak* tpk = &g_dev.transferpaks[i];
         struct gb_cart* gb = tpk->gb_cart;
         int j;

         tpk->enabled             = GETDATA(curr, uint32_t);
         tpk->bank                = GETDATA(curr, uint32_t);
         tpk->access_mode         = GETDATA(curr, uint32_t);
         tpk->access_mode_changed = GETDATA(curr, uint32_t);

         /* Mirror the save side: the block is always present and fixed-size,
          * but gb_cart is NULL when no transferpak/GB cart is inserted. Read
          * and discard the bytes in that case rather than dereferencing NULL. */
         if (gb != NULL)
         {
            struct mbc3_rtc* rtc = &gb->rtc;

            gb->rom_bank = GETDATA(curr, uint32_t);
            gb->ram_bank = GETDATA(curr, uint32_t);
            (void)GETDATA(curr, uint32_t); /* has_rtc dropped (next gb_cart) */

            for (j = 0; j < MBC3_RTC_REGS_COUNT; ++j)
               rtc->regs[j] = GETDATA(curr, uint8_t);
            for (j = 0; j < MBC3_RTC_REGS_COUNT; ++j)
               rtc->latched_regs[j] = GETDATA(curr, uint8_t);
            rtc->latch     = GETDATA(curr, uint32_t);
            rtc->last_time = (time_t)GETDATA(curr, int64_t);
         }
         else
         {
            (void)GETDATA(curr, uint32_t); /* rom_bank */
            (void)GETDATA(curr, uint32_t); /* ram_bank */
            (void)GETDATA(curr, uint32_t); /* has_rtc */

            for (j = 0; j < MBC3_RTC_REGS_COUNT; ++j)
               (void)GETDATA(curr, uint8_t); /* rtc regs */
            for (j = 0; j < MBC3_RTC_REGS_COUNT; ++j)
               (void)GETDATA(curr, uint8_t); /* rtc latched_regs */
            (void)GETDATA(curr, uint32_t); /* rtc latch */
            (void)GETDATA(curr, int64_t);  /* rtc last_time */
         }
      }
   }

   /* RSP DMA FIFO state (since 1.4). The SP_DMA_BUSY/FULL register bits and
    * any pending RSP_DMA_EVT are already restored (via the SP register block
    * and the event queue respectively); this restores the two FIFO transfer
    * descriptors so a state saved with a DMA still queued resumes correctly.
    * Older states lack this block: the FIFO is left zeroed (its power-on
    * state), which is correct because pre-1.4 builds executed SP DMA
    * synchronously and never had an in-flight transfer at a save point. */
   if (version >= 0x00010004) {
      int k;
      for (k = 0; k < SP_DMA_FIFO_SIZE; ++k)
      {
         g_dev.sp.fifo[k].dir      = GETDATA(curr, uint32_t);
         g_dev.sp.fifo[k].length   = GETDATA(curr, uint32_t);
         g_dev.sp.fifo[k].memaddr  = GETDATA(curr, uint32_t);
         g_dev.sp.fifo[k].dramaddr = GETDATA(curr, uint32_t);
      }
   }


   /* deliver callback to indicate 
    * completion of state loading operation */
   StateChanged(M64CORE_STATE_LOADCOMPLETE, 1);

   return 1;
}

int savestates_save_m64p(unsigned char *data, size_t size)
{
   unsigned char outbuf[4];
   int i, queuelength;
   char queue[1024];
   uint32_t* cp0_regs = r4300_cp0_regs(&g_dev.r4300.cp0);
   unsigned char *curr = (unsigned char*)data;

   if (!curr)
   {
      /* Deliver callback to indicate failure of state saving
       * operation; without this, listeners waiting for the
       * SAVECOMPLETE notification block forever when the
       * caller hands us a NULL buffer. Matches the upstream
       * mupen64plus-core fix for issue #1031. */
      StateChanged(M64CORE_STATE_SAVECOMPLETE, 0);
      return 0;
   }

   queuelength = save_eventqueue_infos(&g_dev.r4300.cp0, queue);

   // Write the save state data to memory
   PUTARRAY(savestate_magic, curr, unsigned char, 8);

   outbuf[0] = (savestate_latest_version >> 24) & 0xff;
   outbuf[1] = (savestate_latest_version >> 16) & 0xff;
   outbuf[2] = (savestate_latest_version >>  8) & 0xff;
   outbuf[3] = (savestate_latest_version >>  0) & 0xff;
   PUTARRAY(outbuf, curr, unsigned char, 4);

   PUTARRAY(ROM_SETTINGS.MD5, curr, char, 32);

   PUTDATA(curr, uint32_t, g_dev.rdram.regs[0][RDRAM_CONFIG_REG]);
   PUTDATA(curr, uint32_t, g_dev.rdram.regs[0][RDRAM_DEVICE_ID_REG]);
   PUTDATA(curr, uint32_t, g_dev.rdram.regs[0][RDRAM_DELAY_REG]);
   PUTDATA(curr, uint32_t, g_dev.rdram.regs[0][RDRAM_MODE_REG]);
   PUTDATA(curr, uint32_t, g_dev.rdram.regs[0][RDRAM_REF_INTERVAL_REG]);
   PUTDATA(curr, uint32_t, g_dev.rdram.regs[0][RDRAM_REF_ROW_REG]);
   PUTDATA(curr, uint32_t, g_dev.rdram.regs[0][RDRAM_RAS_INTERVAL_REG]);
   PUTDATA(curr, uint32_t, g_dev.rdram.regs[0][RDRAM_MIN_INTERVAL_REG]);
   PUTDATA(curr, uint32_t, g_dev.rdram.regs[0][RDRAM_ADDR_SELECT_REG]);
   PUTDATA(curr, uint32_t, g_dev.rdram.regs[0][RDRAM_DEVICE_MANUF_REG]);

   PUTDATA(curr, uint32_t, 0);
   PUTDATA(curr, uint32_t, g_dev.mi.regs[MI_INIT_MODE_REG]);
   PUTDATA(curr, uint8_t, g_dev.mi.regs[MI_INIT_MODE_REG] & 0x7F);
   PUTDATA(curr, uint8_t, (g_dev.mi.regs[MI_INIT_MODE_REG] & 0x80) != 0);
   PUTDATA(curr, uint8_t, (g_dev.mi.regs[MI_INIT_MODE_REG] & 0x100) != 0);
   PUTDATA(curr, uint8_t, (g_dev.mi.regs[MI_INIT_MODE_REG] & 0x200) != 0);
   PUTDATA(curr, uint32_t, g_dev.mi.regs[MI_VERSION_REG]);
   PUTDATA(curr, uint32_t, g_dev.mi.regs[MI_INTR_REG]);
   PUTDATA(curr, uint32_t, g_dev.mi.regs[MI_INTR_MASK_REG]);
   PUTDATA(curr, uint32_t, 0); /* Padding from old implementation */
   PUTDATA(curr, uint8_t, (g_dev.mi.regs[MI_INTR_MASK_REG] & 0x1) != 0);
   PUTDATA(curr, uint8_t, (g_dev.mi.regs[MI_INTR_MASK_REG] & 0x2) != 0);
   PUTDATA(curr, uint8_t, (g_dev.mi.regs[MI_INTR_MASK_REG] & 0x4) != 0);
   PUTDATA(curr, uint8_t, (g_dev.mi.regs[MI_INTR_MASK_REG] & 0x8) != 0);
   PUTDATA(curr, uint8_t, (g_dev.mi.regs[MI_INTR_MASK_REG] & 0x10) != 0);
   PUTDATA(curr, uint8_t, (g_dev.mi.regs[MI_INTR_MASK_REG] & 0x20) != 0);
   PUTDATA(curr, uint16_t, 0); // Padding from old implementation

   PUTDATA(curr, uint32_t, g_dev.pi.regs[PI_DRAM_ADDR_REG]);
   PUTDATA(curr, uint32_t, g_dev.pi.regs[PI_CART_ADDR_REG]);
   PUTDATA(curr, uint32_t, g_dev.pi.regs[PI_RD_LEN_REG]);
   PUTDATA(curr, uint32_t, g_dev.pi.regs[PI_WR_LEN_REG]);
   PUTDATA(curr, uint32_t, g_dev.pi.regs[PI_STATUS_REG]);
   PUTDATA(curr, uint32_t, g_dev.pi.regs[PI_BSD_DOM1_LAT_REG]);
   PUTDATA(curr, uint32_t, g_dev.pi.regs[PI_BSD_DOM1_PWD_REG]);
   PUTDATA(curr, uint32_t, g_dev.pi.regs[PI_BSD_DOM1_PGS_REG]);
   PUTDATA(curr, uint32_t, g_dev.pi.regs[PI_BSD_DOM1_RLS_REG]);
   PUTDATA(curr, uint32_t, g_dev.pi.regs[PI_BSD_DOM1_LAT_REG]);
   PUTDATA(curr, uint32_t, g_dev.pi.regs[PI_BSD_DOM1_PWD_REG]);
   PUTDATA(curr, uint32_t, g_dev.pi.regs[PI_BSD_DOM1_PGS_REG]);
   PUTDATA(curr, uint32_t, g_dev.pi.regs[PI_BSD_DOM1_RLS_REG]);

   PUTDATA(curr, uint32_t, g_dev.sp.regs[SP_MEM_ADDR_REG]);
   PUTDATA(curr, uint32_t, g_dev.sp.regs[SP_DRAM_ADDR_REG]);
   PUTDATA(curr, uint32_t, g_dev.sp.regs[SP_RD_LEN_REG]);
   PUTDATA(curr, uint32_t, g_dev.sp.regs[SP_WR_LEN_REG]);
   PUTDATA(curr, uint32_t, 0); /* Padding from old implementation */
   PUTDATA(curr, uint32_t, g_dev.sp.regs[SP_STATUS_REG]);
   PUTDATA(curr, uint8_t, (g_dev.sp.regs[SP_STATUS_REG] & 0x1) != 0);
   PUTDATA(curr, uint8_t, (g_dev.sp.regs[SP_STATUS_REG] & 0x2) != 0);
   PUTDATA(curr, uint8_t, (g_dev.sp.regs[SP_STATUS_REG] & 0x4) != 0);
   PUTDATA(curr, uint8_t, (g_dev.sp.regs[SP_STATUS_REG] & 0x8) != 0);
   PUTDATA(curr, uint8_t, (g_dev.sp.regs[SP_STATUS_REG] & 0x10) != 0);
   PUTDATA(curr, uint8_t, (g_dev.sp.regs[SP_STATUS_REG] & 0x20) != 0);
   PUTDATA(curr, uint8_t, (g_dev.sp.regs[SP_STATUS_REG] & 0x40) != 0);
   PUTDATA(curr, uint8_t, (g_dev.sp.regs[SP_STATUS_REG] & 0x80) != 0);
   PUTDATA(curr, uint8_t, (g_dev.sp.regs[SP_STATUS_REG] & 0x100) != 0);
   PUTDATA(curr, uint8_t, (g_dev.sp.regs[SP_STATUS_REG] & 0x200) != 0);
   PUTDATA(curr, uint8_t, (g_dev.sp.regs[SP_STATUS_REG] & 0x400) != 0);
   PUTDATA(curr, uint8_t, (g_dev.sp.regs[SP_STATUS_REG] & 0x800) != 0);
   PUTDATA(curr, uint8_t, (g_dev.sp.regs[SP_STATUS_REG] & 0x1000) != 0);
   PUTDATA(curr, uint8_t, (g_dev.sp.regs[SP_STATUS_REG] & 0x2000) != 0);
   PUTDATA(curr, uint8_t, (g_dev.sp.regs[SP_STATUS_REG] & 0x4000) != 0);
   PUTDATA(curr, uint8_t, 0);
   PUTDATA(curr, uint32_t, g_dev.sp.regs[SP_DMA_FULL_REG]);
   PUTDATA(curr, uint32_t, g_dev.sp.regs[SP_DMA_BUSY_REG]);
   PUTDATA(curr, uint32_t, g_dev.sp.regs[SP_SEMAPHORE_REG]);

   PUTDATA(curr, uint32_t, g_dev.sp.regs2[SP_PC_REG]);
   PUTDATA(curr, uint32_t, g_dev.sp.regs2[SP_IBIST_REG]);

   PUTDATA(curr, uint32_t, g_dev.si.regs[SI_DRAM_ADDR_REG]);
   PUTDATA(curr, uint32_t, g_dev.si.regs[SI_PIF_ADDR_RD64B_REG]);
   PUTDATA(curr, uint32_t, g_dev.si.regs[SI_PIF_ADDR_WR64B_REG]);
   PUTDATA(curr, uint32_t, g_dev.si.regs[SI_STATUS_REG]);

   PUTDATA(curr, uint32_t, g_dev.vi.regs[VI_STATUS_REG]);
   PUTDATA(curr, uint32_t, g_dev.vi.regs[VI_ORIGIN_REG]);
   PUTDATA(curr, uint32_t, g_dev.vi.regs[VI_WIDTH_REG]);
   PUTDATA(curr, uint32_t, g_dev.vi.regs[VI_V_INTR_REG]);
   PUTDATA(curr, uint32_t, g_dev.vi.regs[VI_CURRENT_REG]);
   PUTDATA(curr, uint32_t, g_dev.vi.regs[VI_BURST_REG]);
   PUTDATA(curr, uint32_t, g_dev.vi.regs[VI_V_SYNC_REG]);
   PUTDATA(curr, uint32_t, g_dev.vi.regs[VI_H_SYNC_REG]);
   PUTDATA(curr, uint32_t, g_dev.vi.regs[VI_LEAP_REG]);
   PUTDATA(curr, uint32_t, g_dev.vi.regs[VI_H_START_REG]);
   PUTDATA(curr, uint32_t, g_dev.vi.regs[VI_V_START_REG]);
   PUTDATA(curr, uint32_t, g_dev.vi.regs[VI_V_BURST_REG]);
   PUTDATA(curr, uint32_t, g_dev.vi.regs[VI_X_SCALE_REG]);
   PUTDATA(curr, uint32_t, g_dev.vi.regs[VI_Y_SCALE_REG]);
   PUTDATA(curr, unsigned int, g_dev.vi.delay);

   PUTDATA(curr, uint32_t, g_dev.ri.regs[RI_MODE_REG]);
   PUTDATA(curr, uint32_t, g_dev.ri.regs[RI_CONFIG_REG]);
   PUTDATA(curr, uint32_t, g_dev.ri.regs[RI_CURRENT_LOAD_REG]);
   PUTDATA(curr, uint32_t, g_dev.ri.regs[RI_SELECT_REG]);
   PUTDATA(curr, uint32_t, g_dev.ri.regs[RI_REFRESH_REG]);
   PUTDATA(curr, uint32_t, g_dev.ri.regs[RI_LATENCY_REG]);
   PUTDATA(curr, uint32_t, g_dev.ri.regs[RI_ERROR_REG]);
   PUTDATA(curr, uint32_t, g_dev.ri.regs[RI_WERROR_REG]);

   PUTDATA(curr, uint32_t, g_dev.ai.regs[AI_DRAM_ADDR_REG]);
   PUTDATA(curr, uint32_t, g_dev.ai.regs[AI_LEN_REG]);
   PUTDATA(curr, uint32_t, g_dev.ai.regs[AI_CONTROL_REG]);
   PUTDATA(curr, uint32_t, g_dev.ai.regs[AI_STATUS_REG]);
   PUTDATA(curr, uint32_t, g_dev.ai.regs[AI_DACRATE_REG]);
   PUTDATA(curr, uint32_t, g_dev.ai.regs[AI_BITRATE_REG]);
   PUTDATA(curr, unsigned int, g_dev.ai.fifo[1].duration);
   PUTDATA(curr, uint32_t, g_dev.ai.fifo[1].length);
   PUTDATA(curr, unsigned int, g_dev.ai.fifo[0].duration);
   PUTDATA(curr, uint32_t, g_dev.ai.fifo[0].length);
   PUTDATA(curr, uint32_t, g_dev.ai.last_read);

   PUTDATA(curr, uint32_t, g_dev.dp.dpc_regs[DPC_START_REG]);
   PUTDATA(curr, uint32_t, g_dev.dp.dpc_regs[DPC_END_REG]);
   PUTDATA(curr, uint32_t, g_dev.dp.dpc_regs[DPC_CURRENT_REG]);
   PUTDATA(curr, uint32_t, 0); /* Padding from oold implementation */
   PUTDATA(curr, uint32_t, g_dev.dp.dpc_regs[DPC_STATUS_REG]);
   PUTDATA(curr, uint8_t, (g_dev.dp.dpc_regs[DPC_STATUS_REG] & 0x1) != 0);
   PUTDATA(curr, uint8_t, (g_dev.dp.dpc_regs[DPC_STATUS_REG] & 0x2) != 0);
   PUTDATA(curr, uint8_t, (g_dev.dp.dpc_regs[DPC_STATUS_REG] & 0x4) != 0);
   PUTDATA(curr, uint8_t, (g_dev.dp.dpc_regs[DPC_STATUS_REG] & 0x8) != 0);
   PUTDATA(curr, uint8_t, (g_dev.dp.dpc_regs[DPC_STATUS_REG] & 0x10) != 0);
   PUTDATA(curr, uint8_t, (g_dev.dp.dpc_regs[DPC_STATUS_REG] & 0x20) != 0);
   PUTDATA(curr, uint8_t, (g_dev.dp.dpc_regs[DPC_STATUS_REG] & 0x40) != 0);
   PUTDATA(curr, uint8_t, (g_dev.dp.dpc_regs[DPC_STATUS_REG] & 0x80) != 0);
   PUTDATA(curr, uint8_t, (g_dev.dp.dpc_regs[DPC_STATUS_REG] & 0x100) != 0);
   PUTDATA(curr, uint8_t, (g_dev.dp.dpc_regs[DPC_STATUS_REG] & 0x200) != 0);
   PUTDATA(curr, uint8_t, (g_dev.dp.dpc_regs[DPC_STATUS_REG] & 0x400) != 0);
   PUTDATA(curr, uint8_t, 0);
   PUTDATA(curr, uint32_t, g_dev.dp.dpc_regs[DPC_CLOCK_REG]);
   PUTDATA(curr, uint32_t, g_dev.dp.dpc_regs[DPC_BUFBUSY_REG]);
   PUTDATA(curr, uint32_t, g_dev.dp.dpc_regs[DPC_PIPEBUSY_REG]);
   PUTDATA(curr, uint32_t, g_dev.dp.dpc_regs[DPC_TMEM_REG]);

   PUTDATA(curr, uint32_t, g_dev.dp.dps_regs[DPS_TBIST_REG]);
   PUTDATA(curr, uint32_t, g_dev.dp.dps_regs[DPS_TEST_MODE_REG]);
   PUTDATA(curr, uint32_t, g_dev.dp.dps_regs[DPS_BUFTEST_ADDR_REG]);
   PUTDATA(curr, uint32_t, g_dev.dp.dps_regs[DPS_BUFTEST_DATA_REG]);

   PUTARRAY(g_dev.rdram.dram, curr, uint32_t, RDRAM_MAX_SIZE/4);
   PUTARRAY(g_dev.sp.mem, curr, uint32_t, SP_MEM_SIZE/4);
   PUTARRAY(g_dev.pif.ram, curr, uint8_t, PIF_RAM_SIZE);

   /* extra rsp handshake state (since 1.2) - see savestates_load_m64p */
   PUTDATA(curr, uint32_t, g_dev.sp.rsp_task_locked);

   PUTDATA(curr, int, g_dev.cart.use_flashram);
   PUTDATA(curr, int, g_dev.cart.flashram.mode);
   PUTDATA(curr, unsigned long long, g_dev.cart.flashram.status);

   PUTARRAY(g_dev.r4300.cp0.tlb.LUT_r, curr, unsigned int, 0x100000);
   PUTARRAY(g_dev.r4300.cp0.tlb.LUT_w, curr, unsigned int, 0x100000);

   PUTDATA(curr, unsigned int, *r4300_llbit(&g_dev.r4300));
   PUTARRAY(r4300_regs(&g_dev.r4300), curr, int64_t, 32);
   PUTARRAY(cp0_regs, curr, uint32_t, 32);
   PUTDATA(curr, int64_t, *r4300_mult_lo(&g_dev.r4300));
   PUTDATA(curr, int64_t, *r4300_mult_hi(&g_dev.r4300));

   if ((cp0_regs[CP0_STATUS_REG] & UINT32_C(0x04000000)) == 0) // FR bit == 0 means 32-bit (MIPS I) FGR mode
      set_fpr_pointers(&g_dev.r4300.cp1, cp0_regs[CP0_STATUS_REG]);
   PUTARRAY(r4300_cp1_regs(&g_dev.r4300.cp1), curr, int64_t, 32);
   if ((cp0_regs[CP0_STATUS_REG] & UINT32_C(0x04000000)) == 0)
      set_fpr_pointers(&g_dev.r4300.cp1, cp0_regs[CP0_STATUS_REG]);  // put it back in 32-bit mode

   PUTDATA(curr, uint32_t, *r4300_cp1_fcr0(&g_dev.r4300.cp1));
   PUTDATA(curr, uint32_t, *r4300_cp1_fcr31(&g_dev.r4300.cp1));

   for (i = 0; i < 32; i++)
   {
      PUTDATA(curr, short, g_dev.r4300.cp0.tlb.entries[i].mask);
      PUTDATA(curr, short, 0);
      PUTDATA(curr, unsigned int, g_dev.r4300.cp0.tlb.entries[i].vpn2);
      PUTDATA(curr, char, g_dev.r4300.cp0.tlb.entries[i].g);
      PUTDATA(curr, unsigned char, g_dev.r4300.cp0.tlb.entries[i].asid);
      PUTDATA(curr, short, 0);
      PUTDATA(curr, unsigned int, g_dev.r4300.cp0.tlb.entries[i].pfn_even);
      PUTDATA(curr, char, g_dev.r4300.cp0.tlb.entries[i].c_even);
      PUTDATA(curr, char, g_dev.r4300.cp0.tlb.entries[i].d_even);
      PUTDATA(curr, char, g_dev.r4300.cp0.tlb.entries[i].v_even);
      PUTDATA(curr, char, 0);
      PUTDATA(curr, unsigned int, g_dev.r4300.cp0.tlb.entries[i].pfn_odd);
      PUTDATA(curr, char, g_dev.r4300.cp0.tlb.entries[i].c_odd);
      PUTDATA(curr, char, g_dev.r4300.cp0.tlb.entries[i].d_odd);
      PUTDATA(curr, char, g_dev.r4300.cp0.tlb.entries[i].v_odd);
      PUTDATA(curr, char, g_dev.r4300.cp0.tlb.entries[i].r);

      PUTDATA(curr, unsigned int, g_dev.r4300.cp0.tlb.entries[i].start_even);
      PUTDATA(curr, unsigned int, g_dev.r4300.cp0.tlb.entries[i].end_even);
      PUTDATA(curr, unsigned int, g_dev.r4300.cp0.tlb.entries[i].phys_even);
      PUTDATA(curr, unsigned int, g_dev.r4300.cp0.tlb.entries[i].start_odd);
      PUTDATA(curr, unsigned int, g_dev.r4300.cp0.tlb.entries[i].end_odd);
      PUTDATA(curr, unsigned int, g_dev.r4300.cp0.tlb.entries[i].phys_odd);
   }
   PUTDATA(curr, uint32_t, *r4300_pc(&g_dev.r4300));

   PUTDATA(curr, unsigned int, *r4300_cp0_next_interrupt(&g_dev.r4300.cp0));
   PUTDATA(curr, unsigned int, g_dev.vi.delay);
   PUTDATA(curr, unsigned int, g_dev.vi.field);

   to_little_endian_buffer(queue, 4, queuelength/4);
   PUTARRAY(queue, curr, char, queuelength);
   
   if( queuelength < sizeof(queue) ) {
      memset( curr, 0, sizeof(queue) - queuelength );
      curr += sizeof(queue) - queuelength;
   }

   struct tm af_rtc_tm; memset(&af_rtc_tm, 0, sizeof(af_rtc_tm));
   af_rtc_tm.tm_year = (int)g_dev.cart.af_rtc.control;
   PUTARRAY( &af_rtc_tm, curr, int, 9 );
   to_little_endian_buffer( (curr - 36), 4, 9 );

   /* Transfer Pak / GB cart volatile state (since 1.3).
    *
    * This persists the runtime state needed to keep a mid-play savestate
    * coherent for Transfer Pak titles: the pak access state, the GB cart
    * bank registers, and the MBC3 RTC (regs / latched regs / latch / last
    * update time).  GB cart *RAM* (battery save data) is intentionally NOT
    * stored here -- that belongs in the save-file path, not the savestate.
    *
    * The block is a fixed size (independent of whether a cart is inserted)
    * so the layout stays trivially seekable across versions. */
   for (i = 0; i < GAME_CONTROLLERS_COUNT; ++i)
   {
      struct transferpak* tpk = &g_dev.transferpaks[i];
      struct gb_cart* gb = tpk->gb_cart;
      int j;

      PUTDATA(curr, uint32_t, tpk->enabled);
      PUTDATA(curr, uint32_t, tpk->bank);
      PUTDATA(curr, uint32_t, tpk->access_mode);
      PUTDATA(curr, uint32_t, tpk->access_mode_changed);

      /* gb_cart is NULL unless a transferpak with a GB cartridge is inserted
       * (the common case is none). Write a zeroed, fixed-size block so the
       * layout stays seekable regardless; the matching load skips it the same
       * way. Dereferencing gb here crashed savestates for every game without a
       * transferpak. */
      if (gb != NULL)
      {
         struct mbc3_rtc* rtc = &gb->rtc;

         PUTDATA(curr, uint32_t, gb->rom_bank);
         PUTDATA(curr, uint32_t, gb->ram_bank);
         PUTDATA(curr, uint32_t, 0); /* has_rtc dropped (next gb_cart) */

         for (j = 0; j < MBC3_RTC_REGS_COUNT; ++j)
            PUTDATA(curr, uint8_t, rtc->regs[j]);
         for (j = 0; j < MBC3_RTC_REGS_COUNT; ++j)
            PUTDATA(curr, uint8_t, rtc->latched_regs[j]);
         PUTDATA(curr, uint32_t, rtc->latch);
         PUTDATA(curr, int64_t, (int64_t)rtc->last_time);
      }
      else
      {
         PUTDATA(curr, uint32_t, 0); /* rom_bank */
         PUTDATA(curr, uint32_t, 0); /* ram_bank */
         PUTDATA(curr, uint32_t, 0); /* has_rtc */

         for (j = 0; j < MBC3_RTC_REGS_COUNT; ++j)
            PUTDATA(curr, uint8_t, 0); /* rtc regs */
         for (j = 0; j < MBC3_RTC_REGS_COUNT; ++j)
            PUTDATA(curr, uint8_t, 0); /* rtc latched_regs */
         PUTDATA(curr, uint32_t, 0); /* rtc latch */
         PUTDATA(curr, int64_t, (int64_t)0); /* rtc last_time */
      }
   }

   /* RSP DMA FIFO state (since 1.4); see the matching load block. */
   {
      int k;
      for (k = 0; k < SP_DMA_FIFO_SIZE; ++k)
      {
         PUTDATA(curr, uint32_t, g_dev.sp.fifo[k].dir);
         PUTDATA(curr, uint32_t, g_dev.sp.fifo[k].length);
         PUTDATA(curr, uint32_t, g_dev.sp.fifo[k].memaddr);
         PUTDATA(curr, uint32_t, g_dev.sp.fifo[k].dramaddr);
      }
   }

   /* Deliver callback to indicate completion 
    * of state saving operation */
   StateChanged(M64CORE_STATE_SAVECOMPLETE, 1);

   return 1;
}

/* ---- libretro-fork savestate job/slot stubs -----------------------------
 * The RetroArch frontend manages save slots itself and drives save/load
 * through the buffer-based savestates_save_m64p/load_m64p above. next's
 * upstream job/slot scheduler (SDL-mutex based) is not used here; these
 * stubs satisfy frontend.c / CoreDoCommand without pulling in SDL. */
static unsigned int l_savestate_slot = 0;

savestates_job savestates_get_job(void) { return savestates_job_nothing; }
void savestates_set_job(savestates_job j, savestates_type t, const char *fn)
{ (void)j; (void)t; (void)fn; }
void savestates_select_slot(unsigned int s) { l_savestate_slot = s; }
unsigned int savestates_get_slot(void) { return l_savestate_slot; }
void savestates_inc_slot(void) { l_savestate_slot = (l_savestate_slot + 1) % 10; }
void savestates_set_autoinc_slot(int b) { (void)b; }
void savestates_init(void) { }
void savestates_deinit(void) { }
int  savestates_save(void) { return 0; }
int  savestates_load(void) { return 0; }
