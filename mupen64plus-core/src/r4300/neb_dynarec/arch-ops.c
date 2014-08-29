/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - Neb Dynarec - Architecture operations                   *
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

#include <stdint.h>
#include <stdlib.h>

#include "arch-config.h"
#include "arch-ops.h"
#include "il-ops.h"

#include "../r4300.h"

arch_block_t* arch_block_init(arch_block_t* block)
{
	block->insn_count = block->insn_capacity = 0;
	block->insns = NULL;
	return block;
}

bool arch_block_reserve_insns(arch_block_t* block, size_t add)
{
	if (block->insn_count + add <= block->insn_capacity)
		return true;
	size_t new_capacity = block->insn_capacity * 2 + add;
	arch_insn_t* new_allocation = realloc(block->insns, new_capacity * sizeof(arch_insn_t));
	if (new_allocation) {
		block->insns = new_allocation;
		block->insn_capacity = new_capacity;
	}
	return new_allocation != NULL;
}

void arch_block_free(arch_block_t* block)
{
	free(block->insns);
	arch_block_init(block);
}

arch_insn_t* arch_block_next_insn(arch_block_t* block)
{
	return &block->insns[block->insn_count++];
}

arch_insn_t* arch_insn_init(arch_insn_t* insn)
{
	return arch_insn_init_as(insn, ARCH_OP_DELETED);
}

arch_insn_t* arch_insn_init_as(arch_insn_t* insn, arch_opcode_t opcode)
{
	size_t i;
	insn->opcode = opcode;
	for (i = 0; i < sizeof(insn->operands) / sizeof(insn->operands[0]); i++) {
		insn->operands[i].type = ARCH_OT_NONE;
	}
	return insn;
}

static void vreg(arch_insn_t* insn, size_t operand, virt_reg_t reg)
{
	insn->operands[operand].type = ARCH_OT_REG;
	insn->operands[operand].value.reg = reg;
}

static void imm8s(arch_insn_t* insn, size_t operand, int8_t imm)
{
	insn->operands[operand].type = ARCH_OT_IMM8S;
	insn->operands[operand].value._8s = imm;
}

static void imm8u(arch_insn_t* insn, size_t operand, uint8_t imm)
{
	insn->operands[operand].type = ARCH_OT_IMM8U;
	insn->operands[operand].value._8u = imm;
}

static void imm16s(arch_insn_t* insn, size_t operand, int16_t imm)
{
	insn->operands[operand].type = ARCH_OT_IMM16S;
	insn->operands[operand].value._16s = imm;
}

static void imm16u(arch_insn_t* insn, size_t operand, uint16_t imm)
{
	insn->operands[operand].type = ARCH_OT_IMM16U;
	insn->operands[operand].value._16u = imm;
}

static void imm32s(arch_insn_t* insn, size_t operand, int32_t imm)
{
	insn->operands[operand].type = ARCH_OT_IMM32S;
	insn->operands[operand].value._32s = imm;
}

static void imm32u(arch_insn_t* insn, size_t operand, uint32_t imm)
{
	insn->operands[operand].type = ARCH_OT_IMM32U;
	insn->operands[operand].value._32u = imm;
}

static void imm64s(arch_insn_t* insn, size_t operand, int64_t imm)
{
	insn->operands[operand].type = ARCH_OT_IMM64S;
	insn->operands[operand].value._64s = imm;
}

static void imm64u(arch_insn_t* insn, size_t operand, uint64_t imm)
{
	insn->operands[operand].type = ARCH_OT_IMM64U;
	insn->operands[operand].value._64u = imm;
}

static void immaddr(arch_insn_t* insn, size_t operand, uintptr_t addr)
{
	insn->operands[operand].type = ARCH_OT_IMMADDR;
	insn->operands[operand].value.addr = addr;
}

#define FAIL_IF(expr, cleanup) do { if (expr) { cleanup; return false; } } while (0)

