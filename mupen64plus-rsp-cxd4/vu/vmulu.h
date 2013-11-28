#include "vu.h"

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

INLINE static void do_mulu(short* VD, short* VS, short* VT)
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
    return;
}

static void VMULU(int vd, int vs, int vt, int e)
{
    short ST[N];

    SHUFFLE_VECTOR(ST, VR[vt], e);
    do_mulu(VR[vd], VR[vs], ST);
    return;
}
