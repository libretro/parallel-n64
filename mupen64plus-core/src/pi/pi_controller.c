/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - pi_controller.c                                         *
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

#include "pi_controller.h"

#define M64P_CORE_PROTOTYPES 1
#include "../api/callbacks.h"
#include "../api/m64p_types.h"
#include "../main/main.h"
#include "../memory/memory.h"
#include "../r4300/cp0.h"
#include "../r4300/cp0_private.h"
#include "../r4300/r4300_core.h"
#include "../ri/rdram_detection_hack.h"
#include "../ri/ri_controller.h"
#include "../dd/dd_controller.h"

#include <string.h>

/* Copies data from the PI into RDRAM */
static void dma_pi_read(struct pi_controller *pi)
{
   if (pi->regs[PI_CART_ADDR_REG] >= 0x08000000
         && pi->regs[PI_CART_ADDR_REG] < 0x08010000)
   {
      if (pi->use_flashram != 1)
      {
         dma_write_sram(pi);
         pi->use_flashram = -1;
      }
      else
      {
         dma_write_flashram(pi);
      }
   }
   else
   {
      DebugMessage(M64MSG_WARNING, "Unknown dma read at 0x%08X in dma_pi_read()", pi->regs[PI_CART_ADDR_REG]);
   }

   pi->regs[PI_STATUS_REG] |= 1;
   cp0_update_count();
   add_interupt_event(PI_INT, 0x1000/* pi->regs[PI_RD_LEN_REG] */);
}

