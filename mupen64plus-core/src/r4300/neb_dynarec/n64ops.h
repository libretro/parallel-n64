/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - Neb Dynarec - Common analysis code declarations         *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
 *   Copyright (C) 2014 Nebuleon                                           *
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

#ifndef __NEB_DYNAREC_N64OPS_H__
#define __NEB_DYNAREC_N64OPS_H__

#include <stdint.h>
#include <stdbool.h>

#include "../recomp.h" /* for precomp_instr */

#define MAKE_OP_TYPE(n)  ((n) * 0x40)
#define GET_OP_TYPE(n)   (((n) / 0x40) * 0x40)
// These constants represent broad categories of opcodes based on their
// operand counts, types and basic functions.

// Each of these operations is the only one of its category.
#define N64_OP_TYPE_MISCELLANEOUS        MAKE_OP_TYPE(0)
// OP Rd, Rs, Rt
#define N64_OP_TYPE_3OP_INT              MAKE_OP_TYPE(1)
// OP Rd, Rs, Sa
#define N64_OP_TYPE_2OP_INT_SHIFT        MAKE_OP_TYPE(2)
// OP Rd, Rs, Imm16
#define N64_OP_TYPE_2OP_INT_IMM16        MAKE_OP_TYPE(3)
// OP Rt, Imm16(Rs)
#define N64_OP_TYPE_MEM_LOAD             MAKE_OP_TYPE(4)
// OP Rt, Imm16(Rs)
#define N64_OP_TYPE_MEM_STORE            MAKE_OP_TYPE(5)
// MF?? Rd [implied HI/LO]
#define N64_OP_TYPE_1OP_INT_FROM_HILO    MAKE_OP_TYPE(6)
// MT?? Rs [implied HI/LO]
#define N64_OP_TYPE_1OP_INT_TO_HILO      MAKE_OP_TYPE(7)
// OP Rs, Rt [implied HI/LO]
#define N64_OP_TYPE_2OP_INT_TO_HILO      MAKE_OP_TYPE(8)
// OP Dest26
#define N64_OP_TYPE_JUMP_IMM26           MAKE_OP_TYPE(9)
// OP Rs
#define N64_OP_TYPE_JUMP_1OP_INT         MAKE_OP_TYPE(10)
// OP Rs, Rt [conditional trap]
#define N64_OP_TYPE_TRAP_2OP_INT         MAKE_OP_TYPE(11)
// OP Rs, Imm16 [conditional trap]
#define N64_OP_TYPE_TRAP_1OP_INT_IMM16   MAKE_OP_TYPE(12)
// OP Rs, Imm16 [implied 0]
#define N64_OP_TYPE_BRANCH_1OP_INT_IMM16 MAKE_OP_TYPE(13)
// OP Rs, Rt, Imm16
#define N64_OP_TYPE_BRANCH_2OP_INT       MAKE_OP_TYPE(14)
// OP Fd32, Fs32, Ft32
#define N64_OP_TYPE_3OP_FP_S             MAKE_OP_TYPE(15)
// OP Fd32, Fs32
#define N64_OP_TYPE_2OP_FP_S             MAKE_OP_TYPE(16)
// OP Fd64, Fs32
#define N64_OP_TYPE_FP_S_TO_FP_D         MAKE_OP_TYPE(17)
// OP Fs32, Ft32 [implied Coprocessor 1 condition bit]
#define N64_OP_TYPE_COMPARE_2OP_FP_S     MAKE_OP_TYPE(18)
// OP Fd64, Fs64, Ft64
#define N64_OP_TYPE_3OP_FP_D             MAKE_OP_TYPE(19)
// OP Fd64, Fs64
#define N64_OP_TYPE_2OP_FP_D             MAKE_OP_TYPE(20)
// OP Fd32, Fs64
#define N64_OP_TYPE_FP_D_TO_FP_S         MAKE_OP_TYPE(21)
// OP Fs64, Ft64 [implied Coprocessor 1 condition bit]
#define N64_OP_TYPE_COMPARE_2OP_FP_D     MAKE_OP_TYPE(22)
// OP Imm16 [implied Coprocessor 1 condition bit]
#define N64_OP_TYPE_BRANCH_FP_COND       MAKE_OP_TYPE(23)

// Guaranteed to be larger than any N64_OP_* value. Usable as an array bound.
#define N64_OP_MAX                       MAKE_OP_TYPE(24)

// These constants are used in the instruction analysis structure.
// They tell the dynamic recompiler which opcode is used for an instruction,
// as well as which fields are valid in each instruction analysis structure.

