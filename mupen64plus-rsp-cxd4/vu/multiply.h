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

#ifndef SEMIFRAC
/*
 * acc = VS * VT;
 * acc = acc + 0x8000; // rounding value
 * acc = acc << 1; // partial value shifting
 *
 * Wrong:  ACC(HI) = -((INT32)(acc) < 0)
 * Right:  ACC(HI) = -(SEMIFRAC < 0)
 */
#define SEMIFRAC    (VS[i]*VT[i]*2/2 + 0x8000/2)
#endif

#ifdef ARCH_MIN_SSE2
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
#else
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
#endif

static INLINE void UNSIGNED_CLAMP(short* VD)
{
   /* sign-zero hybrid clamp of accumulator-mid (bits 31:16) */
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
}

static INLINE void SIGNED_CLAMP_AL(short* VD)
{
   /* sign-clamp accumulator-low (bits 15:0) */
   ALIGNED int16_t temp[N];
   int16_t cond[N];
   register int i;

   SIGNED_CLAMP_AM(temp); /* no direct map in SSE, but closely based on this */
   for (i = 0; i < N; i++)
      cond[i] = (temp[i] != VACC_M[i]); /* result_clamped != result_raw ? */
   for (i = 0; i < N; i++)
      temp[i] ^= 0x8000; /* clamps 0x0000:0xFFFF instead of -0x8000:+0x7FFF */
   for (i = 0; i < N; i++)
      VD[i] = (cond[i] ? temp[i] : VACC_L[i]);
}

static INLINE void do_macf(short* VD, short* VS, short* VT)
{
    int32_t product[N];
    uint32_t addend[N];
    register int i;

    for (i = 0; i < N; i++)
        product[i] = VS[i] * VT[i];
    for (i = 0; i < N; i++)
        addend[i] = (product[i] << 1) & 0x00000000FFFF;
    for (i = 0; i < N; i++)
        addend[i] = (unsigned short)(VACC_L[i]) + addend[i];
    for (i = 0; i < N; i++)
        VACC_L[i] = (short)(addend[i]);
    for (i = 0; i < N; i++)
        addend[i] = (addend[i] >> 16) + (unsigned short)(product[i] >> 15);
    for (i = 0; i < N; i++)
        addend[i] = (unsigned short)(VACC_M[i]) + addend[i];
    for (i = 0; i < N; i++)
        VACC_M[i] = (short)(addend[i]);
    for (i = 0; i < N; i++)
        VACC_H[i] -= (product[i] < 0);
    for (i = 0; i < N; i++)
        VACC_H[i] += addend[i] >> 16;
}

static INLINE void do_mulf(short* VD, short* VS, short* VT)
{
    register int i;

    for (i = 0; i < N; i++)
        VACC_L[i] = (SEMIFRAC << 1) >>  0;
    for (i = 0; i < N; i++)
        VACC_M[i] = (SEMIFRAC << 1) >> 16;
    for (i = 0; i < N; i++)
        VACC_H[i] = -((VACC_M[i] < 0) & (VS[i] != VT[i])); /* -32768 * -32768 */
#ifndef ARCH_MIN_SSE2
    vector_copy(VD, VACC_M);
    for (i = 0; i < N; i++)
        VD[i] -= (VACC_M[i] < 0) & (VS[i] == VT[i]); /* ACC b 31 set, min*min */
#else
    SIGNED_CLAMP_AM(VD);
#endif
}

static INLINE void do_mulu(short* VD, short* VS, short* VT)
{
   register int i;

   for (i = 0; i < N; i++)
      VACC_L[i] = (SEMIFRAC << 1) >>  0;
   for (i = 0; i < N; i++)
      VACC_M[i] = (SEMIFRAC << 1) >> 16;
   for (i = 0; i < N; i++)
      VACC_H[i] = -((VACC_M[i] < 0) & (VS[i] != VT[i])); /* -32768 * -32768 */
#if (0)
   UNSIGNED_CLAMP(VD);
#else
   vector_copy(VD, VACC_M);
   for (i = 0; i < N; i++)
      VD[i] |=  (VACC_M[i] >> 15); /* VD |= -(result == 0x000080008000) */
   for (i = 0; i < N; i++)
      VD[i] &= ~(VACC_H[i] >>  0); /* VD &= -(result >= 0x000000000000) */
#endif
}

static INLINE void do_madm(short* VD, short* VS, short* VT)
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
}

static INLINE void do_madh(short* VD, short* VS, short* VT)
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
}

static INLINE void do_mudm(short* VD, short* VS, short* VT)
{
    register int i;

    for (i = 0; i < N; i++)
        VACC_L[i] = (VS[i]*(unsigned short)(VT[i]) & 0x00000000FFFF) >>  0;
    for (i = 0; i < N; i++)
        VACC_M[i] = (VS[i]*(unsigned short)(VT[i]) & 0x0000FFFF0000) >> 16;
    for (i = 0; i < N; i++)
        VACC_H[i] = -(VACC_M[i] < 0);
    vector_copy(VD, VACC_M); /* no possibilities to clamp */
}

