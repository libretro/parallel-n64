/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - gr4300.c                                                *
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

#include "assemble.h"
#include "../hacktarux_dynarec/interpret.h"
#include "../hacktarux_dynarec/regcache.h"

#include "api/debugger.h"
#include "main/main.h"
#include "memory/memory.h"
#include "r4300/r4300.h"
#include "r4300/cached_interp.h"
#include "r4300/cp0_private.h"
#include "r4300/cp1_private.h"
#include "r4300/interupt.h"
#include "r4300/ops.h"
#include "r4300/recomph.h"
#include "r4300/exception.h"

static precomp_instr fake_instr;
#ifdef COMPARE_CORE
static int eax, ebx, ecx, edx, esp, ebp, esi, edi;
#endif

int branch_taken;

/* static functions */

static void genbne_test(void)
{
   int rs_64bit = is64((unsigned int *)dst->f.i.rs);
   int rt_64bit = is64((unsigned int *)dst->f.i.rt);

   if (!rs_64bit && !rt_64bit)
   {
      int rs = allocate_register((unsigned int *)dst->f.i.rs);
      int rt = allocate_register((unsigned int *)dst->f.i.rt);

      cmp_reg32_reg32(rs, rt);
      je_rj(12);
      mov_m32_imm32((unsigned int *)(&branch_taken), 1); // 10
      jmp_imm_short(10); // 2
      mov_m32_imm32((unsigned int *)(&branch_taken), 0); // 10
   }
   else if (rs_64bit == -1)
   {
      int rt1 = allocate_64_register1((unsigned int *)dst->f.i.rt);
      int rt2 = allocate_64_register2((unsigned int *)dst->f.i.rt);

      cmp_reg32_m32(rt1, (unsigned int *)dst->f.i.rs);
      jne_rj(20);
      cmp_reg32_m32(rt2, ((unsigned int *)dst->f.i.rs)+1); // 6
      jne_rj(12); // 2
      mov_m32_imm32((unsigned int *)(&branch_taken), 0); // 10
      jmp_imm_short(10); // 2
      mov_m32_imm32((unsigned int *)(&branch_taken), 1); // 10
   }
   else if (rt_64bit == -1)
   {
      int rs1 = allocate_64_register1((unsigned int *)dst->f.i.rs);
      int rs2 = allocate_64_register2((unsigned int *)dst->f.i.rs);

      cmp_reg32_m32(rs1, (unsigned int *)dst->f.i.rt);
      jne_rj(20);
      cmp_reg32_m32(rs2, ((unsigned int *)dst->f.i.rt)+1); // 6
      jne_rj(12); // 2
      mov_m32_imm32((unsigned int *)(&branch_taken), 0); // 10
      jmp_imm_short(10); // 2
      mov_m32_imm32((unsigned int *)(&branch_taken), 1); // 10
   }
   else
   {
      int rs1, rs2, rt1, rt2;
      if (!rs_64bit)
      {
         rt1 = allocate_64_register1((unsigned int *)dst->f.i.rt);
         rt2 = allocate_64_register2((unsigned int *)dst->f.i.rt);
         rs1 = allocate_64_register1((unsigned int *)dst->f.i.rs);
         rs2 = allocate_64_register2((unsigned int *)dst->f.i.rs);
      }
      else
      {
         rs1 = allocate_64_register1((unsigned int *)dst->f.i.rs);
         rs2 = allocate_64_register2((unsigned int *)dst->f.i.rs);
         rt1 = allocate_64_register1((unsigned int *)dst->f.i.rt);
         rt2 = allocate_64_register2((unsigned int *)dst->f.i.rt);
      }
      cmp_reg32_reg32(rs1, rt1);
      jne_rj(16);
      cmp_reg32_reg32(rs2, rt2); // 2
      jne_rj(12); // 2
      mov_m32_imm32((unsigned int *)(&branch_taken), 0); // 10
      jmp_imm_short(10); // 2
      mov_m32_imm32((unsigned int *)(&branch_taken), 1); // 10
   }
}

