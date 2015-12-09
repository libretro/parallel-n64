/******************************************************************************\
* Authors:  Iconoclast                                                         *
* Release:  2013.12.04                                                         *
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
#ifndef _SHUFFLE_H
#define _SHUFFLE_H

#ifndef ARCH_MIN_SSE2
/*
 * vector-scalar element decoding
 * Obsolete.  Consider using at least the SSE2 algorithms instead.
 */
static const int ei[16][8] = {
    { 00, 01, 02, 03, 04, 05, 06, 07 }, /* none (vector-only operand) */
    { 00, 01, 02, 03, 04, 05, 06, 07 },
    { 00, 00, 02, 02, 04, 04, 06, 06 }, /* 0Q */
    { 01, 01, 03, 03, 05, 05, 07, 07 }, /* 1Q */
    { 00, 00, 00, 00, 04, 04, 04, 04 }, /* 0H */
    { 01, 01, 01, 01, 05, 05, 05, 05 }, /* 1H */
    { 02, 02, 02, 02, 06, 06, 06, 06 }, /* 2H */
    { 03, 03, 03, 03, 07, 07, 07, 07 }, /* 3H */
    { 00, 00, 00, 00, 00, 00, 00, 00 }, /* 0 */
    { 01, 01, 01, 01, 01, 01, 01, 01 }, /* 1 */
    { 02, 02, 02, 02, 02, 02, 02, 02 }, /* 2 */
    { 03, 03, 03, 03, 03, 03, 03, 03 }, /* 3 */
    { 04, 04, 04, 04, 04, 04, 04, 04 }, /* 4 */
    { 05, 05, 05, 05, 05, 05, 05, 05 }, /* 5 */
    { 06, 06, 06, 06, 06, 06, 06, 06 }, /* 6 */
    { 07, 07, 07, 07, 07, 07, 07, 07 }  /* 7 */
};

static int sub_mask[16] = {
    0x0,
    0x0,
    0x1, 0x1,
    0x3, 0x3, 0x3, 0x3,
    0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7
};

