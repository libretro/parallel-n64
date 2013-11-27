#include "vu.h"

INLINE static void do_mrg(short* VD, short* VS, short* VT)
{
    merge(VACC_L, comp, VS, VT);
    vector_copy(VD, VACC_L);
    return;
}

static void VMRG(int vd, int vs, int vt, int e)
{
    short ST[N];

    SHUFFLE_VECTOR(ST, VR[vt], e);
    do_mrg(VR[vd], VR[vs], ST);
    return;
}