static void genblez_test(void)
{
   int rs_64bit = is64((unsigned int *)dst->f.i.rs);

   if (!rs_64bit)
   {
      int rs = allocate_register((unsigned int *)dst->f.i.rs);

      cmp_reg32_imm32(rs, 0);
      jg_rj(12);
      mov_m32_imm32((unsigned int *)(&branch_taken), 1); // 10
      jmp_imm_short(10); // 2
      mov_m32_imm32((unsigned int *)(&branch_taken), 0); // 10
   }
   else if (rs_64bit == -1)
   {
      cmp_m32_imm32(((unsigned int *)dst->f.i.rs)+1, 0);
      jg_rj(14);
      jne_rj(24); // 2
      cmp_m32_imm32((unsigned int *)dst->f.i.rs, 0); // 10
      je_rj(12); // 2
      mov_m32_imm32((unsigned int *)(&branch_taken), 0); // 10
      jmp_imm_short(10); // 2
      mov_m32_imm32((unsigned int *)(&branch_taken), 1); // 10
   }
   else
   {
      int rs1 = allocate_64_register1((unsigned int *)dst->f.i.rs);
      int rs2 = allocate_64_register2((unsigned int *)dst->f.i.rs);

      cmp_reg32_imm32(rs2, 0);
      jg_rj(10);
      jne_rj(20); // 2
      cmp_reg32_imm32(rs1, 0); // 6
      je_rj(12); // 2
      mov_m32_imm32((unsigned int *)(&branch_taken), 0); // 10
      jmp_imm_short(10); // 2
      mov_m32_imm32((unsigned int *)(&branch_taken), 1); // 10
   }
}

static void genbgtz_test(void)
{
   int rs_64bit = is64((unsigned int *)dst->f.i.rs);

   if (!rs_64bit)
   {
      int rs = allocate_register((unsigned int *)dst->f.i.rs);

      cmp_reg32_imm32(rs, 0);
      jle_rj(12);
      mov_m32_imm32((unsigned int *)(&branch_taken), 1); // 10
      jmp_imm_short(10); // 2
      mov_m32_imm32((unsigned int *)(&branch_taken), 0); // 10
   }
   else if (rs_64bit == -1)
   {
      cmp_m32_imm32(((unsigned int *)dst->f.i.rs)+1, 0);
      jl_rj(14);
      jne_rj(24); // 2
      cmp_m32_imm32((unsigned int *)dst->f.i.rs, 0); // 10
      jne_rj(12); // 2
      mov_m32_imm32((unsigned int *)(&branch_taken), 0); // 10
      jmp_imm_short(10); // 2
      mov_m32_imm32((unsigned int *)(&branch_taken), 1); // 10
   }
   else
   {
      int rs1 = allocate_64_register1((unsigned int *)dst->f.i.rs);
      int rs2 = allocate_64_register2((unsigned int *)dst->f.i.rs);

      cmp_reg32_imm32(rs2, 0);
      jl_rj(10);
      jne_rj(20); // 2
      cmp_reg32_imm32(rs1, 0); // 6
      jne_rj(12); // 2
      mov_m32_imm32((unsigned int *)(&branch_taken), 0); // 10
      jmp_imm_short(10); // 2
      mov_m32_imm32((unsigned int *)(&branch_taken), 1); // 10
   }
}


/* global functions */

void genj_idle(void)
{
#ifdef INTERPRET_J_IDLE
   gencallinterp((unsigned int)cached_interpreter_table.J_IDLE, 1);
#else
   if (((dst->addr & 0xFFF) == 0xFFC && 
            (dst->addr < 0x80000000 || dst->addr >= 0xC0000000))||no_compiled_jump)
   {
      gencallinterp((unsigned int)cached_interpreter_table.J_IDLE, 1);
      return;
   }

   mov_eax_memoffs32((unsigned int *)(&next_interupt));
   sub_reg32_m32(EAX, (unsigned int *)(&g_cp0_regs[CP0_COUNT_REG]));
   cmp_reg32_imm8(EAX, 3);
   jbe_rj(11);

   and_eax_imm32(0xFFFFFFFC);  // 5
   add_m32_reg32((unsigned int *)(&g_cp0_regs[CP0_COUNT_REG]), EAX); // 6

   genj();
#endif
}