bool arch_emit_from_il(const il_block_t* il, arch_block_t* arch)
{
	arch_insn_t* insn;
	size_t i;

	arch_block_init(arch);
	FAIL_IF(!arch_block_reserve_insns(arch, il->insn_count), arch_block_free(arch));

	arch_insn_init_as(arch_block_next_insn(arch), IL_OP_ENTER_FRAME);

	for (i = 0; i < il->insn_count; i++) {
		il_insn_t* il_insn = &il->insns[i];
		switch (il->insns[i].opcode) {
			case IL_OP_ENTER_FRAME:
#if defined(ARCH_HAS_ENTER_FRAME)
				FAIL_IF(!arch_block_reserve_insns(arch, 1), arch_block_free(arch));
				arch_insn_init_as(arch_block_next_insn(arch), ARCH_OP_ENTER_FRAME);
#else
#  warning Insufficient emitters for IL operation ENTER_FRAME
#endif
				break;

			case IL_OP_EXIT_FRAME:
#if defined(ARCH_HAS_EXIT_FRAME)
				FAIL_IF(!arch_block_reserve_insns(arch, 1), arch_block_free(arch));
				arch_insn_init_as(arch_block_next_insn(arch), ARCH_OP_EXIT_FRAME);
#else
#  warning Insufficient emitters for IL operation EXIT_FRAME
#endif
				break;

			case IL_OP_SET_PC:
#if defined(ARCH_HAS_STORE32_IMM32U_AT_MEM_IMMADDR)
				FAIL_IF(!arch_block_reserve_insns(arch, 1), arch_block_free(arch));
				insn = arch_insn_init_as(arch_block_next_insn(arch), ARCH_OP_STORE32_IMM32U_AT_MEM_IMMADDR);
				imm32u(insn, 0, (uint32_t) il_insn->argument);
				immaddr(insn, 1, (uintptr_t) &PC);
#elif defined(ARCH_HAS_SET_REG_IMM32U) \
   && defined(ARCH_HAS_STORE32_REG_AT_MEM_IMMADDR)
				FAIL_IF(!arch_block_reserve_insns(arch, 2), arch_block_free(arch));
				insn = arch_insn_init_as(arch_block_next_insn(arch), ARCH_OP_SET_REG_IMM32U);
				vreg(insn, 0, 0);
				imm32u(insn, 1, (uint32_t) il_insn->argument);
				insn = arch_insn_init_as(arch_block_next_insn(arch), STORE32_REG_AT_MEM_IMMADDR);
				vreg(insn, 0, 0);
				immaddr(insn, 1, (uintptr_t) &PC);
#elif defined(ARCH_HAS_SET_REG_IMM32U) \
   && defined(ARCH_HAS_SET_REG_IMMADDR) \
   && defined(ARCH_HAS_STORE32_REG_AT_MEM_REG)
				FAIL_IF(!arch_block_reserve_insns(arch, 3), arch_block_free(arch));
				insn = arch_insn_init_as(arch_block_next_insn(arch), ARCH_OP_SET_REG_IMM32U);
				vreg(insn, 0, 0);
				imm32u(insn, 1, (uint32_t) il_insn->argument);
				insn = arch_insn_init_as(arch_block_next_insn(arch), ARCH_OP_SET_REG_IMMADDR);
				vreg(insn, 0, 1);
				immaddr(insn, 1, (uintptr_t) &PC);
				insn = arch_insn_init_as(arch_block_next_insn(arch), ARCH_OP_STORE32_REG_AT_MEM_REG);
				vreg(insn, 0, 0);
				vreg(insn, 1, 1);
#else
#  warning Insufficient emitters for IL operation SET_PC
#endif
				break;

			case IL_OP_RETURN:
#if defined(ARCH_HAS_RETURN)
				FAIL_IF(!arch_block_reserve_insns(arch, 1), arch_block_free(arch));
				arch_insn_init_as(arch_block_next_insn(arch), ARCH_OP_RETURN);
#else
#  warning Insufficient emitters for IL operation RETURN
#endif
				break;

			case IL_OP_DELETED:
				break;
		}
	}

	return true;
}