// The following relations are observed in this enumeration:
// * For branch opcodes supporting _OUT and _IDLE variants, for out-of-page
//   branches and branches back to the same instruction, _OUT has the value
//   of the in-page jump + 1, and _IDLE has that value + 2.
// * The Branch Likely variant of each conditional branch instruction has
//   the same value as the ordinary instruction + 3.
enum n64_opcode {
	// Broad categories. Any opcode fitting these is implemented the same.
	N64_OP_NOP           = N64_OP_TYPE_MISCELLANEOUS + 0,
	N64_OP_RESERVED      = N64_OP_TYPE_MISCELLANEOUS + 1,
	N64_OP_UNIMPLEMENTED = N64_OP_TYPE_MISCELLANEOUS + 22,

	// The Arithmetic and Logic Unit (ALU) integer opcodes.
	// - without immediates.
	N64_OP_SLL           = N64_OP_TYPE_2OP_INT_SHIFT + 0,
	N64_OP_SRL           = N64_OP_TYPE_2OP_INT_SHIFT + 1,
	N64_OP_SRA           = N64_OP_TYPE_2OP_INT_SHIFT + 2,
	N64_OP_DSLL          = N64_OP_TYPE_2OP_INT_SHIFT + 3,
	N64_OP_DSRL          = N64_OP_TYPE_2OP_INT_SHIFT + 4,
	N64_OP_DSRA          = N64_OP_TYPE_2OP_INT_SHIFT + 5,
	N64_OP_DSLL32        = N64_OP_TYPE_2OP_INT_SHIFT + 6,
	N64_OP_DSRL32        = N64_OP_TYPE_2OP_INT_SHIFT + 7,
	N64_OP_DSRA32        = N64_OP_TYPE_2OP_INT_SHIFT + 8,
	N64_OP_SLLV          = N64_OP_TYPE_3OP_INT + 0,
	N64_OP_SRLV          = N64_OP_TYPE_3OP_INT + 1,
	N64_OP_SRAV          = N64_OP_TYPE_3OP_INT + 2,
	N64_OP_DSLLV         = N64_OP_TYPE_3OP_INT + 3,
	N64_OP_DSRLV         = N64_OP_TYPE_3OP_INT + 4,
	N64_OP_DSRAV         = N64_OP_TYPE_3OP_INT + 5,
	N64_OP_ADD           = N64_OP_TYPE_3OP_INT + 6,
	N64_OP_ADDU          = N64_OP_TYPE_3OP_INT + 7,
	N64_OP_SUB           = N64_OP_TYPE_3OP_INT + 8,
	N64_OP_SUBU          = N64_OP_TYPE_3OP_INT + 9,
	N64_OP_AND           = N64_OP_TYPE_3OP_INT + 10,
	N64_OP_OR            = N64_OP_TYPE_3OP_INT + 11,
	N64_OP_XOR           = N64_OP_TYPE_3OP_INT + 12,
	N64_OP_NOR           = N64_OP_TYPE_3OP_INT + 13,
	N64_OP_SLT           = N64_OP_TYPE_3OP_INT + 14,
	N64_OP_SLTU          = N64_OP_TYPE_3OP_INT + 15,
	N64_OP_DADD          = N64_OP_TYPE_3OP_INT + 16,
	N64_OP_DADDU         = N64_OP_TYPE_3OP_INT + 17,
	N64_OP_DSUB          = N64_OP_TYPE_3OP_INT + 18,
	N64_OP_DSUBU         = N64_OP_TYPE_3OP_INT + 19,

	// - with immediates.
	N64_OP_ADDI          = N64_OP_TYPE_2OP_INT_IMM16 + 0,
	N64_OP_ADDIU         = N64_OP_TYPE_2OP_INT_IMM16 + 1,
	N64_OP_SLTI          = N64_OP_TYPE_2OP_INT_IMM16 + 2,
	N64_OP_SLTIU         = N64_OP_TYPE_2OP_INT_IMM16 + 3,
	N64_OP_ANDI          = N64_OP_TYPE_2OP_INT_IMM16 + 4,
	N64_OP_ORI           = N64_OP_TYPE_2OP_INT_IMM16 + 5,
	N64_OP_XORI          = N64_OP_TYPE_2OP_INT_IMM16 + 6,
	N64_OP_LUI           = N64_OP_TYPE_2OP_INT_IMM16 + 7,
	N64_OP_DADDI         = N64_OP_TYPE_2OP_INT_IMM16 + 8,
	N64_OP_DADDIU        = N64_OP_TYPE_2OP_INT_IMM16 + 9,

