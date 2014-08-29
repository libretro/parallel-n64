/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - assemble.h                                              *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
 *   Copyright (C) 2002 Hacktarux, 2013 Nebuleon                           *
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

#define CHECK_FP_FMT_S_D(fp_fmt) assert((fp_fmt) == FP_FORMAT_S || (fp_fmt) == FP_FORMAT_D)
#define CHECK_REG(r) assert((r) < 32)
#define CHECK_IMM5(imm) assert((imm) < 32)
#define CHECK_ADDR(dst, src) assert((((uintptr_t) (dst)) & 3) == 0 && \
			(((uintptr_t) (src) + 4) & 0xF0000000) == ((uintptr_t) (dst) & 0xF0000000))

#define _OP_SLL		0x00
#define _OP_SRL		0x02
#define _OP_SRA		0x03
#define _OP_SLLV		0x04
#define _OP_SRLV		0x06
#define _OP_SRAV		0x07
#define _OP_JR		0x08
#define _OP_JALR		0x09
#define _OP_MULT		0x18
#define _OP_MULTU	0x19
#define _OP_DIV		0x1a
#define _OP_DIVU		0x1b
#define _OP_MFHI		0x10
#define _OP_MTHI		0x11
#define _OP_ADDU		0x21
#define _OP_MFLO		0x12
#define _OP_MTLO		0x13
#define _OP_SUBU		0x23
#define _OP_AND		0x24
#define _OP_OR		0x25
#define _OP_XOR		0x26
#define _OP_NOR		0x27
#define _OP_SLT		0x2a
#define _OP_SLTU		0x2b
#define _OP_MOVZ		0x0a
#define _OP_MOVN		0x0b
#define _OP_BLTZ		0x04000000
#define _OP_BGEZ		0x04010000
#define _OP_BLTZAL	0x04100000
#define _OP_BGEZAL	0x04110000
#define _OP_J		0x08000000
#define _OP_JAL		0x0c000000
#define _OP_BEQ		0x10000000
#define _OP_BNE		0x14000000
#define _OP_BLEZ		0x18000000
#define _OP_BGTZ		0x1c000000
#define _OP_ADDIU	0x24000000
#define _OP_SLTI		0x28000000
#define _OP_SLTIU	0x2c000000
#define _OP_ANDI		0x30000000
#define _OP_ORI		0x34000000
#define _OP_XORI		0x38000000
#define _OP_LUI		0x3c000000
#define _OP_LB		0x80000000
#define _OP_LH		0x84000000
#define _OP_LW		0x8c000000
#define _OP_LBU		0x90000000
#define _OP_LHU		0x94000000
#define _OP_SB		0xa0000000
#define _OP_SH		0xa4000000
#define _OP_SW		0xac000000
#define _OP_CFC1	0x44400000
#define _OP_CTC1	0x44c00000
#define _OP_FP_ADD	0x44000000
#define _OP_FP_SUB	0x44000001
#define _OP_FP_MUL	0x44000002
#define _OP_FP_DIV	0x44000003
#define _OP_FP_SQRT	0x44000004
#define _OP_FP_ABS	0x44000005
#define _OP_FP_MOV	0x44000006
#define _OP_FP_NEG	0x44000007
#define _OP_FP_ROUND_W	0x4400000c
#define _OP_FP_ROUND_L	0x44000008
#define _OP_FP_TRUNC_W	0x4400000d
#define _OP_FP_TRUNC_L	0x44000009
#define _OP_FP_CEIL_W	0x4400000e
#define _OP_FP_CEIL_L	0x4400000a
#define _OP_FP_FLOOR_W	0x4400000f
#define _OP_FP_FLOOR_L	0x4400000b
#define _OP_FP_CVT_S	0x44000020
#define _OP_FP_CVT_D	0x44000021
#define _OP_FP_CVT_W	0x44000024
#define _OP_FP_CVT_L	0x44000025
#define _OP_FP_C	0x44000030
#define _OP_MFC1	0x44000000
#define _OP_MTC1	0x44800000
#define _OP_BEQL	0x50000000
#define _OP_BNEL	0x54000000
#define _OP_LWC1	0xc4000000
#define _OP_LDC1	0xd4000000
#define _OP_SWC1	0xe4000000
#define _OP_SDC1	0xf4000000
#define _OP_BC1F	0x45000000
#define _OP_BC1T	0x45010000

#ifdef _MIPS_ARCH_MIPS32R2
#define _OP_EXT		0x7c000000
#define _OP_INS		0x7c000004
#define _OP_SEB		0x7c000420
#define _OP_SEH		0x7c000620
#endif


#define __OP_R(rs1, rs2, rd, imm, op) \
  (((rs1) << 21) | ((rs2) << 16) | ((rd) << 11) | ((imm) << 6) | (op))

#define OP_ADDU(rs1, rs2, rd) __OP_R(rs1, rs2, rd, 0, _OP_ADDU)
#define OP_SUBU(rs1, rs2, rd) __OP_R(rs1, rs2, rd, 0, _OP_SUBU)
#define OP_AND(rs1, rs2, rd)  __OP_R(rs1, rs2, rd, 0, _OP_AND)
#define OP_NOR(rs1, rs2, rd)  __OP_R(rs1, rs2, rd, 0, _OP_NOR)
#define OP_OR(rs1, rs2, rd)   __OP_R(rs1, rs2, rd, 0, _OP_OR)
#define OP_SLT(rs1, rs2, rd)  __OP_R(rs1, rs2, rd, 0, _OP_SLT)
#define OP_SLTU(rs1, rs2, rd) __OP_R(rs1, rs2, rd, 0, _OP_SLTU)
#define OP_XOR(rs1, rs2, rd)  __OP_R(rs1, rs2, rd, 0, _OP_XOR)
#define OP_SLLV(rs1, rs2, rd) __OP_R(rs1, rs2, rd, 0, _OP_SLLV)
#define OP_SRAV(rs1, rs2, rd) __OP_R(rs1, rs2, rd, 0, _OP_SRAV)
#define OP_SRLV(rs1, rs2, rd) __OP_R(rs1, rs2, rd, 0, _OP_SRLV)
#define OP_DIV(rs1, rs2)      __OP_R(rs1, rs2, 0,  0, _OP_DIV)
#define OP_DIVU(rs1, rs2)     __OP_R(rs1, rs2, 0,  0, _OP_DIVU)
#define OP_MULT(rs1, rs2)     __OP_R(rs1, rs2, 0,  0, _OP_MULT)
#define OP_MULTU(rs1, rs2)    __OP_R(rs1, rs2, 0,  0, _OP_MULTU)
#define OP_MOVZ(rs, rc, rd)   __OP_R(rs,  rc,  rd, 0, _OP_MOVZ)
#define OP_MOVN(rs, rc, rd)   __OP_R(rs,  rc,  rd, 0, _OP_MOVN)
#define OP_MFHI(rd)           __OP_R(0,   0,   rd, 0, _OP_MFHI)
#define OP_MFLO(rd)           __OP_R(0,   0,   rd, 0, _OP_MFLO)
#define OP_MTHI(rs)           __OP_R(rs,  0,   0,  0, _OP_MTHI)
#define OP_MTLO(rs)           __OP_R(rs,  0,   0,  0, _OP_MTLO)

#define OP_SLL(rs, rd, imm)   __OP_R(0,   rs,  rd, imm, _OP_SLL)
#define OP_SRA(rs, rd, imm)   __OP_R(0,   rs,  rd, imm, _OP_SRA)
#define OP_SRL(rs, rd, imm)   __OP_R(0,   rs,  rd, imm, _OP_SRL)

#define OP_JALR(rs, rd)       __OP_R(rs,  0,   rd, 0, _OP_JALR)
#define OP_JR(rs)             __OP_R(rs,  0,   0,  0, _OP_JR)

#define OP_CFC1(rt, fs)       __OP_R(0,   rt,  fs, 0, _OP_CFC1)
#define OP_CTC1(rt, fs)       __OP_R(0,   rt,  fs, 0, _OP_CTC1)
#define OP_FP_ADD(fmt, ft, fs, fd)  __OP_R(fmt, ft, fs, fd, _OP_FP_ADD)
#define OP_FP_SUB(fmt, ft, fs, fd)  __OP_R(fmt, ft, fs, fd, _OP_FP_SUB)
#define OP_FP_MUL(fmt, ft, fs, fd)  __OP_R(fmt, ft, fs, fd, _OP_FP_MUL)
#define OP_FP_DIV(fmt, ft, fs, fd)  __OP_R(fmt, ft, fs, fd, _OP_FP_DIV)
#define OP_FP_SQRT(fmt, fs, fd)     __OP_R(fmt, 0,  fs, fd, _OP_FP_SQRT)
#define OP_FP_ABS(fmt, fs, fd)      __OP_R(fmt, 0,  fs, fd, _OP_FP_ABS)
#define OP_FP_MOV(fmt, fs, fd)      __OP_R(fmt, 0,  fs, fd, _OP_FP_MOV)
#define OP_FP_NEG(fmt, fs, fd)      __OP_R(fmt, 0,  fs, fd, _OP_FP_NEG)
#define OP_FP_ROUND_W(fmt, fs, fd)  __OP_R(fmt, 0,  fs, fd, _OP_FP_ROUND_W)
#define OP_FP_ROUND_L(fmt, fs, fd)  __OP_R(fmt, 0,  fs, fd, _OP_FP_ROUND_L)
#define OP_FP_TRUNC_W(fmt, fs, fd)  __OP_R(fmt, 0,  fs, fd, _OP_FP_TRUNC_W)
#define OP_FP_TRUNC_L(fmt, fs, fd)  __OP_R(fmt, 0,  fs, fd, _OP_FP_TRUNC_L)
#define OP_FP_CEIL_W(fmt, fs, fd)   __OP_R(fmt, 0,  fs, fd, _OP_FP_CEIL_W)
#define OP_FP_CEIL_L(fmt, fs, fd)   __OP_R(fmt, 0,  fs, fd, _OP_FP_CEIL_L)
#define OP_FP_FLOOR_W(fmt, fs, fd)  __OP_R(fmt, 0,  fs, fd, _OP_FP_FLOOR_W)
#define OP_FP_FLOOR_L(fmt, fs, fd)  __OP_R(fmt, 0,  fs, fd, _OP_FP_FLOOR_L)
#define OP_FP_CVT_S(fmt, fs, fd)    __OP_R(fmt, 0,  fs, fd, _OP_FP_CVT_S)
#define OP_FP_CVT_D(fmt, fs, fd)    __OP_R(fmt, 0,  fs, fd, _OP_FP_CVT_D)
#define OP_FP_CVT_W(fmt, fs, fd)    __OP_R(fmt, 0,  fs, fd, _OP_FP_CVT_W)
#define OP_FP_CVT_L(fmt, fs, fd)    __OP_R(fmt, 0,  fs, fd, _OP_FP_CVT_L)

