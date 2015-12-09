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
   short diff[N];

   for (i = 0; i < N; i++)
      diff[i] = pass[i] - fail[i];
   for (i = 0; i < N; i++)
      VD[i] = fail[i] + cmp[i]*diff[i]; /* actually `(cmp[i] != 0)*diff[i]` */
}

extern short co[N];

#ifdef ARCH_MIN_SSE2
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
#else
static INLINE void vector_copy(short* VD, short* VS)
{
   register int i;

   for (i = 0; i < N; i++)
      VD[i] = VS[i];
}
#endif

#endif