void genslti(void)
{
#ifdef INTERPRET_SLTI
   gencallinterp((unsigned int)cached_interpreter_table.SLTI, 0);
#else
   int rs1 = allocate_64_register1((unsigned int *)dst->f.i.rs);
   int rs2 = allocate_64_register2((unsigned int *)dst->f.i.rs);
   int rt = allocate_register_w((unsigned int *)dst->f.i.rt);
   long long imm = (long long)dst->f.i.immediate;

   cmp_reg32_imm32(rs2, (unsigned int)(imm >> 32));
   jl_rj(17);
   jne_rj(8); // 2
   cmp_reg32_imm32(rs1, (unsigned int)imm); // 6
   jl_rj(7); // 2
   mov_reg32_imm32(rt, 0); // 5
   jmp_imm_short(5); // 2
   mov_reg32_imm32(rt, 1); // 5
#endif
}

void genlw(void)
{
#ifdef INTERPRET_LW
   gencallinterp((unsigned int)cached_interpreter_table.LW, 0);
#else
   free_all_registers();
   simplify_access();
   mov_eax_memoffs32((unsigned int *)dst->f.i.rs);
   add_eax_imm32((int)dst->f.i.immediate);
   mov_reg32_reg32(EBX, EAX);
   if(fast_memory)
   {
      and_eax_imm32(0xDF800000);
      cmp_eax_imm32(0x80000000);
   }
   else
   {
      shr_reg32_imm8(EAX, 16);
      mov_reg32_preg32x4pimm32(EAX, EAX, (unsigned int)readmem);
      cmp_reg32_imm32(EAX, (unsigned int)read_rdram);
   }
   je_rj(45);

   mov_m32_imm32((unsigned int *)&PC, (unsigned int)(dst+1)); // 10
   mov_m32_reg32((unsigned int *)(&address), EBX); // 6
   mov_m32_imm32((unsigned int *)(&rdword), (unsigned int)dst->f.i.rt); // 10
   shr_reg32_imm8(EBX, 16); // 3
   mov_reg32_preg32x4pimm32(EBX, EBX, (unsigned int)readmem); // 7
   call_reg32(EBX); // 2
   mov_eax_memoffs32((unsigned int *)(dst->f.i.rt)); // 5
   jmp_imm_short(12); // 2

   and_reg32_imm32(EBX, 0x7FFFFF); // 6
   mov_reg32_preg32pimm32(EAX, EBX, (unsigned int)g_rdram); // 6

   set_register_state(EAX, (unsigned int*)dst->f.i.rt, 1);
#endif
}

void genlbu(void)
{
#ifdef INTERPRET_LBU
   gencallinterp((unsigned int)cached_interpreter_table.LBU, 0);
#else
   free_all_registers();
   simplify_access();
   mov_eax_memoffs32((unsigned int *)dst->f.i.rs);
   add_eax_imm32((int)dst->f.i.immediate);
   mov_reg32_reg32(EBX, EAX);
   if(fast_memory)
   {
      and_eax_imm32(0xDF800000);
      cmp_eax_imm32(0x80000000);
   }
   else
   {
      shr_reg32_imm8(EAX, 16);
      mov_reg32_preg32x4pimm32(EAX, EAX, (unsigned int)readmemb);
      cmp_reg32_imm32(EAX, (unsigned int)read_rdramb);
   }
   je_rj(46);

   mov_m32_imm32((unsigned int *)&PC, (unsigned int)(dst+1)); // 10
   mov_m32_reg32((unsigned int *)(&address), EBX); // 6
   mov_m32_imm32((unsigned int *)(&rdword), (unsigned int)dst->f.i.rt); // 10
   shr_reg32_imm8(EBX, 16); // 3
   mov_reg32_preg32x4pimm32(EBX, EBX, (unsigned int)readmemb); // 7
   call_reg32(EBX); // 2
   mov_reg32_m32(EAX, (unsigned int *)dst->f.i.rt); // 6
   jmp_imm_short(15); // 2

   and_reg32_imm32(EBX, 0x7FFFFF); // 6
   xor_reg8_imm8(BL, 3); // 3
   mov_reg32_preg32pimm32(EAX, EBX, (unsigned int)g_rdram); // 6

   and_eax_imm32(0xFF);

   set_register_state(EAX, (unsigned int*)dst->f.i.rt, 1);
#endif
}

