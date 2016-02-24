/******************************************************************************\
* Authors:  Iconoclast                                                         *
* Release:  2013.11.26                                                         *
* License:  CC0 Public Domain Dedication                                       *
*                                                                              *
* To the extent possible under law, the author(s) have dedicated all copyright *
* and related and neighboring rights to this software to the public domain     *
* worldwide. This software is distributed without any warranty.                *
*                                                                              *
* You should have received a copy of the CC0 Public Domain Dedication along    *
* with this software.                                                          *
* If not, see <http://creativecommons.org/publicdomain/zero/1.0/>.             *
\******************************************************************************/
#include "vu.h"

INLINE static void do_mudl(short* VD, short* VS, short* VT)
{
#ifdef ARCH_MIN_SSE2
    __m128i vs, vt;

    vs = _mm_loadu_si128((__m128i *)VS);
    vt = _mm_loadu_si128((__m128i *)VT);

/*
 * 2015.07.27 --cxd4
 * This next instruction is virtually identical to the whole VMUDL operation.
 *     temp[i]31..0   = vs[i]15..0 * vt[i]15..0
 *     result[i]15..0 = temp[i]31..16
 */
    vs = _mm_mulhi_epu16(vs, vt);

    _mm_storeu_si128((__m128i *)VD, vs);

    _mm_storeu_si128((__m128i *)VACC_L, vs);
    vt = _mm_xor_si128(vt, vt); /* (u16 * u16) >> 16 is too small for MD/HI. */
    _mm_storeu_si128((__m128i *)VACC_M, vt);
    _mm_storeu_si128((__m128i *)VACC_H, vt);
#else
    register int i;

    for (i = 0; i < N; i++)
        VACC_L[i] = (unsigned short)(VS[i])*(unsigned short)(VT[i]) >> 16;
    for (i = 0; i < N; i++)
        VACC_M[i] = 0x0000;
    for (i = 0; i < N; i++)
        VACC_H[i] = 0x0000;
    vector_copy(VD, VACC_L); /* no possibilities to clamp */
#endif
    return;
}

static void VMUDL(int vd, int vs, int vt, int e)
{
    short ST[N];

    SHUFFLE_VECTOR(ST, VR[vt], e);
    do_mudl(VR[vd], VR[vs], ST);
    return;
}
