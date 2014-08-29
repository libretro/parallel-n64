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

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#include "arch-config.h"
#include "arch-ops.h"
#include "il-ops.h"

#include "../r4300.h"

/* Adding these offsets to the address of a block of memory that contains
 * a 64-bit value, or an offset from such an address, allows constructing the
 * address that starts the 32-bit block that contains the least-significant
 * word (LSW) or the most-significant word (MSW). */
#if defined(ARCH_IS_LITTLE_ENDIAN)
#  define LSW_OFFSET 0
#  define MSW_OFFSET 4
#elif defined(ARCH_IS_BIG_ENDIAN)
#  define LSW_OFFSET 4
#  define MSW_OFFSET 0
#else
#  error The active Neb Dynarec architecture does not declare its endianness
#endif

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

/* Helper functions */

/* TODO Proper register allocation */
/* Emits architecture instructions to load a register with a base address for
 * further base+offset loads and stores in native memory.
 * 
 * Does not emit any instructions if the architecture can load from immediate
 * addresses, or if the architecture does not support base+offset addressing.
 * 
 * In:
 *   virt_reg: The virtual register to place the base in.
 *   addr: The base address to place in the virtual register.
 * Out:
 *   arch: The block of architecture instructions to add to.
 * Returns:
 *   false if instructions were required and memory allocation failed for
 *   the required instruction(s); true otherwise.
 */
static bool set_base(arch_block_t* arch, virt_reg_t virt_reg, uintptr_t addr)
{
#if defined(ARCH_HAS_LOAD32_REG_FROM_MEM_IMMADDR) \
 || defined(ARCH_HAS_LOAD64_REG_FROM_MEM_IMMADDR) \
 || defined(ARCH_HAS_STORE32_REG_AT_MEM_IMMADDR) \
 || defined(ARCH_HAS_STORE64_REG_AT_MEM_IMMADDR)
	return true;
#elif defined(ARCH_HAS_SET_REG_IMMADDR)
#  if defined(ARCH_HAS_LOAD32_REG_FROM_MEM_REG_OFF16S) \
   || defined(ARCH_HAS_LOAD64_REG_FROM_MEM_REG_OFF16S) \
   || defined(ARCH_HAS_STORE32_REG_AT_MEM_REG_OFF16S) \
   || defined(ARCH_HAS_STORE64_REG_AT_MEM_REG_OFF16S)
	if (!arch_block_reserve_insns(arch, 1)) return false;
	arch_insn_t* insn = arch_insn_init_as(arch_block_next_insn(arch), ARCH_OP_SET_REG_IMMADDR);
	vreg(insn, 0, virt_reg);
	immaddr(insn, 1, addr);
	return true;
#  elif defined(ARCH_HAS_LOAD32_REG_FROM_MEM_REG) \
   || defined(ARCH_HAS_LOAD64_REG_FROM_MEM_REG) \
   || defined(ARCH_HAS_STORE32_REG_AT_MEM_REG) \
   || defined(ARCH_HAS_STORE64_REG_AT_MEM_REG)
	return true;
#  else
	assert(false); /* insufficient emitters, but this was called */
#  endif
#else
	assert(false); /* insufficient emitters, but this was called */
#endif
}

/* Emits architecture instructions to load a register with a complete address
 * for further direct loads and stores in native memory.
 * 
 * Does not emit any instructions if the architecture can load from immediate
 * addresses, or if the architecture supports base+offset addressing (in which
 * case set_base will have done something).
 * 
 * In:
 *   virt_reg: The virtual register to place the address in.
 *   addr: The address to place in the virtual register.
 * Out:
 *   arch: The block of architecture instructions to add to.
 * Returns:
 *   false if instructions were required and memory allocation failed for
 *   the required instruction(s); true otherwise.
 */
