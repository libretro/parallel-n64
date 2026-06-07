static int vi_restore_table[0x400];

/* SIMD kernels for the restore (dither) filter. Compile-time dispatch
 * in the prboom style: SSE2 is baseline on x86-64, NEON on AArch64.
 * The scalar path below is kept verbatim as the fallback and as the
 * bit-exactness reference - vi_restore_table[i] is sign((i & 0x1f) -
 * ((i >> 5) & 0x1f)), i.e. a memoized three-way compare of the
 * neighbor's 5-bit channel against the center's, so the 24 table
 * lookups per pixel reduce to two vector compares and a subtract per
 * channel. */
#if defined(__SSE2__) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2) || defined(_M_X64)
#define AL_VI_RESTORE_SSE2 1
#include <emmintrin.h>
#elif (defined(__ARM_NEON) || defined(__ARM_NEON__)) && (defined(__aarch64__) || defined(_M_ARM64))
/* AArch64 only: the horizontal reduction uses vaddvq_s16, which does
 * not exist on ARMv7 NEON. 32-bit ARM keeps the scalar path. */
#define AL_VI_RESTORE_NEON 1
#include <arm_neon.h>
#endif

#if defined(AL_VI_RESTORE_SSE2)
static STRICTINLINE void restore_filter16_simd(int* r, int* g, int* b, const uint16_t* pix8)
{
    __m128i pix = _mm_loadu_si128((const __m128i*)pix8);

    __m128i nr = _mm_and_si128(_mm_srli_epi16(pix, 11), _mm_set1_epi16(0x1f));
    __m128i ng = _mm_and_si128(_mm_srli_epi16(pix, 6),  _mm_set1_epi16(0x1f));
    __m128i nb = _mm_and_si128(_mm_srli_epi16(pix, 1),  _mm_set1_epi16(0x1f));

    __m128i cr = _mm_set1_epi16((short)((*r >> 3) & 0x1f));
    __m128i cg = _mm_set1_epi16((short)((*g >> 3) & 0x1f));
    __m128i cb = _mm_set1_epi16((short)((*b >> 3) & 0x1f));

    /* sign(neighbor - center) per lane: cmpgt yields 0 or -1, so
     * acc = lt - gt accumulates -1/0/+1 with the same meaning as the
     * scalar table (+1 when neighbor > center). */
    __m128i dr = _mm_sub_epi16(_mm_cmpgt_epi16(cr, nr), _mm_cmpgt_epi16(nr, cr));
    __m128i dg = _mm_sub_epi16(_mm_cmpgt_epi16(cg, ng), _mm_cmpgt_epi16(ng, cg));
    __m128i db = _mm_sub_epi16(_mm_cmpgt_epi16(cb, nb), _mm_cmpgt_epi16(nb, cb));

    /* horizontal sum of 8 signed 16-bit lanes via madd against ones */
    __m128i one = _mm_set1_epi16(1);
    __m128i sr = _mm_madd_epi16(dr, one);
    __m128i sg = _mm_madd_epi16(dg, one);
    __m128i sb = _mm_madd_epi16(db, one);

    sr = _mm_add_epi32(sr, _mm_shuffle_epi32(sr, _MM_SHUFFLE(2, 3, 0, 1)));
    sr = _mm_add_epi32(sr, _mm_shuffle_epi32(sr, _MM_SHUFFLE(1, 0, 3, 2)));
    sg = _mm_add_epi32(sg, _mm_shuffle_epi32(sg, _MM_SHUFFLE(2, 3, 0, 1)));
    sg = _mm_add_epi32(sg, _mm_shuffle_epi32(sg, _MM_SHUFFLE(1, 0, 3, 2)));
    sb = _mm_add_epi32(sb, _mm_shuffle_epi32(sb, _MM_SHUFFLE(2, 3, 0, 1)));
    sb = _mm_add_epi32(sb, _mm_shuffle_epi32(sb, _MM_SHUFFLE(1, 0, 3, 2)));

    /* cmpgt yields -1 for true, so cgt(c,n) - cgt(n,c) is +1 when the
     * neighbor exceeds the center - the same orientation the scalar
     * table adds - hence plain addition here. */
    *r += _mm_cvtsi128_si32(sr);
    *g += _mm_cvtsi128_si32(sg);
    *b += _mm_cvtsi128_si32(sb);
}
#elif defined(AL_VI_RESTORE_NEON)
static STRICTINLINE void restore_filter16_simd(int* r, int* g, int* b, const uint16_t* pix8)
{
    uint16x8_t pix = vld1q_u16(pix8);

    int16x8_t nr = vreinterpretq_s16_u16(vandq_u16(vshrq_n_u16(pix, 11), vdupq_n_u16(0x1f)));
    int16x8_t ng = vreinterpretq_s16_u16(vandq_u16(vshrq_n_u16(pix, 6),  vdupq_n_u16(0x1f)));
    int16x8_t nb = vreinterpretq_s16_u16(vandq_u16(vshrq_n_u16(pix, 1),  vdupq_n_u16(0x1f)));

    int16x8_t cr = vdupq_n_s16((int16_t)((*r >> 3) & 0x1f));
    int16x8_t cg = vdupq_n_s16((int16_t)((*g >> 3) & 0x1f));
    int16x8_t cb = vdupq_n_s16((int16_t)((*b >> 3) & 0x1f));

    int16x8_t dr = vsubq_s16(vreinterpretq_s16_u16(vcgtq_s16(cr, nr)),
                             vreinterpretq_s16_u16(vcgtq_s16(nr, cr)));
    int16x8_t dg = vsubq_s16(vreinterpretq_s16_u16(vcgtq_s16(cg, ng)),
                             vreinterpretq_s16_u16(vcgtq_s16(ng, cg)));
    int16x8_t db = vsubq_s16(vreinterpretq_s16_u16(vcgtq_s16(cb, nb)),
                             vreinterpretq_s16_u16(vcgtq_s16(nb, cb)));

    *r += (int)vaddvq_s16(dr);
    *g += (int)vaddvq_s16(dg);
    *b += (int)vaddvq_s16(db);
}
#endif

