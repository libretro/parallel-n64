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

#ifdef ARCH_MIN_SSE2

static INLINE void SIGNED_CLAMP_ADD(short* VD, short* VS, short* VT)
{
   __m128i src = _mm_load_si128((__m128i *)VS);
   __m128i dst = _mm_load_si128((__m128i *)VT);
   __m128i vco = _mm_load_si128((__m128i *)co);

   /*
    * Due to premature clamping in between adds, sometimes we need to add the
    * LESSER of two integers, either VS or VT, to the carry-in flag matching the
    * current vector register slice, BEFORE finally adding the greater integer.
    */
   __m128i max = _mm_max_epi16(dst, src);
   __m128i min = _mm_min_epi16(dst, src);

   min = _mm_adds_epi16(min, vco);
   max = _mm_adds_epi16(max, min);
   _mm_store_si128((__m128i *)VD, max);
}

static INLINE void SIGNED_CLAMP_SUB(short* VD, short* VS, short* VT)
{
    __m128i xmm;

    __m128i src = _mm_load_si128((__m128i *)VS);
    __m128i dst = _mm_load_si128((__m128i *)VT);
    __m128i vco = _mm_load_si128((__m128i *)co);

    __m128i res = _mm_subs_epi16(src, dst);

/*
 * Due to premature clamps in-between subtracting two of the three operands,
 * we must be careful not to offset the result accidentally when subtracting
 * the corresponding VCO flag AFTER the saturation from doing (VS - VT).
 */
    __m128i dif = _mm_add_epi16(res, vco);

    dif = _mm_xor_si128(dif, res); /* Adding one suddenly inverts the sign? */
    dif = _mm_and_si128(dif, dst); /* Sign change due to subtracting a neg. */
    xmm = _mm_sub_epi16(src, dst);
    src = _mm_andnot_si128(src, dif); /* VS must be >= 0x0000 for overflow. */
    xmm = _mm_and_si128(xmm, src); /* VS + VT != INT16_MIN; VS + VT >= +32768 */
    xmm = _mm_srli_epi16(xmm, 15); /* src = (INT16_MAX + 1 === INT16_MIN) ? */

    xmm = _mm_andnot_si128(xmm, vco); /* If it's NOT overflow, keep flag. */
    res = _mm_subs_epi16(res, xmm);
    _mm_store_si128((__m128i *)VD, res);
}

#else

static INLINE void SIGNED_CLAMP_ADD(short* VD, short* VS, short* VT)
{
   int32_t sum[N];
   short hi[N], lo[N];
   register int i;

   for (i = 0; i < N; i++)
      sum[i] = VS[i] + VT[i] + co[i];
   for (i = 0; i < N; i++)
      lo[i] = (sum[i] + 0x8000) >> 31;
   for (i = 0; i < N; i++)
      hi[i] = (0x7FFF - sum[i]) >> 31;
   vector_copy(VD, VACC_L);
   for (i = 0; i < N; i++)
      VD[i] &= ~lo[i];
   for (i = 0; i < N; i++)
      VD[i] |=  hi[i];
   for (i = 0; i < N; i++)
      VD[i] ^= 0x8000 & (hi[i] | lo[i]);
}

static INLINE void SIGNED_CLAMP_SUB(short* VD, short* VS, short* VT)
{
   int32_t dif[N];
   short hi[N], lo[N];
   register int i;

   for (i = 0; i < N; i++)
      dif[i] = VS[i] - VT[i] - co[i];
   for (i = 0; i < N; i++)
      lo[i] = (dif[i] + 0x8000) >> 31;
   for (i = 0; i < N; i++)
      hi[i] = (0x7FFF - dif[i]) >> 31;
   vector_copy(VD, VACC_L);
   for (i = 0; i < N; i++)
      VD[i] &= ~lo[i];
   for (i = 0; i < N; i++)
      VD[i] |=  hi[i];
   for (i = 0; i < N; i++)
      VD[i] ^= 0x8000 & (hi[i] | lo[i]);
}
#endif

static INLINE void set_bo(short* VD, short* VS, short* VT)
{
   /* set CARRY and borrow out from difference */
   int32_t dif[N];
   register int i;

   for (i = 0; i < N; i++)
      dif[i] = (unsigned short)(VS[i]) - (unsigned short)(VT[i]);
   for (i = 0; i < N; i++)
      VACC_L[i] = VS[i] - VT[i];
   vector_copy(VD, VACC_L);
   for (i = 0; i < N; i++)
      ne[i] = (VS[i] != VT[i]);
   for (i = 0; i < N; i++)
      co[i] = (dif[i] < 0);
}

static INLINE void set_co(short* VD, short* VS, short* VT)
{
   /* set CARRY and carry out from sum */
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
}

/*
 * -1:  VT *= -1, because VS < 0 // VT ^= -2 if even, or ^= -1, += 1
 *  0:  VT *=  0, because VS = 0 // VT ^= VT
 * +1:  VT *= +1, because VS > 0 // VT ^=  0
 *      VT ^= -1, "negate" -32768 as ~+32767 (corner case hack for N64 SP)
 */
