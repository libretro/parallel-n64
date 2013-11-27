#include "vu.h"

INLINE static void clr_ci(short* VD, short* VS, short* VT)
{ /* clear CARRY and carry in to accumulators */
    register int i;

    for (i = 0; i < N; i++)
        VACC_L[i] = VS[i] + VT[i] + co[i];
    SIGNED_CLAMP_ADD(VD, VS, VT);
    for (i = 0; i < N; i++)
        ne[i] = 0;
    for (i = 0; i < N; i++)
        co[i] = 0;
    return;
}

static void VADD(void)
{
    short ST[N];
    const int vd = (inst.W >> 6) & 31;
    const int vs = inst.R.rd;
    const int vt = inst.R.rt;

    SHUFFLE_VECTOR(ST, VR[vt], inst.R.rs & 0xF);
    clr_ci(VR[vd], VR[vs], ST);
    return;
}
