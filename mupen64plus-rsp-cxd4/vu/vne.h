#include "vu.h"

INLINE static void do_ne(short* VD, short* VS, short* VT)
{
    register int i;

    for (i = 0; i < N; i++)
        clip[i] = 0;
    for (i = 0; i < N; i++)
        comp[i] = (VS[i] != VT[i]);
    for (i = 0; i < N; i++)
        comp[i] = comp[i] | ne[i];
#if (0)
    merge(VACC_L, comp, VS, VT); /* correct but redundant */
#else
    vector_copy(VACC_L, VS);
#endif
    vector_copy(VD, VACC_L);

    for (i = 0; i < N; i++)
        ne[i] = 0;
    for (i = 0; i < N; i++)
        co[i] = 0;
    return;
}

static void VNE(void)
{
    short ST[N];
    const int vd = (inst.W >> 6) & 31;
    const int vs = inst.R.rd;
    const int vt = inst.R.rt;

    SHUFFLE_VECTOR(ST, VR[vt], inst.R.rs & 0xF);
    do_ne(VR[vd], VR[vs], ST);
    return;
}