	// The Memory Load and Store opcodes.
	N64_OP_LDL           = N64_OP_TYPE_MEM_LOAD + 0,
	N64_OP_LDR           = N64_OP_TYPE_MEM_LOAD + 1,
	N64_OP_LB            = N64_OP_TYPE_MEM_LOAD + 2,
	N64_OP_LH            = N64_OP_TYPE_MEM_LOAD + 3,
	N64_OP_LWL           = N64_OP_TYPE_MEM_LOAD + 4,
	N64_OP_LW            = N64_OP_TYPE_MEM_LOAD + 5,
	N64_OP_LBU           = N64_OP_TYPE_MEM_LOAD + 6,
	N64_OP_LHU           = N64_OP_TYPE_MEM_LOAD + 7,
	N64_OP_LWR           = N64_OP_TYPE_MEM_LOAD + 8,
	N64_OP_LWU           = N64_OP_TYPE_MEM_LOAD + 9,
	N64_OP_LL            = N64_OP_TYPE_MEM_LOAD + 10,
	N64_OP_LLD           = N64_OP_TYPE_MEM_LOAD + 11,
	N64_OP_LD            = N64_OP_TYPE_MEM_LOAD + 12,
	N64_OP_SB            = N64_OP_TYPE_MEM_STORE + 0,
	N64_OP_SH            = N64_OP_TYPE_MEM_STORE + 1,
	N64_OP_SWL           = N64_OP_TYPE_MEM_STORE + 2,
	N64_OP_SW            = N64_OP_TYPE_MEM_STORE + 3,
	N64_OP_SDL           = N64_OP_TYPE_MEM_STORE + 4,
	N64_OP_SDR           = N64_OP_TYPE_MEM_STORE + 5,
	N64_OP_SWR           = N64_OP_TYPE_MEM_STORE + 6,
	N64_OP_SC            = N64_OP_TYPE_MEM_STORE + 7,
	N64_OP_SCD           = N64_OP_TYPE_MEM_STORE + 8,
	N64_OP_SD            = N64_OP_TYPE_MEM_STORE + 9,

	// The Multiplier Unit opcodes.
	N64_OP_MFHI          = N64_OP_TYPE_1OP_INT_FROM_HILO + 0,
	N64_OP_MFLO          = N64_OP_TYPE_1OP_INT_FROM_HILO + 1,
	N64_OP_MTHI          = N64_OP_TYPE_1OP_INT_TO_HILO + 0,
	N64_OP_MTLO          = N64_OP_TYPE_1OP_INT_TO_HILO + 1,
	N64_OP_MULT          = N64_OP_TYPE_2OP_INT_TO_HILO + 0,
	N64_OP_MULTU         = N64_OP_TYPE_2OP_INT_TO_HILO + 1,
	N64_OP_DIV           = N64_OP_TYPE_2OP_INT_TO_HILO + 2,
	N64_OP_DIVU          = N64_OP_TYPE_2OP_INT_TO_HILO + 3,
	N64_OP_DMULT         = N64_OP_TYPE_2OP_INT_TO_HILO + 4,
	N64_OP_DMULTU        = N64_OP_TYPE_2OP_INT_TO_HILO + 5,
	N64_OP_DDIV          = N64_OP_TYPE_2OP_INT_TO_HILO + 6,
	N64_OP_DDIVU         = N64_OP_TYPE_2OP_INT_TO_HILO + 7,

