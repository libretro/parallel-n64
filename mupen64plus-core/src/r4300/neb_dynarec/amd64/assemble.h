/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - assemble.h                                              *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
 *   Copyright (C) 2013 Nebuleon                                           *
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

#ifndef ASSEMBLE_H
#define ASSEMBLE_H

#include "../driver.h"

#include "osal/preproc.h"

#include <assert.h>
#include <stdlib.h>
#include <stdint.h>

#define AL 0
#define BL 3
#define CL 1
#define DL 2
#define SIL 6
#define DIL 7
#define SPL 4
#define BPL 5
#define R8B 8
#define R9B 9
#define R10B 10
#define R11B 11
#define R12B 12
#define R13B 13
#define R14B 14
#define R15B 15

#define AX 0
#define BX 3
#define CX 1
#define DX 2
#define SI 6
#define DI 7
#define SP 4
#define BP 5
#define R8W 8
#define R9W 9
#define R10W 10
#define R11W 11
#define R12W 12
#define R13W 13
#define R14W 14
#define R15W 15

#define EAX 0
#define EBX 3
#define ECX 1
#define EDX 2
#define ESI 6
#define EDI 7
#define ESP 4
#define EBP 5
#define R8D 8
#define R9D 9
#define R10D 10
#define R11D 11
#define R12D 12
#define R13D 13
#define R14D 14
#define R15D 15

#define RAX 0
#define RBX 3
#define RCX 1
#define RDX 2
#define RSI 6
#define RDI 7
#define RSP 4
#define RBP 5
#define R8 8
#define R9 9
#define R10 10
#define R11 11
#define R12 12
#define R13 13
#define R14 14
#define R15 15

#define CHECK_REG(r) assert((r) < 16)
#define CHECK_IMM5(r) assert((r) < 32)
#define CHECK_IMM6(r) assert((r) < 64)

static uint8_t* output_byte(uint8_t* dst, uint8_t Byte);
static uint8_t* output_hword(uint8_t* dst, uint16_t Hword);
static uint8_t* output_dword(uint8_t* dst, uint32_t Dword);
static uint8_t* output_qword(uint8_t* dst, uint64_t Qword);

// All register references in output_* functions are to native registers.

// return from procedure
/* EMITS: 1 byte */
static osal_inline uint8_t* output_ret(uint8_t* dst)
{
	return output_byte(dst, 0xC3);
}

// register32 = immediate32, signed (64-bit sign extended)
// use E* and R*D register names for easier identification
/* EMITS: 5..6 bytes */
static osal_inline uint8_t* output_mov_imm32_to_r32(uint8_t* dst, uint32_t imm, uint_fast8_t dst_reg)
{
	CHECK_REG(dst_reg);
	if (dst_reg & 0x8) // registers r8d through r15d
		dst = output_byte(dst, 0x41); // require this prefix
	dst = output_byte(dst, 0xB8 | (dst_reg & 0x7));
	return output_dword(dst, imm);
}

// register64 = immediate64
// use R* register names for easier identification
/* EMITS: 10 bytes */
static osal_inline uint8_t* output_mov_imm64_to_r64(uint8_t* dst, uint64_t imm, uint_fast8_t dst_reg)
{
	CHECK_REG(dst_reg);
	// the prefix is either 0x48 or 0x49; 0x49 is for upper 8 registers
	dst = output_byte(dst, 0x48 | ((dst_reg & 0x8) >> 3));
	dst = output_byte(dst, 0xB8 | (dst_reg & 0x7));
	return output_qword(dst, imm);
}

// *(uint64_t*) dst_ptr_reg = src_reg
// use R* register names for easier identification
/* EMITS: 3..4 bytes */
static osal_inline uint8_t* output_mov_r64_to_mem_at_r64(uint8_t* dst, uint_fast8_t src_reg, uint_fast8_t dst_ptr_reg)
{
	CHECK_REG(src_reg);
	CHECK_REG(dst_ptr_reg);
	// the prefix is either 0x48, 0x49, 0x4C or 0x4D;
	// +1 is for the destination (pointer) register being among the 8 upper ones;
	// +4 is for the source register being among the 8 upper ones
	dst = output_byte(dst, 0x48 | ((dst_ptr_reg & 0x8) >> 3) | ((src_reg & 0x8) >> 1));
	dst = output_byte(dst, 0x89);
	// destination (pointer) registers RBP (5) and R13 require | 0x40 in this byte
	dst = output_byte(dst, ((src_reg & 0x7) << 3) | (dst_ptr_reg & 0x7) | ((dst_ptr_reg & 0x7) == 5 ? 0x40 : 0x00));
	// destination (pointer) registers RSP (4) and R12 require an extra 0x24 byte
	if ((dst_ptr_reg & 0x7) == 4)
		dst = output_byte(dst, 0x24);
	// destination (pointer) registers RBP (5) and R13 require an extra 0x00 byte
	else if ((dst_ptr_reg & 0x7) == 5)
		dst = output_byte(dst, 0x00);
	return dst;
}

