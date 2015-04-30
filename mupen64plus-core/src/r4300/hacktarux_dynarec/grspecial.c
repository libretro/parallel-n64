/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - gspecial.c                                              *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
 *   Copyright (C) 2007 Richard Goedeken (Richard42)                       *
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

#include <stdint.h>
#include <stdio.h>

#include "assemble.h"
#include "interpret.h"

#include "r4300/cached_interp.h"
#include "r4300/recomph.h"
#include "r4300/recomp.h"
#include "r4300/r4300.h"
#include "r4300/ops.h"
#include "r4300/cp0_private.h"
#include "r4300/exception.h"

#if !defined(offsetof)
#   define offsetof(TYPE,MEMBER) ((unsigned int) &((TYPE*)0)->MEMBER)
#endif

void gensll(void)
{
#ifdef INTERPRET_SLL
   gencallinterp((native_type)cached_interpreter_table.SLL, 0);
#else
#ifdef __x86_64__
   int rt = allocate_register_32((unsigned int *)dst->f.r.rt);
   int rd = allocate_register_32_w((unsigned int *)dst->f.r.rd);
#else
   int rt = allocate_register((unsigned int *)dst->f.r.rt);
   int rd = allocate_register_w((unsigned int *)dst->f.r.rd);
#endif

   mov_reg32_reg32(rd, rt);
   shl_reg32_imm8(rd, dst->f.r.sa);
#endif
}

void gensrl(void)
{
#ifdef INTERPRET_SRL
   gencallinterp((native_type)cached_interpreter_table.SRL, 0);
#else
#ifdef __x86_64__
   int rt = allocate_register_32((unsigned int *)dst->f.r.rt);
   int rd = allocate_register_32_w((unsigned int *)dst->f.r.rd);
#else
   int rt = allocate_register((unsigned int *)dst->f.r.rt);
   int rd = allocate_register_w((unsigned int *)dst->f.r.rd);
#endif

   mov_reg32_reg32(rd, rt);
   shr_reg32_imm8(rd, dst->f.r.sa);
#endif
}

void gensra(void)
{
#ifdef INTERPRET_SRA
   gencallinterp((native_type)cached_interpreter_table.SRA, 0);
#else
#ifdef __x86_64__
   int rt = allocate_register_32((unsigned int *)dst->f.r.rt);
   int rd = allocate_register_32_w((unsigned int *)dst->f.r.rd);
#else
   int rt = allocate_register((unsigned int *)dst->f.r.rt);
   int rd = allocate_register_w((unsigned int *)dst->f.r.rd);
#endif

   mov_reg32_reg32(rd, rt);
   sar_reg32_imm8(rd, dst->f.r.sa);
#endif
}

void gensllv(void)
{
#ifdef INTERPRET_SLLV
   gencallinterp((native_type)cached_interpreter_table.SLLV, 0);
#else
   int rt, rd;
#ifdef __x86_64__
   allocate_register_32_manually(ECX, (unsigned int *)dst->f.r.rs);

   rt = allocate_register_32((unsigned int *)dst->f.r.rt);
   rd = allocate_register_32_w((unsigned int *)dst->f.r.rd);
#else
   allocate_register_manually(ECX, (unsigned int *)dst->f.r.rs);

   rt = allocate_register((unsigned int *)dst->f.r.rt);
   rd = allocate_register_w((unsigned int *)dst->f.r.rd);
#endif

   if (rd != ECX)
   {
      mov_reg32_reg32(rd, rt);
      shl_reg32_cl(rd);
   }
   else
   {
      int temp = lru_register();
      free_register(temp);
      mov_reg32_reg32(temp, rt);
      shl_reg32_cl(temp);
      mov_reg32_reg32(rd, temp);
   }
#endif
}

void gensrlv(void)
{
#ifdef INTERPRET_SRLV
   gencallinterp((native_type)cached_interpreter_table.SRLV, 0);
#else
   int rt, rd;
#ifdef __x86_64__
   allocate_register_32_manually(ECX, (unsigned int *)dst->f.r.rs);

   rt = allocate_register_32((unsigned int *)dst->f.r.rt);
   rd = allocate_register_32_w((unsigned int *)dst->f.r.rd);
#else
   allocate_register_manually(ECX, (unsigned int *)dst->f.r.rs);

   rt = allocate_register((unsigned int *)dst->f.r.rt);
   rd = allocate_register_w((unsigned int *)dst->f.r.rd);
#endif

   if (rd != ECX)
   {
      mov_reg32_reg32(rd, rt);
      shr_reg32_cl(rd);
   }
   else
   {
      int temp = lru_register();
      free_register(temp);
      mov_reg32_reg32(temp, rt);
      shr_reg32_cl(temp);
      mov_reg32_reg32(rd, temp);
   }
#endif
}

void gensrav(void)
{
#ifdef INTERPRET_SRAV
   gencallinterp((native_type)cached_interpreter_table.SRAV, 0);
#else
   int rt, rd;
#ifdef __x86_64__
   allocate_register_32_manually(ECX, (unsigned int *)dst->f.r.rs);

   rt = allocate_register_32((unsigned int *)dst->f.r.rt);
   rd = allocate_register_32_w((unsigned int *)dst->f.r.rd);
#else
   allocate_register_manually(ECX, (unsigned int *)dst->f.r.rs);

   rt = allocate_register((unsigned int *)dst->f.r.rt);
   rd = allocate_register_w((unsigned int *)dst->f.r.rd);
#endif

   if (rd != ECX)
   {
      mov_reg32_reg32(rd, rt);
      sar_reg32_cl(rd);
   }
   else
   {
      int temp = lru_register();
      free_register(temp);
      mov_reg32_reg32(temp, rt);
      sar_reg32_cl(temp);
      mov_reg32_reg32(rd, temp);
   }
#endif
}

