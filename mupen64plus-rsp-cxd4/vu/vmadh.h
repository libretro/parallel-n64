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

INLINE static void do_madh(short* VD, short* VS, short* VT)
{
#ifdef ARCH_MIN_SSE2
    __m128i acc_mid;
    __m128i prod_high;
    __m128i vs, vt;

    vs = _mm_loadu_si128((__m128i *)VS);
    vt = _mm_loadu_si128((__m128i *)VT);

    prod_high = _mm_mulhi_epi16(vs, vt);
    vs        = _mm_mullo_epi16(vs, vt);

/*
 * We're required to load the source product from the accumulator to add to.
 * While we're at it, conveniently sneak in a acc[31..16] += (vs*vt)[15..0].
 */
    acc_mid = _mm_loadu_si128((__m128i *)VACC_M);
    vs = _mm_add_epi16(vs, acc_mid);
    _mm_storeu_si128((__m128i *)VACC_M, vs);
    vt = _mm_loadu_si128((__m128i *)VACC_H);

/*
 * While accumulating base_lo + product_lo is easy, getting the correct data
 * for base_hi + product_hi is tricky and needs unsigned overflow detection.
 *
 * The one-liner solution to detecting unsigned overflow (thus adding a carry
 * value of 1 to the higher word) is _mm_cmplt_epu16, but none of the Intel
 * MMX-based instruction sets define unsigned comparison ops FOR us, so...
 */
    vt = _mm_add_epi16(vt, prod_high);
    vs = _mm_cmplt_epu16(vs, acc_mid); /* acc.mid + prod.low < acc.mid */
    vt = _mm_sub_epi16(vt, vs); /* += 1 if overflow, by doing -= ~0 */
    _mm_storeu_si128((__m128i *)VACC_H, vt);

    vs = _mm_loadu_si128((__m128i *)VACC_M);
    prod_high = _mm_unpackhi_epi16(vs, vt);
    vs        = _mm_unpacklo_epi16(vs, vt);
    vs = _mm_packs_epi32(vs, prod_high);

    _mm_storeu_si128((__m128i *)VD, vs);
#else
    int32_t product[N];
    uint32_t addend[N];
    register int i;

    for (i = 0; i < N; i++)
        product[i] = (signed short)(VS[i]) * (signed short)(VT[i]);
    for (i = 0; i < N; i++)
        addend[i] = (unsigned short)(VACC_M[i]) + (unsigned short)(product[i]);
    for (i = 0; i < N; i++)
        VACC_M[i] += (short)(VS[i] * VT[i]);
    for (i = 0; i < N; i++)
        VACC_H[i] += (addend[i] >> 16) + (product[i] >> 16);
    SIGNED_CLAMP_AM(VD);
#endif
    return;
}

static void VMADH(int vd, int vs, int vt, int e)
{
    short ST[N];

    SHUFFLE_VECTOR(ST, VR[vt], e);
    do_madh(VR[vd], VR[vs], ST);
    return;
}
