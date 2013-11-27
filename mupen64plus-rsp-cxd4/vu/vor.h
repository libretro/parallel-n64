#include "vu.h"

INLINE void do_or(short* VD, short* VS, short* VT)
{
    register int i;

    for (i = 0; i < N; i++)
        VACC_L[i] = VS[i] | VT[i];
    vector_copy(VD, VACC_L);
    return;
}

static void VOR(void)
{
    short ST[N];
    const int vd = (inst.W >> 6) & 31;
    const int vs = inst.R.rd;
    const int vt = inst.R.rt;

    SHUFFLE_VECTOR(ST, VR[vt], inst.R.rs & 0xF);
    do_or(VR[vd], VR[vs], ST);
    return;
}