#define OP_MFC1(rt, fs)             __OP_R(0,   rt, fs, 0,  _OP_MFC1)
#define OP_MTC1(rt, fs)             __OP_R(0,   rt, fs, 0,  _OP_MTC1)

#ifdef _MIPS_ARCH_MIPS32R2
#define OP_EXT(rs, rd, s, p)  __OP_R(rs,  rd,  (s) - 1,  p, _OP_EXT)
#define OP_INS(rs, rd, s, p)  __OP_R(rs,  rd,  (p) + (s) - 1,  p, _OP_INS)
#define OP_SEB(rs, rd)        __OP_R(0,   rs,  rd, 0, _OP_SEB)
#define OP_SEH(rs, rd)        __OP_R(0,   rs,  rd, 0, _OP_SEH)
#endif


#define OP_FP_C(fmt, ft, fs, cond) \
  (((fmt) << 21) | ((ft) << 16) | ((fs) << 11) | (cond) | _OP_FP_C)

#define __OP_I(rs, rd, imm, op) \
  ((op) | ((rs) << 21) | ((rd) << 16) | ((imm) & 0xffff))

#define OP_ADDIU(rs, rd, imm) __OP_I(rs, rd, imm, _OP_ADDIU)
#define OP_ANDI(rs, rd, imm)  __OP_I(rs, rd, imm, _OP_ANDI)
#define OP_ORI(rs, rd, imm)   __OP_I(rs, rd, imm, _OP_ORI)
#define OP_SLTI(rs, rd, imm)  __OP_I(rs, rd, imm, _OP_SLTI)
#define OP_SLTIU(rs, rd, imm) __OP_I(rs, rd, imm, _OP_SLTIU)
#define OP_XORI(rs, rd, imm)  __OP_I(rs, rd, imm, _OP_XORI)
#define OP_LB(rs, rd, off)    __OP_I(rs, rd, off, _OP_LB)
#define OP_LBU(rs, rd, off)   __OP_I(rs, rd, off, _OP_LBU)
#define OP_LH(rs, rd, off)    __OP_I(rs, rd, off, _OP_LH)
#define OP_LHU(rs, rd, off)   __OP_I(rs, rd, off, _OP_LHU)
#define OP_LW(rs, rd, off)    __OP_I(rs, rd, off, _OP_LW)
#define OP_SB(rs, rd, off)    __OP_I(rs, rd, off, _OP_SB)
#define OP_SH(rs, rd, off)    __OP_I(rs, rd, off, _OP_SH)
#define OP_SW(rs, rd, off)    __OP_I(rs, rd, off, _OP_SW)
#define OP_LWC1(rs, fd, off)  __OP_I(rs, fd, off, _OP_LWC1)
#define OP_LDC1(rs, fd, off)  __OP_I(rs, fd, off, _OP_LDC1)
#define OP_SWC1(rs, fd, off)  __OP_I(rs, fd, off, _OP_SWC1)
#define OP_SDC1(rs, fd, off)  __OP_I(rs, fd, off, _OP_SDC1)

#define OP_BEQ(rs1, rs2, off) __OP_I(rs1, rs2, off, _OP_BEQ)
#define OP_BNE(rs1, rs2, off) __OP_I(rs1, rs2, off, _OP_BNE)

#define OP_BEQL(rs1, rs2, off) __OP_I(rs1, rs2, off, _OP_BEQL)
#define OP_BNEL(rs1, rs2, off) __OP_I(rs1, rs2, off, _OP_BNEL)

#define OP_BGEZ(rs, off)      __OP_I(rs,  0,   off, _OP_BGEZ)
#define OP_BGEZAL(rs, off)    __OP_I(rs,  0,   off, _OP_BGEZAL)
#define OP_BGTZ(rs, off)      __OP_I(rs,  0,   off, _OP_BGTZ)
#define OP_BLEZ(rs, off)      __OP_I(rs,  0,   off, _OP_BLEZ)
#define OP_BLTZ(rs, off)      __OP_I(rs,  0,   off, _OP_BLTZ)
#define OP_BLTZAL(rs, off)    __OP_I(rs,  0,   off, _OP_BLTZAL)

#define OP_BC1F(off)          __OP_I(0,   0,   off, _OP_BC1F)
#define OP_BC1T(off)          __OP_I(0,   0,   off, _OP_BC1T)

#define OP_LUI(rd, imm)       __OP_I(0 ,  rd,  imm, _OP_LUI)

#define __OP_J(addr, op) ((op) | ((((uintptr_t) (addr)) & 0x0ffffffc) >> 2))

#define OP_J(addr)            __OP_J(addr, _OP_J)
#define OP_JAL(addr)          __OP_J(addr, _OP_JAL)

static uint8_t* output_dword(uint8_t* dst, uint32_t* avail, uint32_t opcode);

// All register references in output_* functions are to native registers.

#define output_nop(dst, avail) output_dword(dst, avail, 0)

// rd = rs1 + rs2
static osal_inline uint8_t* output_addu(uint8_t* dst, uint32_t* avail, u_int rs1, u_int rs2, u_int rd)
{
	CHECK_REG(rs1);
	CHECK_REG(rs2);
	CHECK_REG(rd);
	return output_dword(dst, avail, OP_ADDU(rs1, rs2, rd));
}

// rd = rs + imm (signed)
static osal_inline uint8_t* output_addiu(uint8_t* dst, uint32_t* avail, u_int rs, short imm, u_int rd)
{
	CHECK_REG(rs);
	CHECK_REG(rd);
	return output_dword(dst, avail, OP_ADDIU(rs, rd, imm));
}

// rd = rs1 - rs2
static osal_inline uint8_t* output_subu(uint8_t* dst, uint32_t* avail, u_int rs1, u_int rs2, u_int rd)
{
	CHECK_REG(rs1);
	CHECK_REG(rs2);
	CHECK_REG(rd);
	return output_dword(dst, avail, OP_SUBU(rs1, rs2, rd));
}

// rd = rs1 & rs2
static osal_inline uint8_t* output_and(uint8_t* dst, uint32_t* avail, u_int rs1, u_int rs2, u_int rd)
{
	CHECK_REG(rs1);
	CHECK_REG(rs2);
	CHECK_REG(rd);
	return output_dword(dst, avail, OP_AND(rs1, rs2, rd));
}

// rd = rs & imm
static osal_inline uint8_t* output_andi(uint8_t* dst, uint32_t* avail, u_int rs, u_short imm, u_int rd)
{
	CHECK_REG(rs);
	CHECK_REG(rd);
	return output_dword(dst, avail, OP_ANDI(rs, rd, imm));
}

// rd = imm << 16
static osal_inline uint8_t* output_lui(uint8_t* dst, uint32_t* avail, u_int rd, u_short imm)
{
	CHECK_REG(rd);
	return output_dword(dst, avail, OP_LUI(rd, imm));
}

// rd = ~(rs1 | rs2)
static osal_inline uint8_t* output_nor(uint8_t* dst, uint32_t* avail, u_int rs1, u_int rs2, u_int rd)
{
	CHECK_REG(rs1);
	CHECK_REG(rs2);
	CHECK_REG(rd);
	return output_dword(dst, avail, OP_NOR(rs1, rs2, rd));
}

// rd = rs1 | rs2
static osal_inline uint8_t* output_or(uint8_t* dst, uint32_t* avail, u_int rs1, u_int rs2, u_int rd)
{
	CHECK_REG(rs1);
	CHECK_REG(rs2);
	CHECK_REG(rd);
	return output_dword(dst, avail, OP_OR(rs1, rs2, rd));
}

// rd = rs | imm
static osal_inline uint8_t* output_ori(uint8_t* dst, uint32_t* avail, u_int rs, u_short imm, u_int rd)
{
	CHECK_REG(rs);
	CHECK_REG(rd);
	return output_dword(dst, avail, OP_ORI(rs, rd, imm));
}

// rd = 1 if rs1 < rs2 else 0, signed
static osal_inline uint8_t* output_slt(uint8_t* dst, uint32_t* avail, u_int rs1, u_int rs2, u_int rd)
{
	CHECK_REG(rs1);
	CHECK_REG(rs2);
	CHECK_REG(rd);
	return output_dword(dst, avail, OP_SLT(rs1, rs2, rd));
}

// rd = 1 if rs < imm else 0, signed
static osal_inline uint8_t* output_slti(uint8_t* dst, uint32_t* avail, u_int rs, short imm, u_int rd)
{
	CHECK_REG(rs);
	CHECK_REG(rd);
	return output_dword(dst, avail, OP_SLTI(rs, rd, imm));
}

// rd = 1 if rs < imm (signed) else 0, unsigned
static osal_inline uint8_t* output_sltiu(uint8_t* dst, uint32_t* avail, u_int rs, short imm, u_int rd)
{
	CHECK_REG(rs);
	CHECK_REG(rd);
	return output_dword(dst, avail, OP_SLTIU(rs, rd, imm));
}

