/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - asm_defines.c                                           *
 *   Mupen64Plus homepage: https://mupen64plus.org/                        *
 *   Copyright (C) 2016 Bobby Smiles                                       *
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

/**
 * region 14 / Phase 2b: shared C<->asm struct-offset definitions.
 *
 * This file is compiled with the same flags as the core (but without LTO),
 * producing an object that embeds each requested struct offset as a printable
 * string of the form "\n@ASM_DEFINE offsetof_struct_<s>_<m> 0xXXXXXXXX\n".
 * tools/gen_asm_defines.awk extracts those strings (via `strings`) and emits
 * x64/asm_defines_{nasm,gas}.h, so the hand-written linkage assembly can reach
 * struct members by generated offset rather than by a flat extern symbol -- the
 * one source of truth is the C struct, which cannot then drift from the asm.
 *
 * This mechanism (adapted from mupen64plus-next) does not run a program; the
 * values are baked into the object's data and read with `strings`, so it works
 * cross-compiled and without objdump/dumpbin/nm.
 *
 * Phase 2b emits the within-hot_state FIELD offsets only. The structural
 * offsets (device->r4300, r4300->new_dynarec_hot_state, r4300->extra_memory)
 * are added in Phase 2c, when the hot_state struct and the JIT code cache are
 * embedded into struct r4300_core (deferred from 2a). Until then the generated
 * header is not yet consumed by the linkage (still on flat symbols); generating
 * it now establishes and validates the machinery.
 */

#include "device/r4300/new_dynarec/new_dynarec.h"
#include "device/device.h"
#include "device/r4300/r4300_core.h"

#include <stddef.h>
#include <stdint.h>

/* new_dynarec.h aliases some hot-state fields to g_dev.r4300.new_dynarec_hot_state.<f>
 * macros (region 14, Phase 2d) so the JIT and the C use-sites can address them
 * uniformly. This generator's whole job is to emit the *raw struct offsets* via
 * offsetof(struct new_dynarec_hot_state, <field>), which needs the bare member
 * identifier -- the aliases would expand inside offsetof() and not compile. Drop
 * them here; this TU never dereferences the live state, it only inspects layout.
 * (Harmless no-ops for fields that are not currently aliased.) */
#undef pcaddr
#undef pending_exception
#undef cycle_count
#undef memory_map
#undef restore_candidate

#define HEX(n) ((n) >= 10 ? ('a' + ((n) - 10)) : ('0' + (n)))

/* Creates a structure whose bytes form a string like
 * "\n@ASM_DEFINE offsetof_blah_blah 0xdeadbeef\n"
 *
 * The string is distinctive enough that it will not appear by chance, so the
 * object file can be piped straight to awk. The ensure_32bit negative-array
 * bound also asserts the offset fits in 32 bits (rel32 reach). */
#define _DEFINE(str, sym, val) \
    const struct { \
        char before[sizeof(str)-1]; \
        char hexval[8]; \
        char after; \
        char ensure_32bit[(val) > UINT64_C(0xffffffff) ? -1 : 1]; \
    } sym = { \
        str, \
        { \
            HEX(((val) >> 28) & 0xf), \
            HEX(((val) >> 24) & 0xf), \
            HEX(((val) >> 20) & 0xf), \
            HEX(((val) >> 16) & 0xf), \
            HEX(((val) >> 12) & 0xf), \
            HEX(((val) >>  8) & 0xf), \
            HEX(((val) >>  4) & 0xf), \
            HEX(((val) >>  0) & 0xf) \
        }, \
        '\n', \
        {0} \
    }

/* Export member m of structure s. */
#define DEFINE(s, m) \
    _DEFINE("\n@ASM_DEFINE offsetof_struct_" #s "_" #m " 0x", \
            __offsetof_struct_##s##_##m, \
            offsetof(struct s, m))

/* Structural offsets: how to reach the hot state from g_dev.
 * Phase 2c added the embedding (extra_memory + new_dynarec_hot_state in
 * struct r4300_core), so these are now well-defined. The linkage composes
 * g_dev + offsetof(device,r4300) + offsetof(r4300_core,new_dynarec_hot_state)
 * + offsetof(new_dynarec_hot_state,FIELD) to address a hot-state member. */
DEFINE(device, r4300);
DEFINE(r4300_core, new_dynarec_hot_state);
DEFINE(r4300_core, extra_memory);

/* hot_state field offsets (shared region; pinned to next's x64 layout by the
 * static asserts in new_dynarec_64.c). */
DEFINE(new_dynarec_hot_state, dynarec_local);
DEFINE(new_dynarec_hot_state, cycle_count);
DEFINE(new_dynarec_hot_state, pending_exception);
DEFINE(new_dynarec_hot_state, pcaddr);
DEFINE(new_dynarec_hot_state, stop);
DEFINE(new_dynarec_hot_state, invc_ptr);
DEFINE(new_dynarec_hot_state, mem_address);
DEFINE(new_dynarec_hot_state, rdword);
DEFINE(new_dynarec_hot_state, wdword);
DEFINE(new_dynarec_hot_state, wword);
DEFINE(new_dynarec_hot_state, cp1_fcr0);
DEFINE(new_dynarec_hot_state, cp1_fcr31);
DEFINE(new_dynarec_hot_state, regs);
DEFINE(new_dynarec_hot_state, hi);
DEFINE(new_dynarec_hot_state, lo);
DEFINE(new_dynarec_hot_state, cp0_regs);
DEFINE(new_dynarec_hot_state, cp1_regs_simple);
DEFINE(new_dynarec_hot_state, cp1_regs_double);
DEFINE(new_dynarec_hot_state, rounding_modes);
DEFINE(new_dynarec_hot_state, branch_target);
DEFINE(new_dynarec_hot_state, pc);
DEFINE(new_dynarec_hot_state, fake_pc);
DEFINE(new_dynarec_hot_state, rs);
DEFINE(new_dynarec_hot_state, rt);
DEFINE(new_dynarec_hot_state, rd);
DEFINE(new_dynarec_hot_state, ram_offset);
DEFINE(new_dynarec_hot_state, mini_ht);
DEFINE(new_dynarec_hot_state, memory_map);

/* parallel-n64-specific hot_state extensions (appended after the shared region) */
DEFINE(new_dynarec_hot_state, hs_cpu_byte);
DEFINE(new_dynarec_hot_state, hs_cpu_hword);
DEFINE(new_dynarec_hot_state, last_count);
DEFINE(new_dynarec_hot_state, restore_candidate);
