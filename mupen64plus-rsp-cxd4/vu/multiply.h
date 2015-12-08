/******************************************************************************\
* Authors:  Iconoclast                                                         *
* Release:  2013.11.26                                                         *
* License:  CC0 Public Domain Dedication                                       *
*                                                                              *
* To the extent possible under law, the author(s) have dedicated all copyright *
* and related and neighboring rights to this software to the public domain     *
* worldwide. This software is distributed without any warranty.                *
*                                                                              *
* You should have received a copy of the CC0 Public Domain Dedication along    *
* with this software.                                                          *
* If not, see <http://creativecommons.org/publicdomain/zero/1.0/>.             *
\******************************************************************************/
#include "vu.h"

INLINE static void do_macf(short* VD, short* VS, short* VT)
{
    int32_t product[N];
    uint32_t addend[N];
    register int i;

    for (i = 0; i < N; i++)
        product[i] = VS[i] * VT[i];
    for (i = 0; i < N; i++)
        addend[i] = (product[i] << 1) & 0x00000000FFFF;
    for (i = 0; i < N; i++)
        addend[i] = (unsigned short)(VACC_L[i]) + addend[i];
    for (i = 0; i < N; i++)
        VACC_L[i] = (short)(addend[i]);
    for (i = 0; i < N; i++)
        addend[i] = (addend[i] >> 16) + (unsigned short)(product[i] >> 15);
    for (i = 0; i < N; i++)
        addend[i] = (unsigned short)(VACC_M[i]) + addend[i];
    for (i = 0; i < N; i++)
        VACC_M[i] = (short)(addend[i]);
    for (i = 0; i < N; i++)
        VACC_H[i] -= (product[i] < 0);
    for (i = 0; i < N; i++)
        VACC_H[i] += addend[i] >> 16;
}

#ifndef SEMIFRAC
/*
 * acc = VS * VT;
 * acc = acc + 0x8000; // rounding value
 * acc = acc << 1; // partial value shifting
 *
 * Wrong:  ACC(HI) = -((INT32)(acc) < 0)
 * Right:  ACC(HI) = -(SEMIFRAC < 0)
 */
#define SEMIFRAC    (VS[i]*VT[i]*2/2 + 0x8000/2)
#endif

INLINE static void do_mulf(short* VD, short* VS, short* VT)
{
    register int i;

    for (i = 0; i < N; i++)
        VACC_L[i] = (SEMIFRAC << 1) >>  0;
    for (i = 0; i < N; i++)
        VACC_M[i] = (SEMIFRAC << 1) >> 16;
    for (i = 0; i < N; i++)
        VACC_H[i] = -((VACC_M[i] < 0) & (VS[i] != VT[i])); /* -32768 * -32768 */
#ifndef ARCH_MIN_SSE2
    vector_copy(VD, VACC_M);
    for (i = 0; i < N; i++)
        VD[i] -= (VACC_M[i] < 0) & (VS[i] == VT[i]); /* ACC b 31 set, min*min */
#else
    SIGNED_CLAMP_AM(VD);
#endif
}

static INLINE void do_mulu(short* VD, short* VS, short* VT)
{
   register int i;

   for (i = 0; i < N; i++)
      VACC_L[i] = (SEMIFRAC << 1) >>  0;
   for (i = 0; i < N; i++)
      VACC_M[i] = (SEMIFRAC << 1) >> 16;
   for (i = 0; i < N; i++)
      VACC_H[i] = -((VACC_M[i] < 0) & (VS[i] != VT[i])); /* -32768 * -32768 */
#if (0)
   UNSIGNED_CLAMP(VD);
#else
   vector_copy(VD, VACC_M);
   for (i = 0; i < N; i++)
      VD[i] |=  (VACC_M[i] >> 15); /* VD |= -(result == 0x000080008000) */
   for (i = 0; i < N; i++)
      VD[i] &= ~(VACC_H[i] >>  0); /* VD &= -(result >= 0x000000000000) */
#endif
}

static INLINE void VMULU(int vd, int vs, int vt, int e)
{
   short ST[N];

   SHUFFLE_VECTOR(ST, VR[vt], e);
   do_mulu(VR[vd], VR[vs], ST);
}

static void VMULF(int vd, int vs, int vt, int e)
{
    short ST[N];

    SHUFFLE_VECTOR(ST, VR[vt], e);
    do_mulf(VR[vd], VR[vs], ST);
}

static void VMACF(int vd, int vs, int vt, int e)
{
   short ST[N];

   SHUFFLE_VECTOR(ST, VR[vt], e);
   do_macf(VR[vd], VR[vs], ST);
   SIGNED_CLAMP_AM(VR[vd]);
}

static void VMACU(int vd, int vs, int vt, int e)
{
    short ST[N];

    SHUFFLE_VECTOR(ST, VR[vt], e);
    do_macf(VR[vd], VR[vs], ST);
    UNSIGNED_CLAMP(VR[vd]);
}