// rd = 1 if rs1 < rs2 else 0, unsigned
static osal_inline uint8_t* output_sltu(uint8_t* dst, uint32_t* avail, u_int rs1, u_int rs2, u_int rd)
{
	CHECK_REG(rs1);
	CHECK_REG(rs2);
	CHECK_REG(rd);
	return output_dword(dst, avail, OP_SLTU(rs1, rs2, rd));
}

// rd = rs1 ^ rs2
static osal_inline uint8_t* output_xor(uint8_t* dst, uint32_t* avail, u_int rs1, u_int rs2, u_int rd)
{
	CHECK_REG(rs1);
	CHECK_REG(rs2);
	CHECK_REG(rd);
	return output_dword(dst, avail, OP_XOR(rs1, rs2, rd));
}

// rd = rs ^ imm
static osal_inline uint8_t* output_xori(uint8_t* dst, uint32_t* avail, u_int rs, u_short imm, u_int rd)
{
	CHECK_REG(rs);
	CHECK_REG(rd);
	return output_dword(dst, avail, OP_XORI(rs, rd, imm));
}

// rd = rs << amt_imm
static osal_inline uint8_t* output_sll(uint8_t* dst, uint32_t* avail, u_int rs, u_int amt_imm, u_int rd)
{
	CHECK_REG(rs);
	CHECK_REG(rd);
	CHECK_IMM5(amt_imm);
	return output_dword(dst, avail, OP_SLL(rs, rd, amt_imm));
}

// rd = rs << amt_reg
static osal_inline uint8_t* output_sllv(uint8_t* dst, uint32_t* avail, u_int rs, u_int amt_reg, u_int rd)
{
	CHECK_REG(rs);
	CHECK_REG(rd);
	CHECK_REG(amt_reg);
	return output_dword(dst, avail, OP_SLLV(amt_reg, rs, rd));
}

// rd = rs >> amt_imm, signed
static osal_inline uint8_t* output_sra(uint8_t* dst, uint32_t* avail, u_int rs, u_int amt_imm, u_int rd)
{
	CHECK_REG(rs);
	CHECK_REG(rd);
	CHECK_IMM5(amt_imm);
	return output_dword(dst, avail, OP_SRA(rs, rd, amt_imm));
}

// rd = rs >> amt_reg, signed
static osal_inline uint8_t* output_srav(uint8_t* dst, uint32_t* avail, u_int rs, u_int amt_reg, u_int rd)
{
	CHECK_REG(rs);
	CHECK_REG(rd);
	CHECK_REG(amt_reg);
	return output_dword(dst, avail, OP_SRAV(amt_reg, rs, rd));
}

// rd = rs >> amt_imm, unsigned
static osal_inline uint8_t* output_srl(uint8_t* dst, uint32_t* avail, u_int rs, u_int amt_imm, u_int rd)
{
	CHECK_REG(rs);
	CHECK_REG(rd);
	CHECK_IMM5(amt_imm);
	return output_dword(dst, avail, OP_SRL(rs, rd, amt_imm));
}

// rd = rs >> amt_reg, unsigned
static osal_inline uint8_t* output_srlv(uint8_t* dst, uint32_t* avail, u_int rs, u_int amt_reg, u_int rd)
{
	CHECK_REG(rs);
	CHECK_REG(rd);
	CHECK_REG(amt_reg);
	return output_dword(dst, avail, OP_SRLV(amt_reg, rs, rd));
}

// HI = rs1 % rs2, signed (remainder)
// LO = rs1 / rs2, signed (quotient)
// Needs to be taken from HI and LO on the host and placed into the emulated
// HI and LO registers afterwards.
static osal_inline uint8_t* output_div(uint8_t* dst, uint32_t* avail, u_int rs1, u_int rs2)
{
	CHECK_REG(rs1);
	CHECK_REG(rs2);
	return output_dword(dst, avail, OP_DIV(rs1, rs2));
}

// HI = rs1 % rs2, unsigned (remainder)
// LO = rs1 / rs2, unsigned (quotient)
// Needs to be taken from HI and LO on the host and placed into the emulated
// HI and LO registers afterwards.
static osal_inline uint8_t* output_divu(uint8_t* dst, uint32_t* avail, u_int rs1, u_int rs2)
{
	CHECK_REG(rs1);
	CHECK_REG(rs2);
	return output_dword(dst, avail, OP_DIVU(rs1, rs2));
}

// HI:LO = rs1 * rs2, signed
// Needs to be taken from HI and LO on the host and placed into the emulated
// HI and LO registers afterwards.
static osal_inline uint8_t* output_mult(uint8_t* dst, uint32_t* avail, u_int rs1, u_int rs2)
{
	CHECK_REG(rs1);
	CHECK_REG(rs2);
	return output_dword(dst, avail, OP_MULT(rs1, rs2));
}

// HI:LO = rs1 * rs2, unsigned
// Needs to be taken from HI and LO on the host and placed into the emulated
// HI and LO registers afterwards.
static osal_inline uint8_t* output_multu(uint8_t* dst, uint32_t* avail, u_int rs1, u_int rs2)
{
	CHECK_REG(rs1);
	CHECK_REG(rs2);
	return output_dword(dst, avail, OP_MULTU(rs1, rs2));
}

// if (rc == 0) rd = rs; [rc: Register Compare]
static osal_inline uint8_t* output_movz(uint8_t* dst, uint32_t* avail, u_int rc, u_int rs, u_int rd)
{
	CHECK_REG(rc);
	CHECK_REG(rs);
	CHECK_REG(rd);
	return output_dword(dst, avail, OP_MOVZ(rs, rc, rd));
}

// if (rc != 0) rd = rs; [rc: Register Compare]
static osal_inline uint8_t* output_movn(uint8_t* dst, uint32_t* avail, u_int rc, u_int rs, u_int rd)
{
	CHECK_REG(rc);
	CHECK_REG(rs);
	CHECK_REG(rd);
	return output_dword(dst, avail, OP_MOVN(rs, rc, rd));
}

// rd = HI
static osal_inline uint8_t* output_mfhi(uint8_t* dst, uint32_t* avail, u_int rd)
{
	CHECK_REG(rd);
	return output_dword(dst, avail, OP_MFHI(rd));
}

// rd = LO
static osal_inline uint8_t* output_mflo(uint8_t* dst, uint32_t* avail, u_int rd)
{
	CHECK_REG(rd);
	return output_dword(dst, avail, OP_MFLO(rd));
}

// HI = rs
static osal_inline uint8_t* output_mthi(uint8_t* dst, uint32_t* avail, u_int rs)
{
	CHECK_REG(rs);
	return output_dword(dst, avail, OP_MTHI(rs));
}

// LO = rs
static osal_inline uint8_t* output_mtlo(uint8_t* dst, uint32_t* avail, u_int rs)
{
	CHECK_REG(rs);
	return output_dword(dst, avail, OP_MTLO(rs));
}

// MTHI and MTLO are not really needed, as MTHI and MTLO on the emulated N64
// go to the virtual registers allocated to HI and LO via a native MOV.

// $: if (rs1 == rs2) goto $ + 4 + (signed) off * 4
// So the resolved offset can be -131064..+131076.
// However, the received 'off' is native (-32768..+32767).
static osal_inline uint8_t* output_beq(uint8_t* dst, uint32_t* avail, u_int rs1, u_int rs2, short off)
{
	CHECK_REG(rs1);
	CHECK_REG(rs2);
	return output_dword(dst, avail, OP_BEQ(rs1, rs2, off));
}

// $: if (rs1 == rs2) goto $ + 4 + (signed) off * 4
// So the resolved offset can be -131064..+131076.
// However, the received 'off' is native (-32768..+32767).
static osal_inline uint8_t* output_beql(uint8_t* dst, uint32_t* avail, u_int rs1, u_int rs2, short off)
{
	CHECK_REG(rs1);
	CHECK_REG(rs2);
	return output_dword(dst, avail, OP_BEQL(rs1, rs2, off));
}

// $: if (rs >= 0, signed) goto $ + 4 + (signed) off * 4
// So the resolved offset can be -131064..+131076.
// However, the received 'off' is native (-32768..+32767).
static osal_inline uint8_t* output_bgez(uint8_t* dst, uint32_t* avail, u_int rs, short off)
{
	CHECK_REG(rs);
	return output_dword(dst, avail, OP_BGEZ(rs, off));
}

// $: if (rs >= 0, signed) call $ + 4 + (signed) off * 4
// So the resolved offset can be -131064..+131076.
// However, the received 'off' is native (-32768..+32767).
static osal_inline uint8_t* output_bgezal(uint8_t* dst, uint32_t* avail, u_int rs, short off)
{
	CHECK_REG(rs);
	return output_dword(dst, avail, OP_BGEZAL(rs, off));
}

// $: if (rs > 0, signed) goto $ + 4 + (signed) off * 4
// So the resolved offset can be -131064..+131076.
// However, the received 'off' is native (-32768..+32767).
static osal_inline uint8_t* output_bgtz(uint8_t* dst, uint32_t* avail, u_int rs, short off)
{
	CHECK_REG(rs);
	return output_dword(dst, avail, OP_BGTZ(rs, off));
}

// $: if (rs <= 0, signed) goto $ + 4 + (signed) off * 4
// So the resolved offset can be -131064..+131076.
// However, the received 'off' is native (-32768..+32767).
static osal_inline uint8_t* output_blez(uint8_t* dst, uint32_t* avail, u_int rs, short off)
{
	CHECK_REG(rs);
	return output_dword(dst, avail, OP_BLEZ(rs, off));
}

// $: if (rs < 0, signed) goto $ + 4 + (signed) off * 4
// So the resolved offset can be -131064..+131076.
// However, the received 'off' is native (-32768..+32767).
static osal_inline uint8_t* output_bltz(uint8_t* dst, uint32_t* avail, u_int rs, short off)
{
	CHECK_REG(rs);
	return output_dword(dst, avail, OP_BLTZ(rs, off));
}

