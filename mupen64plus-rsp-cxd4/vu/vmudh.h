#include "vu.h"

INLINE static void do_mudh(short* VD, short* VS, short* VT)
{
    register int i;

    for (i = 0; i < N; i++)
        VACC_L[i] = 0x0000;
    for (i = 0; i < N; i++)
        VACC_M[i] = (short)(VS[i]*VT[i] >>  0);
    for (i = 0; i < N; i++)
        VACC_H[i] = (short)(VS[i]*VT[i] >> 16);
    SIGNED_CLAMP_AM(VD);
    return;
}

static void VMUDH(int vd, int vs, int vt, int e)
{
    short ST[N];

    SHUFFLE_VECTOR(ST, VR[vt], e);
    do_mudh(VR[vd], VR[vs], ST);
    return;
}
