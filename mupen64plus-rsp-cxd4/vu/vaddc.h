#include "vu.h"

INLINE static void set_co(short* VD, short* VS, short* VT)
{ /* set CARRY and carry out from sum */
    int32_t sum[N];
    register int i;

    for (i = 0; i < N; i++)
        sum[i] = (unsigned short)(VS[i]) + (unsigned short)(VT[i]);
    for (i = 0; i < N; i++)
        VACC_L[i] = VS[i] + VT[i];
    vector_copy(VD, VACC_L);
    for (i = 0; i < N; i++)
        ne[i] = 0;
    for (i = 0; i < N; i++)
        co[i] = sum[i] >> 16; /* native:  (sum[i] > +65535) */
    return;
}

static void VADDC(int vd, int vs, int vt, int e)
{
    short ST[N];

    SHUFFLE_VECTOR(ST, VR[vt], e);
    set_co(VR[vd], VR[vs], ST);
    return;
}