/* Copies data from the PI into RDRAM. */
static void dma_pi_write(struct pi_controller *pi)
{
   uint32_t length, i;
   uint32_t dram_address;
   uint32_t rom_address;
   uint8_t* dram;
   const uint8_t* rom;

   if (pi->regs[PI_CART_ADDR_REG] < 0x10000000 && !(pi->regs[PI_CART_ADDR_REG] >= 0x06000000 && pi->regs[PI_CART_ADDR_REG] < 0x08000000))
   {
      if (pi->regs[PI_CART_ADDR_REG] >= 0x08000000 && pi->regs[PI_CART_ADDR_REG] < 0x08010000)
      {
         if (pi->use_flashram != 1)
         {
            dma_read_sram(pi);
            pi->use_flashram = -1;
         }
         else
         {
            dma_read_flashram(pi);
         }
      }
      else if (pi->regs[PI_CART_ADDR_REG] >= 0x05000000 && pi->regs[PI_CART_ADDR_REG] < 0x06000000)
      {
         //64DD REG/BUFFER
         length = (pi->regs[PI_WR_LEN_REG] & 0xFFFFFF) + 1;
         i = (pi->regs[PI_CART_ADDR_REG] - 0x05000000) & 0x1FFFFFF;

         if (pi->regs[PI_CART_ADDR_REG] == 0x05000400)
         {
            //SECTOR BUFFER
            i -= 0x400;
            length = (i + length) > 0x100 ? (0x100 - i) : length;
            rom_address = (pi->regs[PI_CART_ADDR_REG] - 0x05000400) & 0x3fffff;
            rom = (uint8_t*)g_dd.sec_buf;
            //g_dd.regs[ASIC_CMD_STATUS] &= ~0x14000000;
            //g_dd.regs[ASIC_CMD_STATUS] &= ~0x10000000;
         }
         else if (pi->regs[PI_CART_ADDR_REG] == 0x05000000)
         {
            //C2 BUFFER
            rom_address = (pi->regs[PI_CART_ADDR_REG] - 0x05000000) & 0x3fffff;
            length      = (i + length) > 0x400 ? (0x400 - i) : length;
            rom         = (uint8_t*)g_dd.c2_buf;
            //g_dd.regs[ASIC_CMD_STATUS] &= ~0x44000000;
            //g_dd.regs[ASIC_CMD_STATUS] &= ~0x40000000;
         }
         else
         {
            pi->regs[PI_STATUS_REG] |= 3;
            cp0_update_count();
            add_interupt_event(PI_INT, length / 8);

            return;
         }

         length = (pi->regs[PI_DRAM_ADDR_REG] + length) > 0x7FFFFF ?
            (0x7FFFFF - pi->regs[PI_DRAM_ADDR_REG]) : length;

         dram_address = pi->regs[PI_DRAM_ADDR_REG];
         dram = (uint8_t*)pi->ri->rdram.dram;

         for (i = 0; i < length; ++i)
            dram[(dram_address + i) ^ S8] = rom[(rom_address + i) ^ S8];

         invalidate_r4300_cached_code(0x80000000 + dram_address, length);
         invalidate_r4300_cached_code(0xa0000000 + dram_address, length);

         //dd_update_bm(&g_dd);
      }
      else
      {
#if 0
         DebugMessage(M64MSG_WARNING, "Unknown dma write 0x%" PRIX32 " in dma_pi_write()", pi->regs[PI_CART_ADDR_REG]);
#endif
      }

      pi->regs[PI_STATUS_REG] |= 3;
      cp0_update_count();
      //add_interupt_event(PI_INT, /*pi->regs[PI_WR_LEN_REG]*/0x1000);
      add_interupt_event(PI_INT, ((pi->regs[PI_WR_LEN_REG] * 63) / 25));

      return;
   }

   if (pi->regs[PI_CART_ADDR_REG] >= 0x1fc00000) // for paper mario
   {
      pi->regs[PI_STATUS_REG] |= 1;
      cp0_update_count();
      add_interupt_event(PI_INT, 0x1000);

      return;
   }

   if (pi->regs[PI_CART_ADDR_REG] >= 0x06000000 && pi->regs[PI_CART_ADDR_REG] < 0x08000000)
   {
      /* 64DD IPL */
      length = (pi->regs[PI_WR_LEN_REG] & 0xFFFFFF) + 1;
      i = (pi->regs[PI_CART_ADDR_REG] - 0x06000000) & 0x1FFFFFF;
      length = (i + length) > pi->dd_rom.rom_size ?
         (pi->dd_rom.rom_size - i) : length;
      length = (pi->regs[PI_DRAM_ADDR_REG] + length) > 0x7FFFFF ?
         (0x7FFFFF - pi->regs[PI_DRAM_ADDR_REG]) : length;

      if (i > pi->dd_rom.rom_size || pi->regs[PI_DRAM_ADDR_REG] > 0x7FFFFF)
      {
         pi->regs[PI_STATUS_REG] |= 3;
         cp0_update_count();
         add_interupt_event(PI_INT, length / 8);

         return;
      }

      dram_address = pi->regs[PI_DRAM_ADDR_REG];
      rom_address = (pi->regs[PI_CART_ADDR_REG] - 0x06000000) & 0x3fffff;
      dram = (uint8_t*)pi->ri->rdram.dram;
      rom = pi->dd_rom.rom;
   }
   else
   {
      /* CART ROM */
      length = (pi->regs[PI_WR_LEN_REG] & 0xFFFFFF) + 1;
      i = (pi->regs[PI_CART_ADDR_REG] - 0x10000000) & 0x3FFFFFF;
      length = (i + length) > pi->cart_rom.rom_size ?
         (pi->cart_rom.rom_size - i) : length;
      length = (pi->regs[PI_DRAM_ADDR_REG] + length) > 0x7FFFFF ?
         (0x7FFFFF - pi->regs[PI_DRAM_ADDR_REG]) : length;

      if (i > pi->cart_rom.rom_size || pi->regs[PI_DRAM_ADDR_REG] > 0x7FFFFF)
      {
         pi->regs[PI_STATUS_REG] |= 3;
         cp0_update_count();
         add_interupt_event(PI_INT, length / 8);

         return;
      }

      dram_address = pi->regs[PI_DRAM_ADDR_REG];
      rom_address = (pi->regs[PI_CART_ADDR_REG] - 0x10000000) & 0x3ffffff;
      dram = (uint8_t*)pi->ri->rdram.dram;
      rom = pi->cart_rom.rom;
   }

   for (i = 0; i < length; ++i)
      dram[(dram_address + i) ^ S8] = rom[(rom_address + i) ^ S8];

   invalidate_r4300_cached_code(0x80000000 + dram_address, length);
   invalidate_r4300_cached_code(0xa0000000 + dram_address, length);

   /* HACK: monitor PI DMA to trigger RDRAM size detection
    * hack just before initial cart ROM loading. */
   if (pi->regs[PI_CART_ADDR_REG] == 0x10001000 || pi->regs[PI_CART_ADDR_REG] == 0x06001000)
   {
      force_detected_rdram_size_hack();
   }
   pi->regs[PI_STATUS_REG] |= 3;
   cp0_update_count();
   add_interupt_event(PI_INT, length / 8);
}