void genlhu(void)
{
#ifdef INTERPRET_LHU
   gencallinterp((unsigned int)cached_interpreter_table.LHU, 0);
#else
   free_all_registers();
   simplify_access();
   mov_eax_memoffs32((unsigned int *)dst->f.i.rs);
   add_eax_imm32((int)dst->f.i.immediate);
   mov_reg32_reg32(EBX, EAX);
   if(fast_memory)
   {
      and_eax_imm32(0xDF800000);
      cmp_eax_imm32(0x80000000);
   }
   else
   {
      shr_reg32_imm8(EAX, 16);
      mov_reg32_preg32x4pimm32(EAX, EAX, (unsigned int)readmemh);
      cmp_reg32_imm32(EAX, (unsigned int)read_rdramh);
   }
   je_rj(46);

   mov_m32_imm32((unsigned int *)&PC, (unsigned int)(dst+1)); // 10
   mov_m32_reg32((unsigned int *)(&address), EBX); // 6
   mov_m32_imm32((unsigned int *)(&rdword), (unsigned int)dst->f.i.rt); // 10
   shr_reg32_imm8(EBX, 16); // 3
   mov_reg32_preg32x4pimm32(EBX, EBX, (unsigned int)readmemh); // 7
   call_reg32(EBX); // 2
   mov_reg32_m32(EAX, (unsigned int *)dst->f.i.rt); // 6
   jmp_imm_short(15); // 2

   and_reg32_imm32(EBX, 0x7FFFFF); // 6
   xor_reg8_imm8(BL, 2); // 3
   mov_reg32_preg32pimm32(EAX, EBX, (unsigned int)g_rdram); // 6

   and_eax_imm32(0xFFFF);

   set_register_state(EAX, (unsigned int*)dst->f.i.rt, 1);
#endif
}

void genlwu(void)
{
#ifdef INTERPRET_LWU
   gencallinterp((unsigned int)cached_interpreter_table.LWU, 0);
#else
   free_all_registers();
   simplify_access();
   mov_eax_memoffs32((unsigned int *)dst->f.i.rs);
   add_eax_imm32((int)dst->f.i.immediate);
   mov_reg32_reg32(EBX, EAX);
   if(fast_memory)
   {
      and_eax_imm32(0xDF800000);
      cmp_eax_imm32(0x80000000);
   }
   else
   {
      shr_reg32_imm8(EAX, 16);
      mov_reg32_preg32x4pimm32(EAX, EAX, (unsigned int)readmem);
      cmp_reg32_imm32(EAX, (unsigned int)read_rdram);
   }
   je_rj(45);

   mov_m32_imm32((unsigned int *)(&PC), (unsigned int)(dst+1)); // 10
   mov_m32_reg32((unsigned int *)(&address), EBX); // 6
   mov_m32_imm32((unsigned int *)(&rdword), (unsigned int)dst->f.i.rt); // 10
   shr_reg32_imm8(EBX, 16); // 3
   mov_reg32_preg32x4pimm32(EBX, EBX, (unsigned int)readmem); // 7
   call_reg32(EBX); // 2
   mov_eax_memoffs32((unsigned int *)(dst->f.i.rt)); // 5
   jmp_imm_short(12); // 2

   and_reg32_imm32(EBX, 0x7FFFFF); // 6
   mov_reg32_preg32pimm32(EAX, EBX, (unsigned int)g_rdram); // 6

   xor_reg32_reg32(EBX, EBX);

   set_64_register_state(EAX, EBX, (unsigned int*)dst->f.i.rt, 1);
#endif
}

