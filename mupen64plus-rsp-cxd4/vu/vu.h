/*
 * mupen64plus-rsp-cxd4 - RSP Interpreter
 * Copyright (C) 2012-2013  RJ 'Iconoclast' Swedlow
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/******************************************************************************\
* Project:  MSP Emulation Layer for Vector Unit Computational Operations       *
* Authors:  Iconoclast                                                         *
* Release:  2013.05.15                                                         *
* License:  none (public domain)                                               *
\******************************************************************************/
#ifndef _VU_H
#define _VU_H

#define MAX_LONG (~0)
#if !(MAX_LONG < 0xFFFFFFFFFFFF)
#define MACHINE_SIZE_48_MIN
#endif

/*
 * vector-scalar element decoding
 *
 * Obviously, doing a switch jump table on (element & 0xF) is very fast
 * because it saves the decoding time of constantly fetching source elements.
 *
 * However, there are several disadvantages to the above method:
 *     * Extremely difficult to maintain.  (Algorithm fixes need 16 copies.)
 *     * Not accurate.  (Real H/W decodes all of the elements iteratively.)
 *     * Colossal boost in program size, cutting down command cache memory.
 *     * Using branch frames for performing at least one conditional jump.
 *     * Even if element = 0x0, it's still 8 un-merged, separate data movs.
 *     * Faster only for large-cache processors.  (RSP is very small-cache.)
 *     * Difficult to read.
 *     * Pointless to try to optimize that way when SSSE3 could be applied.
 */
static const int ei[16][8] = {
    { 00, 01, 02, 03, 04, 05, 06, 07 }, /* none (vector-only operand) */
    { 00, 01, 02, 03, 04, 05, 06, 07 },
    { 00, 00, 02, 02, 04, 04, 06, 06 }, /* 0Q */
    { 01, 01, 03, 03, 05, 05, 07, 07 }, /* 1Q */
    { 00, 00, 00, 00, 04, 04, 04, 04 }, /* 0H */
    { 01, 01, 01, 01, 05, 05, 05, 05 }, /* 1H */
    { 02, 02, 02, 02, 06, 06, 06, 06 }, /* 2H */
    { 03, 03, 03, 03, 07, 07, 07, 07 }, /* 3H */
    { 00, 00, 00, 00, 00, 00, 00, 00 }, /* 0 */
    { 01, 01, 01, 01, 01, 01, 01, 01 }, /* 1 */
    { 02, 02, 02, 02, 02, 02, 02, 02 }, /* 2 */
    { 03, 03, 03, 03, 03, 03, 03, 03 }, /* 3 */
    { 04, 04, 04, 04, 04, 04, 04, 04 }, /* 4 */
    { 05, 05, 05, 05, 05, 05, 05, 05 }, /* 5 */
    { 06, 06, 06, 06, 06, 06, 06, 06 }, /* 6 */
    { 07, 07, 07, 07, 07, 07, 07, 07 }  /* 7 */
};

/*
 * RSP virtual registers (of vector unit)
 * The most important are the 32 general-purpose vector registers.
 * The correct way to accurately store these is using big-endian vectors.
 *
 * For ?WC2 we may need to do byte-precision access just as directly.
 * This is ammended by using the `VU_S` and `VU_B` macros defined in `rsp.h`.
 */
static short VR[32][8];
static short VC[8]; /* vector/scalar coefficient */

/* #define EMULATE_VECTOR_RESULT_BUFFER */
/*
 * There is a hidden vector result register used for moving the data to the
 * real vector destination register near the end of the execute cycle for a
 * vector unit instruction, but it is not required to emulate the presence
 * of this register.  It is currently slower than just directly writing to
 * the destination vector register file from within the vector operation.
 */

/* #define PARALLELIZE_VECTOR_TRANSFERS */
/*
 * Leaving this defined, the RSP emulator will try to encourage parallel
 * transactions within vector element operations by shuffling the target
 * (partially scalar coefficient) vector register as necessary so that
 * the elements `i`(0..7) of VS can directly match up with 1:1
 * parallelism to the short elements `i`(0..7) of the shuffled VT.
 *
 * Be careful when compiling this with GCC or SSE vector support, as the
 * compiler may produce unstable results that can crash in some opcodes.
 */

#ifdef EMULATE_VECTOR_RESULT_BUFFER
static short Result[8];
#endif

