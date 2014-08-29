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

#include "n64ops.h"
#include "branches.h"

#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>

// To parse the instruction fields
#define SET_RD insn->rd = (op >> 11) & 0x1F
#define SET_RS insn->rs = (op >> 21) & 0x1F
#define SET_RT insn->rt = (op >> 16) & 0x1F
#define SET_FD insn->rd = (op >> 6) & 0x1F
#define SET_FS insn->rs = (op >> 11) & 0x1F
#define SET_FT insn->rt = (op >> 16) & 0x1F
#define SET_SA insn->imm = (op >> 6) & 0x1F
#define SET_IMM16 insn->imm = (int32_t) ((int16_t) (op & 0xFFFF))
#define SET_IMM16U insn->imm = (int32_t) ((uint16_t) (op & 0xFFFF))
#define SET_IMM26U insn->imm = (op & 0x03FFFFFF)

// To calculate jump/branch targets
#define SET_TARGET_RELATIVE insn->target = insn->addr + 4 + insn->imm * 4
#define SET_TARGET_ABSOLUTE insn->target = ((insn->addr + 4) & 0xF0000000) | ((insn->imm & 0x03FFFFFF) * 4)

// Mandatory simplifications

// Instructions with immediates that target $0 do not really act.
// Mupen cannot allow these instructions to write new values to it.
#define CHECK_RT_0 do { if (insn->rt == 0) { insn->opcode = N64_OP_NOP; goto end; } } while (0)
// Three-operand instructions that target $0 do not really act.
// Mupen cannot allow these instructions to write new values to it.
#define CHECK_RD_0 do { if (insn->rd == 0) { insn->opcode = N64_OP_NOP; goto end; } } while (0)

// Optional simplifications

// All instructions that move a 32-bit value between registers, including
// instructions that move a 32-bit value from a register into itself, become
// ADDU $x, $y, $0 due to the need to sign-extend bit 31 of the register.
#define REG_MOVE_32(FROM, TO) do { uint8_t rs = FROM, rd = TO; insn->opcode = N64_OP_ADDU; insn->rs = rs; insn->rt = 0; insn->rd = rd; } while (0)

// All instructions that move a 64-bit value between registers, become
// OR $x, $y, $0. Moving a 64-bit register from one register to itself is a
// NOP instead.
#define REG_MOVE_64(FROM, TO) do { uint8_t rs = FROM, rd = TO; insn->opcode = N64_OP_OR; insn->rs = rs; insn->rt = 0; insn->rd = rd; } while (0)

// All instructions that load the value 0 into a register become OR $x, $0, $0.
#define LOAD_0(TO) do { uint8_t rd = TO; insn->opcode = N64_OP_OR; insn->rd = rd; insn->rs = insn->rt = 0; } while (0)

// All instructions that load values between 1 and 65535 become ORI $x, $0,
// IMM16.
#define LOAD_IMM16U(CONST, TO) do { uint8_t rd = TO; uint16_t Const = CONST; insn->opcode = N64_OP_ORI; insn->rt = rd; insn->rs = 0; insn->imm = Const; } while (0)

// Moving a 64-bit register to itself, or initiating an impossible branch,
// creates this.
#define MAKE_NOP do { insn->opcode = N64_OP_NOP; } while (0)

// Unconditional branches, including Branch Likely. [x == x; x >= x, etc.]
#define UNCONDITIONAL_BRANCH do { insn->opcode = N64_OP_BGEZ; insn->rs = 0; } while (0)

