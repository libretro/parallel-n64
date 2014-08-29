/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - Neb Dynarec - Architecture operation declarations       *
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

#ifndef __NEB_DYNAREC_ARCH_OPS_H__
#define __NEB_DYNAREC_ARCH_OPS_H__

#include <stdint.h>

#include "arch-config.h"
#include "il-ops.h"

/*
 * List of available instructions on the architecture for which the
 * Neb Dynarec is being compiled.
 * 
 * Operands for any instruction follow the order in its name.
 * 
 * For example, ARCH_OP_STORE32_IMM32_AT_MEM_IMMADDR will have 2 operands;
 * the first will be the (32-bit) immediate value to store, and the second
 * will be the native address at which to store it.
 * 
 * 2-operand ALU operations such as ARCH_OP_SUB32_REG_TO_REG use the first
 * operand as the second source operand, and the second operand as both the
 * first source operand and the destination. For example, SUB 1, 2 will
 * take the value of register 2, subtract register 1's, and store the result
 * in register 2.
 * 
 * 3-operand ALU operations such as ARCH_OP_SUB32_REG_REG_TO_REG use the first
 * and second operands as the source operands in that order, and the third
 * operand as the destination. For example, SUB 1, 2, 3 will take the value
 * of register 1, subtract register 2's, and store the result in register 3.
 */
enum arch_opcode {
	ARCH_OP_DELETED,
#ifdef ARCH_HAS_ENTER_FRAME
	ARCH_OP_ENTER_FRAME,
#endif
#ifdef ARCH_HAS_EXIT_FRAME
	ARCH_OP_EXIT_FRAME,
#endif
#ifdef ARCH_HAS_RETURN
	ARCH_OP_RETURN,
#endif

#ifdef ARCH_HAS_SET_REG_IMM32U
	ARCH_OP_SET_REG_IMM32U,
#endif
#ifdef ARCH_HAS_SET_REG_IMM64U
	ARCH_OP_SET_REG_IMM64U,
#endif
#ifdef ARCH_HAS_SET_REG_IMMADDR
	ARCH_OP_SET_REG_IMMADDR,
#endif

#ifdef ARCH_HAS_SIGN_EXTEND_REG32_TO_SELF64
	ARCH_OP_SIGN_EXTEND_REG32_TO_SELF64,
#endif

#ifdef ARCH_HAS_LOAD32_REG_FROM_MEM_IMMADDR
	ARCH_OP_LOAD32_REG_FROM_MEM_IMMADDR,
#endif
#ifdef ARCH_HAS_LOAD32_REG_FROM_MEM_REG
	ARCH_OP_LOAD32_REG_FROM_MEM_REG,
#endif
#ifdef ARCH_HAS_LOAD32_REG_FROM_MEM_REG_OFF16S
	ARCH_OP_LOAD32_REG_FROM_MEM_REG_OFF16S,
#endif
#ifdef ARCH_HAS_LOAD64_REG_FROM_MEM_IMMADDR
	ARCH_OP_LOAD64_REG_FROM_MEM_IMMADDR,
#endif
#ifdef ARCH_HAS_LOAD64_REG_FROM_MEM_REG
	ARCH_OP_LOAD64_REG_FROM_MEM_REG,
#endif
#ifdef ARCH_HAS_LOAD64_REG_FROM_MEM_REG_OFF16S
	ARCH_OP_LOAD64_REG_FROM_MEM_REG_OFF16S,
#endif

#ifdef ARCH_HAS_STORE32_IMM32U_AT_MEM_IMMADDR
	ARCH_OP_STORE32_IMM32U_AT_MEM_IMMADDR,
#endif
#ifdef ARCH_HAS_STORE32_REG_AT_MEM_IMMADDR
	ARCH_OP_STORE32_REG_AT_MEM_IMMADDR,
#endif
#ifdef ARCH_HAS_STORE32_REG_AT_MEM_REG
	ARCH_OP_STORE32_REG_AT_MEM_REG,
#endif
#ifdef ARCH_HAS_STORE32_REG_AT_MEM_REG_OFF16S
	ARCH_OP_STORE32_REG_AT_MEM_REG_OFF16S,
#endif
#ifdef ARCH_HAS_STORE32_IMM32U_AT_MEM_IMMADDR
	ARCH_OP_STORE32_IMM32U_AT_MEM_IMMADDR,
#endif
#ifdef ARCH_HAS_STORE64_REG_AT_MEM_IMMADDR
	ARCH_OP_STORE64_REG_AT_MEM_IMMADDR,
#endif
#ifdef ARCH_HAS_STORE64_REG_AT_MEM_REG
	ARCH_OP_STORE64_REG_AT_MEM_REG,
#endif
#ifdef ARCH_HAS_STORE64_REG_AT_MEM_REG_OFF16S
	ARCH_OP_STORE64_REG_AT_MEM_REG_OFF16S,
#endif

#ifdef ARCH_HAS_SLL32_IMM8U_TO_REG
	ARCH_OP_SLL32_IMM8U_TO_REG,
#endif
#ifdef ARCH_HAS_SLL32_REG_IMM8U_TO_REG
	ARCH_OP_SLL32_REG_IMM8U_TO_REG,
#endif
#ifdef ARCH_HAS_SLL64_IMM8U_TO_REG
	ARCH_OP_SLL64_IMM8U_TO_REG,
#endif
#ifdef ARCH_HAS_SLL64_REG_IMM8U_TO_REG
	ARCH_OP_SLL64_REG_IMM8U_TO_REG,
#endif

#ifdef ARCH_HAS_SRL32_IMM8U_TO_REG
	ARCH_OP_SRL32_IMM8U_TO_REG,
#endif
#ifdef ARCH_HAS_SRL32_REG_IMM8U_TO_REG
	ARCH_OP_SRL32_REG_IMM8U_TO_REG,
#endif
#ifdef ARCH_HAS_SRL64_IMM8U_TO_REG
	ARCH_OP_SRL64_IMM8U_TO_REG,
#endif
#ifdef ARCH_HAS_SRL64_REG_IMM8U_TO_REG
	ARCH_OP_SRL64_REG_IMM8U_TO_REG,
#endif

#ifdef ARCH_HAS_SRA32_IMM8U_TO_REG
	ARCH_OP_SRA32_IMM8U_TO_REG,
#endif
#ifdef ARCH_HAS_SRA32_REG_IMM8U_TO_REG
	ARCH_OP_SRA32_REG_IMM8U_TO_REG,
#endif
#ifdef ARCH_HAS_SRA64_IMM8U_TO_REG
	ARCH_OP_SRA64_IMM8U_TO_REG,
#endif
#ifdef ARCH_HAS_SRA64_REG_IMM8U_TO_REG
	ARCH_OP_SRA64_REG_IMM8U_TO_REG,
#endif
};