	// The Branch, Jump and Exception opcodes.
	N64_OP_J             = N64_OP_TYPE_JUMP_IMM26 + 0,
	N64_OP_J_OUT         = N64_OP_TYPE_JUMP_IMM26 + 1,
	N64_OP_J_IDLE        = N64_OP_TYPE_JUMP_IMM26 + 2,
	N64_OP_JAL           = N64_OP_TYPE_JUMP_IMM26 + 3,
	N64_OP_JAL_OUT       = N64_OP_TYPE_JUMP_IMM26 + 4,
	N64_OP_JAL_IDLE      = N64_OP_TYPE_JUMP_IMM26 + 5,
	N64_OP_JR            = N64_OP_TYPE_JUMP_1OP_INT + 0,
	N64_OP_JALR          = N64_OP_TYPE_JUMP_1OP_INT + 1,
	N64_OP_SYSCALL       = N64_OP_TYPE_MISCELLANEOUS + 2,
	N64_OP_ERET          = N64_OP_TYPE_MISCELLANEOUS + 3,
	N64_OP_TGE           = N64_OP_TYPE_TRAP_2OP_INT + 0,
	N64_OP_TGEU          = N64_OP_TYPE_TRAP_2OP_INT + 1,
	N64_OP_TLT           = N64_OP_TYPE_TRAP_2OP_INT + 2,
	N64_OP_TLTU          = N64_OP_TYPE_TRAP_2OP_INT + 3,
	N64_OP_TEQ           = N64_OP_TYPE_TRAP_2OP_INT + 4,
	N64_OP_TNE           = N64_OP_TYPE_TRAP_2OP_INT + 5,
	N64_OP_TGEI          = N64_OP_TYPE_TRAP_1OP_INT_IMM16 + 0,
	N64_OP_TGEIU         = N64_OP_TYPE_TRAP_1OP_INT_IMM16 + 1,
	N64_OP_TLTI          = N64_OP_TYPE_TRAP_1OP_INT_IMM16 + 2,
	N64_OP_TLTIU         = N64_OP_TYPE_TRAP_1OP_INT_IMM16 + 3,
	N64_OP_TEQI          = N64_OP_TYPE_TRAP_1OP_INT_IMM16 + 4,
	N64_OP_TNEI          = N64_OP_TYPE_TRAP_1OP_INT_IMM16 + 5,
	N64_OP_BLTZ          = N64_OP_TYPE_BRANCH_1OP_INT_IMM16 + 0,
	N64_OP_BLTZ_OUT      = N64_OP_TYPE_BRANCH_1OP_INT_IMM16 + 1,
	N64_OP_BLTZ_IDLE     = N64_OP_TYPE_BRANCH_1OP_INT_IMM16 + 2,
	N64_OP_BLTZL         = N64_OP_TYPE_BRANCH_1OP_INT_IMM16 + 3,
	N64_OP_BLTZL_OUT     = N64_OP_TYPE_BRANCH_1OP_INT_IMM16 + 4,
	N64_OP_BLTZL_IDLE    = N64_OP_TYPE_BRANCH_1OP_INT_IMM16 + 5,
	N64_OP_BGEZ          = N64_OP_TYPE_BRANCH_1OP_INT_IMM16 + 6,
	N64_OP_BGEZ_OUT      = N64_OP_TYPE_BRANCH_1OP_INT_IMM16 + 7,
	N64_OP_BGEZ_IDLE     = N64_OP_TYPE_BRANCH_1OP_INT_IMM16 + 8,
	N64_OP_BGEZL         = N64_OP_TYPE_BRANCH_1OP_INT_IMM16 + 9,
	N64_OP_BGEZL_OUT     = N64_OP_TYPE_BRANCH_1OP_INT_IMM16 + 10,
	N64_OP_BGEZL_IDLE    = N64_OP_TYPE_BRANCH_1OP_INT_IMM16 + 11,
	N64_OP_BLTZAL        = N64_OP_TYPE_BRANCH_1OP_INT_IMM16 + 12,
	N64_OP_BLTZAL_OUT    = N64_OP_TYPE_BRANCH_1OP_INT_IMM16 + 13,
	N64_OP_BLTZAL_IDLE   = N64_OP_TYPE_BRANCH_1OP_INT_IMM16 + 14,
	N64_OP_BLTZALL       = N64_OP_TYPE_BRANCH_1OP_INT_IMM16 + 15,
	N64_OP_BLTZALL_OUT   = N64_OP_TYPE_BRANCH_1OP_INT_IMM16 + 16,
	N64_OP_BLTZALL_IDLE  = N64_OP_TYPE_BRANCH_1OP_INT_IMM16 + 17,
	N64_OP_BGEZAL        = N64_OP_TYPE_BRANCH_1OP_INT_IMM16 + 18,
	N64_OP_BGEZAL_OUT    = N64_OP_TYPE_BRANCH_1OP_INT_IMM16 + 19,
	N64_OP_BGEZAL_IDLE   = N64_OP_TYPE_BRANCH_1OP_INT_IMM16 + 20,
	N64_OP_BGEZALL       = N64_OP_TYPE_BRANCH_1OP_INT_IMM16 + 21,
	N64_OP_BGEZALL_OUT   = N64_OP_TYPE_BRANCH_1OP_INT_IMM16 + 22,
	N64_OP_BGEZALL_IDLE  = N64_OP_TYPE_BRANCH_1OP_INT_IMM16 + 23,
	N64_OP_BLEZ          = N64_OP_TYPE_BRANCH_1OP_INT_IMM16 + 24,
	N64_OP_BLEZ_OUT      = N64_OP_TYPE_BRANCH_1OP_INT_IMM16 + 25,
	N64_OP_BLEZ_IDLE     = N64_OP_TYPE_BRANCH_1OP_INT_IMM16 + 26,
	N64_OP_BLEZL         = N64_OP_TYPE_BRANCH_1OP_INT_IMM16 + 27,
	N64_OP_BLEZL_OUT     = N64_OP_TYPE_BRANCH_1OP_INT_IMM16 + 28,
	N64_OP_BLEZL_IDLE    = N64_OP_TYPE_BRANCH_1OP_INT_IMM16 + 29,
	N64_OP_BGTZ          = N64_OP_TYPE_BRANCH_1OP_INT_IMM16 + 30,
	N64_OP_BGTZ_OUT      = N64_OP_TYPE_BRANCH_1OP_INT_IMM16 + 31,
	N64_OP_BGTZ_IDLE     = N64_OP_TYPE_BRANCH_1OP_INT_IMM16 + 32,
	N64_OP_BGTZL         = N64_OP_TYPE_BRANCH_1OP_INT_IMM16 + 33,
	N64_OP_BGTZL_OUT     = N64_OP_TYPE_BRANCH_1OP_INT_IMM16 + 34,
	N64_OP_BGTZL_IDLE    = N64_OP_TYPE_BRANCH_1OP_INT_IMM16 + 35,
	N64_OP_BEQ           = N64_OP_TYPE_BRANCH_2OP_INT + 0,
	N64_OP_BEQ_OUT       = N64_OP_TYPE_BRANCH_2OP_INT + 1,
	N64_OP_BEQ_IDLE      = N64_OP_TYPE_BRANCH_2OP_INT + 2,
	N64_OP_BEQL          = N64_OP_TYPE_BRANCH_2OP_INT + 3,
	N64_OP_BEQL_OUT      = N64_OP_TYPE_BRANCH_2OP_INT + 4,
	N64_OP_BEQL_IDLE     = N64_OP_TYPE_BRANCH_2OP_INT + 5,
	N64_OP_BNE           = N64_OP_TYPE_BRANCH_2OP_INT + 6,
	N64_OP_BNE_OUT       = N64_OP_TYPE_BRANCH_2OP_INT + 7,
	N64_OP_BNE_IDLE      = N64_OP_TYPE_BRANCH_2OP_INT + 8,
	N64_OP_BNEL          = N64_OP_TYPE_BRANCH_2OP_INT + 9,
	N64_OP_BNEL_OUT      = N64_OP_TYPE_BRANCH_2OP_INT + 10,
	N64_OP_BNEL_IDLE     = N64_OP_TYPE_BRANCH_2OP_INT + 11,

