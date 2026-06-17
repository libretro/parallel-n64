/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - cp0_private.h                                           *
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

#ifndef M64P_R4300_CP0_PRIVATE_H
#define M64P_R4300_CP0_PRIVATE_H

#include "cp0.h"

#if !defined(__arm64__) && !defined(__aarch64__)
#if defined(_M_X64)
/* region 14 / Phase 2d (increment 11): the CP0 register file g_cp0_regs moves
 * into g_dev.r4300.new_dynarec_hot_state (member cp0_regs[32], offset 0x250).
 * The alias is an array lvalue, so g_cp0_regs[i], &g_cp0_regs[i], memset() and
 * sizeof() all behave exactly as before. Consumers span the interpreter, CP0
 * core, exception path, the Ari64 JIT and the Hacktarux JIT; each use-site is in
 * a function body where g_dev is in scope. */
#define g_cp0_regs (g_dev.r4300.new_dynarec_hot_state.cp0_regs)
#else
extern uint32_t g_cp0_regs[CP0_REGS_COUNT];
#endif
#else
#include "new_dynarec/arm64/memory_layout_arm64.h"
#define g_cp0_regs (RECOMPILER_MEMORY->rml_g_cp0_regs)
#endif

/* CP0 write latch, see cp0.c for details. */
extern uint64_t cp0_latch;

int check_cop1_unusable(void);

#endif /* M64P_R4300_CP0_PRIVATE_H */

