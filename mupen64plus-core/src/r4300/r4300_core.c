/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - r4300_core.c                                            *
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

#include "r4300_core.h"

#include "cached_interp.h"
#include "cp0_private.h"
#include "cp1_private.h"
#include "mi_controller.h"
#include "new_dynarec/new_dynarec.h"
#include "r4300.h"
#include "recomp.h"

void poweron_r4300(struct r4300_core* r4300)
{
   unsigned int i;

   /* clear r4300 registers and TLB entries */
   for (i = 0; i < 32; i++)
   {
      reg[i]=0;
      g_cp0_regs[i]=0;
      reg_cop1_fgr_64[i]=0;

      /* --------------tlb------------------------ */
      tlb_e[i].mask=0;
      tlb_e[i].vpn2=0;
      tlb_e[i].g=0;
      tlb_e[i].asid=0;
      tlb_e[i].pfn_even=0;
      tlb_e[i].c_even=0;
      tlb_e[i].d_even=0;
      tlb_e[i].v_even=0;
      tlb_e[i].pfn_odd=0;
      tlb_e[i].c_odd=0;
      tlb_e[i].d_odd=0;
      tlb_e[i].v_odd=0;
      tlb_e[i].r=0;
#if 0
      tlb_e[i].check_parity_mask=0x1000;
#endif

      tlb_e[i].start_even=0;
      tlb_e[i].end_even=0;
      tlb_e[i].phys_even=0;
      tlb_e[i].start_odd=0;
      tlb_e[i].end_odd=0;
      tlb_e[i].phys_odd=0;
   }
   for (i=0; i<0x100000; i++)
   {
      tlb_LUT_r[i] = 0;
      tlb_LUT_w[i] = 0;
   }
   hi=0;
   lo=0;
   llbit=0;

   r4300->delay_slot = 0;

   FCR0 = UINT32_C(0x511);
   FCR31=0;

   /* set COP0 registers */
   g_cp0_regs[CP0_RANDOM_REG] = UINT32_C(31);
   g_cp0_regs[CP0_STATUS_REG]= UINT32_C(0x34000000);
   set_fpr_pointers(g_cp0_regs[CP0_STATUS_REG]);
   g_cp0_regs[CP0_CONFIG_REG]= UINT32_C(0x6e463);
   g_cp0_regs[CP0_PREVID_REG] = UINT32_C(0xb00);
   g_cp0_regs[CP0_COUNT_REG] = UINT32_C(0x5000);
   g_cp0_regs[CP0_CAUSE_REG] = UINT32_C(0x5C);
   g_cp0_regs[CP0_CONTEXT_REG] = UINT32_C(0x7FFFF0);
   g_cp0_regs[CP0_EPC_REG] = UINT32_C(0xFFFFFFFF);
   g_cp0_regs[CP0_BADVADDR_REG] = UINT32_C(0xFFFFFFFF);
   g_cp0_regs[CP0_ERROREPC_REG] = UINT32_C(0xFFFFFFFF);

   update_x86_rounding_mode(FCR31);
   poweron_mi(&r4300->mi);
}

int64_t* r4300_regs(void)
{
    return reg;
}

int64_t* r4300_mult_hi(void)
{
    return &hi;
}

int64_t* r4300_mult_lo(void)
{
    return &lo;
}

unsigned int* r4300_llbit(void)
{
    return &llbit;
}

uint32_t* r4300_pc(void)
{
#ifdef NEW_DYNAREC
   if (r4300emu == CORE_DYNAREC)
      return (uint32_t*)&pcaddr;
#endif
   return &PC->addr;
}

uint32_t* r4300_last_addr(void)
{
    return &last_addr;
}

unsigned int* r4300_next_interrupt(void)
{
    return &next_interupt;
}

unsigned int get_r4300_emumode(void)
{
    return r4300emu;
}

void invalidate_r4300_cached_code(uint32_t address, size_t size)
{
   if (r4300emu == CORE_PURE_INTERPRETER)
      return;

#ifdef NEW_DYNAREC
   if (r4300emu == CORE_DYNAREC)
      invalidate_cached_code_new_dynarec(address, size);
   else
#endif
      invalidate_cached_code_hacktarux(address, size);
}

/* XXX: not really a good interface but it gets the job done... */
void savestates_load_set_pc(uint32_t pc)
{
#ifdef NEW_DYNAREC
    if (r4300emu == CORE_DYNAREC)
    {
        pcaddr = pc;
        pending_exception = 1;
        invalidate_all_pages();
    }
    else
#endif
    {
        generic_jump_to(pc);
        invalidate_r4300_cached_code(0,0);
    }
}
