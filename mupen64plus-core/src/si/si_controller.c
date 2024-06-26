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

#include "../api/m64p_types.h"
#include "../api/callbacks.h"
#include "../main/main.h"
#include "../main/rom.h"
#include "../memory/memory.h"
#include "../r4300/r4300_core.h"
#include "../ri/ri_controller.h"
#include "../ri/safe_rdram.h"

#include <string.h>

enum
{
   SI_STATUS_DMA_BUSY  = 0x0001,
   SI_STATUS_RD_BUSY   = 0x0002,
   SI_STATUS_DMA_ERROR = 0x0008,
   SI_STATUS_INTERRUPT = 0x1000,

};

static void dma_si_write(struct si_controller* si)
{
   int i;

   if (si->regs[SI_PIF_ADDR_WR64B_REG] != 0x1FC007C0)
   {
      DebugMessage(M64MSG_ERROR, "dma_si_write(): unknown SI use");
      return;
   }

   for (i = 0; i < PIF_RAM_SIZE; i += 4)
   {
      const uint32_t dram_i = si->regs[SI_DRAM_ADDR_REG]+i;
      const uint32_t value = rdram_safe_read_word(si->ri->rdram.dram, dram_i);
      *((uint32_t*)(&si->pif.ram[i])) = sl(value);
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
      signal_rcp_interrupt(si->r4300, MI_INTR_SI);
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

   update_pif_read(si);

   for (i = 0; i < PIF_RAM_SIZE; i += 4)
   {
      const uint32_t dram_i = si->regs[SI_DRAM_ADDR_REG]+i;
      const uint32_t value = *(uint32_t*)(&si->pif.ram[i]);
      rdram_safe_write_word(si->ri->rdram.dram, dram_i, sl(value));
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
      signal_rcp_interrupt(si->r4300, MI_INTR_SI);
   }
}

void init_si(struct si_controller* si,
      void* eeprom_user_data,
      void (*eeprom_save)(void*),
      uint8_t* eeprom_data,
      size_t eeprom_size,
      uint16_t eeprom_id,
      void* af_rtc_user_data,
      const struct tm* (*af_rtc_get_time)(void*),
      const uint8_t* ipl3,
      struct r4300_core* r4300,
      struct ri_controller *ri)
{
   si->r4300 = r4300;
   si->ri    = ri;

   init_pif(&si->pif,
         eeprom_user_data, eeprom_save, eeprom_data, eeprom_size, eeprom_id,
         af_rtc_user_data, af_rtc_get_time, ipl3 
         );
}

void poweron_si(struct si_controller* si)
{
    memset(si->regs, 0, SI_REGS_COUNT*sizeof(uint32_t));

    poweron_pif(&si->pif);
}


int read_si_regs(void* opaque, uint32_t address, uint32_t* value)
{
    struct si_controller* si = (struct si_controller*)opaque;
    uint32_t reg             = SI_REG(address);

    *value                   = si->regs[reg];

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
          clear_rcp_interrupt(si->r4300, MI_INTR_SI);
          break;
    }

    return 0;
}

void si_end_of_dma_event(struct si_controller* si)
{
   main_check_inputs();

   si->pif.ram[0x3f] = 0x0;

   /* trigger SI interrupt */
   si->regs[SI_STATUS_REG] &= ~SI_STATUS_DMA_BUSY;
   si->regs[SI_STATUS_REG] |= SI_STATUS_INTERRUPT;
   raise_rcp_interrupt(si->r4300, MI_INTR_SI);
}