static INLINE void do_mudn(short* VD, short* VS, short* VT)
{
   register int i;

   for (i = 0; i < N; i++)
      VACC_L[i] = ((unsigned short)(VS[i])*VT[i] & 0x00000000FFFF) >>  0;
   for (i = 0; i < N; i++)
      VACC_M[i] = ((unsigned short)(VS[i])*VT[i] & 0x0000FFFF0000) >> 16;
   for (i = 0; i < N; i++)
      VACC_H[i] = -(VACC_M[i] < 0);
   vector_copy(VD, VACC_L); /* no possibilities to clamp */
}

static INLINE void do_mudh(short* VD, short* VS, short* VT)
{
   register int i;

   for (i = 0; i < N; i++)
      VACC_L[i] = 0x0000;
   for (i = 0; i < N; i++)
      VACC_M[i] = (short)(VS[i]*VT[i] >>  0);
   for (i = 0; i < N; i++)
      VACC_H[i] = (short)(VS[i]*VT[i] >> 16);
   SIGNED_CLAMP_AM(VD);
}

static INLINE void do_madn(short* VD, short* VS, short* VT)
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
}

static INLINE void do_madl(short* VD, short* VS, short* VT)
{
   int32_t product[N];
   uint32_t addend[N];
   register int i;

   for (i = 0; i < N; i++)
      product[i] = (unsigned short)(VS[i]) * (unsigned short)(VT[i]);
   for (i = 0; i < N; i++)
      addend[i] = (product[i] & 0x0000FFFF0000) >> 16;
   for (i = 0; i < N; i++)
      addend[i] = (unsigned short)(VACC_L[i]) + addend[i];
   for (i = 0; i < N; i++)
      VACC_L[i] = (short)(addend[i]);
   for (i = 0; i < N; i++)
      addend[i] = (unsigned short)(addend[i] >> 16);
   for (i = 0; i < N; i++)
      addend[i] = (unsigned short)(VACC_M[i]) + addend[i];
   for (i = 0; i < N; i++)
      VACC_M[i] = (short)(addend[i]);
   for (i = 0; i < N; i++)
      VACC_H[i] += addend[i] >> 16;
   SIGNED_CLAMP_AL(VD);
}

static INLINE void do_mudl(short* VD, short* VS, short* VT)
{
#ifdef ARCH_MIN_SSE2
   __m128i vs = _mm_loadu_si128((__m128i *)VS);
   __m128i vt = _mm_loadu_si128((__m128i *)VT);

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
}

static void VMUDL(int vd, int vs, int vt, int e)
{
   short ST[N];

   SHUFFLE_VECTOR(ST, VR[vt], e);
   do_mudl(VR[vd], VR[vs], ST);
}

static void VMADL(int vd, int vs, int vt, int e)
{
   short ST[N];

   SHUFFLE_VECTOR(ST, VR[vt], e);
   do_madl(VR[vd], VR[vs], ST);
}

static void VMADN(int vd, int vs, int vt, int e)
{
   short ST[N];

   SHUFFLE_VECTOR(ST, VR[vt], e);
   do_madn(VR[vd], VR[vs], ST);
}

static void VMUDH(int vd, int vs, int vt, int e)
{
   short ST[N];

   SHUFFLE_VECTOR(ST, VR[vt], e);
   do_mudh(VR[vd], VR[vs], ST);
}

static void VMUDN(int vd, int vs, int vt, int e)
{
   short ST[N];

   SHUFFLE_VECTOR(ST, VR[vt], e);
   do_mudn(VR[vd], VR[vs], ST);
}

static void VMUDM(int vd, int vs, int vt, int e)
{
   short ST[N];

   SHUFFLE_VECTOR(ST, VR[vt], e);
   do_mudm(VR[vd], VR[vs], ST);
}

static void VMADH(int vd, int vs, int vt, int e)
{
   short ST[N];

   SHUFFLE_VECTOR(ST, VR[vt], e);
   do_madh(VR[vd], VR[vs], ST);
}

static void VMADM(int vd, int vs, int vt, int e)
{
    short ST[N];

    SHUFFLE_VECTOR(ST, VR[vt], e);
    do_madm(VR[vd], VR[vs], ST);
}

static INLINE void VMULU(int vd, int vs, int vt, int e)
{
   short ST[N];

   SHUFFLE_VECTOR(ST, VR[vt], e);
   do_mulu(VR[vd], VR[vs], ST);
}

static void VMULF(int vd, int vs, int vt, int e)
{
    short ST[N];

    SHUFFLE_VECTOR(ST, VR[vt], e);
    do_mulf(VR[vd], VR[vs], ST);
}

static void VMACF(int vd, int vs, int vt, int e)
{
   short ST[N];

   SHUFFLE_VECTOR(ST, VR[vt], e);
   do_macf(VR[vd], VR[vs], ST);
   SIGNED_CLAMP_AM(VR[vd]);
}

static void VMACU(int vd, int vs, int vt, int e)
{
    short ST[N];

    SHUFFLE_VECTOR(ST, VR[vt], e);
    do_macf(VR[vd], VR[vs], ST);
    UNSIGNED_CLAMP(VR[vd]);
}