void parse_n64_insn(const uint32_t op, const uint32_t addr, n64_insn_t* insn)
{
	insn->addr = addr;
	insn->branch_flags = 0;

	switch ((op >> 26) & 0x3F)
	{
		case  0: // Major opcode 0: SPECIAL
			switch (op & 0x3F)
			{
				case  0: // Minor opcode 0: SLL
					SET_RD; SET_RT; SET_SA; insn->opcode = N64_OP_SLL;
					CHECK_RD_0;
					break;
				case  2: // Minor opcode 2: SRL
					SET_RD; SET_RT; SET_SA; insn->opcode = N64_OP_SRL;
					CHECK_RD_0;
					break;
				case  3: // Minor opcode 3: SRA
					SET_RD; SET_RT; SET_SA; insn->opcode = N64_OP_SRA;
					CHECK_RD_0;
					break;
				case  4: // Minor opcode 4: SLLV
					SET_RD; SET_RT; SET_RS; insn->opcode = N64_OP_SLLV;
					CHECK_RD_0;
					break;
				case  6: // Minor opcode 6: SRLV
					SET_RD; SET_RT; SET_RS; insn->opcode = N64_OP_SRLV;
					CHECK_RD_0;
					break;
				case  7: // Minor opcode 7: SRAV
					SET_RD; SET_RT; SET_RS; insn->opcode = N64_OP_SRAV;
					CHECK_RD_0;
					break;
				case  8: // Minor opcode 8: JR
					SET_RS; insn->opcode = N64_OP_JR;
					insn->branch_flags = BRANCH_N64 | BRANCH_UNCONDITIONAL | BRANCH_VIA_REGISTER | BRANCH_DELAY_SLOT;
					break;
				case  9: // Minor opcode 9: JALR
					SET_RS; SET_RD; insn->opcode = N64_OP_JALR;
					if (insn->rd == 0) // cannot write to $0!
					{
						insn->opcode = N64_OP_JR;
						insn->branch_flags = BRANCH_N64 | BRANCH_UNCONDITIONAL | BRANCH_VIA_REGISTER | BRANCH_DELAY_SLOT;
					}
					else
						insn->branch_flags = BRANCH_N64 | BRANCH_UNCONDITIONAL | BRANCH_VIA_REGISTER | BRANCH_DELAY_SLOT | BRANCH_LINK;
					break;
				case 12: // Minor opcode 12: SYSCALL
					insn->opcode = N64_OP_SYSCALL;
					insn->branch_flags = BRANCH_N64 | BRANCH_UNCONDITIONAL | BRANCH_ABSOLUTE;
					insn->target = 0x80000180;
					break;
				case 13: // Minor opcode 13: BREAK
					insn->opcode = N64_OP_UNIMPLEMENTED;
					break;
				case 15: // Minor opcode 15: SYNC
					insn->opcode = N64_OP_SYNC;
					break;
				case 16: // Minor opcode 16: MFHI
					SET_RD; insn->opcode = N64_OP_MFHI;
					CHECK_RD_0;
					break;
				case 17: // Minor opcode 17: MTHI
					SET_RS; insn->opcode = N64_OP_MTHI;
					break;
				case 18: // Minor opcode 18: MFLO
					SET_RD; insn->opcode = N64_OP_MFLO;
					CHECK_RD_0;
					break;
				case 19: // Minor opcode 19: MTLO
					SET_RS; insn->opcode = N64_OP_MTLO;
					break;
				case 20: // Minor opcode 20: DSLLV
					SET_RD; SET_RT; SET_RS; insn->opcode = N64_OP_DSLLV;
					CHECK_RD_0;
					break;
				case 22: // Minor opcode 22: DSRLV
					SET_RD; SET_RT; SET_RS; insn->opcode = N64_OP_DSRLV;
					CHECK_RD_0;
					break;
				case 23: // Minor opcode 23: DSRAV
					SET_RD; SET_RT; SET_RS; insn->opcode = N64_OP_DSRAV;
					CHECK_RD_0;
					break;
				case 24: // Minor opcode 24: MULT
					SET_RS; SET_RT; insn->opcode = N64_OP_MULT;
					break;
				case 25: // Minor opcode 25: MULTU
					SET_RS; SET_RT; insn->opcode = N64_OP_MULTU;
					break;
				case 26: // Minor opcode 26: DIV
					SET_RS; SET_RT; insn->opcode = N64_OP_DIV;
					break;
				case 27: // Minor opcode 27: DIVU
					SET_RS; SET_RT; insn->opcode = N64_OP_DIVU;
					break;
				case 28: // Minor opcode 28: DMULT
					SET_RS; SET_RT; insn->opcode = N64_OP_DMULT;
					break;
				case 29: // Minor opcode 29: DMULTU
					SET_RS; SET_RT; insn->opcode = N64_OP_DMULTU;
					break;
				case 30: // Minor opcode 30: DDIV
					SET_RS; SET_RT; insn->opcode = N64_OP_DDIV;
					break;
				case 31: // Minor opcode 31: DDIVU
					SET_RS; SET_RT; insn->opcode = N64_OP_DDIVU;
					break;
				case 32: // Minor opcode 32: ADD
					SET_RD; SET_RS; SET_RT; insn->opcode = N64_OP_ADD;
					CHECK_RD_0;
					break;
				case 33: // Minor opcode 33: ADDU
					SET_RD; SET_RS; SET_RT; insn->opcode = N64_OP_ADDU;
					CHECK_RD_0;
					break;
				case 34: // Minor opcode 34: SUB
					SET_RD; SET_RS; SET_RT; insn->opcode = N64_OP_SUB;
					CHECK_RD_0;
					break;
				case 35: // Minor opcode 35: SUBU
					SET_RD; SET_RS; SET_RT; insn->opcode = N64_OP_SUBU;
					CHECK_RD_0;
					break;
				case 36: // Minor opcode 36: AND
					SET_RD; SET_RS; SET_RT; insn->opcode = N64_OP_AND;
					CHECK_RD_0;
					break;
				case 37: // Minor opcode 37: OR
					SET_RD; SET_RS; SET_RT; insn->opcode = N64_OP_OR;
					CHECK_RD_0;
					break;
				case 38: // Minor opcode 38: XOR
					SET_RD; SET_RS; SET_RT; insn->opcode = N64_OP_XOR;
					CHECK_RD_0;
					break;
				case 39: // Minor opcode 39: NOR
					SET_RD; SET_RS; SET_RT; insn->opcode = N64_OP_NOR;
					CHECK_RD_0;
					break;
				case 42: // Minor opcode 42: SLT
					SET_RD; SET_RS; SET_RT; insn->opcode = N64_OP_SLT;
					CHECK_RD_0;
					break;
				case 43: // Minor opcode 43: SLTU
					SET_RD; SET_RS; SET_RT; insn->opcode = N64_OP_SLTU;
					CHECK_RD_0;
					break;
				case 44: // Minor opcode 44: DADD
					SET_RD; SET_RS; SET_RT; insn->opcode = N64_OP_DADD;
					CHECK_RD_0;
					break;
				case 45: // Minor opcode 45: DADDU
					SET_RD; SET_RS; SET_RT; insn->opcode = N64_OP_DADDU;
					CHECK_RD_0;
					break;
				case 46: // Minor opcode 46: DSUB
					SET_RD; SET_RS; SET_RT; insn->opcode = N64_OP_DSUB;
					CHECK_RD_0;
					break;
				case 47: // Minor opcode 47: DSUBU
					SET_RD; SET_RS; SET_RT; insn->opcode = N64_OP_DSUBU;
					CHECK_RD_0;
					break;
				case 48: // Minor opcode 48: TGE
					SET_RS; SET_RT; insn->opcode = N64_OP_TGE;
					break;
				case 49: // Minor opcode 49: TGEU
					SET_RS; SET_RT; insn->opcode = N64_OP_TGEU;
					break;
				case 50: // Minor opcode 50: TLT
					SET_RS; SET_RT; insn->opcode = N64_OP_TLT;
					break;
				case 51: // Minor opcode 51: TLTU
					SET_RS; SET_RT; insn->opcode = N64_OP_TLTU;
					break;
				case 52: // Minor opcode 52: TEQ
					SET_RS; SET_RT; insn->opcode = N64_OP_TEQ;
					break;
				case 54: // Minor opcode 54: TNE
					SET_RS; SET_RT; insn->opcode = N64_OP_TNE;
					break;
				case 56: // Minor opcode 56: DSLL
					SET_RD; SET_RT; SET_SA; insn->opcode = N64_OP_DSLL;
					CHECK_RD_0;
					break;
				case 58: // Minor opcode 58: DSRL
					SET_RD; SET_RT; SET_SA; insn->opcode = N64_OP_DSRL;
					CHECK_RD_0;
					break;
				case 59: // Minor opcode 59: DSRA
					SET_RD; SET_RT; SET_SA; insn->opcode = N64_OP_DSRA;
					CHECK_RD_0;
					break;
				case 60: // Minor opcode 60: DSLL32
					SET_RD; SET_RT; SET_SA; insn->opcode = N64_OP_DSLL32;
					CHECK_RD_0;
					break;
				case 62: // Minor opcode 62: DSRL32
					SET_RD; SET_RT; SET_SA; insn->opcode = N64_OP_DSRL32;
					CHECK_RD_0;
					break;
				case 63: // Minor opcode 63: DSRA32
					SET_RD; SET_RT; SET_SA; insn->opcode = N64_OP_DSRA32;
					CHECK_RD_0;
					break;
				default: insn->opcode = N64_OP_RESERVED; break;
			}
			break;
		case  1: // Major opcode 1: REGIMM
			switch ((op >> 16) & 0x1F)
			{
				case  0: // Minor opcode 0: BLTZ
					SET_RS; SET_IMM16; insn->opcode = N64_OP_BLTZ;
					if (insn->rs == 0) insn->branch_flags = 0;
					else
					{
						insn->branch_flags = BRANCH_N64 | BRANCH_CONDITIONAL | BRANCH_RELATIVE | BRANCH_DELAY_SLOT;
						SET_TARGET_RELATIVE;
					}
					break;
				case  1: // Minor opcode 1: BGEZ
					SET_RS; SET_IMM16; insn->opcode = N64_OP_BGEZ;
					if (insn->rs == 0)
						insn->branch_flags = BRANCH_N64 | BRANCH_UNCONDITIONAL | BRANCH_RELATIVE | BRANCH_DELAY_SLOT;
					else
						insn->branch_flags = BRANCH_N64 | BRANCH_CONDITIONAL | BRANCH_RELATIVE | BRANCH_DELAY_SLOT;
					SET_TARGET_RELATIVE;
					break;
				case  2: // Minor opcode 2: BLTZL
					SET_RS; SET_IMM16; insn->opcode = N64_OP_BLTZL;
					if (insn->rs == 0)
						insn->branch_flags = 0;
					else
						insn->branch_flags = BRANCH_N64 | BRANCH_CONDITIONAL | BRANCH_RELATIVE | BRANCH_DELAY_SLOT | BRANCH_LIKELY;
					SET_TARGET_RELATIVE;
					break;
				case  3: // Minor opcode 3: BGEZL
					SET_RS; SET_IMM16; insn->opcode = N64_OP_BGEZL;
					if (insn->rs == 0)
						insn->branch_flags = BRANCH_N64 | BRANCH_UNCONDITIONAL | BRANCH_RELATIVE | BRANCH_DELAY_SLOT;
					else
						insn->branch_flags = BRANCH_N64 | BRANCH_CONDITIONAL | BRANCH_RELATIVE | BRANCH_DELAY_SLOT | BRANCH_LIKELY;
					SET_TARGET_RELATIVE;
					break;
				case  8: // Minor opcode 8: TGEI
					SET_RS; SET_IMM16; insn->opcode = N64_OP_TGEI;
					break;
				case  9: // Minor opcode 9: TGEIU
					SET_RS; SET_IMM16; insn->opcode = N64_OP_TGEIU;
					break;
				case 10: // Minor opcode 10: TLTI
					SET_RS; SET_IMM16; insn->opcode = N64_OP_TLTI;
					break;
				case 11: // Minor opcode 11: TLTIU
					SET_RS; SET_IMM16; insn->opcode = N64_OP_TLTIU;
					break;
				case 12: // Minor opcode 12: TEQI
					SET_RS; SET_IMM16; insn->opcode = N64_OP_TEQI;
					break;
				case 14: // Minor opcode 14: TNEI
					SET_RS; SET_IMM16; insn->opcode = N64_OP_TNEI;
					break;
				case 16: // Minor opcode 16: BLTZAL
					SET_RS; SET_IMM16; insn->opcode = N64_OP_BLTZAL;
					insn->branch_flags = BRANCH_N64 | BRANCH_CONDITIONAL | BRANCH_RELATIVE | BRANCH_DELAY_SLOT | BRANCH_LINK;
					SET_TARGET_RELATIVE;
					break;
				case 17: // Minor opcode 17: BGEZAL
					SET_RS; SET_IMM16; insn->opcode = N64_OP_BGEZAL;
					if (insn->rs == 0)
						insn->branch_flags = BRANCH_N64 | BRANCH_UNCONDITIONAL | BRANCH_RELATIVE | BRANCH_DELAY_SLOT | BRANCH_LINK;
					else
						insn->branch_flags = BRANCH_N64 | BRANCH_CONDITIONAL | BRANCH_RELATIVE | BRANCH_DELAY_SLOT | BRANCH_LINK;
					SET_TARGET_RELATIVE;
					break;
				case 18: // Minor opcode 18: BLTZALL
					SET_RS; SET_IMM16; insn->opcode = N64_OP_BLTZALL;
					insn->branch_flags = BRANCH_N64 | BRANCH_CONDITIONAL | BRANCH_RELATIVE | BRANCH_DELAY_SLOT | BRANCH_LIKELY | BRANCH_LINK;
					SET_TARGET_RELATIVE;
					break;
				case 19: // Minor opcode 19: BGEZALL
					SET_RS; SET_IMM16; insn->opcode = N64_OP_BGEZALL;
					if (insn->rs == 0)
						insn->branch_flags = BRANCH_N64 | BRANCH_UNCONDITIONAL | BRANCH_RELATIVE | BRANCH_DELAY_SLOT | BRANCH_LINK;
					else
						insn->branch_flags = BRANCH_N64 | BRANCH_CONDITIONAL | BRANCH_RELATIVE | BRANCH_DELAY_SLOT | BRANCH_LIKELY | BRANCH_LINK;
					SET_TARGET_RELATIVE;
					break;
				default: insn->opcode = N64_OP_RESERVED; break;
			}
			break;
		case  2: // Major opcode 2: J
			SET_IMM26U; insn->opcode = N64_OP_J;
			insn->branch_flags = BRANCH_N64 | BRANCH_UNCONDITIONAL | BRANCH_ABSOLUTE | BRANCH_DELAY_SLOT;
			SET_TARGET_ABSOLUTE;
			break;
		case  3: // Major opcode 3: JAL
			SET_IMM26U; insn->opcode = N64_OP_JAL;
			insn->branch_flags = BRANCH_N64 | BRANCH_UNCONDITIONAL | BRANCH_ABSOLUTE | BRANCH_DELAY_SLOT | BRANCH_LINK;
			SET_TARGET_ABSOLUTE;
			break;
		case  4: // Major opcode 4: BEQ
			SET_RS; SET_RT; SET_IMM16; insn->opcode = N64_OP_BEQ;
			if (insn->rs == insn->rt)
				insn->branch_flags = BRANCH_N64 | BRANCH_UNCONDITIONAL | BRANCH_RELATIVE | BRANCH_DELAY_SLOT;
			else
				insn->branch_flags = BRANCH_N64 | BRANCH_CONDITIONAL | BRANCH_RELATIVE | BRANCH_DELAY_SLOT;
			SET_TARGET_RELATIVE;
			break;
		case  5: // Major opcode 5: BNE
			SET_RS; SET_RT; SET_IMM16; insn->opcode = N64_OP_BNE;
			if (insn->rs == insn->rt) insn->branch_flags = 0;
			else
			{
				insn->branch_flags = BRANCH_N64 | BRANCH_CONDITIONAL | BRANCH_RELATIVE | BRANCH_DELAY_SLOT;
				SET_TARGET_RELATIVE;
			}
			break;
		case  6: // Major opcode 6: BLEZ
			SET_RS; SET_IMM16; insn->opcode = N64_OP_BLEZ;
			if (insn->rs == 0)
				insn->branch_flags = BRANCH_N64 | BRANCH_UNCONDITIONAL | BRANCH_RELATIVE | BRANCH_DELAY_SLOT;
			else
				insn->branch_flags = BRANCH_N64 | BRANCH_CONDITIONAL | BRANCH_RELATIVE | BRANCH_DELAY_SLOT;
			SET_TARGET_RELATIVE;
			break;
		case  7: // Major opcode 7: BGTZ
			SET_RS; SET_IMM16; insn->opcode = N64_OP_BGTZ;
			if (insn->rs == 0) insn->branch_flags = 0;
			else
			{
				insn->branch_flags = BRANCH_N64 | BRANCH_CONDITIONAL | BRANCH_RELATIVE | BRANCH_DELAY_SLOT;
				SET_TARGET_RELATIVE;
			}
			break;
		case  8: // Major opcode 8: ADDI
			SET_RT; SET_RS; SET_IMM16; insn->opcode = N64_OP_ADDI;
			CHECK_RT_0;
			break;
		case  9: // Major opcode 9: ADDIU
			SET_RT; SET_RS; SET_IMM16; insn->opcode = N64_OP_ADDIU;
			CHECK_RT_0;
			break;
		case 10: // Major opcode 10: SLTI
			SET_RT; SET_RS; SET_IMM16; insn->opcode = N64_OP_SLTI;
			CHECK_RT_0;
			break;
		case 11: // Major opcode 11: SLTIU
			SET_RT; SET_RS; SET_IMM16; insn->opcode = N64_OP_SLTIU;
			CHECK_RT_0;
			break;
		case 12: // Major opcode 12: ANDI
			SET_RT; SET_RS; SET_IMM16U; insn->opcode = N64_OP_ANDI;
			CHECK_RT_0;
			break;
		case 13: // Major opcode 13: ORI
			SET_RT; SET_RS; SET_IMM16U; insn->opcode = N64_OP_ORI;
			CHECK_RT_0;
			break;
		case 14: // Major opcode 14: XORI
			SET_RT; SET_RS; SET_IMM16U; insn->opcode = N64_OP_XORI;
			CHECK_RT_0;
			break;
		case 15: // Major opcode 15: LUI
			SET_RT; SET_RS; SET_IMM16; insn->opcode = N64_OP_LUI;
			CHECK_RT_0;
			break;
		case 16: // Major opcode 16: COP0
			switch ((op >> 21) & 0x1F)
			{
				case  0: // Minor opcode 0: MFC0
					SET_RT; SET_RD; insn->opcode = N64_OP_MFC0;
					CHECK_RT_0;
					break;
				case  4: // Minor opcode 4: MTC0
					SET_RT; SET_RD; insn->opcode = N64_OP_MTC0;
					break;
				case 16: // Minor opcode 16: TLB
					switch (op & 0x3F)
					{
						case  1: insn->opcode = N64_OP_TLBR; break;
						case  2: insn->opcode = N64_OP_TLBWI; break;
						case  6: insn->opcode = N64_OP_TLBWR; break;
						case  8: insn->opcode = N64_OP_TLBP; break;
						case 24:
							insn->opcode = N64_OP_ERET;
							insn->branch_flags = BRANCH_N64 | BRANCH_UNCONDITIONAL | BRANCH_VIA_REGISTER;
							break;
						default: insn->opcode = N64_OP_RESERVED; break;
					}
					break;
				default: insn->opcode = N64_OP_RESERVED; break;
			}
			break;
		case 17: // Major opcode: COP1
			switch ((op >> 21) & 0x1F)
			{
				case  0: // Minor opcode 0: MFC1
					SET_RT; SET_FS; insn->opcode = N64_OP_MFC1;
					CHECK_RT_0;
					break;
				case  1: // Minor opcode 1: DMFC1
					SET_RT; SET_FS; insn->opcode = N64_OP_DMFC1;
					CHECK_RT_0;
					break;
				case  2: // Minor opcode 2: CFC1
					SET_RT; SET_FS; insn->opcode = N64_OP_CFC1;
					CHECK_RT_0;
					break;
				case  4: // Minor opcode 4: MTC1
					SET_RT; SET_FS; insn->opcode = N64_OP_MTC1;
					break;
				case  5: // Minor opcode 5: DMTC1
					SET_RT; SET_FS; insn->opcode = N64_OP_DMTC1;
					break;
				case  6: // Minor opcode 6: CTC1
					SET_RT; SET_FS; insn->opcode = N64_OP_CTC1;
					break;
				case  8: // Minor opcode 8: BC
					switch ((op >> 16) & 0x03)
					{
						case  0: // Sub-opcode 0: BC1F
							SET_IMM16; insn->opcode = N64_OP_BC1F;
							insn->branch_flags = BRANCH_N64 | BRANCH_CONDITIONAL | BRANCH_RELATIVE | BRANCH_DELAY_SLOT;
							SET_TARGET_RELATIVE;
							break;
						case  1: // Sub-opcode 1: BC1T
							SET_IMM16; insn->opcode = N64_OP_BC1T;
							insn->branch_flags = BRANCH_N64 | BRANCH_CONDITIONAL | BRANCH_RELATIVE | BRANCH_DELAY_SLOT;
							SET_TARGET_RELATIVE;
							break;
						case  2: // Sub-opcode 2: BC1FL
							SET_IMM16; insn->opcode = N64_OP_BC1FL;
							insn->branch_flags = BRANCH_N64 | BRANCH_CONDITIONAL | BRANCH_RELATIVE | BRANCH_DELAY_SLOT | BRANCH_LIKELY;
							SET_TARGET_RELATIVE;
							break;
						case  3: // Sub-opcode 3: BC1TL
							SET_IMM16; insn->opcode = N64_OP_BC1TL;
							insn->branch_flags = BRANCH_N64 | BRANCH_CONDITIONAL | BRANCH_RELATIVE | BRANCH_DELAY_SLOT | BRANCH_LIKELY;
							SET_TARGET_RELATIVE;
							break;
					}
					break;
				case 16: // Minor opcode 16: S
					switch (op & 0x3F)
					{
						case  0: // Sub-opcode 0: ADD.S
							SET_FD; SET_FS; SET_FT; insn->opcode = N64_OP_ADD_S;
							break;
						case  1: // Sub-opcode 1: SUB.S
							SET_FD; SET_FS; SET_FT; insn->opcode = N64_OP_SUB_S;
							break;
						case  2: // Sub-opcode 2: MUL.S
							SET_FD; SET_FS; SET_FT; insn->opcode = N64_OP_MUL_S;
							break;
						case  3: // Sub-opcode 3: DIV.S
							SET_FD; SET_FS; SET_FT; insn->opcode = N64_OP_DIV_S;
							break;
						case  4: // Sub-opcode 4: SQRT.S
							SET_FD; SET_FS; SET_FT; insn->opcode = N64_OP_SQRT_S;
							break;
						case  5: // Sub-opcode 5: ABS.S
							SET_FD; SET_FS; insn->opcode = N64_OP_ABS_S;
							break;
						case  6: // Sub-opcode 6: MOV.S
							SET_FD; SET_FS; insn->opcode = N64_OP_MOV_S;
							break;
						case  7: // Sub-opcode 7: NEG.S
							SET_FD; SET_FS; insn->opcode = N64_OP_NEG_S;
							break;
						case  8: // Sub-opcode 8: ROUND.L.S
							SET_FD; SET_FS; insn->opcode = N64_OP_ROUND_L_S;
							break;
						case  9: // Sub-opcode 9: TRUNC.L.S
							SET_FD; SET_FS; insn->opcode = N64_OP_TRUNC_L_S;
							break;
						case 10: // Sub-opcode 10: CEIL.L.S
							SET_FD; SET_FS; insn->opcode = N64_OP_CEIL_L_S;
							break;
						case 11: // Sub-opcode 11: FLOOR.L.S
							SET_FD; SET_FS; insn->opcode = N64_OP_FLOOR_L_S;
							break;
						case 12: // Sub-opcode 12: ROUND.W.S
							SET_FD; SET_FS; insn->opcode = N64_OP_ROUND_W_S;
							break;
						case 13: // Sub-opcode 13: TRUNC.W.S
							SET_FD; SET_FS; insn->opcode = N64_OP_TRUNC_W_S;
							break;
						case 14: // Sub-opcode 14: CEIL.W.S
							SET_FD; SET_FS; insn->opcode = N64_OP_CEIL_W_S;
							break;
						case 15: // Sub-opcode 15: FLOOR.W.S
							SET_FD; SET_FS; insn->opcode = N64_OP_FLOOR_W_S;
							break;
						case 33: // Sub-opcode 33: CVT.D.S
							SET_FD; SET_FS; insn->opcode = N64_OP_CVT_D_S;
							break;
						case 36: // Sub-opcode 36: CVT.W.S
							SET_FD; SET_FS; insn->opcode = N64_OP_CVT_W_S;
							break;
						case 37: // Sub-opcode 37: CVT.L.S
							SET_FD; SET_FS; insn->opcode = N64_OP_CVT_L_S;
							break;
						case 48: // Sub-opcode 48: C.F.S
							insn->opcode = N64_OP_C_F_S;
							break;
						case 49: // Sub-opcode 49: C.UN.S
							SET_FS; SET_FT; insn->opcode = N64_OP_C_UN_S;
							break;
						case 50: // Sub-opcode 50: C.EQ.S
							SET_FS; SET_FT; insn->opcode = N64_OP_C_EQ_S;
							break;
						case 51: // Sub-opcode 51: C.UEQ.S
							SET_FS; SET_FT; insn->opcode = N64_OP_C_UEQ_S;
							break;
						case 52: // Sub-opcode 52: C.OLT.S
							SET_FS; SET_FT; insn->opcode = N64_OP_C_OLT_S;
							break;
						case 53: // Sub-opcode 53: C.ULT.S
							SET_FS; SET_FT; insn->opcode = N64_OP_C_ULT_S;
							break;
						case 54: // Sub-opcode 54: C.OLE.S
							SET_FS; SET_FT; insn->opcode = N64_OP_C_OLE_S;
							break;
						case 55: // Sub-opcode 55: C.ULE.S
							SET_FS; SET_FT; insn->opcode = N64_OP_C_ULE_S;
							break;
						case 56: // Sub-opcode 56: C.SF.S
							SET_FS; SET_FT; insn->opcode = N64_OP_C_SF_S;
							break;
						case 57: // Sub-opcode 57: C.NGLE.S
							SET_FS; SET_FT; insn->opcode = N64_OP_C_NGLE_S;
							break;
						case 58: // Sub-opcode 58: C.SEQ.S
							SET_FS; SET_FT; insn->opcode = N64_OP_C_SEQ_S;
							break;
						case 59: // Sub-opcode 59: C.NGL.S
							SET_FS; SET_FT; insn->opcode = N64_OP_C_NGL_S;
							break;
						case 60: // Sub-opcode 60: C.LT.S
							SET_FS; SET_FT; insn->opcode = N64_OP_C_LT_S;
							break;
						case 61: // Sub-opcode 61: C.NGE.S
							SET_FS; SET_FT; insn->opcode = N64_OP_C_NGE_S;
							break;
						case 62: // Sub-opcode 62: C.LE.S
							SET_FS; SET_FT; insn->opcode = N64_OP_C_LE_S;
							break;
						case 63: // Sub-opcode 63: C.NGT.S
							SET_FS; SET_FT; insn->opcode = N64_OP_C_NGT_S;
							break;
						default: insn->opcode = N64_OP_RESERVED; break;
					}
					break;
				case 17: // Minor opcode 17: D
					switch (op & 0x3F)
					{
						case  0: // Sub-opcode 0: ADD.D
							SET_FD; SET_FS; SET_FT; insn->opcode = N64_OP_ADD_D;
							break;
						case  1: // Sub-opcode 1: SUB.D
							SET_FD; SET_FS; SET_FT; insn->opcode = N64_OP_SUB_D;
							break;
						case  2: // Sub-opcode 2: MUL.D
							SET_FD; SET_FS; SET_FT; insn->opcode = N64_OP_MUL_D;
							break;
						case  3: // Sub-opcode 3: DIV.D
							SET_FD; SET_FS; SET_FT; insn->opcode = N64_OP_DIV_D;
							break;
						case  4: // Sub-opcode 4: SQRT.D
							SET_FD; SET_FS; SET_FT; insn->opcode = N64_OP_SQRT_D;
							break;
						case  5: // Sub-opcode 5: ABS.D
							SET_FD; SET_FS; insn->opcode = N64_OP_ABS_D;
							break;
						case  6: // Sub-opcode 6: MOV.D
							SET_FD; SET_FS; insn->opcode = N64_OP_MOV_D;
							break;
						case  7: // Sub-opcode 7: NEG.D
							SET_FD; SET_FS; insn->opcode = N64_OP_NEG_D;
							break;
						case  8: // Sub-opcode 8: ROUND.L.D
							SET_FD; SET_FS; insn->opcode = N64_OP_ROUND_L_D;
							break;
						case  9: // Sub-opcode 9: TRUNC.L.D
							SET_FD; SET_FS; insn->opcode = N64_OP_TRUNC_L_D;
							break;
						case 10: // Sub-opcode 10: CEIL.L.D
							SET_FD; SET_FS; insn->opcode = N64_OP_CEIL_L_D;
							break;
						case 11: // Sub-opcode 11: FLOOR.L.D
							SET_FD; SET_FS; insn->opcode = N64_OP_FLOOR_L_D;
							break;
						case 12: // Sub-opcode 12: ROUND.W.D
							SET_FD; SET_FS; insn->opcode = N64_OP_ROUND_W_D;
							break;
						case 13: // Sub-opcode 13: TRUNC.W.D
							SET_FD; SET_FS; insn->opcode = N64_OP_TRUNC_W_D;
							break;
						case 14: // Sub-opcode 14: CEIL.W.D
							SET_FD; SET_FS; insn->opcode = N64_OP_CEIL_W_D;
							break;
						case 15: // Sub-opcode 15: FLOOR.W.D
							SET_FD; SET_FS; insn->opcode = N64_OP_FLOOR_W_D;
							break;
						case 32: // Sub-opcode 32: CVT.S.D
							SET_FD; SET_FS; insn->opcode = N64_OP_CVT_S_D;
							break;
						case 36: // Sub-opcode 36: CVT.W.D
							SET_FD; SET_FS; insn->opcode = N64_OP_CVT_W_D;
							break;
						case 37: // Sub-opcode 37: CVT.L.D
							SET_FD; SET_FS; insn->opcode = N64_OP_CVT_L_D;
							break;
						case 48: // Sub-opcode 48: C.F.D
							insn->opcode = N64_OP_C_F_D;
							break;
						case 49: // Sub-opcode 49: C.UN.D
							SET_FS; SET_FT; insn->opcode = N64_OP_C_UN_D;
							break;
						case 50: // Sub-opcode 50: C.EQ.D
							SET_FS; SET_FT; insn->opcode = N64_OP_C_EQ_D;
							break;
						case 51: // Sub-opcode 51: C.UEQ.D
							SET_FS; SET_FT; insn->opcode = N64_OP_C_UEQ_D;
							break;
						case 52: // Sub-opcode 52: C.OLT.D
							SET_FS; SET_FT; insn->opcode = N64_OP_C_OLT_D;
							break;
						case 53: // Sub-opcode 53: C.ULT.D
							SET_FS; SET_FT; insn->opcode = N64_OP_C_ULT_D;
							break;
						case 54: // Sub-opcode 54: C.OLE.D
							SET_FS; SET_FT; insn->opcode = N64_OP_C_OLE_D;
							break;
						case 55: // Sub-opcode 55: C.ULE.D
							SET_FS; SET_FT; insn->opcode = N64_OP_C_ULE_D;
							break;
						case 56: // Sub-opcode 56: C.SF.D
							SET_FS; SET_FT; insn->opcode = N64_OP_C_SF_D;
							break;
						case 57: // Sub-opcode 57: C.NGLE.D
							SET_FS; SET_FT; insn->opcode = N64_OP_C_NGLE_D;
							break;
						case 58: // Sub-opcode 58: C.SEQ.D
							SET_FS; SET_FT; insn->opcode = N64_OP_C_SEQ_D;
							break;
						case 59: // Sub-opcode 59: C.NGL.D
							SET_FS; SET_FT; insn->opcode = N64_OP_C_NGL_D;
							break;
						case 60: // Sub-opcode 60: C.LT.D
							SET_FS; SET_FT; insn->opcode = N64_OP_C_LT_D;
							break;
						case 61: // Sub-opcode 61: C.NGE.D
							SET_FS; SET_FT; insn->opcode = N64_OP_C_NGE_D;
							break;
						case 62: // Sub-opcode 62: C.LE.D
							SET_FS; SET_FT; insn->opcode = N64_OP_C_LE_D;
							break;
						case 63: // Sub-opcode 63: C.NGT.D
							SET_FS; SET_FT; insn->opcode = N64_OP_C_NGT_D;
							break;
						default: insn->opcode = N64_OP_RESERVED; break;
					}
					break;
				case 20: // Minor opcode 20: W
					switch (op & 0x3F)
					{
						case 32: // Sub-opcode 32: CVT.S.W
							SET_FD; SET_FS; insn->opcode = N64_OP_CVT_S_W;
							break;
						case 33: // Sub-opcode 33: CVT.D.W
							SET_FD; SET_FS; insn->opcode = N64_OP_CVT_D_W;
							break;
						default: insn->opcode = N64_OP_RESERVED; break;
					}
					break;
				case 21: // Minor opcode 21: L
					switch (op & 0x3F)
					{
						case 32: // Sub-opcode 32: CVT.S.L
							SET_FD; SET_FS; insn->opcode = N64_OP_CVT_S_L;
							break;
						case 33: // Sub-opcode 33: CVT.D.L
							SET_FD; SET_FS; insn->opcode = N64_OP_CVT_D_L;
							break;
						default: insn->opcode = N64_OP_RESERVED; break;
					}
					break;
				default: insn->opcode = N64_OP_RESERVED; break;
			}
			break;
		case 20: // Major opcode 20: BEQL
			SET_RS; SET_RT; SET_IMM16; insn->opcode = N64_OP_BEQL;
			if (insn->rs == insn->rt)
				insn->branch_flags = BRANCH_N64 | BRANCH_UNCONDITIONAL | BRANCH_RELATIVE | BRANCH_DELAY_SLOT;
			else
				insn->branch_flags = BRANCH_N64 | BRANCH_CONDITIONAL | BRANCH_RELATIVE | BRANCH_DELAY_SLOT | BRANCH_LIKELY;
			SET_TARGET_RELATIVE;
			break;
		case 21: // Major opcode 21: BNEL
			SET_RS; SET_RT; SET_IMM16; insn->opcode = N64_OP_BNEL;
			if (insn->rs == insn->rt)
				insn->branch_flags = 0;
			else
				insn->branch_flags = BRANCH_N64 | BRANCH_CONDITIONAL | BRANCH_RELATIVE | BRANCH_DELAY_SLOT | BRANCH_LIKELY;
			SET_TARGET_RELATIVE;
			break;
		case 22: // Major opcode 22: BLEZL
			SET_RS; SET_IMM16; insn->opcode = N64_OP_BLEZL;
			if (insn->rs == 0)
				insn->branch_flags = BRANCH_N64 | BRANCH_UNCONDITIONAL | BRANCH_RELATIVE | BRANCH_DELAY_SLOT;
			else
				insn->branch_flags = BRANCH_N64 | BRANCH_CONDITIONAL | BRANCH_RELATIVE | BRANCH_DELAY_SLOT | BRANCH_LIKELY;
			SET_TARGET_RELATIVE;
			break;
		case 23: // Major opcode 23: BGTZL
			SET_RS; SET_IMM16; insn->opcode = N64_OP_BGTZL;
			if (insn->rs == 0)
				insn->branch_flags = 0;
			else
				insn->branch_flags = BRANCH_N64 | BRANCH_CONDITIONAL | BRANCH_RELATIVE | BRANCH_DELAY_SLOT | BRANCH_LIKELY;
			SET_TARGET_RELATIVE;
			break;
		case 24: // Major opcode 24: DADDI
			SET_RT; SET_RS; SET_IMM16; insn->opcode = N64_OP_DADDI;
			CHECK_RT_0;
			break;
		case 25: // Major opcode 25: DADDIU
			SET_RT; SET_RS; SET_IMM16; insn->opcode = N64_OP_DADDIU;
			CHECK_RT_0;
			break;
		case 26: // Major opcode 26: LDL
			SET_RT; SET_RS; SET_IMM16; insn->opcode = N64_OP_LDL;
			CHECK_RT_0;
			break;
		case 27: // Major opcode 27: LDR
			SET_RT; SET_RS; SET_IMM16; insn->opcode = N64_OP_LDR;
			CHECK_RT_0;
			break;
		case 32: // Major opcode 32: LB
			SET_RT; SET_RS; SET_IMM16; insn->opcode = N64_OP_LB;
			CHECK_RT_0;
			break;
		case 33: // Major opcode 33: LH
			SET_RT; SET_RS; SET_IMM16; insn->opcode = N64_OP_LH;
			CHECK_RT_0;
			break;
		case 34: // Major opcode 34: LWL
			SET_RT; SET_RS; SET_IMM16; insn->opcode = N64_OP_LWL;
			CHECK_RT_0;
			break;
		case 35: // Major opcode 35: LW
			SET_RT; SET_RS; SET_IMM16; insn->opcode = N64_OP_LW;
			CHECK_RT_0;
			break;
		case 36: // Major opcode 36: LBU
			SET_RT; SET_RS; SET_IMM16; insn->opcode = N64_OP_LBU;
			CHECK_RT_0;
			break;
		case 37: // Major opcode 37: LHU
			SET_RT; SET_RS; SET_IMM16; insn->opcode = N64_OP_LHU;
			CHECK_RT_0;
			break;
		case 38: // Major opcode 38: LWR
			SET_RT; SET_RS; SET_IMM16; insn->opcode = N64_OP_LWR;
			CHECK_RT_0;
			break;
		case 39: // Major opcode 39: LWU
			SET_RT; SET_RS; SET_IMM16; insn->opcode = N64_OP_LWU;
			CHECK_RT_0;
			break;
		case 40: // Major opcode 40: SB
			SET_RT; SET_RS; SET_IMM16; insn->opcode = N64_OP_SB;
			break;
		case 41: // Major opcode 41: SH
			SET_RT; SET_RS; SET_IMM16; insn->opcode = N64_OP_SH;
			break;
		case 42: // Major opcode 42: SWL
			SET_RT; SET_RS; SET_IMM16; insn->opcode = N64_OP_SWL;
			break;
		case 43: // Major opcode 43: SW
			SET_RT; SET_RS; SET_IMM16; insn->opcode = N64_OP_SW;
			break;
		case 44: // Major opcode 44: SDL
			SET_RT; SET_RS; SET_IMM16; insn->opcode = N64_OP_SDL;
			break;
		case 45: // Major opcode 45: SDR
			SET_RT; SET_RS; SET_IMM16; insn->opcode = N64_OP_SDR;
			break;
		case 46: // Major opcode 46: SWR
			SET_RT; SET_RS; SET_IMM16; insn->opcode = N64_OP_SWR;
			break;
		case 47: // Major opcode 47: CACHE
			insn->opcode = N64_OP_CACHE;
			break;
		case 48: // Major opcode 48: LL
			SET_RT; SET_RS; SET_IMM16; insn->opcode = N64_OP_LL;
			CHECK_RT_0;
			break;
		case 49: // Major opcode 49: LWC1
			SET_FT; SET_RS; SET_IMM16; insn->opcode = N64_OP_LWC1;
			break;
		case 52: // Major opcode 52: LLD
			SET_RT; SET_RS; SET_IMM16; insn->opcode = N64_OP_LLD;
			CHECK_RT_0;
			break;
		case 53: // Major opcode 53: LDC1
			SET_FT; SET_RS; SET_IMM16; insn->opcode = N64_OP_LDC1;
			break;
		case 55: // Major opcode 55: LD
			SET_RT; SET_RS; SET_IMM16; insn->opcode = N64_OP_LD;
			CHECK_RT_0;
			break;
		case 56: // Major opcode 56: SC
			SET_RT; SET_RS; SET_IMM16; insn->opcode = N64_OP_SC;
			break;
		case 57: // Major opcode 57: SWC1
			SET_FT; SET_RS; SET_IMM16; insn->opcode = N64_OP_SWC1;
			break;
		case 60: // Major opcode 60: SCD
			SET_RT; SET_RS; SET_IMM16; insn->opcode = N64_OP_SCD;
			break;
		case 61: // Major opcode 61: SDC1
			SET_FT; SET_RS; SET_IMM16; insn->opcode = N64_OP_SDC1;
			break;
		case 63: // Major opcode 63: SD
			SET_RT; SET_RS; SET_IMM16; insn->opcode = N64_OP_SD;
			break;
		default: insn->opcode = N64_OP_RESERVED; break;
	}

end: ;
#ifdef ND_SHOW_N64_INSNS
	printf("%08" PRIX32 ": %08" PRIX32 " = %-13s; rd = %2" PRIu8 ", rs = %2" PRIu8 ", rt = %2" PRIu8 ", imm = %08" PRIX32 ", target = %08" PRIX32 "\n", addr, op, get_n64_op_name(insn->opcode), insn->rd, insn->rs, insn->rt, insn->imm, insn->target);
#endif
}

