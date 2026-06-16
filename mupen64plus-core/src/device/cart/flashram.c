/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - flashram.c                                              *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
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

#include "flashram.h"
#include "cart.h"
#include "../rcp/pi/pi_controller.h"

#include "../../api/m64p_types.h"
#include "../../api/callbacks.h"
#include "../memory/memory.h"
#include "../rcp/ri/ri_controller.h"
#include "../rdram/safe_rdram.h"
#include "../../backends/api/storage_backend.h"
#include "../../main/main.h"
#include "../device.h"

#include <string.h>

void init_flashram(struct flashram* flashram,
      void* storage,
      const struct storage_backend_interface* istorage)
{
   flashram->storage  = storage;
   flashram->istorage = istorage;
}

static void flashram_command(struct pi_controller *pi, uint32_t command)
{
   unsigned int i;
   struct flashram *flashram = &pi->cart->flashram;
   uint8_t *dram             = (uint8_t*)pi->ri->rdram->dram;
   uint8_t *mem              = flashram->istorage->data(flashram->storage);

   switch (command & 0xff000000)
   {
      case 0x4b000000:
         flashram->erase_offset = (command & 0xffff) * 128;
         break;
      case 0x78000000:
         flashram->mode = FLASHRAM_MODE_ERASE;
         flashram->status = 0x1111800800c20000LL;
         break;
      case 0xa5000000:
         flashram->erase_offset = (command & 0xffff) * 128;
         flashram->status = 0x1111800400c20000LL;
         break;
      case 0xb4000000:
         flashram->mode = FLASHRAM_MODE_WRITE;
         break;
      case 0xd2000000:  // execute
         switch (flashram->mode)
         {
            case FLASHRAM_MODE_NOPES:
            case FLASHRAM_MODE_READ:
               break;
            case FLASHRAM_MODE_ERASE:
               {
                  for (i=flashram->erase_offset; i<(flashram->erase_offset+128); ++i)
                  {
                     if ((i^S8) < (unsigned)FLASHRAM_SIZE)
                        mem[i^S8] = 0xff;
                  }
                  flashram->istorage->save(flashram->storage, 0, FLASHRAM_SIZE);
               }
               break;
            case FLASHRAM_MODE_WRITE:
               {
                  for(i = 0; i < 128; ++i)
                  {
                     const unsigned int flash_i = (flashram->erase_offset+i)^S8;
                     if (flash_i >= (unsigned)FLASHRAM_SIZE) continue;
                     mem[flash_i] = rdram_safe_read_byte(dram, (flashram->write_pointer+i)^S8);
                  }
                  flashram->istorage->save(flashram->storage, 0, FLASHRAM_SIZE);
               }
               break;
            case FLASHRAM_MODE_STATUS:
               break;
            default:
               DebugMessage(M64MSG_WARNING, "unknown flashram command with mode:%x", (int)flashram->mode);
               break;
         }
         flashram->mode = FLASHRAM_MODE_NOPES;
         break;
      case 0xe1000000:
         flashram->mode = FLASHRAM_MODE_STATUS;
         flashram->status = 0x1111800100c20000LL;
         flashram->status |= 0x01; /* Needed for Pokemon Puzzle League */
         break;
      case 0xf0000000:
         flashram->mode = FLASHRAM_MODE_READ;
         flashram->status = 0x11118004f0000000LL;
         break;
      case 0x00000000:
         break;
      default:
         DebugMessage(M64MSG_WARNING, "unknown flashram command: %x", (int)command);
         break;
   }
}

void poweron_flashram(struct flashram* flashram)
{
   flashram->mode          = FLASHRAM_MODE_NOPES;
   flashram->status        = 0;
   flashram->erase_offset  = 0;
   flashram->write_pointer = 0;
}

void format_flashram(uint8_t* flash)
{
   memset(flash, 0xff, FLASHRAM_SIZE);
}

int read_flashram_status(void* opaque, uint32_t address, uint32_t* value)
{
   struct pi_controller* pi = (struct pi_controller*)opaque;

   if ((pi->cart->use_flashram == -1) || ((address & 0xffff) != 0))
   {
      DebugMessage(M64MSG_ERROR, "unknown read in read_flashram_status()");
      return -1;
   }
   pi->cart->use_flashram = 1;
   *value = pi->cart->flashram.status >> 32;
   return 0;
}

int write_flashram_command(void* opaque, uint32_t address, uint32_t value, uint32_t mask)
{
   struct pi_controller* pi = (struct pi_controller*)opaque;

   if ((pi->cart->use_flashram == -1) || ((address & 0xffff) != 0))
   {
      DebugMessage(M64MSG_ERROR, "unknown write in write_flashram_command()");
      return -1;
   }
   pi->cart->use_flashram = 1;
   flashram_command(pi, value & mask);
   return 0;
}

