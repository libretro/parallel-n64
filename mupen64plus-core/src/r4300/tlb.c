/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - tlb.c                                                   *
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

#include "tlb.h"
#include "exception.h"

#include "main/rom.h"

tlb tlb_e[32];

uint32_t tlb_LUT_r[0x100000];
uint32_t tlb_LUT_w[0x100000];

void tlb_unmap(tlb *entry)
{
    unsigned int i;

    if (entry->v_even)
    {
        for (i=entry->start_even; i<entry->end_even; i += 0x1000)
            tlb_LUT_r[i>>12] = 0;
        if (entry->d_even)
            for (i=entry->start_even; i<entry->end_even; i += 0x1000)
                tlb_LUT_w[i>>12] = 0;
    }

    if (entry->v_odd)
    {
        for (i=entry->start_odd; i<entry->end_odd; i += 0x1000)
            tlb_LUT_r[i>>12] = 0;
        if (entry->d_odd)
            for (i=entry->start_odd; i<entry->end_odd; i += 0x1000)
                tlb_LUT_w[i>>12] = 0;
    }
}

void tlb_map(tlb *entry)
{
    unsigned int i;

    if (entry->v_even)
    {
        if (entry->start_even < entry->end_even &&
            !(entry->start_even >= 0x80000000 && entry->end_even < 0xC0000000) &&
            entry->phys_even < 0x20000000)
        {
            for (i=entry->start_even;i<entry->end_even;i+=0x1000)
                tlb_LUT_r[i>>12] = UINT32_C(0x80000000) | (entry->phys_even + (i - entry->start_even) + 0xFFF);
            if (entry->d_even)
                for (i=entry->start_even;i<entry->end_even;i+=0x1000)
                    tlb_LUT_w[i>>12] = UINT32_C(0x80000000) | (entry->phys_even + (i - entry->start_even) + 0xFFF);
        }
    }

    if (entry->v_odd)
    {
        if (entry->start_odd < entry->end_odd &&
            !(entry->start_odd >= 0x80000000 && entry->end_odd < 0xC0000000) &&
            entry->phys_odd < 0x20000000)
        {
            for (i=entry->start_odd;i<entry->end_odd;i+=0x1000)
                tlb_LUT_r[i>>12] = UINT32_C(0x80000000) | (entry->phys_odd + (i - entry->start_odd) + 0xFFF);
            if (entry->d_odd)
                for (i=entry->start_odd;i<entry->end_odd;i+=0x1000)
                    tlb_LUT_w[i>>12] = UINT32_C(0x80000000) | (entry->phys_odd + (i - entry->start_odd) + 0xFFF);
        }
    }
}

uint32_t virtual_to_physical_address(uint32_t address, int mode)
{
    if (address >= UINT32_C(0x7f000000) && address < UINT32_C(0x80000000) && isGoldeneyeRom)
    {
        /**************************************************
         GoldenEye 007 hack allows for use of TLB.
         Recoded by okaygo to support all US, J, and E ROMS.
        **************************************************/
        switch (ROM_HEADER.destination_code)
        {
        case 'E': /* North American */
            return UINT32_C(0xb0034b30) + (address & UINT32_C(0xFFFFFF));
        case 'J': /* Japanese */
            return UINT32_C(0xb0034b70) + (address & UINT32_C(0xFFFFFF));
        case 'P': /* European (PAL spec.) */
            return UINT32_C(0xb00329f0) + (address & UINT32_C(0xFFFFFF));
        default:
            // UNKNOWN COUNTRY CODE FOR GOLDENEYE USING AMERICAN VERSION HACK
            return UINT32_C(0xb0034b30) + (address & UINT32_C(0xFFFFFF));
        }
    }

    if (mode == TLB_WRITE)
    {
        if (tlb_LUT_w[address>>12])
            return (tlb_LUT_w[address>>12] & UINT32_C(0xFFFFF000))|(address & UINT32_C(0xFFF));
    }
    else
    {
        if (tlb_LUT_r[address>>12])
            return (tlb_LUT_r[address>>12] & UINT32_C(0xFFFFF000))|(address & UINT32_C(0xFFF));
    }

    TLB_refill_exception(address, mode);

    return 0x00000000;
}