// dst_reg = *(uint64_t*) src_ptr_reg
// use R* register names for easier identification
/* EMITS: 3..4 bytes */
static osal_inline uint8_t* output_mov_mem_at_r64_to_r64(uint8_t* dst, uint_fast8_t src_ptr_reg, uint_fast8_t dst_reg)
{
	CHECK_REG(src_ptr_reg);
	CHECK_REG(dst_reg);
	// the prefix is either 0x48, 0x49, 0x4C or 0x4D;
	// +1 is for the source (pointer) register being among the 8 upper ones;
	// +4 is for the destination register being among the 8 upper ones
	dst = output_byte(dst, 0x48 | ((src_ptr_reg & 0x8) >> 3) | ((dst_reg & 0x8) >> 1));
	dst = output_byte(dst, 0x8B);
	// source (pointer) registers RBP (5) and R13 require | 0x40 in this byte
	dst = output_byte(dst, ((dst_reg & 0x7) << 3) | (src_ptr_reg & 0x7) | ((src_ptr_reg & 0x7) == 5 ? 0x40 : 0x00));
	// source (pointer) registers RSP (4) and R12 require an extra 0x24 byte
	if ((src_ptr_reg & 0x7) == 4)
		dst = output_byte(dst, 0x24);
	// source (pointer) registers RBP (5) and R13 require an extra 0x00 byte
	else if ((src_ptr_reg & 0x7) == 5)
		dst = output_byte(dst, 0x00);
	return dst;
}

// *(uint32_t*) dst_ptr_reg = src_reg
// use E* and R*D register names for 'src_reg'
// and R* register names for 'dst_ptr_reg' for easier identification
/* EMITS: 2..4 bytes */
static osal_inline uint8_t* output_mov_r32_to_mem_at_r64(uint8_t* dst, uint_fast8_t src_reg, uint_fast8_t dst_ptr_reg)
{
	CHECK_REG(src_reg);
	CHECK_REG(dst_ptr_reg);
	// the prefix is either none, 0x41, 0x44 or 0x45;
	// +1 is for the destination (pointer) register being among the 8 upper ones;
	// +4 is for the source register being among the 8 upper ones
	if ((src_reg & 0x8) || (dst_ptr_reg & 0x8))
		dst = output_byte(dst, 0x40 | ((dst_ptr_reg & 0x8) >> 3) | ((src_reg & 0x8) >> 1));
	dst = output_byte(dst, 0x89);
	// destination (pointer) registers RBP (5) and R13 require | 0x40 in this byte
	dst = output_byte(dst, ((src_reg & 0x7) << 3) | (dst_ptr_reg & 0x7) | ((dst_ptr_reg & 0x7) == 5 ? 0x40 : 0x00));
	// destination (pointer) registers RSP (4) and R12 require an extra 0x24 byte
	if ((dst_ptr_reg & 0x7) == 4)
		dst = output_byte(dst, 0x24);
	// destination (pointer) registers RBP (5) and R13 require an extra 0x00 byte
	else if ((dst_ptr_reg & 0x7) == 5)
		dst = output_byte(dst, 0x00);
	return dst;
}