// $: if (rs < 0, signed) call $ + 4 + (signed) off * 4
// So the resolved offset can be -131064..+131076.
// However, the received 'off' is native (-32768..+32767).
static osal_inline uint8_t* output_bltzal(uint8_t* dst, uint32_t* avail, u_int rs, short off)
{
	CHECK_REG(rs);
	return output_dword(dst, avail, OP_BLTZAL(rs, off));
}

// $: if (rs1 != rs2) goto $ + 4 + (signed) off * 4
// So the resolved offset can be -131064..+131076.
// However, the received 'off' is native (-32768..+32767).
static osal_inline uint8_t* output_bne(uint8_t* dst, uint32_t* avail, u_int rs1, u_int rs2, short off)
{
	CHECK_REG(rs1);
	CHECK_REG(rs2);
	return output_dword(dst, avail, OP_BNE(rs1, rs2, off));
}

// goto (PC & 0xF0000000) | (target << 2) [Verilog: PC[31:28] . target[25:0] . 00]
// The received 'target' is native (it's the full address for verification).
static osal_inline uint8_t* output_j(uint8_t* dst, uint32_t* avail, const void* target)
{
	CHECK_ADDR(target, dst);
	return output_dword(dst, avail, OP_J(target));
}

// call (PC & 0xF0000000) | (target << 2) [Verilog: PC[31:28] . target[25:0] . 00]
// The received 'target' is native (it's the full address for verification).
static osal_inline uint8_t* output_jal(uint8_t* dst, uint32_t* avail, const void* target)
{
	CHECK_ADDR(target, dst);
	return output_dword(dst, avail, OP_JAL(target));
}

// call rs, placing the return address in rd (usually $31)
static osal_inline uint8_t* output_jalr(uint8_t* dst, uint32_t* avail, u_int rs, u_int rd)
{
	CHECK_REG(rs);
	CHECK_REG(rd);
	return output_dword(dst, avail, OP_JALR(rs, rd));
}

// goto rs
static osal_inline uint8_t* output_jr(uint8_t* dst, uint32_t* avail, u_int rs)
{
	CHECK_REG(rs);
	return output_dword(dst, avail, OP_JR(rs));
}

// MFC0 and MTC0 are not really needed, as MFC0 and MTC0 on the emulated N64
// go to the native registers allocated to COP0 registers via a native MOV.
// SYSCALL is not needed either, as the kernel is likely different.

// rd = memory[rs + off, signed], signed 8-bit
static osal_inline uint8_t* output_lb(uint8_t* dst, uint32_t* avail, u_int rs, short off, int rd)
{
	CHECK_REG(rs);
	CHECK_REG(rd);
	return output_dword(dst, avail, OP_LB(rs, rd, off));
}

// rd = memory[rs + off, signed], unsigned 8-bit
static osal_inline uint8_t* output_lbu(uint8_t* dst, uint32_t* avail, u_int rs, short off, u_int rd)
{
	CHECK_REG(rs);
	CHECK_REG(rd);
	return output_dword(dst, avail, OP_LBU(rs, rd, off));
}

// rd = memory[rs + off, signed], signed 16-bit
static osal_inline uint8_t* output_lh(uint8_t* dst, uint32_t* avail, u_int rs, short off, int rd)
{
	CHECK_REG(rs);
	CHECK_REG(rd);
	return output_dword(dst, avail, OP_LH(rs, rd, off));
}

// rd = memory[rs + off, signed], unsigned 16-bit
static osal_inline uint8_t* output_lhu(uint8_t* dst, uint32_t* avail, u_int rs, short off, u_int rd)
{
	CHECK_REG(rs);
	CHECK_REG(rd);
	return output_dword(dst, avail, OP_LHU(rs, rd, off));
}

// rd = memory[rs + off, signed], 32-bit
static osal_inline uint8_t* output_lw(uint8_t* dst, uint32_t* avail, u_int rs, short off, u_int rd)
{
	CHECK_REG(rs);
	CHECK_REG(rd);
	return output_dword(dst, avail, OP_LW(rs, rd, off));
}

// memory[rs + off, signed] = rd, 8-bit
static osal_inline uint8_t* output_sb(uint8_t* dst, uint32_t* avail, u_int rs, short off, u_int rd)
{
	CHECK_REG(rs);
	CHECK_REG(rd);
	return output_dword(dst, avail, OP_SB(rs, rd, off));
}

// memory[rs + off, signed] = rd, 16-bit
static osal_inline uint8_t* output_sh(uint8_t* dst, uint32_t* avail, u_int rs, short off, u_int rd)
{
	CHECK_REG(rs);
	CHECK_REG(rd);
	return output_dword(dst, avail, OP_SH(rs, rd, off));
}

// memory[rs + off, signed] = rd, 32-bit
static osal_inline uint8_t* output_sw(uint8_t* dst, uint32_t* avail, u_int rs, short off, u_int rd)
{
	CHECK_REG(rs);
	CHECK_REG(rd);
	return output_dword(dst, avail, OP_SW(rs, rd, off));
}

/* - - - FLOATING-POINT INSTRUCTIONS - - - */

#define FP_FORMAT_S 16
#define FP_FORMAT_D 17
#define FP_FORMAT_W 20
#define FP_FORMAT_L 21

#define FP_PREDICATE_F    0 /**< false */
#define FP_PREDICATE_UN   1 /**< unordered */
#define FP_PREDICATE_EQ   2 /**< equal */
#define FP_PREDICATE_LT   4 /**< less than */
#define FP_EXCEPTION_QNAN 8 /**< raise an exception if a quiet NaN is present */

// rd = FPU Control Register fcs;
static osal_inline uint8_t* output_cfc1(uint8_t* dst, uint32_t* avail, u_int fcs, u_int rd)
{
	CHECK_REG(fcs);
	CHECK_REG(rd);
	return output_dword(dst, avail, OP_CFC1(rd, fcs));
}

// FPU Control Register fcd = rs;
static osal_inline uint8_t* output_ctc1(uint8_t* dst, uint32_t* avail, u_int rs, u_int fcd)
{
	CHECK_REG(rs);
	CHECK_REG(fcd);
	return output_dword(dst, avail, OP_CTC1(rs, fcd));
}

// rd = reinterpret fs as int, 32-bit
static osal_inline uint8_t* output_mfc1(uint8_t* dst, uint32_t* avail, u_int fs, u_int rd)
{
	CHECK_REG(fs);
	CHECK_REG(rd);
	return output_dword(dst, avail, OP_MFC1(rd, fs));
}

// fs = reinterpret fs as float, 32-bit
static osal_inline uint8_t* output_mtc1(uint8_t* dst, uint32_t* avail, u_int rs, u_int fd)
{
	CHECK_REG(rs);
	CHECK_REG(fd);
	return output_dword(dst, avail, OP_MTC1(rs, fd));
}

// fd = memory[rs + off, signed], 32-bit
static osal_inline uint8_t* output_lwc1(uint8_t* dst, uint32_t* avail, u_int rs, short off, u_int fd)
{
	CHECK_REG(rs);
	CHECK_REG(fd);
	return output_dword(dst, avail, OP_LWC1(rs, fd, off));
}

// fd = memory[rs + off, signed], 64-bit
static osal_inline uint8_t* output_ldc1(uint8_t* dst, uint32_t* avail, u_int rs, short off, u_int fd)
{
	CHECK_REG(rs);
	CHECK_REG(fd);
	return output_dword(dst, avail, OP_LDC1(rs, fd, off));
}

// memory[rs + off, signed] = fd, 32-bit
static osal_inline uint8_t* output_swc1(uint8_t* dst, uint32_t* avail, u_int rs, short off, u_int fd)
{
	CHECK_REG(rs);
	CHECK_REG(fd);
	return output_dword(dst, avail, OP_SWC1(rs, fd, off));
}

// memory[rs + off, signed] = fd, 64-bit
static osal_inline uint8_t* output_sdc1(uint8_t* dst, uint32_t* avail, u_int rs, short off, u_int fd)
{
	CHECK_REG(rs);
	CHECK_REG(fd);
	return output_dword(dst, avail, OP_SDC1(rs, fd, off));
}

// $: if (!C1.cc) goto $ + 4 + (signed) off * 4
// So the resolved offset can be -131064..+131076.
// However, the received 'off' is native (-32768..+32767).
static osal_inline uint8_t* output_bc1f(uint8_t* dst, uint32_t* avail, short off)
{
	return output_dword(dst, avail, OP_BC1F(off));
}

// $: if (C1.cc) goto $ + 4 + (signed) off * 4
// So the resolved offset can be -131064..+131076.
// However, the received 'off' is native (-32768..+32767).
static osal_inline uint8_t* output_bc1t(uint8_t* dst, uint32_t* avail, short off)
{
	return output_dword(dst, avail, OP_BC1T(off));
}

// fd = fs + ft, 32- or 64-bit IEEE 754 floating-point
static osal_inline uint8_t* output_fp_add(uint8_t* dst, uint32_t* avail, u_int fp_fmt, u_int fs, u_int ft, u_int fd)
{
	CHECK_FP_FMT_S_D(fp_fmt);
	CHECK_REG(fs);
	CHECK_REG(ft);
	CHECK_REG(fd);
	return output_dword(dst, avail, OP_FP_ADD(fp_fmt, ft, fs, fd));
}

// fd = fs - ft, 32- or 64-bit IEEE 754 floating-point
static osal_inline uint8_t* output_fp_sub(uint8_t* dst, uint32_t* avail, u_int fp_fmt, u_int fs, u_int ft, u_int fd)
{
	CHECK_FP_FMT_S_D(fp_fmt);
	CHECK_REG(fs);
	CHECK_REG(ft);
	CHECK_REG(fd);
	return output_dword(dst, avail, OP_FP_SUB(fp_fmt, ft, fs, fd));
}

