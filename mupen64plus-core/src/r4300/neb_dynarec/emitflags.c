/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - Neb Dynarec - Instruction information                   *
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

/*
 * Functions in this file are meant to give information to assist the
 * Neb Dynarec's driver in its decisions for code generation.
 */

#include <stdbool.h>

#include "arch-config.h"
#include "n64ops.h"
#include "emitflags.h"

#include "../r4300.h"

#if defined(ARCH_HAS_ENTER_FRAME)
#  define CAN_ENTER_FRAME true
#else
#  define CAN_ENTER_FRAME false
#endif
#if defined(ARCH_HAS_EXIT_FRAME)
#  define CAN_EXIT_FRAME true
#else
#  define CAN_EXIT_FRAME false
#endif
#if defined(ARCH_HAS_STORE32_IMM32U_AT_MEM_IMMADDR) \
 || (defined(ARCH_HAS_SET_REG_IMM32U) \
  && (defined(ARCH_HAS_STORE32_REG_AT_MEM_IMMADDR) \
   || (defined(ARCH_HAS_SET_REG_IMMADDR) \
    && defined(ARCH_HAS_STORE32_REG_AT_MEM_REG))))
#  define CAN_SET_PC true
#else
#  define CAN_SET_PC false
#endif
#if defined(ARCH_HAS_RETURN)
#  define CAN_RETURN true
#else
#  define CAN_RETURN false
#endif

#define CAN_EMIT_FUNCTIONS (CAN_ENTER_FRAME && CAN_EXIT_FRAME && CAN_SET_PC && CAN_RETURN)

#if defined(ARCH_HAS_LOAD32_REG_FROM_MEM_IMMADDR) \
 || (defined(ARCH_HAS_SET_REG_IMMADDR) \
  && (defined(ARCH_HAS_LOAD32_REG_FROM_MEM_REG) \
   || defined(ARCH_HAS_LOAD32_REG_FROM_MEM_REG_OFF16S) \
   || defined(ARCH_HAS_LOAD32_REG_FROM_MEM_REG_OFF32S)))
#  define CAN_LOAD32 true
#else
#  define CAN_LOAD32 false
#endif

#if defined(ARCH_HAS_SET_REG_IMM32U)
#  define CAN_SET_IMM32 true
#else
#  define CAN_SET_IMM32 false
#endif

#if defined(ARCH_HAS_SET_REG_IMM64U)
#  define CAN_SET_IMM64 true
#else
#  define CAN_SET_IMM64 false
#endif

#if defined(ARCH_HAS_LOAD64_REG_FROM_MEM_IMMADDR) \
 || (defined(ARCH_HAS_SET_REG_IMMADDR) \
  && (defined(ARCH_HAS_LOAD64_REG_FROM_MEM_REG) \
   || defined(ARCH_HAS_LOAD64_REG_FROM_MEM_REG_OFF16S)))
#  define CAN_LOAD64 true
#else
#  define CAN_LOAD64 false
#endif

#if defined(ARCH_HAS_STORE32_REG_AT_MEM_IMMADDR) \
 || (defined(ARCH_HAS_SET_REG_IMMADDR) \
  && (defined(ARCH_HAS_STORE32_REG_AT_MEM_REG) \
   || defined(ARCH_HAS_STORE32_REG_AT_MEM_REG_OFF16S)))
#  define CAN_STORE32 true
#else
#  define CAN_STORE32 false
#endif

#if defined(ARCH_HAS_STORE64_REG_AT_MEM_IMMADDR) \
 || (defined(ARCH_HAS_SET_REG_IMMADDR) \
  && (defined(ARCH_HAS_STORE64_REG_AT_MEM_REG) \
   || defined(ARCH_HAS_STORE64_REG_AT_MEM_REG_OFF16S)))
#  define CAN_STORE64 true
#else
#  define CAN_STORE64 false
#endif

