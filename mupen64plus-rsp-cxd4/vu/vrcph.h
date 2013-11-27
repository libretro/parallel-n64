#include "vu.h"
#include "divrom.h"

static void VRCPH(void)
{
    const int vd = (inst.W >> 6) & 31;
    const int de = inst.R.rd & 07;
    const int vt = inst.R.rt;

    DivIn = VR[vt][inst.R.rs & 07] << 16;
    SHUFFLE_VECTOR(VACC_L, VR[vt], inst.R.rs & 0xF);
    VR[vd][de] = DivOut >> 16;
    DPH = 1;
    return;
}
