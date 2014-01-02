/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - tlb.h                                                   *
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

#ifndef TLB_H
#define TLB_H

#include <stdint.h>

typedef struct _tlb
{
   int16_t mask;
   int32_t vpn2;
   int8_t g;
   uint8_t asid;
   int32_t pfn_even;
   int8_t c_even;
   int8_t d_even;
   int8_t v_even;
   int32_t pfn_odd;
   int8_t c_odd;
   int8_t d_odd;
   int8_t v_odd;
   int8_t r;
   //int32_t check_parity_mask;
   
   uint32_t start_even;
   uint32_t end_even;
   uint32_t phys_even;
   uint32_t start_odd;
   uint32_t end_odd;
   uint32_t phys_odd;
} tlb;

extern uint32_t tlb_LUT_r[0x100000];
extern uint32_t tlb_LUT_w[0x100000];
void tlb_unmap(tlb *entry);
void tlb_map(tlb *entry);
uint32_t virtual_to_physical_address(uint32_t addresse, int w);

#endif

