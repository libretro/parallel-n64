/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - rml_offsets.h                                           *
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
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/* region 14 / Phase 3b: byte offsets of the fields in recompiler_memory_layout
 * (the aarch64 dynarec hot state, RECOMPILER_MEMORY / x29).
 *
 * This is the single source of truth for those offsets. It contains nothing but
 * preprocessor integer arithmetic, so it is safe to #include from both C and the
 * GAS assembler:
 *   - memory_layout_arm64.h includes it and pins every value to the real struct
 *     with RML_CHECK_SIZE (offsetof) static assertions, so a layout change that
 *     is not reflected here fails the C build.
 *   - linkage_aarch64.S includes it and binds its rml_off_<field> symbols to
 *     these macros, so the trampoline's [x29, #...] displacements track the same
 *     numbers automatically.
 *
 * Previously this chain was duplicated by hand in both files; that duplication is
 * what allowed the assembler offsets to drift from the C struct. Keep this file
 * free of C declarations and of any GAS-only directives. */

#ifndef M64P_RML_OFFSETS_H
#define M64P_RML_OFFSETS_H

#define RML_SIZE_EXTRA_MEMORY 0
#define RML_SIZE_DYNAREC_LOCAL     RML_SIZE_EXTRA_MEMORY      + 0
#define RML_SIZE_NEXT_INTERRUPT    RML_SIZE_DYNAREC_LOCAL     + 256
#define RML_SIZE_CYCLE_COUNT       RML_SIZE_NEXT_INTERRUPT    + 4
#define RML_SIZE_LAST_COUNT        RML_SIZE_CYCLE_COUNT       + 4
#define RML_SIZE_PENDING_EXCEPTION RML_SIZE_LAST_COUNT        + 4
#define RML_SIZE_PCADDR            RML_SIZE_PENDING_EXCEPTION + 4
#define RML_SIZE_STOP              RML_SIZE_PCADDR            + 4
#define RML_SIZE_INVC_PTR          RML_SIZE_STOP              + 4
#define RML_SIZE_ADDRESS           RML_SIZE_INVC_PTR          + 8
#define RML_SIZE_READMEM_DWORD     RML_SIZE_ADDRESS           + 8
#define RML_SIZE_CPU_DWORD         RML_SIZE_READMEM_DWORD     + 8
#define RML_SIZE_CPU_WORD          RML_SIZE_CPU_DWORD         + 8
#define RML_SIZE_CPU_HWORD         RML_SIZE_CPU_WORD          + 4
#define RML_SIZE_CPU_BYTE          RML_SIZE_CPU_HWORD         + 2
#define RML_SIZE_FCR0              RML_SIZE_CPU_HWORD         + 4
#define RML_SIZE_FCR31             RML_SIZE_FCR0              + 4
#define RML_SIZE_REG               RML_SIZE_FCR31             + 4
#define RML_SIZE_HI                RML_SIZE_REG               + 256
#define RML_SIZE_LO                RML_SIZE_HI                + 8
#define RML_SIZE_G_CP0_REGS        RML_SIZE_LO                + 8
#define RML_SIZE_REG_COP1_SIMPLE   RML_SIZE_G_CP0_REGS        + 128
#define RML_SIZE_REG_COP1_DOUBLE   RML_SIZE_REG_COP1_SIMPLE   + 256
#define RML_SIZE_ROUNDING_MODES    RML_SIZE_REG_COP1_DOUBLE   + 256
#define RML_SIZE_BRANCH_TARGET     RML_SIZE_ROUNDING_MODES    + 16
#define RML_SIZE_PC                RML_SIZE_BRANCH_TARGET     + 8
#define RML_SIZE_FAKE_PC           RML_SIZE_PC                + 8
#define RML_SIZE_RAM_OFFSET        RML_SIZE_FAKE_PC           + 208
#define RML_SIZE_MINI_HT           RML_SIZE_RAM_OFFSET        + 8
#define RML_SIZE_RESTORE_CANDIDATE RML_SIZE_MINI_HT           + 512
#define RML_SIZE_INSTR_ADDR        RML_SIZE_RESTORE_CANDIDATE + 512
#define RML_SIZE_LINK_REGISTER     RML_SIZE_INSTR_ADDR        + 8
#define RML_SIZE_MEMORY_MAP        RML_SIZE_LINK_REGISTER     + 8

#endif /* M64P_RML_OFFSETS_H */