#if (defined(ARCH_HAS_64BIT_REGS) \
  && (defined(ARCH_HAS_SIGN_EXTEND_REG32_TO_SELF64) \
   || ((defined(ARCH_HAS_SLL64_IMM8U_TO_REG) \
     || defined(ARCH_HAS_SLL64_REG_IMM8U_TO_REG)) \
    && (defined(ARCH_HAS_SRA64_IMM8U_TO_REG) \
     || defined(ARCH_HAS_SRA64_REG_IMM8U_TO_REG))))) \
 || (!defined(ARCH_HAS_64BIT_REGS) \
  && (defined(ARCH_HAS_SRA32_IMM8U_TO_REG) \
   || defined(ARCH_HAS_SRA32_REG_IMM8U_TO_REG)))
#  define CAN_SIGN_EXTEND true
#else
#  define CAN_SIGN_EXTEND false
#endif

#if defined(ARCH_HAS_SLL32_IMM8U_TO_REG) \
 || defined(ARCH_HAS_SLL32_REG_IMM8U_TO_REG)
#  define CAN_SLL32 true
#else
#  define CAN_SLL32 false
#endif

#if defined(ARCH_HAS_SRL32_IMM8U_TO_REG) \
 || defined(ARCH_HAS_SRL32_REG_IMM8U_TO_REG)
#  define CAN_SRL32 true
#else
#  define CAN_SRL32 false
#endif

#if defined(ARCH_HAS_SRA32_IMM8U_TO_REG) \
 || defined(ARCH_HAS_SRA32_REG_IMM8U_TO_REG)
#  define CAN_SRA32 true
#else
#  define CAN_SRA32 false
#endif