void simplify_n64_insn(n64_insn_t* insn)
{
	if ((insn->branch_flags & (BRANCH_N64 | BRANCH_DELAY_SLOT | BRANCH_LINK | BRANCH_LIKELY | BRANCH_VIA_REGISTER)) == (BRANCH_N64 | BRANCH_DELAY_SLOT))
	{
		/*
		 * All conditional branch instructions (including effectively
		 * unconditional branch instructions) that jump 1 instruction ahead
		 * of their delay slot:
		 * | 0:  BGEZ  $0, +1     |
		 * | 1:  ADDIU $4, $4, 4  | delay slot (branch is also relative to it)
		 * | 2:  MULT  $4, $5     | jump target
		 * are transformed into NOP, because they have no effect on the
		 * execution order of any other instruction. With the branch, the
		 * delay slot (ADDIU) executes first, then the branch, then the
		 * following instruction (MULT). Without it, the delay slot (ADDIU)
		 * executes first by virtue of being next in program order, and the
		 * would-be jump target (MULT) follows.
		 *
		 * If the branch fails, its delay slot is still executed, and it goes
		 * to the exact same instruction as if it succeeded. Therefore, a
		 * branch targetting 8 bytes later can be safely removed.
		 */
		if (insn->target == insn->addr + 8)
		{
			insn->opcode = N64_OP_NOP;
		}
	}

	switch (insn->opcode)
	{
		case N64_OP_SLL:
		case N64_OP_SRL:
		case N64_OP_SRA:
			if (insn->imm == 0) REG_MOVE_32(insn->rt, insn->rd);
			else if (insn->rt == 0) LOAD_0(insn->rd);
			break;
		case N64_OP_SLLV:
		case N64_OP_SRLV:
		case N64_OP_SRAV:
			if (insn->rs == 0) REG_MOVE_32(insn->rt, insn->rd);
			else if (insn->rt == 0) LOAD_0(insn->rd);
			break;
		case N64_OP_DSLLV:
		case N64_OP_DSRLV:
		case N64_OP_DSRAV:
			if (insn->rd == insn->rt && insn->rs == 0) MAKE_NOP;
			else if (insn->rs == 0) REG_MOVE_64(insn->rt, insn->rd);
			else if (insn->rt == 0) LOAD_0(insn->rd);
			break;
		case N64_OP_ADD:
		case N64_OP_ADDU:
			if (insn->rs == 0 && insn->rt == 0) LOAD_0(insn->rd);
			else if (insn->rs == 0) REG_MOVE_32(insn->rt, insn->rd);
			else if (insn->rt == 0) REG_MOVE_32(insn->rs, insn->rd);
			break;
		case N64_OP_SUB:
		case N64_OP_SUBU:
			if (insn->rs == 0 && insn->rt == 0) LOAD_0(insn->rd);
			else if (insn->rt == 0) REG_MOVE_32(insn->rs, insn->rd);
			break;
		case N64_OP_AND:
			if (insn->rs == insn->rt && insn->rt == insn->rd) MAKE_NOP;
			else if (insn->rs == 0 || insn->rt == 0) LOAD_0(insn->rd);
			else if (insn->rs == insn->rt) REG_MOVE_64(insn->rs, insn->rd);
			break;
		case N64_OP_OR:
			if ((insn->rs == insn->rt && insn->rt == insn->rd)
			 || (insn->rs == insn->rd && insn->rt == 0)
			 || (insn->rt == insn->rd && insn->rs == 0)) MAKE_NOP;
			else if (insn->rs == insn->rt) REG_MOVE_64(insn->rs, insn->rd);
			else if (insn->rs == 0) REG_MOVE_64(insn->rt, insn->rd);
			break;
		case N64_OP_XOR:
			if ((insn->rs == insn->rd && insn->rt == 0)
			 || (insn->rt == insn->rd && insn->rs == 0)) MAKE_NOP;
			else if (insn->rs == insn->rt) LOAD_0(insn->rd);
			else if (insn->rs == 0) REG_MOVE_64(insn->rt, insn->rd);
			else if (insn->rt == 0) REG_MOVE_64(insn->rs, insn->rd);
			break;
		case N64_OP_NOR:
			if (insn->rs == insn->rt) insn->rt = 0;
			else if (insn->rs == 0) { insn->rs = insn->rt; insn->rt = 0; }
			break;
		case N64_OP_SLT:
			if (insn->rs == insn->rt) LOAD_0(insn->rd);
			break;
		case N64_OP_SLTU:
			if (insn->rs == insn->rt) LOAD_0(insn->rd);
			else if (insn->rt == 0) LOAD_0(insn->rd);
			break;
		case N64_OP_DADD:
		case N64_OP_DADDU:
			if ((insn->rs == insn->rd && insn->rt == 0)
			 || (insn->rt == insn->rd && insn->rs == 0)) MAKE_NOP;
			else if (insn->rs == 0) REG_MOVE_64(insn->rt, insn->rd);
			else if (insn->rt == 0) REG_MOVE_64(insn->rs, insn->rd);
			break;
		case N64_OP_DSUB:
		case N64_OP_DSUBU:
			if (insn->rs == insn->rd && insn->rt == 0) MAKE_NOP;
			else if (insn->rs == insn->rt) LOAD_0(insn->rd);
			else if (insn->rt == 0) REG_MOVE_64(insn->rs, insn->rd);
			break;
		case N64_OP_DSLL:
		case N64_OP_DSRL:
		case N64_OP_DSRA:
			if (insn->imm == 0)
			{
				if (insn->rt == insn->rd) MAKE_NOP;
				else REG_MOVE_64(insn->rt, insn->rd);
			}
			else if (insn->rt == 0) LOAD_0(insn->rd);
			break;
		case N64_OP_DSLL32:
		case N64_OP_DSRL32:
		case N64_OP_DSRA32:
			if (insn->rt == 0) LOAD_0(insn->rd);
			break;
		case N64_OP_BLTZ:
			if (insn->rs == 0) MAKE_NOP;
			break;
		case N64_OP_BGEZL:
			if (insn->rs == 0) insn->opcode = N64_OP_BGEZ;
			break;
		case N64_OP_BGEZALL:
			if (insn->rs == 0) insn->opcode = N64_OP_BGEZAL;
			break;
		case N64_OP_BEQ:
			if (insn->rs == insn->rt) UNCONDITIONAL_BRANCH;
			break;
		case N64_OP_BNE:
			if (insn->rs == insn->rt) MAKE_NOP;
			break;
		case N64_OP_BLEZ:
			if (insn->rs == 0) insn->opcode = N64_OP_BGEZ;
			break;
		case N64_OP_BGTZ:
			if (insn->rs == 0) MAKE_NOP;
			break;
		case N64_OP_BEQL:
			if (insn->rs == insn->rt) UNCONDITIONAL_BRANCH;
			break;
		case N64_OP_BLEZL:
			if (insn->rs == 0) insn->opcode = N64_OP_BGEZ;
			break;
		case N64_OP_ADDI:
		case N64_OP_ADDIU:
			if (insn->rs == 0)
			{
				if (insn->imm == -1)
				{
					insn->opcode = N64_OP_NOR;
					insn->rd = insn->rt;
					insn->rs = insn->rt = 0;
				}
				else if (insn->imm == 0) LOAD_0(insn->rt);
				else if (insn->imm > 0) insn->opcode = N64_OP_ORI;
				else insn->opcode = N64_OP_ADDIU;
			}
			else if (insn->imm == 0) REG_MOVE_32(insn->rs, insn->rt);
			break;
		case N64_OP_SLTI:
			if (insn->rs == 0)
			{
				if (insn->imm <= 0) LOAD_0(insn->rt);
				else LOAD_IMM16U(1, insn->rt);
			}
			else if (insn->imm == 0)
			{
				insn->opcode = N64_OP_SLT;
				insn->rd = insn->rt;
				insn->rt = 0;
			}
			break;
		case N64_OP_SLTIU:
			if (insn->rs == 0)
			{
				if (insn->imm == 0) LOAD_0(insn->rt);
				else LOAD_IMM16U(1, insn->rt);
			}
			else if (insn->imm == 0)
			{
				insn->opcode = N64_OP_SLTU;
				insn->rd = insn->rt;
				insn->rt = 0;
			}
			break;
		case N64_OP_ANDI:
			if (insn->rs == 0 || insn->imm == 0) LOAD_0(insn->rt);
			break;
		case N64_OP_ORI:
			if (insn->imm == 0)
			{
				if (insn->rs == insn->rt) MAKE_NOP;
				else if (insn->rs == 0) LOAD_0(insn->rt);
				else REG_MOVE_64(insn->rs, insn->rt);
			}
			break;
		case N64_OP_XORI:
			if (insn->rs == insn->rt && insn->imm == 0) MAKE_NOP;
			else if (insn->rs == 0) insn->opcode = N64_OP_ORI;
			break;
		case N64_OP_LUI:
			if (insn->imm == 0) LOAD_0(insn->rt);
			break;
		case N64_OP_DADDI:
		case N64_OP_DADDIU:
			if (insn->rs == 0)
			{
				if (insn->imm == -1)
				{
					insn->opcode = N64_OP_NOR;
					insn->rd = insn->rt;
					insn->rs = insn->rt = 0;
				}
				else if (insn->imm == 0) LOAD_0(insn->rt);
				else if (insn->imm > 0) insn->opcode = N64_OP_ORI;
				else insn->opcode = N64_OP_ADDIU;
			}
			else if (insn->imm == 0)
			{
				if (insn->rs == insn->rt) MAKE_NOP;
				else REG_MOVE_64(insn->rs, insn->rt);
			}
			break;
		case N64_OP_MOV_S:
		case N64_OP_MOV_D:
			if (insn->rs == insn->rd) MAKE_NOP;
			break;
		default: break;
	}
}

