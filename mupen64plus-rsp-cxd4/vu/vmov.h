#include "vu.h"

static void VMOV(void)
{
    const int vd = (inst.W >> 6) & 31;
    const int de = inst.R.rd & 07;
    const int vt = inst.R.rt;

    SHUFFLE_VECTOR(VACC_L, VR[vt], inst.R.rs & 0xF);
    VR[vd][de] = VACC_L[inst.R.rs & 07];
    return;
}
