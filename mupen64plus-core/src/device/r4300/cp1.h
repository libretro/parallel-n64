/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - cp1.h                                                   *
 *   Mupen64Plus homepage: https://mupen64plus.org/                        *
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

#ifndef M64P_DEVICE_R4300_CP1_H
#define M64P_DEVICE_R4300_CP1_H

#include <stdint.h>
#include "osal/preproc.h"
#include "new_dynarec/new_dynarec.h"

typedef union {
    int64_t  dword;
    double   float64;
    float    float32[2];
}cp1_reg;

struct cp1
{
    cp1_reg regs[32];

    /* ari64 keeps fcr0/fcr31 and the simple/double register pointer tables in
     * its hot state; the interpreters and Hacktarux use these. Both present for
     * runtime-selectable dynarecs. */
    uint32_t fcr0;
    uint32_t fcr31;

    float* regs_simple[32];
    double* regs_double[32];

    /* This is the x86 version of the rounding mode contained in FCR31.
     * It should not really be here. Its size should also really be uint16_t,
     * because FLDCW (Floating-point LoaD Control Word) loads 16-bit control
     * words. However, x86/gcop1.c and x86-64/gcop1.c update this variable
     * using 32-bit stores. */
    uint32_t rounding_mode;

#ifdef OSAL_SSE
    uint32_t flush_mode;
#endif

    /* ari64 hot state pointer (NULL/unused for the other cores). */
    struct new_dynarec_hot_state* new_dynarec_hot_state;
};

/* Hacktarux codegen offsets target the cp1 struct fields; ari64 uses its own
 * cp1_regs_simple/double offsets through generated code. */
#define R4300_CP1_REGS_S_OFFSET (\
    offsetof(struct r4300_core, cp1) + \
    offsetof(struct cp1, regs_simple))

#define R4300_CP1_REGS_D_OFFSET (\
    offsetof(struct r4300_core, cp1) + \
    offsetof(struct cp1, regs_double))

#define R4300_CP1_FCR0_OFFSET (\
    offsetof(struct r4300_core, cp1) + \
    offsetof(struct cp1, fcr0))

#define R4300_CP1_FCR31_OFFSET (\
    offsetof(struct r4300_core, cp1) + \
    offsetof(struct cp1, fcr31))

void init_cp1(struct cp1* cp1, struct new_dynarec_hot_state* new_dynarec_hot_state);
void poweron_cp1(struct cp1* cp1);

cp1_reg* r4300_cp1_regs(struct cp1* cp1);
float** r4300_cp1_regs_simple(struct cp1* cp1);
double** r4300_cp1_regs_double(struct cp1* cp1);

uint32_t* r4300_cp1_fcr0(struct cp1* cp1);
uint32_t* r4300_cp1_fcr31(struct cp1* cp1);

void set_fpr_pointers(struct cp1* cp1, uint32_t newStatus);

void update_x86_rounding_mode(struct cp1* cp1);

#endif /* M64P_DEVICE_R4300_CP1_H */

