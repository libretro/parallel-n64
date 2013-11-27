#include "vu.h"
#include "divrom.h"

static void VRCPH(int vd, int de, int vt, int e)
{
    DivIn = VR[vt][e & 07] << 16;
    SHUFFLE_VECTOR(VACC_L, VR[vt], e);
    VR[vd][de &= 07] = DivOut >> 16;
    DPH = SP_DIV_PRECISION_DOUBLE;
    return;
}