void genjr(void)
{
#ifdef INTERPRET_JR
   gencallinterp((native_type)cached_interpreter_table.JR, 1);
#else
#ifdef __x86_64__
   static unsigned int precomp_instr_size = sizeof(precomp_instr);
   unsigned int diff = (unsigned int) offsetof(precomp_instr, local_addr);
   unsigned int diff_need = (unsigned int) offsetof(precomp_instr, reg_cache_infos.need_map);
   unsigned int diff_wrap = (unsigned int) offsetof(precomp_instr, reg_cache_infos.jump_wrapper);

   if (((dst->addr & 0xFFF) == 0xFFC && 
            (dst->addr < 0x80000000 || dst->addr >= 0xC0000000))||no_compiled_jump)
   {
      gencallinterp((native_type)cached_interpreter_table.JR, 1);
      return;
   }

   free_registers_move_start();

   mov_xreg32_m32rel(EAX, (unsigned int *)dst->f.i.rs);
   mov_m32rel_xreg32((unsigned int *)&local_rs, EAX);

   gendelayslot();

   mov_xreg32_m32rel(EAX, (unsigned int *)&local_rs);
   mov_m32rel_xreg32((unsigned int *)&last_addr, EAX);

   gencheck_interupt_reg();

   mov_xreg32_m32rel(EAX, (unsigned int *)&local_rs);
   mov_reg32_reg32(EBX, EAX);
   and_eax_imm32(0xFFFFF000);
   cmp_eax_imm32(dst_block->start & 0xFFFFF000);
   je_near_rj(0);

   jump_start_rel32();

   mov_m32rel_xreg32(&jump_to_address, EBX);
   mov_reg64_imm64(RAX, (unsigned long long) (dst+1));
   mov_m64rel_xreg64((unsigned long long *)(&PC), RAX);
   mov_reg64_imm64(RAX, (unsigned long long) jump_to_func);
   call_reg64(RAX);  /* will never return from call */

   jump_end_rel32();

   mov_reg64_imm64(RSI, (unsigned long long) dst_block->block);
   mov_reg32_reg32(EAX, EBX);
   sub_eax_imm32(dst_block->start);
   shr_reg32_imm8(EAX, 2);
   mul_m32rel((unsigned int *)(&precomp_instr_size));

   mov_reg32_preg64preg64pimm32(EBX, RAX, RSI, diff_need);
   cmp_reg32_imm32(EBX, 1);
   jne_rj(11);

   add_reg32_imm32(EAX, diff_wrap); // 6
   add_reg64_reg64(RAX, RSI); // 3
   jmp_reg64(RAX); // 2

   mov_reg32_preg64preg64pimm32(EBX, RAX, RSI, diff);
   mov_rax_memoffs64((unsigned long long *) &dst_block->code);
   add_reg64_reg64(RAX, RBX);
   jmp_reg64(RAX);
#else
   static unsigned int precomp_instr_size = sizeof(precomp_instr);
   unsigned int diff =
      (unsigned int)(&dst->local_addr) - (unsigned int)(dst);
   unsigned int diff_need =
      (unsigned int)(&dst->reg_cache_infos.need_map) - (unsigned int)(dst);
   unsigned int diff_wrap =
      (unsigned int)(&dst->reg_cache_infos.jump_wrapper) - (unsigned int)(dst);

   if (((dst->addr & 0xFFF) == 0xFFC && 
            (dst->addr < 0x80000000 || dst->addr >= 0xC0000000))||no_compiled_jump)
   {
      gencallinterp((unsigned int)cached_interpreter_table.JR, 1);
      return;
   }

   free_all_registers();
   simplify_access();
   mov_eax_memoffs32((unsigned int *)dst->f.i.rs);
   mov_memoffs32_eax((unsigned int *)&local_rs);

   gendelayslot();

   mov_eax_memoffs32((unsigned int *)&local_rs);
   mov_memoffs32_eax((unsigned int *)&last_addr);

   gencheck_interupt_reg();

   mov_eax_memoffs32((unsigned int *)&local_rs);
   mov_reg32_reg32(EBX, EAX);
   and_eax_imm32(0xFFFFF000);
   cmp_eax_imm32(dst_block->start & 0xFFFFF000);
   je_near_rj(0);

   jump_start_rel32();

   mov_m32_reg32(&jump_to_address, EBX);
   mov_m32_imm32((unsigned int*)(&PC), (unsigned int)(dst+1));
   mov_reg32_imm32(EAX, (unsigned int)jump_to_func);
   call_reg32(EAX);

   jump_end_rel32();

   mov_reg32_reg32(EAX, EBX);
   sub_eax_imm32(dst_block->start);
   shr_reg32_imm8(EAX, 2);
   mul_m32((unsigned int *)(&precomp_instr_size));

   mov_reg32_preg32pimm32(EBX, EAX, (unsigned int)(dst_block->block)+diff_need);
   cmp_reg32_imm32(EBX, 1);
   jne_rj(7);

   add_eax_imm32((unsigned int)(dst_block->block)+diff_wrap); // 5
   jmp_reg32(EAX); // 2

   mov_reg32_preg32pimm32(EAX, EAX, (unsigned int)(dst_block->block)+diff);
   add_reg32_m32(EAX, (unsigned int *)(&dst_block->code));

   jmp_reg32(EAX);
#endif
#endif
}

void genjalr(void)
{
#ifdef INTERPRET_JALR
   gencallinterp((native_type)cached_interpreter_table.JALR, 0);
#else
#ifdef __x86_64__
   static unsigned int precomp_instr_size = sizeof(precomp_instr);
   unsigned int diff = (unsigned int) offsetof(precomp_instr, local_addr);
   unsigned int diff_need = (unsigned int) offsetof(precomp_instr, reg_cache_infos.need_map);
   unsigned int diff_wrap = (unsigned int) offsetof(precomp_instr, reg_cache_infos.jump_wrapper);

   if (((dst->addr & 0xFFF) == 0xFFC && 
            (dst->addr < 0x80000000 || dst->addr >= 0xC0000000))||no_compiled_jump)
   {
      gencallinterp((native_type)cached_interpreter_table.JALR, 1);
      return;
   }

   free_registers_move_start();

   mov_xreg32_m32rel(EAX, (unsigned int *)dst->f.r.rs);
   mov_m32rel_xreg32((unsigned int *)&local_rs, EAX);

   gendelayslot();

   mov_m32rel_imm32((unsigned int *)(dst-1)->f.r.rd, dst->addr+4);
   if ((dst->addr+4) & 0x80000000)
      mov_m32rel_imm32(((unsigned int *)(dst-1)->f.r.rd)+1, 0xFFFFFFFF);
   else
      mov_m32rel_imm32(((unsigned int *)(dst-1)->f.r.rd)+1, 0);

   mov_xreg32_m32rel(EAX, (unsigned int *)&local_rs);
   mov_m32rel_xreg32((unsigned int *)&last_addr, EAX);

   gencheck_interupt_reg();

   mov_xreg32_m32rel(EAX, (unsigned int *)&local_rs);
   mov_reg32_reg32(EBX, EAX);
   and_eax_imm32(0xFFFFF000);
   cmp_eax_imm32(dst_block->start & 0xFFFFF000);
   je_near_rj(0);

   jump_start_rel32();

   mov_m32rel_xreg32(&jump_to_address, EBX);
   mov_reg64_imm64(RAX, (unsigned long long) (dst+1));
   mov_m64rel_xreg64((unsigned long long *)(&PC), RAX);
   mov_reg64_imm64(RAX, (unsigned long long) jump_to_func);
   call_reg64(RAX);  /* will never return from call */

   jump_end_rel32();

   mov_reg64_imm64(RSI, (unsigned long long) dst_block->block);
   mov_reg32_reg32(EAX, EBX);
   sub_eax_imm32(dst_block->start);
   shr_reg32_imm8(EAX, 2);
   mul_m32rel((unsigned int *)(&precomp_instr_size));

   mov_reg32_preg64preg64pimm32(EBX, RAX, RSI, diff_need);
   cmp_reg32_imm32(EBX, 1);
   jne_rj(11);

   add_reg32_imm32(EAX, diff_wrap); // 6
   add_reg64_reg64(RAX, RSI); // 3
   jmp_reg64(RAX); // 2

   mov_reg32_preg64preg64pimm32(EBX, RAX, RSI, diff);
   mov_rax_memoffs64((unsigned long long *) &dst_block->code);
   add_reg64_reg64(RAX, RBX);
   jmp_reg64(RAX);
#else
   static unsigned int precomp_instr_size = sizeof(precomp_instr);
   unsigned int diff =
      (unsigned int)(&dst->local_addr) - (unsigned int)(dst);
   unsigned int diff_need =
      (unsigned int)(&dst->reg_cache_infos.need_map) - (unsigned int)(dst);
   unsigned int diff_wrap =
      (unsigned int)(&dst->reg_cache_infos.jump_wrapper) - (unsigned int)(dst);

   if (((dst->addr & 0xFFF) == 0xFFC && 
            (dst->addr < 0x80000000 || dst->addr >= 0xC0000000))||no_compiled_jump)
   {
      gencallinterp((unsigned int)cached_interpreter_table.JALR, 1);
      return;
   }

   free_all_registers();
   simplify_access();
   mov_eax_memoffs32((unsigned int *)dst->f.r.rs);
   mov_memoffs32_eax((unsigned int *)&local_rs);

   gendelayslot();

   mov_m32_imm32((unsigned int *)(dst-1)->f.r.rd, dst->addr+4);
   if ((dst->addr+4) & 0x80000000)
      mov_m32_imm32(((unsigned int *)(dst-1)->f.r.rd)+1, 0xFFFFFFFF);
   else
      mov_m32_imm32(((unsigned int *)(dst-1)->f.r.rd)+1, 0);

   mov_eax_memoffs32((unsigned int *)&local_rs);
   mov_memoffs32_eax((unsigned int *)&last_addr);

   gencheck_interupt_reg();

   mov_eax_memoffs32((unsigned int *)&local_rs);
   mov_reg32_reg32(EBX, EAX);
   and_eax_imm32(0xFFFFF000);
   cmp_eax_imm32(dst_block->start & 0xFFFFF000);
   je_near_rj(0);

   jump_start_rel32();

   mov_m32_reg32(&jump_to_address, EBX);
   mov_m32_imm32((unsigned int*)(&PC), (unsigned int)(dst+1));
   mov_reg32_imm32(EAX, (unsigned int)jump_to_func);
   call_reg32(EAX);

   jump_end_rel32();

   mov_reg32_reg32(EAX, EBX);
   sub_eax_imm32(dst_block->start);
   shr_reg32_imm8(EAX, 2);
   mul_m32((unsigned int *)(&precomp_instr_size));

   mov_reg32_preg32pimm32(EBX, EAX, (unsigned int)(dst_block->block)+diff_need);
   cmp_reg32_imm32(EBX, 1);
   jne_rj(7);

   add_eax_imm32((unsigned int)(dst_block->block)+diff_wrap); // 5
   jmp_reg32(EAX); // 2

   mov_reg32_preg32pimm32(EAX, EAX, (unsigned int)(dst_block->block)+diff);
   add_reg32_m32(EAX, (unsigned int *)(&dst_block->code));

   jmp_reg32(EAX);
#endif
#endif
}