static bool set_address(arch_block_t* arch, virt_reg_t virt_reg, uintptr_t addr)
{
#if defined(ARCH_HAS_LOAD32_REG_FROM_MEM_IMMADDR) \
 || defined(ARCH_HAS_LOAD64_REG_FROM_MEM_IMMADDR) \
 || defined(ARCH_HAS_STORE32_REG_AT_MEM_IMMADDR) \
 || defined(ARCH_HAS_STORE64_REG_AT_MEM_IMMADDR)
	return true;
#elif defined(ARCH_HAS_SET_REG_IMMADDR)
#  if defined(ARCH_HAS_LOAD32_REG_FROM_MEM_REG_OFF16S) \
   || defined(ARCH_HAS_LOAD64_REG_FROM_MEM_REG_OFF16S) \
   || defined(ARCH_HAS_STORE32_REG_AT_MEM_REG_OFF16S) \
   || defined(ARCH_HAS_STORE64_REG_AT_MEM_REG_OFF16S)
	return true;
#  elif defined(ARCH_HAS_LOAD32_REG_FROM_MEM_REG) \
   || defined(ARCH_HAS_LOAD64_REG_FROM_MEM_REG) \
   || defined(ARCH_HAS_STORE32_REG_AT_MEM_REG) \
   || defined(ARCH_HAS_STORE64_REG_AT_MEM_REG)
	if (!arch_block_reserve_insns(arch, 1)) return false;
	arch_insn_t* insn = arch_insn_init_as(arch_block_next_insn(arch), ARCH_OP_SET_REG_IMMADDR);
	vreg(insn, 0, virt_reg);
	immaddr(insn, 1, addr);
	return true;
#  else
	assert(false); /* insufficient emitters, but this was called */
#  endif
#else
	assert(false); /* insufficient emitters, but this was called */
#endif
}

/*
 * Emits architecture instructions to load a register with a 32-bit value from
 * memory.
 * 
 * Depending on the architecture's instructions, the following is done:
 * - If immediate addresses are supported, the addr_base and addr_offset
 *   parameters are combined to form the address to be used;
 * - Failing that, if loading via a single indirection is supported, but
 *   displacements are not, addr_reg is used;
 * - If loading via a single indirection with displacement is supported,
 *   addr_reg and addr_offset are used.
 * 
 * In:
 *   addr_reg: The virtual register that the base or complete address may be
 *     in.
 *   dest_reg: The virtual register to be loaded into.
 *   addr_base: The base address to be used if virt_reg does not contain it.
 *   addr_offset: The offset to apply to addr_base or to the address contained
 *     in virt_reg.
 * Out:
 *   arch: The block of architecture instructions to add to.
 * Returns:
 *   true if memory allocation succeeded for the instruction; false if not.
 */
static bool load32(arch_block_t* arch, virt_reg_t addr_reg, virt_reg_t dest_reg, uintptr_t addr_base, int16_t addr_offset)
{
#if defined(ARCH_HAS_LOAD32_REG_FROM_MEM_IMMADDR)
	if (!arch_block_reserve_insns(arch, 1)) return false;
	arch_insn_t* insn = arch_insn_init_as(arch_block_next_insn(arch), ARCH_OP_LOAD32_REG_FROM_MEM_IMMADDR);
	vreg(insn, 0, dest_reg);
	immaddr(insn, 1, addr_base, addr_offset);
	return true;
#elif defined(ARCH_HAS_SET_REG_IMMADDR)
#  if defined(ARCH_HAS_LOAD32_REG_FROM_MEM_REG_OFF16S)
	if (!arch_block_reserve_insns(arch, 1)) return false;
	arch_insn_t* insn = arch_insn_init_as(arch_block_next_insn(arch), ARCH_OP_LOAD32_REG_FROM_MEM_REG_OFF16S);
	vreg(insn, 0, dest_reg);
	vreg(insn, 1, addr_reg);
	imm16s(insn, 2, addr_offset);
	return true;
#  elif defined(ARCH_HAS_LOAD32_REG_FROM_MEM_REG)
	if (!arch_block_reserve_insns(arch, 1)) return false;
	arch_insn_t* insn = arch_insn_init_as(arch_block_next_insn(arch), ARCH_OP_LOAD32_REG_FROM_MEM_REG);
	vreg(insn, 0, dest_reg);
	vreg(insn, 1, addr_reg);
	return true;
#  else
#    warning Insufficient emitters to load 32-bit registers from memory addresses
	assert(false); /* insufficient emitters, but this was called */
#  endif
#else
#  warning Insufficient emitters to load 32-bit registers from memory addresses
	assert(false); /* insufficient emitters, but this was called */
#endif
}

