#include "vu.h"

INLINE static void do_lt(short* VD, short* VS, short* VT)
{
    short cn[N];
    short eq[N];
    register int i;

    for (i = 0; i < N; i++)
        eq[i] = (VS[i] == VT[i]);
    for (i = 0; i < N; i++)
        cn[i] = ne[i] & co[i];
    for (i = 0; i < N; i++)
        eq[i] = eq[i] & cn[i];
    for (i = 0; i < N; i++)
        clip[i] = 0;
    for (i = 0; i < N; i++)
        comp[i] = (VS[i] < VT[i]); /* less than */
    for (i = 0; i < N; i++)
        comp[i] = comp[i] | eq[i]; /* ... or equal (uncommonly) */

    merge(VACC_L, comp, VS, VT);
    vector_copy(VD, VACC_L);
    for (i = 0; i < N; i++)
        ne[i] = 0;
    for (i = 0; i < N; i++)
        co[i] = 0;
    return;
}

static void VLT(void)
{
    short ST[N];
    const int vd = (inst.W >> 6) & 31;
    const int vs = inst.R.rd;
    const int vt = inst.R.rt;

    SHUFFLE_VECTOR(ST, VR[vt], inst.R.rs & 0xF);
    do_lt(VR[vd], VR[vs], ST);
    return;
}
