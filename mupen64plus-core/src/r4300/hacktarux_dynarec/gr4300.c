/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - gr4300.c                                                *
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

#include "assemble.h"
#include "regcache.h"
#include "interpret.h"

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

#if !defined(offsetof)
#define offsetof(TYPE,MEMBER) ((unsigned int) &((TYPE*)0)->MEMBER)
#endif

static precomp_instr fake_instr;
#ifdef COMPARE_CORE
#ifdef __x86_64__
static long long debug_reg_storage[8];
#else
static int eax, ebx, ecx, edx, esp, ebp, esi, edi;
#endif
#endif

int branch_taken = 0;

/* static functions */

static void genupdate_count(unsigned int addr)
{
#if !defined(COMPARE_CORE) && !defined(DBG)
   mov_reg32_imm32(EAX, addr);
#ifdef __x86_64__
   sub_xreg32_m32rel(EAX, (unsigned int*)(&last_addr));
   shr_reg32_imm8(EAX, 2);
   mov_xreg32_m32rel(EDX, (void*)&count_per_op);
   mul_reg32(EDX);
   add_m32rel_xreg32((unsigned int*)(&g_cp0_regs[CP0_COUNT_REG]), EAX);
#else
   sub_reg32_m32(EAX, (unsigned int*)(&last_addr));
   shr_reg32_imm8(EAX, 2);
   mov_reg32_imm32(EDX, &count_per_op);
   mul_reg32(EDX);
   add_m32_reg32((unsigned int*)(&g_cp0_regs[CP0_COUNT_REG]), EAX);
#endif

#else

#ifdef __x86_64__
   mov_reg64_imm64(RAX, (unsigned long long) (dst+1));
   mov_m64rel_xreg64((unsigned long long *)(&PC), RAX);
   mov_reg64_imm64(RAX, (unsigned long long)update_count);
   call_reg64(RAX);
#else
   mov_m32_imm32((unsigned int*)(&PC), (unsigned int)(dst+1));
   mov_reg32_imm32(EAX, (unsigned int)update_count);
   call_reg32(EAX);
#endif
#endif
}

static void gencheck_interupt(unsigned long long instr_structure)
{
#ifdef __x86_64__
   mov_xreg32_m32rel(EAX, (void*)(&next_interupt));
   cmp_xreg32_m32rel(EAX, (void*)&g_cp0_regs[CP0_COUNT_REG]);
   ja_rj(0);
   jump_start_rel8();

   mov_reg64_imm64(RAX, (unsigned long long) instr_structure);
   mov_m64rel_xreg64((unsigned long long *)(&PC), RAX);
   mov_reg64_imm64(RAX, (unsigned long long) gen_interupt);
   call_reg64(RAX);

   jump_end_rel8();
#else
   mov_eax_memoffs32(&next_interupt);
   cmp_reg32_m32(EAX, &g_cp0_regs[CP0_COUNT_REG]);
   ja_rj(17);
   mov_m32_imm32((unsigned int*)(&PC), instr_structure); // 10
   mov_reg32_imm32(EAX, (unsigned int)gen_interupt); // 5
   call_reg32(EAX); // 2
#endif
}

static void gencheck_interupt_out(unsigned int addr)
{
#ifdef __x86_64__
   mov_xreg32_m32rel(EAX, (void*)(&next_interupt));
   cmp_xreg32_m32rel(EAX, (void*)&g_cp0_regs[CP0_COUNT_REG]);
   ja_rj(0);
   jump_start_rel8();

   mov_m32rel_imm32((unsigned int*)(&fake_instr.addr), addr);
   mov_reg64_imm64(RAX, (unsigned long long) (&fake_instr));
   mov_m64rel_xreg64((unsigned long long *)(&PC), RAX);
   mov_reg64_imm64(RAX, (unsigned long long) gen_interupt);
   call_reg64(RAX);

   jump_end_rel8();
#else
   mov_eax_memoffs32(&next_interupt);
   cmp_reg32_m32(EAX, &g_cp0_regs[CP0_COUNT_REG]);
   ja_rj(27);
   mov_m32_imm32((unsigned int*)(&fake_instr.addr), addr);
   mov_m32_imm32((unsigned int*)(&PC), (unsigned int)(&fake_instr));
   mov_reg32_imm32(EAX, (unsigned int)gen_interupt);
   call_reg32(EAX);
#endif
}

static void genbeq_test(void)
{
   int rs_64bit = is64((unsigned int *)dst->f.i.rs);
   int rt_64bit = is64((unsigned int *)dst->f.i.rt);

   if (rs_64bit == 0 && rt_64bit == 0)
   {
#ifdef __x86_64__
      int rs = allocate_register_32((unsigned int *)dst->f.i.rs);
      int rt = allocate_register_32((unsigned int *)dst->f.i.rt);

      cmp_reg32_reg32(rs, rt);
      sete_m8rel((unsigned char *) &branch_taken);
#else
      int rs = allocate_register((unsigned int *)dst->f.i.rs);
      int rt = allocate_register((unsigned int *)dst->f.i.rt);

      cmp_reg32_reg32(rs, rt);
      jne_rj(12);
      mov_m32_imm32((unsigned int *)(&branch_taken), 1); // 10
      jmp_imm_short(10); // 2
      mov_m32_imm32((unsigned int *)(&branch_taken), 0); // 10
#endif
   }
   else if (rs_64bit == -1)
   {
#ifdef __x86_64__
      int rt = allocate_register_64((unsigned long long *)dst->f.i.rt);

      cmp_xreg64_m64rel(rt, (unsigned long long *) dst->f.i.rs);
      sete_m8rel((unsigned char *) &branch_taken);
#else
      int rt1 = allocate_64_register1((unsigned int *)dst->f.i.rt);
      int rt2 = allocate_64_register2((unsigned int *)dst->f.i.rt);

      cmp_reg32_m32(rt1, (unsigned int *)dst->f.i.rs);
      jne_rj(20);
      cmp_reg32_m32(rt2, ((unsigned int *)dst->f.i.rs)+1); // 6
      jne_rj(12); // 2
      mov_m32_imm32((unsigned int *)(&branch_taken), 1); // 10
      jmp_imm_short(10); // 2
      mov_m32_imm32((unsigned int *)(&branch_taken), 0); // 10
#endif
   }
   else if (rt_64bit == -1)
   {
#ifdef __x86_64__
      int rs = allocate_register_64((unsigned long long *)dst->f.i.rs);

      cmp_xreg64_m64rel(rs, (unsigned long long *)dst->f.i.rt);
      sete_m8rel((unsigned char *) &branch_taken);
#else
      int rs1 = allocate_64_register1((unsigned int *)dst->f.i.rs);
      int rs2 = allocate_64_register2((unsigned int *)dst->f.i.rs);

      cmp_reg32_m32(rs1, (unsigned int *)dst->f.i.rt);
      jne_rj(20);
      cmp_reg32_m32(rs2, ((unsigned int *)dst->f.i.rt)+1); // 6
      jne_rj(12); // 2
      mov_m32_imm32((unsigned int *)(&branch_taken), 1); // 10
      jmp_imm_short(10); // 2
      mov_m32_imm32((unsigned int *)(&branch_taken), 0); // 10
#endif
   }
   else
   {
#ifdef __x86_64__
      int rs = allocate_register_64((unsigned long long *)dst->f.i.rs);
      int rt = allocate_register_64((unsigned long long *)dst->f.i.rt);
      cmp_reg64_reg64(rs, rt);
      sete_m8rel((unsigned char *) &branch_taken);
#else
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
      mov_m32_imm32((unsigned int *)(&branch_taken), 1); // 10
      jmp_imm_short(10); // 2
      mov_m32_imm32((unsigned int *)(&branch_taken), 0); // 10
#endif
   }
}

static void genbne_test(void)
{
   int rs_64bit = is64((unsigned int *)dst->f.i.rs);
   int rt_64bit = is64((unsigned int *)dst->f.i.rt);

#ifdef __x86_64__
   if (rs_64bit == 0 && rt_64bit == 0)
   {
      int rs = allocate_register_32((unsigned int *)dst->f.i.rs);
      int rt = allocate_register_32((unsigned int *)dst->f.i.rt);

      cmp_reg32_reg32(rs, rt);
      setne_m8rel((unsigned char *) &branch_taken);
   }
   else if (rs_64bit == -1)
   {
      int rt = allocate_register_64((unsigned long long *) dst->f.i.rt);

      cmp_xreg64_m64rel(rt, (unsigned long long *)dst->f.i.rs);
      setne_m8rel((unsigned char *) &branch_taken);
   }
   else if (rt_64bit == -1)
   {
      int rs = allocate_register_64((unsigned long long *) dst->f.i.rs);

      cmp_xreg64_m64rel(rs, (unsigned long long *)dst->f.i.rt);
      setne_m8rel((unsigned char *) &branch_taken);
   }
   else
   {
      int rs = allocate_register_64((unsigned long long *)dst->f.i.rs);
      int rt = allocate_register_64((unsigned long long *)dst->f.i.rt);

      cmp_reg64_reg64(rs, rt);
      setne_m8rel((unsigned char *) &branch_taken);
   }
#else
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
#endif
}

static void genblez_test(void)
{
   int rs_64bit = is64((unsigned int *)dst->f.i.rs);

#ifdef __x86_64__
   if (rs_64bit == 0)
   {
      int rs = allocate_register_32((unsigned int *)dst->f.i.rs);

      cmp_reg32_imm32(rs, 0);
      setle_m8rel((unsigned char *) &branch_taken);
   }
   else
   {
      int rs = allocate_register_64((unsigned long long *)dst->f.i.rs);

      cmp_reg64_imm8(rs, 0);
      setle_m8rel((unsigned char *) &branch_taken);
   }
#else
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
#endif
}

static void genbgtz_test(void)
{
   int rs_64bit = is64((unsigned int *)dst->f.i.rs);
#ifdef __x86_64__
   if (rs_64bit == 0)
   {
      int rs = allocate_register_32((unsigned int *)dst->f.i.rs);

      cmp_reg32_imm32(rs, 0);
      setg_m8rel((unsigned char *) &branch_taken);
   }
   else
   {
      int rs = allocate_register_64((unsigned long long *)dst->f.i.rs);

      cmp_reg64_imm8(rs, 0);
      setg_m8rel((unsigned char *) &branch_taken);
   }
#else
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
#endif
}

#ifdef __x86_64__
static void ld_register_alloc(int *pGpr1, int *pGpr2, int *pBase1, int *pBase2)
{
   int gpr1, gpr2, base1, base2 = 0;

#ifdef COMPARE_CORE
   free_registers_move_start(); // to maintain parity with 32-bit core
#endif

   if (dst->f.i.rs == dst->f.i.rt)
   {
      allocate_register_32((unsigned int*)dst->f.r.rs);          // tell regcache we need to read RS register here
      gpr1 = allocate_register_32_w((unsigned int*)dst->f.r.rt); // tell regcache we will modify RT register during this instruction
      gpr2 = lock_register(lru_register());                      // free and lock least recently used register for usage here
      add_reg32_imm32(gpr1, (int)dst->f.i.immediate);
      mov_reg32_reg32(gpr2, gpr1);
   }
   else
   {
      gpr2 = allocate_register_32((unsigned int*)dst->f.r.rs);   // tell regcache we need to read RS register here
      gpr1 = allocate_register_32_w((unsigned int*)dst->f.r.rt); // tell regcache we will modify RT register during this instruction
      free_register(gpr2);                                       // write out gpr2 if dirty because I'm going to trash it right now
      add_reg32_imm32(gpr2, (int)dst->f.i.immediate);
      mov_reg32_reg32(gpr1, gpr2);
      lock_register(gpr2);                                       // lock the freed gpr2 it so it doesn't get returned in the lru query
   }
   base1 = lock_register(lru_base_register());                  // get another lru register
   if (!fast_memory)
   {
      base2 = lock_register(lru_base_register());                // and another one if necessary
      unlock_register(base2);
   }
   unlock_register(base1);                                      // unlock the locked registers (they are 
   unlock_register(gpr2);
   set_register_state(gpr1, NULL, 0, 0);                        // clear gpr1 state because it hasn't been written yet -
   // we don't want it to be pushed/popped around read_rdramX call
   *pGpr1 = gpr1;
   *pGpr2 = gpr2;
   *pBase1 = base1;
   *pBase2 = base2;
}
#endif


/* global functions */

void gennotcompiled(void)
{
#ifdef __x86_64__
   free_registers_move_start();

   mov_reg64_imm64(RAX, (unsigned long long) dst);
   mov_memoffs64_rax((unsigned long long *) &PC); /* RIP-relative will not work here */
   mov_reg64_imm64(RAX, (unsigned long long) cached_interpreter_table.NOTCOMPILED);
   call_reg64(RAX);
#else
   free_all_registers();
   simplify_access();

   mov_m32_imm32((unsigned int*)(&PC), (unsigned int)(dst));
   mov_reg32_imm32(EAX, (unsigned int)cached_interpreter_table.NOTCOMPILED);
   call_reg32(EAX);
#endif
}

