#include "vu.h"

INLINE static void do_macu(short* VD, short* VS, short* VT)
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
    UNSIGNED_CLAMP(VD);
    return;
}

static void VMACU(int vd, int vs, int vt, int e)
{
    short ST[N];

    SHUFFLE_VECTOR(ST, VR[vt], e);
    do_macu(VR[vd], VR[vs], ST);
    return;
}