// fd = fs * ft, 32- or 64-bit IEEE 754 floating-point
static osal_inline uint8_t* output_fp_mul(uint8_t* dst, uint32_t* avail, u_int fp_fmt, u_int fs, u_int ft, u_int fd)
{
	CHECK_FP_FMT_S_D(fp_fmt);
	CHECK_REG(fs);
	CHECK_REG(ft);
	CHECK_REG(fd);
	return output_dword(dst, avail, OP_FP_MUL(fp_fmt, ft, fs, fd));
}

// fd = fs / ft, 32- or 64-bit IEEE 754 floating-point
static osal_inline uint8_t* output_fp_div(uint8_t* dst, uint32_t* avail, u_int fp_fmt, u_int fs, u_int ft, u_int fd)
{
	CHECK_FP_FMT_S_D(fp_fmt);
	CHECK_REG(fs);
	CHECK_REG(ft);
	CHECK_REG(fd);
	return output_dword(dst, avail, OP_FP_DIV(fp_fmt, ft, fs, fd));
}

// fd = sqrt(rs), 32- or 64-bit IEEE 754 floating-point
static osal_inline uint8_t* output_fp_sqrt(uint8_t* dst, uint32_t* avail, u_int fp_fmt, u_int fs, u_int fd)
{
	CHECK_FP_FMT_S_D(fp_fmt);
	CHECK_REG(fs);
	CHECK_REG(fd);
	return output_dword(dst, avail, OP_FP_SQRT(fp_fmt, fs, fd));
}

// fd = abs(rs), 32- or 64-bit IEEE 754 floating-point
static osal_inline uint8_t* output_fp_abs(uint8_t* dst, uint32_t* avail, u_int fp_fmt, u_int fs, u_int fd)
{
	CHECK_FP_FMT_S_D(fp_fmt);
	CHECK_REG(fs);
	CHECK_REG(fd);
	return output_dword(dst, avail, OP_FP_ABS(fp_fmt, fs, fd));
}

// fd = rs, 32- or 64-bit IEEE 754 floating-point
static osal_inline uint8_t* output_fp_mov(uint8_t* dst, uint32_t* avail, u_int fp_fmt, u_int fs, u_int fd)
{
	CHECK_FP_FMT_S_D(fp_fmt);
	CHECK_REG(fs);
	CHECK_REG(fd);
	return output_dword(dst, avail, OP_FP_MOV(fp_fmt, fs, fd));
}

// fd = -rs, 32- or 64-bit IEEE 754 floating-point
static osal_inline uint8_t* output_fp_neg(uint8_t* dst, uint32_t* avail, u_int fp_fmt, u_int fs, u_int fd)
{
	CHECK_FP_FMT_S_D(fp_fmt);
	CHECK_REG(fs);
	CHECK_REG(fd);
	return output_dword(dst, avail, OP_FP_NEG(fp_fmt, fs, fd));
}

// FCR31[23] = fs predicate ft, 32- or 64-bit IEEE 754 floating-point
static osal_inline uint8_t* output_fp_c(uint8_t* dst, uint32_t* avail, u_int fp_fmt, u_int fs, u_int fp_predicate, u_int ft)
{
	CHECK_FP_FMT_S_D(fp_fmt);
	CHECK_REG(fs);
	CHECK_REG(ft);
	return output_dword(dst, avail, OP_FP_C(fp_fmt, ft, fs, fp_predicate));
}

// fd = (W) round(fs), 32- or 64-bit IEEE 754 floating-point
static osal_inline uint8_t* output_fp_round_w(uint8_t* dst, uint32_t* avail, u_int fp_fmt, u_int fs, u_int fd)
{
	CHECK_FP_FMT_S_D(fp_fmt);
	CHECK_REG(fs);
	CHECK_REG(fd);
	return output_dword(dst, avail, OP_FP_ROUND_W(fp_fmt, fs, fd));
}

// fd = (L) round(fs), 32- or 64-bit IEEE 754 floating-point
static osal_inline uint8_t* output_fp_round_l(uint8_t* dst, uint32_t* avail, u_int fp_fmt, u_int fs, u_int fd)
{
	CHECK_FP_FMT_S_D(fp_fmt);
	CHECK_REG(fs);
	CHECK_REG(fd);
	return output_dword(dst, avail, OP_FP_ROUND_L(fp_fmt, fs, fd));
}

// fd = (W) fs, 32- or 64-bit IEEE 754 floating-point
static osal_inline uint8_t* output_fp_trunc_w(uint8_t* dst, uint32_t* avail, u_int fp_fmt, u_int fs, u_int fd)
{
	CHECK_FP_FMT_S_D(fp_fmt);
	CHECK_REG(fs);
	CHECK_REG(fd);
	return output_dword(dst, avail, OP_FP_TRUNC_W(fp_fmt, fs, fd));
}

// fd = (L) fs, 32- or 64-bit IEEE 754 floating-point
static osal_inline uint8_t* output_fp_trunc_l(uint8_t* dst, uint32_t* avail, u_int fp_fmt, u_int fs, u_int fd)
{
	CHECK_FP_FMT_S_D(fp_fmt);
	CHECK_REG(fs);
	CHECK_REG(fd);
	return output_dword(dst, avail, OP_FP_TRUNC_L(fp_fmt, fs, fd));
}

// fd = (W) ceil(fs), 32- or 64-bit IEEE 754 floating-point
static osal_inline uint8_t* output_fp_ceil_w(uint8_t* dst, uint32_t* avail, u_int fp_fmt, u_int fs, u_int fd)
{
	CHECK_FP_FMT_S_D(fp_fmt);
	CHECK_REG(fs);
	CHECK_REG(fd);
	return output_dword(dst, avail, OP_FP_CEIL_W(fp_fmt, fs, fd));
}

// fd = (L) ceil(fs), 32- or 64-bit IEEE 754 floating-point
static osal_inline uint8_t* output_fp_ceil_l(uint8_t* dst, uint32_t* avail, u_int fp_fmt, u_int fs, u_int fd)
{
	CHECK_FP_FMT_S_D(fp_fmt);
	CHECK_REG(fs);
	CHECK_REG(fd);
	return output_dword(dst, avail, OP_FP_CEIL_L(fp_fmt, fs, fd));
}

// fd = (W) floor(fs), 32- or 64-bit IEEE 754 floating-point
static osal_inline uint8_t* output_fp_floor_w(uint8_t* dst, uint32_t* avail, u_int fp_fmt, u_int fs, u_int fd)
{
	CHECK_FP_FMT_S_D(fp_fmt);
	CHECK_REG(fs);
	CHECK_REG(fd);
	return output_dword(dst, avail, OP_FP_FLOOR_W(fp_fmt, fs, fd));
}

// fd = (L) floor(fs), 32- or 64-bit IEEE 754 floating-point
static osal_inline uint8_t* output_fp_floor_l(uint8_t* dst, uint32_t* avail, u_int fp_fmt, u_int fs, u_int fd)
{
	CHECK_FP_FMT_S_D(fp_fmt);
	CHECK_REG(fs);
	CHECK_REG(fd);
	return output_dword(dst, avail, OP_FP_FLOOR_L(fp_fmt, fs, fd));
}

// fd = (float) fs, 64-bit IEEE 754 floating-point or Formats W or L
static osal_inline uint8_t* output_fp_cvt_s(uint8_t* dst, uint32_t* avail, u_int fp_fmt, u_int fs, u_int fd)
{
	assert(fp_fmt == FP_FORMAT_D || fp_fmt == FP_FORMAT_W || fp_fmt == FP_FORMAT_L);
	CHECK_REG(fs);
	CHECK_REG(fd);
	return output_dword(dst, avail, OP_FP_CVT_S(fp_fmt, fs, fd));
}

// fd = (double) fs, 32-bit IEEE 754 floating-point or Formats W or L
static osal_inline uint8_t* output_fp_cvt_d(uint8_t* dst, uint32_t* avail, u_int fp_fmt, u_int fs, u_int fd)
{
	assert(fp_fmt == FP_FORMAT_S || fp_fmt == FP_FORMAT_W || fp_fmt == FP_FORMAT_L);
	CHECK_REG(fs);
	CHECK_REG(fd);
	return output_dword(dst, avail, OP_FP_CVT_D(fp_fmt, fs, fd));
}

// fd = (W) fs, 32- or 64-bit IEEE 754 floating-point or Format L
static osal_inline uint8_t* output_fp_cvt_w(uint8_t* dst, uint32_t* avail, u_int fp_fmt, u_int fs, u_int fd)
{
	assert(fp_fmt == FP_FORMAT_S || fp_fmt == FP_FORMAT_D || fp_fmt == FP_FORMAT_L);
	CHECK_REG(fs);
	CHECK_REG(fd);
	return output_dword(dst, avail, OP_FP_CVT_W(fp_fmt, fs, fd));
}

// fd = (L) fs, 32- or 64-bit IEEE 754 floating-point or Format W
static osal_inline uint8_t* output_fp_cvt_l(uint8_t* dst, uint32_t* avail, u_int fp_fmt, u_int fs, u_int fd)
{
	assert(fp_fmt == FP_FORMAT_S || fp_fmt == FP_FORMAT_D || fp_fmt == FP_FORMAT_W);
	CHECK_REG(fs);
	CHECK_REG(fd);
	return output_dword(dst, avail, OP_FP_CVT_L(fp_fmt, fs, fd));
}

#ifdef _MIPS_ARCH_MIPS32R2
// mask = ((1 << size_imm) - 1) << shift_imm;
// rd = (rs & mask) >> shift_imm;
// Extracts bits shift_imm .. shift_imm + size_imm - 1 from rs into
// bits 0 .. size_imm - 1 of rd.
static osal_inline uint8_t* output_ext(uint8_t* dst, uint32_t* avail, u_int rs, u_int shift_imm, u_int size_imm, u_int rd)
{
	CHECK_REG(rs);
	CHECK_REG(rd);
	CHECK_IMM5(shift_imm);
	CHECK_IMM5(size_imm);
	return output_dword(dst, avail, OP_EXT(rs, rd, size_imm, shift_imm));
}