void gensyscall(void)
{
#ifdef INTERPRET_SYSCALL
   gencallinterp((native_type)cached_interpreter_table.SYSCALL, 0);
#else
#ifdef __x86_64__
   free_registers_move_start();

   mov_m32rel_imm32(&g_cp0_regs[CP0_CAUSE_REG], 8 << 2);
#else
   free_all_registers();
   simplify_access();
   mov_m32_imm32(&g_cp0_regs[CP0_CAUSE_REG], 8 << 2);
#endif
   gencallinterp((native_type)exception_general, 0);
#endif
}

void gensync(void)
{
}

void genmfhi(void)
{
#ifdef INTERPRET_MFHI
   gencallinterp((native_type)cached_interpreter_table.MFHI, 0);
#else
#ifdef __x86_64__
   int rd = allocate_register_64_w((unsigned long long *) dst->f.r.rd);
   int _hi = allocate_register_64((unsigned long long *) &hi);

   mov_reg64_reg64(rd, _hi);
#else
   int rd1 = allocate_64_register1_w((unsigned int*)dst->f.r.rd);
   int rd2 = allocate_64_register2_w((unsigned int*)dst->f.r.rd);
   int hi1 = allocate_64_register1((unsigned int*)&hi);
   int hi2 = allocate_64_register2((unsigned int*)&hi);

   mov_reg32_reg32(rd1, hi1);
   mov_reg32_reg32(rd2, hi2);
#endif
#endif
}

void genmthi(void)
{
#ifdef INTERPRET_MTHI
   gencallinterp((native_type)cached_interpreter_table.MTHI, 0);
#else
#ifdef __x86_64__
   int _hi = allocate_register_64_w((unsigned long long *) &hi);
   int rs = allocate_register_64((unsigned long long *) dst->f.r.rs);

   mov_reg64_reg64(_hi, rs);
#else
   int hi1 = allocate_64_register1_w((unsigned int*)&hi);
   int hi2 = allocate_64_register2_w((unsigned int*)&hi);
   int rs1 = allocate_64_register1((unsigned int*)dst->f.r.rs);
   int rs2 = allocate_64_register2((unsigned int*)dst->f.r.rs);

   mov_reg32_reg32(hi1, rs1);
   mov_reg32_reg32(hi2, rs2);
#endif
#endif
}

void genmflo(void)
{
#ifdef INTERPRET_MFLO
   gencallinterp((native_type)cached_interpreter_table.MFLO, 0);
#else
#ifdef __x86_64__
   int rd = allocate_register_64_w((unsigned long long *) dst->f.r.rd);
   int _lo = allocate_register_64((unsigned long long *) &lo);

   mov_reg64_reg64(rd, _lo);
#else
   int rd1 = allocate_64_register1_w((unsigned int*)dst->f.r.rd);
   int rd2 = allocate_64_register2_w((unsigned int*)dst->f.r.rd);
   int lo1 = allocate_64_register1((unsigned int*)&lo);
   int lo2 = allocate_64_register2((unsigned int*)&lo);

   mov_reg32_reg32(rd1, lo1);
   mov_reg32_reg32(rd2, lo2);
#endif
#endif
}

void genmtlo(void)
{
#ifdef INTERPRET_MTLO
   gencallinterp((native_type)cached_interpreter_table.MTLO, 0);
#else
#ifdef __x86_64__
   int _lo = allocate_register_64_w((unsigned long long *)&lo);
   int rs = allocate_register_64((unsigned long long *)dst->f.r.rs);

   mov_reg64_reg64(_lo, rs);
#else
   int lo1 = allocate_64_register1_w((unsigned int*)&lo);
   int lo2 = allocate_64_register2_w((unsigned int*)&lo);
   int rs1 = allocate_64_register1((unsigned int*)dst->f.r.rs);
   int rs2 = allocate_64_register2((unsigned int*)dst->f.r.rs);

   mov_reg32_reg32(lo1, rs1);
   mov_reg32_reg32(lo2, rs2);
#endif
#endif
}

void gendsllv(void)
{
#ifdef INTERPRET_DSLLV
   gencallinterp((native_type)cached_interpreter_table.DSLLV, 0);
#else
#ifdef __x86_64__
   int rt, rd;
   allocate_register_32_manually(ECX, (unsigned int *)dst->f.r.rs);

   rt = allocate_register_64((unsigned long long *)dst->f.r.rt);
   rd = allocate_register_64_w((unsigned long long *)dst->f.r.rd);

   if (rd != ECX)
   {
      mov_reg64_reg64(rd, rt);
      shl_reg64_cl(rd);
   }
   else
   {
      int temp;
      temp = lru_register();
      free_register(temp);

      mov_reg64_reg64(temp, rt);
      shl_reg64_cl(temp);
      mov_reg64_reg64(rd, temp);
   }
#else
   int rt1, rt2, rd1, rd2;
   allocate_register_manually(ECX, (unsigned int *)dst->f.r.rs);

   rt1 = allocate_64_register1((unsigned int *)dst->f.r.rt);
   rt2 = allocate_64_register2((unsigned int *)dst->f.r.rt);
   rd1 = allocate_64_register1_w((unsigned int *)dst->f.r.rd);
   rd2 = allocate_64_register2_w((unsigned int *)dst->f.r.rd);

   if (rd1 != ECX && rd2 != ECX)
   {
      mov_reg32_reg32(rd1, rt1);
      mov_reg32_reg32(rd2, rt2);
      shld_reg32_reg32_cl(rd2,rd1);
      shl_reg32_cl(rd1);
      test_reg32_imm32(ECX, 0x20);
      je_rj(4);
      mov_reg32_reg32(rd2, rd1); // 2
      xor_reg32_reg32(rd1, rd1); // 2
   }
   else
   {
      int temp1, temp2;
      force_32(ECX);
      temp1 = lru_register();
      temp2 = lru_register_exc1(temp1);
      free_register(temp1);
      free_register(temp2);

      mov_reg32_reg32(temp1, rt1);
      mov_reg32_reg32(temp2, rt2);
      shld_reg32_reg32_cl(temp2, temp1);
      shl_reg32_cl(temp1);
      test_reg32_imm32(ECX, 0x20);
      je_rj(4);
      mov_reg32_reg32(temp2, temp1); // 2
      xor_reg32_reg32(temp1, temp1); // 2

      mov_reg32_reg32(rd1, temp1);
      mov_reg32_reg32(rd2, temp2);
   }
#endif
#endif
}

