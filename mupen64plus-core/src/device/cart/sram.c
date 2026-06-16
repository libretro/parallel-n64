/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - sram.c                                                  *
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

#include "sram.h"
#include "cart.h"
#include "../rcp/pi/pi_controller.h"

#include "../memory/memory.h"

#include "../rcp/ri/ri_controller.h"
#include "../rdram/safe_rdram.h"
#include "../../backends/api/storage_backend.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

void init_sram(struct sram* sram, void* storage, const struct storage_backend_interface* istorage)
{
   sram->storage  = storage;
   sram->istorage = istorage;
}

void format_sram(uint8_t* sram)
{
   /* parallel-n64 formats a fresh SRAM to zero (next uses 0xff); keep pn64's
    * value so a brand-new SRAM save is unchanged. */
   memset(sram, 0, SRAM_SIZE);
}

void dma_write_sram(struct pi_controller* pi)
{
   size_t i;
   size_t length = (pi->regs[PI_RD_LEN_REG] & 0xffffff) + 1;

   uint8_t* sram = pi->cart->sram.istorage->data(pi->cart->sram.storage);
   uint8_t* dram = (uint8_t*)pi->ri->rdram->dram;
   uint32_t cart_addr = pi->regs[PI_CART_ADDR_REG] - 0x08000000;
   uint32_t dram_addr = pi->regs[PI_DRAM_ADDR_REG];

   for(i = 0; i < length; ++i)
   {
      const unsigned int sram_i = (cart_addr+i)^S8;
      if (sram_i >= (unsigned)SRAM_SIZE) continue;
      sram[sram_i] = rdram_safe_read_byte(dram, (dram_addr+i)^S8);
   }

   pi->cart->sram.istorage->save(pi->cart->sram.storage, 0, SRAM_SIZE);
}

void dma_read_sram(struct pi_controller* pi)
{
   size_t i;
   size_t length = (pi->regs[PI_WR_LEN_REG] & 0xffffff) + 1;

   uint8_t* sram = pi->cart->sram.istorage->data(pi->cart->sram.storage);
   uint8_t* dram = (uint8_t*)pi->ri->rdram->dram;
   uint32_t cart_addr = (pi->regs[PI_CART_ADDR_REG] - 0x08000000) & 0xffff;
   uint32_t dram_addr = pi->regs[PI_DRAM_ADDR_REG];

   for(i = 0; i < length; ++i)
   {
      const unsigned int sram_i = (cart_addr+i)^S8;
      rdram_safe_write_byte(dram, (dram_addr+i)^S8, (sram_i < (unsigned)SRAM_SIZE) ? sram[sram_i] : 0);
   }
}
