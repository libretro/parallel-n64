/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - r4300.h                                                 *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
 *   Copyright (C) 2002 Hacktarux                                          *
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

#ifndef M64P_R4300_R4300_H
#define M64P_R4300_R4300_H

#include <stdint.h>

#include "ops.h"
#include "r4300_core.h"
#include "recomp.h"

#if !defined(__arm64__)
extern struct precomp_instr *PC;
extern int64_t reg[32], hi, lo;
extern uint32_t next_interrupt;
extern int stop;
#define mupencorePC PC
#define mupencorereg reg
#define mupencorestop stop
#else
#include "new_dynarec/arm64/memory_layout_arm64.h"
#define mupencorePC    (RECOMPILER_MEMORY->rml_PC)
#define mupencorereg   (RECOMPILER_MEMORY->rml_reg)
#define hi             (RECOMPILER_MEMORY->rml_hi)
#define next_interrupt (RECOMPILER_MEMORY->rml_next_interrupt)
#define lo             (RECOMPILER_MEMORY->rml_lo)
#define mupencorestop  (RECOMPILER_MEMORY->rml_stop)
#endif
extern unsigned int llbit;
extern long long int local_rs;
extern uint32_t skip_jump;
extern unsigned int dyna_interp;
extern unsigned int r4300emu;
extern uint32_t last_addr;
#define COUNT_PER_OP_DEFAULT 2
extern unsigned int count_per_op;
extern cpu_instruction_table current_instruction_table;

void r4300_init(void);
void r4300_execute(void);
void r4300_step(void);

// r4300 emulators
#define CORE_PURE_INTERPRETER 0
#define CORE_INTERPRETER      1
#define CORE_DYNAREC          2

#endif /* M64P_R4300_R4300_H */