void gendsrlv(void)
{
#ifdef INTERPRET_DSRLV
   gencallinterp((native_type)cached_interpreter_table.DSRLV, 0);
#else
#ifdef __x86_64__
   int rt, rd;
   allocate_register_32_manually(ECX, (unsigned int *)dst->f.r.rs);

   rt = allocate_register_64((unsigned long long *)dst->f.r.rt);
   rd = allocate_register_64_w((unsigned long long *)dst->f.r.rd);

   if (rd != ECX)
   {
      mov_reg64_reg64(rd, rt);
      shr_reg64_cl(rd);
   }
   else
   {
      int temp;
      temp = lru_register();
      free_register(temp);

      mov_reg64_reg64(temp, rt);
      shr_reg64_cl(temp);
      mov_reg64_reg64(rd, temp);
   }
#else
   int rt1, rt2, rd1, rd2;
   allocate_register_manually(ECX, (unsigned int *)dst->f.r.rs);

   rt1 = allocate_64_register1((unsigned int *)dst->f.r.rt);
   rt2 = allocate_64_register2((unsigned int *)dst->f.r.rt);
   rd1 = allocate_64_register1_w((unsigned int *)dst->f.r.rd);
   rd2 = allocate_64_register2_w((unsigned int *)dst->f.r.rd);

   if (rd1 != ECX && rd2 != ECX)
   {
      mov_reg32_reg32(rd1, rt1);
      mov_reg32_reg32(rd2, rt2);
      shrd_reg32_reg32_cl(rd1,rd2);
      shr_reg32_cl(rd2);
      test_reg32_imm32(ECX, 0x20);
      je_rj(4);
      mov_reg32_reg32(rd1, rd2); // 2
      xor_reg32_reg32(rd2, rd2); // 2
   }
   else
   {
      int temp1, temp2;
      force_32(ECX);
      temp1 = lru_register();
      temp2 = lru_register_exc1(temp1);
      free_register(temp1);
      free_register(temp2);

      mov_reg32_reg32(temp1, rt1);
      mov_reg32_reg32(temp2, rt2);
      shrd_reg32_reg32_cl(temp1, temp2);
      shr_reg32_cl(temp2);
      test_reg32_imm32(ECX, 0x20);
      je_rj(4);
      mov_reg32_reg32(temp1, temp2); // 2
      xor_reg32_reg32(temp2, temp2); // 2

      mov_reg32_reg32(rd1, temp1);
      mov_reg32_reg32(rd2, temp2);
   }
#endif
#endif
}

void gendsrav(void)
{
#ifdef INTERPRET_DSRAV
   gencallinterp((native_type)cached_interpreter_table.DSRAV, 0);
#else
#ifdef __x86_64__
   int rt, rd;
   allocate_register_32_manually(ECX, (unsigned int *)dst->f.r.rs);

   rt = allocate_register_64((unsigned long long *)dst->f.r.rt);
   rd = allocate_register_64_w((unsigned long long *)dst->f.r.rd);

   if (rd != ECX)
   {
      mov_reg64_reg64(rd, rt);
      sar_reg64_cl(rd);
   }
   else
   {
      int temp;
      temp = lru_register();
      free_register(temp);

      mov_reg64_reg64(temp, rt);
      sar_reg64_cl(temp);
      mov_reg64_reg64(rd, temp);
   }
#else
   int rt1, rt2, rd1, rd2;
   allocate_register_manually(ECX, (unsigned int *)dst->f.r.rs);

   rt1 = allocate_64_register1((unsigned int *)dst->f.r.rt);
   rt2 = allocate_64_register2((unsigned int *)dst->f.r.rt);
   rd1 = allocate_64_register1_w((unsigned int *)dst->f.r.rd);
   rd2 = allocate_64_register2_w((unsigned int *)dst->f.r.rd);

   if (rd1 != ECX && rd2 != ECX)
   {
      mov_reg32_reg32(rd1, rt1);
      mov_reg32_reg32(rd2, rt2);
      shrd_reg32_reg32_cl(rd1,rd2);
      sar_reg32_cl(rd2);
      test_reg32_imm32(ECX, 0x20);
      je_rj(5);
      mov_reg32_reg32(rd1, rd2); // 2
      sar_reg32_imm8(rd2, 31); // 3
   }
   else
   {
      int temp1, temp2;
      force_32(ECX);
      temp1 = lru_register();
      temp2 = lru_register_exc1(temp1);
      free_register(temp1);
      free_register(temp2);

      mov_reg32_reg32(temp1, rt1);
      mov_reg32_reg32(temp2, rt2);
      shrd_reg32_reg32_cl(temp1, temp2);
      sar_reg32_cl(temp2);
      test_reg32_imm32(ECX, 0x20);
      je_rj(5);
      mov_reg32_reg32(temp1, temp2); // 2
      sar_reg32_imm8(temp2, 31); // 3

      mov_reg32_reg32(rd1, temp1);
      mov_reg32_reg32(rd2, temp2);
   }
#endif
#endif
}

void genmult(void)
{
#ifdef INTERPRET_MULT
   gencallinterp((native_type)cached_interpreter_table.MULT, 0);
#else
   int rs, rt;
#ifdef __x86_64__
   allocate_register_32_manually_w(EAX, (unsigned int *)&lo); /* these must be done first so they are not assigned by allocate_register() */
   allocate_register_32_manually_w(EDX, (unsigned int *)&hi);
   rs = allocate_register_32((unsigned int*)dst->f.r.rs);
   rt = allocate_register_32((unsigned int*)dst->f.r.rt);
#else
   allocate_register_manually_w(EAX, (unsigned int *)&lo, 0);
   allocate_register_manually_w(EDX, (unsigned int *)&hi, 0);
   rs = allocate_register((unsigned int*)dst->f.r.rs);
   rt = allocate_register((unsigned int*)dst->f.r.rt);
#endif
   mov_reg32_reg32(EAX, rs);
   imul_reg32(rt);
#endif
}

void genmultu(void)
{
#ifdef INTERPRET_MULTU
   gencallinterp((native_type)cached_interpreter_table.MULTU, 0);
#else
   int rs, rt;
#ifdef __x86_64__
   allocate_register_32_manually_w(EAX, (unsigned int *)&lo);
   allocate_register_32_manually_w(EDX, (unsigned int *)&hi);
   rs = allocate_register_32((unsigned int*)dst->f.r.rs);
   rt = allocate_register_32((unsigned int*)dst->f.r.rt);
#else
   allocate_register_manually_w(EAX, (unsigned int *)&lo, 0);
   allocate_register_manually_w(EDX, (unsigned int *)&hi, 0);
   rs = allocate_register((unsigned int*)dst->f.r.rs);
   rt = allocate_register((unsigned int*)dst->f.r.rt);
#endif
   mov_reg32_reg32(EAX, rs);
   mul_reg32(rt);
#endif
}

void gendiv(void)
{
#ifdef INTERPRET_DIV
   gencallinterp((native_type)cached_interpreter_table.DIV, 0);
#else
   int rs, rt;
#ifdef __x86_64__
   allocate_register_32_manually_w(EAX, (unsigned int *)&lo);
   allocate_register_32_manually_w(EDX, (unsigned int *)&hi);
   rs = allocate_register_32((unsigned int*)dst->f.r.rs);
   rt = allocate_register_32((unsigned int*)dst->f.r.rt);
#else
   allocate_register_manually_w(EAX, (unsigned int *)&lo, 0);
   allocate_register_manually_w(EDX, (unsigned int *)&hi, 0);
   rs = allocate_register((unsigned int*)dst->f.r.rs);
   rt = allocate_register((unsigned int*)dst->f.r.rt);
#endif
   cmp_reg32_imm32(rt, 0);
   je_rj((rs == EAX ? 0 : 2) + 1 + 2);
   mov_reg32_reg32(EAX, rs); // 0 or 2
   cdq(); // 1
   idiv_reg32(rt); // 2
#endif
}

void gendivu(void)
{
#ifdef INTERPRET_DIVU
   gencallinterp((native_type)cached_interpreter_table.DIVU, 0);
#else
   int rs, rt;
#ifdef __x86_64__
   allocate_register_32_manually_w(EAX, (unsigned int *)&lo);
   allocate_register_32_manually_w(EDX, (unsigned int *)&hi);
   rs = allocate_register_32((unsigned int*)dst->f.r.rs);
   rt = allocate_register_32((unsigned int*)dst->f.r.rt);
#else
   allocate_register_manually_w(EAX, (unsigned int *)&lo, 0);
   allocate_register_manually_w(EDX, (unsigned int *)&hi, 0);
   rs = allocate_register((unsigned int*)dst->f.r.rs);
   rt = allocate_register((unsigned int*)dst->f.r.rt);
#endif
   cmp_reg32_imm32(rt, 0);
   je_rj((rs == EAX ? 0 : 2) + 2 + 2);
   mov_reg32_reg32(EAX, rs); // 0 or 2
   xor_reg32_reg32(EDX, EDX); // 2
   div_reg32(rt); // 2
#endif
}