// mask = ((1 << size_imm) - 1) << shift_imm;
// rd = (rd & ~mask) | ((rs << shift_imm) & mask);
// Inserts bits 0 .. size_imm - 1 from rs into
// bits shift_imm .. shift_imm + size_imm - 1 of rd.
static osal_inline uint8_t* output_ins(uint8_t* dst, uint32_t* avail, u_int rs, u_int shift_imm, u_int size_imm, u_int rd)
{
	CHECK_REG(rs);
	CHECK_REG(rd);
	CHECK_IMM5(shift_imm);
	CHECK_IMM5(size_imm);
	return output_dword(dst, avail, OP_INS(rs, rd, size_imm, shift_imm));
}

// rd = (int32_t) ((int8_t) rs);
static osal_inline uint8_t* output_seb(uint8_t* dst, uint32_t* avail, u_int rs, u_int rd)
{
	CHECK_REG(rs);
	CHECK_REG(rd);
	return output_dword(dst, avail, OP_SEB(rs, rd));
}

// rd = (int32_t) ((int16_t) rs);
static osal_inline uint8_t* output_seh(uint8_t* dst, uint32_t* avail, u_int rs, u_int rd)
{
	CHECK_REG(rs);
	CHECK_REG(rd);
	return output_dword(dst, avail, OP_SEH(rs, rd));
}
#endif

/* Helper functions (with instruction counts) */

// A useful helper that loads a native register with a 32-bit constant, always
// in a LUI/ORI pair so as to be easily modifiable.
// 2 instructions.
static osal_inline uint8_t* output_modifiable_imm32(uint8_t* dst, uint32_t* avail, u_int imm, u_int rd)
{
	CHECK_REG(rd);
	dst = output_lui(dst, avail, rd, imm >> 16);
	return output_ori(dst, avail, rd, imm & 0xFFFF, rd);
}

// A useful helper that loads a native register with a 32-bit constant.
// 1 or 2 instructions.
static osal_inline uint8_t* output_load_imm32(uint8_t* dst, uint32_t* avail, u_int rd, u_int imm)
{
	CHECK_REG(rd);
	if ((imm >> 16) == 0)                 // fits in lower 16 bits
		return output_ori(dst, avail, 0, imm, rd);
	else if ((imm << 16) == 0)            // fits in upper 16 bits
		return output_lui(dst, avail, rd, imm >> 16);
	else if (imm >= 0xFFFF8000)           // negative 16-bit immediate
		return output_addiu(dst, avail, 0, (short) imm, rd);
	else
		return output_modifiable_imm32(dst, avail, imm, rd);
}

// A useful helper that loads a native register with a 32-bit constant,
// optimising for the known value of 1 register.
// 1 or 2 instructions.
static osal_inline uint8_t* output_load_imm32_1known(uint8_t* dst, uint32_t* avail, u_int rd, u_int imm, u_int known_reg, u_int known_value)
{
	CHECK_REG(rd);
	CHECK_REG(known_reg);
	if ((imm >> 16) == 0)                 // fits in lower 16 bits
		return output_ori(dst, avail, 0, imm, rd);
	else if ((imm << 16) == 0)            // fits in upper 16 bits
		return output_lui(dst, avail, rd, imm >> 16);
	else if (imm >= 0xFFFF8000)           // negative 16-bit immediate
		return output_addiu(dst, avail, 0, (short) imm, rd);
	else
	{
		// Otherwise, try hard to optimise the loading of the immediate.
		// Let's go!
		if ((imm >> 16) == (known_value >> 16)) // Upper 16 bits equal? 1 ins
			return output_xori(dst, avail, known_reg, (imm ^ known_value) & 0xFFFF, rd);
		else if (imm == -known_value) // Negation of the known value? 1 ins
			return output_subu(dst, avail, 0, known_reg, rd);
		else if (imm == ~known_value) // Complement of the known value? 1 ins
			return output_nor(dst, avail, known_reg, 0, rd);
		else
		{
			int i = (int) imm - (int) known_value;
			if (i >= -32768 && i <= 32767) // In additive range? 1 ins
				return output_addiu(dst, avail, known_reg, (short) i, rd);
			else
				return output_modifiable_imm32(dst, avail, imm, rd);
		}
	}
}

// A useful helper that loads a native register with a 32-bit constant,
// optimising for the known values of 2 registers.
// 1 or 2 instructions.
static osal_inline uint8_t* output_load_imm32_2known(uint8_t* dst, uint32_t* avail, u_int rd, u_int imm, u_int known_reg1, u_int known_value1, u_int known_reg2, u_int known_value2)
{
	CHECK_REG(rd);
	CHECK_REG(known_reg1);
	CHECK_REG(known_reg2);
	if ((imm >> 16) == 0)                 // fits in lower 16 bits
		return output_ori(dst, avail, 0, imm, rd);
	else if ((imm << 16) == 0)            // fits in upper 16 bits
		return output_lui(dst, avail, rd, imm >> 16);
	else if (imm >= 0xFFFF8000)           // negative 16-bit immediate
		return output_addiu(dst, avail, 0, (short) imm, rd);
	else
	{
		// Otherwise, try hard to optimise the loading of the immediate.
		// Let's go!
		if ((imm >> 16) == (known_value1 >> 16)) // Upper 16 bits equal? 1 ins
			return output_xori(dst, avail, known_reg1, (imm ^ known_value1) & 0xFFFF, rd);
		else if ((imm >> 16) == (known_value2 >> 16))
			return output_xori(dst, avail, known_reg2, (imm ^ known_value2) & 0xFFFF, rd);
		else if ((int) imm == -(int) known_value1) // Negation of the known value? 1 ins
			return output_subu(dst, avail, 0, known_reg1, rd);
		else if ((int) imm == -(int) known_value2)
			return output_subu(dst, avail, 0, known_reg2, rd);
		else if (imm == ~known_value1) // Complement of the known value? 1 ins
			return output_nor(dst, avail, known_reg1, 0, rd);
		else if (imm == ~known_value2)
			return output_nor(dst, avail, known_reg2, 0, rd);
		else if (imm == known_value1 + known_value2)
			return output_addu(dst, avail, known_reg1, known_reg2, rd);
		else if (imm == known_value1 - known_value2)
			return output_subu(dst, avail, known_reg1, known_reg2, rd);
		else if (imm == known_value2 - known_value1)
			return output_subu(dst, avail, known_reg2, known_reg1, rd);
		else if (imm == (known_value1 & known_value2))
			return output_and(dst, avail, known_reg1, known_reg2, rd);
		else if (imm == (known_value1 | known_value2))
			return output_or(dst, avail, known_reg1, known_reg2, rd);
		else if (imm == (known_value1 ^ known_value2))
			return output_xor(dst, avail, known_reg1, known_reg2, rd);
		else if (imm == ~(known_value1 | known_value2))
			return output_nor(dst, avail, known_reg1, known_reg2, rd);
		else
		{
			int i = (int) imm - (int) known_value1;
			if (i >= -32768 && i <= 32767) // In additive range? 1 ins
			{
				return output_addiu(dst, avail, known_reg1, (short) i, rd);
			} else {
				i = (int) imm - (int) known_value2;
				if (i >= -32768 && i <= 32767) // In additive range? 1 ins
					return output_addiu(dst, avail, known_reg2, (short) i, rd);
				else
					return output_modifiable_imm32(dst, avail, imm, rd);
			}
		}
	}
}

// Modifies an immediate stored in a LUI/ORI pair by output_modifiable_imm32.
static osal_inline void modify_imm32(void* native_op, u_int new_imm)
{
	*(uint16_t *) native_op = new_imm >> 16;
	*((uint16_t *) native_op + 2) = new_imm & 0xFFFF;
}

// Emits a load of register 'rd' from an absolute memory address.
// The address is calculated using native register 'addr_reg', and the value
// of 'addr_reg' at the end of the load is returned. This allows for known
// register value optimisations.
// 2 instructions.
static osal_inline uint8_t* output_lbu_abs(uint8_t* dst, uint32_t* avail, const void* mem, u_int addr_reg, u_int rd, uint32_t* known_value)
{
	CHECK_REG(addr_reg);
	CHECK_REG(rd);
	u_short mem_hi = (((u_int) mem) + 0x8000) >> 16;
	*known_value = mem_hi << 16;
	dst = output_lui(dst, avail, addr_reg, mem_hi);
	short mem_off = (short) (((int) mem) & 0xFFFF);
	return output_lbu(dst, avail, addr_reg, mem_off, rd);
}

// Emits a load of register 'rd' from an absolute memory address, optimising
// for the known value of 1 register. The address is calculated using native
// register 'addr_reg', and the value of 'addr_reg' at the end of the load
// is returned. If 'known_reg' was used and known_reg != addr_reg, 0 is
// returned instead.
// 1 or 2 instructions.
static osal_inline uint8_t* output_lbu_abs_1known(uint8_t* dst, uint32_t* avail, const void* mem, u_int addr_reg, u_int rd, u_int known_reg, uint32_t* known_value)
{
	CHECK_REG(addr_reg);
	CHECK_REG(rd);
	CHECK_REG(known_reg);
	int known_off = (int) mem - (int32_t) *known_value;
	if (known_off >= -32768 && known_off <= 32767)
	{
		*known_value = (addr_reg == known_reg) ? *known_value : 0;
		return output_lbu(dst, avail, known_reg, known_off, rd);
	}
	else
		return output_lbu_abs(dst, avail, mem, addr_reg, rd, known_value);
}

// Emits a load of register 'rd' from an absolute memory address.
// The address is calculated using native register 'addr_reg', and the value
// of 'addr_reg' at the end of the load is returned. This allows for known
// register value optimisations.
// 2 instructions.
static osal_inline uint8_t* output_lb_abs(uint8_t* dst, uint32_t* avail, const void* mem, u_int addr_reg, u_int rd, uint32_t* known_value)
{
	CHECK_REG(addr_reg);
	CHECK_REG(rd);
	u_short mem_hi = (((u_int) mem) + 0x8000) >> 16;
	*known_value = mem_hi << 16;
	dst = output_lui(dst, avail, addr_reg, mem_hi);
	short mem_off = (short) (((int) mem) & 0xFFFF);
	return output_lb(dst, avail, addr_reg, mem_off, rd);
}