void gensb(void)
{
#ifdef INTERPRET_SB
   gencallinterp((unsigned int)cached_interpreter_table.SB, 0);
#else
   free_all_registers();
   simplify_access();
   mov_reg8_m8(CL, (unsigned char *)dst->f.i.rt);
   mov_eax_memoffs32((unsigned int *)dst->f.i.rs);
   add_eax_imm32((int)dst->f.i.immediate);
   mov_reg32_reg32(EBX, EAX);
   if(fast_memory)
   {
      and_eax_imm32(0xDF800000);
      cmp_eax_imm32(0x80000000);
   }
   else
   {
      shr_reg32_imm8(EAX, 16);
      mov_reg32_preg32x4pimm32(EAX, EAX, (unsigned int)writememb);
      cmp_reg32_imm32(EAX, (unsigned int)write_rdramb);
   }
   je_rj(41);

   mov_m32_imm32((unsigned int *)(&PC), (unsigned int)(dst+1)); // 10
   mov_m32_reg32((unsigned int *)(&address), EBX); // 6
   mov_m8_reg8((unsigned char *)(&cpu_byte), CL); // 6
   shr_reg32_imm8(EBX, 16); // 3
   mov_reg32_preg32x4pimm32(EBX, EBX, (unsigned int)writememb); // 7
   call_reg32(EBX); // 2
   mov_eax_memoffs32((unsigned int *)(&address)); // 5
   jmp_imm_short(17); // 2

   mov_reg32_reg32(EAX, EBX); // 2
   and_reg32_imm32(EBX, 0x7FFFFF); // 6
   xor_reg8_imm8(BL, 3); // 3
   mov_preg32pimm32_reg8(EBX, (unsigned int)g_rdram, CL); // 6

   mov_reg32_reg32(EBX, EAX);
   shr_reg32_imm8(EBX, 12);
   cmp_preg32pimm32_imm8(EBX, (unsigned int)invalid_code, 0);
   jne_rj(54);
   mov_reg32_reg32(ECX, EBX); // 2
   shl_reg32_imm8(EBX, 2); // 3
   mov_reg32_preg32pimm32(EBX, EBX, (unsigned int)blocks); // 6
   mov_reg32_preg32pimm32(EBX, EBX, (int)&actual->block - (int)actual); // 6
   and_eax_imm32(0xFFF); // 5
   shr_reg32_imm8(EAX, 2); // 3
   mov_reg32_imm32(EDX, sizeof(precomp_instr)); // 5
   mul_reg32(EDX); // 2
   mov_reg32_preg32preg32pimm32(EAX, EAX, EBX, (int)&dst->ops - (int)dst); // 7
   cmp_reg32_imm32(EAX, (unsigned int)cached_interpreter_table.NOTCOMPILED); // 6
   je_rj(7); // 2
   mov_preg32pimm32_imm8(ECX, (unsigned int)invalid_code, 1); // 7
#endif
}

void gensh(void)
{
#ifdef INTERPRET_SH
   gencallinterp((unsigned int)cached_interpreter_table.SH, 0);
#else
   free_all_registers();
   simplify_access();
   mov_reg16_m16(CX, (unsigned short *)dst->f.i.rt);
   mov_eax_memoffs32((unsigned int *)dst->f.i.rs);
   add_eax_imm32((int)dst->f.i.immediate);
   mov_reg32_reg32(EBX, EAX);
   if(fast_memory)
   {
      and_eax_imm32(0xDF800000);
      cmp_eax_imm32(0x80000000);
   }
   else
   {
      shr_reg32_imm8(EAX, 16);
      mov_reg32_preg32x4pimm32(EAX, EAX, (unsigned int)writememh);
      cmp_reg32_imm32(EAX, (unsigned int)write_rdramh);
   }
   je_rj(42);

   mov_m32_imm32((unsigned int *)(&PC), (unsigned int)(dst+1)); // 10
   mov_m32_reg32((unsigned int *)(&address), EBX); // 6
   mov_m16_reg16((unsigned short *)(&hword), CX); // 7
   shr_reg32_imm8(EBX, 16); // 3
   mov_reg32_preg32x4pimm32(EBX, EBX, (unsigned int)writememh); // 7
   call_reg32(EBX); // 2
   mov_eax_memoffs32((unsigned int *)(&address)); // 5
   jmp_imm_short(18); // 2

   mov_reg32_reg32(EAX, EBX); // 2
   and_reg32_imm32(EBX, 0x7FFFFF); // 6
   xor_reg8_imm8(BL, 2); // 3
   mov_preg32pimm32_reg16(EBX, (unsigned int)g_rdram, CX); // 7

   mov_reg32_reg32(EBX, EAX);
   shr_reg32_imm8(EBX, 12);
   cmp_preg32pimm32_imm8(EBX, (unsigned int)invalid_code, 0);
   jne_rj(54);
   mov_reg32_reg32(ECX, EBX); // 2
   shl_reg32_imm8(EBX, 2); // 3
   mov_reg32_preg32pimm32(EBX, EBX, (unsigned int)blocks); // 6
   mov_reg32_preg32pimm32(EBX, EBX, (int)&actual->block - (int)actual); // 6
   and_eax_imm32(0xFFF); // 5
   shr_reg32_imm8(EAX, 2); // 3
   mov_reg32_imm32(EDX, sizeof(precomp_instr)); // 5
   mul_reg32(EDX); // 2
   mov_reg32_preg32preg32pimm32(EAX, EAX, EBX, (int)&dst->ops - (int)dst); // 7
   cmp_reg32_imm32(EAX, (unsigned int)cached_interpreter_table.NOTCOMPILED); // 6
   je_rj(7); // 2
   mov_preg32pimm32_imm8(ECX, (unsigned int)invalid_code, 1); // 7
#endif
}

