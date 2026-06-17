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

#if !defined(__arm64__) && !defined(__aarch64__)
extern struct precomp_instr *PC;
extern uint32_t next_interrupt;
extern int g_cp0_cycle_count;
#define mupencorePC PC
#define mupencorereg reg
#if defined(_M_X64)
/* region 14 / Phase 2d (increment 9): the core stop flag's storage moves into
 * g_dev.r4300.new_dynarec_hot_state (member stop, offset 0x10c). The alias is on
 * mupencorestop -- the name every C consumer uses -- rather than on the bare
 * token 'stop', which is also a struct member name in libretro.h
 * (retro_camera_callback::stop) and must not be clobbered. The linkage addresses
 * the member g_dev-relative. Every mupencorestop use-site is in a function body
 * where g_dev is in scope.
 *
 * region 14 / Phase 2d (increment 10): the multiply/divide accumulators hi and
 * lo move into the struct too (members hi 0x240, lo 0x248). Unlike reg these are
 * not declared in the Hacktarux assemble.h, and the bare tokens 'hi'/'lo' do not
 * collide with any reachable header identifier, so they are aliased directly.
 * Consumers are the interpreter, the cached-interp mult/div ops, the Ari64 JIT,
 * the Hacktarux JIT (which takes &hi/&lo) and the linkage; all see g_dev. reg is
 * migrated in a later increment (it has its own assemble.h declarations). */
#define mupencorestop (g_dev.r4300.new_dynarec_hot_state.stop)
#define hi            (g_dev.r4300.new_dynarec_hot_state.hi)
#define lo            (g_dev.r4300.new_dynarec_hot_state.lo)
extern int64_t reg[32];
#else
extern int64_t reg[32], hi, lo;
extern int stop;
#define mupencorestop stop
#endif
#else
#include "new_dynarec/arm64/memory_layout_arm64.h"
#define mupencorePC    (RECOMPILER_MEMORY->rml_PC)
#define mupencorereg   (RECOMPILER_MEMORY->rml_reg)
#define hi             (RECOMPILER_MEMORY->rml_hi)
#define next_interrupt (RECOMPILER_MEMORY->rml_next_interrupt)
#define lo             (RECOMPILER_MEMORY->rml_lo)
#define mupencorestop  (RECOMPILER_MEMORY->rml_stop)
#endif
extern int frame_break;
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
#define CORE_DYNAREC          2 /* Hacktarux dynarec */
#define CORE_DYNAREC_ARI64    3 /* Ari64 new_dynarec */

#endif /* M64P_R4300_R4300_H */