// Emits a load of register 'rd' from an absolute memory address, optimising
// for the known value of 1 register. The address is calculated using native
// register 'addr_reg', and the value of 'addr_reg' at the end of the load
// is returned. If 'known_reg' was used and known_reg != addr_reg, 0 is
// returned instead.
// 1 or 2 instructions.
static osal_inline uint8_t* output_lb_abs_1known(uint8_t* dst, uint32_t* avail, const void* mem, u_int addr_reg, u_int rd, u_int known_reg, uint32_t* known_value)
{
	CHECK_REG(addr_reg);
	CHECK_REG(rd);
	CHECK_REG(known_reg);
	int known_off = (int) mem - (int32_t) *known_value;
	if (known_off >= -32768 && known_off <= 32767)
	{
		*known_value = (addr_reg == known_reg) ? *known_value : 0;
		return output_lb(dst, avail, known_reg, known_off, rd);
	}
	else
		return output_lb_abs(dst, avail, mem, addr_reg, rd, known_value);
}

// Emits a store of register 'rd' to an absolute memory address.
// The address is calculated using native register 'addr_reg', and the value
// of 'addr_reg' at the end of the store is returned. This allows for known
// register value optimisations.
// 2 instructions.
static osal_inline uint8_t* output_sb_abs(uint8_t* dst, uint32_t* avail, const void* mem, u_int addr_reg, u_int rd, uint32_t* known_value)
{
	CHECK_REG(addr_reg);
	CHECK_REG(rd);
	u_short mem_hi = (((u_int) mem) + 0x8000) >> 16;
	*known_value = mem_hi << 16;
	dst = output_lui(dst, avail, addr_reg, mem_hi);
	short mem_off = (short) (((int) mem) & 0xFFFF);
	return output_sb(dst, avail, addr_reg, mem_off, rd);
}

// Emits a store of register 'rd' to an absolute memory address, optimising
// for the known value of 1 register. The address is calculated using native
// register 'addr_reg', and the value of 'addr_reg' at the end of the store
// is returned. If 'known_reg' was used and known_reg != addr_reg, 0 is
// returned instead.
// 1 or 2 instructions.
static osal_inline uint8_t* output_sb_abs_1known(uint8_t* dst, uint32_t* avail, const void* mem, u_int addr_reg, u_int rd, u_int known_reg, uint32_t* known_value)
{
	CHECK_REG(addr_reg);
	CHECK_REG(rd);
	CHECK_REG(known_reg);
	int known_off = (int) mem - (int32_t) *known_value;
	if (known_off >= -32768 && known_off <= 32767)
	{
		*known_value = (addr_reg == known_reg) ? *known_value : 0;
		return output_sb(dst, avail, known_reg, known_off, rd);
	}
	else
		return output_sb_abs(dst, avail, mem, addr_reg, rd, known_value);
}

// Emits a load of register 'rd' from an absolute memory address.
// The address is calculated using native register 'addr_reg', and the value
// of 'addr_reg' at the end of the load is returned. This allows for known
// register value optimisations.
// 2 instructions.
static osal_inline uint8_t* output_lh_abs(uint8_t* dst, uint32_t* avail, const void* mem, u_int addr_reg, u_int rd, uint32_t* known_value)
{
	CHECK_REG(addr_reg);
	CHECK_REG(rd);
	u_short mem_hi = (((u_int) mem) + 0x8000) >> 16;
	*known_value = mem_hi << 16;
	dst = output_lui(dst, avail, addr_reg, mem_hi);
	short mem_off = (short) (((int) mem) & 0xFFFF);
	return output_lh(dst, avail, addr_reg, mem_off, rd);
}

// Emits a load of register 'rd' from an absolute memory address, optimising
// for the known value of 1 register. The address is calculated using native
// register 'addr_reg', and the value of 'addr_reg' at the end of the load
// is returned. If 'known_reg' was used and known_reg != addr_reg, 0 is
// returned instead.
// 1 or 2 instructions.
static osal_inline uint8_t* output_lh_abs_1known(uint8_t* dst, uint32_t* avail, const void* mem, u_int addr_reg, u_int rd, u_int known_reg, uint32_t* known_value)
{
	CHECK_REG(addr_reg);
	CHECK_REG(rd);
	CHECK_REG(known_reg);
	int known_off = (int) mem - (int32_t) *known_value;
	if (known_off >= -32768 && known_off <= 32767)
	{
		*known_value = (addr_reg == known_reg) ? *known_value : 0;
		return output_lh(dst, avail, known_reg, known_off, rd);
	}
	else
		return output_lh_abs(dst, avail, mem, addr_reg, rd, known_value);
}

// Emits a load of register 'rd' from an absolute memory address.
// The address is calculated using native register 'addr_reg', and the value
// of 'addr_reg' at the end of the load is returned. This allows for known
// register value optimisations.
// 2 instructions.
static osal_inline uint8_t* output_lhu_abs(uint8_t* dst, uint32_t* avail, const void* mem, u_int addr_reg, u_int rd, uint32_t* known_value)
{
	CHECK_REG(addr_reg);
	CHECK_REG(rd);
	u_short mem_hi = (((u_int) mem) + 0x8000) >> 16;
	*known_value = mem_hi << 16;
	dst = output_lui(dst, avail, addr_reg, mem_hi);
	short mem_off = (short) (((int) mem) & 0xFFFF);
	return output_lhu(dst, avail, addr_reg, mem_off, rd);
}

// Emits a load of register 'rd' from an absolute memory address, optimising
// for the known value of 1 register. The address is calculated using native
// register 'addr_reg', and the value of 'addr_reg' at the end of the load
// is returned. If 'known_reg' was used and known_reg != addr_reg, 0 is
// returned instead.
// 1 or 2 instructions.
static osal_inline uint8_t* output_lhu_abs_1known(uint8_t* dst, uint32_t* avail, const void* mem, u_int addr_reg, u_int rd, u_int known_reg, uint32_t* known_value)
{
	CHECK_REG(addr_reg);
	CHECK_REG(rd);
	CHECK_REG(known_reg);
	int known_off = (int) mem - (int32_t) *known_value;
	if (known_off >= -32768 && known_off <= 32767)
	{
		*known_value = (addr_reg == known_reg) ? *known_value : 0;
		return output_lhu(dst, avail, known_reg, known_off, rd);
	}
	else
		return output_lhu_abs(dst, avail, mem, addr_reg, rd, known_value);
}

// Emits a store of register 'rd' to an absolute memory address.
// The address is calculated using native register 'addr_reg', and the value
// of 'addr_reg' at the end of the store is returned. This allows for known
// register value optimisations.
// 2 instructions.
static osal_inline uint8_t* output_sh_abs(uint8_t* dst, uint32_t* avail, const void* mem, u_int addr_reg, u_int rd, uint32_t* known_value)
{
	CHECK_REG(addr_reg);
	CHECK_REG(rd);
	u_short mem_hi = (((u_int) mem) + 0x8000) >> 16;
	*known_value = mem_hi << 16;
	dst = output_lui(dst, avail, addr_reg, mem_hi);
	short mem_off = (short) (((int) mem) & 0xFFFF);
	return output_sh(dst, avail, addr_reg, mem_off, rd);
}

// Emits a store of register 'rd' to an absolute memory address, optimising
// for the known value of 1 register. The address is calculated using native
// register 'addr_reg', and the value of 'addr_reg' at the end of the store
// is returned. If 'known_reg' was used and known_reg != addr_reg, 0 is
// returned instead.
// 1 or 2 instructions.
static osal_inline uint8_t* output_sh_abs_1known(uint8_t* dst, uint32_t* avail, const void* mem, u_int addr_reg, u_int rd, u_int known_reg, uint32_t* known_value)
{
	CHECK_REG(addr_reg);
	CHECK_REG(rd);
	CHECK_REG(known_reg);
	int known_off = (int) mem - (int32_t) *known_value;
	if (known_off >= -32768 && known_off <= 32767)
	{
		*known_value = (addr_reg == known_reg) ? *known_value : 0;
		return output_sh(dst, avail, known_reg, known_off, rd);
	}
	else
		return output_sh_abs(dst, avail, mem, addr_reg, rd, known_value);
}

// Emits a load of register 'rd' from an absolute memory address.
// The address is calculated using native register 'addr_reg', and the value
// of 'addr_reg' at the end of the load is returned. This allows for known
// register value optimisations.
// 2 instructions.
static osal_inline uint8_t* output_lw_abs(uint8_t* dst, uint32_t* avail, const void* mem, u_int addr_reg, u_int rd, uint32_t* known_value)
{
	CHECK_REG(addr_reg);
	CHECK_REG(rd);
	u_short mem_hi = (((u_int) mem) + 0x8000) >> 16;
	*known_value = mem_hi << 16;
	dst = output_lui(dst, avail, addr_reg, mem_hi);
	short mem_off = (short) (((int) mem) & 0xFFFF);
	return output_lw(dst, avail, addr_reg, mem_off, rd);
}

// Emits a load of register 'rd' from an absolute memory address, optimising
// for the known value of 1 register. The address is calculated using native
// register 'addr_reg', and the value of 'addr_reg' at the end of the load
// is returned. If 'known_reg' was used and known_reg != addr_reg, 0 is
// returned instead.
// 1 or 2 instructions.
static osal_inline uint8_t* output_lw_abs_1known(uint8_t* dst, uint32_t* avail, const void* mem, u_int addr_reg, u_int rd, u_int known_reg, uint32_t* known_value)
{
	CHECK_REG(addr_reg);
	CHECK_REG(rd);
	CHECK_REG(known_reg);
	int known_off = (int) mem - (int32_t) *known_value;
	if (known_off >= -32768 && known_off <= 32767)
	{
		*known_value = (addr_reg == known_reg) ? *known_value : 0;
		return output_lw(dst, avail, known_reg, known_off, rd);
	}
	else
		return output_lw_abs(dst, avail, mem, addr_reg, rd, known_value);
}

