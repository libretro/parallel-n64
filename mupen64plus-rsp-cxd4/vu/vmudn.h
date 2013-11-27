#include "vu.h"

INLINE static void do_mudn(short* VD, short* VS, short* VT)
{
    register int i;

    for (i = 0; i < N; i++)
        VACC_L[i] = ((unsigned short)(VS[i])*VT[i] & 0x00000000FFFF) >>  0;
    for (i = 0; i < N; i++)
        VACC_M[i] = ((unsigned short)(VS[i])*VT[i] & 0x0000FFFF0000) >> 16;
    for (i = 0; i < N; i++)
        VACC_H[i] = -(VACC_M[i] < 0);
    vector_copy(VD, VACC_L); /* no possibilities to clamp */
    return;
}

static void VMUDN(void)
{
    short ST[N];
    const int vd = (inst.W >> 6) & 31;
    const int vs = inst.R.rd;
    const int vt = inst.R.rt;

    SHUFFLE_VECTOR(ST, VR[vt], inst.R.rs & 0xF);
    do_mudn(VR[vd], VR[vs], ST);
    return;
}