/*
 * Emits architecture instructions to store a 32-bit value into memory from a
 * register.
 * 
 * Depending on the architecture's instructions, the following is done:
 * - If immediate addresses are supported, the addr_base and addr_offset
 *   parameters are combined to form the address to be used;
 * - Failing that, if loading via a single indirection is supported, but
 *   displacements are not, addr_reg is used;
 * - If loading via a single indirection with displacement is supported,
 *   addr_reg and addr_offset are used.
 * 
 * In:
 *   addr_reg: The virtual register that the base or complete address may be
 *     in.
 *   src_reg: The virtual register containing the value to be stored.
 *   addr_base: The base address to be used if virt_reg does not contain it.
 *   addr_offset: The offset to apply to addr_base or to the address contained
 *     in virt_reg.
 * Out:
 *   arch: The block of architecture instructions to add to.
 * Returns:
 *   true if memory allocation succeeded for the instruction; false if not.
 */
static bool store32(arch_block_t* arch, virt_reg_t addr_reg, virt_reg_t src_reg, uintptr_t addr_base, int16_t addr_offset)
{
#if defined(ARCH_HAS_STORE32_REG_AT_MEM_IMMADDR)
	if (!arch_block_reserve_insns(arch, 1)) return false;
	arch_insn_t* insn = arch_insn_init_as(arch_block_next_insn(arch), ARCH_OP_STORE32_REG_AT_MEM_IMMADDR);
	vreg(insn, 0, src_reg);
	immaddr(insn, 1, addr_base, addr_offset);
	return true;
#elif defined(ARCH_HAS_SET_REG_IMMADDR)
#  if defined(ARCH_HAS_STORE32_REG_AT_MEM_REG_OFF16S)
	if (!arch_block_reserve_insns(arch, 1)) return false;
	arch_insn_t* insn = arch_insn_init_as(arch_block_next_insn(arch), ARCH_OP_STORE32_REG_AT_MEM_REG_OFF16S);
	vreg(insn, 0, src_reg);
	vreg(insn, 1, addr_reg);
	imm16s(insn, 2, addr_offset);
	return true;
#  elif defined(ARCH_HAS_STORE32_REG_AT_MEM_REG)
	if (!arch_block_reserve_insns(arch, 1)) return false;
	arch_insn_t* insn = arch_insn_init_as(arch_block_next_insn(arch), ARCH_OP_STORE32_REG_AT_MEM_REG);
	vreg(insn, 0, src_reg);
	vreg(insn, 1, addr_reg);
	return true;
#  else
#    warning Insufficient emitters to store 32-bit registers to memory
	assert(false); /* insufficient emitters, but this was called */
#  endif
#else
#  warning Insufficient emitters to store 32-bit registers to memory
	assert(false); /* insufficient emitters, but this was called */
#endif
}

#if defined(ARCH_HAS_64BIT_REGS)
/*
 * Emits architecture instructions to store a 64-bit value into memory from a
 * register.
 * 
 * Depending on the architecture's instructions, the following is done:
 * - If immediate addresses are supported, the addr_base and addr_offset
 *   parameters are combined to form the address to be used;
 * - Failing that, if loading via a single indirection is supported, but
 *   displacements are not, addr_reg is used;
 * - If loading via a single indirection with displacement is supported,
 *   addr_reg and addr_offset are used.
 * 
 * In:
 *   addr_reg: The virtual register that the base or complete address may be
 *     in.
 *   src_reg: The virtual register containing the value to be stored.
 *   addr_base: The base address to be used if virt_reg does not contain it.
 *   addr_offset: The offset to apply to addr_base or to the address contained
 *     in virt_reg.
 * Out:
 *   arch: The block of architecture instructions to add to.
 * Returns:
 *   true if memory allocation succeeded for the instruction; false if not.
 */