#ifdef PARALLELIZE_VECTOR_TRANSFERS
#define VR_T(i) VC[i]
#else
#define VR_T(i) VR[vt][ei[e][i]]
#endif

#ifdef EMULATE_VECTOR_RESULT_BUFFER
#define VMUL_PTR    Result
#else
#define VMUL_PTR    VR[vd]
#endif

#define VR_D(i) VMUL_PTR[i]

#ifdef PARALLELIZE_VECTOR_TRANSFERS
#define ACC_R(i)    VR_D(i)
#define ACC_W(i)    VACC[i].s[00]
#else
#define ACC_R(i)    VACC[i].s[00]
#define ACC_W(i)    VR_D(i)
#endif
/*
 * If we want to parallelize vector transfers, we probably also want to
 * linearize the register files.  (VR dest. reads from VR src. op. VR trg.)
 * Lining up the emulator for VR[vd] = VR[vs] & VR[vt] is a lot easier than
 * doing it for VACC[i](15..0) = VR[vs][i] & VR[vt][i] inside of some loop.
 * However, the correct order in vector units is to update the accumulator
 * register file BEFORE the vector register file.  This is slower but more
 * accurate and even required in some cases (VMAC* and VMAD* operations).
 * However, it is worth sacrificing if it means doing vectors in parallel.
 */

int sub_mask[16] = {
    0x0,
    0x0,
    0x1, 0x1,
    0x3, 0x3, 0x3, 0x3,
    0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7
};

static inline void SHUFFLE_VECTOR(int vt, int e)
{
    register int i, j;
#if (0 == 0)
    j = sub_mask[e];
    e &= j;
    j ^= 07;
    for (i = 0; i < 8; i++)
        VC[i] = VR[vt][(i & j) | e];
#else
    if (e & 0x8)
        for (i = 0; i < 8; i++)
            VC[i] = VR[vt][(i & 00) | (e & 0x7)];
    else if (e & 0x4)
        for (i = 0; i < 8; i++)
            VC[i] = VR[vt][(i & 04) | (e & 0x3)];
    else if (e & 0x2)
        for (i = 0; i < 8; i++)
            VC[i] = VR[vt][(i & 06) | (e & 0x1)];
    else /* if ((e == 0b0000) || (e == 0b0001)) */
        for (i = 0; i < 8; i++)
            VC[i] = VR[vt][(i & 07) | (e & 0x0)];
#endif
    return;
}

/*
 * accumulator-indexing macros
 */
#define LO  00
#define MD  01
#define HI  02

static union ACC {
#ifdef MACHINE_SIZE_48_MIN
    signed e:  48; /* There are eight elements in the accumulator. */
#endif
    short int s[3]; /* Each element has a low, middle, and high 16-bit slice. */
    signed char SB[6];
/* 64-bit access: */
    unsigned char B[8];
    short int HW[4];
    unsigned short UHW[4];
    int W[2];
    unsigned int UW[2];
    long long int DW;
    unsigned long long UDW;
} VACC[8];

/*
 * special macro service for clamping accumulators
 *
 * Clamping on the RSP is the same as traditional vector units, not just SGI.
 * This algorithm, therefore, is public domain material.
 *
 * In almost all cases, the RSP requests clamping to bits 47..16 of each acc.
 * We therefore compare the 32-bit (signed int)(acc >> 16) and clamp it down
 * to, usually, 16-bit results (0x8000 if < -32768, 0x7FFF if > +32767).
 *
 * The exception is VMACQ, which requests a clamp index lsb of >> 17.
 */
#define CLAMP_BASE(acc, lo) ((signed int)(VACC[acc].DW >> lo))
/*
 * This algorithm might have a bug if you invoke shifts greater than 16,
 * because the 48-bit acc needs to be sign-extended when shifting right here.
 */

