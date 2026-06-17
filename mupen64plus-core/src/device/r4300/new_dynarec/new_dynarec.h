/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - new_dynarec.h                                           *
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

#ifndef M64P_R4300_NEW_DYNAREC_H
#define M64P_R4300_NEW_DYNAREC_H

#include <stddef.h>
#include <stdint.h>

#define NEW_DYNAREC_X86 1
#define NEW_DYNAREC_AMD64 2
#define NEW_DYNAREC_ARM 3
#define NEW_DYNAREC_ARM64 4

/* alias: mupen64plus-next spells the x86-64 backend NEW_DYNAREC_X64; parallel-n64
 * spells it NEW_DYNAREC_AMD64. Same value (2). Provide the next spelling so the
 * shared hot-state layout below can be written against next's naming. */
#ifndef NEW_DYNAREC_X64
#define NEW_DYNAREC_X64 NEW_DYNAREC_AMD64
#endif

#include "../recomp_types.h" /* for struct precomp_instr */

struct r4300_core;

/* region 14 / Phase 2a: the "hot" dynarec state, converging toward
 * mupen64plus-next's struct new_dynarec_hot_state. The field order of the
 * SHARED region (dynarec_local .. memory_map) is kept byte-identical to next so
 * the generated asm_defines offsets match next's known x64 layout; this is
 * pinned by the static_asserts in new_dynarec.c. parallel-n64-specific staging
 * (the LDL/LDR + mult/div blocks, cpu_byte/hword, last_count, restore_candidate)
 * is appended AFTER the shared region so it does not perturb the shared offsets.
 *
 * NOTE (Phase 2a): this struct is DEFINED but not yet referenced by the JIT or
 * the hand-written linkage -- those still use the flat globals. Wiring happens in
 * 2c (codegen) and 2d (linkage). Defining it now lets us validate the layout
 * (offsets vs next) before any machine-code emission changes. */
struct new_dynarec_hot_state
{
#if (NEW_DYNAREC == NEW_DYNAREC_ARM64) || (NEW_DYNAREC == NEW_DYNAREC_X64)
    uint64_t dynarec_local[32];
#else
    uint32_t dynarec_local[16];
#endif
    int cycle_count;
    int pending_exception;
    int pcaddr;
    int stop;
    char* invc_ptr;
    uint32_t address;
    uint64_t rdword;
    uint64_t wdword;
    uint32_t wword;
    uint32_t cp1_fcr0;
    uint32_t cp1_fcr31;
    int64_t  regs[32];
    int64_t  hi;
    int64_t  lo;
    uint32_t cp0_regs[32];
    uint64_t cp0_latch;
    float* cp1_regs_simple[32];
    double* cp1_regs_double[32];
    uint64_t cp2_latch;
    uint32_t rounding_modes[4];
    int branch_target;
    struct precomp_instr* pc;
    struct precomp_instr fake_pc;
    int64_t rs;
    int64_t rt;
    int64_t rd;
    intptr_t ram_offset;
    uintptr_t mini_ht[32][2];
    uintptr_t memory_map[1048576];

    /* --- parallel-n64-specific extensions (after the next-shared region) --- */
    /* width-specific memory transfer staging used by the memory layer */
    uint8_t  cpu_byte;
    uint16_t cpu_hword;
    /* cycle accounting carried across blocks */
    int last_count;
    /* LDL/LDR operand staging (this tree calls void merge shims) */
    struct { uint64_t rt; uint64_t rs; uint32_t shift; } ldlr_block;
    /* 64-bit mult/div operand staging (routed through cached-interp ops) */
    int64_t multdiv_rs_scratch;
    int64_t multdiv_rt_scratch;
    struct precomp_instr multdiv_fake_pc;
    /* code-invalidation restore bookkeeping */
    unsigned char restore_candidate[512];
};

#if !defined(__arm64__) && !defined(__aarch64__)
#ifdef __cplusplus
extern "C" {
#endif
extern int pcaddr;
extern int pending_exception;
#ifdef __cplusplus
}
#endif
#else
#include "arm64/memory_layout_arm64.h"
#define pcaddr            (RECOMPILER_MEMORY->rml_pcaddr)
#define pending_exception (RECOMPILER_MEMORY->rml_pending_exception)
#endif

void invalidate_all_pages(void);
void invalidate_block(unsigned int block);
void invalidate_cached_code_new_dynarec(uint32_t address, size_t size);
void new_dynarec_init(void);
void new_dyna_start(void);
void new_dynarec_cleanup(void);

#endif /* M64P_R4300_NEW_DYNAREC_H */
