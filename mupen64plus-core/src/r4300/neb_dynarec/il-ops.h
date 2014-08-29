/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - Neb Dynarec - Intermediate language declarations        *
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

#ifndef __NEB_DYNAREC_ILOPS_H__
#define __NEB_DYNAREC_ILOPS_H__

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#include "n64ops.h"

enum il_opcode {
	/* Represents an op that has been deleted by optimisations and need not
	 * be emitted. Useful mostly if the IL is stored in an array. */
	IL_OP_DELETED,

	/* Represents the entry into a function, which saves callee-saved
	 * registers used in it. */
	IL_OP_ENTER_FRAME,
	/* Represents the exit from a function, which restores callee-saved
	 * registers used in it. */
	IL_OP_EXIT_FRAME,
	/* Represents setting the N64 Program Counter to a new value.
	 * Argument: The address, in the N64 address space, to set. */
	IL_OP_SET_PC,
	/* Represents a return from a function to its caller. */
	IL_OP_RETURN,

	/* Represents a left shift of a 64-bit register's lower 32 bits.
	 * Input value 1: The value that is to be shifted.
	 * Output value 1: Th destination of the operation.
	 * Argument: The number of bits to shift the input value. */
	IL_OP_SLL32,
	/* Represents a right logical shift of a 64-bit register's lower 32 bits.
	 * (Upper bits are filled with 0.)
	 * Input value 1: The value that is to be shifted.
	 * Output value 1: Th destination of the operation.
	 * Argument: The number of bits to shift the input value. */
	IL_OP_SRL32,
	/* Represents a right arithmetic shift of a 64-bit register's lower 32
	 * bits.
	 * (Upper bits are filled with copies of the original value's bit 31.)
	 * Input value 1: The value that is to be shifted.
	 * Output value 1: Th destination of the operation.
	 * Argument: The number of bits to shift the input value. */
	IL_OP_SRA32,
};

typedef enum il_opcode il_opcode_t;

enum il_value_type {
	/* A value (il_value_t) was allocated but became unused as a result of
	 * optimisations. */
	IL_VT_DELETED,
	/* A value (il_value_t) represents a 32-bit integer constant. It's held in
	 * il_value_t.value._32. */
	IL_VT_CONST_INT32,
	/* A value (il_value_t) represents a 64-bit integer constant. It's held in
	 * il_value_t.value._64. */
	IL_VT_CONST_INT64,
	/* A value (il_value_t) represents the lower 32 bits of a 64-bit value
	 * read from somewhere in native memory (for example, &reg[N], &lo, &hi).
	 * The address is held in il_value_t.value.addr.base and offset. */
	IL_VT_READ_INT_LO32,
	/* A value (il_value_t) represents a 64-bit value read from somewhere in
	 * native memory (for example, &reg[N], &lo, &hi).
	 * The address is held in il_value_t.value.addr.base and offset. */
	IL_VT_READ_INT_64,
	/* A value (il_value_t) represents a 32-bit value read from somewhere in
	 * native memory (for example, &g_cp0_regs[N]).
	 * The address is held in il_value_t.value.addr.base and offset. */
	IL_VT_READ_INT_32,
	/* A value (il_value_t) represents the lower 32 bits of a 64-bit value
	 * to be written somewhere in native memory (for example, &reg[N], &lo,
	 * &hi).
	 * The address is held in il_value_t.value.addr.base and offset. */
	IL_VT_WRITE_INT_LO32,
	/* A value (il_value_t) represents a 64-bit value to be written somewhere
	 * in native memory (for example, &reg[N], &lo, &hi).
	 * The address is held in il_value_t.value.addr.base and offset. */
	IL_VT_WRITE_INT_64,
	/* A value (il_value_t) represents a 32-bit value to be written somewhere
	 * in native memory (for example, &g_cp0_regs[N]).
	 * The address is held in il_value_t.value.addr.base and offset. */
	IL_VT_WRITE_INT_32,
};

typedef enum il_value_type il_value_type_t;

typedef uint32_t il_value_number_t;