// dst_reg = *(uint32_t*) src_ptr_reg
// use R* register names for 'src_ptr_reg'
// and E* and R*D register names for 'dst_reg' for easier identification
/* EMITS: 2..4 bytes */
static osal_inline uint8_t* output_mov_mem_at_r64_to_r32(uint8_t* dst, uint_fast8_t src_ptr_reg, uint_fast8_t dst_reg)
{
	CHECK_REG(src_ptr_reg);
	CHECK_REG(dst_reg);
	// the prefix is either none, 0x41, 0x44 or 0x45;
	// +1 is for the source (pointer) register being among the 8 upper ones;
	// +4 is for the destination register being among the 8 upper ones
	if ((dst_reg & 0x8) || (src_ptr_reg & 0x8))
		dst = output_byte(dst, 0x40 | ((src_ptr_reg & 0x8) >> 3) | ((dst_reg & 0x8) >> 1));
	dst = output_byte(dst, 0x8B);
	// source (pointer) registers RBP (5) and R13 require | 0x40 in this byte
	dst = output_byte(dst, ((dst_reg & 0x7) << 3) | (src_ptr_reg & 0x7) | ((src_ptr_reg & 0x7) == 5 ? 0x40 : 0x00));
	// source (pointer) registers RSP (4) and R12 require an extra 0x24 byte
	if ((src_ptr_reg & 0x7) == 4)
		dst = output_byte(dst, 0x24);
	// source (pointer) registers RBP (5) and R13 require an extra 0x00 byte
	else if ((src_ptr_reg & 0x7) == 5)
		dst = output_byte(dst, 0x00);
	return dst;
}

// *(uint64_t*) ((uint8_t*) dst_ptr_reg + offset) = src_reg
// use R* register names for easier identification
/* EMITS: 4..5 bytes */
static osal_inline uint8_t* output_mov_r64_to_mem_at_r64_off8(uint8_t* dst, uint_fast8_t src_reg, uint_fast8_t dst_ptr_reg, int8_t offset)
{
	CHECK_REG(src_reg);
	CHECK_REG(dst_ptr_reg);
	// the prefix is either 0x48, 0x49, 0x4C or 0x4D;
	// +1 is for the destination (pointer) register being among the 8 upper ones;
	// +4 is for the source register being among the 8 upper ones
	dst = output_byte(dst, 0x48 | ((dst_ptr_reg & 0x8) >> 3) | ((src_reg & 0x8) >> 1));
	dst = output_byte(dst, 0x89);
	// 0x40 is the 8-bit displacement marker
	dst = output_byte(dst, 0x40 | ((src_reg & 0x7) << 3) | (dst_ptr_reg & 0x7));
	// destination (pointer) registers RSP (4) and R12 require an extra 0x24 byte
	if ((dst_ptr_reg & 0x7) == 4)
		dst = output_byte(dst, 0x24);
	return output_byte(dst, offset);
}

// dst_reg = *(uint64_t*) ((uint8_t*) src_ptr_reg + offset)
// use R* register names for easier identification
/* EMITS: 4..5 bytes */
static osal_inline uint8_t* output_mov_mem_at_r64_off8_to_r64(uint8_t* dst, uint_fast8_t src_ptr_reg, int8_t offset, uint_fast8_t dst_reg)
{
	CHECK_REG(src_ptr_reg);
	CHECK_REG(dst_reg);
	// the prefix is either 0x48, 0x49, 0x4C or 0x4D;
	// +1 is for the source (pointer) register being among the 8 upper ones;
	// +4 is for the destination register being among the 8 upper ones
	dst = output_byte(dst, 0x48 | ((src_ptr_reg & 0x8) >> 3) | ((dst_reg & 0x8) >> 1));
	dst = output_byte(dst, 0x8B);
	// 0x40 is the 8-bit displacement marker
	dst = output_byte(dst, 0x40 | ((dst_reg & 0x7) << 3) | (src_ptr_reg & 0x7));
	// source (pointer) registers RSP (4) and R12 require an extra 0x24 byte
	if ((src_ptr_reg & 0x7) == 4)
		dst = output_byte(dst, 0x24);
	return output_byte(dst, offset);
}

// *(uint32_t*) ((uint8_t*) dst_ptr_reg + offset) = src_reg
// use E* and R*D register names for 'src_reg'
// and R* register names for 'dst_ptr_reg' for easier identification
/* EMITS: 3..5 bytes */
static osal_inline uint8_t* output_mov_r32_to_mem_at_r64_off8(uint8_t* dst, uint_fast8_t src_reg, uint_fast8_t dst_ptr_reg, int8_t offset)
{
	CHECK_REG(src_reg);
	CHECK_REG(dst_ptr_reg);
	// the prefix is either none, 0x41, 0x44 or 0x45;
	// +1 is for the destination (pointer) register being among the 8 upper ones;
	// +4 is for the source register being among the 8 upper ones
	if ((src_reg & 0x8) || (dst_ptr_reg & 0x8))
		dst = output_byte(dst, 0x40 | ((dst_ptr_reg & 0x8) >> 3) | ((src_reg & 0x8) >> 1));
	dst = output_byte(dst, 0x89);
	// 0x40 is the 8-bit displacement marker
	dst = output_byte(dst, 0x40 | ((src_reg & 0x7) << 3) | (dst_ptr_reg & 0x7));
	// destination (pointer) registers RSP (4) and R12 require an extra 0x24 byte
	if ((dst_ptr_reg & 0x7) == 4)
		dst = output_byte(dst, 0x24);
	return output_byte(dst, offset);
}