void genlink_subblock(void)
{
   free_all_registers();
   jmp(dst->addr+4);
}

#ifdef COMPARE_CORE
extern unsigned int op_R4300; /* api/debugger.c */

void gendebug(void)
{
   free_all_registers();

#ifdef __x86_64__
   mov_memoffs64_rax((unsigned long long *) &debug_reg_storage);
   mov_reg64_imm64(RAX, (unsigned long long) &debug_reg_storage);
   mov_preg64pimm8_reg64(RAX,  8, RBX);
   mov_preg64pimm8_reg64(RAX, 16, RCX);
   mov_preg64pimm8_reg64(RAX, 24, RDX);
   mov_preg64pimm8_reg64(RAX, 32, RSP);
   mov_preg64pimm8_reg64(RAX, 40, RBP);
   mov_preg64pimm8_reg64(RAX, 48, RSI);
   mov_preg64pimm8_reg64(RAX, 56, RDI);

   mov_reg64_imm64(RAX, (unsigned long long) dst);
   mov_memoffs64_rax((unsigned long long *) &PC);
   mov_reg32_imm32(EAX, (unsigned int) src);
   mov_memoffs32_eax((unsigned int *) &op);
   mov_reg64_imm64(RAX, (unsigned long long) CoreCompareCallback);
   call_reg64(RAX);

   mov_reg64_imm64(RAX, (unsigned long long) &debug_reg_storage);
   mov_reg64_preg64pimm8(RDI, RAX, 56);
   mov_reg64_preg64pimm8(RSI, RAX, 48);
   mov_reg64_preg64pimm8(RBP, RAX, 40);
   mov_reg64_preg64pimm8(RSP, RAX, 32);
   mov_reg64_preg64pimm8(RDX, RAX, 24);
   mov_reg64_preg64pimm8(RCX, RAX, 16);
   mov_reg64_preg64pimm8(RBX, RAX,  8);
   mov_reg64_preg64(RAX, RAX);
#else
   mov_m32_reg32((unsigned int*)&eax, EAX);
   mov_m32_reg32((unsigned int*)&ebx, EBX);
   mov_m32_reg32((unsigned int*)&ecx, ECX);
   mov_m32_reg32((unsigned int*)&edx, EDX);
   mov_m32_reg32((unsigned int*)&esp, ESP);
   mov_m32_reg32((unsigned int*)&ebp, EBP);
   mov_m32_reg32((unsigned int*)&esi, ESI);
   mov_m32_reg32((unsigned int*)&edi, EDI);

   mov_m32_imm32((unsigned int*)(&PC), (unsigned int)(dst));
   mov_m32_imm32((unsigned int*)(&op), (unsigned int)(src));
   mov_reg32_imm32(EAX, (unsigned int) CoreCompareCallback);
   call_reg32(EAX);

   mov_reg32_m32(EAX, (unsigned int*)&eax);
   mov_reg32_m32(EBX, (unsigned int*)&ebx);
   mov_reg32_m32(ECX, (unsigned int*)&ecx);
   mov_reg32_m32(EDX, (unsigned int*)&edx);
   mov_reg32_m32(ESP, (unsigned int*)&esp);
   mov_reg32_m32(EBP, (unsigned int*)&ebp);
   mov_reg32_m32(ESI, (unsigned int*)&esi);
   mov_reg32_m32(EDI, (unsigned int*)&edi);
#endif
}
#endif

void gencallinterp(uintptr_t addr, int jump)
{
#ifdef __x86_64__
   free_registers_move_start();

   if (jump)
      mov_m32rel_imm32((unsigned int*)(&dyna_interp), 1);

   mov_reg64_imm64(RAX, (unsigned long long) dst);
   mov_m64rel_xreg64((unsigned long long *)(&PC), RAX);
   mov_reg64_imm64(RAX, addr);
   call_reg64(RAX);

   if (jump)
   {
      mov_m32rel_imm32((unsigned int*)(&dyna_interp), 0);
      mov_reg64_imm64(RAX, (unsigned long long)dyna_jump);
      call_reg64(RAX);
   }
#else
   free_all_registers();
   simplify_access();
   if (jump)
      mov_m32_imm32((unsigned int*)(&dyna_interp), 1);
   mov_m32_imm32((unsigned int*)(&PC), (unsigned int)(dst));
   mov_reg32_imm32(EAX, addr);
   call_reg32(EAX);
   if (jump)
   {
      mov_m32_imm32((unsigned int*)(&dyna_interp), 0);
      mov_reg32_imm32(EAX, (unsigned int)dyna_jump);
      call_reg32(EAX);
   }
#endif
}

void gendelayslot(void)
{
#ifdef __x86_64__
   mov_m32rel_imm32((void*)(&delay_slot), 1);
   recompile_opcode();

   free_all_registers();
   genupdate_count(dst->addr+4);

   mov_m32rel_imm32((void*)(&delay_slot), 0);
#else
   mov_m32_imm32(&delay_slot, 1);
   recompile_opcode();

   free_all_registers();
   genupdate_count(dst->addr+4);

   mov_m32_imm32(&delay_slot, 0);
#endif
}

void genni(void)
{
   gencallinterp((native_type)cached_interpreter_table.NI, 0);
}

void genreserved(void)
{
   gencallinterp((native_type)cached_interpreter_table.RESERVED, 0);
}

void genfin_block(void)
{
   gencallinterp((native_type)cached_interpreter_table.FIN_BLOCK, 0);
}

void gencheck_interupt_reg(void) // addr is in EAX
{
#ifdef __x86_64__
   mov_xreg32_m32rel(EBX, (void*)&next_interupt);
   cmp_xreg32_m32rel(EBX, (void*)&g_cp0_regs[CP0_COUNT_REG]);
   ja_rj(0);
   jump_start_rel8();

   mov_m32rel_xreg32((unsigned int*)(&fake_instr.addr), EAX);
   mov_reg64_imm64(RAX, (unsigned long long) (&fake_instr));
   mov_m64rel_xreg64((unsigned long long *)(&PC), RAX);
   mov_reg64_imm64(RAX, (unsigned long long) gen_interupt);
   call_reg64(RAX);

   jump_end_rel8();
#else
   mov_reg32_m32(EBX, &next_interupt);
   cmp_reg32_m32(EBX, &g_cp0_regs[CP0_COUNT_REG]);
   ja_rj(22);
   mov_memoffs32_eax((unsigned int*)(&fake_instr.addr)); // 5
   mov_m32_imm32((unsigned int*)(&PC), (unsigned int)(&fake_instr)); // 10
   mov_reg32_imm32(EAX, (unsigned int)gen_interupt); // 5
   call_reg32(EAX); // 2
#endif
}

void gennop(void)
{
}

void genj(void)
{
#ifdef INTERPRET_J
   gencallinterp((native_type)cached_interpreter_table.J, 1);
#else
   unsigned int naddr;

   if (((dst->addr & 0xFFF) == 0xFFC && 
            (dst->addr < 0x80000000 || dst->addr >= 0xC0000000))||no_compiled_jump)
   {
      gencallinterp((native_type)cached_interpreter_table.J, 1);
      return;
   }

   gendelayslot();
   naddr = ((dst-1)->f.j.inst_index<<2) | (dst->addr & 0xF0000000);

#ifdef __x86_64__
   mov_m32rel_imm32((void*)(&last_addr), naddr);
#else
   mov_m32_imm32(&last_addr, naddr);
#endif
   gencheck_interupt((native_type) &actual->block[(naddr-actual->start)/4]);
   jmp(naddr);
#endif
}

void genj_out(void)
{
#ifdef INTERPRET_J_OUT
   gencallinterp((native_type)cached_interpreter_table.J_OUT, 1);
#else
   unsigned int naddr;

   if (((dst->addr & 0xFFF) == 0xFFC && 
            (dst->addr < 0x80000000 || dst->addr >= 0xC0000000))||no_compiled_jump)
   {
      gencallinterp((native_type)cached_interpreter_table.J_OUT, 1);
      return;
   }

   gendelayslot();
   naddr = ((dst-1)->f.j.inst_index<<2) | (dst->addr & 0xF0000000);

#ifdef __x86_64__
   mov_m32rel_imm32((void*)(&last_addr), naddr);
   gencheck_interupt_out(naddr);
   mov_m32rel_imm32(&jump_to_address, naddr);
   mov_reg64_imm64(RAX, (unsigned long long) (dst+1));
   mov_m64rel_xreg64((unsigned long long *)(&PC), RAX);
   mov_reg64_imm64(RAX, (unsigned long long)jump_to_func);
   call_reg64(RAX);
#else
   mov_m32_imm32(&last_addr, naddr);
   gencheck_interupt_out(naddr);
   mov_m32_imm32(&jump_to_address, naddr);
   mov_m32_imm32((unsigned int*)(&PC), (unsigned int)(dst+1));
   mov_reg32_imm32(EAX, (unsigned int)jump_to_func);
   call_reg32(EAX);
#endif
#endif
}

void genj_idle(void)
{
#ifdef INTERPRET_J_IDLE
   gencallinterp((native_type)cached_interpreter_table.J_IDLE, 1);
#else
   if (((dst->addr & 0xFFF) == 0xFFC && 
            (dst->addr < 0x80000000 || dst->addr >= 0xC0000000))||no_compiled_jump)
   {
      gencallinterp((native_type)cached_interpreter_table.J_IDLE, 1);
      return;
   }

#ifdef __x86_64__
   mov_xreg32_m32rel(EAX, (unsigned int *)(&next_interupt));
   sub_xreg32_m32rel(EAX, (unsigned int *)(&g_cp0_regs[CP0_COUNT_REG]));
   cmp_reg32_imm8(EAX, 3);
   jbe_rj(12);

   and_eax_imm32(0xFFFFFFFC);  // 5
   add_m32rel_xreg32((unsigned int *)(&g_cp0_regs[CP0_COUNT_REG]), EAX); // 7
#else
   mov_eax_memoffs32((unsigned int *)(&next_interupt));
   sub_reg32_m32(EAX, (unsigned int *)(&g_cp0_regs[CP0_COUNT_REG]));
   cmp_reg32_imm8(EAX, 3);
   jbe_rj(11);

   and_eax_imm32(0xFFFFFFFC);  // 5
   add_m32_reg32((unsigned int *)(&g_cp0_regs[CP0_COUNT_REG]), EAX); // 6
#endif

   genj();
#endif
}

void genjal(void)
{
#ifdef INTERPRET_JAL
   gencallinterp((native_type)cached_interpreter_table.JAL, 1);
#else
   unsigned int naddr;

   if (((dst->addr & 0xFFF) == 0xFFC && 
            (dst->addr < 0x80000000 || dst->addr >= 0xC0000000))||no_compiled_jump)
   {
      gencallinterp((native_type)cached_interpreter_table.JAL, 1);
      return;
   }

   gendelayslot();

#ifdef __x86_64__
   mov_m32rel_imm32((unsigned int *)(reg + 31), dst->addr + 4);
   if (((dst->addr + 4) & 0x80000000))
      mov_m32rel_imm32((unsigned int *)(&reg[31])+1, 0xFFFFFFFF);
   else
      mov_m32rel_imm32((unsigned int *)(&reg[31])+1, 0);

   naddr = ((dst-1)->f.j.inst_index<<2) | (dst->addr & 0xF0000000);

   mov_m32rel_imm32((void*)(&last_addr), naddr);
#else
   mov_m32_imm32((unsigned int *)(reg + 31), dst->addr + 4);
   if (((dst->addr + 4) & 0x80000000))
      mov_m32_imm32((unsigned int *)(&reg[31])+1, 0xFFFFFFFF);
   else
      mov_m32_imm32((unsigned int *)(&reg[31])+1, 0);

   naddr = ((dst-1)->f.j.inst_index<<2) | (dst->addr & 0xF0000000);

   mov_m32_imm32(&last_addr, naddr);
#endif
   gencheck_interupt((native_type) &actual->block[(naddr-actual->start)/4]);
   jmp(naddr);
#endif
}