struct il_insn {
	il_opcode_t opcode;
	size_t input_count; // Number of input values for this opcode.
	size_t input_capacity; // Number of allocated entries in 'inputs' below.
	il_value_number_t* inputs; // Value numbers in malloc'd space.
	size_t output_count; // Number of output values for this opcode.
	size_t output_capacity; // Number of allocated entries in 'outputs' below.
	il_value_number_t* outputs; // Value numbers in malloc'd space.
	int64_t argument; // Argument to Set PC and shift opcodes.
	ssize_t target; // How many instructions away a jump goes to (0 = self).
};

typedef struct il_insn il_insn_t;

struct il_value {
	il_value_type_t type;
	union {
		int32_t _32;
		int64_t _64;
		struct {
			uintptr_t base;
			int16_t offset;
		} addr;
	} value; // If it's a constant, its value will be here.
};

typedef struct il_value il_value_t;

struct il_block {
	size_t insn_count; // Number of instructions for this block.
	size_t insn_capacity; // Number of allocated entries in 'insns' below.
	il_insn_t* insns; // Instructions in malloc'd space.
	il_value_number_t value_count;
	il_value_number_t value_capacity;
	il_value_t* values; // Descriptors for all values in the block.
};

typedef struct il_block il_block_t;

/*
 * Emits preliminary IL for a range of N64 instructions.
 * 
 * In:
 *   n64: A pointer to the first N64 instruction structure to be read.
 *   n64_count: The number of valid structures to be read from 'n64'.
 * Out:
 *   il: The IL block to be written. It is initialised by this function.
 * Returns:
 *   true if memory allocation succeeded for the IL; false if not.
 */
bool il_emit_from_n64(const n64_insn_t* n64, uint32_t n64_count, il_block_t* il);

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

il_block_t* il_block_init(il_block_t* block);
bool il_block_reserve_insns(il_block_t* block, size_t add);
bool il_block_reserve_values(il_block_t* block, il_value_number_t add);
void il_block_free(il_block_t* block);

/*
 * Returns a pointer to the next IL instruction to be emitted in a block,
 * adding one to 'insn_count'.
 * 
 * The caller is responsible for having reserved space for the new
 * IL instruction by calling il_block_reserve_insns.
 */
il_insn_t* il_block_next_insn(il_block_t* block);

il_insn_t* il_insn_init(il_insn_t* insn);
il_insn_t* il_insn_init_as(il_insn_t* insn, il_opcode_t opcode);
bool il_insn_reserve_inputs(il_insn_t* insn, size_t add);
bool il_insn_reserve_outputs(il_insn_t* insn, size_t add);
void il_insn_free(il_insn_t* insn);

il_value_number_t il_value_add_const_int32(il_block_t* block, int32_t constant);
il_value_number_t il_value_add_const_int64(il_block_t* block, int64_t constant);
il_value_number_t il_value_add_read_int_lo32(il_block_t* block, void* addr, int16_t offset);
il_value_number_t il_value_add_read_int_64(il_block_t* block, void* addr, int16_t offset);
il_value_number_t il_value_add_read_int_32(il_block_t* block, void* addr, int16_t offset);
il_value_number_t il_value_add_write_int_lo32(il_block_t* block, void* addr, int16_t offset);
il_value_number_t il_value_add_write_int_64(il_block_t* block, void* addr, int16_t offset);
il_value_number_t il_value_add_write_int_32(il_block_t* block, void* addr, int16_t offset);
void il_value_set_const_int32(il_value_t* value, int32_t constant);
void il_value_set_const_int64(il_value_t* value, int64_t constant);
void il_value_set_read_int_lo32(il_value_t* value, void* addr, int16_t offset);
void il_value_set_read_int_64(il_value_t* value, void* addr, int16_t offset);
void il_value_set_read_int_32(il_value_t* value, void* addr, int16_t offset);
void il_value_set_write_int_lo32(il_value_t* value, void* addr, int16_t offset);
void il_value_set_write_int_64(il_value_t* value, void* addr, int16_t offset);
void il_value_set_write_int_32(il_value_t* value, void* addr, int16_t offset);
void il_value_delete(il_value_t* value);

#endif /* !__NEB_DYNAREC_ILOPS_H__ */
