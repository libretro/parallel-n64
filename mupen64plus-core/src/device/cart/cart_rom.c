/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - cart_rom.c                                              *
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

#include "cart_rom.h"
#include "cart.h"
#include "../rcp/pi/pi_controller.h"
#include "../memory/m64p_memory.h"
#include "../r4300/r4300_core.h"

void init_cart_rom(struct cart_rom* cart_rom,
                      uint8_t* rom, size_t rom_size)
{
    cart_rom->rom      = rom;
    cart_rom->rom_size = rom_size;
}

void poweron_cart_rom(struct cart_rom* cart_rom)
{
    cart_rom->last_write  = 0;
    cart_rom->rom_written = 0;
}


int read_cart_rom(void* opaque, uint32_t address, uint32_t* value)
{
    struct pi_controller* pi    = (struct pi_controller*)opaque;
    uint32_t addr               = ROM_ADDR(address);

    if (pi->cart->cart_rom.rom_written)
    {
        *value                   = pi->cart->cart_rom.last_write;
        pi->cart->cart_rom.rom_written = 0;
    }
    else if (addr < pi->cart->cart_rom.rom_size)
    {
        *value = *(uint32_t*)(pi->cart->cart_rom.rom + addr);
    }
    else
    {
        *value = 0;
    }

    return 0;
}

int write_cart_rom(void* opaque, uint32_t address, uint32_t value, uint32_t mask)
{
    struct pi_controller* pi     = (struct pi_controller*)opaque;
    pi->cart->cart_rom.last_write      = value & mask;
    pi->cart->cart_rom.rom_written     = 1;

    return 0;
}

/* mupen64plus-next-style ROM DMA accessors (used by the joybus/PI-DMA cart
 * dispatch). cart_rom_dma_write copies ROM -> DRAM (the normal cartridge read);
 * cart_rom_dma_read (DRAM -> ROM) is not a real operation and is a no-op, as in
 * next. These take the cart_rom struct directly (next form). parallel-n64s
 * existing inline pi_controller ROM DMA path -- with its summercart cfg_rom_write
 * gating and framebuffer DMA hooks -- is left in place; these accessors are the
 * convergent core copy that the cart dispatch will route through. */
unsigned int cart_rom_dma_read(void* opaque, const uint8_t* dram, uint32_t dram_addr, uint32_t cart_addr, uint32_t length)
{
    (void)opaque; (void)dram; (void)dram_addr; (void)cart_addr;
    /* DMA writing to cart ROM is not a real operation */
    return /* length / 8 */0x1000;
}

unsigned int cart_rom_dma_write(void* opaque, uint8_t* dram, uint32_t dram_addr, uint32_t cart_addr, uint32_t length)
{
    size_t i;
    struct cart_rom* cart_rom = (struct cart_rom*)opaque;
    const uint8_t* mem = cart_rom->rom;

    cart_addr &= 0x03ffffff;

    if (cart_addr + length < cart_rom->rom_size)
    {
        for (i = 0; i < length; ++i)
            dram[(dram_addr+i)^S8] = mem[(cart_addr+i)^S8];
    }
    else
    {
        unsigned int diff = (cart_rom->rom_size <= cart_addr)
            ? 0
            : (unsigned int)(cart_rom->rom_size - cart_addr);

        for (i = 0; i < diff; ++i)
            dram[(dram_addr+i)^S8] = mem[(cart_addr+i)^S8];
        for (; i < length; ++i)
            dram[(dram_addr+i)^S8] = 0;
    }

    /* invalidate any cached code at the destination */
    invalidate_r4300_cached_code(0x80000000 + dram_addr, length);
    invalidate_r4300_cached_code(0xa0000000 + dram_addr, length);

    return /* length / 8 */0x1000;
}
