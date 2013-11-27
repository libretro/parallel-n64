#include "vu.h"

static void VMOV(int vd, int de, int vt, int e)
{
    SHUFFLE_VECTOR(VACC_L, VR[vt], e);
    VR[vd][de &= 07] = VACC_L[e & 07];
    return;
}