void genjal_out(void)
{
#ifdef INTERPRET_JAL_OUT
   gencallinterp((native_type)cached_interpreter_table.JAL_OUT, 1);
#else
   unsigned int naddr;

   if (((dst->addr & 0xFFF) == 0xFFC && 
            (dst->addr < 0x80000000 || dst->addr >= 0xC0000000))||no_compiled_jump)
   {
      gencallinterp((native_type)cached_interpreter_table.JAL_OUT, 1);
      return;
   }

   gendelayslot();

#ifdef __x86_64__
   mov_m32rel_imm32((unsigned int *)(reg + 31), dst->addr + 4);
   if (((dst->addr + 4) & 0x80000000))
      mov_m32rel_imm32((unsigned int *)(&reg[31])+1, 0xFFFFFFFF);
   else
      mov_m32rel_imm32((unsigned int *)(&reg[31])+1, 0);

   naddr = ((dst-1)->f.j.inst_index<<2) | (dst->addr & 0xF0000000);

   mov_m32rel_imm32((void*)(&last_addr), naddr);
   gencheck_interupt_out(naddr);
   mov_m32rel_imm32(&jump_to_address, naddr);
   mov_reg64_imm64(RAX, (unsigned long long) (dst+1));
   mov_m64rel_xreg64((unsigned long long *)(&PC), RAX);
   mov_reg64_imm64(RAX, (unsigned long long) jump_to_func);
   call_reg64(RAX);
#else
   mov_m32_imm32((unsigned int *)(reg + 31), dst->addr + 4);
   if (((dst->addr + 4) & 0x80000000))
      mov_m32_imm32((unsigned int *)(&reg[31])+1, 0xFFFFFFFF);
   else
      mov_m32_imm32((unsigned int *)(&reg[31])+1, 0);

   naddr = ((dst-1)->f.j.inst_index<<2) | (dst->addr & 0xF0000000);

   mov_m32_imm32(&last_addr, naddr);
   gencheck_interupt_out(naddr);
   mov_m32_imm32(&jump_to_address, naddr);
   mov_m32_imm32((unsigned int*)(&PC), (unsigned int)(dst+1));
   mov_reg32_imm32(EAX, (unsigned int)jump_to_func);
   call_reg32(EAX);
#endif
#endif
}

void genjal_idle(void)
{
#ifdef INTERPRET_JAL_IDLE
   gencallinterp((native_type)cached_interpreter_table.JAL_IDLE, 1);
#else
   if (((dst->addr & 0xFFF) == 0xFFC && 
            (dst->addr < 0x80000000 || dst->addr >= 0xC0000000))||no_compiled_jump)
   {
      gencallinterp((native_type)cached_interpreter_table.JAL_IDLE, 1);
      return;
   }

#ifdef __x86_64__
   mov_xreg32_m32rel(EAX, (unsigned int *)(&next_interupt));
   sub_xreg32_m32rel(EAX, (unsigned int *)(&g_cp0_regs[CP0_COUNT_REG]));
   cmp_reg32_imm8(EAX, 3);
   jbe_rj(12);

   and_eax_imm32(0xFFFFFFFC);  // 5
   add_m32rel_xreg32((unsigned int *)(&g_cp0_regs[CP0_COUNT_REG]), EAX); // 7
#else
   mov_eax_memoffs32((unsigned int *)(&next_interupt));
   sub_reg32_m32(EAX, (unsigned int *)(&g_cp0_regs[CP0_COUNT_REG]));
   cmp_reg32_imm8(EAX, 3);
   jbe_rj(11);

   and_eax_imm32(0xFFFFFFFC);
   add_m32_reg32((unsigned int *)(&g_cp0_regs[CP0_COUNT_REG]), EAX);
#endif

   genjal();
#endif
}

void gentest(void)
{
#ifdef __x86_64__
   cmp_m32rel_imm32((unsigned int *)(&branch_taken), 0);
   je_near_rj(0);
   jump_start_rel32();

   mov_m32rel_imm32((void*)(&last_addr), dst->addr + (dst-1)->f.i.immediate*4);
   gencheck_interupt((unsigned long long) (dst + (dst-1)->f.i.immediate));
   jmp(dst->addr + (dst-1)->f.i.immediate*4);

   jump_end_rel32();

   mov_m32rel_imm32((void*)(&last_addr), dst->addr + 4);
#else
   cmp_m32_imm32((unsigned int *)(&branch_taken), 0);
   je_near_rj(0);

   jump_start_rel32();

   mov_m32_imm32(&last_addr, dst->addr + (dst-1)->f.i.immediate*4);
   gencheck_interupt((unsigned int)(dst + (dst-1)->f.i.immediate));
   jmp(dst->addr + (dst-1)->f.i.immediate*4);

   jump_end_rel32();

   mov_m32_imm32(&last_addr, dst->addr + 4);
#endif
   gencheck_interupt((native_type)(dst + 1));
   jmp(dst->addr + 4);
}

void genbeq(void)
{
#ifdef INTERPRET_BEQ
   gencallinterp((native_type)cached_interpreter_table.BEQ, 1);
#else
   if (((dst->addr & 0xFFF) == 0xFFC && 
            (dst->addr < 0x80000000 || dst->addr >= 0xC0000000))||no_compiled_jump)
   {
      gencallinterp((native_type)cached_interpreter_table.BEQ, 1);
      return;
   }

   genbeq_test();
   gendelayslot();
   gentest();
#endif
}

void gentest_out(void)
{
#ifdef __x86_64__
   cmp_m32rel_imm32((unsigned int *)(&branch_taken), 0);
   je_near_rj(0);
   jump_start_rel32();

   mov_m32rel_imm32((void*)(&last_addr), dst->addr + (dst-1)->f.i.immediate*4);
   gencheck_interupt_out(dst->addr + (dst-1)->f.i.immediate*4);
   mov_m32rel_imm32(&jump_to_address, dst->addr + (dst-1)->f.i.immediate*4);
   mov_reg64_imm64(RAX, (unsigned long long) (dst+1));
   mov_m64rel_xreg64((unsigned long long *)(&PC), RAX);
   mov_reg64_imm64(RAX, (unsigned long long) jump_to_func);
   call_reg64(RAX);
   jump_end_rel32();

   mov_m32rel_imm32((void*)(&last_addr), dst->addr + 4);
#else
   cmp_m32_imm32((unsigned int *)(&branch_taken), 0);
   je_near_rj(0);

   jump_start_rel32();

   mov_m32_imm32(&last_addr, dst->addr + (dst-1)->f.i.immediate*4);
   gencheck_interupt_out(dst->addr + (dst-1)->f.i.immediate*4);
   mov_m32_imm32(&jump_to_address, dst->addr + (dst-1)->f.i.immediate*4);
   mov_m32_imm32((unsigned int*)(&PC), (unsigned int)(dst+1));
   mov_reg32_imm32(EAX, (unsigned int)jump_to_func);
   call_reg32(EAX);

   jump_end_rel32();

   mov_m32_imm32(&last_addr, dst->addr + 4);
#endif
   gencheck_interupt((native_type) (dst + 1));
   jmp(dst->addr + 4);
}

void genbeq_out(void)
{
#ifdef INTERPRET_BEQ_OUT
   gencallinterp((native_type)cached_interpreter_table.BEQ_OUT, 1);
#else
   if (((dst->addr & 0xFFF) == 0xFFC && 
            (dst->addr < 0x80000000 || dst->addr >= 0xC0000000))||no_compiled_jump)
   {
      gencallinterp((native_type)cached_interpreter_table.BEQ_OUT, 1);
      return;
   }

   genbeq_test();
   gendelayslot();
   gentest_out();
#endif
}

void gentest_idle(void)
{
   int reg;

   reg = lru_register();
   free_register(reg);

#ifdef __x86_64__
   cmp_m32rel_imm32((unsigned int *)(&branch_taken), 0);
   je_near_rj(0);
   jump_start_rel32();

   mov_xreg32_m32rel(reg, (unsigned int *)(&next_interupt));
   sub_xreg32_m32rel(reg, (unsigned int *)(&g_cp0_regs[CP0_COUNT_REG]));
   cmp_reg32_imm8(reg, 3);
   jbe_rj(0);
   jump_start_rel8();

   and_reg32_imm32(reg, 0xFFFFFFFC);
   add_m32rel_xreg32((unsigned int *)(&g_cp0_regs[CP0_COUNT_REG]), reg);

   jump_end_rel8();
#else
   cmp_m32_imm32((unsigned int *)(&branch_taken), 0);
   je_near_rj(0);

   jump_start_rel32();

   mov_reg32_m32(reg, (unsigned int *)(&next_interupt));
   sub_reg32_m32(reg, (unsigned int *)(&g_cp0_regs[CP0_COUNT_REG]));
   cmp_reg32_imm8(reg, 5);
   jbe_rj(18);

   sub_reg32_imm32(reg, 2); // 6
   and_reg32_imm32(reg, 0xFFFFFFFC); // 6
   add_m32_reg32((unsigned int *)(&g_cp0_regs[CP0_COUNT_REG]), reg); // 6
#endif
   jump_end_rel32();
}

void genbeq_idle(void)
{
#ifdef INTERPRET_BEQ_IDLE
   gencallinterp((native_type)cached_interpreter_table.BEQ_IDLE, 1);
#else
   if (((dst->addr & 0xFFF) == 0xFFC && 
            (dst->addr < 0x80000000 || dst->addr >= 0xC0000000))||no_compiled_jump)
   {
      gencallinterp((native_type)cached_interpreter_table.BEQ_IDLE, 1);
      return;
   }

   genbeq_test();
   gentest_idle();
   genbeq();
#endif
}

void genbne(void)
{
#ifdef INTERPRET_BNE
   gencallinterp((native_type)cached_interpreter_table.BNE, 1);
#else
   if (((dst->addr & 0xFFF) == 0xFFC && 
            (dst->addr < 0x80000000 || dst->addr >= 0xC0000000))||no_compiled_jump)
   {
      gencallinterp((native_type)cached_interpreter_table.BNE, 1);
      return;
   }

   genbne_test();
   gendelayslot();
   gentest();
#endif
}

void genbne_out(void)
{
#ifdef INTERPRET_BNE_OUT
   gencallinterp((native_type)cached_interpreter_table.BNE_OUT, 1);
#else
   if (((dst->addr & 0xFFF) == 0xFFC && 
            (dst->addr < 0x80000000 || dst->addr >= 0xC0000000))||no_compiled_jump)
   {
      gencallinterp((native_type)cached_interpreter_table.BNE_OUT, 1);
      return;
   }

   genbne_test();
   gendelayslot();
   gentest_out();
#endif
}

void genbne_idle(void)
{
#ifdef INTERPRET_BNE_IDLE
   gencallinterp((native_type)cached_interpreter_table.BNE_IDLE, 1);
#else
   if (((dst->addr & 0xFFF) == 0xFFC && 
            (dst->addr < 0x80000000 || dst->addr >= 0xC0000000))||no_compiled_jump)
   {
      gencallinterp((native_type)cached_interpreter_table.BNE_IDLE, 1);
      return;
   }

   genbne_test();
   gentest_idle();
   genbne();
#endif
}

void genblez(void)
{
#ifdef INTERPRET_BLEZ
   gencallinterp((native_type)cached_interpreter_table.BLEZ, 1);
#else
   if (((dst->addr & 0xFFF) == 0xFFC && 
            (dst->addr < 0x80000000 || dst->addr >= 0xC0000000))||no_compiled_jump)
   {
      gencallinterp((native_type)cached_interpreter_table.BLEZ, 1);
      return;
   }

   genblez_test();
   gendelayslot();
   gentest();
#endif
}

void genblez_out(void)
{
#ifdef INTERPRET_BLEZ_OUT
   gencallinterp((native_type)cached_interpreter_table.BLEZ_OUT, 1);
#else
   if (((dst->addr & 0xFFF) == 0xFFC && 
            (dst->addr < 0x80000000 || dst->addr >= 0xC0000000))||no_compiled_jump)
   {
      gencallinterp((native_type)cached_interpreter_table.BLEZ_OUT, 1);
      return;
   }

   genblez_test();
   gendelayslot();
   gentest_out();
#endif
}

void genblez_idle(void)
{
#ifdef INTERPRET_BLEZ_IDLE
   gencallinterp((native_type)cached_interpreter_table.BLEZ_IDLE, 1);
#else
   if (((dst->addr & 0xFFF) == 0xFFC && 
            (dst->addr < 0x80000000 || dst->addr >= 0xC0000000))||no_compiled_jump)
   {
      gencallinterp((native_type)cached_interpreter_table.BLEZ_IDLE, 1);
      return;
   }

   genblez_test();
   gentest_idle();
   genblez();
#endif
}

void genbgtz(void)
{
#ifdef INTERPRET_BGTZ
   gencallinterp((native_type)cached_interpreter_table.BGTZ, 1);
#else
   if (((dst->addr & 0xFFF) == 0xFFC && 
            (dst->addr < 0x80000000 || dst->addr >= 0xC0000000))||no_compiled_jump)
   {
      gencallinterp((native_type)cached_interpreter_table.BGTZ, 1);
      return;
   }

   genbgtz_test();
   gendelayslot();
   gentest();
#endif
}

void genbgtz_out(void)
{
#ifdef INTERPRET_BGTZ_OUT
   gencallinterp((native_type)cached_interpreter_table.BGTZ_OUT, 1);
#else
   if (((dst->addr & 0xFFF) == 0xFFC && 
            (dst->addr < 0x80000000 || dst->addr >= 0xC0000000))||no_compiled_jump)
   {
      gencallinterp((native_type)cached_interpreter_table.BGTZ_OUT, 1);
      return;
   }

   genbgtz_test();
   gendelayslot();
   gentest_out();
#endif
}