INLINE static void do_abs(short* VD, short* VS, short* VT)
{
   short neg[N], pos[N];
   short nez[N], cch[N]; /* corner case hack -- abs(-32768) == +32767 */
   short res[N];
   register int i;

   vector_copy(res, VT);
#ifndef ARCH_MIN_SSE2
#define MASK_XOR
#endif
   for (i = 0; i < N; i++)
      neg[i]  = (VS[i] <  0x0000);
   for (i = 0; i < N; i++)
      pos[i]  = (VS[i] >  0x0000);
   for (i = 0; i < N; i++)
      nez[i]  = 0;
#ifdef MASK_XOR
   for (i = 0; i < N; i++)
      neg[i]  = -neg[i];
   for (i = 0; i < N; i++)
      nez[i] += neg[i];
#else
   for (i = 0; i < N; i++)
      nez[i] -= neg[i];
#endif
   for (i = 0; i < N; i++)
      nez[i] += pos[i];
#ifdef MASK_XOR
   for (i = 0; i < N; i++)
      res[i] ^= nez[i];
   for (i = 0; i < N; i++)
      cch[i]  = (res[i] != -32768);
   for (i = 0; i < N; i++)
      res[i] += cch[i]; /* -(x) === (x ^ -1) + 1 */
#else
   for (i = 0; i < N; i++)
      res[i] *= nez[i];
   for (i = 0; i < N; i++)
      cch[i]  = (res[i] == -32768);
   for (i = 0; i < N; i++)
      res[i] -= cch[i];
#endif
   vector_copy(VACC_L, res);
   vector_copy(VD, VACC_L);
}

static INLINE void clr_bi(short* VD, short* VS, short* VT)
{
   /* clear CARRY and borrow in to accumulators */
   register int i;

   for (i = 0; i < N; i++)
      VACC_L[i] = VS[i] - VT[i] - co[i];
   SIGNED_CLAMP_SUB(VD, VS, VT);
   for (i = 0; i < N; i++)
      ne[i] = 0;
   for (i = 0; i < N; i++)
      co[i] = 0;
}

static INLINE void clr_ci(short* VD, short* VS, short* VT)
{
   /* clear CARRY and carry in to accumulators */
   register int i;

   for (i = 0; i < N; i++)
      VACC_L[i] = VS[i] + VT[i] + co[i];
   SIGNED_CLAMP_ADD(VD, VS, VT);
   for (i = 0; i < N; i++)
      ne[i] = 0;
   for (i = 0; i < N; i++)
      co[i] = 0;
}

static INLINE void VADD(int vd, int vs, int vt, int e)
{
   short ST[N];

   SHUFFLE_VECTOR(ST, VR[vt], e);
   clr_ci(VR[vd], VR[vs], ST);
}

static INLINE void VSUB(int vd, int vs, int vt, int e)
{
   short ST[N];

   SHUFFLE_VECTOR(ST, VR[vt], e);
   clr_bi(VR[vd], VR[vs], ST);
}

static void VABS(int vd, int vs, int vt, int e)
{
   short ST[N];

   SHUFFLE_VECTOR(ST, VR[vt], e);
   do_abs(VR[vd], VR[vs], ST);
}

static void VADDC(int vd, int vs, int vt, int e)
{
    short ST[N];

    SHUFFLE_VECTOR(ST, VR[vt], e);
    set_co(VR[vd], VR[vs], ST);
}

static void VSUBC(int vd, int vs, int vt, int e)
{
   short ST[N];

   SHUFFLE_VECTOR(ST, VR[vt], e);
   set_bo(VR[vd], VR[vs], ST);
}

#ifdef VU_EMULATE_SCALAR_ACCUMULATOR_READ

static void VSAR(int vd, int vs, int vt, int e)
{
   short oldval[N];
   register int i;

   for (i = 0; i < N; i++)
      oldval[i] = VR[vs][i];
   vt = 0;
   /* Even though VT is ignored in VSAR, according to official sources as well
    * as reversing, lots of games seem to specify it as non-zero, possibly to
    * avoid register stalling or other VU hazards.  Not really certain why yet.
    */
   e ^= 0x8;
   /* Or, for exception overrides, should this be `e &= 0x7;` ?
    * Currently this code is safer because &= is less likely to catch oddities.
    * Either way, documentation shows that the switch range is 0:2, not 8:A.
    */
   if (e > 2) /* VSAR, invalid mask */
   {
      for (i = 0; i < N; i++)
         VR[vd][i] = 0x0000; /* override behavior (zilmar) */
   }
   else
      for (i = 0; i < N; i++)
         VR[vd][i] = VACC[e][i];
   for (i = 0; i < N; i++)
      VACC[e][i] = oldval[i]; /* ... = VS */
}
#endif

static void VSAW(int vd, int vs, int vt, int e)
{
   vs = 0; /* unused--old VSAR algorithm */
   vt = 0; /* unused but mysteriously set many times */
   if (vs | vt)
      return;
   e ^= 0x8; /* &= 7 */

   if (e > 0x2)
   {
      /* VSAW, illegal mask */
      /* branch very unlikely...never seen a game do VSAW illegally */
      register int i;

      for (i = 0; i < N; i++)
         VR[vd][i] = 0x0000; /* override behavior (zilmar) */
      return;
   }
   vector_copy(VR[vd], VACC[e]);
}