// dst_reg = *(uint32_t*) ((uint8_t*) src_ptr_reg + offset)
// use R* register names for 'src_ptr_reg'
// and E* and R*D register names for 'dst_reg' for easier identification
/* EMITS: 3..5 bytes */
static osal_inline uint8_t* output_mov_mem_at_r64_off8_to_r32(uint8_t* dst, uint_fast8_t src_ptr_reg, int8_t offset, uint_fast8_t dst_reg)
{
	CHECK_REG(src_ptr_reg);
	CHECK_REG(dst_reg);
	// the prefix is either none, 0x41, 0x44 or 0x45;
	// +1 is for the source (pointer) register being among the 8 upper ones;
	// +4 is for the destination register being among the 8 upper ones
	if ((dst_reg & 0x8) || (src_ptr_reg & 0x8))
		dst = output_byte(dst, 0x40 | ((src_ptr_reg & 0x8) >> 3) | ((dst_reg & 0x8) >> 1));
	dst = output_byte(dst, 0x8B);
	// 0x40 is the 8-bit displacement marker
	dst = output_byte(dst, 0x40 | ((dst_reg & 0x7) << 3) | (src_ptr_reg & 0x7));
	// source (pointer) registers RSP (4) and R12 require an extra 0x24 byte
	if ((src_ptr_reg & 0x7) == 4)
		dst = output_byte(dst, 0x24);
	return output_byte(dst, offset);
}

// goto (void (*) (void)) dst_ptr_reg
// use R* register names for easier identification
/* EMITS: 2..3 bytes */
static osal_inline uint8_t* output_jmp_at_r64(uint8_t* dst, uint_fast8_t dst_ptr_reg)
{
	CHECK_REG(dst_ptr_reg);
	if (dst_ptr_reg & 0x8) // registers r8 through r15
		dst = output_byte(dst, 0x41); // require this prefix
	dst = output_byte(dst, 0xFF);
	// destination (pointer) registers RBP (5) and R13 require | 0x40 in this byte
	dst = output_byte(dst, 0x20 | (dst_ptr_reg & 0x7) | ((dst_ptr_reg & 0x7) == 5 ? 0x40 : 0x00));
	// destination (pointer) registers RSP (4) and R12 require an extra 0x24 byte
	if ((dst_ptr_reg & 0x7) == 4)
		dst = output_byte(dst, 0x24);
	// destination (pointer) registers RBP (5) and R13 require an extra 0x00 byte
	else if ((dst_ptr_reg & 0x7) == 5)
		dst = output_byte(dst, 0x00);
	return dst;
}

// dst_reg = dst_reg ^ src_reg, 64-bit
// use R* register names for easier identification
/* EMITS: 3 bytes */
static osal_inline uint8_t* output_xor_r64_to_r64(uint8_t* dst, uint_fast8_t src_reg, uint_fast8_t dst_reg)
{
	CHECK_REG(src_reg);
	CHECK_REG(dst_reg);
	// the prefix is either 0x48, 0x49, 0x4C or 0x4D;
	// +1 is for the destination register being among the 8 upper ones;
	// +4 is for the source register being among the 8 upper ones
	dst = output_byte(dst, 0x48 | ((dst_reg & 0x8) >> 3) | ((src_reg & 0x8) >> 1));
	dst = output_byte(dst, 0x31);
	return output_byte(dst, 0xC0 | ((src_reg & 0x7) << 3) | (dst_reg & 0x7));
}

