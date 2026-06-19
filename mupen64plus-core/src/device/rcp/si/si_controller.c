/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - si_controller.c                                         *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
 *   Copyright (C) 2014 Bobby Smiles                                       *
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

#include "si_controller.h"

#include "../../../api/m64p_types.h"
#include "../../../api/callbacks.h"
#include "../../../main/main.h"
#include "../../../main/rom.h"
#include "../../memory/m64p_memory.h"
#include "../../r4300/r4300_core.h"
#include "../../r4300/interrupt.h"
#include "../ri/ri_controller.h"
#include "../../rdram/safe_rdram.h"

#include <string.h>

enum
{
   SI_STATUS_DMA_BUSY  = 0x0001,
   SI_STATUS_RD_BUSY   = 0x0002,
   SI_STATUS_DMA_ERROR = 0x0008,
   SI_STATUS_INTERRUPT = 0x1000,

};

/* The SI is a serial bus: hardware completes one PIF/EEPROM transaction
 * before the next can begin -- two SI DMAs never overlap.  With delayed SI
 * completion (g_delay_si), the emulated guest can issue a new SI DMA while a
 * previous one's completion interrupt is still pending in the event queue.
 * That window is where the SM64 60fps hacks freeze on an in-game save: the
 * hack's per-frame controller poll issues a read DMA between the EEPROM write
 * DMA and its SI interrupt, and the save thread's osRecvMesg wakeup is lost.
 *
 * Resolve it generically (no per-ROM CRC list): if a previous SI DMA is still
 * in flight when a new one is requested, deliver the pending completion now,
 * mirroring the hardware serialization, then let the new DMA proceed.  This
 * closes the race for every title -- including ones whose ROM bytes have been
 * patched at runtime (widescreen / cheat hacks), where a CRC match would not
 * fire. */
static void si_flush_pending_dma(struct si_controller* si)
{
   if ((si->regs[SI_STATUS_REG] & SI_STATUS_DMA_BUSY) == 0)
      return;
   if (get_event(SI_INT) == NULL)
      return;

   /* Cancel the scheduled completion and deliver it now, using the same
    * immediate-completion path as the g_delay_si == 0 case below (a plain
    * signal_rcp_interrupt, which schedules the interrupt check) rather than
    * the event-dispatcher's si_end_of_dma_event(): we are running inside the
    * SI register write that kicked the new DMA, not inside the interrupt
    * event loop. */
   remove_event(SI_INT);
   si->pif->ram[0x3f] = 0x0;
   si->regs[SI_STATUS_REG] &= ~SI_STATUS_DMA_BUSY;
   si->regs[SI_STATUS_REG] |= SI_STATUS_INTERRUPT;
   signal_rcp_interrupt(si->mi, MI_INTR_SI);
}

static void dma_si_write(struct si_controller* si)
{
   int i;

   if (si->regs[SI_PIF_ADDR_WR64B_REG] != 0x1FC007C0)
   {
      DebugMessage(M64MSG_ERROR, "dma_si_write(): unknown SI use");
      return;
   }

   /* serialize with any SI DMA still in flight (hardware cannot overlap) */
   si_flush_pending_dma(si);

   for (i = 0; i < PIF_RAM_SIZE; i += 4)
   {
      const uint32_t dram_i = (si->regs[SI_DRAM_ADDR_REG] & UINT32_C(0xFFFFFFFC))+i;
      const uint32_t value = rdram_safe_read_word(si->ri->rdram->dram, dram_i);
      *((uint32_t*)(&si->pif->ram[i])) = sl(value);
   }

   update_pif_write(si);
   cp0_update_count();

   if (g_delay_si)
   {
      si->regs[SI_STATUS_REG] |= SI_STATUS_DMA_BUSY;
      add_interrupt_event(SI_INT, ROM_SETTINGS.sidmaduration);
   }
   else
   {
      si->regs[SI_STATUS_REG] |= SI_STATUS_INTERRUPT;
      signal_rcp_interrupt(si->mi, MI_INTR_SI);
   }
}

