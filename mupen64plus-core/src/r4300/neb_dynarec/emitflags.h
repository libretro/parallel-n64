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

#ifndef __NEB_DYNAREC_EMITFLAGS_H__
#define __NEB_DYNAREC_EMITFLAGS_H__

#include <stdint.h>
#include "n64ops.h"

/* An instruction has sufficient emitters on the native architecture running
 * the Neb Dynarec. This implies that there are enough emitters to start and
 * end a function as well as, for a function that is not all NOPs, load and
 * save registers from &reg[N]. */
#define INSTRUCTION_HAS_EMITTERS           0x0001
/* An instruction does not require the delay_slot flag to be set during its
 * execution, because it does not care whether it's in the delay slot of a
 * branch or not.
 * 
 * An opcode is considered to ignore the delay_slot flag when it has all of
 * the following properties:
 * a) The opcode does not ever raise an exception. If it can, then the EPC
 *    is set to the address of the current instruction, minus 4 if it's in
 *    the delay slot of a branch, so the opcode requires delay_slot to be set.
 * b) The opcode is specified to have predictable operation in the delay slot
 *    of a branch. All branches, jumps and ERET are specified to have
 *    unpredictable operation if placed in the delay slot of a branch; any
 *    function implementing those opcodes can declare a Core Error if the
 *    delay_slot flag is 1 during their operation.
 */
#define INSTRUCTION_IGNORES_DELAY_SLOT     0x0002
/* An instruction requires a Coprocessor 1 usability check before itself.
 * Coprocessor 1 is deemed to be unusable if bit 29 of Coprocessor 0 register
 * 12, Status, is unset; this triggers a Coprocessor Unusable exception.
 * Such a check is only needed once per block, before the first floating-point
 * instruction, unless an MTC0 instruction affects Coprocessor 0 register 12.
 * More correctly, the check is only needed once per time an interrupt is
 * resolved, but in Mupen64plus, interrupts are resolved only after branches
 * and jumps.
 */
#define INSTRUCTION_NEEDS_COP1_CHECK       0x0004

void fill_emit_flags(n64_insn_t* insn);

#endif /* !__NEB_DYNAREC_EMITFLAGS_H__ */