void gendmult(void)
{
   gencallinterp((native_type)cached_interpreter_table.DMULT, 0);
}

void gendmultu(void)
{
#ifdef INTERPRET_DMULTU
   gencallinterp((native_type)cached_interpreter_table.DMULTU, 0);
#else
#ifdef __x86_64__
   free_registers_move_start();

   mov_xreg64_m64rel(RAX, (unsigned long long *) dst->f.r.rs);
   mov_xreg64_m64rel(RDX, (unsigned long long *) dst->f.r.rt);
   mul_reg64(RDX);
   mov_m64rel_xreg64((unsigned long long *) &lo, RAX);
   mov_m64rel_xreg64((unsigned long long *) &hi, RDX);
#else
   free_all_registers();
   simplify_access();

   mov_eax_memoffs32((unsigned int *)dst->f.r.rs);
   mul_m32((unsigned int *)dst->f.r.rt); // EDX:EAX = temp1
   mov_memoffs32_eax((unsigned int *)(&lo));

   mov_reg32_reg32(EBX, EDX); // EBX = temp1>>32
   mov_eax_memoffs32((unsigned int *)dst->f.r.rs);
   mul_m32((unsigned int *)(dst->f.r.rt)+1);
   add_reg32_reg32(EBX, EAX);
   adc_reg32_imm32(EDX, 0);
   mov_reg32_reg32(ECX, EDX); // ECX:EBX = temp2

   mov_eax_memoffs32((unsigned int *)(dst->f.r.rs)+1);
   mul_m32((unsigned int *)dst->f.r.rt); // EDX:EAX = temp3

   add_reg32_reg32(EBX, EAX);
   adc_reg32_imm32(ECX, 0); // ECX:EBX = result2
   mov_m32_reg32((unsigned int*)(&lo)+1, EBX);

   mov_reg32_reg32(ESI, EDX); // ESI = temp3>>32
   mov_eax_memoffs32((unsigned int *)(dst->f.r.rs)+1);
   mul_m32((unsigned int *)(dst->f.r.rt)+1);
   add_reg32_reg32(EAX, ESI);
   adc_reg32_imm32(EDX, 0); // EDX:EAX = temp4

   add_reg32_reg32(EAX, ECX);
   adc_reg32_imm32(EDX, 0); // EDX:EAX = result3
   mov_memoffs32_eax((unsigned int *)(&hi));
   mov_m32_reg32((unsigned int *)(&hi)+1, EDX);
#endif
#endif
}

void genddiv(void)
{
   gencallinterp((native_type)cached_interpreter_table.DDIV, 0);
}

void genddivu(void)
{
   gencallinterp((native_type)cached_interpreter_table.DDIVU, 0);
}

void genadd(void)
{
#ifdef INTERPRET_ADD
   gencallinterp((native_type)cached_interpreter_table.ADD, 0);
#else
#ifdef __x86_64__
   int rs = allocate_register_32((unsigned int *)dst->f.r.rs);
   int rt = allocate_register_32((unsigned int *)dst->f.r.rt);
   int rd = allocate_register_32_w((unsigned int *)dst->f.r.rd);

   if (rs == rd)
      add_reg32_reg32(rd, rt);
   else if (rt == rd)
      add_reg32_reg32(rd, rs);
   else
   {
      mov_reg32_reg32(rd, rs);
      add_reg32_reg32(rd, rt);
   }
#else
   int rs = allocate_register((unsigned int *)dst->f.r.rs);
   int rt = allocate_register((unsigned int *)dst->f.r.rt);
   int rd = allocate_register_w((unsigned int *)dst->f.r.rd);

   if (rt != rd && rs != rd)
   {
      mov_reg32_reg32(rd, rs);
      add_reg32_reg32(rd, rt);
   }
   else
   {
      int temp = lru_register();
      free_register(temp);
      mov_reg32_reg32(temp, rs);
      add_reg32_reg32(temp, rt);
      mov_reg32_reg32(rd, temp);
   }
#endif
#endif
}

void genaddu(void)
{
#ifdef INTERPRET_ADDU
   gencallinterp((native_type)cached_interpreter_table.ADDU, 0);
#else
#ifdef __x86_64__
   int rs = allocate_register_32((unsigned int *)dst->f.r.rs);
   int rt = allocate_register_32((unsigned int *)dst->f.r.rt);
   int rd = allocate_register_32_w((unsigned int *)dst->f.r.rd);

   if (rs == rd)
      add_reg32_reg32(rd, rt);
   else if (rt == rd)
      add_reg32_reg32(rd, rs);
   else
   {
      mov_reg32_reg32(rd, rs);
      add_reg32_reg32(rd, rt);
   }
#else
   int rs = allocate_register((unsigned int *)dst->f.r.rs);
   int rt = allocate_register((unsigned int *)dst->f.r.rt);
   int rd = allocate_register_w((unsigned int *)dst->f.r.rd);

   if (rt != rd && rs != rd)
   {
      mov_reg32_reg32(rd, rs);
      add_reg32_reg32(rd, rt);
   }
   else
   {
      int temp = lru_register();
      free_register(temp);
      mov_reg32_reg32(temp, rs);
      add_reg32_reg32(temp, rt);
      mov_reg32_reg32(rd, temp);
   }
#endif
#endif
}

void gensub(void)
{
#ifdef INTERPRET_SUB
   gencallinterp((native_type)cached_interpreter_table.SUB, 0);
#else
#ifdef __x86_64__
   int rs = allocate_register_32((unsigned int *)dst->f.r.rs);
   int rt = allocate_register_32((unsigned int *)dst->f.r.rt);
   int rd = allocate_register_32_w((unsigned int *)dst->f.r.rd);

   if (rs == rd)
      sub_reg32_reg32(rd, rt);
   else if (rt == rd)
   {
      neg_reg32(rd);
      add_reg32_reg32(rd, rs);
   }
   else
   {
      mov_reg32_reg32(rd, rs);
      sub_reg32_reg32(rd, rt);
   }
#else
   int rs = allocate_register((unsigned int *)dst->f.r.rs);
   int rt = allocate_register((unsigned int *)dst->f.r.rt);
   int rd = allocate_register_w((unsigned int *)dst->f.r.rd);

   if (rt != rd && rs != rd)
   {
      mov_reg32_reg32(rd, rs);
      sub_reg32_reg32(rd, rt);
   }
   else
   {
      int temp = lru_register();
      free_register(temp);
      mov_reg32_reg32(temp, rs);
      sub_reg32_reg32(temp, rt);
      mov_reg32_reg32(rd, temp);
   }
#endif
#endif
}

void gensubu(void)
{
#ifdef INTERPRET_SUBU
   gencallinterp((native_type)cached_interpreter_table.SUBU, 0);
#else
#ifdef __x86_64__
   int rs = allocate_register_32((unsigned int *)dst->f.r.rs);
   int rt = allocate_register_32((unsigned int *)dst->f.r.rt);
   int rd = allocate_register_32_w((unsigned int *)dst->f.r.rd);

   if (rs == rd)
      sub_reg32_reg32(rd, rt);
   else if (rt == rd)
   {
      neg_reg32(rd);
      add_reg32_reg32(rd, rs);
   }
   else
   {
      mov_reg32_reg32(rd, rs);
      sub_reg32_reg32(rd, rt);
   }
#else
   int rs = allocate_register((unsigned int *)dst->f.r.rs);
   int rt = allocate_register((unsigned int *)dst->f.r.rt);
   int rd = allocate_register_w((unsigned int *)dst->f.r.rd);

   if (rt != rd && rs != rd)
   {
      mov_reg32_reg32(rd, rs);
      sub_reg32_reg32(rd, rt);
   }
   else
   {
      int temp = lru_register();
      free_register(temp);
      mov_reg32_reg32(temp, rs);
      sub_reg32_reg32(temp, rt);
      mov_reg32_reg32(rd, temp);
   }
#endif
#endif
}