static void dma_si_read(struct si_controller* si)
{
   int i;

   if (si->regs[SI_PIF_ADDR_RD64B_REG] != 0x1FC007C0)
   {
      DebugMessage(M64MSG_ERROR, "dma_si_read(): unknown SI use");
      return;
   }

   /* serialize with any SI DMA still in flight (hardware cannot overlap) */
   si_flush_pending_dma(si);

   update_pif_read(si);

   for (i = 0; i < PIF_RAM_SIZE; i += 4)
   {
      const uint32_t dram_i = (si->regs[SI_DRAM_ADDR_REG] & UINT32_C(0xFFFFFFFC))+i;
      const uint32_t value = *(uint32_t*)(&si->pif->ram[i]);
      rdram_safe_write_word(si->ri->rdram->dram, dram_i, sl(value));
   }
   cp0_update_count();

   if (g_delay_si)
   {
      si->regs[SI_STATUS_REG] |= SI_STATUS_DMA_BUSY;
      add_interrupt_event(SI_INT, ROM_SETTINGS.sidmaduration);
   }
   else
   {
      si->regs[SI_STATUS_REG] |= SI_STATUS_INTERRUPT;
      signal_rcp_interrupt(si->mi, MI_INTR_SI);
   }
}

void init_si(struct si_controller* si,
      void* eeprom_user_data,
      void (*eeprom_save)(void*),
      uint8_t* eeprom_data,
      size_t eeprom_size,
      uint32_t eeprom_id,
      void* af_rtc_user_data,
      const struct tm* (*af_rtc_get_time)(void*),
      const uint8_t* ipl3,
      struct mi_controller* mi,
      struct ri_controller *ri)
{
   si->mi = mi;
   si->ri    = ri;

   init_pif(si->pif,
         eeprom_user_data, eeprom_save, eeprom_data, eeprom_size, eeprom_id,
         af_rtc_user_data, af_rtc_get_time, ipl3 
         );
}

void poweron_si(struct si_controller* si)
{
    memset(si->regs, 0, SI_REGS_COUNT*sizeof(uint32_t));

    poweron_pif(si->pif);
}


int read_si_regs(void* opaque, uint32_t address, uint32_t* value)
{
    struct si_controller* si = (struct si_controller*)opaque;
    uint32_t reg             = SI_REG(address);

    *value                   = (reg < SI_REGS_COUNT) ? si->regs[reg] : 0u;

    return 0;
}

int write_si_regs(void* opaque, uint32_t address, uint32_t value, uint32_t mask)
{
    struct si_controller* si = (struct si_controller*)opaque;
    uint32_t reg             = SI_REG(address);

    switch (reg)
    {
       case SI_DRAM_ADDR_REG:
          si->regs[SI_DRAM_ADDR_REG] = MASKED_WRITE(&si->regs[SI_DRAM_ADDR_REG], value, mask);
          break;

       case SI_PIF_ADDR_RD64B_REG:
          si->regs[SI_PIF_ADDR_RD64B_REG] = MASKED_WRITE(&si->regs[SI_PIF_ADDR_RD64B_REG], value, mask);
          dma_si_read(si);
          break;

       case SI_PIF_ADDR_WR64B_REG:
          si->regs[SI_PIF_ADDR_WR64B_REG] = MASKED_WRITE(&si->regs[SI_PIF_ADDR_WR64B_REG], value, mask);
          dma_si_write(si);
          break;

       case SI_STATUS_REG:
          si->regs[SI_STATUS_REG] &= ~SI_STATUS_INTERRUPT;
          clear_rcp_interrupt(si->mi, MI_INTR_SI);
          break;
    }

    return 0;
}

void si_end_of_dma_event(struct si_controller* si)
{
   /* Do NOT poll input here. Input is polled exactly once per frame from
    * main_on_vi_event() (the VI frame boundary), which is the single
    * poll_cb() call libretro expects per retro_run. The SI DMA can
    * complete one or more times per frame (every controller read, plus
    * controller-pak/rumble writes), so polling here produced multiple,
    * input-count-dependent poll_cb() calls per retro_run -- breaking the
    * libretro polling contract and making the input the game reads depend
    * on sub-frame SI timing rather than being a clean per-frame value
    * (a determinism hazard for run-ahead / netplay / movie replay). The
    * controller data is filled by update_pif_read() in dma_si_read()
    * BEFORE this event fires anyway, so this poll never freshened the
    * read it preceded -- it was both wrong and useless. */
   si->pif->ram[0x3f] = 0x0;

   /* trigger SI interrupt */
   si->regs[SI_STATUS_REG] &= ~SI_STATUS_DMA_BUSY;
   si->regs[SI_STATUS_REG] |= SI_STATUS_INTERRUPT;
   raise_rcp_interrupt(si->mi, MI_INTR_SI);
}