static bool store64(arch_block_t* arch, virt_reg_t addr_reg, virt_reg_t src_reg, uintptr_t addr_base, int16_t addr_offset)
{
#if defined(ARCH_HAS_STORE64_REG_AT_MEM_IMMADDR)
	if (!arch_block_reserve_insns(arch, 1)) return false;
	arch_insn_t* insn = arch_insn_init_as(arch_block_next_insn(arch), ARCH_OP_STORE64_REG_AT_MEM_IMMADDR);
	vreg(insn, 0, src_reg);
	immaddr(insn, 1, addr_base, addr_offset);
	return true;
#elif defined(ARCH_HAS_SET_REG_IMMADDR)
#  if defined(ARCH_HAS_STORE64_REG_AT_MEM_REG_OFF16S)
	if (!arch_block_reserve_insns(arch, 1)) return false;
	arch_insn_t* insn = arch_insn_init_as(arch_block_next_insn(arch), ARCH_OP_STORE64_REG_AT_MEM_REG_OFF16S);
	vreg(insn, 0, src_reg);
	vreg(insn, 1, addr_reg);
	imm16s(insn, 2, addr_offset);
	return true;
#  elif defined(ARCH_HAS_STORE64_REG_AT_MEM_REG)
	if (!arch_block_reserve_insns(arch, 1)) return false;
	arch_insn_t* insn = arch_insn_init_as(arch_block_next_insn(arch), ARCH_OP_STORE64_REG_AT_MEM_REG);
	vreg(insn, 0, src_reg);
	vreg(insn, 1, addr_reg);
	return true;
#  else
#    warning Insufficient emitters to store 64-bit registers to memory
	assert(false); /* insufficient emitters, but this was called */
#  endif
#else
#  warning Insufficient emitters to store 64-bit registers to memory
	assert(false); /* insufficient emitters, but this was called */
#endif
}
#endif /* ARCH_HAS_64BIT_REGS */

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
				assert(false); /* insufficient emitters, but this IL was added */
#endif
				break;

			case IL_OP_EXIT_FRAME:
#if defined(ARCH_HAS_EXIT_FRAME)
				FAIL_IF(!arch_block_reserve_insns(arch, 1), arch_block_free(arch));
				arch_insn_init_as(arch_block_next_insn(arch), ARCH_OP_EXIT_FRAME);
#else
#  warning Insufficient emitters for IL operation EXIT_FRAME
				assert(false); /* insufficient emitters, but this IL was added */
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
				assert(false); /* insufficient emitters, but this IL was added */
#endif
				break;

			case IL_OP_RETURN:
#if defined(ARCH_HAS_RETURN)
				FAIL_IF(!arch_block_reserve_insns(arch, 1), arch_block_free(arch));
				arch_insn_init_as(arch_block_next_insn(arch), ARCH_OP_RETURN);
#else
#  warning Insufficient emitters for IL operation RETURN
				assert(false); /* insufficient emitters, but this IL was added */