static INLINE void SHUFFLE_VECTOR(short* VD, short* VT, const int e)
{
   short SV[8];
   register int i, j;
#if (0 == 0)
   j = sub_mask[e];
   for (i = 0; i < N; i++)
      SV[i] = VT[(i & ~j) | (e & j)];
#else
   if (e & 0x8)
      for (i = 0; i < N; i++)
         SV[i] = VT[(i & 0x0) | (e & 0x7)];
   else if (e & 0x4)
      for (i = 0; i < N; i++)
         SV[i] = VT[(i & 0xC) | (e & 0x3)];
   else if (e & 0x2)
      for (i = 0; i < N; i++)
         SV[i] = VT[(i & 0xE) | (e & 0x1)];
   else /* if ((e == 0b0000) || (e == 0b0001)) */
      for (i = 0; i < N; i++)
         SV[i] = VT[(i & 0x7) | (e & 0x0)];
#endif
   for (i = 0; i < N; i++)
      *(VD + i) = *(SV + i);
}
#else
#ifdef ARCH_MIN_SSSE3
static const unsigned char smask[16][16] = {
    {0x0,0x1,0x2,0x3,0x4,0x5,0x6,0x7,0x8,0x9,0xA,0xB,0xC,0xD,0xE,0xF},
    {0x0,0x1,0x2,0x3,0x4,0x5,0x6,0x7,0x8,0x9,0xA,0xB,0xC,0xD,0xE,0xF},
    {0x0,0x1,0x0,0x1,0x4,0x5,0x4,0x5,0x8,0x9,0x8,0x9,0xC,0xD,0xC,0xD},
    {0x2,0x3,0x2,0x3,0x6,0x7,0x6,0x7,0xA,0xB,0xA,0xB,0xE,0xF,0xE,0xF},
    {0x0,0x1,0x0,0x1,0x0,0x1,0x0,0x1,0x8,0x9,0x8,0x9,0x8,0x9,0x8,0x9},
    {0x2,0x3,0x2,0x3,0x2,0x3,0x2,0x3,0xA,0xB,0xA,0xB,0xA,0xB,0xA,0xB},
    {0x4,0x5,0x4,0x5,0x4,0x5,0x4,0x5,0xC,0xD,0xC,0xD,0xC,0xD,0xC,0xD},
    {0x6,0x7,0x6,0x7,0x6,0x7,0x6,0x7,0xE,0xF,0xE,0xF,0xE,0xF,0xE,0xF},
    {0x0,0x1,0x0,0x1,0x0,0x1,0x0,0x1,0x0,0x1,0x0,0x1,0x0,0x1,0x0,0x1},
    {0x2,0x3,0x2,0x3,0x2,0x3,0x2,0x3,0x2,0x3,0x2,0x3,0x2,0x3,0x2,0x3},
    {0x4,0x5,0x4,0x5,0x4,0x5,0x4,0x5,0x4,0x5,0x4,0x5,0x4,0x5,0x4,0x5},
    {0x6,0x7,0x6,0x7,0x6,0x7,0x6,0x7,0x6,0x7,0x6,0x7,0x6,0x7,0x6,0x7},
    {0x8,0x9,0x8,0x9,0x8,0x9,0x8,0x9,0x8,0x9,0x8,0x9,0x8,0x9,0x8,0x9},
    {0xA,0xB,0xA,0xB,0xA,0xB,0xA,0xB,0xA,0xB,0xA,0xB,0xA,0xB,0xA,0xB},
    {0xC,0xD,0xC,0xD,0xC,0xD,0xC,0xD,0xC,0xD,0xC,0xD,0xC,0xD,0xC,0xD},
    {0xE,0xF,0xE,0xF,0xE,0xF,0xE,0xF,0xE,0xF,0xE,0xF,0xE,0xF,0xE,0xF}
};

static INLINE void SHUFFLE_VECTOR(short* VD, short* VT, const int e)
{
   /* SSSE3 shuffling method was written entirely by CEN64 author MarathonMan. */
   __m128i xmm;
   __m128i key;

   xmm = _mm_load_si128((__m128i *)VT);
   key = _mm_load_si128((__m128i *)(smask[e]));
   xmm = _mm_shuffle_epi8(xmm, key);
   _mm_store_si128((__m128i *)VD, xmm);
}
#else
#define B(x)    ((x) & 3)
/*
 * Here, the SSE2 implementation is mostly written to be readable, not fast.
 *
 * Probably it would be fastest yet to not use SSE instructions to shuffle
 * at this point, but currently I think the algorithm is a bigger concern.
 */
#define SHUFFLE(a,b,c,d)    ((B(d)<<6) | (B(c)<<4) | (B(b)<<2) | (B(a)<<0))

static __m128i shuffle_none(__m128i xmm)
{
 /* xmm = _mm_shufflehi_epi16(xmm, SHUFFLE(0, 1, 2, 3));
    xmm = _mm_shufflelo_epi16(xmm, SHUFFLE(4, 5, 6, 7)); */
    return (xmm);
}