typedef enum arch_opcode arch_opcode_t;

typedef uint8_t virt_reg_t;

enum arch_operand_type {
	/* A value (arch_operand_t) is unused in an arch_insn_t. */
	ARCH_OT_NONE,
	/* A value (arch_operand_t) is a virtual register, to be mapped to a real
	 * native register. The register number is in arch_operand_t.value.reg. */
	ARCH_OT_REG,
	/* A value (arch_operand_t) is a signed 8-bit immediate value. The
	 * immediate value is in arch_operand_t.value._8s. */
	ARCH_OT_IMM8S,
	/* A value (arch_operand_t) is an unsigned 8-bit immediate value. The
	 * immediate value is in arch_operand_t.value._8u. */
	ARCH_OT_IMM8U,
	/* A value (arch_operand_t) is a signed 16-bit immediate value. The
	 * immediate value is in arch_operand_t.value._16s. */
	ARCH_OT_IMM16S,
	/* A value (arch_operand_t) is an unsigned 16-bit immediate value. The
	 * immediate value is in arch_operand_t.value._16u. */
	ARCH_OT_IMM16U,
	/* A value (arch_operand_t) is a signed 32-bit immediate value. The
	 * immediate value is in arch_operand_t.value._32s. */
	ARCH_OT_IMM32S,
	/* A value (arch_operand_t) is an unsigned 32-bit immediate value. The
	 * immediate value is in arch_operand_t.value._32u. */
	ARCH_OT_IMM32U,
	/* A value (arch_operand_t) is a signed 64-bit immediate value. The
	 * immediate value is in arch_operand_t.value._64s. */
	ARCH_OT_IMM64S,
	/* A value (arch_operand_t) is an unsigned 64-bit immediate value. The
	 * immediate value is in arch_operand_t.value._64u. */
	ARCH_OT_IMM64U,
	/* A value (arch_operand_t) is an immediate address value of architecture-
	 * dependent width. The immediate value is in arch_operand_t.value.addr. */
	ARCH_OT_IMMADDR,
};

typedef enum arch_operand_type arch_operand_type_t;

struct arch_operand {
	arch_operand_type_t type;
	union {
		virt_reg_t reg;
		int8_t _8s;
		uint8_t _8u;
		int16_t _16s;
		uint16_t _16u;
		int32_t _32s;
		uint32_t _32u;
		int64_t _64s;
		uint64_t _64u;
		uintptr_t addr;
	} value;
};

typedef struct arch_operand arch_operand_t;

struct arch_insn {
	arch_opcode_t opcode; // The opcode to emit.
	arch_operand_t operands[4];
};

typedef struct arch_insn arch_insn_t;

struct arch_block {
	size_t insn_count; // Number of instructions for this block.
	size_t insn_capacity; // Number of allocated entries in 'insns' below.
	arch_insn_t* insns; // Instructions in malloc'd space.
};

typedef struct arch_block arch_block_t;

/*
 * Emits an architecture instruction block from an IL block.
 * 
 * In:
 *   il: A pointer to the IL block structure.
 * Out:
 *   arch: The architecture instruction block to be written. It is initialised
 *     by this function.
 * Returns:
 *   true if memory allocation succeeded for the architecture instruction
 *   block; false if not.
 */
bool arch_emit_from_il(const il_block_t* il, arch_block_t* arch);

/* Helper functions */
/*
 * Any function returning 'bool' below, returns 'true' if it succeeds in
 * allocating or reallocating memory, or 'false' if the allocation or
 * reallocation fails.
 * 
 * Any function that takes an argument called 'add' requests the number of
 * objects to add, not the new capacity.
 * 
 * Any function that returns a pointer of the same type as its first argument
 * returns the first argument to allow call chaining.
 */

arch_block_t* arch_block_init(arch_block_t* block);
bool arch_block_reserve_insns(arch_block_t* block, size_t add);
void arch_block_free(arch_block_t* block);

/*
 * Returns a pointer to the next architecture instruction to be emitted in a
 * block, adding one to 'insn_count'.
 * 
 * The caller is responsible for having reserved space for the new
 * architecture instruction by calling arch_block_reserve_insns.
 */
arch_insn_t* arch_block_next_insn(arch_block_t* block);

arch_insn_t* arch_insn_init(arch_insn_t* insn);
arch_insn_t* arch_insn_init_as(arch_insn_t* insn, arch_opcode_t opcode);

#endif /* !__NEB_DYNAREC_ARCH_OPS_H__ */