void post_parse_n64_insns(n64_insn_t* insns, const uint32_t insn_count, const uint32_t page_start, const uint32_t page_end)
{
	uint32_t i;
	for (i = 0; i < insn_count; i++)
	{
		switch (insns[i].opcode)
		{
			case N64_OP_BLTZL:
			case N64_OP_BGTZL:
				if (insns[i].rs == 0)
				{
					insns[i].opcode = insns[i + 1].opcode = N64_OP_NOP;
				}
				break;
			case N64_OP_BNEL:
				if (insns[i].rs == insns[i].rt)
				{
					insns[i].opcode = insns[i + 1].opcode = N64_OP_NOP;
				}
				break;
			default: break;
		}

		n64_insn_t* insn = &insns[i];
		// Broad fixups.
		switch (GET_OP_TYPE(insn->opcode))
		{
			case N64_OP_TYPE_JUMP_IMM26:
			case N64_OP_TYPE_BRANCH_1OP_INT_IMM16:
			case N64_OP_TYPE_BRANCH_2OP_INT:
			case N64_OP_TYPE_BRANCH_FP_COND:
				if (insn->target == insn->addr && i + 1 < insn_count && insns[i + 1].opcode == N64_OP_NOP)
				{
					// If a branch targets itself and does nothing in its
					// delay slot, it's an idle-looping branch as soon as
					// it succeeds.
					insn->opcode += 2; // from in-page to idle-looping
					insn->branch_flags |= BRANCH_IN_PAGE;
				}
				else if (insn->target < page_start || insn->target >= page_end || insn->addr == page_end - 4)
				{
					// If a branch is not entirely contained within this page
					// or targets an instruction outside this page, it's an
					// out-of-page branch.
					insn->opcode += 1; // from in-page to out-of-page
					insn->branch_flags |= BRANCH_OUT_OF_PAGE;
				}
				else
				{
					// This branch was already in-page.
					insn->branch_flags |= BRANCH_IN_PAGE;
				}
				break;
		}
	}
}

