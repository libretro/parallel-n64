/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - Neb Dynarec - AMD64 (little-endian) functions           *
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

#include <stdbool.h>
#include <stdint.h>

#include "../arch-ops.h"
#include "../../r4300.h"
#include "assemble.h"

#define OFFSET_FROM(a, b) ((uint8_t*) (b) - (uint8_t*) (a))

static uint8_t* output_byte(uint8_t* dst, uint32_t* avail, uint8_t Byte)
{
	*(uint8_t*) dst = Byte;
	*avail -= 1;
	return dst + 1;
}

static uint8_t* output_hword(uint8_t* dst, uint32_t* avail, uint16_t Hword)
{
	*(uint16_t*) dst = Hword;
	*avail -= 2;
	return dst + 2;
}

static uint8_t* output_dword(uint8_t* dst, uint32_t* avail, uint32_t Dword)
{
	*(uint32_t*) dst = Dword;
	*avail -= 4;
	return dst + 4;
}

static uint8_t* output_qword(uint8_t* dst, uint32_t* avail, uint64_t Qword)
{
	*(uint64_t*) dst = Qword;
	*avail -= 8;
	return dst + 8;
}

/* Maps virtual register numbers from arch-ops.c into native registers. */
static const uint8_t v2n_int_regs[ARCH_INT_TEMP_REGISTERS + ARCH_INT_SAVED_REGISTERS] = {
	RAX, RCX, RDX, RSI, RDI, R8, R9, R10, R11,
	RBX, R12, R13, R14, R15,
};

static uint8_t v2n_int(virt_reg_t virt_reg)
{
	return v2n_int_regs[virt_reg];
}

#define FAIL_IF(expr) do { if (expr) { return false; } } while (0)

bool arch_emit_code(uint8_t* start, uint32_t avail,
	uint8_t** next, const arch_block_t* block)
{
	size_t i;
	uint8_t* end = start;

	/* Early exit if the requested number of instructions exceeds the
	 * remaining capacity even at 1 byte per instruction */
	FAIL_IF(block->insn_count > avail);

	for (i = 0; i < block->insn_count; i++) {
		arch_insn_t* insn = &block->insns[i];
		switch (insn->opcode) {
			case ARCH_OP_DELETED:
				break;

			case ARCH_OP_ENTER_FRAME:
			case ARCH_OP_EXIT_FRAME:
				/* TODO implement callee-saved register preservation */
				break;

			case ARCH_OP_SET_REG_IMM32U:
				FAIL_IF(avail < 6);
				end = output_mov_imm32_to_r32(end, &avail, insn->operands[1].value._32u, v2n_int(insn->operands[0].value.reg));
				break;

			case ARCH_OP_SET_REG_IMMADDR:
				FAIL_IF(avail < 10);
				end = output_mov_imm64_to_r64(end, &avail, insn->operands[1].value.addr, v2n_int(insn->operands[0].value.reg));
				break;

			case ARCH_OP_SIGN_EXTEND_REG32_TO_SELF64:
				FAIL_IF(avail < 3);
				end = output_movsx_r32_to_r64(end, &avail, insn->operands[0].value.reg, v2n_int(insn->operands[0].value.reg));
				break;

			case ARCH_OP_LOAD32_REG_FROM_MEM_REG:
				FAIL_IF(avail < 4);
				end = output_mov_mem_at_r64_to_r32(end, &avail, v2n_int(insn->operands[1].value.reg), v2n_int(insn->operands[0].value.reg));
				break;

			case ARCH_OP_STORE32_REG_AT_MEM_REG:
				FAIL_IF(avail < 4);
				end = output_mov_r32_to_mem_at_r64(end, &avail, v2n_int(insn->operands[0].value.reg), v2n_int(insn->operands[1].value.reg));
				break;

			case ARCH_OP_STORE64_REG_AT_MEM_REG:
			case ARCH_OP_STOREADDR_REG_AT_MEM_REG:
				FAIL_IF(avail < 4);
				end = output_mov_r64_to_mem_at_r64(end, &avail, v2n_int(insn->operands[0].value.reg), v2n_int(insn->operands[1].value.reg));
				break;

			case ARCH_OP_RETURN:
				FAIL_IF(avail < 1);
				end = output_ret(end, &avail);
				break;

			case ARCH_OP_SLL32_IMM8U_TO_REG:
				FAIL_IF(avail < 4);
				end = output_shl_r32_by_imm5(end, &avail, v2n_int(insn->operands[1].value.reg), insn->operands[0].value._8u);
				break;

			case ARCH_OP_SLL64_IMM8U_TO_REG:
				FAIL_IF(avail < 4);
				end = output_shl_r64_by_imm6(end, &avail, v2n_int(insn->operands[1].value.reg), insn->operands[0].value._8u);
				break;

			case ARCH_OP_SRL32_IMM8U_TO_REG:
				FAIL_IF(avail < 4);
				end = output_shr_r32_by_imm5(end, &avail, v2n_int(insn->operands[1].value.reg), insn->operands[0].value._8u);
				break;

			case ARCH_OP_SRL64_IMM8U_TO_REG:
				FAIL_IF(avail < 4);
				end = output_shr_r64_by_imm6(end, &avail, v2n_int(insn->operands[1].value.reg), insn->operands[0].value._8u);
				break;

			case ARCH_OP_SRA32_IMM8U_TO_REG:
				FAIL_IF(avail < 4);
				end = output_sar_r32_by_imm5(end, &avail, v2n_int(insn->operands[1].value.reg), insn->operands[0].value._8u);
				break;

			case ARCH_OP_SRA64_IMM8U_TO_REG:
				FAIL_IF(avail < 4);
				end = output_sar_r64_by_imm6(end, &avail, v2n_int(insn->operands[1].value.reg), insn->operands[0].value._8u);
				break;
		}
	}

	/* If the block was emitted, update variables and return success. */
	*next = end;
	return true;
}

uint8_t* arch_align_code(uint8_t* code_start)
{
	/* AMD64 requires no particular alignment. */
	return code_start;
}