	// The Memory Maintenance opcodes.
	N64_OP_SYNC          = N64_OP_TYPE_MISCELLANEOUS + 4,
	N64_OP_CACHE         = N64_OP_TYPE_MISCELLANEOUS + 5,

	// The Coprocessor 0 opcodes.
	N64_OP_MFC0          = N64_OP_TYPE_MISCELLANEOUS + 6,
	N64_OP_MTC0          = N64_OP_TYPE_MISCELLANEOUS + 7,
	N64_OP_TLBR          = N64_OP_TYPE_MISCELLANEOUS + 8,
	N64_OP_TLBWI         = N64_OP_TYPE_MISCELLANEOUS + 9,
	N64_OP_TLBWR         = N64_OP_TYPE_MISCELLANEOUS + 10,
	N64_OP_TLBP          = N64_OP_TYPE_MISCELLANEOUS + 11,

	// The Coprocessor 1 (floating-point) arithmetic opcodes.
	// - 32-bit floating-point or integer operands.
	N64_OP_ABS_S         = N64_OP_TYPE_2OP_FP_S + 0,
	N64_OP_MOV_S         = N64_OP_TYPE_2OP_FP_S + 1,
	N64_OP_NEG_S         = N64_OP_TYPE_2OP_FP_S + 2,
	N64_OP_ROUND_W_S     = N64_OP_TYPE_2OP_FP_S + 3,
	N64_OP_TRUNC_W_S     = N64_OP_TYPE_2OP_FP_S + 4,
	N64_OP_CEIL_W_S      = N64_OP_TYPE_2OP_FP_S + 5,
	N64_OP_FLOOR_W_S     = N64_OP_TYPE_2OP_FP_S + 6,
	N64_OP_CVT_W_S       = N64_OP_TYPE_2OP_FP_S + 7,
	N64_OP_CVT_S_W       = N64_OP_TYPE_2OP_FP_S + 8,
	N64_OP_ADD_S         = N64_OP_TYPE_3OP_FP_S + 0,
	N64_OP_SUB_S         = N64_OP_TYPE_3OP_FP_S + 1,
	N64_OP_MUL_S         = N64_OP_TYPE_3OP_FP_S + 2,
	N64_OP_DIV_S         = N64_OP_TYPE_3OP_FP_S + 3,
	N64_OP_SQRT_S        = N64_OP_TYPE_3OP_FP_S + 4,
	N64_OP_ROUND_L_S     = N64_OP_TYPE_FP_S_TO_FP_D + 0,
	N64_OP_TRUNC_L_S     = N64_OP_TYPE_FP_S_TO_FP_D + 1,
	N64_OP_CEIL_L_S      = N64_OP_TYPE_FP_S_TO_FP_D + 2,
	N64_OP_FLOOR_L_S     = N64_OP_TYPE_FP_S_TO_FP_D + 3,
	N64_OP_CVT_D_S       = N64_OP_TYPE_FP_S_TO_FP_D + 4,
	N64_OP_CVT_L_S       = N64_OP_TYPE_FP_S_TO_FP_D + 5,
	N64_OP_CVT_D_W       = N64_OP_TYPE_FP_S_TO_FP_D + 6,
	N64_OP_C_F_S         = N64_OP_TYPE_COMPARE_2OP_FP_S + 0,
	N64_OP_C_UN_S        = N64_OP_TYPE_COMPARE_2OP_FP_S + 1,
	N64_OP_C_EQ_S        = N64_OP_TYPE_COMPARE_2OP_FP_S + 2,
	N64_OP_C_UEQ_S       = N64_OP_TYPE_COMPARE_2OP_FP_S + 3,
	N64_OP_C_OLT_S       = N64_OP_TYPE_COMPARE_2OP_FP_S + 4,
	N64_OP_C_ULT_S       = N64_OP_TYPE_COMPARE_2OP_FP_S + 5,
	N64_OP_C_OLE_S       = N64_OP_TYPE_COMPARE_2OP_FP_S + 6,
	N64_OP_C_ULE_S       = N64_OP_TYPE_COMPARE_2OP_FP_S + 7,
	N64_OP_C_SF_S        = N64_OP_TYPE_COMPARE_2OP_FP_S + 8,
	N64_OP_C_NGLE_S      = N64_OP_TYPE_COMPARE_2OP_FP_S + 9,
	N64_OP_C_SEQ_S       = N64_OP_TYPE_COMPARE_2OP_FP_S + 10,
	N64_OP_C_NGL_S       = N64_OP_TYPE_COMPARE_2OP_FP_S + 11,
	N64_OP_C_LT_S        = N64_OP_TYPE_COMPARE_2OP_FP_S + 12,
	N64_OP_C_NGE_S       = N64_OP_TYPE_COMPARE_2OP_FP_S + 13,
	N64_OP_C_LE_S        = N64_OP_TYPE_COMPARE_2OP_FP_S + 14,
	N64_OP_C_NGT_S       = N64_OP_TYPE_COMPARE_2OP_FP_S + 15,

