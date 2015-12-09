/******************************************************************************\
* Authors:  Iconoclast                                                         *
* Release:  2013.10.07                                                         *
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
#ifndef _CLAMP_H
#define _CLAMP_H

/*
 * dependency for 48-bit accumulator access
 */
#include "vu.h"

/*
 * vector select merge (`VMRG`) formula
 *
 * This is really just a vectorizer for ternary conditional storage.
 * I've named it so because it directly maps to the VMRG op-code.
 * -- example --
 * for (i = 0; i < N; i++)
 *     if (c_pass)
 *         dest = element_a;
 *     else
 *         dest = element_b;
 */
static INLINE void merge(short* VD, short* cmp, short* pass, short* fail)
{
   register int i;
#if (0)
   /* Do not use this version yet, as it still does not vectorize to SSE2. */
   for (i = 0; i < N; i++)
      VD[i] = (cmp[i] != 0) ? pass[i] : fail[i];
#else
   short diff[N];

   for (i = 0; i < N; i++)
      diff[i] = pass[i] - fail[i];
   for (i = 0; i < N; i++)
      VD[i] = fail[i] + cmp[i]*diff[i]; /* actually `(cmp[i] != 0)*diff[i]` */
#endif
}
extern short co[N];

#ifndef ARCH_MIN_SSE2
static INLINE void vector_copy(short* VD, short* VS)
{
#if (0)
    memcpy(VD, VS, N*sizeof(short));
#else
    register int i;

    for (i = 0; i < N; i++)
        VD[i] = VS[i];
#endif
    return;
}

static INLINE void SIGNED_CLAMP_AM(short* VD)
{
   /* typical sign-clamp of accumulator-mid (bits 31:16) */
   short hi[N], lo[N];
   register int i;

   for (i = 0; i < N; i++)
      lo[i]  = (VACC_H[i] < ~0);
   for (i = 0; i < N; i++)
      lo[i] |= (VACC_H[i] < 0) & !(VACC_M[i] < 0);
   for (i = 0; i < N; i++)
      hi[i]  = (VACC_H[i] >  0);
   for (i = 0; i < N; i++)
      hi[i] |= (VACC_H[i] == 0) & (VACC_M[i] < 0);
   vector_copy(VD, VACC_M);
   for (i = 0; i < N; i++)
      VD[i] &= -(lo[i] ^ 1);
   for (i = 0; i < N; i++)
      VD[i] |= -(hi[i] ^ 0);
   for (i = 0; i < N; i++)
      VD[i] ^= 0x8000 * (hi[i] | lo[i]);
}
#else
/*
 * We actually need to write explicit SSE2 code for this because GCC 4.8.1
 * (and possibly later versions) has a code generation bug with vectorizing
 * the accumulator when it's a signed short (but not when it's unsigned, for
 * some stupid and buggy reason).
 *
 * In addition, as of the more stable GCC 4.7.2 release, while vectorizing
 * the accumulator write-backs into SSE2 for me is successfully done, we save
 * just one extra scalar x86 instruction for every RSP vector op-code when we
 * use SSE2 explicitly for this particular goal instead of letting GCC do it.
 */
static INLINE void vector_copy(short* VD, short* VS)
{
    __m128i xmm = _mm_load_si128((__m128i *)VS);
    _mm_store_si128((__m128i *)VD, xmm);
}

static INLINE void SIGNED_CLAMP_AM(short* VD)
{
   /* typical sign-clamp of accumulator-mid (bits 31:16) */
   __m128i pvs = _mm_load_si128((__m128i *)VACC_H);
   __m128i pvd = _mm_load_si128((__m128i *)VACC_M);
   __m128i dst = _mm_unpacklo_epi16(pvd, pvs);
   __m128i src = _mm_unpackhi_epi16(pvd, pvs);

   dst = _mm_packs_epi32(dst, src);
   _mm_store_si128((__m128i *)VD, dst);
}
#endif

static INLINE void UNSIGNED_CLAMP(short* VD)
{ /* sign-zero hybrid clamp of accumulator-mid (bits 31:16) */
    short cond[N];
    short temp[N];
    register int i;

    SIGNED_CLAMP_AM(temp); /* no direct map in SSE, but closely based on this */
    for (i = 0; i < N; i++)
        cond[i] = -(temp[i] >  VACC_M[i]); /* VD |= -(ACC47..16 > +32767) */
    for (i = 0; i < N; i++)
        VD[i] = temp[i] & ~(temp[i] >> 15); /* Only this clamp is unsigned. */
    for (i = 0; i < N; i++)
        VD[i] = VD[i] | cond[i];
    return;
}
static INLINE void SIGNED_CLAMP_AL(short* VD)
{ /* sign-clamp accumulator-low (bits 15:0) */
    short cond[N];
    short temp[N];
    register int i;

    SIGNED_CLAMP_AM(temp); /* no direct map in SSE, but closely based on this */
    for (i = 0; i < N; i++)
        cond[i] = (temp[i] != VACC_M[i]); /* result_clamped != result_raw ? */
    for (i = 0; i < N; i++)
        temp[i] ^= 0x8000; /* half-assed unsigned saturation mix in the clamp */
    merge(VD, cond, temp, VACC_L);
    return;
}
#endif