// dst_reg = dst_reg | src_reg, 64-bit
// use R* register names for easier identification
/* EMITS: 3 bytes */
static osal_inline uint8_t* output_or_r64_to_r64(uint8_t* dst, uint_fast8_t src_reg, uint_fast8_t dst_reg)
{
	CHECK_REG(src_reg);
	CHECK_REG(dst_reg);
	// the prefix is either 0x48, 0x49, 0x4C or 0x4D;
	// +1 is for the destination register being among the 8 upper ones;
	// +4 is for the source register being among the 8 upper ones
	dst = output_byte(dst, 0x48 | ((dst_reg & 0x8) >> 3) | ((src_reg & 0x8) >> 1));
	dst = output_byte(dst, 0x09);
	return output_byte(dst, 0xC0 | ((src_reg & 0x7) << 3) | (dst_reg & 0x7));
}

// dst_reg = dst_reg & src_reg, 64-bit
// use R* register names for easier identification
/* EMITS: 3 bytes */
static osal_inline uint8_t* output_and_r64_to_r64(uint8_t* dst, uint_fast8_t src_reg, uint_fast8_t dst_reg)
{
	CHECK_REG(src_reg);
	CHECK_REG(dst_reg);
	// the prefix is either 0x48, 0x49, 0x4C or 0x4D;
	// +1 is for the destination register being among the 8 upper ones;
	// +4 is for the source register being among the 8 upper ones
	dst = output_byte(dst, 0x48 | ((dst_reg & 0x8) >> 3) | ((src_reg & 0x8) >> 1));
	dst = output_byte(dst, 0x21);
	return output_byte(dst, 0xC0 | ((src_reg & 0x7) << 3) | (dst_reg & 0x7));
}

// dst_reg = dst_reg + src_reg, 64-bit
// use R* register names for easier identification
/* EMITS: 3 bytes */
static osal_inline uint8_t* output_add_r64_to_r64(uint8_t* dst, uint_fast8_t src_reg, uint_fast8_t dst_reg)
{
	CHECK_REG(src_reg);
	CHECK_REG(dst_reg);
	// the prefix is either 0x48, 0x49, 0x4C or 0x4D;
	// +1 is for the destination register being among the 8 upper ones;
	// +4 is for the source register being among the 8 upper ones
	dst = output_byte(dst, 0x48 | ((dst_reg & 0x8) >> 3) | ((src_reg & 0x8) >> 1));
	dst = output_byte(dst, 0x01);
	return output_byte(dst, 0xC0 | ((src_reg & 0x7) << 3) | (dst_reg & 0x7));
}

// dst_reg = dst_reg - src_reg, 64-bit
// use R* register names for easier identification
/* EMITS: 3 bytes */
static osal_inline uint8_t* output_sub_r64_to_r64(uint8_t* dst, uint_fast8_t src_reg, uint_fast8_t dst_reg)
{
	CHECK_REG(src_reg);
	CHECK_REG(dst_reg);
	// the prefix is either 0x48, 0x49, 0x4C or 0x4D;
	// +1 is for the destination register being among the 8 upper ones;
	// +4 is for the source register being among the 8 upper ones
	dst = output_byte(dst, 0x48 | ((dst_reg & 0x8) >> 3) | ((src_reg & 0x8) >> 1));
	dst = output_byte(dst, 0x29);
	return output_byte(dst, 0xC0 | ((src_reg & 0x7) << 3) | (dst_reg & 0x7));
}

// dst_reg = ~dst_reg, 64-bit
// use R* register names for easier identification
/* EMITS: 3 bytes */
static osal_inline uint8_t* output_not_r64(uint8_t* dst, uint_fast8_t dst_reg)
{
	CHECK_REG(dst_reg);
	// the prefix is either 0x48 or 0x49;
	// +1 is for the destination register being among the 8 upper ones
	dst = output_byte(dst, 0x48 | ((dst_reg & 0x8) >> 3));
	dst = output_byte(dst, 0xF7);
	return output_byte(dst, 0xD0 | (dst_reg & 0x7));
}

// dst_reg = -dst_reg, 64-bit
// use R* register names for easier identification
/* EMITS: 3 bytes */
static osal_inline uint8_t* output_neg_r64(uint8_t* dst, uint_fast8_t dst_reg)
{
	CHECK_REG(dst_reg);
	// the prefix is either 0x48 or 0x49;
	// +1 is for the destination register being among the 8 upper ones
	dst = output_byte(dst, 0x48 | ((dst_reg & 0x8) >> 3));
	dst = output_byte(dst, 0xF7);
	return output_byte(dst, 0xD8 | (dst_reg & 0x7));
}