	// - 64-bit floating-point or integer operands.
	N64_OP_ABS_D         = N64_OP_TYPE_2OP_FP_D + 0,
	N64_OP_MOV_D         = N64_OP_TYPE_2OP_FP_D + 1,
	N64_OP_NEG_D         = N64_OP_TYPE_2OP_FP_D + 2,
	N64_OP_ROUND_L_D     = N64_OP_TYPE_2OP_FP_D + 3,
	N64_OP_TRUNC_L_D     = N64_OP_TYPE_2OP_FP_D + 4,
	N64_OP_CEIL_L_D      = N64_OP_TYPE_2OP_FP_D + 5,
	N64_OP_FLOOR_L_D     = N64_OP_TYPE_2OP_FP_D + 6,
	N64_OP_CVT_L_D       = N64_OP_TYPE_2OP_FP_D + 7,
	N64_OP_CVT_D_L       = N64_OP_TYPE_2OP_FP_D + 8,
	N64_OP_ADD_D         = N64_OP_TYPE_3OP_FP_D + 0,
	N64_OP_SUB_D         = N64_OP_TYPE_3OP_FP_D + 1,
	N64_OP_MUL_D         = N64_OP_TYPE_3OP_FP_D + 2,
	N64_OP_DIV_D         = N64_OP_TYPE_3OP_FP_D + 3,
	N64_OP_SQRT_D        = N64_OP_TYPE_3OP_FP_D + 4,
	N64_OP_ROUND_W_D     = N64_OP_TYPE_FP_D_TO_FP_S + 0,
	N64_OP_TRUNC_W_D     = N64_OP_TYPE_FP_D_TO_FP_S + 1,
	N64_OP_CEIL_W_D      = N64_OP_TYPE_FP_D_TO_FP_S + 2,
	N64_OP_FLOOR_W_D     = N64_OP_TYPE_FP_D_TO_FP_S + 3,
	N64_OP_CVT_S_D       = N64_OP_TYPE_FP_D_TO_FP_S + 4,
	N64_OP_CVT_W_D       = N64_OP_TYPE_FP_D_TO_FP_S + 5,
	N64_OP_CVT_S_L       = N64_OP_TYPE_FP_D_TO_FP_S + 6,
	N64_OP_C_F_D         = N64_OP_TYPE_COMPARE_2OP_FP_D + 0,
	N64_OP_C_UN_D        = N64_OP_TYPE_COMPARE_2OP_FP_D + 1,
	N64_OP_C_EQ_D        = N64_OP_TYPE_COMPARE_2OP_FP_D + 2,
	N64_OP_C_UEQ_D       = N64_OP_TYPE_COMPARE_2OP_FP_D + 3,
	N64_OP_C_OLT_D       = N64_OP_TYPE_COMPARE_2OP_FP_D + 4,
	N64_OP_C_ULT_D       = N64_OP_TYPE_COMPARE_2OP_FP_D + 5,
	N64_OP_C_OLE_D       = N64_OP_TYPE_COMPARE_2OP_FP_D + 6,
	N64_OP_C_ULE_D       = N64_OP_TYPE_COMPARE_2OP_FP_D + 7,
	N64_OP_C_SF_D        = N64_OP_TYPE_COMPARE_2OP_FP_D + 8,
	N64_OP_C_NGLE_D      = N64_OP_TYPE_COMPARE_2OP_FP_D + 9,
	N64_OP_C_SEQ_D       = N64_OP_TYPE_COMPARE_2OP_FP_D + 10,
	N64_OP_C_NGL_D       = N64_OP_TYPE_COMPARE_2OP_FP_D + 11,
	N64_OP_C_LT_D        = N64_OP_TYPE_COMPARE_2OP_FP_D + 12,
	N64_OP_C_NGE_D       = N64_OP_TYPE_COMPARE_2OP_FP_D + 13,
	N64_OP_C_LE_D        = N64_OP_TYPE_COMPARE_2OP_FP_D + 14,
	N64_OP_C_NGT_D       = N64_OP_TYPE_COMPARE_2OP_FP_D + 15,