#endif
				break;

			case IL_OP_SLL32:
			case IL_OP_SRL32:
			case IL_OP_SRA32:
			{
				uintptr_t base = il->values[il_insn->inputs[0]].value.addr.base;
				int16_t offset = il->values[il_insn->inputs[0]].value.addr.offset;
				// TODO More proper register allocation.
				FAIL_IF(!set_base(arch, 0, base), arch_block_free(arch));
				// TODO In the below code, don't add LSW_OFFSET if performing
				//      a 32-bit op on a 32-bit source value.
				FAIL_IF(!set_address(arch, 0, base + offset + LSW_OFFSET), arch_block_free(arch));
				FAIL_IF(!load32(arch, 0, 1, base, offset + LSW_OFFSET), arch_block_free(arch));
				switch (il->insns[i].opcode) {
					case IL_OP_SLL32:
						FAIL_IF(!arch_block_reserve_insns(arch, 1), arch_block_free(arch));
#if defined(ARCH_HAS_SLL32_REG_IMM8U_TO_REG)
						insn = arch_insn_init_as(arch_block_next_insn(arch), ARCH_OP_SLL32_REG_IMM8U_TO_REG);
						vreg(insn, 0, 1);
						imm8u(insn, 1, il_insn->argument);
						vreg(insn, 2, 1);
#elif defined(ARCH_HAS_SLL32_IMM8U_TO_REG)
						insn = arch_insn_init_as(arch_block_next_insn(arch), ARCH_OP_SLL32_IMM8U_TO_REG);
						imm8u(insn, 0, il_insn->argument);
						vreg(insn, 1, 1);
#else
#  warning Insufficient emitters for IL operation SLL32
						assert(false); /* insufficient emitters, but this IL was added */
#endif
						break;

					case IL_OP_SRL32:
						FAIL_IF(!arch_block_reserve_insns(arch, 1), arch_block_free(arch));
#if defined(ARCH_HAS_SRL32_REG_IMM8U_TO_REG)
						insn = arch_insn_init_as(arch_block_next_insn(arch), ARCH_OP_SRL32_REG_IMM8U_TO_REG);
						vreg(insn, 0, 1);
						imm8u(insn, 1, il_insn->argument);
						vreg(insn, 2, 1);
#elif defined(ARCH_HAS_SRL32_IMM8U_TO_REG)
						insn = arch_insn_init_as(arch_block_next_insn(arch), ARCH_OP_SRL32_IMM8U_TO_REG);
						imm8u(insn, 0, il_insn->argument);
						vreg(insn, 1, 1);
#else
#  warning Insufficient emitters for IL operation SRL32
						assert(false); /* insufficient emitters, but this IL was added */
#endif
						break;

					case IL_OP_SRA32:
						FAIL_IF(!arch_block_reserve_insns(arch, 1), arch_block_free(arch));
#if defined(ARCH_HAS_SRA32_REG_IMM8U_TO_REG)
						insn = arch_insn_init_as(arch_block_next_insn(arch), ARCH_OP_SRA32_REG_IMM8U_TO_REG);
						vreg(insn, 0, 1);
						imm8u(insn, 1, il_insn->argument);
						vreg(insn, 2, 1);
#elif defined(ARCH_HAS_SRA32_IMM8U_TO_REG)
						insn = arch_insn_init_as(arch_block_next_insn(arch), ARCH_OP_SRA32_IMM8U_TO_REG);
						imm8u(insn, 0, il_insn->argument);
						vreg(insn, 1, 1);
#else
#  warning Insufficient emitters for IL operation SRA32
						assert(false); /* insufficient emitters, but this IL was added */
#endif
						break;

					default:
						break;
				}

				/* Now it's time to sign-extend and store the result.
				 * The base might be different, in which case emit code to
				 * reload it. */
				if (base != il->values[il_insn->outputs[0]].value.addr.base) {
					base = il->values[il_insn->outputs[0]].value.addr.base;
					FAIL_IF(!set_base(arch, 0, base), arch_block_free(arch));
				}
#if defined(ARCH_HAS_64BIT_REGS)
				/* The offset is probably also different. */
				offset = il->values[il_insn->outputs[0]].value.addr.offset;
				FAIL_IF(!set_address(arch, 0, base + offset), arch_block_free(arch));
#  if defined(ARCH_HAS_SIGN_EXTEND_REG32_TO_SELF64)
				/* Sign-extend. Store later. */
				FAIL_IF(!arch_block_reserve_insns(arch, 1), arch_block_free(arch));
				insn = arch_insn_init_as(arch_block_next_insn(arch), ARCH_OP_SIGN_EXTEND_REG32_TO_SELF64);
				vreg(insn, 0, 1);
#  elif (defined(ARCH_HAS_SLL64_IMM8U_TO_REG) \
      || defined(ARCH_HAS_SLL64_REG_IMM8U_TO_REG)) \
     && (defined(ARCH_HAS_SRA64_IMM8U_TO_REG) \
      || defined(ARCH_HAS_SRA64_REG_IMM8U_TO_REG))
				/* Sign-extend via SLL of 32 and SRA of 32. Store later. */
				FAIL_IF(!arch_block_reserve_insns(arch, 2), arch_block_free(arch));
