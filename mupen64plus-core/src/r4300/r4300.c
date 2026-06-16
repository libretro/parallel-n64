/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - r4300.c                                                 *
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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "api/callbacks.h"
#include "api/debugger.h"
#include "api/m64p_types.h"
#include "cached_interp.h"
#include "cp0_private.h"
#include "cp1_private.h"
#include "interrupt.h"
#include "main/main.h"
#include "main/device.h"
#include "main/rom.h"
#include "new_dynarec/new_dynarec.h"
#include "ops.h"
#include "pure_interp.h"
#include "r4300.h"
#include "r4300_core.h"
#include "recomp.h"
#include "recomph.h"
#include "tlb.h"


unsigned int r4300emu = 0;
unsigned int count_per_op = COUNT_PER_OP_DEFAULT;
unsigned int llbit;
/* Set at the frame boundary (VI): every CPU core
 * unwinds out of its execution loop back to retro_run, and the next
 * retro_run re-enters where it left off.  Unlike 'stop' this does not
 * tear anything down.  A plain global on every arch, including arm64
 * where 'stop' itself lives in RECOMPILER_MEMORY: the aarch64 linkage
 * reaches it adrp-style, like base_addr. */
int frame_break;
/* Referenced unconditionally by cp0.c / exception.c / interrupt.c /
 * reset.c / pure_interp.c on every architecture, so it must be defined on
 * every architecture -- not just the x86 (NEW_DYNAREC < ARM) path. Keeping
 * it inside the arch guard below left it undefined on arm/arm64, breaking
 * the link there. A plain global like frame_break above. */
int g_cp0_cycle_count = 0;
#if !defined(__arm64__) && !defined(__aarch64__)
int stop;
#if NEW_DYNAREC < NEW_DYNAREC_ARM
int64_t reg[32], hi, lo;
uint32_t next_interrupt;
struct precomp_instr *PC;
#endif
#endif
long long int local_rs;
uint32_t skip_jump = 0;
unsigned int dyna_interp = 0;
uint32_t last_addr;

cpu_instruction_table current_instruction_table;

void generic_jump_to(uint32_t address)
{
   if (r4300emu == CORE_PURE_INTERPRETER)
      mupencorePC->addr = address;
   else {
#if NEW_DYNAREC
      if (r4300emu == CORE_DYNAREC_ARI64)
         last_addr = pcaddr;
      else
         jump_to(address);
#else
      jump_to(address);
#endif
   }
}

#if !defined(NO_ASM)
static void dynarec_setup_code(void)
{
   // The dynarec jumps here after we call dyna_start and it prepares
   // Here we need to prepare the initial code block and jump to it
   /* Re-entrant: resume wherever the last frame break left us. */
   jump_to(last_addr);

   // Prevent segfault on failed jump_to
   if (!actual->block || !actual->code)
      dyna_stop();
}
#endif

void r4300_init(void)
{
    current_instruction_table = cached_interpreter_table;

    mupencorestop = 0;

    if (r4300emu == CORE_PURE_INTERPRETER)
    {
        DebugMessage(M64MSG_INFO, "Starting R4300 emulator: Pure Interpreter");
        r4300emu = CORE_PURE_INTERPRETER;
        pure_interpreter_init();
    }
#if defined(DYNAREC)
    else if (r4300emu >= 2)
    {
#if NEW_DYNAREC && defined(HAVE_DYNAREC_HACKTARUX)
        /* Both dynarecs are built in: emumode 3 selects Ari64,
         * anything else (2, legacy values) selects Hacktarux. */
        if (r4300emu == CORE_DYNAREC_ARI64)
        {
            DebugMessage(M64MSG_INFO, "Starting R4300 emulator: Dynamic Recompiler (Ari64)");
            init_blocks();
            new_dynarec_init();
        }
        else
        {
            DebugMessage(M64MSG_INFO, "Starting R4300 emulator: Dynamic Recompiler (Hacktarux)");
            r4300emu = CORE_DYNAREC;
            init_blocks();
            last_addr = UINT32_C(0xa4000040);
        }
#elif NEW_DYNAREC
        DebugMessage(M64MSG_INFO, "Starting R4300 emulator: Dynamic Recompiler (Ari64)");
        r4300emu = CORE_DYNAREC_ARI64;
        init_blocks();
        new_dynarec_init();
#else
        DebugMessage(M64MSG_INFO, "Starting R4300 emulator: Dynamic Recompiler (Hacktarux)");
        r4300emu = CORE_DYNAREC;
        init_blocks();
        last_addr = UINT32_C(0xa4000040);
#endif
    }
#endif
    else /* if (r4300emu == CORE_INTERPRETER) */
    {
        DebugMessage(M64MSG_INFO, "Starting R4300 emulator: Cached Interpreter");
        r4300emu = CORE_INTERPRETER;
        init_blocks();
        jump_to(UINT32_C(0xa4000040));

        /* Prevent segfault on failed jump_to */
        if (!actual->block)
            return;

        last_addr = mupencorePC->addr;
    }
}

void r4300_execute(void)
{
    if (r4300emu == CORE_PURE_INTERPRETER)
    {
        pure_interpreter();
    }
#if defined(DYNAREC)
    else if (r4300emu >= 2)
    {
#if NEW_DYNAREC && defined(HAVE_DYNAREC_HACKTARUX)
        if (r4300emu == CORE_DYNAREC_ARI64)
        {
            new_dyna_start();
            if (mupencorestop)
                new_dynarec_cleanup();
        }
        else
        {
            dyna_start(dynarec_setup_code);
            if (mupencorestop)
                mupencorePC++;
        }
#elif NEW_DYNAREC
        new_dyna_start();
        if (mupencorestop)
            new_dynarec_cleanup();
#else
        dyna_start(dynarec_setup_code);
        if (mupencorestop)
            mupencorePC++;
#endif
        if (mupencorestop)
            free_blocks();
    }
#endif
    else /* if (r4300emu == CORE_INTERPRETER) */
    {
        r4300_step();

        if (mupencorestop)
            free_blocks();
    }

    if (mupencorestop)
        DebugMessage(M64MSG_INFO, "R4300 emulator finished.");
}

int retro_stop_stepping(void);

void r4300_step(void)
{
   while (!mupencorestop && !retro_stop_stepping())
   {
      mupencorePC->ops();
   }
}
