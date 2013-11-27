#include "vu.h"

INLINE static void set_bo(short* VD, short* VS, short* VT)
{ /* set CARRY and borrow out from difference */
    long dif[N];
    register int i;

    for (i = 0; i < N; i++)
        dif[i] = (unsigned short)(VS[i]) - (unsigned short)(VT[i]);
    for (i = 0; i < N; i++)
        VACC_L[i] = VS[i] - VT[i];
    vector_copy(VD, VACC_L);
    for (i = 0; i < N; i++)
        ne[i] = (VS[i] != VT[i]);
    for (i = 0; i < N; i++)
        co[i] = (dif[i] < 0);
    return;
}

static void VSUBC(void)
{
    short ST[N];
    const int vd = (inst.W >> 6) & 31;
    const int vs = inst.R.rd;
    const int vt = inst.R.rt;

    SHUFFLE_VECTOR(ST, VR[vt], inst.R.rs & 0xF);
    set_bo(VR[vd], VR[vs], ST);
    return;
}