// Generated via a command:
// grep " = " mupen64plus-core/src/r4300/neb_dynarec/analysis.h | cut -d= -f1 | sed 's/^\s*/case /' | sed 's/N64_OP_\([A-Z_0-9]*\)\s*$/N64_OP_\1: return "\1";/'
const char* get_n64_op_name(const n64_opcode_t opcode)
{
	switch (opcode)
	{
		case N64_OP_NOP: return "NOP";
		case N64_OP_RESERVED: return "RESERVED";
		case N64_OP_UNIMPLEMENTED: return "UNIMPLEMENTED";
		case N64_OP_SLL: return "SLL";
		case N64_OP_SRL: return "SRL";
		case N64_OP_SRA: return "SRA";
		case N64_OP_DSLL: return "DSLL";
		case N64_OP_DSRL: return "DSRL";
		case N64_OP_DSRA: return "DSRA";
		case N64_OP_DSLL32: return "DSLL32";
		case N64_OP_DSRL32: return "DSRL32";
		case N64_OP_DSRA32: return "DSRA32";
		case N64_OP_SLLV: return "SLLV";
		case N64_OP_SRLV: return "SRLV";
		case N64_OP_SRAV: return "SRAV";
		case N64_OP_DSLLV: return "DSLLV";
		case N64_OP_DSRLV: return "DSRLV";
		case N64_OP_DSRAV: return "DSRAV";
		case N64_OP_ADD: return "ADD";
		case N64_OP_ADDU: return "ADDU";
		case N64_OP_SUB: return "SUB";
		case N64_OP_SUBU: return "SUBU";
		case N64_OP_AND: return "AND";
		case N64_OP_OR: return "OR";
		case N64_OP_XOR: return "XOR";
		case N64_OP_NOR: return "NOR";
		case N64_OP_SLT: return "SLT";
		case N64_OP_SLTU: return "SLTU";
		case N64_OP_DADD: return "DADD";
		case N64_OP_DADDU: return "DADDU";
		case N64_OP_DSUB: return "DSUB";
		case N64_OP_DSUBU: return "DSUBU";
		case N64_OP_ADDI: return "ADDI";
		case N64_OP_ADDIU: return "ADDIU";
		case N64_OP_SLTI: return "SLTI";
		case N64_OP_SLTIU: return "SLTIU";
		case N64_OP_ANDI: return "ANDI";
		case N64_OP_ORI: return "ORI";
		case N64_OP_XORI: return "XORI";
		case N64_OP_LUI: return "LUI";
		case N64_OP_DADDI: return "DADDI";
		case N64_OP_DADDIU: return "DADDIU";
		case N64_OP_LDL: return "LDL";
		case N64_OP_LDR: return "LDR";
		case N64_OP_LB: return "LB";
		case N64_OP_LH: return "LH";
		case N64_OP_LWL: return "LWL";
		case N64_OP_LW: return "LW";
		case N64_OP_LBU: return "LBU";
		case N64_OP_LHU: return "LHU";
		case N64_OP_LWR: return "LWR";
		case N64_OP_LWU: return "LWU";
		case N64_OP_LL: return "LL";
		case N64_OP_LLD: return "LLD";
		case N64_OP_LD: return "LD";
		case N64_OP_SB: return "SB";
		case N64_OP_SH: return "SH";
		case N64_OP_SWL: return "SWL";
		case N64_OP_SW: return "SW";
		case N64_OP_SDL: return "SDL";
		case N64_OP_SDR: return "SDR";
		case N64_OP_SWR: return "SWR";
		case N64_OP_SC: return "SC";
		case N64_OP_SCD: return "SCD";
		case N64_OP_SD: return "SD";
		case N64_OP_MFHI: return "MFHI";
		case N64_OP_MFLO: return "MFLO";
		case N64_OP_MTHI: return "MTHI";
		case N64_OP_MTLO: return "MTLO";
		case N64_OP_MULT: return "MULT";
		case N64_OP_MULTU: return "MULTU";
		case N64_OP_DIV: return "DIV";
		case N64_OP_DIVU: return "DIVU";
		case N64_OP_DMULT: return "DMULT";
		case N64_OP_DMULTU: return "DMULTU";
		case N64_OP_DDIV: return "DDIV";
		case N64_OP_DDIVU: return "DDIVU";
		case N64_OP_J: return "J";
		case N64_OP_J_OUT: return "J_OUT";
		case N64_OP_J_IDLE: return "J_IDLE";
		case N64_OP_JAL: return "JAL";
		case N64_OP_JAL_OUT: return "JAL_OUT";
		case N64_OP_JAL_IDLE: return "JAL_IDLE";
		case N64_OP_JR: return "JR";
		case N64_OP_JALR: return "JALR";
		case N64_OP_SYSCALL: return "SYSCALL";
		case N64_OP_ERET: return "ERET";
		case N64_OP_TGE: return "TGE";
		case N64_OP_TGEU: return "TGEU";
		case N64_OP_TLT: return "TLT";
		case N64_OP_TLTU: return "TLTU";
		case N64_OP_TEQ: return "TEQ";
		case N64_OP_TNE: return "TNE";
		case N64_OP_TGEI: return "TGEI";
		case N64_OP_TGEIU: return "TGEIU";
		case N64_OP_TLTI: return "TLTI";
		case N64_OP_TLTIU: return "TLTIU";
		case N64_OP_TEQI: return "TEQI";
		case N64_OP_TNEI: return "TNEI";
		case N64_OP_BLTZ: return "BLTZ";
		case N64_OP_BLTZ_OUT: return "BLTZ_OUT";
		case N64_OP_BLTZ_IDLE: return "BLTZ_IDLE";
		case N64_OP_BLTZL: return "BLTZL";
		case N64_OP_BLTZL_OUT: return "BLTZL_OUT";
		case N64_OP_BLTZL_IDLE: return "BLTZL_IDLE";
		case N64_OP_BGEZ: return "BGEZ";
		case N64_OP_BGEZ_OUT: return "BGEZ_OUT";
		case N64_OP_BGEZ_IDLE: return "BGEZ_IDLE";
		case N64_OP_BGEZL: return "BGEZL";
		case N64_OP_BGEZL_OUT: return "BGEZL_OUT";
		case N64_OP_BGEZL_IDLE: return "BGEZL_IDLE";
		case N64_OP_BLTZAL: return "BLTZAL";
		case N64_OP_BLTZAL_OUT: return "BLTZAL_OUT";
		case N64_OP_BLTZAL_IDLE: return "BLTZAL_IDLE";
		case N64_OP_BLTZALL: return "BLTZALL";
		case N64_OP_BLTZALL_OUT: return "BLTZALL_OUT";
		case N64_OP_BLTZALL_IDLE: return "BLTZALL_IDLE";
		case N64_OP_BGEZAL: return "BGEZAL";
		case N64_OP_BGEZAL_OUT: return "BGEZAL_OUT";
		case N64_OP_BGEZAL_IDLE: return "BGEZAL_IDLE";
		case N64_OP_BGEZALL: return "BGEZALL";
		case N64_OP_BGEZALL_OUT: return "BGEZALL_OUT";
		case N64_OP_BGEZALL_IDLE: return "BGEZALL_IDLE";
		case N64_OP_BLEZ: return "BLEZ";
		case N64_OP_BLEZ_OUT: return "BLEZ_OUT";
		case N64_OP_BLEZ_IDLE: return "BLEZ_IDLE";
		case N64_OP_BLEZL: return "BLEZL";
		case N64_OP_BLEZL_OUT: return "BLEZL_OUT";
		case N64_OP_BLEZL_IDLE: return "BLEZL_IDLE";
		case N64_OP_BGTZ: return "BGTZ";
		case N64_OP_BGTZ_OUT: return "BGTZ_OUT";
		case N64_OP_BGTZ_IDLE: return "BGTZ_IDLE";
		case N64_OP_BGTZL: return "BGTZL";
		case N64_OP_BGTZL_OUT: return "BGTZL_OUT";
		case N64_OP_BGTZL_IDLE: return "BGTZL_IDLE";
		case N64_OP_BEQ: return "BEQ";
		case N64_OP_BEQ_OUT: return "BEQ_OUT";
		case N64_OP_BEQ_IDLE: return "BEQ_IDLE";
		case N64_OP_BEQL: return "BEQL";
		case N64_OP_BEQL_OUT: return "BEQL_OUT";
		case N64_OP_BEQL_IDLE: return "BEQL_IDLE";
		case N64_OP_BNE: return "BNE";
		case N64_OP_BNE_OUT: return "BNE_OUT";
		case N64_OP_BNE_IDLE: return "BNE_IDLE";
		case N64_OP_BNEL: return "BNEL";
		case N64_OP_BNEL_OUT: return "BNEL_OUT";
		case N64_OP_BNEL_IDLE: return "BNEL_IDLE";
		case N64_OP_SYNC: return "SYNC";
		case N64_OP_CACHE: return "CACHE";
		case N64_OP_MFC0: return "MFC0";
		case N64_OP_MTC0: return "MTC0";
		case N64_OP_TLBR: return "TLBR";
		case N64_OP_TLBWI: return "TLBWI";
		case N64_OP_TLBWR: return "TLBWR";
		case N64_OP_TLBP: return "TLBP";
		case N64_OP_ABS_S: return "ABS_S";
		case N64_OP_MOV_S: return "MOV_S";
		case N64_OP_NEG_S: return "NEG_S";
		case N64_OP_ROUND_W_S: return "ROUND_W_S";
		case N64_OP_TRUNC_W_S: return "TRUNC_W_S";
		case N64_OP_CEIL_W_S: return "CEIL_W_S";
		case N64_OP_FLOOR_W_S: return "FLOOR_W_S";
		case N64_OP_CVT_W_S: return "CVT_W_S";
		case N64_OP_CVT_S_W: return "CVT_S_W";
		case N64_OP_ADD_S: return "ADD_S";
		case N64_OP_SUB_S: return "SUB_S";
		case N64_OP_MUL_S: return "MUL_S";
		case N64_OP_DIV_S: return "DIV_S";
		case N64_OP_SQRT_S: return "SQRT_S";
		case N64_OP_ROUND_L_S: return "ROUND_L_S";
		case N64_OP_TRUNC_L_S: return "TRUNC_L_S";
		case N64_OP_CEIL_L_S: return "CEIL_L_S";
		case N64_OP_FLOOR_L_S: return "FLOOR_L_S";
		case N64_OP_CVT_D_S: return "CVT_D_S";
		case N64_OP_CVT_L_S: return "CVT_L_S";
		case N64_OP_CVT_D_W: return "CVT_D_W";
		case N64_OP_C_F_S: return "C_F_S";
		case N64_OP_C_UN_S: return "C_UN_S";
		case N64_OP_C_EQ_S: return "C_EQ_S";
		case N64_OP_C_UEQ_S: return "C_UEQ_S";
		case N64_OP_C_OLT_S: return "C_OLT_S";
		case N64_OP_C_ULT_S: return "C_ULT_S";
		case N64_OP_C_OLE_S: return "C_OLE_S";
		case N64_OP_C_ULE_S: return "C_ULE_S";
		case N64_OP_C_SF_S: return "C_SF_S";
		case N64_OP_C_NGLE_S: return "C_NGLE_S";
		case N64_OP_C_SEQ_S: return "C_SEQ_S";
		case N64_OP_C_NGL_S: return "C_NGL_S";
		case N64_OP_C_LT_S: return "C_LT_S";
		case N64_OP_C_NGE_S: return "C_NGE_S";
		case N64_OP_C_LE_S: return "C_LE_S";
		case N64_OP_C_NGT_S: return "C_NGT_S";
		case N64_OP_ABS_D: return "ABS_D";
		case N64_OP_MOV_D: return "MOV_D";
		case N64_OP_NEG_D: return "NEG_D";
		case N64_OP_ROUND_L_D: return "ROUND_L_D";
		case N64_OP_TRUNC_L_D: return "TRUNC_L_D";
		case N64_OP_CEIL_L_D: return "CEIL_L_D";
		case N64_OP_FLOOR_L_D: return "FLOOR_L_D";
		case N64_OP_CVT_L_D: return "CVT_L_D";
		case N64_OP_CVT_D_L: return "CVT_D_L";
		case N64_OP_ADD_D: return "ADD_D";
		case N64_OP_SUB_D: return "SUB_D";
		case N64_OP_MUL_D: return "MUL_D";
		case N64_OP_DIV_D: return "DIV_D";
		case N64_OP_SQRT_D: return "SQRT_D";
		case N64_OP_ROUND_W_D: return "ROUND_W_D";
		case N64_OP_TRUNC_W_D: return "TRUNC_W_D";
		case N64_OP_CEIL_W_D: return "CEIL_W_D";
		case N64_OP_FLOOR_W_D: return "FLOOR_W_D";
		case N64_OP_CVT_S_D: return "CVT_S_D";
		case N64_OP_CVT_W_D: return "CVT_W_D";
		case N64_OP_CVT_S_L: return "CVT_S_L";
		case N64_OP_C_F_D: return "C_F_D";
		case N64_OP_C_UN_D: return "C_UN_D";
		case N64_OP_C_EQ_D: return "C_EQ_D";
		case N64_OP_C_UEQ_D: return "C_UEQ_D";
		case N64_OP_C_OLT_D: return "C_OLT_D";
		case N64_OP_C_ULT_D: return "C_ULT_D";
		case N64_OP_C_OLE_D: return "C_OLE_D";
		case N64_OP_C_ULE_D: return "C_ULE_D";
		case N64_OP_C_SF_D: return "C_SF_D";
		case N64_OP_C_NGLE_D: return "C_NGLE_D";
		case N64_OP_C_SEQ_D: return "C_SEQ_D";
		case N64_OP_C_NGL_D: return "C_NGL_D";
		case N64_OP_C_LT_D: return "C_LT_D";
		case N64_OP_C_NGE_D: return "C_NGE_D";
		case N64_OP_C_LE_D: return "C_LE_D";
		case N64_OP_C_NGT_D: return "C_NGT_D";
		case N64_OP_MFC1: return "MFC1";
		case N64_OP_DMFC1: return "DMFC1";
		case N64_OP_CFC1: return "CFC1";
		case N64_OP_MTC1: return "MTC1";
		case N64_OP_DMTC1: return "DMTC1";
		case N64_OP_CTC1: return "CTC1";
		case N64_OP_LWC1: return "LWC1";
		case N64_OP_LDC1: return "LDC1";
		case N64_OP_SWC1: return "SWC1";
		case N64_OP_SDC1: return "SDC1";
		case N64_OP_BC1F: return "BC1F";
		case N64_OP_BC1F_OUT: return "BC1F_OUT";
		case N64_OP_BC1F_IDLE: return "BC1F_IDLE";
		case N64_OP_BC1FL: return "BC1FL";
		case N64_OP_BC1FL_OUT: return "BC1FL_OUT";
		case N64_OP_BC1FL_IDLE: return "BC1FL_IDLE";
		case N64_OP_BC1T: return "BC1T";
		case N64_OP_BC1T_OUT: return "BC1T_OUT";
		case N64_OP_BC1T_IDLE: return "BC1T_IDLE";
		case N64_OP_BC1TL: return "BC1TL";
		case N64_OP_BC1TL_OUT: return "BC1TL_OUT";
		case N64_OP_BC1TL_IDLE: return "BC1TL_IDLE";
		default: return "???";
	}
}