void genbgtz_idle(void)
{
#ifdef INTERPRET_BGTZ_IDLE
   gencallinterp((native_type)cached_interpreter_table.BGTZ_IDLE, 1);
#else
   if (((dst->addr & 0xFFF) == 0xFFC && 
            (dst->addr < 0x80000000 || dst->addr >= 0xC0000000))||no_compiled_jump)
   {
      gencallinterp((native_type)cached_interpreter_table.BGTZ_IDLE, 1);
      return;
   }

   genbgtz_test();
   gentest_idle();
   genbgtz();
#endif
}

void genaddi(void)
{
#ifdef INTERPRET_ADDI
   gencallinterp((native_type)cached_interpreter_table.ADDI, 0);
#else
#ifdef __x86_64__
   int rs = allocate_register_32((unsigned int *)dst->f.i.rs);
   int rt = allocate_register_32_w((unsigned int *)dst->f.i.rt);
#else
   int rs = allocate_register((unsigned int *)dst->f.i.rs);
   int rt = allocate_register_w((unsigned int *)dst->f.i.rt);
#endif

   mov_reg32_reg32(rt, rs);
   add_reg32_imm32(rt,(int)dst->f.i.immediate);
#endif
}

void genaddiu(void)
{
#ifdef INTERPRET_ADDIU
   gencallinterp((native_type)cached_interpreter_table.ADDIU, 0);
#else
#ifdef __x86_64__
   int rs = allocate_register_32((unsigned int *)dst->f.i.rs);
   int rt = allocate_register_32_w((unsigned int *)dst->f.i.rt);
#else
   int rs = allocate_register((unsigned int *)dst->f.i.rs);
   int rt = allocate_register_w((unsigned int *)dst->f.i.rt);
#endif

   mov_reg32_reg32(rt, rs);
   add_reg32_imm32(rt,(int)dst->f.i.immediate);
#endif
}

void genslti(void)
{
#ifdef INTERPRET_SLTI
   gencallinterp((native_type)cached_interpreter_table.SLTI, 0);
#else
#ifdef __x86_64__
   int rs = allocate_register_64((unsigned long long *) dst->f.i.rs);
   int rt = allocate_register_64_w((unsigned long long *) dst->f.i.rt);
   int imm = (int) dst->f.i.immediate;

   cmp_reg64_imm32(rs, imm);
   setl_reg8(rt);
   and_reg64_imm8(rt, 1);
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
#endif
}

void gensltiu(void)
{
#ifdef INTERPRET_SLTIU
   gencallinterp((native_type)cached_interpreter_table.SLTIU, 0);
#else
#ifdef __x86_64__
   int rs = allocate_register_64((unsigned long long *)dst->f.i.rs);
   int rt = allocate_register_64_w((unsigned long long *)dst->f.i.rt);
   int imm = (int) dst->f.i.immediate;

   cmp_reg64_imm32(rs, imm);
   setb_reg8(rt);
   and_reg64_imm8(rt, 1);
#else
   int rs1 = allocate_64_register1((unsigned int *)dst->f.i.rs);
   int rs2 = allocate_64_register2((unsigned int *)dst->f.i.rs);
   int rt = allocate_register_w((unsigned int *)dst->f.i.rt);
   long long imm = (long long)dst->f.i.immediate;

   cmp_reg32_imm32(rs2, (unsigned int)(imm >> 32));
   jb_rj(17);
   jne_rj(8); // 2
   cmp_reg32_imm32(rs1, (unsigned int)imm); // 6
   jb_rj(7); // 2
   mov_reg32_imm32(rt, 0); // 5
   jmp_imm_short(5); // 2
   mov_reg32_imm32(rt, 1); // 5
#endif
#endif
}

void genandi(void)
{
#ifdef INTERPRET_ANDI
   gencallinterp((native_type)cached_interpreter_table.ANDI, 0);
#else
#ifdef __x86_64__
   int rs = allocate_register_64((unsigned long long *)dst->f.i.rs);
   int rt = allocate_register_64_w((unsigned long long *)dst->f.i.rt);

   mov_reg64_reg64(rt, rs);
   and_reg64_imm32(rt, (unsigned short)dst->f.i.immediate);
#else
   int rs = allocate_register((unsigned int *)dst->f.i.rs);
   int rt = allocate_register_w((unsigned int *)dst->f.i.rt);

   mov_reg32_reg32(rt, rs);
   and_reg32_imm32(rt, (unsigned short)dst->f.i.immediate);
#endif
#endif
}

void genori(void)
{
#ifdef INTERPRET_ORI
   gencallinterp((native_type)cached_interpreter_table.ORI, 0);
#else
#ifdef __x86_64__
   int rs = allocate_register_64((unsigned long long *) dst->f.i.rs);
   int rt = allocate_register_64_w((unsigned long long *) dst->f.i.rt);

   mov_reg64_reg64(rt, rs);
   or_reg64_imm32(rt, (unsigned short)dst->f.i.immediate);
#else
   int rs1 = allocate_64_register1((unsigned int *)dst->f.i.rs);
   int rs2 = allocate_64_register2((unsigned int *)dst->f.i.rs);
   int rt1 = allocate_64_register1_w((unsigned int *)dst->f.i.rt);
   int rt2 = allocate_64_register2_w((unsigned int *)dst->f.i.rt);

   mov_reg32_reg32(rt1, rs1);
   mov_reg32_reg32(rt2, rs2);
   or_reg32_imm32(rt1, (unsigned short)dst->f.i.immediate);
#endif
#endif
}

void genxori(void)
{
#ifdef INTERPRET_XORI
   gencallinterp((native_type)cached_interpreter_table.XORI, 0);
#else

#ifdef __x86_64__
   int rs = allocate_register_64((unsigned long long *)dst->f.i.rs);
   int rt = allocate_register_64_w((unsigned long long *)dst->f.i.rt);

   mov_reg64_reg64(rt, rs);
   xor_reg64_imm32(rt, (unsigned short)dst->f.i.immediate);
#else
   int rs1 = allocate_64_register1((unsigned int *)dst->f.i.rs);
   int rs2 = allocate_64_register2((unsigned int *)dst->f.i.rs);
   int rt1 = allocate_64_register1_w((unsigned int *)dst->f.i.rt);
   int rt2 = allocate_64_register2_w((unsigned int *)dst->f.i.rt);

   mov_reg32_reg32(rt1, rs1);
   mov_reg32_reg32(rt2, rs2);
   xor_reg32_imm32(rt1, (unsigned short)dst->f.i.immediate);
#endif
#endif
}

void genlui(void)
{
#ifdef INTERPRET_LUI
   gencallinterp((native_type)cached_interpreter_table.LUI, 0);
#else
#ifdef __x86_64__
   int rt = allocate_register_32_w((unsigned int *)dst->f.i.rt);
#else
   int rt = allocate_register_w((unsigned int *)dst->f.i.rt);
#endif

   mov_reg32_imm32(rt, (unsigned int)dst->f.i.immediate << 16);
#endif
}

void gentestl(void)
{
#ifdef __x86_64__
   cmp_m32rel_imm32((unsigned int *)(&branch_taken), 0);
   je_near_rj(0);
   jump_start_rel32();

   gendelayslot();
   mov_m32rel_imm32((void*)(&last_addr), dst->addr + (dst-1)->f.i.immediate*4);
   gencheck_interupt((unsigned long long) (dst + (dst-1)->f.i.immediate));
   jmp(dst->addr + (dst-1)->f.i.immediate*4);

   jump_end_rel32();

   genupdate_count(dst->addr-4);
   mov_m32rel_imm32((void*)(&last_addr), dst->addr + 4);
#else
   cmp_m32_imm32((unsigned int *)(&branch_taken), 0);
   je_near_rj(0);

   jump_start_rel32();

   gendelayslot();
   mov_m32_imm32(&last_addr, dst->addr + (dst-1)->f.i.immediate*4);
   gencheck_interupt((unsigned int)(dst + (dst-1)->f.i.immediate));
   jmp(dst->addr + (dst-1)->f.i.immediate*4);

   jump_end_rel32();

   genupdate_count(dst->addr+4);
   mov_m32_imm32(&last_addr, dst->addr + 4);
#endif

   gencheck_interupt((native_type) (dst + 1));
   jmp(dst->addr + 4);
}

void genbeql(void)
{
#ifdef INTERPRET_BEQL
   gencallinterp((native_type)cached_interpreter_table.BEQL, 1);
#else
   if (((dst->addr & 0xFFF) == 0xFFC && 
            (dst->addr < 0x80000000 || dst->addr >= 0xC0000000))||no_compiled_jump)
   {
      gencallinterp((native_type)cached_interpreter_table.BEQL, 1);
      return;
   }

   genbeq_test();
   free_all_registers();
   gentestl();
#endif
}

void gentestl_out(void)
{
#ifdef __x86_64__
   cmp_m32rel_imm32((unsigned int *)(&branch_taken), 0);
   je_near_rj(0);
   jump_start_rel32();

   gendelayslot();
   mov_m32rel_imm32((void*)(&last_addr), dst->addr + (dst-1)->f.i.immediate*4);
   gencheck_interupt_out(dst->addr + (dst-1)->f.i.immediate*4);
   mov_m32rel_imm32(&jump_to_address, dst->addr + (dst-1)->f.i.immediate*4);

   mov_reg64_imm64(RAX, (unsigned long long) (dst+1));
   mov_m64rel_xreg64((unsigned long long *)(&PC), RAX);
   mov_reg64_imm64(RAX, (unsigned long long) jump_to_func);
   call_reg64(RAX);

   jump_end_rel32();

   genupdate_count(dst->addr-4);
   mov_m32rel_imm32((void*)(&last_addr), dst->addr + 4);
#else
   cmp_m32_imm32((unsigned int *)(&branch_taken), 0);
   je_near_rj(0);

   jump_start_rel32();

   gendelayslot();
   mov_m32_imm32(&last_addr, dst->addr + (dst-1)->f.i.immediate*4);
   gencheck_interupt_out(dst->addr + (dst-1)->f.i.immediate*4);
   mov_m32_imm32(&jump_to_address, dst->addr + (dst-1)->f.i.immediate*4);
   mov_m32_imm32((unsigned int*)(&PC), (unsigned int)(dst+1));
   mov_reg32_imm32(EAX, (unsigned int)jump_to_func);
   call_reg32(EAX);

   jump_end_rel32();

   genupdate_count(dst->addr+4);
   mov_m32_imm32(&last_addr, dst->addr + 4);
#endif
   gencheck_interupt((native_type)(dst + 1));
   jmp(dst->addr + 4);
}

void genbeql_out(void)
{
#ifdef INTERPRET_BEQL_OUT
   gencallinterp((native_type)cached_interpreter_table.BEQL_OUT, 1);
#else
   if (((dst->addr & 0xFFF) == 0xFFC && 
            (dst->addr < 0x80000000 || dst->addr >= 0xC0000000))||no_compiled_jump)
   {
      gencallinterp((native_type)cached_interpreter_table.BEQL_OUT, 1);
      return;
   }

   genbeq_test();
   free_all_registers();
   gentestl_out();
#endif
}

void genbeql_idle(void)
{
#ifdef INTERPRET_BEQL_IDLE
   gencallinterp((native_type)cached_interpreter_table.BEQL_IDLE, 1);
#else
   if (((dst->addr & 0xFFF) == 0xFFC && 
            (dst->addr < 0x80000000 || dst->addr >= 0xC0000000))||no_compiled_jump)
   {
      gencallinterp((native_type)cached_interpreter_table.BEQL_IDLE, 1);
      return;
   }

   genbeq_test();
   gentest_idle();
   genbeql();
#endif
}

void genbnel(void)
{
#ifdef INTERPRET_BNEL
   gencallinterp((native_type)cached_interpreter_table.BNEL, 1);
#else
   if (((dst->addr & 0xFFF) == 0xFFC && 
            (dst->addr < 0x80000000 || dst->addr >= 0xC0000000))||no_compiled_jump)
   {
      gencallinterp((native_type)cached_interpreter_table.BNEL, 1);
      return;
   }

   genbne_test();
   free_all_registers();
   gentestl();
#endif
}

void genbnel_out(void)
{
#ifdef INTERPRET_BNEL_OUT
   gencallinterp((native_type)cached_interpreter_table.BNEL_OUT, 1);
#else
   if (((dst->addr & 0xFFF) == 0xFFC && 
            (dst->addr < 0x80000000 || dst->addr >= 0xC0000000))||no_compiled_jump)
   {
      gencallinterp((native_type)cached_interpreter_table.BNEL_OUT, 1);
      return;
   }

   genbne_test();
   free_all_registers();
   gentestl_out();
#endif
}