	// The Coprocessor 1 (floating-point) Data Movement opcodes.
	N64_OP_MFC1          = N64_OP_TYPE_MISCELLANEOUS + 12,
	N64_OP_DMFC1         = N64_OP_TYPE_MISCELLANEOUS + 13,
	N64_OP_CFC1          = N64_OP_TYPE_MISCELLANEOUS + 14,
	N64_OP_MTC1          = N64_OP_TYPE_MISCELLANEOUS + 15,
	N64_OP_DMTC1         = N64_OP_TYPE_MISCELLANEOUS + 16,
	N64_OP_CTC1          = N64_OP_TYPE_MISCELLANEOUS + 17,
	N64_OP_LWC1          = N64_OP_TYPE_MISCELLANEOUS + 18,
	N64_OP_LDC1          = N64_OP_TYPE_MISCELLANEOUS + 19,
	N64_OP_SWC1          = N64_OP_TYPE_MISCELLANEOUS + 20,
	N64_OP_SDC1          = N64_OP_TYPE_MISCELLANEOUS + 21,

	// The Coprocessor 1 (floating-point) conditional branch opcodes.
	N64_OP_BC1F          = N64_OP_TYPE_BRANCH_FP_COND + 0,
	N64_OP_BC1F_OUT      = N64_OP_TYPE_BRANCH_FP_COND + 1,
	N64_OP_BC1F_IDLE     = N64_OP_TYPE_BRANCH_FP_COND + 2,
	N64_OP_BC1FL         = N64_OP_TYPE_BRANCH_FP_COND + 3,
	N64_OP_BC1FL_OUT     = N64_OP_TYPE_BRANCH_FP_COND + 4,
	N64_OP_BC1FL_IDLE    = N64_OP_TYPE_BRANCH_FP_COND + 5,
	N64_OP_BC1T          = N64_OP_TYPE_BRANCH_FP_COND + 6,
	N64_OP_BC1T_OUT      = N64_OP_TYPE_BRANCH_FP_COND + 7,
	N64_OP_BC1T_IDLE     = N64_OP_TYPE_BRANCH_FP_COND + 8,
	N64_OP_BC1TL         = N64_OP_TYPE_BRANCH_FP_COND + 9,
	N64_OP_BC1TL_OUT     = N64_OP_TYPE_BRANCH_FP_COND + 10,
	N64_OP_BC1TL_IDLE    = N64_OP_TYPE_BRANCH_FP_COND + 11,
};