static STRICTINLINE void restore_filter16(int* r, int* g, int* b, uint32_t fboffset, uint32_t num, uint32_t hres, uint32_t fetchbugstate)
{
    int i;

    uint32_t idx = (fboffset >> 1) + num;

    uint32_t toleftpix = idx - 1;

    uint32_t leftuppix, leftdownpix, maxpix;

    leftuppix = idx - hres - 1;

    if (fetchbugstate != 1)
    {
        leftdownpix = idx + hres - 1;
        maxpix = idx + hres + 1;
    }
    else
    {
        leftdownpix = toleftpix;
        maxpix = toleftpix + 2;
    }

    int rend = *r;
    int gend = *g;
    int bend = *b;
    const int* redptr = &vi_restore_table[(rend << 2) & 0x3e0];
    const int* greenptr = &vi_restore_table[(gend << 2) & 0x3e0];
    const int* blueptr = &vi_restore_table[(bend << 2) & 0x3e0];

    uint32_t tempr, tempg, tempb;
    uint16_t pix;

    const uint32_t dirs[] =
    {
        leftuppix, leftuppix + 1, leftuppix + 2, leftdownpix,
        leftdownpix + 1, maxpix, toleftpix, toleftpix + 2
    };

    if (rdram_valid_idx16(maxpix) && rdram_valid_idx16(leftuppix))
    {
#if defined(AL_VI_RESTORE_SSE2) || defined(AL_VI_RESTORE_NEON)
        uint16_t pix8[8];
        for (i = 0; i < 8; i++)
            pix8[i] = rdram_read_idx16_fast(dirs[i]);
        restore_filter16_simd(&rend, &gend, &bend, pix8);
#else
        for (i = 0; i < 8; i++)
        {
            pix = rdram_read_idx16_fast(dirs[i]);
            tempr = (pix >> 11) & 0x1f;
            tempg = (pix >> 6) & 0x1f;
            tempb = (pix >> 1) & 0x1f;
            rend += redptr[tempr];
            gend += greenptr[tempg];
            bend += blueptr[tempb];
        }
#endif
    }
    else
    {
        for (i = 0; i < 8; i++)
        {
            pix = rdram_read_idx16(dirs[i]);
            tempr = (pix >> 11) & 0x1f;
            tempg = (pix >> 6) & 0x1f;
            tempb = (pix >> 1) & 0x1f;
            rend += redptr[tempr];
            gend += greenptr[tempg];
            bend += blueptr[tempb];
        }
    }


    *r = rend;
    *g = gend;
    *b = bend;
}

static STRICTINLINE void restore_filter32(int* r, int* g, int* b, uint32_t fboffset, uint32_t num, uint32_t hres, uint32_t fetchbugstate)
{
    int i;
    uint32_t idx = (fboffset >> 2) + num;

    uint32_t toleftpix = idx - 1;

    uint32_t leftuppix, leftdownpix, maxpix;

    leftuppix = idx - hres - 1;

    if (fetchbugstate != 1)
    {
        leftdownpix = idx + hres - 1;
        maxpix = idx +hres + 1;
    }
    else
    {
        leftdownpix = toleftpix;
        maxpix = toleftpix + 2;
    }

    int rend = *r;
    int gend = *g;
    int bend = *b;
    const int* redptr = &vi_restore_table[(rend << 2) & 0x3e0];
    const int* greenptr = &vi_restore_table[(gend << 2) & 0x3e0];
    const int* blueptr = &vi_restore_table[(bend << 2) & 0x3e0];

    uint32_t tempr, tempg, tempb;
    uint32_t pix;

    const uint32_t dirs[] =
    {
        leftuppix, leftuppix + 1, leftuppix + 2, leftdownpix,
        leftdownpix + 1, maxpix, toleftpix, toleftpix + 2
    };

    if (rdram_valid_idx32(maxpix) && rdram_valid_idx32(leftuppix))
    {
        for (i = 0; i < 8; i++)
        {
            pix = rdram_read_idx32_fast(dirs[i]);
            tempr = (pix >> 27) & 0x1f;
            tempg = (pix >> 19) & 0x1f;
            tempb = (pix >> 11) & 0x1f;
            rend += redptr[tempr];
            gend += greenptr[tempg];
            bend += blueptr[tempb];
        }
    }
    else
    {
        for (i = 0; i < 8; i++)
        {
            pix = rdram_read_idx32(dirs[i]);
            tempr = (pix >> 27) & 0x1f;
            tempg = (pix >> 19) & 0x1f;
            tempb = (pix >> 11) & 0x1f;
            rend += redptr[tempr];
            gend += greenptr[tempg];
            bend += blueptr[tempb];
        }
    }

    *r = rend;
    *g = gend;
    *b = bend;
}

void vi_restore_init()
{
    int i;
    for (i = 0; i < 0x400; i++)
    {
        if (((i >> 5) & 0x1f) < (i & 0x1f))
            vi_restore_table[i] = 1;
        else if (((i >> 5) & 0x1f) > (i & 0x1f))
            vi_restore_table[i] = -1;
        else
            vi_restore_table[i] = 0;
    }
}