void genbnel_idle(void)
{
#ifdef INTERPRET_BNEL_IDLE
   gencallinterp((native_type)cached_interpreter_table.BNEL_IDLE, 1);
#else
   if (((dst->addr & 0xFFF) == 0xFFC && 
            (dst->addr < 0x80000000 || dst->addr >= 0xC0000000))||no_compiled_jump)
   {
      gencallinterp((native_type)cached_interpreter_table.BNEL_IDLE, 1);
      return;
   }

   genbne_test();
   gentest_idle();
   genbnel();
#endif
}

void genblezl(void)
{
#ifdef INTERPRET_BLEZL
   gencallinterp((native_type)cached_interpreter_table.BLEZL, 1);
#else
   if (((dst->addr & 0xFFF) == 0xFFC && 
            (dst->addr < 0x80000000 || dst->addr >= 0xC0000000))||no_compiled_jump)
   {
      gencallinterp((native_type)cached_interpreter_table.BLEZL, 1);
      return;
   }

   genblez_test();
   free_all_registers();
   gentestl();
#endif
}

void genblezl_out(void)
{
#ifdef INTERPRET_BLEZL_OUT
   gencallinterp((native_type)cached_interpreter_table.BLEZL_OUT, 1);
#else
   if (((dst->addr & 0xFFF) == 0xFFC && 
            (dst->addr < 0x80000000 || dst->addr >= 0xC0000000))||no_compiled_jump)
   {
      gencallinterp((native_type)cached_interpreter_table.BLEZL_OUT, 1);
      return;
   }

   genblez_test();
   free_all_registers();
   gentestl_out();
#endif
}

void genblezl_idle(void)
{
#ifdef INTERPRET_BLEZL_IDLE
   gencallinterp((native_type)cached_interpreter_table.BLEZL_IDLE, 1);
#else
   if (((dst->addr & 0xFFF) == 0xFFC && 
            (dst->addr < 0x80000000 || dst->addr >= 0xC0000000))||no_compiled_jump)
   {
      gencallinterp((native_type)cached_interpreter_table.BLEZL_IDLE, 1);
      return;
   }

   genblez_test();
   gentest_idle();
   genblezl();
#endif
}

void genbgtzl(void)
{
#ifdef INTERPRET_BGTZL
   gencallinterp((native_type)cached_interpreter_table.BGTZL, 1);
#else
   if (((dst->addr & 0xFFF) == 0xFFC && 
            (dst->addr < 0x80000000 || dst->addr >= 0xC0000000))||no_compiled_jump)
   {
      gencallinterp((native_type)cached_interpreter_table.BGTZL, 1);
      return;
   }

   genbgtz_test();
   free_all_registers();
   gentestl();
#endif
}

void genbgtzl_out(void)
{
#ifdef INTERPRET_BGTZL_OUT
   gencallinterp((native_type)cached_interpreter_table.BGTZL_OUT, 1);
#else
   if (((dst->addr & 0xFFF) == 0xFFC && 
            (dst->addr < 0x80000000 || dst->addr >= 0xC0000000))||no_compiled_jump)
   {
      gencallinterp((native_type)cached_interpreter_table.BGTZL_OUT, 1);
      return;
   }

   genbgtz_test();
   free_all_registers();
   gentestl_out();
#endif
}

void genbgtzl_idle(void)
{
#ifdef INTERPRET_BGTZL_IDLE
   gencallinterp((native_type)cached_interpreter_table.BGTZL_IDLE, 1);
#else
   if (((dst->addr & 0xFFF) == 0xFFC && 
            (dst->addr < 0x80000000 || dst->addr >= 0xC0000000))||no_compiled_jump)
   {
      gencallinterp((native_type)cached_interpreter_table.BGTZL_IDLE, 1);
      return;
   }

   genbgtz_test();
   gentest_idle();
   genbgtzl();
#endif
}

void gendaddi(void)
{
#ifdef INTERPRET_DADDI
   gencallinterp((native_type)cached_interpreter_table.DADDI, 0);
#else
#ifdef __x86_64__
   int rs = allocate_register_64((unsigned long long *)dst->f.i.rs);
   int rt = allocate_register_64_w((unsigned long long *)dst->f.i.rt);

   mov_reg64_reg64(rt, rs);
   add_reg64_imm32(rt, (int) dst->f.i.immediate);
#else
   int rs1 = allocate_64_register1((unsigned int *)dst->f.i.rs);
   int rs2 = allocate_64_register2((unsigned int *)dst->f.i.rs);
   int rt1 = allocate_64_register1_w((unsigned int *)dst->f.i.rt);
   int rt2 = allocate_64_register2_w((unsigned int *)dst->f.i.rt);

   mov_reg32_reg32(rt1, rs1);
   mov_reg32_reg32(rt2, rs2);
   add_reg32_imm32(rt1, dst->f.i.immediate);
   adc_reg32_imm32(rt2, (int)dst->f.i.immediate>>31);
#endif
#endif
}

void gendaddiu(void)
{
#ifdef INTERPRET_DADDIU
   gencallinterp((native_type)cached_interpreter_table.DADDIU, 0);
#else
#ifdef __x86_64__
   int rs = allocate_register_64((unsigned long long *)dst->f.i.rs);
   int rt = allocate_register_64_w((unsigned long long *)dst->f.i.rt);

   mov_reg64_reg64(rt, rs);
   add_reg64_imm32(rt, (int) dst->f.i.immediate);
#else
   int rs1 = allocate_64_register1((unsigned int *)dst->f.i.rs);
   int rs2 = allocate_64_register2((unsigned int *)dst->f.i.rs);
   int rt1 = allocate_64_register1_w((unsigned int *)dst->f.i.rt);
   int rt2 = allocate_64_register2_w((unsigned int *)dst->f.i.rt);

   mov_reg32_reg32(rt1, rs1);
   mov_reg32_reg32(rt2, rs2);
   add_reg32_imm32(rt1, dst->f.i.immediate);
   adc_reg32_imm32(rt2, (int)dst->f.i.immediate>>31);
#endif
#endif
}

void gencache(void)
{
}

void genldl(void)
{
#ifdef INTERPRET_LD
   gencallinterp((unsigned int)cached_interpreter_table.LD, 0);
#else
#ifdef __x86_64__
   gencallinterp((native_type)cached_interpreter_table.LDL, 0);
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
      mov_reg32_preg32x4pimm32(EAX, EAX, (unsigned int)readmemd);
      cmp_reg32_imm32(EAX, (unsigned int)read_rdramd);
   }
   je_rj(51);

   mov_m32_imm32((unsigned int *)(&PC), (unsigned int)(dst+1)); // 10
   mov_m32_reg32((unsigned int *)(&address), EBX); // 6
   mov_m32_imm32((unsigned int *)(&rdword), (unsigned int)dst->f.i.rt); // 10
   shr_reg32_imm8(EBX, 16); // 3
   mov_reg32_preg32x4pimm32(EBX, EBX, (unsigned int)readmemd); // 7
   call_reg32(EBX); // 2
   mov_eax_memoffs32((unsigned int *)(dst->f.i.rt)); // 5
   mov_reg32_m32(ECX, (unsigned int *)(dst->f.i.rt)+1); // 6
   jmp_imm_short(18); // 2

   and_reg32_imm32(EBX, 0x7FFFFF); // 6
   mov_reg32_preg32pimm32(EAX, EBX, ((unsigned int)g_rdram)+4); // 6
   mov_reg32_preg32pimm32(ECX, EBX, ((unsigned int)g_rdram)); // 6

   set_64_register_state(EAX, ECX, (unsigned int*)dst->f.i.rt, 1);
#endif
#endif
}

void genldr(void)
{
   gencallinterp((native_type)cached_interpreter_table.LDR, 0);
}

void genlb(void)
{
   int gpr1, gpr2, base1, base2 = 0;
#ifdef INTERPRET_LB
   gencallinterp((native_type)cached_interpreter_table.LB, 0);
#else

#ifdef __x86_64__
   free_registers_move_start();

   ld_register_alloc(&gpr1, &gpr2, &base1, &base2);

   mov_reg64_imm64(base1, (unsigned long long) readmemb);
   if(fast_memory)
   {
      and_reg32_imm32(gpr1, 0xDF800000);
      cmp_reg32_imm32(gpr1, 0x80000000);
   }
   else
   {
      mov_reg64_imm64(base2, (unsigned long long) read_rdramb);
      shr_reg32_imm8(gpr1, 16);
      mov_reg64_preg64x8preg64(gpr1, gpr1, base1);
      cmp_reg64_reg64(gpr1, base2);
   }
   je_rj(0);
   jump_start_rel8();

   mov_reg64_imm64(gpr1, (unsigned long long) (dst+1));
   mov_m64rel_xreg64((unsigned long long *)(&PC), gpr1);
   mov_m32rel_xreg32((unsigned int *)(&address), gpr2);
   mov_reg64_imm64(gpr1, (unsigned long long) dst->f.i.rt);
   mov_m64rel_xreg64((unsigned long long *)(&rdword), gpr1);
   shr_reg32_imm8(gpr2, 16);
   mov_reg64_preg64x8preg64(gpr2, gpr2, base1);
   call_reg64(gpr2);
   movsx_xreg32_m8rel(gpr1, (unsigned char *)dst->f.i.rt);
   jmp_imm_short(24);

   jump_end_rel8();
   mov_reg64_imm64(base1, (unsigned long long) g_rdram); // 10
   and_reg32_imm32(gpr2, 0x7FFFFF); // 6
   xor_reg8_imm8(gpr2, 3); // 4
   movsx_reg32_8preg64preg64(gpr1, gpr2, base1); // 4

   set_register_state(gpr1, (unsigned int*)dst->f.i.rt, 1, 0);
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
   je_rj(47);

   mov_m32_imm32((unsigned int *)&PC, (unsigned int)(dst+1)); // 10
   mov_m32_reg32((unsigned int *)(&address), EBX); // 6
   mov_m32_imm32((unsigned int *)(&rdword), (unsigned int)dst->f.i.rt); // 10
   shr_reg32_imm8(EBX, 16); // 3
   mov_reg32_preg32x4pimm32(EBX, EBX, (unsigned int)readmemb); // 7
   call_reg32(EBX); // 2
   movsx_reg32_m8(EAX, (unsigned char *)dst->f.i.rt); // 7
   jmp_imm_short(16); // 2

   and_reg32_imm32(EBX, 0x7FFFFF); // 6
   xor_reg8_imm8(BL, 3); // 3
   movsx_reg32_8preg32pimm32(EAX, EBX, (unsigned int)g_rdram); // 7

   set_register_state(EAX, (unsigned int*)dst->f.i.rt, 1, 0);
#endif
#endif
}

void genlh(void)
{
#ifdef INTERPRET_LH
   gencallinterp((native_type)cached_interpreter_table.LH, 0);
#else
#ifdef __x86_64__
   int gpr1, gpr2, base1, base2 = 0;
   free_registers_move_start();

   ld_register_alloc(&gpr1, &gpr2, &base1, &base2);

   mov_reg64_imm64(base1, (unsigned long long) readmemh);
   if(fast_memory)
   {
      and_reg32_imm32(gpr1, 0xDF800000);
      cmp_reg32_imm32(gpr1, 0x80000000);
   }
   else
   {
      mov_reg64_imm64(base2, (unsigned long long) read_rdramh);
      shr_reg32_imm8(gpr1, 16);
      mov_reg64_preg64x8preg64(gpr1, gpr1, base1);
      cmp_reg64_reg64(gpr1, base2);
   }
   je_rj(0);
   jump_start_rel8();

   mov_reg64_imm64(gpr1, (unsigned long long) (dst+1));
   mov_m64rel_xreg64((unsigned long long *)(&PC), gpr1);
   mov_m32rel_xreg32((unsigned int *)(&address), gpr2);
   mov_reg64_imm64(gpr1, (unsigned long long) dst->f.i.rt);
   mov_m64rel_xreg64((unsigned long long *)(&rdword), gpr1);
   shr_reg32_imm8(gpr2, 16);
   mov_reg64_preg64x8preg64(gpr2, gpr2, base1);
   call_reg64(gpr2);
   movsx_xreg32_m16rel(gpr1, (unsigned short *)dst->f.i.rt);
   jmp_imm_short(24);

   jump_end_rel8();   
   mov_reg64_imm64(base1, (unsigned long long) g_rdram); // 10
   and_reg32_imm32(gpr2, 0x7FFFFF); // 6
   xor_reg8_imm8(gpr2, 2); // 4
   movsx_reg32_16preg64preg64(gpr1, gpr2, base1); // 4

   set_register_state(gpr1, (unsigned int*)dst->f.i.rt, 1, 0);
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
   je_rj(47);

   mov_m32_imm32((unsigned int *)&PC, (unsigned int)(dst+1)); // 10
   mov_m32_reg32((unsigned int *)(&address), EBX); // 6
   mov_m32_imm32((unsigned int *)(&rdword), (unsigned int)dst->f.i.rt); // 10
   shr_reg32_imm8(EBX, 16); // 3
   mov_reg32_preg32x4pimm32(EBX, EBX, (unsigned int)readmemh); // 7
   call_reg32(EBX); // 2
   movsx_reg32_m16(EAX, (unsigned short *)dst->f.i.rt); // 7
   jmp_imm_short(16); // 2

   and_reg32_imm32(EBX, 0x7FFFFF); // 6
   xor_reg8_imm8(BL, 2); // 3
   movsx_reg32_16preg32pimm32(EAX, EBX, (unsigned int)g_rdram); // 7

   set_register_state(EAX, (unsigned int*)dst->f.i.rt, 1, 0);
#endif
#endif
}