void gensw(void)
{
#ifdef INTERPRET_SW
   gencallinterp((unsigned int)cached_interpreter_table.SW, 0);
#else
   free_all_registers();
   simplify_access();
   mov_reg32_m32(ECX, (unsigned int *)dst->f.i.rt);
   mov_eax_memoffs32((unsigned int *)dst->f.i.rs);
   add_eax_imm32((int)dst->f.i.immediate);
   mov_reg32_reg32(EBX, EAX);
   if(fast_memory)
   {
      and_eax_imm32(0xDF800000);
      cmp_eax_imm32(0x80000000);
   }
   else
   {
      shr_reg32_imm8(EAX, 16);
      mov_reg32_preg32x4pimm32(EAX, EAX, (unsigned int)writemem);
      cmp_reg32_imm32(EAX, (unsigned int)write_rdram);
   }
   je_rj(41);

   mov_m32_imm32((unsigned int *)(&PC), (unsigned int)(dst+1)); // 10
   mov_m32_reg32((unsigned int *)(&address), EBX); // 6
   mov_m32_reg32((unsigned int *)(&word), ECX); // 6
   shr_reg32_imm8(EBX, 16); // 3
   mov_reg32_preg32x4pimm32(EBX, EBX, (unsigned int)writemem); // 7
   call_reg32(EBX); // 2
   mov_eax_memoffs32((unsigned int *)(&address)); // 5
   jmp_imm_short(14); // 2

   mov_reg32_reg32(EAX, EBX); // 2
   and_reg32_imm32(EBX, 0x7FFFFF); // 6
   mov_preg32pimm32_reg32(EBX, (unsigned int)g_rdram, ECX); // 6

   mov_reg32_reg32(EBX, EAX);
   shr_reg32_imm8(EBX, 12);
   cmp_preg32pimm32_imm8(EBX, (unsigned int)invalid_code, 0);
   jne_rj(54);
   mov_reg32_reg32(ECX, EBX); // 2
   shl_reg32_imm8(EBX, 2); // 3
   mov_reg32_preg32pimm32(EBX, EBX, (unsigned int)blocks); // 6
   mov_reg32_preg32pimm32(EBX, EBX, (int)&actual->block - (int)actual); // 6
   and_eax_imm32(0xFFF); // 5
   shr_reg32_imm8(EAX, 2); // 3
   mov_reg32_imm32(EDX, sizeof(precomp_instr)); // 5
   mul_reg32(EDX); // 2
   mov_reg32_preg32preg32pimm32(EAX, EAX, EBX, (int)&dst->ops - (int)dst); // 7
   cmp_reg32_imm32(EAX, (unsigned int)cached_interpreter_table.NOTCOMPILED); // 6
   je_rj(7); // 2
   mov_preg32pimm32_imm8(ECX, (unsigned int)invalid_code, 1); // 7
#endif
}