#    if defined(ARCH_HAS_SLL64_REG_IMM8U_TO_REG)
				insn = arch_insn_init_as(arch_block_next_insn(arch), ARCH_OP_SLL64_REG_IMM8U_TO_REG);
				vreg(insn, 0, 1);
				imm8u(insn, 1, 32);
				vreg(insn, 2, 1);
#    elif defined(ARCH_HAS_SLL64_IMM8U_TO_REG)
				insn = arch_insn_init_as(arch_block_next_insn(arch), ARCH_OP_SLL64_IMM8U_TO_REG);
				imm8u(insn, 0, 32);
				vreg(insn, 1, 1);
#    endif
#    if defined(ARCH_HAS_SRA64_REG_IMM8U_TO_REG)
				insn = arch_insn_init_as(arch_block_next_insn(arch), ARCH_OP_SRA64_REG_IMM8U_TO_REG);
				vreg(insn, 0, 1);
				imm8u(insn, 1, 32);
				vreg(insn, 2, 1);
#    elif defined(ARCH_HAS_SRA64_IMM8U_TO_REG)
				insn = arch_insn_init_as(arch_block_next_insn(arch), ARCH_OP_SRA64_IMM8U_TO_REG);
				imm8u(insn, 0, 32);
				vreg(insn, 1, 1);
#    endif
#  else
#    warning Insufficient emitters to sign-extend 32-bit values to 64-bit
				assert(false); /* insufficient emitters, but this IL was added */
#  endif
				/* Now that the value is sign-extended, store it. */
				// TODO Support writes to 32-bit locations.
				FAIL_IF(!store64(arch, 0, 1, base, offset), arch_block_free(arch));
#else /* !ARCH_HAS_64BIT_REGS */
#  if (defined(ARCH_HAS_SRA32_IMM8U_TO_REG) \
    || defined(ARCH_HAS_SRA32_REG_IMM8U_TO_REG))
				/* The offset is probably also different. */
				offset = il->values[il_insn->outputs[0]].value.addr.offset;
				FAIL_IF(!set_address(arch, 0, base + offset + LSW_OFFSET), arch_block_free(arch));
				/* Store the lower half back. */
				FAIL_IF(!store32(arch, 0, 1, base, offset + LSW_OFFSET), arch_block_free(arch));
				/* Sign-extend via SRA of 31, killing the original value.
				 * Store later. */
				FAIL_IF(!arch_block_reserve_insns(arch, 1), arch_block_free(arch));
#    if defined(ARCH_HAS_SRA32_REG_IMM8U_TO_REG)
				insn = arch_insn_init_as(arch_block_next_insn(arch), ARCH_OP_SRA32_REG_IMM8U_TO_REG);
				vreg(insn, 0, 1);
				imm8u(insn, 1, 31);
				vreg(insn, 2, 1);
#    elif defined(ARCH_HAS_SRA32_IMM8U_TO_REG)
				insn = arch_insn_init_as(arch_block_next_insn(arch), ARCH_OP_SRA32_IMM8U_TO_REG);
				imm8u(insn, 0, 31);
				vreg(insn, 1, 1);
#    endif
#  else
#    warning Insufficient emitters to sign-extend 32-bit values to 64-bit
				assert(false); /* insufficient emitters, but this IL was added */
#  endif
				/* Now that the high half is available, store it. */
				// TODO Support writes to 32-bit locations.
				FAIL_IF(!set_address(arch, 0, base + offset + MSW_OFFSET), arch_block_free(arch));
				FAIL_IF(!store32(arch, 0, 1, base, offset + MSW_OFFSET), arch_block_free(arch));
#endif /* [!]ARCH_HAS_64BIT_REGS */
				break;
			}

			case IL_OP_DELETED:
				break;
		}
	}

	return true;
}