void genlwl(void)
{
   gencallinterp((native_type)cached_interpreter_table.LWL, 0);
}

void genlw(void)
{
   int gpr1, gpr2, base1, base2 = 0;
#ifdef INTERPRET_LW
   gencallinterp((native_type)cached_interpreter_table.LW, 0);
#else
#ifdef __x86_64__
   free_registers_move_start();

   ld_register_alloc(&gpr1, &gpr2, &base1, &base2);

   mov_reg64_imm64(base1, (unsigned long long) readmem);
   if(fast_memory)
   {
      and_reg32_imm32(gpr1, 0xDF800000);
      cmp_reg32_imm32(gpr1, 0x80000000);
   }
   else
   {
      mov_reg64_imm64(base2, (unsigned long long) read_rdram);
      shr_reg32_imm8(gpr1, 16);
      mov_reg64_preg64x8preg64(gpr1, gpr1, base1);
      cmp_reg64_reg64(gpr1, base2);
   }
   jne_rj(21);

   mov_reg64_imm64(base1, (unsigned long long) g_rdram); // 10
   and_reg32_imm32(gpr2, 0x7FFFFF); // 6
   mov_reg32_preg64preg64(gpr1, gpr2, base1); // 3
   jmp_imm_short(0); // 2
   jump_start_rel8();

   mov_reg64_imm64(gpr1, (unsigned long long) (dst+1));
   mov_m64rel_xreg64((unsigned long long *)(&PC), gpr1);
   mov_m32rel_xreg32((unsigned int *)(&address), gpr2);
   mov_reg64_imm64(gpr1, (unsigned long long) dst->f.i.rt);
   mov_m64rel_xreg64((unsigned long long *)(&rdword), gpr1);
   shr_reg32_imm8(gpr2, 16);
   mov_reg64_preg64x8preg64(gpr1, gpr2, base1);
   call_reg64(gpr1);
   mov_xreg32_m32rel(gpr1, (unsigned int *)(dst->f.i.rt));

   jump_end_rel8();

   set_register_state(gpr1, (unsigned int*)dst->f.i.rt, 1, 0);     // set gpr1 state as dirty, and bound to r4300 reg RT
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

   set_register_state(EAX, (unsigned int*)dst->f.i.rt, 1, 0);
#endif
#endif
}

void genlbu(void)
{
   int gpr1, gpr2, base1, base2 = 0;
#ifdef INTERPRET_LBU
   gencallinterp((native_type)cached_interpreter_table.LBU, 0);
#else
#ifdef __x86_64__
   free_registers_move_start();

   ld_register_alloc(&gpr1, &gpr2, &base1, &base2);

   mov_reg64_imm64(base1, (unsigned long long) readmemb);
   if(fast_memory)
   {
      and_reg32_imm32(gpr1, 0xDF800000);
      cmp_reg32_imm32(gpr1, 0x80000000);
   }
   else
   {
      mov_reg64_imm64(base2, (unsigned long long) read_rdramb);
      shr_reg32_imm8(gpr1, 16);
      mov_reg64_preg64x8preg64(gpr1, gpr1, base1);
      cmp_reg64_reg64(gpr1, base2);
   }
   je_rj(0);
   jump_start_rel8();

   mov_reg64_imm64(gpr1, (unsigned long long) (dst+1));
   mov_m64rel_xreg64((unsigned long long *)(&PC), gpr1);
   mov_m32rel_xreg32((unsigned int *)(&address), gpr2);
   mov_reg64_imm64(gpr1, (unsigned long long) dst->f.i.rt);
   mov_m64rel_xreg64((unsigned long long *)(&rdword), gpr1);
   shr_reg32_imm8(gpr2, 16);
   mov_reg64_preg64x8preg64(gpr2, gpr2, base1);
   call_reg64(gpr2);
   mov_xreg32_m32rel(gpr1, (unsigned int *)dst->f.i.rt);
   jmp_imm_short(23);

   jump_end_rel8();
   mov_reg64_imm64(base1, (unsigned long long) g_rdram); // 10
   and_reg32_imm32(gpr2, 0x7FFFFF); // 6
   xor_reg8_imm8(gpr2, 3); // 4
   mov_reg32_preg64preg64(gpr1, gpr2, base1); // 3

   and_reg32_imm32(gpr1, 0xFF);
   set_register_state(gpr1, (unsigned int*)dst->f.i.rt, 1, 0);
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

   set_register_state(EAX, (unsigned int*)dst->f.i.rt, 1, 0);
#endif
#endif
}

void genlhu(void)
{
   int gpr1, gpr2, base1, base2 = 0;
#ifdef INTERPRET_LHU
   gencallinterp((native_type)cached_interpreter_table.LHU, 0);
#else
#ifdef __x86_64__
   free_registers_move_start();

   ld_register_alloc(&gpr1, &gpr2, &base1, &base2);

   mov_reg64_imm64(base1, (unsigned long long) readmemh);
   if(fast_memory)
   {
      and_reg32_imm32(gpr1, 0xDF800000);
      cmp_reg32_imm32(gpr1, 0x80000000);
   }
   else
   {
      mov_reg64_imm64(base2, (unsigned long long) read_rdramh);
      shr_reg32_imm8(gpr1, 16);
      mov_reg64_preg64x8preg64(gpr1, gpr1, base1);
      cmp_reg64_reg64(gpr1, base2);
   }
   je_rj(0);
   jump_start_rel8();

   mov_reg64_imm64(gpr1, (unsigned long long) (dst+1));
   mov_m64rel_xreg64((unsigned long long *)(&PC), gpr1);
   mov_m32rel_xreg32((unsigned int *)(&address), gpr2);
   mov_reg64_imm64(gpr1, (unsigned long long) dst->f.i.rt);
   mov_m64rel_xreg64((unsigned long long *)(&rdword), gpr1);
   shr_reg32_imm8(gpr2, 16);
   mov_reg64_preg64x8preg64(gpr2, gpr2, base1);
   call_reg64(gpr2);
   mov_xreg32_m32rel(gpr1, (unsigned int *)dst->f.i.rt);
   jmp_imm_short(23);

   jump_end_rel8();
   mov_reg64_imm64(base1, (unsigned long long) g_rdram); // 10
   and_reg32_imm32(gpr2, 0x7FFFFF); // 6
   xor_reg8_imm8(gpr2, 2); // 4
   mov_reg32_preg64preg64(gpr1, gpr2, base1); // 3

   and_reg32_imm32(gpr1, 0xFFFF);
   set_register_state(gpr1, (unsigned int*)dst->f.i.rt, 1, 0);
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

   set_register_state(EAX, (unsigned int*)dst->f.i.rt, 1, 0);
#endif
#endif
}

void genlwr(void)
{
   gencallinterp((native_type)cached_interpreter_table.LWR, 0);
}

void genlwu(void)
{
   int gpr1, gpr2, base1, base2 = 0;
#ifdef INTERPRET_LWU
   gencallinterp((native_type)cached_interpreter_table.LWU, 0);
#else
#ifdef __x86_64__
   free_registers_move_start();

   ld_register_alloc(&gpr1, &gpr2, &base1, &base2);

   mov_reg64_imm64(base1, (unsigned long long) readmem);
   if(fast_memory)
   {
      and_reg32_imm32(gpr1, 0xDF800000);
      cmp_reg32_imm32(gpr1, 0x80000000);
   }
   else
   {
      mov_reg64_imm64(base2, (unsigned long long) read_rdram);
      shr_reg32_imm8(gpr1, 16);
      mov_reg64_preg64x8preg64(gpr1, gpr1, base1);
      cmp_reg64_reg64(gpr1, base2);
   }
   je_rj(0);
   jump_start_rel8();

   mov_reg64_imm64(gpr1, (unsigned long long) (dst+1));
   mov_m64rel_xreg64((unsigned long long *)(&PC), gpr1);
   mov_m32rel_xreg32((unsigned int *)(&address), gpr2);
   mov_reg64_imm64(gpr1, (unsigned long long) dst->f.i.rt);
   mov_m64rel_xreg64((unsigned long long *)(&rdword), gpr1);
   shr_reg32_imm8(gpr2, 16);
   mov_reg64_preg64x8preg64(gpr2, gpr2, base1);
   call_reg64(gpr2);
   mov_xreg32_m32rel(gpr1, (unsigned int *)dst->f.i.rt);
   jmp_imm_short(19);

   jump_end_rel8();
   mov_reg64_imm64(base1, (unsigned long long) g_rdram); // 10
   and_reg32_imm32(gpr2, 0x7FFFFF); // 6
   mov_reg32_preg64preg64(gpr1, gpr2, base1); // 3

   set_register_state(gpr1, (unsigned int*)dst->f.i.rt, 1, 1);
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
#endif
}

void gensb(void)
{
#ifdef INTERPRET_SB
   gencallinterp((native_type)cached_interpreter_table.SB, 0);
#else
#ifdef __x86_64__
   free_registers_move_start();

   mov_xreg8_m8rel(CL, (unsigned char *)dst->f.i.rt);
   mov_xreg32_m32rel(EAX, (unsigned int *)dst->f.i.rs);
   add_eax_imm32((int)dst->f.i.immediate);
   mov_reg32_reg32(EBX, EAX);
   mov_reg64_imm64(RSI, (unsigned long long) writememb);
   if(fast_memory)
   {
      and_eax_imm32(0xDF800000);
      cmp_eax_imm32(0x80000000);
   }
   else
   {
      mov_reg64_imm64(RDI, (unsigned long long) write_rdramb);
      shr_reg32_imm8(EAX, 16);
      mov_reg64_preg64x8preg64(RAX, RAX, RSI);
      cmp_reg64_reg64(RAX, RDI);
   }
   je_rj(49);

   mov_reg64_imm64(RAX, (unsigned long long) (dst+1)); // 10
   mov_m64rel_xreg64((unsigned long long *)(&PC), RAX); // 7
   mov_m32rel_xreg32((unsigned int *)(&address), EBX); // 7
   mov_m8rel_xreg8((unsigned char *)(&cpu_byte), CL); // 7
   shr_reg32_imm8(EBX, 16); // 3
   mov_reg64_preg64x8preg64(RBX, RBX, RSI);  // 4
   call_reg64(RBX); // 2
   mov_xreg32_m32rel(EAX, (unsigned int *)(&address)); // 7
   jmp_imm_short(25); // 2

   mov_reg64_imm64(RSI, (unsigned long long) g_rdram); // 10
   mov_reg32_reg32(EAX, EBX); // 2
   and_reg32_imm32(EBX, 0x7FFFFF); // 6
   xor_reg8_imm8(BL, 3); // 4
   mov_preg64preg64_reg8(RBX, RSI, CL); // 3

   mov_reg64_imm64(RSI, (unsigned long long) invalid_code);
   mov_reg32_reg32(EBX, EAX);
   shr_reg32_imm8(EBX, 12);
   cmp_preg64preg64_imm8(RBX, RSI, 0);
   jne_rj(65);

   mov_reg64_imm64(RDI, (unsigned long long) blocks); // 10
   mov_reg32_reg32(ECX, EBX); // 2
   mov_reg64_preg64x8preg64(RBX, RBX, RDI);  // 4
   mov_reg64_preg64pimm32(RBX, RBX, (int) offsetof(precomp_block, block)); // 7
   mov_reg64_imm64(RDI, (unsigned long long) cached_interpreter_table.NOTCOMPILED); // 10
   and_eax_imm32(0xFFF); // 5
   shr_reg32_imm8(EAX, 2); // 3
   mov_reg32_imm32(EDX, sizeof(precomp_instr)); // 5
   mul_reg32(EDX); // 2
   mov_reg64_preg64preg64pimm32(RAX, RAX, RBX, (int) offsetof(precomp_instr, ops)); // 8
   cmp_reg64_reg64(RAX, RDI); // 3
   je_rj(4); // 2
   mov_preg64preg64_imm8(RCX, RSI, 1); // 4
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
#endif
}