// Emits a store of register 'rd' to an absolute memory address.
// The address is calculated using native register 'addr_reg', and the value
// of 'addr_reg' at the end of the store is returned. This allows for known
// register value optimisations.
// 2 instructions.
static osal_inline uint8_t* output_sw_abs(uint8_t* dst, uint32_t* avail, const void* mem, u_int addr_reg, u_int rd, uint32_t* known_value)
{
	CHECK_REG(addr_reg);
	CHECK_REG(rd);
	u_short mem_hi = (((u_int) mem) + 0x8000) >> 16;
	*known_value = mem_hi << 16;
	dst = output_lui(dst, avail, addr_reg, mem_hi);
	short mem_off = (short) (((int) mem) & 0xFFFF);
	return output_sw(dst, avail, addr_reg, mem_off, rd);
}

// Emits a store of register 'rd' to an absolute memory address, optimising
// for the known value of 1 register. The address is calculated using native
// register 'addr_reg', and the value of 'addr_reg' at the end of the store
// is returned. If 'known_reg' was used and known_reg != addr_reg, 0 is
// returned instead.
// 1 or 2 instructions.
static osal_inline uint8_t* output_sw_abs_1known(uint8_t* dst, uint32_t* avail, const void* mem, u_int addr_reg, u_int rd, u_int known_reg, uint32_t* known_value)
{
	CHECK_REG(addr_reg);
	CHECK_REG(rd);
	CHECK_REG(known_reg);
	int known_off = (int) mem - (int32_t) *known_value;
	if (known_off >= -32768 && known_off <= 32767)
	{
		*known_value = (addr_reg == known_reg) ? *known_value : 0;
		return output_sw(dst, avail, known_reg, known_off, rd);
	}
	else
		return output_sw_abs(dst, avail, mem, addr_reg, rd, known_value);
}

// Emits a load of register 'fd' from an absolute memory address.
// The address is calculated using native register 'addr_reg', and the value
// of 'addr_reg' at the end of the load is returned. This allows for known
// register value optimisations.
// 2 instructions.
static osal_inline uint8_t* output_lwc1_abs(uint8_t* dst, uint32_t* avail, const void* mem, u_int addr_reg, u_int fd, uint32_t* known_value)
{
	CHECK_REG(addr_reg);
	CHECK_REG(fd);
	u_short mem_hi = (((u_int) mem) + 0x8000) >> 16;
	*known_value = mem_hi << 16;
	dst = output_lui(dst, avail, addr_reg, mem_hi);
	short mem_off = (short) (((int) mem) & 0xFFFF);
	return output_lwc1(dst, avail, addr_reg, mem_off, fd);
}

// Emits a load of register 'fd' from an absolute memory address, optimising
// for the known value of 1 register. The address is calculated using native
// register 'addr_reg', and the value of 'addr_reg' at the end of the load
// is returned. If 'known_reg' was used and known_reg != addr_reg, 0 is
// returned instead.
// 1 or 2 instructions.
static osal_inline uint8_t* output_lwc1_abs_1known(uint8_t* dst, uint32_t* avail, const void* mem, u_int addr_reg, u_int fd, u_int known_reg, uint32_t* known_value)
{
	CHECK_REG(addr_reg);
	CHECK_REG(fd);
	CHECK_REG(known_reg);
	int known_off = (int) mem - (int32_t) *known_value;
	if (known_off >= -32768 && known_off <= 32767)
	{
		*known_value = (addr_reg == known_reg) ? *known_value : 0;
		return output_lwc1(dst, avail, known_reg, known_off, fd);
	}
	else
		return output_lwc1_abs(dst, avail, mem, addr_reg, fd, known_value);
}

// Emits a store of register 'fd' to an absolute memory address.
// The address is calculated using native register 'addr_reg', and the value
// of 'addr_reg' at the end of the store is returned. This allows for known
// register value optimisations.
// 2 instructions.
static osal_inline uint8_t* output_swc1_abs(uint8_t* dst, uint32_t* avail, const void* mem, u_int addr_reg, u_int fd, uint32_t* known_value)
{
	CHECK_REG(addr_reg);
	CHECK_REG(fd);
	u_short mem_hi = (((u_int) mem) + 0x8000) >> 16;
	*known_value = mem_hi << 16;
	dst = output_lui(dst, avail, addr_reg, mem_hi);
	short mem_off = (short) (((int) mem) & 0xFFFF);
	return output_swc1(dst, avail, addr_reg, mem_off, fd);
}

// Emits a store of register 'fd' to an absolute memory address, optimising
// for the known value of 1 register. The address is calculated using native
// register 'addr_reg', and the value of 'addr_reg' at the end of the store
// is returned. If 'known_reg' was used and known_reg != addr_reg, 0 is
// returned instead.
// 1 or 2 instructions.
static osal_inline uint8_t* output_swc1_abs_1known(uint8_t* dst, uint32_t* avail, const void* mem, u_int addr_reg, u_int fd, u_int known_reg, uint32_t* known_value)
{
	CHECK_REG(addr_reg);
	CHECK_REG(fd);
	CHECK_REG(known_reg);
	int known_off = (int) mem - (int32_t) *known_value;
	if (known_off >= -32768 && known_off <= 32767)
	{
		*known_value = (addr_reg == known_reg) ? *known_value : 0;
		return output_swc1(dst, avail, known_reg, known_off, fd);
	}
	else
		return output_swc1_abs(dst, avail, mem, addr_reg, fd, known_value);
}

// Emits a load of register 'fd' from an absolute memory address.
// The address is calculated using native register 'addr_reg', and the value
// of 'addr_reg' at the end of the load is returned. This allows for known
// register value optimisations.
// 2 instructions.
static osal_inline uint8_t* output_ldc1_abs(uint8_t* dst, uint32_t* avail, const void* mem, u_int addr_reg, u_int fd, uint32_t* known_value)
{
	CHECK_REG(addr_reg);
	CHECK_REG(fd);
	u_short mem_hi = (((u_int) mem) + 0x8000) >> 16;
	*known_value = mem_hi << 16;
	dst = output_lui(dst, avail, addr_reg, mem_hi);
	short mem_off = (short) (((int) mem) & 0xFFFF);
	return output_ldc1(dst, avail, addr_reg, mem_off, fd);
}

// Emits a load of register 'fd' from an absolute memory address, optimising
// for the known value of 1 register. The address is calculated using native
// register 'addr_reg', and the value of 'addr_reg' at the end of the load
// is returned. If 'known_reg' was used and known_reg != addr_reg, 0 is
// returned instead.
// 1 or 2 instructions.
static osal_inline uint8_t* output_ldc1_abs_1known(uint8_t* dst, uint32_t* avail, const void* mem, u_int addr_reg, u_int fd, u_int known_reg, uint32_t* known_value)
{
	CHECK_REG(addr_reg);
	CHECK_REG(fd);
	CHECK_REG(known_reg);
	int known_off = (int) mem - (int32_t) *known_value;
	if (known_off >= -32768 && known_off <= 32767)
	{
		*known_value = (addr_reg == known_reg) ? *known_value : 0;
		return output_ldc1(dst, avail, known_reg, known_off, fd);
	}
	else
		return output_ldc1_abs(dst, avail, mem, addr_reg, fd, known_value);
}

// Emits a store of register 'fd' to an absolute memory address.
// The address is calculated using native register 'addr_reg', and the value
// of 'addr_reg' at the end of the store is returned. This allows for known
// register value optimisations.
// 2 instructions.
static osal_inline uint8_t* output_sdc1_abs(uint8_t* dst, uint32_t* avail, const void* mem, u_int addr_reg, u_int fd, uint32_t* known_value)
{
	CHECK_REG(addr_reg);
	CHECK_REG(fd);
	u_short mem_hi = (((u_int) mem) + 0x8000) >> 16;
	*known_value = mem_hi << 16;
	dst = output_lui(dst, avail, addr_reg, mem_hi);
	short mem_off = (short) (((int) mem) & 0xFFFF);
	return output_sdc1(dst, avail, addr_reg, mem_off, fd);
}

// Emits a store of register 'fd' to an absolute memory address, optimising
// for the known value of 1 register. The address is calculated using native
// register 'addr_reg', and the value of 'addr_reg' at the end of the store
// is returned. If 'known_reg' was used and known_reg != addr_reg, 0 is
// returned instead.
// 1 or 2 instructions.
static osal_inline uint8_t* output_sdc1_abs_1known(uint8_t* dst, uint32_t* avail, const void* mem, u_int addr_reg, u_int fd, u_int known_reg, uint32_t* known_value)
{
	CHECK_REG(addr_reg);
	CHECK_REG(fd);
	CHECK_REG(known_reg);
	int known_off = (int) mem - (int32_t) *known_value;
	if (known_off >= -32768 && known_off <= 32767)
	{
		*known_value = (addr_reg == known_reg) ? *known_value : 0;
		return output_sdc1(dst, avail, known_reg, known_off, fd);
	}
	else
		return output_sdc1_abs(dst, avail, mem, addr_reg, fd, known_value);
}

static osal_inline void jump_from_past(uint8_t* Source, uint8_t* Destination)
{
	assert(Source != NULL);
	assert(Destination != NULL);
	*((int16_t*) Source) = (int16_t) ((Destination - Source - 4) >> 2);
}

// Switches the two instructions immediately before NextCode.
// The intent is that the last instruction is a jump, and the instruction
// before that is not.
static osal_inline void switch_delay_slot(uint8_t* dst)
{
	uint32_t temp = *((uint32_t*) dst - 2);
	*((uint32_t*) dst - 2) = *((uint32_t*) dst - 1);
	*((uint32_t*) dst - 1) = temp;
}

#endif // ASSEMBLE_H