void genand(void)
{
#ifdef INTERPRET_AND
   gencallinterp((native_type)cached_interpreter_table.AND, 0);
#else
#ifdef __x86_64__
   int rs = allocate_register_64((unsigned long long *)dst->f.r.rs);
   int rt = allocate_register_64((unsigned long long *)dst->f.r.rt);
   int rd = allocate_register_64_w((unsigned long long *)dst->f.r.rd);

   if (rs == rd)
      and_reg64_reg64(rd, rt);
   else if (rt == rd)
      and_reg64_reg64(rd, rs);
   else
   {
      mov_reg64_reg64(rd, rs);
      and_reg64_reg64(rd, rt);
   }
#else
   int rs1 = allocate_64_register1((unsigned int *)dst->f.r.rs);
   int rs2 = allocate_64_register2((unsigned int *)dst->f.r.rs);
   int rt1 = allocate_64_register1((unsigned int *)dst->f.r.rt);
   int rt2 = allocate_64_register2((unsigned int *)dst->f.r.rt);
   int rd1 = allocate_64_register1_w((unsigned int *)dst->f.r.rd);
   int rd2 = allocate_64_register2_w((unsigned int *)dst->f.r.rd);

   if (rt1 != rd1 && rs1 != rd1)
   {
      mov_reg32_reg32(rd1, rs1);
      mov_reg32_reg32(rd2, rs2);
      and_reg32_reg32(rd1, rt1);
      and_reg32_reg32(rd2, rt2);
   }
   else
   {
      int temp = lru_register();
      free_register(temp);
      mov_reg32_reg32(temp, rs1);
      and_reg32_reg32(temp, rt1);
      mov_reg32_reg32(rd1, temp);
      mov_reg32_reg32(temp, rs2);
      and_reg32_reg32(temp, rt2);
      mov_reg32_reg32(rd2, temp);
   }
#endif
#endif
}

void genor(void)
{
#ifdef INTERPRET_OR
   gencallinterp((native_type)cached_interpreter_table.OR, 0);
#else
#ifdef __x86_64__
   int rs = allocate_register_64((unsigned long long *)dst->f.r.rs);
   int rt = allocate_register_64((unsigned long long *)dst->f.r.rt);
   int rd = allocate_register_64_w((unsigned long long *)dst->f.r.rd);

   if (rs == rd)
      or_reg64_reg64(rd, rt);
   else if (rt == rd)
      or_reg64_reg64(rd, rs);
   else
   {
      mov_reg64_reg64(rd, rs);
      or_reg64_reg64(rd, rt);
   }
#else
   int rs1 = allocate_64_register1((unsigned int *)dst->f.r.rs);
   int rs2 = allocate_64_register2((unsigned int *)dst->f.r.rs);
   int rt1 = allocate_64_register1((unsigned int *)dst->f.r.rt);
   int rt2 = allocate_64_register2((unsigned int *)dst->f.r.rt);
   int rd1 = allocate_64_register1_w((unsigned int *)dst->f.r.rd);
   int rd2 = allocate_64_register2_w((unsigned int *)dst->f.r.rd);

   if (rt1 != rd1 && rs1 != rd1)
   {
      mov_reg32_reg32(rd1, rs1);
      mov_reg32_reg32(rd2, rs2);
      or_reg32_reg32(rd1, rt1);
      or_reg32_reg32(rd2, rt2);
   }
   else
   {
      int temp = lru_register();
      free_register(temp);
      mov_reg32_reg32(temp, rs1);
      or_reg32_reg32(temp, rt1);
      mov_reg32_reg32(rd1, temp);
      mov_reg32_reg32(temp, rs2);
      or_reg32_reg32(temp, rt2);
      mov_reg32_reg32(rd2, temp);
   }
#endif
#endif
}

void genxor(void)
{
#ifdef INTERPRET_XOR
   gencallinterp((native_type)cached_interpreter_table.XOR, 0);
#else
#ifdef __x86_64__
   int rs = allocate_register_64((unsigned long long *)dst->f.r.rs);
   int rt = allocate_register_64((unsigned long long *)dst->f.r.rt);
   int rd = allocate_register_64_w((unsigned long long *)dst->f.r.rd);

   if (rs == rd)
      xor_reg64_reg64(rd, rt);
   else if (rt == rd)
      xor_reg64_reg64(rd, rs);
   else
   {
      mov_reg64_reg64(rd, rs);
      xor_reg64_reg64(rd, rt);
   }
#else
   int rs1 = allocate_64_register1((unsigned int *)dst->f.r.rs);
   int rs2 = allocate_64_register2((unsigned int *)dst->f.r.rs);
   int rt1 = allocate_64_register1((unsigned int *)dst->f.r.rt);
   int rt2 = allocate_64_register2((unsigned int *)dst->f.r.rt);
   int rd1 = allocate_64_register1_w((unsigned int *)dst->f.r.rd);
   int rd2 = allocate_64_register2_w((unsigned int *)dst->f.r.rd);

   if (rt1 != rd1 && rs1 != rd1)
   {
      mov_reg32_reg32(rd1, rs1);
      mov_reg32_reg32(rd2, rs2);
      xor_reg32_reg32(rd1, rt1);
      xor_reg32_reg32(rd2, rt2);
   }
   else
   {
      int temp = lru_register();
      free_register(temp);
      mov_reg32_reg32(temp, rs1);
      xor_reg32_reg32(temp, rt1);
      mov_reg32_reg32(rd1, temp);
      mov_reg32_reg32(temp, rs2);
      xor_reg32_reg32(temp, rt2);
      mov_reg32_reg32(rd2, temp);
   }
#endif
#endif
}

void gennor(void)
{
#ifdef INTERPRET_NOR
   gencallinterp((native_type)cached_interpreter_table.NOR, 0);
#else
#ifdef __x86_64__
   int rs = allocate_register_64((unsigned long long *)dst->f.r.rs);
   int rt = allocate_register_64((unsigned long long *)dst->f.r.rt);
   int rd = allocate_register_64_w((unsigned long long *)dst->f.r.rd);

   if (rs == rd)
   {
      or_reg64_reg64(rd, rt);
      not_reg64(rd);
   }
   else if (rt == rd)
   {
      or_reg64_reg64(rd, rs);
      not_reg64(rd);
   }
   else
   {
      mov_reg64_reg64(rd, rs);
      or_reg64_reg64(rd, rt);
      not_reg64(rd);
   }
#else
   int rs1 = allocate_64_register1((unsigned int *)dst->f.r.rs);
   int rs2 = allocate_64_register2((unsigned int *)dst->f.r.rs);
   int rt1 = allocate_64_register1((unsigned int *)dst->f.r.rt);
   int rt2 = allocate_64_register2((unsigned int *)dst->f.r.rt);
   int rd1 = allocate_64_register1_w((unsigned int *)dst->f.r.rd);
   int rd2 = allocate_64_register2_w((unsigned int *)dst->f.r.rd);

   if (rt1 != rd1 && rs1 != rd1)
   {
      mov_reg32_reg32(rd1, rs1);
      mov_reg32_reg32(rd2, rs2);
      or_reg32_reg32(rd1, rt1);
      or_reg32_reg32(rd2, rt2);
      not_reg32(rd1);
      not_reg32(rd2);
   }
   else
   {
      int temp = lru_register();
      free_register(temp);
      mov_reg32_reg32(temp, rs1);
      or_reg32_reg32(temp, rt1);
      mov_reg32_reg32(rd1, temp);
      mov_reg32_reg32(temp, rs2);
      or_reg32_reg32(temp, rt2);
      mov_reg32_reg32(rd2, temp);
      not_reg32(rd1);
      not_reg32(rd2);
   }
#endif
#endif
}

