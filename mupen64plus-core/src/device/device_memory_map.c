/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - device_memory_map.c                                     *
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

#include "device_memory_map.h"

#include <string.h>

#include "device.h"
#include "r4300/r4300_core.h"

static size_t add_desc(struct retro_memory_descriptor* descs, size_t n,
                       size_t max, void* ptr, size_t start, size_t len,
                       uint64_t flags)
{
    if (n >= max || ptr == NULL || len == 0)
        return n;

    memset(&descs[n], 0, sizeof(descs[n]));
    descs[n].ptr   = ptr;
    descs[n].start = start;
    descs[n].len   = len;
    descs[n].flags = flags;
    return n + 1;
}

size_t device_memory_descriptors(const struct device* dev,
                                 struct retro_memory_descriptor* descs,
                                 size_t max)
{
    size_t n = 0;

    /* RDRAM is reachable cached through KSEG0 and uncached through KSEG1 */
    n = add_desc(descs, n, max, dev->rdram.dram, R4300_KSEG0 | MM_RDRAM_DRAM,
                 dev->rdram.dram_size, RETRO_MEMDESC_SYSTEM_RAM);
    if (n > 0) {
        descs[n - 1].select     = 0x20000000;
        descs[n - 1].disconnect = 0xC0000000;
    }
    n = add_desc(descs, n, max, dev->rdram.dram, R4300_KSEG1 | MM_RDRAM_DRAM,
                 dev->rdram.dram_size, RETRO_MEMDESC_SYSTEM_RAM);

    n = add_desc(descs, n, max, dev->sp.mem, R4300_KSEG1 | MM_RSP_MEM,
                 SP_MEM_SIZE, 0);

    /* the PIF ROM is read only to the CPU */
    n = add_desc(descs, n, max, dev->pif.base, R4300_KSEG1 | MM_PIF_MEM,
                 PIF_ROM_SIZE, RETRO_MEMDESC_CONST);
    n = add_desc(descs, n, max, dev->pif.ram, R4300_KSEG1 | (MM_PIF_MEM + PIF_ROM_SIZE),
                 PIF_RAM_SIZE, 0);

    if (dev->cart.cart_rom.rom_size > 0)
        n = add_desc(descs, n, max, dev->cart.cart_rom.rom, R4300_KSEG1 | MM_CART_ROM,
                     dev->cart.cart_rom.rom_size, RETRO_MEMDESC_CONST);

    if (dev->dd.rom_size > 0)
        n = add_desc(descs, n, max, (void*)dev->dd.rom, R4300_KSEG1 | MM_DD_ROM,
                     dev->dd.rom_size, RETRO_MEMDESC_CONST);

    return n;
}