void fill_emit_flags(n64_insn_t* insn)
{
	insn->emit_flags = 0;

	switch (insn->opcode)
	{
		case N64_OP_NOP:
			insn->emit_flags = INSTRUCTION_IGNORES_DELAY_SLOT;
			if (CAN_EMIT_FUNCTIONS)
				insn->emit_flags |= INSTRUCTION_HAS_EMITTERS;
			break;
		case N64_OP_RESERVED:
		case N64_OP_UNIMPLEMENTED:
			break;
		case N64_OP_SLL:
			insn->emit_flags = INSTRUCTION_IGNORES_DELAY_SLOT;
#if defined(ARCH_HAS_64BIT_REGS)
			if (CAN_EMIT_FUNCTIONS && CAN_LOAD32 && CAN_SLL32 && CAN_SIGN_EXTEND && CAN_STORE64)
				insn->emit_flags |= INSTRUCTION_HAS_EMITTERS;
#else
			if (CAN_EMIT_FUNCTIONS && CAN_LOAD32 && CAN_SLL32 && CAN_SIGN_EXTEND && CAN_STORE32)
				insn->emit_flags |= INSTRUCTION_HAS_EMITTERS;
#endif
			break;
		case N64_OP_SRL:
			insn->emit_flags = INSTRUCTION_IGNORES_DELAY_SLOT;
#if defined(ARCH_HAS_64BIT_REGS)
			if (CAN_EMIT_FUNCTIONS && CAN_LOAD32 && CAN_SRL32 && CAN_SIGN_EXTEND && CAN_STORE64)
				insn->emit_flags |= INSTRUCTION_HAS_EMITTERS;
#else
			if (CAN_EMIT_FUNCTIONS && CAN_LOAD32 && CAN_SRL32 && CAN_SIGN_EXTEND && CAN_STORE32)
				insn->emit_flags |= INSTRUCTION_HAS_EMITTERS;
#endif
			break;
		case N64_OP_SRA:
			insn->emit_flags = INSTRUCTION_IGNORES_DELAY_SLOT;
#if defined(ARCH_HAS_64BIT_REGS)
			if (CAN_EMIT_FUNCTIONS && CAN_LOAD32 && CAN_SRA32 && CAN_SIGN_EXTEND && CAN_STORE64)
				insn->emit_flags |= INSTRUCTION_HAS_EMITTERS;
#else
			if (CAN_EMIT_FUNCTIONS && CAN_LOAD32 && CAN_SRA32 && CAN_SIGN_EXTEND && CAN_STORE32)
				insn->emit_flags |= INSTRUCTION_HAS_EMITTERS;
#endif
			break;
		case N64_OP_DSLL:
		case N64_OP_DSRL:
		case N64_OP_DSRA:
		case N64_OP_DSLL32:
		case N64_OP_DSRL32:
		case N64_OP_DSRA32:
		case N64_OP_SLLV:
		case N64_OP_SRLV:
		case N64_OP_SRAV:
		case N64_OP_DSLLV:
		case N64_OP_DSRLV:
		case N64_OP_DSRAV:
		case N64_OP_ADD:
			// The signed version of ADDU can raise the Integer Overflow
			// exception on real hardware. We don't.
		case N64_OP_ADDU:
		case N64_OP_SUB:
			// The signed version of SUBU can raise the Integer Overflow
			// exception on real hardware. We don't.
		case N64_OP_SUBU:
		case N64_OP_AND:
		case N64_OP_OR:
		case N64_OP_XOR:
		case N64_OP_NOR:
		case N64_OP_SLT:
		case N64_OP_SLTU:
		case N64_OP_DADD:
			// The signed version of DADDU can raise the Integer Overflow
			// exception on real hardware. We don't.
		case N64_OP_DADDU:
		case N64_OP_DSUB:
			// The signed version of DSUBU can raise the Integer Overflow
			// exception on real hardware. We don't.
		case N64_OP_DSUBU:
		case N64_OP_ADDI:
			// The signed version of ADDIU can raise the Integer Overflow
			// exception on real hardware. We don't.
		case N64_OP_ADDIU:
		case N64_OP_SLTI:
		case N64_OP_SLTIU:
		case N64_OP_ANDI:
		case N64_OP_ORI:
		case N64_OP_XORI:
		case N64_OP_LUI:
		case N64_OP_DADDI:
			// The signed version of DADDIU can raise the Integer Overflow
			// exception on real hardware. We don't.
		case N64_OP_DADDIU:
			insn->emit_flags = INSTRUCTION_IGNORES_DELAY_SLOT;
			break;
		case N64_OP_LDL:
		case N64_OP_LDR:
		case N64_OP_LB:
		case N64_OP_LH:
		case N64_OP_LWL:
		case N64_OP_LW:
		case N64_OP_LBU:
		case N64_OP_LHU:
		case N64_OP_LWR:
		case N64_OP_LWU:
		case N64_OP_LL:
		case N64_OP_LLD:
		case N64_OP_LD:
		case N64_OP_SB:
		case N64_OP_SH:
		case N64_OP_SWL:
		case N64_OP_SW:
		case N64_OP_SDL:
		case N64_OP_SDR:
		case N64_OP_SWR:
		case N64_OP_SC:
		case N64_OP_SCD:
		case N64_OP_SD:
			break;
		case N64_OP_MFHI:
		case N64_OP_MFLO:
		case N64_OP_MTHI:
		case N64_OP_MTLO:
		case N64_OP_MULT:
		case N64_OP_MULTU:
		case N64_OP_DIV:
		case N64_OP_DIVU:
		case N64_OP_DMULT:
		case N64_OP_DMULTU:
		case N64_OP_DDIV:
		case N64_OP_DDIVU:
			insn->emit_flags = INSTRUCTION_IGNORES_DELAY_SLOT;
			break;
		case N64_OP_J:
		case N64_OP_J_OUT:
		case N64_OP_J_IDLE:
		case N64_OP_JAL:
		case N64_OP_JAL_OUT:
		case N64_OP_JAL_IDLE:
		case N64_OP_JR:
		case N64_OP_JALR:
		case N64_OP_SYSCALL:
		case N64_OP_ERET:
			break;
		case N64_OP_TGE:
		case N64_OP_TGEU:
		case N64_OP_TLT:
		case N64_OP_TLTU:
			break;
		case N64_OP_TEQ:
			break;
		case N64_OP_TNE:
		case N64_OP_TGEI:
		case N64_OP_TGEIU:
		case N64_OP_TLTI:
		case N64_OP_TLTIU:
		case N64_OP_TEQI:
		case N64_OP_TNEI:
			break;
		case N64_OP_BLTZ:
		case N64_OP_BLTZ_OUT:
		case N64_OP_BLTZ_IDLE:
		case N64_OP_BLTZL:
		case N64_OP_BLTZL_OUT:
		case N64_OP_BLTZL_IDLE:
		case N64_OP_BGEZ:
		case N64_OP_BGEZ_OUT:
		case N64_OP_BGEZ_IDLE:
		case N64_OP_BGEZL:
		case N64_OP_BGEZL_OUT:
		case N64_OP_BGEZL_IDLE:
		case N64_OP_BLTZAL:
		case N64_OP_BLTZAL_OUT:
		case N64_OP_BLTZAL_IDLE:
		case N64_OP_BLTZALL:
		case N64_OP_BLTZALL_OUT:
		case N64_OP_BLTZALL_IDLE:
		case N64_OP_BGEZAL:
		case N64_OP_BGEZAL_OUT:
		case N64_OP_BGEZAL_IDLE:
		case N64_OP_BGEZALL:
		case N64_OP_BGEZALL_OUT:
		case N64_OP_BGEZALL_IDLE:
		case N64_OP_BLEZ:
		case N64_OP_BLEZ_OUT:
		case N64_OP_BLEZ_IDLE:
		case N64_OP_BLEZL:
		case N64_OP_BLEZL_OUT:
		case N64_OP_BLEZL_IDLE:
		case N64_OP_BGTZ:
		case N64_OP_BGTZ_OUT:
		case N64_OP_BGTZ_IDLE:
		case N64_OP_BGTZL:
		case N64_OP_BGTZL_OUT:
		case N64_OP_BGTZL_IDLE:
		case N64_OP_BEQ:
		case N64_OP_BEQ_OUT:
		case N64_OP_BEQ_IDLE:
		case N64_OP_BEQL:
		case N64_OP_BEQL_OUT:
		case N64_OP_BEQL_IDLE:
		case N64_OP_BNE:
		case N64_OP_BNE_OUT:
		case N64_OP_BNE_IDLE:
		case N64_OP_BNEL:
		case N64_OP_BNEL_OUT:
		case N64_OP_BNEL_IDLE:
			break;
		case N64_OP_SYNC:
		case N64_OP_CACHE:
			/* CACHE can raise TLB Refill, TLB Invalid, Coprocessor Unusable,
			 * Address Error, Cache Error and Bus Error exceptions on real
			 * hardware. We don't. */
			insn->emit_flags = INSTRUCTION_IGNORES_DELAY_SLOT;
			if (CAN_EMIT_FUNCTIONS)
				insn->emit_flags |= INSTRUCTION_HAS_EMITTERS;
			break;
		case N64_OP_MFC0:
		case N64_OP_MTC0:
		case N64_OP_TLBR:
		case N64_OP_TLBWI:
		case N64_OP_TLBWR:
		case N64_OP_TLBP:
			break;
		case N64_OP_ABS_S:
		case N64_OP_MOV_S:
		case N64_OP_NEG_S:
		case N64_OP_ROUND_W_S:
		case N64_OP_TRUNC_W_S:
		case N64_OP_CEIL_W_S:
		case N64_OP_FLOOR_W_S:
		case N64_OP_CVT_W_S:
		case N64_OP_CVT_S_W:
		case N64_OP_ADD_S:
		case N64_OP_SUB_S:
		case N64_OP_MUL_S:
		case N64_OP_DIV_S:
		case N64_OP_SQRT_S:
		case N64_OP_ROUND_L_S:
		case N64_OP_TRUNC_L_S:
		case N64_OP_CEIL_L_S:
		case N64_OP_FLOOR_L_S:
		case N64_OP_CVT_D_S:
		case N64_OP_CVT_L_S:
		case N64_OP_CVT_D_W:
		case N64_OP_C_F_S:
		case N64_OP_C_UN_S:
		case N64_OP_C_EQ_S:
		case N64_OP_C_UEQ_S:
		case N64_OP_C_OLT_S:
		case N64_OP_C_ULT_S:
		case N64_OP_C_OLE_S:
		case N64_OP_C_ULE_S:
		case N64_OP_C_SF_S:
		case N64_OP_C_NGLE_S:
		case N64_OP_C_SEQ_S:
		case N64_OP_C_NGL_S:
		case N64_OP_C_LT_S:
		case N64_OP_C_NGE_S:
		case N64_OP_C_LE_S:
		case N64_OP_C_NGT_S:
		case N64_OP_ABS_D:
		case N64_OP_MOV_D:
		case N64_OP_NEG_D:
		case N64_OP_ROUND_L_D:
		case N64_OP_TRUNC_L_D:
		case N64_OP_CEIL_L_D:
		case N64_OP_FLOOR_L_D:
		case N64_OP_CVT_L_D:
		case N64_OP_CVT_D_L:
		case N64_OP_ADD_D:
		case N64_OP_SUB_D:
		case N64_OP_MUL_D:
		case N64_OP_DIV_D:
		case N64_OP_SQRT_D:
		case N64_OP_ROUND_W_D:
		case N64_OP_TRUNC_W_D:
		case N64_OP_CEIL_W_D:
		case N64_OP_FLOOR_W_D:
		case N64_OP_CVT_S_D:
		case N64_OP_CVT_W_D:
		case N64_OP_CVT_S_L:
		case N64_OP_C_F_D:
		case N64_OP_C_UN_D:
		case N64_OP_C_EQ_D:
		case N64_OP_C_UEQ_D:
		case N64_OP_C_OLT_D:
		case N64_OP_C_ULT_D:
		case N64_OP_C_OLE_D:
		case N64_OP_C_ULE_D:
		case N64_OP_C_SF_D:
		case N64_OP_C_NGLE_D:
		case N64_OP_C_SEQ_D:
		case N64_OP_C_NGL_D:
		case N64_OP_C_LT_D:
		case N64_OP_C_NGE_D:
		case N64_OP_C_LE_D:
		case N64_OP_C_NGT_D:
		case N64_OP_MFC1:
		case N64_OP_DMFC1:
		case N64_OP_CFC1:
		case N64_OP_MTC1:
		case N64_OP_DMTC1:
		case N64_OP_CTC1:
		case N64_OP_LWC1:
		case N64_OP_LDC1:
		case N64_OP_SWC1:
		case N64_OP_SDC1:
		case N64_OP_BC1F:
		case N64_OP_BC1F_OUT:
		case N64_OP_BC1F_IDLE:
		case N64_OP_BC1FL:
		case N64_OP_BC1FL_OUT:
		case N64_OP_BC1FL_IDLE:
		case N64_OP_BC1T:
		case N64_OP_BC1T_OUT:
		case N64_OP_BC1T_IDLE:
		case N64_OP_BC1TL:
		case N64_OP_BC1TL_OUT:
		case N64_OP_BC1TL_IDLE:
			insn->emit_flags = INSTRUCTION_NEEDS_COP1_CHECK;
			break;
	}

	if (no_compiled_jump
	 && (GET_OP_TYPE(insn->opcode) == N64_OP_TYPE_JUMP_IMM26
	  || GET_OP_TYPE(insn->opcode) == N64_OP_TYPE_JUMP_1OP_INT
	  || GET_OP_TYPE(insn->opcode) == N64_OP_TYPE_TRAP_2OP_INT
	  || GET_OP_TYPE(insn->opcode) == N64_OP_TYPE_TRAP_1OP_INT_IMM16
	  || GET_OP_TYPE(insn->opcode) == N64_OP_TYPE_BRANCH_1OP_INT_IMM16
	  || GET_OP_TYPE(insn->opcode) == N64_OP_TYPE_BRANCH_2OP_INT
	  || GET_OP_TYPE(insn->opcode) == N64_OP_TYPE_BRANCH_FP_COND))
		insn->emit_flags &= ~INSTRUCTION_HAS_EMITTERS;
}