static __m128i shuffle_0q(__m128i xmm)
{
    xmm = _mm_shufflehi_epi16(xmm, SHUFFLE(0, 0, 2, 2));
    xmm = _mm_shufflelo_epi16(xmm, SHUFFLE(4, 4, 6, 6));
    return (xmm);
}
static __m128i shuffle_1q(__m128i xmm)
{
    xmm = _mm_shufflehi_epi16(xmm, SHUFFLE(1, 1, 3, 3));
    xmm = _mm_shufflelo_epi16(xmm, SHUFFLE(5, 5, 7, 7));
    return (xmm);
}
static __m128i shuffle_0h(__m128i xmm)
{
    xmm = _mm_shufflehi_epi16(xmm, SHUFFLE(0, 0, 0, 0));
    xmm = _mm_shufflelo_epi16(xmm, SHUFFLE(4, 4, 4, 4));
    return (xmm);
}
static __m128i shuffle_1h(__m128i xmm)
{
    xmm = _mm_shufflehi_epi16(xmm, SHUFFLE(1, 1, 1, 1));
    xmm = _mm_shufflelo_epi16(xmm, SHUFFLE(5, 5, 5, 5));
    return (xmm);
}
static __m128i shuffle_2h(__m128i xmm)
{
    xmm = _mm_shufflehi_epi16(xmm, SHUFFLE(2, 2, 2, 2));
    xmm = _mm_shufflelo_epi16(xmm, SHUFFLE(6, 6, 6, 6));
    return (xmm);
}
static __m128i shuffle_3h(__m128i xmm)
{
    xmm = _mm_shufflehi_epi16(xmm, SHUFFLE(3, 3, 3, 3));
    xmm = _mm_shufflelo_epi16(xmm, SHUFFLE(7, 7, 7, 7));
    return (xmm);
}
static __m128i shuffle_0w(__m128i xmm)
{
    xmm = _mm_shufflelo_epi16(xmm, SHUFFLE(0, 0, 0, 0));
    xmm = _mm_unpacklo_epi16(xmm, xmm);
    return (xmm);
}
static __m128i shuffle_1w(__m128i xmm)
{
    xmm = _mm_shufflelo_epi16(xmm, SHUFFLE(1, 1, 1, 1));
    xmm = _mm_unpacklo_epi16(xmm, xmm);
    return (xmm);
}
static __m128i shuffle_2w(__m128i xmm)
{
    xmm = _mm_shufflelo_epi16(xmm, SHUFFLE(2, 2, 2, 2));
    xmm = _mm_unpacklo_epi16(xmm, xmm);
    return (xmm);
}
static __m128i shuffle_3w(__m128i xmm)
{
    xmm = _mm_shufflelo_epi16(xmm, SHUFFLE(3, 3, 3, 3));
    xmm = _mm_unpacklo_epi16(xmm, xmm);
    return (xmm);
}
static __m128i shuffle_4w(__m128i xmm)
{
    xmm = _mm_shufflehi_epi16(xmm, SHUFFLE(4, 4, 4, 4));
    xmm = _mm_unpackhi_epi16(xmm, xmm);
    return (xmm);
}
static __m128i shuffle_5w(__m128i xmm)
{
    xmm = _mm_shufflehi_epi16(xmm, SHUFFLE(5, 5, 5, 5));
    xmm = _mm_unpackhi_epi16(xmm, xmm);
    return (xmm);
}
static __m128i shuffle_6w(__m128i xmm)
{
    xmm = _mm_shufflehi_epi16(xmm, SHUFFLE(6, 6, 6, 6));
    xmm = _mm_unpackhi_epi16(xmm, xmm);
    return (xmm);
}
static __m128i shuffle_7w(__m128i xmm)
{
    xmm = _mm_shufflehi_epi16(xmm, SHUFFLE(7, 7, 7, 7));
    xmm = _mm_unpackhi_epi16(xmm, xmm);
    return (xmm);
}

static __m128i (*SSE2_SHUFFLE_16[16])(__m128i) = {
    shuffle_none, shuffle_none,
    shuffle_0q, shuffle_1q,
    shuffle_0h, shuffle_1h, shuffle_2h, shuffle_3h,
    shuffle_0w, shuffle_1w, shuffle_2w, shuffle_3w,
    shuffle_4w, shuffle_5w, shuffle_6w, shuffle_7w
};

INLINE static void SHUFFLE_VECTOR(short* VD, short* VT, const int e)
{
    __m128i xmm;

    xmm = _mm_load_si128((__m128i *)VT);
    xmm = SSE2_SHUFFLE_16[e](xmm);
    _mm_store_si128((__m128i *)VD, xmm);
    return;
}
#endif
#endif
#endif
