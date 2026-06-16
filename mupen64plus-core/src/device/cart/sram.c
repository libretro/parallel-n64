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

/* mupen64plus-next-style accessors (used by the joybus/PI-DMA cart dispatch).
 * These take the operands explicitly (next form) rather than reading the PI
 * registers, and keep parallel-n64's bounds-checked addressing: the cart
 * address is masked to 0xffff (next) but every byte is still range-checked
 * against SRAM_SIZE so the 0x8000-0xffff half cannot read/write out of bounds. */
unsigned int sram_dma_read(void* opaque, const uint8_t* dram, uint32_t dram_addr, uint32_t cart_addr, uint32_t length)
{
   /* DRAM -> SRAM (the save write) */
   size_t i;
   struct sram* sram = (struct sram*)opaque;
   uint8_t* mem = sram->istorage->data(sram->storage);

   cart_addr &= 0xffff;

   for (i = 0; i < length; ++i)
   {
      const unsigned int sram_i = (cart_addr+i)^S8;
      if (sram_i >= (unsigned)SRAM_SIZE) continue;
      mem[sram_i] = rdram_safe_read_byte(dram, (dram_addr+i)^S8);
   }

   sram->istorage->save(sram->storage, 0, SRAM_SIZE);

   return /* length / 8 */0x1000;
}

unsigned int sram_dma_write(void* opaque, uint8_t* dram, uint32_t dram_addr, uint32_t cart_addr, uint32_t length)
{
   /* SRAM -> DRAM (the read back) */
   size_t i;
   struct sram* sram = (struct sram*)opaque;
   const uint8_t* mem = sram->istorage->data(sram->storage);

   cart_addr &= 0xffff;

   for (i = 0; i < length; ++i)
   {
      const unsigned int sram_i = (cart_addr+i)^S8;
      rdram_safe_write_byte(dram, (dram_addr+i)^S8, (sram_i < (unsigned)SRAM_SIZE) ? mem[sram_i] : 0);
   }

   return /* length / 8 */0x1000;
}

void read_sram(void* opaque, uint32_t address, uint32_t* value)
{
   struct sram* sram = (struct sram*)opaque;
   const uint8_t* mem = sram->istorage->data(sram->storage);

   address &= 0xffff;
   if (address + sizeof(uint32_t) <= (unsigned)SRAM_SIZE)
      *value = *(uint32_t*)(mem + address);
   else
      *value = 0;
}

void write_sram(void* opaque, uint32_t address, uint32_t value, uint32_t mask)
{
   struct sram* sram = (struct sram*)opaque;
   uint8_t* mem = sram->istorage->data(sram->storage);

   address &= 0xffff;
   if (address + sizeof(uint32_t) <= (unsigned)SRAM_SIZE)
   {
      uint32_t* dst = (uint32_t*)(mem + address);
      *dst = MASKED_WRITE(dst, value, mask);
      sram->istorage->save(sram->storage, address, sizeof(value));
   }
}