static inline void SIGNED_CLAMP(short* VD, int mode)
{
    register int i;

    switch (mode)
    {
        case 0: /* typical sign-clamp of accumulator-mid (bits 31:16) */
            for (i = 0; i < 8; i++)
                if (VACC[i].DW & 0x800000000000)
                    if ((VACC[i].DW & 0xFFFF80000000) != 0xFFFF80000000)
                        VD[i] = -32768;
                    else
                        VD[i] = VACC[i].s[MD];
                else
                    if ((VACC[i].DW & 0xFFFF80000000) != 0x000000000000)
                        VD[i] = +32767;
                    else
                        VD[i] = VACC[i].s[MD];
            return;
        case 1: /* sign-clamp accumulator-low (bits 15:0) */
            for (i = 0; i < 8; i++)
                if (VACC[i].DW & 0x800000000000)
                    if ((VACC[i].DW & 0xFFFF80000000) != 0xFFFF80000000)
                        VD[i] = 0;
                    else
                        VD[i] = VACC[i].s[LO];
                else
                    if ((VACC[i].DW & 0xFFFF80000000) != 0x000000000000)
                        VD[i] = ~0;
                    else
                        VD[i] = VACC[i].s[LO];
            return;
        case 2: /* oddified sign-clamp employed by VMACQ and VMULQ */
            for (i = 0; i < 8; i++)
            {
                register const signed int result = CLAMP_BASE(i, 17);

                if (result < -32768)
                    VD[i] = -32768 & ~0x000F;
                else if (result > +32767)
                    VD[i] = +32767 & ~0x000F;
                else
                    VD[i] = result & 0x0000FFF0;
            }
            return;
    }
}

/* special-purpose vector control registers */
unsigned short VCO; /* vector carry out register */
unsigned short VCC; /* vector compare code register */
unsigned char VCE; /* vector compare extension register */

static void res_V(int vd, int rd, int rt, int e)
{
    rt = rd = 0;
    message("C2\nRESERVED", 2); /* uncertain how to handle reserved, untested */
    for (e = 0; e < 8; e++)
        VR_D(e) = 0x0000; /* override behavior (Michael Tedder) */
    return;
}
static void res_M(int sa, int rd, int rt, int e)
{
    message("VRNDP/VRNDN/VMULQ", 0);
    res_V(sa, rd, rt, e);
    return; /* Ultra64 OS did have these, so one could implement this ext. */
}

/* vector computational multiplies */
static void VMULF(int vd, int vs, int vt, int e);
static void VMULU(int vd, int vs, int vt, int e);
/* omitted from the RCP:  VMULI "VRNDP" */
/* omitted from the RCP:  VMULQ */
static void VMUDL(int vd, int vs, int vt, int e);
static void VMUDM(int vd, int vs, int vt, int e);
static void VMUDN(int vd, int vs, int vt, int e);
static void VMUDH(int vd, int vs, int vt, int e);
static void VMACF(int vd, int vs, int vt, int e);
static void VMACU(int vd, int vs, int vt, int e);
/* omitted from the RCP:  VMACI "VRNDN" */
static void VMACQ(int vd, int vs, int vt, int e);
static void VMADL(int vd, int vs, int vt, int e);
static void VMADM(int vd, int vs, int vt, int e);
static void VMADN(int vd, int vs, int vt, int e);
static void VMADH(int vd, int vs, int vt, int e);

/* vector computational add operations */
static void VADD(int vd, int vs, int vt, int e);
static void VSUB(int vd, int vs, int vt, int e);
/* omitted from the RCP:  VSUT */
static void VABS(int vd, int vs, int vt, int e);
static void VADDC(int vd, int vs, int vt, int e);
static void VSUBC(int vd, int vs, int vt, int e);
/* omitted from the RCP:  VADDB */
/* omitted from the RCP:  VSUBB */
/* omitted from the RCP:  VACCB */
/* omitted from the RCP:  VSUCB */
/* omitted from the RCP:  VSAD */
/* omitted from the RCP:  VSAC */
/* omitted from the RCP:  VSUM */
static void VSAW(int vd, int vs, int vt, int e);
/* reserved "VACC" */
/* reserved "VSUC" */

/* vector computational select operations */
static void VLT(int vd, int vs, int vt, int e);
static void VEQ(int vd, int vs, int vt, int e);
static void VNE(int vd, int vs, int vt, int e);
static void VGE(int vd, int vs, int vt, int e);
static void VCL(int vd, int vs, int vt, int e);
static void VCH(int vd, int vs, int vt, int e);
static void VCR(int vd, int vs, int vt, int e);
static void VMRG(int vd, int vs, int vt, int e);

/* vector computational logical operations */
static void VAND(int vd, int vs, int vt, int e);
static void VNAND(int vd, int vs, int vt, int e);
static void VOR(int vd, int vs, int vt, int e);
static void VNOR(int vd, int vs, int vt, int e);
static void VXOR(int vd, int vs, int vt, int e);
static void VNXOR(int vd, int vs, int vt, int e);
/* reserved "VUNK" */
/* reserved */