void gensh(void)
{
#ifdef INTERPRET_SH
   gencallinterp((native_type)cached_interpreter_table.SH, 0);
#else
#if defined(__x86_64__)
   free_registers_move_start();

   mov_xreg16_m16rel(CX, (unsigned short *)dst->f.i.rt);
   mov_xreg32_m32rel(EAX, (unsigned int *)dst->f.i.rs);
   add_eax_imm32((int)dst->f.i.immediate);
   mov_reg32_reg32(EBX, EAX);
   mov_reg64_imm64(RSI, (unsigned long long) writememh);
   if(fast_memory)
   {
      and_eax_imm32(0xDF800000);
      cmp_eax_imm32(0x80000000);
   }
   else
   {
      mov_reg64_imm64(RDI, (unsigned long long) write_rdramh);
      shr_reg32_imm8(EAX, 16);
      mov_reg64_preg64x8preg64(RAX, RAX, RSI);
      cmp_reg64_reg64(RAX, RDI);
   }
   je_rj(50);

   mov_reg64_imm64(RAX, (unsigned long long) (dst+1)); // 10
   mov_m64rel_xreg64((unsigned long long *)(&PC), RAX); // 7
   mov_m32rel_xreg32((unsigned int *)(&address), EBX); // 7
   mov_m16rel_xreg16((unsigned short *)(&hword), CX); // 8
   shr_reg32_imm8(EBX, 16); // 3
   mov_reg64_preg64x8preg64(RBX, RBX, RSI);  // 4
   call_reg64(RBX); // 2
   mov_xreg32_m32rel(EAX, (unsigned int *)(&address)); // 7
   jmp_imm_short(26); // 2

   mov_reg64_imm64(RSI, (unsigned long long) g_rdram); // 10
   mov_reg32_reg32(EAX, EBX); // 2
   and_reg32_imm32(EBX, 0x7FFFFF); // 6
   xor_reg8_imm8(BL, 2); // 4
   mov_preg64preg64_reg16(RBX, RSI, CX); // 4

   mov_reg64_imm64(RSI, (unsigned long long) invalid_code);
   mov_reg32_reg32(EBX, EAX);
   shr_reg32_imm8(EBX, 12);
   cmp_preg64preg64_imm8(RBX, RSI, 0);
   jne_rj(65);

   mov_reg64_imm64(RDI, (unsigned long long) blocks); // 10
   mov_reg32_reg32(ECX, EBX); // 2
   mov_reg64_preg64x8preg64(RBX, RBX, RDI);  // 4
   mov_reg64_preg64pimm32(RBX, RBX, (int) offsetof(precomp_block, block)); // 7
   mov_reg64_imm64(RDI, (unsigned long long) cached_interpreter_table.NOTCOMPILED); // 10
   and_eax_imm32(0xFFF); // 5
   shr_reg32_imm8(EAX, 2); // 3
   mov_reg32_imm32(EDX, sizeof(precomp_instr)); // 5
   mul_reg32(EDX); // 2
   mov_reg64_preg64preg64pimm32(RAX, RAX, RBX, (int) offsetof(precomp_instr, ops)); // 8
   cmp_reg64_reg64(RAX, RDI); // 3
   je_rj(4); // 2
   mov_preg64preg64_imm8(RCX, RSI, 1); // 4
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
#endif
}

void genswl(void)
{
   gencallinterp((native_type)cached_interpreter_table.SWL, 0);
}

void gensw(void)
{
#ifdef INTERPRET_SW
   gencallinterp((native_type)cached_interpreter_table.SW, 0);
#else
#ifdef __x86_64__
   free_registers_move_start();

   mov_xreg32_m32rel(ECX, (unsigned int *)dst->f.i.rt);
   mov_xreg32_m32rel(EAX, (unsigned int *)dst->f.i.rs);
   add_eax_imm32((int)dst->f.i.immediate);
   mov_reg32_reg32(EBX, EAX);
   mov_reg64_imm64(RSI, (unsigned long long) writemem);
   if(fast_memory)
   {
      and_eax_imm32(0xDF800000);
      cmp_eax_imm32(0x80000000);
   }
   else
   {
      mov_reg64_imm64(RDI, (unsigned long long) write_rdram);
      shr_reg32_imm8(EAX, 16);
      mov_reg64_preg64x8preg64(RAX, RAX, RSI);
      cmp_reg64_reg64(RAX, RDI);
   }
   je_rj(49);

   mov_reg64_imm64(RAX, (unsigned long long) (dst+1)); // 10
   mov_m64rel_xreg64((unsigned long long *)(&PC), RAX); // 7
   mov_m32rel_xreg32((unsigned int *)(&address), EBX); // 7
   mov_m32rel_xreg32((unsigned int *)(&word), ECX); // 7
   shr_reg32_imm8(EBX, 16); // 3
   mov_reg64_preg64x8preg64(RBX, RBX, RSI);  // 4
   call_reg64(RBX); // 2
   mov_xreg32_m32rel(EAX, (unsigned int *)(&address)); // 7
   jmp_imm_short(21); // 2

   mov_reg64_imm64(RSI, (unsigned long long) g_rdram); // 10
   mov_reg32_reg32(EAX, EBX); // 2
   and_reg32_imm32(EBX, 0x7FFFFF); // 6
   mov_preg64preg64_reg32(RBX, RSI, ECX); // 3

   mov_reg64_imm64(RSI, (unsigned long long) invalid_code);
   mov_reg32_reg32(EBX, EAX);
   shr_reg32_imm8(EBX, 12);
   cmp_preg64preg64_imm8(RBX, RSI, 0);
   jne_rj(65);

   mov_reg64_imm64(RDI, (unsigned long long) blocks); // 10
   mov_reg32_reg32(ECX, EBX); // 2
   mov_reg64_preg64x8preg64(RBX, RBX, RDI);  // 4
   mov_reg64_preg64pimm32(RBX, RBX, (int) offsetof(precomp_block, block)); // 7
   mov_reg64_imm64(RDI, (unsigned long long) cached_interpreter_table.NOTCOMPILED); // 10
   and_eax_imm32(0xFFF); // 5
   shr_reg32_imm8(EAX, 2); // 3
   mov_reg32_imm32(EDX, sizeof(precomp_instr)); // 5
   mul_reg32(EDX); // 2
   mov_reg64_preg64preg64pimm32(RAX, RAX, RBX, (int) offsetof(precomp_instr, ops)); // 8
   cmp_reg64_reg64(RAX, RDI); // 3
   je_rj(4); // 2
   mov_preg64preg64_imm8(RCX, RSI, 1); // 4
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
#endif
}

void gensdl(void)
{
   gencallinterp((native_type)cached_interpreter_table.SDL, 0);
}

void gensdr(void)
{
   gencallinterp((native_type)cached_interpreter_table.SDR, 0);
}

void genswr(void)
{
   gencallinterp((native_type)cached_interpreter_table.SWR, 0);
}

void gencheck_cop1_unusable(void)
{
#ifdef __x86_64__
   free_registers_move_start();

   test_m32rel_imm32((unsigned int*)&g_cp0_regs[CP0_STATUS_REG], 0x20000000);
#else
   free_all_registers();
   simplify_access();
   test_m32_imm32((unsigned int*)&g_cp0_regs[CP0_STATUS_REG], 0x20000000);
#endif
   jne_rj(0);
   jump_start_rel8();

   gencallinterp((native_type)check_cop1_unusable, 0);

   jump_end_rel8();
}

void genlwc1(void)
{
#ifdef INTERPRET_LWC1
   gencallinterp((native_type)cached_interpreter_table.LWC1, 0);
#else
   gencheck_cop1_unusable();

#ifdef __x86_64__
   mov_xreg32_m32rel(EAX, (unsigned int *)(&reg[dst->f.lf.base]));
   add_eax_imm32((int)dst->f.lf.offset);
   mov_reg32_reg32(EBX, EAX);
   mov_reg64_imm64(RSI, (unsigned long long) readmem);
   if(fast_memory)
   {
      and_eax_imm32(0xDF800000);
      cmp_eax_imm32(0x80000000);
   }
   else
   {
      mov_reg64_imm64(RDI, (unsigned long long) read_rdram);
      shr_reg32_imm8(EAX, 16);
      mov_reg64_preg64x8preg64(RAX, RAX, RSI);
      cmp_reg64_reg64(RAX, RDI);
   }
   je_rj(49);

   mov_reg64_imm64(RAX, (unsigned long long) (dst+1)); // 10
   mov_m64rel_xreg64((unsigned long long *)(&PC), RAX); // 7
   mov_m32rel_xreg32((unsigned int *)(&address), EBX); // 7
   mov_xreg64_m64rel(RDX, (unsigned long long *)(&reg_cop1_simple[dst->f.lf.ft])); // 7
   mov_m64rel_xreg64((unsigned long long *)(&rdword), RDX); // 7
   shr_reg32_imm8(EBX, 16); // 3
   mov_reg64_preg64x8preg64(RBX, RBX, RSI);  // 4
   call_reg64(RBX); // 2
   jmp_imm_short(28); // 2

   mov_reg64_imm64(RSI, (unsigned long long) g_rdram); // 10
   and_reg32_imm32(EBX, 0x7FFFFF); // 6
   mov_reg32_preg64preg64(EAX, RBX, RSI); // 3
   mov_xreg64_m64rel(RBX, (unsigned long long *)(&reg_cop1_simple[dst->f.lf.ft])); // 7
   mov_preg64_reg32(RBX, EAX); // 2
#else
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
      mov_reg32_preg32x4pimm32(EAX, EAX, (unsigned int)readmem);
      cmp_reg32_imm32(EAX, (unsigned int)read_rdram);
   }
   je_rj(42);

   mov_m32_imm32((unsigned int *)(&PC), (unsigned int)(dst+1)); // 10
   mov_m32_reg32((unsigned int *)(&address), EBX); // 6
   mov_reg32_m32(EDX, (unsigned int*)(&reg_cop1_simple[dst->f.lf.ft])); // 6
   mov_m32_reg32((unsigned int *)(&rdword), EDX); // 6
   shr_reg32_imm8(EBX, 16); // 3
   mov_reg32_preg32x4pimm32(EBX, EBX, (unsigned int)readmem); // 7
   call_reg32(EBX); // 2
   jmp_imm_short(20); // 2

   and_reg32_imm32(EBX, 0x7FFFFF); // 6
   mov_reg32_preg32pimm32(EAX, EBX, (unsigned int)g_rdram); // 6
   mov_reg32_m32(EBX, (unsigned int*)(&reg_cop1_simple[dst->f.lf.ft])); // 6
   mov_preg32_reg32(EBX, EAX); // 2
#endif
#endif
}

void genldc1(void)
{
#ifdef INTERPRET_LDC1
   gencallinterp((native_type)cached_interpreter_table.LDC1, 0);
#else
   gencheck_cop1_unusable();

#ifdef __x86_64__
   mov_xreg32_m32rel(EAX, (unsigned int *)(&reg[dst->f.lf.base]));
   add_eax_imm32((int)dst->f.lf.offset);
   mov_reg32_reg32(EBX, EAX);
   mov_reg64_imm64(RSI, (unsigned long long) readmemd);
   if(fast_memory)
   {
      and_eax_imm32(0xDF800000);
      cmp_eax_imm32(0x80000000);
   }
   else
   {
      mov_reg64_imm64(RDI, (unsigned long long) read_rdramd);
      shr_reg32_imm8(EAX, 16);
      mov_reg64_preg64x8preg64(RAX, RAX, RSI);
      cmp_reg64_reg64(RAX, RDI);
   }
   je_rj(49);

   mov_reg64_imm64(RAX, (unsigned long long) (dst+1)); // 10
   mov_m64rel_xreg64((unsigned long long *)(&PC), RAX); // 7
   mov_m32rel_xreg32((unsigned int *)(&address), EBX); // 7
   mov_xreg64_m64rel(RDX, (unsigned long long *)(&reg_cop1_double[dst->f.lf.ft])); // 7
   mov_m64rel_xreg64((unsigned long long *)(&rdword), RDX); // 7
   shr_reg32_imm8(EBX, 16); // 3
   mov_reg64_preg64x8preg64(RBX, RBX, RSI);  // 4
   call_reg64(RBX); // 2
   jmp_imm_short(39); // 2

   mov_reg64_imm64(RSI, (unsigned long long) g_rdram); // 10
   and_reg32_imm32(EBX, 0x7FFFFF); // 6
   mov_reg64_preg64preg64(RAX, RBX, RSI); // 4
   mov_xreg64_m64rel(RBX, (unsigned long long *)(&reg_cop1_double[dst->f.lf.ft])); // 7
   mov_preg64pimm32_reg32(RBX, 4, EAX); // 6
   shr_reg64_imm8(RAX, 32); // 4
   mov_preg64_reg32(RBX, EAX); // 2
#else
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
#endif
}

