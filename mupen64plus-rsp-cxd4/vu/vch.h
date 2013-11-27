#include "vu.h"

INLINE static void do_ch(short* VD, short* VS, short* VT)
{
    short eq[N], ge[N], le[N];
    short sn[N];
    short VC[N];
    short diff[N];
    register int i;

    for (i = 0; i < N; i++)
        VC[i] = VT[i];
    for (i = 0; i < N; i++)
        sn[i] = (VS[i] ^ VC[i]) < 0;
    for (i = 0; i < N; i++)
        VC[i] ^= -sn[i]; /* if (sn == ~0) {VT = ~VT;} else {VT =  VT;} */
    for (i = 0; i < N; i++)
        vce[i]  = (VS[i] == VC[i]); /* (VR[vs][i] + ~VC[i] == ~1); */
    for (i = 0; i < N; i++)
        vce[i] &= sn[i];
    for (i = 0; i < N; i++)
        VC[i] += sn[i]; /* converts ~(VT) into -(VT) if (sign) */
    for (i = 0; i < N; i++)
        eq[i]  = (VS[i] == VC[i]);
    for (i = 0; i < N; i++)
        eq[i] |= vce[i];

#if (0)
    for (i = 0; i < N; i++)
        le[i] = sn[i] ? (VS[i] <= VC[i]) : (VC[i] < 0);
    for (i = 0; i < N; i++)
        ge[i] = sn[i] ? (VC[i] > 0x0000) : (VS[i] >= VC[i]);
#elif (0)
    for (i = 0; i < N; i++)
        le[i] = sn[i] ? (VT[i] <= -VS[i]) : (VT[i] <= ~0x0000);
    for (i = 0; i < N; i++)
        ge[i] = sn[i] ? (~0x0000 >= VT[i]) : (VS[i] >= VT[i]);
#else
    for (i = 0; i < N; i++)
        diff[i] = -VS[i] | -(sn[i] ^ 1);
    for (i = 0; i < N; i++)
        le[i] = VT[i] <= diff[i];
    for (i = 0; i < N; i++)
        diff[i] = +VS[i] | -(sn[i] ^ 0);
    for (i = 0; i < N; i++)
        ge[i] = diff[i] >= VT[i];
#endif

    merge(comp, sn, le, ge);
    merge(VACC_L, comp, VC, VS);
    vector_copy(VD, VACC_L);

    for (i = 0; i < N; i++)
        clip[i] = ge[i];
    for (i = 0; i < N; i++)
        comp[i] = le[i];
    for (i = 0; i < N; i++)
        ne[i] = eq[i] ^ 1;
    for (i = 0; i < N; i++)
        co[i] = sn[i];
    return;
}

static void VCH(void)
{
    short ST[N];
    const int vd = (inst.W >> 6) & 31;
    const int vs = inst.R.rd;
    const int vt = inst.R.rt;

    SHUFFLE_VECTOR(ST, VR[vt], inst.R.rs & 0xF);
    do_ch(VR[vd], VR[vs], ST);
    return;
}