/* vector computational divide operations */
static void VRCP(int vd, int de, int vt, int e);
static void VRCPL(int vd, int de, int vt, int e);
static void VRCPH(int vd, int de, int vt, int e);
static void VMOV(int vd, int de, int vt, int e);
static void VRSQ(int vd, int de, int vt, int e);
static void VRSQL(int vd, int de, int vt, int e);
static void VRSQH(int vd, int de, int vt, int e);
static void VNOP(int sa, int rd, int rt, int rs);
/*
 * Architecturally speaking, VMOV is still considered a reciprocal operation,
 * and VNOP is still considered a reciprocal square root operation.
 *
 * This is also true from the legacy and future perspectives, in which both
 * operation codes service an entirely different functionality.  In fact, one
 * of the extensions to GNU acknowledge none of the above "divide" mnemonics.
 */

/*
 * vector computational pack operations (None were used by the RSP.)
 *
 * `VEXTT` "VPKT"
 * `VEXTQ` "VPKQ"
 * `VEXTN` "VPKN"
 * reserved
 * `VINST` "VUPKT"
 * `VINSQ` "VUPKQ"
 * `VINSN` "VUPKN"
 * `VNULLOP` (archaic method of VNOP)
 */

#include "vabs.h"
#include "vadd.h"
#include "vaddc.h"
#include "vand.h"
#include "vch.h"
#include "vcl.h"
#include "vcr.h"
#include "veq.h"
#include "vge.h"
#include "vlt.h"
#include "vmacf.h"
#include "vmacq.h"
#include "vmacu.h"
#include "vmadh.h"
#include "vmadl.h"
#include "vmadm.h"
#include "vmadn.h"
#include "vmov.h"
#include "vmrg.h"
#include "vmudh.h"
#include "vmudl.h"
#include "vmudm.h"
#include "vmudn.h"
#include "vmulf.h"
#include "vmulu.h"
#include "vnand.h"
#include "vne.h"
#include "vnop.h"
#include "vnor.h"
#include "vnxor.h"
#include "vor.h"
#include "vrcp.h"
#include "vrcph.h"
#include "vrcpl.h"
#include "vrsq.h"
#include "vrsqh.h"
#include "vrsql.h"
#include "vsaw.h"
#include "vsub.h"
#include "vsubc.h"
#include "vxor.h"

static void (*SP_COP2_C2[64])(int, int, int, int) = {
    VMULF  ,VMULU  ,res_M  ,res_M  ,VMUDL  ,VMUDM  ,VMUDN  ,VMUDH  , /* 000 */
    VMACF  ,VMACU  ,res_M  ,VMACQ  ,VMADL  ,VMADM  ,VMADN  ,VMADH  , /* 001 */
    VADD   ,VSUB   ,res_V  ,VABS   ,VADDC  ,VSUBC  ,res_V  ,res_V  , /* 010 */
    res_V  ,res_V  ,res_V  ,res_V  ,res_V  ,VSAW   ,res_V  ,res_V  , /* 011 */
    VLT    ,VEQ    ,VNE    ,VGE    ,VCL    ,VCH    ,VCR    ,VMRG   , /* 100 */
    VAND   ,VNAND  ,VOR    ,VNOR   ,VXOR   ,VNXOR  ,res_V  ,res_V  , /* 101 */
    VRCP   ,VRCPL  ,VRCPH  ,VMOV   ,VRSQ   ,VRSQL  ,VRSQH  ,VNOP   , /* 110 */
    res_V  ,res_V  ,res_V  ,res_V  ,res_V  ,res_V  ,res_V  ,res_V    /* 111 */
}; /* 000     001     010     011     100     101     110     111 */

/* Some notes about the vector operation codes matrix.
 * VMULQ, VRNDN, and VRNDP did exist but were all for MPEG DCT and omitted.
 * VMACQ ignores `vs` and `vt` and only oddifies, but no games would do that.
 * VSAR operation was perhaps modified, so it is instead called VSAW.
 * VNXOR in other VUs is often called "VXNOR", but not here.
 * VRSQ is the single-precision FP for square roots, not favored by Nintendo.
 * "VNOOP" is a typo of "VNOP", the latter being correct for assemblers.
 */
#endif
