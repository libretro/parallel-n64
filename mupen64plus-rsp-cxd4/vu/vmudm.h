#include "vu.h"

INLINE static void do_mudm(short* VD, short* VS, short* VT)
{
    register int i;

    for (i = 0; i < N; i++)
        VACC_L[i] = (VS[i]*(unsigned short)(VT[i]) & 0x00000000FFFF) >>  0;
    for (i = 0; i < N; i++)
        VACC_M[i] = (VS[i]*(unsigned short)(VT[i]) & 0x0000FFFF0000) >> 16;
    for (i = 0; i < N; i++)
        VACC_H[i] = -(VACC_M[i] < 0);
    vector_copy(VD, VACC_M); /* no possibilities to clamp */
    return;
}

static void VMUDM(int vd, int vs, int vt, int e)
{
    short ST[N];

    SHUFFLE_VECTOR(ST, VR[vt], e);
    do_mudm(VR[vd], VR[vs], ST);
    return;
}
