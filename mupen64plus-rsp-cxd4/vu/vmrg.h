#include "vu.h"

INLINE static void do_mrg(short* VD, short* VS, short* VT)
{
    merge(VACC_L, comp, VS, VT);
    vector_copy(VD, VACC_L);
    return;
}

static void VMRG(void)
{
    short ST[N];
    const int vd = (inst.W >> 6) & 31;
    const int vs = inst.R.rd;
    const int vt = inst.R.rt;

    SHUFFLE_VECTOR(ST, VR[vt], inst.R.rs & 0xF);
    do_mrg(VR[vd], VR[vs], ST);
    return;
}