void genldc1(void)
{
#ifdef INTERPRET_LDC1
   gencallinterp((unsigned int)cached_interpreter_table.LDC1, 0);
#else
   gencheck_cop1_unusable();

   mov_eax_memoffs32((unsigned int *)(&reg[dst->f.lf.base]));
   add_eax_imm32((int)dst->f.lf.offset);
   mov_reg32_reg32(EBX, EAX);
   if(fast_memory)
   {
      and_eax_imm32(0xDF800000);
      cmp_eax_imm32(0x80000000);
   }
   else
   {
      shr_reg32_imm8(EAX, 16);
      mov_reg32_preg32x4pimm32(EAX, EAX, (unsigned int)readmemd);
      cmp_reg32_imm32(EAX, (unsigned int)read_rdramd);
   }
   je_rj(42);

   mov_m32_imm32((unsigned int *)(&PC), (unsigned int)(dst+1)); // 10
   mov_m32_reg32((unsigned int *)(&address), EBX); // 6
   mov_reg32_m32(EDX, (unsigned int*)(&reg_cop1_double[dst->f.lf.ft])); // 6
   mov_m32_reg32((unsigned int *)(&rdword), EDX); // 6
   shr_reg32_imm8(EBX, 16); // 3
   mov_reg32_preg32x4pimm32(EBX, EBX, (unsigned int)readmemd); // 7
   call_reg32(EBX); // 2
   jmp_imm_short(32); // 2

   and_reg32_imm32(EBX, 0x7FFFFF); // 6
   mov_reg32_preg32pimm32(EAX, EBX, ((unsigned int)g_rdram)+4); // 6
   mov_reg32_preg32pimm32(ECX, EBX, ((unsigned int)g_rdram)); // 6
   mov_reg32_m32(EBX, (unsigned int*)(&reg_cop1_double[dst->f.lf.ft])); // 6
   mov_preg32_reg32(EBX, EAX); // 2
   mov_preg32pimm32_reg32(EBX, 4, ECX); // 6
#endif
}

void gensdc1(void)
{
#ifdef INTERPRET_SDC1
   gencallinterp((unsigned int)cached_interpreter_table.SDC1, 0);
#else
   gencheck_cop1_unusable();

   mov_reg32_m32(ESI, (unsigned int*)(&reg_cop1_double[dst->f.lf.ft]));
   mov_reg32_preg32(ECX, ESI);
   mov_reg32_preg32pimm32(EDX, ESI, 4);
   mov_eax_memoffs32((unsigned int *)(&reg[dst->f.lf.base]));
   add_eax_imm32((int)dst->f.lf.offset);
   mov_reg32_reg32(EBX, EAX);
   if(fast_memory)
   {
      and_eax_imm32(0xDF800000);
      cmp_eax_imm32(0x80000000);
   }
   else
   {
      shr_reg32_imm8(EAX, 16);
      mov_reg32_preg32x4pimm32(EAX, EAX, (unsigned int)writememd);
      cmp_reg32_imm32(EAX, (unsigned int)write_rdramd);
   }
   je_rj(47);

   mov_m32_imm32((unsigned int *)(&PC), (unsigned int)(dst+1)); // 10
   mov_m32_reg32((unsigned int *)(&address), EBX); // 6
   mov_m32_reg32((unsigned int *)(&dword), ECX); // 6
   mov_m32_reg32((unsigned int *)(&dword)+1, EDX); // 6
   shr_reg32_imm8(EBX, 16); // 3
   mov_reg32_preg32x4pimm32(EBX, EBX, (unsigned int)writememd); // 7
   call_reg32(EBX); // 2
   mov_eax_memoffs32((unsigned int *)(&address)); // 5
   jmp_imm_short(20); // 2

   mov_reg32_reg32(EAX, EBX); // 2
   and_reg32_imm32(EBX, 0x7FFFFF); // 6
   mov_preg32pimm32_reg32(EBX, ((unsigned int)g_rdram)+4, ECX); // 6
   mov_preg32pimm32_reg32(EBX, ((unsigned int)g_rdram)+0, EDX); // 6

   mov_reg32_reg32(EBX, EAX);
   shr_reg32_imm8(EBX, 12);
   cmp_preg32pimm32_imm8(EBX, (unsigned int)invalid_code, 0);
   jne_rj(54);
   mov_reg32_reg32(ECX, EBX); // 2
   shl_reg32_imm8(EBX, 2); // 3
   mov_reg32_preg32pimm32(EBX, EBX, (unsigned int)blocks); // 6
   mov_reg32_preg32pimm32(EBX, EBX, (int)&actual->block - (int)actual); // 6
   and_eax_imm32(0xFFF); // 5
   shr_reg32_imm8(EAX, 2); // 3
   mov_reg32_imm32(EDX, sizeof(precomp_instr)); // 5
   mul_reg32(EDX); // 2
   mov_reg32_preg32preg32pimm32(EAX, EAX, EBX, (int)&dst->ops - (int)dst); // 7
   cmp_reg32_imm32(EAX, (unsigned int)cached_interpreter_table.NOTCOMPILED); // 6
   je_rj(7); // 2
   mov_preg32pimm32_imm8(ECX, (unsigned int)invalid_code, 1); // 7
#endif
}