void genslt(void)
{
#ifdef INTERPRET_SLT
   gencallinterp((native_type)cached_interpreter_table.SLT, 0);
#else
#ifdef __x86_64__
   int rs = allocate_register_64((unsigned long long *)dst->f.r.rs);
   int rt = allocate_register_64((unsigned long long *)dst->f.r.rt);
   int rd = allocate_register_64_w((unsigned long long *)dst->f.r.rd);

   cmp_reg64_reg64(rs, rt);
   setl_reg8(rd);
   and_reg64_imm8(rd, 1);
#else
   int rs1 = allocate_64_register1((unsigned int *)dst->f.r.rs);
   int rs2 = allocate_64_register2((unsigned int *)dst->f.r.rs);
   int rt1 = allocate_64_register1((unsigned int *)dst->f.r.rt);
   int rt2 = allocate_64_register2((unsigned int *)dst->f.r.rt);
   int rd = allocate_register_w((unsigned int *)dst->f.r.rd);

   cmp_reg32_reg32(rs2, rt2);
   jl_rj(13);
   jne_rj(4); // 2
   cmp_reg32_reg32(rs1, rt1); // 2
   jl_rj(7); // 2
   mov_reg32_imm32(rd, 0); // 5
   jmp_imm_short(5); // 2
   mov_reg32_imm32(rd, 1); // 5
#endif
#endif
}

void gensltu(void)
{
#ifdef INTERPRET_SLTU
   gencallinterp((native_type)cached_interpreter_table.SLTU, 0);
#else
#ifdef __x86_64__
   int rs = allocate_register_64((unsigned long long *)dst->f.r.rs);
   int rt = allocate_register_64((unsigned long long *)dst->f.r.rt);
   int rd = allocate_register_64_w((unsigned long long *)dst->f.r.rd);

   cmp_reg64_reg64(rs, rt);
   setb_reg8(rd);
   and_reg64_imm8(rd, 1);
#else
   int rs1 = allocate_64_register1((unsigned int *)dst->f.r.rs);
   int rs2 = allocate_64_register2((unsigned int *)dst->f.r.rs);
   int rt1 = allocate_64_register1((unsigned int *)dst->f.r.rt);
   int rt2 = allocate_64_register2((unsigned int *)dst->f.r.rt);
   int rd = allocate_register_w((unsigned int *)dst->f.r.rd);

   cmp_reg32_reg32(rs2, rt2);
   jb_rj(13);
   jne_rj(4); // 2
   cmp_reg32_reg32(rs1, rt1); // 2
   jb_rj(7); // 2
   mov_reg32_imm32(rd, 0); // 5
   jmp_imm_short(5); // 2
   mov_reg32_imm32(rd, 1); // 5
#endif
#endif
}

void gendadd(void)
{
#ifdef INTERPRET_DADD
   gencallinterp((native_type)cached_interpreter_table.DADD, 0);
#else
#ifdef __x86_64__
   int rs = allocate_register_64((unsigned long long *)dst->f.r.rs);
   int rt = allocate_register_64((unsigned long long *)dst->f.r.rt);
   int rd = allocate_register_64_w((unsigned long long *)dst->f.r.rd);

   if (rs == rd)
      add_reg64_reg64(rd, rt);
   else if (rt == rd)
      add_reg64_reg64(rd, rs);
   else
   {
      mov_reg64_reg64(rd, rs);
      add_reg64_reg64(rd, rt);
   }
#else
   int rs1 = allocate_64_register1((unsigned int *)dst->f.r.rs);
   int rs2 = allocate_64_register2((unsigned int *)dst->f.r.rs);
   int rt1 = allocate_64_register1((unsigned int *)dst->f.r.rt);
   int rt2 = allocate_64_register2((unsigned int *)dst->f.r.rt);
   int rd1 = allocate_64_register1_w((unsigned int *)dst->f.r.rd);
   int rd2 = allocate_64_register2_w((unsigned int *)dst->f.r.rd);

   if (rt1 != rd1 && rs1 != rd1)
   {
      mov_reg32_reg32(rd1, rs1);
      mov_reg32_reg32(rd2, rs2);
      add_reg32_reg32(rd1, rt1);
      adc_reg32_reg32(rd2, rt2);
   }
   else
   {
      int temp = lru_register();
      free_register(temp);
      mov_reg32_reg32(temp, rs1);
      add_reg32_reg32(temp, rt1);
      mov_reg32_reg32(rd1, temp);
      mov_reg32_reg32(temp, rs2);
      adc_reg32_reg32(temp, rt2);
      mov_reg32_reg32(rd2, temp);
   }
#endif
#endif
}

void gendaddu(void)
{
#ifdef INTERPRET_DADDU
   gencallinterp((native_type)cached_interpreter_table.DADDU, 0);
#else
#ifdef __x86_64__
   int rs = allocate_register_64((unsigned long long *)dst->f.r.rs);
   int rt = allocate_register_64((unsigned long long *)dst->f.r.rt);
   int rd = allocate_register_64_w((unsigned long long *)dst->f.r.rd);

   if (rs == rd)
      add_reg64_reg64(rd, rt);
   else if (rt == rd)
      add_reg64_reg64(rd, rs);
   else
   {
      mov_reg64_reg64(rd, rs);
      add_reg64_reg64(rd, rt);
   }
#else
   int rs1 = allocate_64_register1((unsigned int *)dst->f.r.rs);
   int rs2 = allocate_64_register2((unsigned int *)dst->f.r.rs);
   int rt1 = allocate_64_register1((unsigned int *)dst->f.r.rt);
   int rt2 = allocate_64_register2((unsigned int *)dst->f.r.rt);
   int rd1 = allocate_64_register1_w((unsigned int *)dst->f.r.rd);
   int rd2 = allocate_64_register2_w((unsigned int *)dst->f.r.rd);

   if (rt1 != rd1 && rs1 != rd1)
   {
      mov_reg32_reg32(rd1, rs1);
      mov_reg32_reg32(rd2, rs2);
      add_reg32_reg32(rd1, rt1);
      adc_reg32_reg32(rd2, rt2);
   }
   else
   {
      int temp = lru_register();
      free_register(temp);
      mov_reg32_reg32(temp, rs1);
      add_reg32_reg32(temp, rt1);
      mov_reg32_reg32(rd1, temp);
      mov_reg32_reg32(temp, rs2);
      adc_reg32_reg32(temp, rt2);
      mov_reg32_reg32(rd2, temp);
   }
#endif
#endif
}

void gendsub(void)
{
#ifdef INTERPRET_DSUB
   gencallinterp((native_type)cached_interpreter_table.DSUB, 0);
#else
#ifdef __x86_64__
   int rs = allocate_register_64((unsigned long long *)dst->f.r.rs);
   int rt = allocate_register_64((unsigned long long *)dst->f.r.rt);
   int rd = allocate_register_64_w((unsigned long long *)dst->f.r.rd);

   if (rs == rd)
      sub_reg64_reg64(rd, rt);
   else if (rt == rd)
   {
      neg_reg64(rd);
      add_reg64_reg64(rd, rs);
   }
   else
   {
      mov_reg64_reg64(rd, rs);
      sub_reg64_reg64(rd, rt);
   }
#else
   int rs1 = allocate_64_register1((unsigned int *)dst->f.r.rs);
   int rs2 = allocate_64_register2((unsigned int *)dst->f.r.rs);
   int rt1 = allocate_64_register1((unsigned int *)dst->f.r.rt);
   int rt2 = allocate_64_register2((unsigned int *)dst->f.r.rt);
   int rd1 = allocate_64_register1_w((unsigned int *)dst->f.r.rd);
   int rd2 = allocate_64_register2_w((unsigned int *)dst->f.r.rd);

   if (rt1 != rd1 && rs1 != rd1)
   {
      mov_reg32_reg32(rd1, rs1);
      mov_reg32_reg32(rd2, rs2);
      sub_reg32_reg32(rd1, rt1);
      sbb_reg32_reg32(rd2, rt2);
   }
   else
   {
      int temp = lru_register();
      free_register(temp);
      mov_reg32_reg32(temp, rs1);
      sub_reg32_reg32(temp, rt1);
      mov_reg32_reg32(rd1, temp);
      mov_reg32_reg32(temp, rs2);
      sbb_reg32_reg32(temp, rt2);
      mov_reg32_reg32(rd2, temp);
   }
#endif
#endif
}

