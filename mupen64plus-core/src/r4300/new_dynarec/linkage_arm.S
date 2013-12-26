/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - linkage_arm.s                                           *
 *   Copyright (C) 2009-2011 Ari64                                         *
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
/*	.cpu arm9tdmi
	.fpu softvfp
	.eabi_attribute 20, 1
	.eabi_attribute 21, 1
	.eabi_attribute 23, 3
	.eabi_attribute 24, 1
	.eabi_attribute 25, 1
	.eabi_attribute 26, 2
	.eabi_attribute 30, 6
	.eabi_attribute 18, 4
	.file	"linkage_arm.S"*/

/* global function/external symbol */
#ifndef __MACH__
#define ESYM(name) name

#define FUNCTION(name) \
  .align 2; \
  .globl name; \
  .type name, %function; \
  name

#define EXTRA_UNSAVED_REGS

#else
#define ESYM(name) _##name

#define FUNCTION(name) \
  .align 2; \
  .globl ESYM(name); \
  name: \
  ESYM(name)

#define EXTRA_UNSAVED_REGS r9,

#endif

#include "linkage_offsets.h"

#ifdef __MACH__
#define extra_memory ESYM(extra_memory)
#define add_link ESYM(add_link)
#define check_interupt ESYM(check_interupt)
#define clean_blocks ESYM(clean_blocks)
#define dynarec_local ESYM(dynarec_local)
#define gen_interupt ESYM(gen_interupt)
#define get_addr ESYM(get_addr)
#define get_addr_32 ESYM(get_addr_32)
#define get_addr_ht ESYM(get_addr_ht)
#define invalidate_block ESYM(invalidate_block)
#define new_recompile_block ESYM(new_recompile_block)
#endif

	.bss
	.align	12
	.global extra_memory
	.type	extra_memory, %object
	.size	extra_memory, 33554432
extra_memory:
	.space	33554432

	.global dynarec_local
	.type	dynarec_local, %object
	.size	dynarec_local, 64+16+16+8+8+8+8+256+8+8+128+128+128+16+8+132+4+256+512+4194304
dynarec_local:
        .space 64+16+16+8+8+8+8+256+8+8+128+128+128+16+8+132+4+256+512+4194304


#define DRC_VAR_(name, vname, size_) \
	vname = dynarec_local + LO_##name; \
	.global vname; \
	.type	vname, %object; \
	.size	vname, size_

#define DRC_VAR(name, size_) \
	DRC_VAR_(name, ESYM(name), size_)

	DRC_VAR(next_interupt, 4)
	DRC_VAR(cycle_count, 4)
	DRC_VAR(last_count, 4)
	DRC_VAR(pending_exception, 4)
	DRC_VAR(pcaddr, 4)
	DRC_VAR(stop, 4)
	DRC_VAR(invc_ptr, 4)
	DRC_VAR(address, 4)
	DRC_VAR(readmem_dword, 8)
	DRC_VAR(dword, 8)
	DRC_VAR(word, 4)
	DRC_VAR(hword, 2)
	DRC_VAR(cpu_byte, 1) /* 1 byte free */
	DRC_VAR(cpu_byte_two, 1) /* padding */
	DRC_VAR(FCR0, 4)
	DRC_VAR(FCR31, 4)
	DRC_VAR(reg, 256)
	DRC_VAR(hi, 8)
	DRC_VAR(lo, 8)
	DRC_VAR(reg_cop0, 128)
	DRC_VAR(reg_cop1_simple, 128)
	DRC_VAR(reg_cop1_double, 128)
	DRC_VAR(rounding_modes, 16)
	DRC_VAR(branch_target, 4)
	DRC_VAR(PC, 4)
	DRC_VAR(fake_pc, 132)
	DRC_VAR(ram_offset, 4)
	DRC_VAR(mini_ht, 256)
	DRC_VAR(restore_candidate, 512)
	DRC_VAR(memory_map, 4194304)

#ifdef __MACH__
	.data
	.align 2
ptr_jump_in:            .word	ESYM(jump_in)
ptr_jump_dirty:	        .word	ESYM(jump_dirty)
ptr_tlb_LUT_r:          .word	ESYM(tlb_LUT_r)
ptr_hash_table:         .word	ESYM(hash_table)
ptr_out:                .word   ESYM(out)
#endif


        .syntax unified
	.text

.macro load_varadr reg var
#if defined(__ARM_ARCH_7A__) && !defined(__PIC__)
	movw	\reg, #:lower16:\var
	movt	\reg, #:upper16:\var
#elif defined(__ARM_ARCH_7A__) && defined(__MACH__)
	movw	\reg, #:lower16:(\var-(1678f+8))
	movt	\reg, #:upper16:(\var-(1678f+8))
1678:
	add	\reg, pc
#else
	ldr	\reg, =\var
#endif
.endm

.macro load_varadr_ext reg var
#if defined(__ARM_ARCH_7A__) && defined(__MACH__) && defined(__PIC__)
	movw	\reg, #:lower16:(ptr_\var-(1678f+8))
	movt	\reg, #:upper16:(ptr_\var-(1678f+8))