void connect_pi(struct pi_controller* pi,
                struct r4300_core* r4300,
                struct ri_controller *ri,
                uint8_t *rom, size_t rom_size,
                uint8_t *ddrom, size_t ddrom_size)
{
   connect_cart_rom(&pi->cart_rom, rom, rom_size);
   connect_dd_rom(&pi->dd_rom, ddrom, ddrom_size);

   pi->r4300 = r4300;
   pi->ri    = ri;
}

/* Initializes the PI. */
void init_pi(struct pi_controller* pi)
{
    memset(pi->regs, 0, PI_REGS_COUNT*sizeof(uint32_t));

    init_flashram(&pi->flashram);

    pi->use_flashram = 0;
}

/* Reads a word from the PI MMIO register space. */
int read_pi_regs(void* opaque, uint32_t address, uint32_t* value)
{
    struct pi_controller* pi = (struct pi_controller*)opaque;
    uint32_t reg             = PI_REG(address);

    *value                   = pi->regs[reg];

    return 0;
}

/* writes a word to the PI MMIO register space. */
int write_pi_regs(void* opaque, uint32_t address,
      uint32_t value, uint32_t mask)
{
   struct pi_controller* pi = (struct pi_controller*)opaque;
   uint32_t reg             = PI_REG(address);

   switch (reg)
   {
      case PI_CART_ADDR_REG:
      {
         if (pi->regs[PI_CART_ADDR_REG] == 0x05000000)
         {
            g_dd.regs[ASIC_CMD_STATUS] &= ~0x1C000000;
            dd_pi_test();
         }
         else if (pi->regs[PI_CART_ADDR_REG] == 0x05000400)
         {
            g_dd.regs[ASIC_CMD_STATUS] &= ~0x4C000000;
            dd_pi_test();
         }
         break;
      }
      case PI_RD_LEN_REG:
         pi->regs[PI_RD_LEN_REG] = MASKED_WRITE(&pi->regs[PI_RD_LEN_REG], value, mask);
         dma_pi_read(pi);
         return 0;

      case PI_WR_LEN_REG:
         pi->regs[PI_WR_LEN_REG] = MASKED_WRITE(&pi->regs[PI_WR_LEN_REG], value, mask);
         dma_pi_write(pi);
         return 0;

      case PI_STATUS_REG:
         if (value & mask & 2)
            clear_rcp_interrupt(pi->r4300, MI_INTR_PI);
         return 0;

      case PI_BSD_DOM1_LAT_REG:
      case PI_BSD_DOM1_PWD_REG:
      case PI_BSD_DOM1_PGS_REG:
      case PI_BSD_DOM1_RLS_REG:
      case PI_BSD_DOM2_LAT_REG:
      case PI_BSD_DOM2_PWD_REG:
      case PI_BSD_DOM2_PGS_REG:
      case PI_BSD_DOM2_RLS_REG:
         pi->regs[reg] = MASKED_WRITE(&pi->regs[reg], value & 0xff, mask);
         return 0;
   }

   pi->regs[reg] = MASKED_WRITE(&pi->regs[reg], value, mask);

   return 0;
}

void pi_end_of_dma_event(struct pi_controller* pi)
{
   pi->regs[PI_STATUS_REG] &= ~3;
   raise_rcp_interrupt(pi->r4300, MI_INTR_PI);

   if ((pi->regs[PI_CART_ADDR_REG] == 0x05000000) || (pi->regs[PI_CART_ADDR_REG] == 0x05000400))
   {
      printf("--DD UPDATE BM CONTEXT - PI EVENT\n");
      dd_update_bm(&g_dd);
   }
}
