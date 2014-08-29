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

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "il-ops.h"
#include "n64ops.h"

il_block_t* il_block_init(il_block_t* block)
{
	block->insn_count = block->insn_capacity = 0;
	block->insns = NULL;
	block->value_count = block->value_capacity = 0;
	block->values = NULL;
	return block;
}

bool il_block_reserve_insns(il_block_t* block, size_t add)
{
	if (block->insn_count + add <= block->insn_capacity)
		return true;
	size_t new_capacity = block->insn_capacity * 2 + add;
	il_insn_t* new_allocation = realloc(block->insns, new_capacity * sizeof(il_insn_t));
	if (new_allocation) {
		block->insns = new_allocation;
		block->insn_capacity = new_capacity;
	}
	return new_allocation != NULL;
}

bool il_block_reserve_values(il_block_t* block, il_value_number_t add)
{
	if (block->value_count + add <= block->value_capacity)
		return true;
	il_value_number_t new_capacity = block->value_capacity + add * 2;
	il_value_t* new_allocation = realloc(block->values, new_capacity * sizeof(il_value_t));
	if (new_allocation) {
		block->values = new_allocation;
		block->value_capacity = new_capacity;
	}
	return new_allocation != NULL;
}

void il_block_free(il_block_t* block)
{
	size_t i;
	for (i = 0; i < block->insn_count; i++) {
		il_insn_free(&block->insns[i]);
	}
	free(block->values);
	free(block->insns);
	il_block_init(block);
}

il_insn_t* il_block_next_insn(il_block_t* block)
{
	return &block->insns[block->insn_count++];
}

il_insn_t* il_insn_init(il_insn_t* insn)
{
	return il_insn_init_as(insn, IL_OP_DELETED);
}

il_insn_t* il_insn_init_as(il_insn_t* insn, il_opcode_t opcode)
{
	insn->opcode = opcode;
	insn->input_count = insn->input_capacity = 0;
	insn->inputs = NULL;
	insn->output_count = insn->output_capacity = 0;
	insn->outputs = NULL;
	insn->argument = 0;
	insn->target = 0;
	return insn;
}

bool il_insn_reserve_inputs(il_insn_t* insn, size_t add)
{
	size_t new_capacity = insn->input_capacity + add;
	il_value_number_t* new_allocation = realloc(insn->inputs, new_capacity * sizeof(il_value_number_t));
	if (new_allocation) {
		insn->inputs = new_allocation;
		insn->input_capacity = new_capacity;
	}
	return new_allocation != NULL;
}

bool il_insn_reserve_outputs(il_insn_t* insn, size_t add)
{
	size_t new_capacity = insn->output_capacity + add;
	il_value_number_t* new_allocation = realloc(insn->outputs, new_capacity * sizeof(il_value_number_t));
	if (new_allocation) {
		insn->outputs = new_allocation;
		insn->output_capacity = new_capacity;
	}
	return new_allocation != NULL;
}

void il_insn_free(il_insn_t* insn)
{
	free(insn->inputs);
	free(insn->outputs);
	il_insn_init(insn);
}

il_value_number_t il_value_add_const_int32(il_block_t* block, int32_t constant)
{
	block->values[block->value_count].type = IL_VT_CONST_INT32;
	block->values[block->value_count].value._32 = constant;
	return block->value_count++;
}

il_value_number_t il_value_add_const_int64(il_block_t* block, int64_t constant)
{
	block->values[block->value_count].type = IL_VT_CONST_INT64;
	block->values[block->value_count].value._64 = constant;
	return block->value_count++;
}

void il_value_set_const_int32(il_value_t* value, int32_t constant)
{
	value->type = IL_VT_CONST_INT32;
	value->value._32 = constant;
}

void il_value_set_const_int64(il_value_t* value, int64_t constant)
{
	value->type = IL_VT_CONST_INT64;
	value->value._64 = constant;
}

void il_value_delete(il_value_t* value)
{
	value->type = IL_VT_DELETED;
}

static bool in1(il_insn_t* insn, il_value_number_t in)
{
	if (!il_insn_reserve_inputs(insn, 1)) return false;
	insn->inputs[0] = in;
	return true;
}

static bool in2(il_insn_t* insn, il_value_number_t in1, il_value_number_t in2)
{
	if (!il_insn_reserve_inputs(insn, 2)) return false;
	insn->inputs[0] = in1;
	insn->inputs[1] = in2;
	return true;
}

static bool out1(il_insn_t* insn, il_value_number_t out)
{
	if (!il_insn_reserve_outputs(insn, 1)) return false;
	insn->outputs[0] = out;
	return true;
}

static bool out2(il_insn_t* insn, il_value_number_t out1, il_value_number_t out2)
{
	if (!il_insn_reserve_outputs(insn, 2)) return false;
	insn->outputs[0] = out1;
	insn->outputs[1] = out2;
	return true;
}

#define FAIL_IF(expr, cleanup) do { if (expr) { cleanup; return false; } } while (0)

bool il_emit_from_n64(const n64_insn_t* n64, uint32_t n64_count, il_block_t* il)
{
	il_insn_t* insn;
	const n64_insn_t* n64_insn = n64;
	const n64_insn_t* const n64_end = n64 + n64_count;

	assert(n64_count > 0); /* otherwise what's the point? */

	il_block_init(il);
	FAIL_IF(!il_block_reserve_insns(il, n64_count + 4)
	     || !il_block_reserve_values(il, n64_count * 3), il_block_free(il));

	il_insn_init_as(il_block_next_insn(il), IL_OP_ENTER_FRAME);

	while (n64_insn < n64_end
	    && (n64_insn->opcode == N64_OP_NOP
	     || n64_insn->opcode == N64_OP_CACHE
	     || n64_insn->opcode == N64_OP_SYNC)) {
		n64_insn++;
	}

	FAIL_IF(!il_block_reserve_insns(il, 3), il_block_free(il));

	il_insn_init_as(il_block_next_insn(il), IL_OP_SET_PC)
		->argument = (n64_insn - 1)->runtime + 1;
	il_insn_init_as(il_block_next_insn(il), IL_OP_EXIT_FRAME);
	il_insn_init_as(il_block_next_insn(il), IL_OP_RETURN);

	return true;
}