void dma_read_flashram(struct pi_controller *pi)
{
   unsigned int dram_addr, cart_addr;
   unsigned int i, length;
   struct flashram* flashram = &pi->cart->flashram;
   uint32_t *dram            = pi->ri->rdram->dram;
   uint8_t *mem              = flashram->istorage->data(flashram->storage);

   switch (flashram->mode)
   {
      case FLASHRAM_MODE_STATUS:
         dram[pi->regs[PI_DRAM_ADDR_REG]/4]   = (uint32_t)(flashram->status >> 32);
         dram[pi->regs[PI_DRAM_ADDR_REG]/4+1] = (uint32_t)(flashram->status);
         break;
      case FLASHRAM_MODE_READ:
         length = (pi->regs[PI_WR_LEN_REG] & 0xffffff) + 1;
         dram_addr = pi->regs[PI_DRAM_ADDR_REG];
         cart_addr = ((pi->regs[PI_CART_ADDR_REG]-0x08000000)&0xffff)*2;

         for (i = 0; i < length; ++i)
         {
            const unsigned int cart_i = (cart_addr+i)^S8;
            rdram_safe_write_byte(dram, (dram_addr+i)^S8, (cart_i < (unsigned)FLASHRAM_SIZE) ? mem[cart_i] : 0);
         }
         break;
      default:
         DebugMessage(M64MSG_WARNING, "unknown dma_read_flashram: %x", flashram->mode);
         break;
   }
}

void dma_write_flashram(struct pi_controller *pi)
{
   struct flashram *flashram = &pi->cart->flashram;

   switch (flashram->mode)
   {
      case FLASHRAM_MODE_WRITE:
         flashram->write_pointer = pi->regs[PI_DRAM_ADDR_REG];
         break;
      default:
         DebugMessage(M64MSG_ERROR, "unknown dma_write_flashram: %x", flashram->mode);
         break;
   }
}

/* mupen64plus-next-style accessors (used by the joybus/PI-DMA cart dispatch).
 * These present next's (opaque, ...) signatures but drive parallel-n64's
 * existing flashram mode/status machine rather than next's silicon_id/page_buf
 * model, so the save protocol and status words are unchanged. Note the naming
 * follows next's PI-perspective convention, which is inverted relative to
 * pn64's dma_*_flashram(pi) helpers:
 *   flashram_dma_write == cart -> DRAM (status/array read-back)  [pn64 dma_read]
 *   flashram_dma_read  == DRAM -> cart (program staging)         [pn64 dma_write] */
void read_flashram(void* opaque, uint32_t address, uint32_t* value)
{
   struct flashram* flashram = (struct flashram*)opaque;

   if ((address & 0xffff) != 0)
   {
      DebugMessage(M64MSG_ERROR, "unknown read in read_flashram()");
      return;
   }
   *value = (uint32_t)(flashram->status >> 32);
}

void write_flashram(void* opaque, uint32_t address, uint32_t value, uint32_t mask)
{
   struct flashram* flashram = (struct flashram*)opaque;
   (void)flashram;

   if ((address & 0xffff) != 0)
   {
      DebugMessage(M64MSG_ERROR, "unknown write in write_flashram()");
      return;
   }
   /* pn64's flashram_command takes the pi_controller (its execute path reads
    * DRAM for the WRITE mode); reach it through the device global, matching
    * write_flashram_command's behaviour. */
   flashram_command(&g_dev.pi, value & mask);
}

unsigned int flashram_dma_write(void* opaque, uint8_t* dram, uint32_t dram_addr, uint32_t cart_addr, uint32_t length)
{
   /* cart -> DRAM: status report or array read-back (pn64 dma_read_flashram) */
   struct flashram* flashram = (struct flashram*)opaque;
   const uint8_t* mem = flashram->istorage->data(flashram->storage);
   unsigned int i;

   switch (flashram->mode)
   {
      case FLASHRAM_MODE_STATUS:
         ((uint32_t*)dram)[dram_addr/4]   = (uint32_t)(flashram->status >> 32);
         ((uint32_t*)dram)[dram_addr/4+1] = (uint32_t)(flashram->status);
         break;
      case FLASHRAM_MODE_READ:
      {
         uint32_t cart = (cart_addr & 0xffff) * 2;
         for (i = 0; i < length; ++i)
         {
            const unsigned int cart_i = (cart+i)^S8;
            dram[(dram_addr+i)^S8] = (cart_i < (unsigned)FLASHRAM_SIZE) ? mem[cart_i] : 0;
         }
         break;
      }
      default:
         DebugMessage(M64MSG_WARNING, "unknown flashram_dma_write: %x", flashram->mode);
         break;
   }

   return /* length / 8 */0x1000;
}

unsigned int flashram_dma_read(void* opaque, const uint8_t* dram, uint32_t dram_addr, uint32_t cart_addr, uint32_t length)
{
   /* DRAM -> cart: program staging (pn64 dma_write_flashram sets write_pointer) */
   struct flashram* flashram = (struct flashram*)opaque;

   switch (flashram->mode)
   {
      case FLASHRAM_MODE_WRITE:
         flashram->write_pointer = dram_addr;
         break;
      default:
         DebugMessage(M64MSG_ERROR, "unknown flashram_dma_read: %x", flashram->mode);
         break;
   }

   return /* length / 8 */0x1000;
}