void gendsubu(void)
{
#ifdef INTERPRET_DSUBU
   gencallinterp((native_type)cached_interpreter_table.DSUBU, 0);
#else
#ifdef __x86_64__
   int rs = allocate_register_64((unsigned long long *)dst->f.r.rs);
   int rt = allocate_register_64((unsigned long long *)dst->f.r.rt);
   int rd = allocate_register_64_w((unsigned long long *)dst->f.r.rd);

   if (rs == rd)
      sub_reg64_reg64(rd, rt);
   else if (rt == rd)
   {
      neg_reg64(rd);
      add_reg64_reg64(rd, rs);
   }
   else
   {
      mov_reg64_reg64(rd, rs);
      sub_reg64_reg64(rd, rt);
   }
#else
   int rs1 = allocate_64_register1((unsigned int *)dst->f.r.rs);
   int rs2 = allocate_64_register2((unsigned int *)dst->f.r.rs);
   int rt1 = allocate_64_register1((unsigned int *)dst->f.r.rt);
   int rt2 = allocate_64_register2((unsigned int *)dst->f.r.rt);
   int rd1 = allocate_64_register1_w((unsigned int *)dst->f.r.rd);
   int rd2 = allocate_64_register2_w((unsigned int *)dst->f.r.rd);

   if (rt1 != rd1 && rs1 != rd1)
   {
      mov_reg32_reg32(rd1, rs1);
      mov_reg32_reg32(rd2, rs2);
      sub_reg32_reg32(rd1, rt1);
      sbb_reg32_reg32(rd2, rt2);
   }
   else
   {
      int temp = lru_register();
      free_register(temp);
      mov_reg32_reg32(temp, rs1);
      sub_reg32_reg32(temp, rt1);
      mov_reg32_reg32(rd1, temp);
      mov_reg32_reg32(temp, rs2);
      sbb_reg32_reg32(temp, rt2);
      mov_reg32_reg32(rd2, temp);
   }
#endif
#endif
}

void genteq(void)
{
   gencallinterp((native_type)cached_interpreter_table.TEQ, 0);
}

void gendsll(void)
{
#ifdef INTERPRET_DSLL
   gencallinterp((native_type)cached_interpreter_table.DSLL, 0);
#else
#ifdef __x86_64__
   int rt = allocate_register_64((unsigned long long *)dst->f.r.rt);
   int rd = allocate_register_64_w((unsigned long long *)dst->f.r.rd);

   mov_reg64_reg64(rd, rt);
   shl_reg64_imm8(rd, dst->f.r.sa);
#else
   int rt1 = allocate_64_register1((unsigned int *)dst->f.r.rt);
   int rt2 = allocate_64_register2((unsigned int *)dst->f.r.rt);
   int rd1 = allocate_64_register1_w((unsigned int *)dst->f.r.rd);
   int rd2 = allocate_64_register2_w((unsigned int *)dst->f.r.rd);

   mov_reg32_reg32(rd1, rt1);
   mov_reg32_reg32(rd2, rt2);
   shld_reg32_reg32_imm8(rd2, rd1, dst->f.r.sa);
   shl_reg32_imm8(rd1, dst->f.r.sa);
   if (dst->f.r.sa & 0x20)
   {
      mov_reg32_reg32(rd2, rd1);
      xor_reg32_reg32(rd1, rd1);
   }
#endif
#endif
}

void gendsrl(void)
{
#ifdef INTERPRET_DSRL
   gencallinterp((native_type)cached_interpreter_table.DSRL, 0);
#else
#ifdef __x86_64__
   int rt = allocate_register_64((unsigned long long *)dst->f.r.rt);
   int rd = allocate_register_64_w((unsigned long long *)dst->f.r.rd);

   mov_reg64_reg64(rd, rt);
   shr_reg64_imm8(rd, dst->f.r.sa);
#else
   int rt1 = allocate_64_register1((unsigned int *)dst->f.r.rt);
   int rt2 = allocate_64_register2((unsigned int *)dst->f.r.rt);
   int rd1 = allocate_64_register1_w((unsigned int *)dst->f.r.rd);
   int rd2 = allocate_64_register2_w((unsigned int *)dst->f.r.rd);

   mov_reg32_reg32(rd1, rt1);
   mov_reg32_reg32(rd2, rt2);
   shrd_reg32_reg32_imm8(rd1, rd2, dst->f.r.sa);
   shr_reg32_imm8(rd2, dst->f.r.sa);
   if (dst->f.r.sa & 0x20)
   {
      mov_reg32_reg32(rd1, rd2);
      xor_reg32_reg32(rd2, rd2);
   }
#endif
#endif
}

void gendsra(void)
{
#ifdef INTERPRET_DSRA
   gencallinterp((native_type)cached_interpreter_table.DSRA, 0);
#else
#ifdef __x86_64__
   int rt = allocate_register_64((unsigned long long *)dst->f.r.rt);
   int rd = allocate_register_64_w((unsigned long long *)dst->f.r.rd);

   mov_reg64_reg64(rd, rt);
   sar_reg64_imm8(rd, dst->f.r.sa);
#else
   int rt1 = allocate_64_register1((unsigned int *)dst->f.r.rt);
   int rt2 = allocate_64_register2((unsigned int *)dst->f.r.rt);
   int rd1 = allocate_64_register1_w((unsigned int *)dst->f.r.rd);
   int rd2 = allocate_64_register2_w((unsigned int *)dst->f.r.rd);

   mov_reg32_reg32(rd1, rt1);
   mov_reg32_reg32(rd2, rt2);
   shrd_reg32_reg32_imm8(rd1, rd2, dst->f.r.sa);
   sar_reg32_imm8(rd2, dst->f.r.sa);
   if (dst->f.r.sa & 0x20)
   {
      mov_reg32_reg32(rd1, rd2);
      sar_reg32_imm8(rd2, 31);
   }
#endif
#endif
}

void gendsll32(void)
{
#ifdef INTERPRET_DSLL32
   gencallinterp((native_type)cached_interpreter_table.DSLL32, 0);
#else
#ifdef __x86_64__
   int rt = allocate_register_64((unsigned long long *)dst->f.r.rt);
   int rd = allocate_register_64_w((unsigned long long *)dst->f.r.rd);

   mov_reg64_reg64(rd, rt);
   shl_reg64_imm8(rd, dst->f.r.sa + 32);
#else
   int rt1 = allocate_64_register1((unsigned int *)dst->f.r.rt);
   int rd1 = allocate_64_register1_w((unsigned int *)dst->f.r.rd);
   int rd2 = allocate_64_register2_w((unsigned int *)dst->f.r.rd);

   mov_reg32_reg32(rd2, rt1);
   shl_reg32_imm8(rd2, dst->f.r.sa);
   xor_reg32_reg32(rd1, rd1);
#endif
#endif
}

void gendsrl32(void)
{
#ifdef INTERPRET_DSRL32
   gencallinterp((native_type)cached_interpreter_table.DSRL32, 0);
#else
#ifdef __x86_64__
   int rt = allocate_register_64((unsigned long long *)dst->f.r.rt);
   int rd = allocate_register_64_w((unsigned long long *)dst->f.r.rd);

   mov_reg64_reg64(rd, rt);
   shr_reg64_imm8(rd, dst->f.r.sa + 32);
#else
   int rt2 = allocate_64_register2((unsigned int *)dst->f.r.rt);
   int rd1 = allocate_64_register1_w((unsigned int *)dst->f.r.rd);
   int rd2 = allocate_64_register2_w((unsigned int *)dst->f.r.rd);

   mov_reg32_reg32(rd1, rt2);
   shr_reg32_imm8(rd1, dst->f.r.sa);
   xor_reg32_reg32(rd2, rd2);
#endif
#endif
}

void gendsra32(void)
{
#ifdef INTERPRET_DSRA32
   gencallinterp((native_type)cached_interpreter_table.DSRA32, 0);
#else
#ifdef __x86_64__
   int rt = allocate_register_64((unsigned long long *)dst->f.r.rt);
   int rd = allocate_register_64_w((unsigned long long *)dst->f.r.rd);

   mov_reg64_reg64(rd, rt);
   sar_reg64_imm8(rd, dst->f.r.sa + 32);
#else
   int rt2 = allocate_64_register2((unsigned int *)dst->f.r.rt);
   int rd = allocate_register_w((unsigned int *)dst->f.r.rd);

   mov_reg32_reg32(rd, rt2);
   sar_reg32_imm8(rd, dst->f.r.sa);
#endif
#endif
}