void genld(void)
{
#if defined(INTERPRET_LD) || !defined(__x86_64__)
   gencallinterp((native_type)cached_interpreter_table.LD, 0);
#else
   free_registers_move_start();

   mov_xreg32_m32rel(EAX, (unsigned int *)dst->f.i.rs);
   add_eax_imm32((int)dst->f.i.immediate);
   mov_reg32_reg32(EBX, EAX);
   mov_reg64_imm64(RSI, (unsigned long long) readmemd);
   if(fast_memory)
   {
      and_eax_imm32(0xDF800000);
      cmp_eax_imm32(0x80000000);
   }
   else
   {
      mov_reg64_imm64(RDI, (unsigned long long) read_rdramd);
      shr_reg32_imm8(EAX, 16);
      mov_reg64_preg64x8preg64(RAX, RAX, RSI);
      cmp_reg64_reg64(RAX, RDI);
   }
   je_rj(59);

   mov_reg64_imm64(RAX, (unsigned long long) (dst+1)); // 10
   mov_m64rel_xreg64((unsigned long long *)(&PC), RAX); // 7
   mov_m32rel_xreg32((unsigned int *)(&address), EBX); // 7
   mov_reg64_imm64(RAX, (unsigned long long) dst->f.i.rt); // 10
   mov_m64rel_xreg64((unsigned long long *)(&rdword), RAX); // 7
   shr_reg32_imm8(EBX, 16); // 3
   mov_reg64_preg64x8preg64(RBX, RBX, RSI);  // 4
   call_reg64(RBX); // 2
   mov_xreg64_m64rel(RAX, (unsigned long long *)(dst->f.i.rt)); // 7
   jmp_imm_short(33); // 2

   mov_reg64_imm64(RSI, (unsigned long long) g_rdram); // 10
   and_reg32_imm32(EBX, 0x7FFFFF); // 6

   mov_reg32_preg64preg64(EAX, RBX, RSI); // 3
   mov_reg32_preg64preg64pimm32(EBX, RBX, RSI, 4); // 7
   shl_reg64_imm8(RAX, 32); // 4
   or_reg64_reg64(RAX, RBX); // 3

   set_register_state(RAX, (unsigned int*)dst->f.i.rt, 1, 1);
#endif
}

void genswc1(void)
{
#ifdef INTERPRET_SWC1
   gencallinterp((native_type)cached_interpreter_table.SWC1, 0);
#else
   gencheck_cop1_unusable();

#ifdef __x86_64__
   mov_xreg64_m64rel(RDX, (unsigned long long *)(&reg_cop1_simple[dst->f.lf.ft]));
   mov_reg32_preg64(ECX, RDX);
   mov_xreg32_m32rel(EAX, (unsigned int *)(&reg[dst->f.lf.base]));
   add_eax_imm32((int)dst->f.lf.offset);
   mov_reg32_reg32(EBX, EAX);
   mov_reg64_imm64(RSI, (unsigned long long) writemem);
   if(fast_memory)
   {
      and_eax_imm32(0xDF800000);
      cmp_eax_imm32(0x80000000);
   }
   else
   {
      mov_reg64_imm64(RDI, (unsigned long long) write_rdram);
      shr_reg32_imm8(EAX, 16);
      mov_reg64_preg64x8preg64(RAX, RAX, RSI);
      cmp_reg64_reg64(RAX, RDI);
   }
   je_rj(49);

   mov_reg64_imm64(RAX, (unsigned long long) (dst+1)); // 10
   mov_m64rel_xreg64((unsigned long long *)(&PC), RAX); // 7
   mov_m32rel_xreg32((unsigned int *)(&address), EBX); // 7
   mov_m32rel_xreg32((unsigned int *)(&word), ECX); // 7
   shr_reg32_imm8(EBX, 16); // 3
   mov_reg64_preg64x8preg64(RBX, RBX, RSI);  // 4
   call_reg64(RBX); // 2
   mov_xreg32_m32rel(EAX, (unsigned int *)(&address)); // 7
   jmp_imm_short(21); // 2

   mov_reg64_imm64(RSI, (unsigned long long) g_rdram); // 10
   mov_reg32_reg32(EAX, EBX); // 2
   and_reg32_imm32(EBX, 0x7FFFFF); // 6
   mov_preg64preg64_reg32(RBX, RSI, ECX); // 3

   mov_reg64_imm64(RSI, (unsigned long long) invalid_code);
   mov_reg32_reg32(EBX, EAX);
   shr_reg32_imm8(EBX, 12);
   cmp_preg64preg64_imm8(RBX, RSI, 0);
   jne_rj(65);

   mov_reg64_imm64(RDI, (unsigned long long) blocks); // 10
   mov_reg32_reg32(ECX, EBX); // 2
   mov_reg64_preg64x8preg64(RBX, RBX, RDI);  // 4
   mov_reg64_preg64pimm32(RBX, RBX, (int) offsetof(precomp_block, block)); // 7
   mov_reg64_imm64(RDI, (unsigned long long) cached_interpreter_table.NOTCOMPILED); // 10
   and_eax_imm32(0xFFF); // 5
   shr_reg32_imm8(EAX, 2); // 3
   mov_reg32_imm32(EDX, sizeof(precomp_instr)); // 5
   mul_reg32(EDX); // 2
   mov_reg64_preg64preg64pimm32(RAX, RAX, RBX, (int) offsetof(precomp_instr, ops)); // 8
   cmp_reg64_reg64(RAX, RDI); // 3
   je_rj(4); // 2
   mov_preg64preg64_imm8(RCX, RSI, 1); // 4
#else
   mov_reg32_m32(EDX, (unsigned int*)(&reg_cop1_simple[dst->f.lf.ft]));
   mov_reg32_preg32(ECX, EDX);
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
#endif
}

void gensdc1(void)
{
#ifdef INTERPRET_SDC1
   gencallinterp((native_type)cached_interpreter_table.SDC1, 0);
#else
   gencheck_cop1_unusable();

#ifdef __x86_64__
   mov_xreg64_m64rel(RSI, (unsigned long long *)(&reg_cop1_double[dst->f.lf.ft]));
   mov_reg32_preg64(ECX, RSI);
   mov_reg32_preg64pimm32(EDX, RSI, 4);
   mov_xreg32_m32rel(EAX, (unsigned int *)(&reg[dst->f.lf.base]));
   add_eax_imm32((int)dst->f.lf.offset);
   mov_reg32_reg32(EBX, EAX);
   mov_reg64_imm64(RSI, (unsigned long long) writememd);
   if(fast_memory)
   {
      and_eax_imm32(0xDF800000);
      cmp_eax_imm32(0x80000000);
   }
   else
   {
      mov_reg64_imm64(RDI, (unsigned long long) write_rdramd);
      shr_reg32_imm8(EAX, 16);
      mov_reg64_preg64x8preg64(RAX, RAX, RSI);
      cmp_reg64_reg64(RAX, RDI);
   }
   je_rj(56);

   mov_reg64_imm64(RAX, (unsigned long long) (dst+1)); // 10
   mov_m64rel_xreg64((unsigned long long *)(&PC), RAX); // 7
   mov_m32rel_xreg32((unsigned int *)(&address), EBX); // 7
   mov_m32rel_xreg32((unsigned int *)(&dword), ECX); // 7
   mov_m32rel_xreg32((unsigned int *)(&dword)+1, EDX); // 7
   shr_reg32_imm8(EBX, 16); // 3
   mov_reg64_preg64x8preg64(RBX, RBX, RSI);  // 4
   call_reg64(RBX); // 2
   mov_xreg32_m32rel(EAX, (unsigned int *)(&address)); // 7
   jmp_imm_short(28); // 2

   mov_reg64_imm64(RSI, (unsigned long long) g_rdram); // 10
   mov_reg32_reg32(EAX, EBX); // 2
   and_reg32_imm32(EBX, 0x7FFFFF); // 6
   mov_preg64preg64pimm32_reg32(RBX, RSI, 4, ECX); // 7
   mov_preg64preg64_reg32(RBX, RSI, EDX); // 3

   mov_reg64_imm64(RSI, (unsigned long long) invalid_code);
   mov_reg32_reg32(EBX, EAX);
   shr_reg32_imm8(EBX, 12);
   cmp_preg64preg64_imm8(RBX, RSI, 0);
   jne_rj(65);

   mov_reg64_imm64(RDI, (unsigned long long) blocks); // 10
   mov_reg32_reg32(ECX, EBX); // 2
   mov_reg64_preg64x8preg64(RBX, RBX, RDI);  // 4
   mov_reg64_preg64pimm32(RBX, RBX, (int) offsetof(precomp_block, block)); // 7
   mov_reg64_imm64(RDI, (unsigned long long) cached_interpreter_table.NOTCOMPILED); // 10
   and_eax_imm32(0xFFF); // 5
   shr_reg32_imm8(EAX, 2); // 3
   mov_reg32_imm32(EDX, sizeof(precomp_instr)); // 5
   mul_reg32(EDX); // 2
   mov_reg64_preg64preg64pimm32(RAX, RAX, RBX, (int) offsetof(precomp_instr, ops)); // 8
   cmp_reg64_reg64(RAX, RDI); // 3
   je_rj(4); // 2
   mov_preg64preg64_imm8(RCX, RSI, 1); // 4
#else
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
#endif
}

void gensd(void)
{
#ifdef INTERPRET_SD
   gencallinterp((native_type)cached_interpreter_table.SD, 0);
#else

#ifdef __x86_64__
   free_registers_move_start();

   mov_xreg32_m32rel(ECX, (unsigned int *)dst->f.i.rt);
   mov_xreg32_m32rel(EDX, ((unsigned int *)dst->f.i.rt)+1);
   mov_xreg32_m32rel(EAX, (unsigned int *)dst->f.i.rs);
   add_eax_imm32((int)dst->f.i.immediate);
   mov_reg32_reg32(EBX, EAX);
   mov_reg64_imm64(RSI, (unsigned long long) writememd);
   if(fast_memory)
   {
      and_eax_imm32(0xDF800000);
      cmp_eax_imm32(0x80000000);
   }
   else
   {
      mov_reg64_imm64(RDI, (unsigned long long) write_rdramd);
      shr_reg32_imm8(EAX, 16);
      mov_reg64_preg64x8preg64(RAX, RAX, RSI);
      cmp_reg64_reg64(RAX, RDI);
   }
   je_rj(56);

   mov_reg64_imm64(RAX, (unsigned long long) (dst+1)); // 10
   mov_m64rel_xreg64((unsigned long long *)(&PC), RAX); // 7
   mov_m32rel_xreg32((unsigned int *)(&address), EBX); // 7
   mov_m32rel_xreg32((unsigned int *)(&dword), ECX); // 7
   mov_m32rel_xreg32((unsigned int *)(&dword)+1, EDX); // 7
   shr_reg32_imm8(EBX, 16); // 3
   mov_reg64_preg64x8preg64(RBX, RBX, RSI);  // 4
   call_reg64(RBX); // 2
   mov_xreg32_m32rel(EAX, (unsigned int *)(&address)); // 7
   jmp_imm_short(28); // 2

   mov_reg64_imm64(RSI, (unsigned long long) g_rdram); // 10
   mov_reg32_reg32(EAX, EBX); // 2
   and_reg32_imm32(EBX, 0x7FFFFF); // 6
   mov_preg64preg64pimm32_reg32(RBX, RSI, 4, ECX); // 7
   mov_preg64preg64_reg32(RBX, RSI, EDX); // 3

   mov_reg64_imm64(RSI, (unsigned long long) invalid_code);
   mov_reg32_reg32(EBX, EAX);
   shr_reg32_imm8(EBX, 12);
   cmp_preg64preg64_imm8(RBX, RSI, 0);
   jne_rj(65);

   mov_reg64_imm64(RDI, (unsigned long long) blocks); // 10
   mov_reg32_reg32(ECX, EBX); // 2
   mov_reg64_preg64x8preg64(RBX, RBX, RDI);  // 4
   mov_reg64_preg64pimm32(RBX, RBX, (int) offsetof(precomp_block, block)); // 7
   mov_reg64_imm64(RDI, (unsigned long long) cached_interpreter_table.NOTCOMPILED); // 10
   and_eax_imm32(0xFFF); // 5
   shr_reg32_imm8(EAX, 2); // 3
   mov_reg32_imm32(EDX, sizeof(precomp_instr)); // 5
   mul_reg32(EDX); // 2
   mov_reg64_preg64preg64pimm32(RAX, RAX, RBX, (int) offsetof(precomp_instr, ops)); // 8
   cmp_reg64_reg64(RAX, RDI); // 3
   je_rj(4); // 2
   mov_preg64preg64_imm8(RCX, RSI, 1); // 4
#else
   free_all_registers();
   simplify_access();

   mov_reg32_m32(ECX, (unsigned int *)dst->f.i.rt);
   mov_reg32_m32(EDX, ((unsigned int *)dst->f.i.rt)+1);
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
#endif
}

void genll(void)
{
   gencallinterp((native_type)cached_interpreter_table.LL, 0);
}

void gensc(void)
{
   gencallinterp((native_type)cached_interpreter_table.SC, 0);
}
