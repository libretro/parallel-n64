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

INLINE static void do_madn(short* VD, short* VS, short* VT)
{
#ifdef INTENSE_DEBUG
   unsigned i;
  for (i = 0; i < 8; i++)
     fprintf(stderr, "ACC LO[%u] = %d\n", i, VACC_L[i]);
  for (i = 0; i < 8; i++)
     fprintf(stderr, "ACC MD[%u] = %d\n", i, VACC_M[i]);
  for (i = 0; i < 8; i++)
     fprintf(stderr, "ACC HI[%u] = %d\n", i, VACC_H[i]);
  for (i = 0; i < 8; i++)
     fprintf(stderr, "VS[%u] = %d\n", i, VS[i]);
  for (i = 0; i < 8; i++)
     fprintf(stderr, "VT[%u] = %d\n", i, VT[i]);
#endif

#ifdef ARCH_MIN_SSE2
    __m128i acc_hi, acc_md, acc_lo;
    __m128i prod_hi, prod_lo;
    __m128i overflow;
    __m128i vs, vt;

    vs = _mm_loadu_si128((__m128i *)VS);
    vt = _mm_loadu_si128((__m128i *)VT);

    prod_lo = _mm_mullo_epi16(vs, vt);
    prod_hi = _mm_mulhi_epu16(vs, vt);

    vt = _mm_srai_epi16(vt, 15);
    vs = _mm_and_si128(vs, vt);
    prod_hi = _mm_sub_epi16(prod_hi, vs);

/*
 * Writeback phase to the accumulator.
 * VMADN stores accumulator += the product achieved by VMUDN.
 */
    acc_lo = _mm_loadu_si128((__m128i *)VACC_L);
    acc_md = _mm_loadu_si128((__m128i *)VACC_M);
    acc_hi = _mm_loadu_si128((__m128i *)VACC_H);

    acc_lo = _mm_add_epi16(acc_lo, prod_lo);
    _mm_storeu_si128((__m128i *)VACC_L, acc_lo);

    overflow = _mm_cmplt_epu16(acc_lo, prod_lo); /* overflow: (x + y < y) */
    prod_hi = _mm_sub_epi16(prod_hi, overflow);
    acc_md = _mm_add_epi16(acc_md, prod_hi);
    _mm_storeu_si128((__m128i *)VACC_M, acc_md);

    overflow = _mm_cmplt_epu16(acc_md, prod_hi);
    prod_hi = _mm_srai_epi16(prod_hi, 15);
    acc_hi = _mm_add_epi16(acc_hi, prod_hi);
    acc_hi = _mm_sub_epi16(acc_hi, overflow);
    _mm_storeu_si128((__m128i *)VACC_H, acc_hi);

/*
 * Do a signed clamp...sort of (VM?DM, VM?DH: middle; VM?DL, VM?DN: low).
 *     if (acc_47..16 < -32768) result = -32768 ^ 0x8000; # 0000
 *     else if (acc_47..16 > +32767) result = +32767 ^ 0x8000; # FFFF
 *     else { result = acc_15..0 & 0xFFFF; }
 * So it is based on the standard signed clamping logic for VM?DM, VM?DH,
 * except that extra steps must be concatenated to that definition.
 */
    vt = _mm_unpackhi_epi16(acc_md, acc_hi);
    vs = _mm_unpacklo_epi16(acc_md, acc_hi);
    vs = _mm_packs_epi32(vs, vt);

    acc_md = _mm_cmpeq_epi16(acc_md, vs); /* (unclamped == clamped) ... */
    acc_lo = _mm_and_si128(acc_lo, acc_md); /* ... ? low : mid */
    vt = _mm_cmpeq_epi16(vt, vt);
    acc_md = _mm_xor_si128(acc_md, vt); /* (unclamped != clamped) ... */

    vs = _mm_and_si128(vs, acc_md); /* ... ? VS_clamped : 0x0000 */
    vs = _mm_or_si128(vs, acc_lo); /* : acc_lo */
    acc_md = _mm_slli_epi16(acc_md, 15); /* ... ? ^ 0x8000 : ^ 0x0000 */
    vs = _mm_xor_si128(vs, acc_md); /* Stupid unsigned-clamp-ish adjustment. */

    _mm_storeu_si128((__m128i *)VD, vs);
#ifdef INTENSE_DEBUG
    for (i = 0; i < 8; i++)
       fprintf(stderr, "VD[%u] = %d\n", i, VD[i]);
#endif
#else
    uint32_t addend[N];
    register int i;

    for (i = 0; i < N; i++)
        addend[i] = (unsigned short)(VACC_L[i]) + (unsigned short)(VS[i]*VT[i]);
    for (i = 0; i < N; i++)
        VACC_L[i] += (short)(VS[i] * VT[i]);
    for (i = 0; i < N; i++)
        addend[i] = (addend[i] >> 16) + ((unsigned short)(VS[i])*VT[i] >> 16);
    for (i = 0; i < N; i++)
        addend[i] = (unsigned short)(VACC_M[i]) + addend[i];
    for (i = 0; i < N; i++)
        VACC_M[i] = (short)addend[i];
    for (i = 0; i < N; i++)
        VACC_H[i] += addend[i] >> 16;
    SIGNED_CLAMP_AL(VD);
#endif
    return;
}

static void VMADN(int vd, int vs, int vt, int e)
{
    short ST[N];

    SHUFFLE_VECTOR(ST, VR[vt], e);
    do_madn(VR[vd], VR[vs], ST);
    return;
}