// dst_reg = dst_reg << 1, 32-bit
// use E* and R*D register names for easier identification
/* EMITS: 2..3 bytes */
static osal_inline uint8_t* output_shl_r32_by_1(uint8_t* dst, uint_fast8_t dst_reg)
{
	CHECK_REG(dst_reg);
	if (dst_reg & 0x8) // registers r8d through r15d
		dst = output_byte(dst, 0x41); // require this prefix
	dst = output_byte(dst, 0xD1);
	return output_byte(dst, 0xE0 | (dst_reg & 0x7));
}

// dst_reg = dst_reg << amt_imm, 32-bit
// use E* and R*D register names for easier identification
/* EMITS: 2..4 bytes */
static osal_inline uint8_t* output_shl_r32_by_imm5(uint8_t* dst, uint_fast8_t dst_reg, uint8_t amt_imm)
{
	CHECK_REG(dst_reg);
	CHECK_IMM5(amt_imm);
	if (amt_imm == 1)
		return output_shl_r32_by_1(dst, dst_reg);
	else
	{
		if (dst_reg & 0x8) // registers r8d through r15d
			dst = output_byte(dst, 0x41); // require this prefix
		dst = output_byte(dst, 0xC1);
		dst = output_byte(dst, 0xE0 | (dst_reg & 0x7));
		return output_byte(dst, amt_imm);
	}
}

// dst_reg = dst_reg << 1, 64-bit
// use R* register names for easier identification
/* EMITS: 3 bytes */
static osal_inline uint8_t* output_shl_r64_by_1(uint8_t* dst, uint_fast8_t dst_reg)
{
	CHECK_REG(dst_reg);
	// the prefix is either 0x48 or 0x49; 0x49 is for upper 8 registers
	dst = output_byte(dst, 0x48 | ((dst_reg & 0x8) >> 3));
	dst = output_byte(dst, 0xD1);
	return output_byte(dst, 0xE0 | (dst_reg & 0x7));
}

// dst_reg = dst_reg << amt_imm, 64-bit
// use R* register names for easier identification
/* EMITS: 3..4 bytes */
static osal_inline uint8_t* output_shl_r64_by_imm6(uint8_t* dst, uint_fast8_t dst_reg, uint8_t amt_imm)
{
	CHECK_REG(dst_reg);
	CHECK_IMM6(amt_imm);
	if (amt_imm == 1)
		return output_shl_r64_by_1(dst, dst_reg);
	else
	{
		// the prefix is either 0x48 or 0x49;
		// +1 is for the destination register being among the 8 upper ones
		dst = output_byte(dst, 0x48 | ((dst_reg & 0x8) >> 3));
		dst = output_byte(dst, 0xC1);
		dst = output_byte(dst, 0xE0 | (dst_reg & 0x7));
		return output_byte(dst, amt_imm);
	}
}

// dst_reg = dst_reg >> 1, unsigned, 32-bit
// use E* and R*D register names for easier identification
/* EMITS: 2..3 bytes */
static osal_inline uint8_t* output_shr_r32_by_1(uint8_t* dst, uint_fast8_t dst_reg)
{
	CHECK_REG(dst_reg);
	if (dst_reg & 0x8) // registers r8d through r15d
		dst = output_byte(dst, 0x41); // require this prefix
	dst = output_byte(dst, 0xD1);
	return output_byte(dst, 0xE8 | (dst_reg & 0x7));
}

// dst_reg = dst_reg >> amt_imm, unsigned, 32-bit
// use E* and R*D register names for easier identification
/* EMITS: 2..4 bytes */
static osal_inline uint8_t* output_shr_r32_by_imm5(uint8_t* dst, uint_fast8_t dst_reg, uint8_t amt_imm)
{
	CHECK_REG(dst_reg);
	CHECK_IMM5(amt_imm);
	if (amt_imm == 1)
		return output_shr_r32_by_1(dst, dst_reg);
	else
	{
		if (dst_reg & 0x8) // registers r8d through r15d
			dst = output_byte(dst, 0x41); // require this prefix
		dst = output_byte(dst, 0xC1);
		dst = output_byte(dst, 0xE8 | (dst_reg & 0x7));
		return output_byte(dst, amt_imm);
	}
}