typedef enum n64_opcode n64_opcode_t;

struct n64_insn {
	n64_opcode_t opcode;

	precomp_instr* runtime; // The cached interpreter's runtime info structure
	uint32_t addr; // Address of the instruction in N64 memory
	uint16_t branch_flags; // Branch flags or 0 if not a branch: see branches.h
	uint16_t emit_flags; // Emission flags: see emitflags.h

	// Some of these don't apply to all instructions.
	uint8_t rd; // Register Destination (always between 0 and 31)
	uint8_t rs; // Register Source 1 (always between 0 and 31)
	uint8_t rt; // Register Source 2, or Target (always between 0 and 31)
	int32_t imm; // Immediate (5-bit shift amount, 16-bit or 26-bit,
	             // appropriately sign- or zero-extended)
	uint32_t target; // Calculated jump target address
};

typedef struct n64_insn n64_insn_t;

/*
 * Parses a single N64 instruction.
 * 
 * This function takes care of the following:
 * - replacing any operation that would write to N64 integer register 0 with
 *   a NOP, which is mandatory for MIPS processors;
 * - giving the flag BRANCH_UNCONDITIONAL to any conditional branch that is
 *   effectively unconditional, but leaving the actual opcode intact;
 * - removing all branch flags from any regular conditional branch that is
 *   effectively impossible, but leaving the actual opcode intact.
 * 
 * In:
 *   op: The MIPS III instruction to parse.
 *   addr: The address, in the N64 memory space, where 'op' was found.
 * Out:
 *   insn: A pointer to the instruction structure to fill.
 */
extern void parse_n64_insn(const uint32_t op, const uint32_t addr, n64_insn_t* insn);

/*
 * Simplifies a single N64 instruction.
 * 
 * This function takes care of making simplifications that fully apply to
 * single instructions, for example:
 * - turning branches (except Branch Likely) that skip one instruction into
 *   NOP [those go to the same instruction if they succeed or fail, and their
 *   delay slot is executed first in either case];
 * - turning more instructions that have no effect into NOP;
 * - turning instructions whose effect is to move 64-bit values from one
 *   register to another into OR;
 * - turning instructions whose effect is to sign-extend the 32-bit value
 *   in one register to another into ADDU;
 * - turning relative conditional branches that are effectively unconditional
 *   into BGEZ $0, $0, Offset;
 * - turning regular conditional branches that are effectively impossible
 *   into NOP;
 * - turning loads of certain constants into ORI.
 */
extern void simplify_n64_insn(n64_insn_t* insn);

/*
 * Finalises parsing for a group of N64 instructions.
 * 
 * This function takes care of making simplifications and applying final
 * touches to opcodes as part of a range:
 * - making _OUT and _IDLE jumps and branches according to whether they target
 *   instructions inside or outside of a page;
 * - turning Branch Likely instructions that are effectively impossible,
 *   as well as their delay slots, into NOP.
 * 
 * In/Out:
 *   insns: A pointer to at least 'insn_count' instruction structures
 *     previously filled by parse_n64_insn (and possibly simplify_n64_insn).
 * In:
 *   insn_count: The number of instructions being parsed. This is not
 *     necessarily the same as '(page_end - page_start) / 4'.
 *   page_start: The N64 memory address starting the current page.
 *   page_end: The N64 memory address one instruction past the last in the
 *     current page.
 */
extern void post_parse_n64_insns(n64_insn_t* insns, const uint32_t insn_count, const uint32_t page_start, const uint32_t page_end);

/*
 * Gets the name of an analysed opcode for ease of debugging.
 * 
 * In:
 *   opcode: An opcode parsed into 'n64_opcode_t' form.
 * Returns:
 *   A pointer to a string describing the opcode. The number of bytes
 *   will not exceed 13, excluding the terminating null byte.
 *   The return string must not be freed.
 */
extern const char* get_n64_op_name(const n64_opcode_t opcode);

#endif /* !__NEB_DYNAREC_N64OPS_H__ */