1678:
	ldr	\reg, [pc, \reg]
#else
	load_varadr \reg \var
#endif
.endm


FUNCTION(dyna_linker):
	/* r0 = virtual target address */
	/* r1 = instruction to patch */
	load_varadr_ext	r4, tlb_LUT_r
	lsr	r5, r0, #12
	mov	r12, r0
	cmp	r0, #0xC0000000
	mov	r6, #4096
	ldrge	r12, [r4, r5, lsl #2]
	mov	r2, #0x80000
	load_varadr_ext	r3, jump_in
	tst	r12, r12
	sub	r6, r6, #1
	moveq	r12, r0
	ldr	r7, [r1]
	eor	r2, r2, r12, lsr #12
	and	r6, r6, r12, lsr #12
	cmp	r2, #2048
	add	r12, r7, #2
	orrcs	r2, r6, #2048
	ldr	r5, [r3, r2, lsl #2]
	lsl	r12, r12, #8
	/* jump_in lookup */
.A1:
	movs	r4, r5
	beq	.A3
	ldr	r3, [r5]
	ldr	r5, [r4, #12]
	teq	r3, r0
	bne	.A1
	ldr	r3, [r4, #4]
	ldr	r4, [r4, #8]
	tst	r3, r3
	bne	.A1
.A2:
	mov	r5, r1
	add	r1, r1, r12, asr #6
	teq	r1, r4
	moveq	pc, r4 /* Stale i-cache */
	bl	add_link
	sub	r2, r4, r5
	and	r1, r7, #0xff000000
	lsl	r2, r2, #6
	sub	r1, r1, #2
	add	r1, r1, r2, lsr #8
	str	r1, [r5]
	mov	pc, r4
.A3:
	/* hash_table lookup */
	cmp	r2, #2048
	load_varadr_ext	r3, jump_dirty
	eor	r4, r0, r0, lsl #16
	lslcc	r2, r0, #9
	load_varadr_ext	r6, hash_table
	lsr	r4, r4, #12
	lsrcc	r2, r2, #21
	bic	r4, r4, #15
	ldr	r5, [r3, r2, lsl #2]
	ldr	r7, [r6, r4]!
	teq	r7, r0
	ldreq	pc, [r6, #4]
	ldr	r7, [r6, #8]
	teq	r7, r0
	ldreq	pc, [r6, #12]
	/* jump_dirty lookup */
.A6:
	movs	r4, r5
	beq	.A8
	ldr	r3, [r5]
	ldr	r5, [r4, #12]
	teq	r3, r0
	bne	.A6
.A7:
	ldr	r1, [r4, #8]
	/* hash_table insert */
	ldr	r2, [r6]
	ldr	r3, [r6, #4]
	str	r0, [r6]
	str	r1, [r6, #4]
	str	r2, [r6, #8]
	str	r3, [r6, #12]
	mov	pc, r1
.A8:
	mov	r4, r0
	mov	r5, r1
	bl	new_recompile_block
	tst	r0, r0
	mov	r0, r4
	mov	r1, r5
	beq	dyna_linker
	/* pagefault */
	mov	r1, r0
	mov	r2, #8
	.size	dyna_linker, .-dyna_linker

FUNCTION(exec_pagefault):
	/* r0 = instruction pointer */
	/* r1 = fault address */
	/* r2 = cause */
	ldr	r3, [fp, #LO_reg_cop0+48-LO_dynarec_local] /* Status */
	mvn	r6, #0xF000000F
	ldr	r4, [fp, #LO_reg_cop0+16-LO_dynarec_local] /* Context */
	bic	r6, r6, #0x0F800000
	str	r0, [fp, #LO_reg_cop0+56-LO_dynarec_local] /* EPC */
	orr	r3, r3, #2
	str	r1, [fp, #LO_reg_cop0+32-LO_dynarec_local] /* BadVAddr */
	bic	r4, r4, r6
	str	r3, [fp, #LO_reg_cop0+48-LO_dynarec_local] /* Status */
	and	r5, r6, r1, lsr #9
	str	r2, [fp, #LO_reg_cop0+52-LO_dynarec_local] /* Cause */
	and	r1, r1, r6, lsl #9
	str	r1, [fp, #LO_reg_cop0+40-LO_dynarec_local] /* EntryHi */
	orr	r4, r4, r5
	str	r4, [fp, #LO_reg_cop0+16-LO_dynarec_local] /* Context */
	mov	r0, #0x80000000
	bl	get_addr_ht
	mov	pc, r0
	.size	exec_pagefault, .-exec_pagefault

/* Special dynamic linker for the case where a page fault
   may occur in a branch delay slot */
FUNCTION(dyna_linker_ds):
	/* r0 = virtual target address */
	/* r1 = instruction to patch */
	load_varadr_ext	r4, tlb_LUT_r
	lsr	r5, r0, #12
	mov	r12, r0
	cmp	r0, #0xC0000000
	mov	r6, #4096
	ldrge	r12, [r4, r5, lsl #2]
	mov	r2, #0x80000
	load_varadr_ext	r3, jump_in
	tst	r12, r12
	sub	r6, r6, #1
	moveq	r12, r0
	ldr	r7, [r1]
	eor	r2, r2, r12, lsr #12
	and	r6, r6, r12, lsr #12
	cmp	r2, #2048
	add	r12, r7, #2
	orrcs	r2, r6, #2048
	ldr	r5, [r3, r2, lsl #2]
	lsl	r12, r12, #8
	/* jump_in lookup */
.B1:
	movs	r4, r5
	beq	.B3
	ldr	r3, [r5]
	ldr	r5, [r4, #12]
	teq	r3, r0
	bne	.B1
	ldr	r3, [r4, #4]
	ldr	r4, [r4, #8]
	tst	r3, r3
	bne	.B1
.B2:
	mov	r5, r1
	add	r1, r1, r12, asr #6
	teq	r1, r4
	moveq	pc, r4 /* Stale i-cache */
	bl	add_link
	sub	r2, r4, r5
	and	r1, r7, #0xff000000
	lsl	r2, r2, #6
	sub	r1, r1, #2
	add	r1, r1, r2, lsr #8
	str	r1, [r5]
	mov	pc, r4
.B3:
	/* hash_table lookup */
	cmp	r2, #2048
	load_varadr_ext	r3, jump_dirty
	eor	r4, r0, r0, lsl #16
	lslcc	r2, r0, #9
	load_varadr_ext	r6, hash_table
	lsr	r4, r4, #12
	lsrcc	r2, r2, #21
	bic	r4, r4, #15
	ldr	r5, [r3, r2, lsl #2]
	ldr	r7, [r6, r4]!
	teq	r7, r0
	ldreq	pc, [r6, #4]
	ldr	r7, [r6, #8]
	teq	r7, r0
	ldreq	pc, [r6, #12]
	/* jump_dirty lookup */
.B6:
	movs	r4, r5
	beq	.B8
	ldr	r3, [r5]
	ldr	r5, [r4, #12]
	teq	r3, r0
	bne	.B6
.B7:
	ldr	r1, [r4, #8]
	/* hash_table insert */
	ldr	r2, [r6]
	ldr	r3, [r6, #4]
	str	r0, [r6]
	str	r1, [r6, #4]
	str	r2, [r6, #8]
	str	r3, [r6, #12]
	mov	pc, r1
.B8:
	mov	r4, r0
	bic	r0, r0, #7
	mov	r5, r1
	orr	r0, r0, #1
	bl	new_recompile_block
	tst	r0, r0
	mov	r0, r4
	mov	r1, r5
	beq	dyna_linker_ds
	/* pagefault */
	bic	r1, r0, #7
	mov	r2, #0x80000008 /* High bit set indicates pagefault in delay slot */
	sub	r0, r1, #4
	b	exec_pagefault
	.size	dyna_linker_ds, .-dyna_linker_ds

FUNCTION(jump_vaddr_r0):
	eor	r2, r0, r0, lsl #16
	b	jump_vaddr
	.size	jump_vaddr_r0, .-jump_vaddr_r0

FUNCTION(jump_vaddr_r1):
	eor	r2, r1, r1, lsl #16
	mov	r0, r1
	b	jump_vaddr
	.size	jump_vaddr_r1, .-jump_vaddr_r1

FUNCTION(jump_vaddr_r2):
	mov	r0, r2
	eor	r2, r2, r2, lsl #16
	b	jump_vaddr
	.size	jump_vaddr_r2, .-jump_vaddr_r2

FUNCTION(jump_vaddr_r3):
	eor	r2, r3, r3, lsl #16
	mov	r0, r3
	b	jump_vaddr
	.size	jump_vaddr_r3, .-jump_vaddr_r3

FUNCTION(jump_vaddr_r4):
	eor	r2, r4, r4, lsl #16
	mov	r0, r4
	b	jump_vaddr
	.size	jump_vaddr_r4, .-jump_vaddr_r4

FUNCTION(jump_vaddr_r5):
	eor	r2, r5, r5, lsl #16
	mov	r0, r5
	b	jump_vaddr
	.size	jump_vaddr_r5, .-jump_vaddr_r5

FUNCTION(jump_vaddr_r6):
	eor	r2, r6, r6, lsl #16
	mov	r0, r6
	b	jump_vaddr
	.size	jump_vaddr_r6, .-jump_vaddr_r6

FUNCTION(jump_vaddr_r8):
	eor	r2, r8, r8, lsl #16
	mov	r0, r8
	b	jump_vaddr
	.size	jump_vaddr_r8, .-jump_vaddr_r8

FUNCTION(jump_vaddr_r9):
	eor	r2, r9, r9, lsl #16
	mov	r0, r9
	b	jump_vaddr
	.size	jump_vaddr_r9, .-jump_vaddr_r9

FUNCTION(jump_vaddr_r10):
	eor	r2, r10, r10, lsl #16
	mov	r0, r10
	b	jump_vaddr
	.size	jump_vaddr_r10, .-jump_vaddr_r10

FUNCTION(jump_vaddr_r12):
	eor	r2, r12, r12, lsl #16
	mov	r0, r12
	b	jump_vaddr
	.size	jump_vaddr_r12, .-jump_vaddr_r12

FUNCTION(jump_vaddr_r7):
	eor	r2, r7, r7, lsl #16
	add	r0, r7, #0
	.size	jump_vaddr_r7, .-jump_vaddr_r7

FUNCTION(jump_vaddr):
	load_varadr_ext	r1, hash_table
	mvn	r3, #15
	and	r2, r3, r2, lsr #12
	ldr	r2, [r1, r2]!
	teq	r2, r0
	ldreq	pc, [r1, #4]
	ldr	r2, [r1, #8]
	teq	r2, r0
	ldreq	pc, [r1, #12]
	str	r10, [fp, #LO_cycle_count-LO_dynarec_local]
	bl	get_addr
	ldr	r10, [fp, #LO_cycle_count-LO_dynarec_local]
	mov	pc, r0
	.size	jump_vaddr, .-jump_vaddr

FUNCTION(verify_code_ds): // Unused?
	str	r8, [fp, #LO_branch_target-LO_dynarec_local]
	.size	verify_code_ds, .-verify_code_ds

FUNCTION(verify_code_vm):
	/* r0 = instruction pointer (virtual address) */
	/* r1 = source (virtual address) */
	/* r2 = target */
	/* r3 = length */
	cmp	r1, #0xC0000000
	blt	verify_code
	add	r12, fp, #LO_memory_map-LO_dynarec_local
	lsr	r4, r1, #12
	add	r5, r1, r3
	sub	r5, #1
	ldr	r6, [r12, r4, lsl #2]
	lsr	r5, r5, #12
	movs	r7, r6
	bmi	.D5
	add	r1, r1, r6, lsl #2
	lsl	r6, r6, #2
.D1:
	add	r4, r4, #1
	teq	r6, r7, lsl #2
	bne	.D5
	ldr	r7, [r12, r4, lsl #2]
	cmp	r4, r5
	bls	.D1
	.size	verify_code_vm, .-verify_code_vm

FUNCTION(verify_code):
	/* r1 = source */
	/* r2 = target */
	/* r3 = length */
	tst	r3, #4
	mov	r4, #0
	add	r3, r1, r3
	mov	r5, #0
	ldrne	r4, [r1], #4
	mov	r12, #0
	ldrne	r5, [r2], #4
	teq	r1, r3
	beq	.D3
.D2:
	ldr	r7, [r1], #4
	eor	r9, r4, r5
	ldr	r8, [r2], #4
	orrs	r9, r9, r12
	bne	.D4
	ldr	r4, [r1], #4
	eor	r12, r7, r8
	ldr	r5, [r2], #4
	cmp	r1, r3
	bcc	.D2
	teq	r7, r8
.D3:
	teqeq	r4, r5
.D4:
	ldr	r8, [fp, #LO_branch_target-LO_dynarec_local]
	moveq	pc, lr
.D5:
	bl	get_addr
	mov	pc, r0
	.size	verify_code, .-verify_code


FUNCTION(cc_interrupt):
	ldr	r0, [fp, #LO_last_count-LO_dynarec_local]
	mov	r1, #0
	mov	r2, #0x1fc
	add	r10, r0, r10
	str	r1, [fp, #LO_pending_exception-LO_dynarec_local]
	and	r2, r2, r10, lsr #17
	add	r3, fp, #LO_restore_candidate-LO_dynarec_local
	str	r10, [fp, #LO_reg_cop0+36-LO_dynarec_local] /* Count */
	ldr	r4, [r2, r3]
	mov	r10, lr
	tst	r4, r4
	bne	.E4
.E1:
	bl	gen_interupt
	mov	lr, r10
	ldr	r10, [fp, #LO_reg_cop0+36-LO_dynarec_local] /* Count */
	ldr	r0, [fp, #LO_next_interupt-LO_dynarec_local]
	ldr	r1, [fp, #LO_pending_exception-LO_dynarec_local]
	ldr	r2, [fp, #LO_stop-LO_dynarec_local]
	str	r0, [fp, #LO_last_count-LO_dynarec_local]
	sub	r10, r10, r0
	tst	r2, r2
	bne	.E3
	tst	r1, r1
	moveq	pc, lr
.E2:
	ldr	r0, [fp, #LO_pcaddr-LO_dynarec_local]
	bl	get_addr_ht
	mov	pc, r0
.E3:
	add	r12, fp, #28
	ldmia	r12, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.E4:
	/* Move 'dirty' blocks to the 'clean' list */
	lsl	r5, r2, #3
	str	r1, [r2, r3]
.E5:
	lsrs	r4, r4, #1
	mov	r0, r5
	add	r5, r5, #1
	blcs	clean_blocks
	tst	r5, #31
	bne	.E5
	b	.E1
	.size	cc_interrupt, .-cc_interrupt

FUNCTION(do_interrupt):
	ldr	r0, [fp, #LO_pcaddr-LO_dynarec_local]
	bl	get_addr_ht
	ldr	r1, [fp, #LO_next_interupt-LO_dynarec_local]
	ldr	r10, [fp, #LO_reg_cop0+36-LO_dynarec_local] /* Count */
	str	r1, [fp, #LO_last_count-LO_dynarec_local]
	sub	r10, r10, r1
	add	r10, r10, #2
	mov	pc, r0
	.size	do_interrupt, .-do_interrupt

FUNCTION(fp_exception):
	mov	r2, #0x10000000
.E7:
	ldr	r1, [fp, #LO_reg_cop0+48-LO_dynarec_local] /* Status */
	mov	r3, #0x80000000
	str	r0, [fp, #LO_reg_cop0+56-LO_dynarec_local] /* EPC */
	orr	r1, #2
	add	r2, r2, #0x2c
	str	r1, [fp, #LO_reg_cop0+48-LO_dynarec_local] /* Status */
	str	r2, [fp, #LO_reg_cop0+52-LO_dynarec_local] /* Cause */
	add	r0, r3, #0x180
	bl	get_addr_ht
	mov	pc, r0
	.size	fp_exception, .-fp_exception

FUNCTION(fp_exception_ds):
	mov	r2, #0x90000000 /* Set high bit if delay slot */
	b	.E7
	.size	fp_exception_ds, .-fp_exception_ds

FUNCTION(jump_syscall):
	ldr	r1, [fp, #LO_reg_cop0+48-LO_dynarec_local] /* Status */
	mov	r3, #0x80000000
	str	r0, [fp, #LO_reg_cop0+56-LO_dynarec_local] /* EPC */
	orr	r1, #2
	mov	r2, #0x20
	str	r1, [fp, #LO_reg_cop0+48-LO_dynarec_local] /* Status */
	str	r2, [fp, #LO_reg_cop0+52-LO_dynarec_local] /* Cause */
	add	r0, r3, #0x180
	bl	get_addr_ht
	mov	pc, r0
	.size	jump_syscall, .-jump_syscall

FUNCTION(indirect_jump_indexed):
	ldr	r0, [r0, r1, lsl #2]
	.size	indirect_jump_indexed, .-indirect_jump_indexed

FUNCTION(indirect_jump):
	ldr	r12, [fp, #LO_last_count-LO_dynarec_local]
	add	r2, r2, r12 
	str	r2, [fp, #LO_reg_cop0+36-LO_dynarec_local] /* Count */
	mov	pc, r0
	.size	indirect_jump, .-indirect_jump

FUNCTION(jump_eret):
	ldr	r1, [fp, #LO_reg_cop0+48-LO_dynarec_local] /* Status */
	ldr	r0, [fp, #LO_last_count-LO_dynarec_local]
	bic	r1, r1, #2
	add	r10, r0, r10
	str	r1, [fp, #LO_reg_cop0+48-LO_dynarec_local] /* Status */
	str	r10, [fp, #LO_reg_cop0+36-LO_dynarec_local] /* Count */
	bl	check_interupt
	ldr	r1, [fp, #LO_next_interupt-LO_dynarec_local]
	ldr	r0, [fp, #LO_reg_cop0+56-LO_dynarec_local] /* EPC */
	str	r1, [fp, #LO_last_count-LO_dynarec_local]
	subs	r10, r10, r1
	bpl	.E11
.E8:
	add	r6, fp, #LO_reg+256-LO_dynarec_local
	mov	r5, #248
	mov	r1, #0
.E9:
	ldr	r2, [r6, #-8]!
	ldr	r3, [r6, #4]
	eor	r3, r3, r2, asr #31
	subs	r3, r3, #1
	adc	r1, r1, r1
	subs	r5, r5, #8
	bne	.E9
	ldr	r2, [fp, #LO_hi-LO_dynarec_local]
	ldr	r3, [fp, #LO_hi+4-LO_dynarec_local]
	eors	r3, r3, r2, asr #31
	ldr	r2, [fp, #LO_lo-LO_dynarec_local]
	ldreq	r3, [fp, #LO_lo+4-LO_dynarec_local]
	eoreq	r3, r3, r2, asr #31
	subs	r3, r3, #1
	adc	r1, r1, r1
	bl	get_addr_32
	mov	pc, r0
.E11:
	str	r0, [fp, #LO_pcaddr-LO_dynarec_local]
	bl	cc_interrupt
	ldr	r0, [fp, #LO_pcaddr-LO_dynarec_local]
	b	.E8
	.size	jump_eret, .-jump_eret

FUNCTION(new_dyna_start):
	load_varadr r12, dynarec_local
   add   r12, #28
	load_varadr_ext	r1, out
	mov	r0, #0xa4000000
	stmia	r12, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	sub	fp, r12, #28
	ldr	r4, [r1]
	add	r0, r0, #0x40
	bl	new_recompile_block
	ldr	r0, [fp, #LO_next_interupt-LO_dynarec_local]
	ldr	r10, [fp, #LO_reg_cop0+36-LO_dynarec_local] /* Count */
	str	r0, [fp, #LO_last_count-LO_dynarec_local]
	sub	r10, r10, r0
	mov	pc, r4

	.size	new_dyna_start, .-new_dyna_start

FUNCTION(invalidate_addr_r0):
	stmia	fp, {r0, r1, r2, r3, EXTRA_UNSAVED_REGS r12, lr}
	lsr	r0, r0, #12	
	b	invalidate_addr_call
	.size	invalidate_addr_r0, .-invalidate_addr_r0

FUNCTION(invalidate_addr_r1):
	stmia	fp, {r0, r1, r2, r3, EXTRA_UNSAVED_REGS r12, lr}
	lsr	r0, r1, #12	
	b	invalidate_addr_call
	.size	invalidate_addr_r1, .-invalidate_addr_r1

FUNCTION(invalidate_addr_r2):
	stmia	fp, {r0, r1, r2, r3, EXTRA_UNSAVED_REGS r12, lr}
	lsr	r0, r2, #12	
	b	invalidate_addr_call
	.size	invalidate_addr_r2, .-invalidate_addr_r2

FUNCTION(invalidate_addr_r3):
	stmia	fp, {r0, r1, r2, r3, EXTRA_UNSAVED_REGS r12, lr}
	lsr	r0, r3, #12	
	b	invalidate_addr_call
	.size	invalidate_addr_r3, .-invalidate_addr_r3

FUNCTION(invalidate_addr_r4):
	stmia	fp, {r0, r1, r2, r3, EXTRA_UNSAVED_REGS r12, lr}
	lsr	r0, r4, #12	
	b	invalidate_addr_call
	.size	invalidate_addr_r4, .-invalidate_addr_r4

FUNCTION(invalidate_addr_r5):
	stmia	fp, {r0, r1, r2, r3, EXTRA_UNSAVED_REGS r12, lr}
	lsr	r0, r5, #12	
	b	invalidate_addr_call
	.size	invalidate_addr_r5, .-invalidate_addr_r5

FUNCTION(invalidate_addr_r6):
	stmia	fp, {r0, r1, r2, r3, EXTRA_UNSAVED_REGS r12, lr}
	lsr	r0, r6, #12	
	b	invalidate_addr_call
	.size	invalidate_addr_r6, .-invalidate_addr_r6

FUNCTION(invalidate_addr_r7):
	stmia	fp, {r0, r1, r2, r3, EXTRA_UNSAVED_REGS r12, lr}
	lsr	r0, r7, #12	
	b	invalidate_addr_call
	.size	invalidate_addr_r7, .-invalidate_addr_r7

FUNCTION(invalidate_addr_r8):
	stmia	fp, {r0, r1, r2, r3, EXTRA_UNSAVED_REGS r12, lr}
	lsr	r0, r8, #12	
	b	invalidate_addr_call
	.size	invalidate_addr_r8, .-invalidate_addr_r8

FUNCTION(invalidate_addr_r9):
	stmia	fp, {r0, r1, r2, r3, EXTRA_UNSAVED_REGS r12, lr}
	lsr	r0, r9, #12	
	b	invalidate_addr_call
	.size	invalidate_addr_r9, .-invalidate_addr_r9

FUNCTION(invalidate_addr_r10):
	stmia	fp, {r0, r1, r2, r3, EXTRA_UNSAVED_REGS r12, lr}
	lsr	r0, r10, #12	
	b	invalidate_addr_call
	.size	invalidate_addr_r10, .-invalidate_addr_r10

FUNCTION(invalidate_addr_r12):
	stmia	fp, {r0, r1, r2, r3, EXTRA_UNSAVED_REGS r12, lr}
	lsr	r0, r12, #12	
	.size	invalidate_addr_r12, .-invalidate_addr_r12

FUNCTION(invalidate_addr_call): // Not global?
	bl	invalidate_block
	ldmia	fp, {r0, r1, r2, r3, EXTRA_UNSAVED_REGS r12, pc}
	.size	invalidate_addr_call, .-invalidate_addr_call

FUNCTION(write_rdram_new):
	ldr	r3, [fp, #LO_ram_offset-LO_dynarec_local]
	ldr	r2, [fp, #LO_address-LO_dynarec_local]
	ldr	r0, [fp, #LO_word-LO_dynarec_local]
	str	r0, [r2, r3, lsl #2]
	b	.E12
	.size	write_rdram_new, .-write_rdram_new

FUNCTION(write_rdramb_new):
	ldr	r3, [fp, #LO_ram_offset-LO_dynarec_local]
	ldr	r2, [fp, #LO_address-LO_dynarec_local]
	ldrb	r0, [fp, #LO_cpu_byte-LO_dynarec_local]
	eor	r2, r2, #3
	strb	r0, [r2, r3, lsl #2]
	b	.E12
	.size	write_rdramb_new, .-write_rdramb_new

FUNCTION(write_rdramh_new):
	ldr	r3, [fp, #LO_ram_offset-LO_dynarec_local]
	ldr	r2, [fp, #LO_address-LO_dynarec_local]
	ldrh	r0, [fp, #LO_hword-LO_dynarec_local]
	eor	r2, r2, #2
	lsl	r3, r3, #2
	strh	r0, [r2, r3]
	b	.E12
	.size	write_rdramh_new, .-write_rdramh_new

FUNCTION(write_rdramd_new):
	ldr	r3, [fp, #LO_ram_offset-LO_dynarec_local]
	ldr	r2, [fp, #LO_address-LO_dynarec_local]
/*	ldrd	r0, [fp, #LO_dword-LO_dynarec_local]*/
	ldr	r0, [fp, #LO_dword-LO_dynarec_local]
	ldr	r1, [fp, #LO_dword+4-LO_dynarec_local]
	add	r3, r2, r3, lsl #2
	str	r0, [r3, #4]
	str	r1, [r3]
	b	.E12
	.size	write_rdramd_new, .-write_rdramd_new

FUNCTION(do_invalidate): // Not Global?
	ldr	r2, [fp, #LO_address-LO_dynarec_local]
.E12:
	ldr	r1, [fp, #LO_invc_ptr-LO_dynarec_local]
	lsr	r0, r2, #12
	ldrb	r2, [r1, r0]
	tst	r2, r2
	beq	invalidate_block
	mov	pc, lr
	.size	do_invalidate, .-do_invalidate

FUNCTION(read_nomem_new):
	ldr	r2, [fp, #LO_address-LO_dynarec_local]
	add	r12, fp, #LO_memory_map-LO_dynarec_local
	lsr	r0, r2, #12
	ldr	r12, [r12, r0, lsl #2]
	mov	r1, #8
	tst	r12, r12
	bmi	tlb_exception
	ldr	r0, [r2, r12, lsl #2]
	str	r0, [fp, #LO_readmem_dword-LO_dynarec_local]
	mov	pc, lr
	.size	read_nomem_new, .-read_nomem_new

FUNCTION(read_nomemb_new):
	ldr	r2, [fp, #LO_address-LO_dynarec_local]
	add	r12, fp, #LO_memory_map-LO_dynarec_local
	lsr	r0, r2, #12
	ldr	r12, [r12, r0, lsl #2]
	mov	r1, #8
	tst	r12, r12
	bmi	tlb_exception
	eor	r2, r2, #3
	ldrb	r0, [r2, r12, lsl #2]
	str	r0, [fp, #LO_readmem_dword-LO_dynarec_local]
	mov	pc, lr
	.size	read_nomemb_new, .-read_nomemb_new

FUNCTION(read_nomemh_new):
	ldr	r2, [fp, #LO_address-LO_dynarec_local]
	add	r12, fp, #LO_memory_map-LO_dynarec_local
	lsr	r0, r2, #12
	ldr	r12, [r12, r0, lsl #2]
	mov	r1, #8
	tst	r12, r12
	bmi	tlb_exception
	lsl	r12, r12, #2
	eor	r2, r2, #2
	ldrh	r0, [r2, r12]
	str	r0, [fp, #LO_readmem_dword-LO_dynarec_local]
	mov	pc, lr
	.size	read_nomemh_new, .-read_nomemh_new

FUNCTION(read_nomemd_new):
	ldr	r2, [fp, #LO_address-LO_dynarec_local]
	add	r12, fp, #LO_memory_map-LO_dynarec_local
	lsr	r0, r2, #12
	ldr	r12, [r12, r0, lsl #2]
	mov	r1, #8
	tst	r12, r12
	bmi	tlb_exception
	lsl	r12, r12, #2
/*	ldrd	r0, [r2, r12]*/
	add	r3, r2, #4
	ldr	r0, [r2, r12]
	ldr	r1, [r3, r12]
	str	r0, [fp, #LO_readmem_dword+4-LO_dynarec_local]
	str	r1, [fp, #LO_readmem_dword-LO_dynarec_local]
	mov	pc, lr
	.size	read_nomemd_new, .-read_nomemd_new

FUNCTION(write_nomem_new):
	str	r3, [fp, #24]
	str	lr, [fp, #28]
	bl	do_invalidate
	ldr	r2, [fp, #LO_address-LO_dynarec_local]
	add	r12, fp, #LO_memory_map-LO_dynarec_local
	ldr	lr, [fp, #28]
	lsr	r0, r2, #12
	ldr	r3, [fp, #24]
	ldr	r12, [r12, r0, lsl #2]
	mov	r1, #0xc
	tst	r12, #0x40000000
	bne	tlb_exception
	ldr	r0, [fp, #LO_word-LO_dynarec_local]
	str	r0, [r2, r12, lsl #2]
	mov	pc, lr
	.size	write_nomem_new, .-write_nomem_new

FUNCTION(write_nomemb_new):
	str	r3, [fp, #24]
	str	lr, [fp, #28]
	bl	do_invalidate
	ldr	r2, [fp, #LO_address-LO_dynarec_local]
	add	r12, fp, #LO_memory_map-LO_dynarec_local
	ldr	lr, [fp, #28]
	lsr	r0, r2, #12
	ldr	r3, [fp, #24]
	ldr	r12, [r12, r0, lsl #2]
	mov	r1, #0xc
	tst	r12, #0x40000000
	bne	tlb_exception
	eor	r2, r2, #3
	ldrb	r0, [fp, #LO_cpu_byte-LO_dynarec_local]
	strb	r0, [r2, r12, lsl #2]
	mov	pc, lr
	.size	write_nomemb_new, .-write_nomemb_new

FUNCTION(write_nomemh_new):
	str	r3, [fp, #24]
	str	lr, [fp, #28]
	bl	do_invalidate
	ldr	r2, [fp, #LO_address-LO_dynarec_local]
	add	r12, fp, #LO_memory_map-LO_dynarec_local
	ldr	lr, [fp, #28]
	lsr	r0, r2, #12
	ldr	r3, [fp, #24]
	ldr	r12, [r12, r0, lsl #2]
	mov	r1, #0xc
	lsls	r12, #2
	bcs	tlb_exception
	eor	r2, r2, #2
	ldrh	r0, [fp, #LO_hword-LO_dynarec_local]
	strh	r0, [r2, r12]
	mov	pc, lr
	.size	write_nomemh_new, .-write_nomemh_new

FUNCTION(write_nomemd_new):
	str	r3, [fp, #24]
	str	lr, [fp, #28]
	bl	do_invalidate
	ldr	r2, [fp, #LO_address-LO_dynarec_local]
	add	r12, fp, #LO_memory_map-LO_dynarec_local
	ldr	lr, [fp, #28]
	lsr	r0, r2, #12
	ldr	r3, [fp, #24]
	ldr	r12, [r12, r0, lsl #2]
	mov	r1, #0xc
	lsls	r12, #2
	bcs	tlb_exception
	add	r3, r2, #4
	ldr	r0, [fp, #LO_dword+4-LO_dynarec_local]
	ldr	r1, [fp, #LO_dword-LO_dynarec_local]
/*	strd	r0, [r2, r12]*/
	str	r0, [r2, r12]
	str	r1, [r3, r12]
	mov	pc, lr
	.size	write_nomemd_new, .-write_nomemd_new

FUNCTION(tlb_exception): // Not global?
	/* r1 = cause */
	/* r2 = address */
	/* r3 = instr addr/flags */
	ldr	r4, [fp, #LO_reg_cop0+48-LO_dynarec_local] /* Status */
	add	r5, fp, #LO_memory_map-LO_dynarec_local
	lsr	r6, r3, #12
	orr	r1, r1, r3, lsl #31
	orr	r4, r4, #2
	ldr	r7, [r5, r6, lsl #2]
	bic	r8, r3, #3
	str	r4, [fp, #LO_reg_cop0+48-LO_dynarec_local] /* Status */
	mov	r6, #0x6000000
	str	r1, [fp, #LO_reg_cop0+52-LO_dynarec_local] /* Cause */
	orr	r6, r6, #0x22
	ldr	r0, [r8, r7, lsl #2]
	add	r4, r8, r1, asr #29
	add	r5, fp, #LO_reg-LO_dynarec_local
	str	r4, [fp, #LO_reg_cop0+56-LO_dynarec_local] /* EPC */
	mov	r7, #0xf8
	ldr	r8, [fp, #LO_reg_cop0+16-LO_dynarec_local] /* Context */
	lsl	r1, r0, #16
	lsr	r4, r0,	#26
	and	r7, r7, r0, lsr #18
	mvn	r9, #0xF000000F
	sub	r2, r2, r1, asr #16
	bic	r9, r9, #0x0F800000
	rors	r6, r6, r4
	mov	r0, #0x80000000
	ldrcs	r2, [r5, r7]
	bic	r8, r8, r9
	tst	r3, #2
	str	r2, [r5, r7]
	add	r4, r2, r1, asr #16
	add	r6, fp, #LO_reg+4-LO_dynarec_local
	asr	r3, r2, #31
	str	r4, [fp, #LO_reg_cop0+32-LO_dynarec_local] /* BadVAddr */
	add	r0, r0, #0x180
	and	r4, r9, r4, lsr #9
	strne	r3, [r6, r7]
	orr	r8, r8, r4
	str	r8, [fp, #LO_reg_cop0+16-LO_dynarec_local] /* Context */
	bl	get_addr_ht
	ldr	r1, [fp, #LO_next_interupt-LO_dynarec_local]
	ldr	r10, [fp, #LO_reg_cop0+36-LO_dynarec_local] /* Count */
	str	r1, [fp, #LO_last_count-LO_dynarec_local]
	sub	r10, r10, r1
	mov	pc, r0	
	.size	tlb_exception, .-tlb_exception

FUNCTION(breakpoint):
	/* Set breakpoint here for debugging */
	mov	pc, lr
	.size	breakpoint, .-breakpoint

/*  The following bug-fix implements __clear_cache (missing in Android)  */
FUNCTION(__clear_cache_bugfix):
	push    {r7, lr}
	mov     r2, #0
	mov     r7, #0x2
	add     r7, r7, #0xf0000
	svc     0x00000000
	pop     {r7, pc}
	.size	__clear_cache_bugfix, .-__clear_cache_bugfix
/*  End of bug-fix	*/
#ifndef __MACH__
	.section	.note.GNU-stack,"",%progbits
#endif