// dst_reg = dst_reg >> 1, unsigned, 64-bit
// use R* register names for easier identification
/* EMITS: 3 bytes */
static osal_inline uint8_t* output_shr_r64_by_1(uint8_t* dst, uint_fast8_t dst_reg)
{
	CHECK_REG(dst_reg);
	// the prefix is either 0x48 or 0x49; 0x49 is for upper 8 registers
	dst = output_byte(dst, 0x48 | ((dst_reg & 0x8) >> 3));
	dst = output_byte(dst, 0xD1);
	return output_byte(dst, 0xE8 | (dst_reg & 0x7));
}

// dst_reg = dst_reg >> amt_imm, unsigned, 64-bit
// use R* register names for easier identification
/* EMITS: 3..4 bytes */
static osal_inline uint8_t* output_shr_r64_by_imm6(uint8_t* dst, uint_fast8_t dst_reg, uint8_t amt_imm)
{
	CHECK_REG(dst_reg);
	CHECK_IMM6(amt_imm);
	if (amt_imm == 1)
		return output_shr_r64_by_1(dst, dst_reg);
	else
	{
		// the prefix is either 0x48 or 0x49;
		// +1 is for the destination register being among the 8 upper ones
		dst = output_byte(dst, 0x48 | ((dst_reg & 0x8) >> 3));
		dst = output_byte(dst, 0xC1);
		dst = output_byte(dst, 0xE8 | (dst_reg & 0x7));
		return output_byte(dst, amt_imm);
	}
}

// -- SAR COPY LINE --

// dst_reg = dst_reg >> 1, signed, 32-bit
// use E* and R*D register names for easier identification
/* EMITS: 2..3 bytes */
static osal_inline uint8_t* output_sar_r32_by_1(uint8_t* dst, uint_fast8_t dst_reg)
{
	CHECK_REG(dst_reg);
	if (dst_reg & 0x8) // registers r8d through r15d
		dst = output_byte(dst, 0x41); // require this prefix
	dst = output_byte(dst, 0xD1);
	return output_byte(dst, 0xF8 | (dst_reg & 0x7));
}

// dst_reg = dst_reg >> amt_imm, signed, 32-bit
// use E* and R*D register names for easier identification
/* EMITS: 2..4 bytes */
static osal_inline uint8_t* output_sar_r32_by_imm5(uint8_t* dst, uint_fast8_t dst_reg, uint8_t amt_imm)
{
	CHECK_REG(dst_reg);
	CHECK_IMM5(amt_imm);
	if (amt_imm == 1)
		return output_sar_r32_by_1(dst, dst_reg);
	else
	{
		if (dst_reg & 0x8) // registers r8d through r15d
			dst = output_byte(dst, 0x41); // require this prefix
		dst = output_byte(dst, 0xC1);
		dst = output_byte(dst, 0xF8 | (dst_reg & 0x7));
		return output_byte(dst, amt_imm);
	}
}

// dst_reg = dst_reg >> 1, signed, 64-bit
// use R* register names for easier identification
/* EMITS: 3 bytes */
static osal_inline uint8_t* output_sar_r64_by_1(uint8_t* dst, uint_fast8_t dst_reg)
{
	CHECK_REG(dst_reg);
	// the prefix is either 0x48 or 0x49; 0x49 is for upper 8 registers
	dst = output_byte(dst, 0x48 | ((dst_reg & 0x8) >> 3));
	dst = output_byte(dst, 0xD1);
	return output_byte(dst, 0xF8 | (dst_reg & 0x7));
}

// dst_reg = dst_reg >> amt_imm, signed, 64-bit
// use R* register names for easier identification
/* EMITS: 3..4 bytes */
static osal_inline uint8_t* output_sar_r64_by_imm6(uint8_t* dst, uint_fast8_t dst_reg, uint8_t amt_imm)
{
	CHECK_REG(dst_reg);
	CHECK_IMM6(amt_imm);
	if (amt_imm == 1)
		return output_sar_r64_by_1(dst, dst_reg);
	else
	{
		// the prefix is either 0x48 or 0x49;
		// +1 is for the destination register being among the 8 upper ones
		dst = output_byte(dst, 0x48 | ((dst_reg & 0x8) >> 3));
		dst = output_byte(dst, 0xC1);
		dst = output_byte(dst, 0xF8 | (dst_reg & 0x7));
		return output_byte(dst, amt_imm);
	}
}

#endif // ASSEMBLE_H
