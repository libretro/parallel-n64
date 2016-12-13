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

INLINE static void do_madm(short* VD, short* VS, short* VT)
{
#ifdef ARCH_MIN_SSE2
    __m128i acc_hi, acc_md, acc_lo;
    __m128i prod_hi, prod_lo;
    __m128i overflow;
    __m128i vs, vt;

    vs = _mm_loadu_si128((__m128i *)VS);
    vt = _mm_loadu_si128((__m128i *)VT);

    prod_lo = _mm_mullo_epi16(vs, vt);
    prod_hi = _mm_mulhi_epu16(vs, vt);

    vs = _mm_srai_epi16(vs, 15);
    vt = _mm_and_si128(vt, vs);
    prod_hi = _mm_sub_epi16(prod_hi, vt);

/*
 * Writeback phase to the accumulator.
 * VMADM stores accumulator += the product achieved by VMUDM.
 */
    acc_lo = _mm_loadu_si128((__m128i *)VACC_L);
    acc_md = _mm_loadu_si128((__m128i *)VACC_M);
    acc_hi = _mm_loadu_si128((__m128i *)VACC_H);

    acc_lo = _mm_add_epi16(acc_lo, prod_lo);
    _mm_storeu_si128((__m128i *)VACC_L, acc_lo);

    overflow = _mm_cmplt_epu16(acc_lo, prod_lo); /* overflow:  (x + y < y) */
    prod_hi = _mm_sub_epi16(prod_hi, overflow);
    acc_md = _mm_add_epi16(acc_md, prod_hi);
    _mm_storeu_si128((__m128i *)VACC_M, acc_md);

    overflow = _mm_cmplt_epu16(acc_md, prod_hi);
    prod_hi = _mm_srai_epi16(prod_hi, 15);
    acc_hi = _mm_add_epi16(acc_hi, prod_hi);
    acc_hi = _mm_sub_epi16(acc_hi, overflow);
    _mm_storeu_si128((__m128i *)VACC_H, acc_hi);

    vt = _mm_unpackhi_epi16(acc_md, acc_hi);
    vs = _mm_unpacklo_epi16(acc_md, acc_hi);
    vs = _mm_packs_epi32(vs, vt);

    _mm_storeu_si128((__m128i *)VD, vs);
#else
    uint32_t addend[N];
    register int i;

    for (i = 0; i < N; i++)
        addend[i] = (unsigned short)(VACC_L[i]) + (unsigned short)(VS[i]*VT[i]);
    for (i = 0; i < N; i++)
        VACC_L[i] += (short)(VS[i] * VT[i]);
    for (i = 0; i < N; i++)
        addend[i] = (addend[i] >> 16) + (VS[i]*(unsigned short)(VT[i]) >> 16);
    for (i = 0; i < N; i++)
        addend[i] = (unsigned short)(VACC_M[i]) + addend[i];
    for (i = 0; i < N; i++)
        VACC_M[i] = (short)addend[i];
    for (i = 0; i < N; i++)
        VACC_H[i] += addend[i] >> 16;
    SIGNED_CLAMP_AM(VD);
#endif
    return;
}

static void VMADM(int vd, int vs, int vt, int e)
{
    short ST[N];

    SHUFFLE_VECTOR(ST, VR[vt], e);
    do_madm(VR[vd], VR[vs], ST);
    return;
}
