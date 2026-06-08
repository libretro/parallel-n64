static STRICTINLINE void video_max_optimized(uint32_t* pixels, uint32_t* penumin, uint32_t* penumax, int numofels)
{
    int i;
    int posmax = 0, posmin = 0;
    uint32_t curpenmax = pixels[0], curpenmin = pixels[0];
    uint32_t max, min;

    for (i = 1; i < numofels; i++)
    {
        if (pixels[i] > pixels[posmax])
        {
            curpenmax = pixels[posmax];
            posmax = i;
        }
        else if (pixels[i] < pixels[posmin])
        {
            curpenmin = pixels[posmin];
            posmin = i;
        }
    }
    max = pixels[posmax];
    min = pixels[posmin];
    if (curpenmax != max)
    {
        for (i = posmax + 1; i < numofels; i++)
        {
            if (pixels[i] > curpenmax)
                curpenmax = pixels[i];
        }
    }
    if (curpenmin != min)
    {
        for (i = posmin + 1; i < numofels; i++)
        {
            if (pixels[i] < curpenmin)
                curpenmin = pixels[i];
        }
    }
    *penumax = curpenmax;
    *penumin = curpenmin;
}


static void video_filter16_scalar(int* endr, int* endg, int* endb, uint32_t fboffset, uint32_t num, uint32_t hres, uint32_t centercvg, uint32_t fetchbugstate)
{
    int i;
    uint32_t penumaxr, penumaxg, penumaxb, penuminr, penuming, penuminb;
    uint16_t pix;
    uint32_t numoffull = 1;
    uint8_t hidval;
    uint32_t r, g, b;
    uint32_t backr[7], backg[7], backb[7];

    r = *endr;
    g = *endg;
    b = *endb;

    backr[0] = r;
    backg[0] = g;
    backb[0] = b;

    uint32_t idx = (fboffset >> 1) + num;

    uint32_t toleft = idx - 2;
    uint32_t toright = idx + 2;

    uint32_t leftup, rightup, leftdown, rightdown;

    leftup = idx - hres - 1;
    rightup = idx - hres + 1;

    if (fetchbugstate != 1)
    {
        leftdown = idx + hres - 1;
        rightdown = idx + hres + 1;
    }
    else
    {
        leftdown = toleft;
        rightdown = toright;
    }

    const uint32_t dirs[] = {leftup, rightup, toleft, toright, leftdown, rightdown};

    for (i = 0; i < 6; i++)
    {
        rdram_read_pair16(&pix, &hidval, dirs[i]);
        if (hidval == 3 && (pix & 1))
        {
            backr[numoffull] = RGBA16_R(pix);
            backg[numoffull] = RGBA16_G(pix);
            backb[numoffull] = RGBA16_B(pix);
            numoffull++;
        }
    }

    uint32_t colr, colg, colb;


    /* No neighbor qualified: penumin and penumax both equal the
     * center, the color delta is zero and the coefficient blend
     * reduces to ((0 * coeff) + 4) >> 3 == 0 for every coverage,
     * so the filter is the exact identity. */
    if (numoffull == 1)
        return;

    video_max_optimized(backr, &penuminr, &penumaxr, numoffull);
    video_max_optimized(backg, &penuming, &penumaxg, numoffull);
    video_max_optimized(backb, &penuminb, &penumaxb, numoffull);

    uint32_t coeff = 7 - centercvg;
    colr = penuminr + penumaxr - (r << 1);
    colg = penuming + penumaxg - (g << 1);
    colb = penuminb + penumaxb - (b << 1);

    colr = (((colr * coeff) + 4) >> 3) + r;
    colg = (((colg * coeff) + 4) >> 3) + g;
    colb = (((colb * coeff) + 4) >> 3) + b;

    *endr = colr & 0xff;
    *endg = colg & 0xff;
    *endb = colb & 0xff;
}

/* Vectorized AA filter. The six neighbor reads stay scalar (their
 * addresses scatter across three frame-buffer rows) behind one
 * hoisted bounds check; rows touching the address-space edges take
 * the original scalar path, which also remains the non-SIMD build.
 * When no neighbor passes the coverage test the filter is the
 * exact identity (the penultimate extremes both equal the center,
 * so the coefficient blend adds ((0 * coeff) + 4) >> 3 == 0).
 * Otherwise the three channels run lane-parallel: the RGBA5551
 * decode is one multiply by (1 << {0,5,10}) and a shared
 * shift-and-mask, the penultimate min and max come from a
 * streaming top-two update per candidate (non-qualifying
 * candidates become neutral values that provably cannot displace
 * either pair), and the coefficient blend runs in signed 16-bit
 * lanes, whose low byte matches the scalar's unsigned-wrapping
 * arithmetic exactly. */
#if defined(AL_VI_SSE2) || defined(AL_VI_NEON)
static STRICTINLINE void video_filter16(int* endr, int* endg, int* endb, uint32_t fboffset, uint32_t num, uint32_t hres, uint32_t centercvg, uint32_t fetchbugstate)
{
    uint32_t idx = (fboffset >> 1) + num;
    uint32_t hi = idx + hres + 1;
    uint32_t toleft, toright, leftup, rightup, leftdown, rightdown;
    uint16_t p0, p1, p2, p3, p4, p5;
    int q0, q1, q2, q3, q4, q5, numoffull;

    if (idx < hres + 1 || hi < idx || !rdram_valid_idx16(hi))
    {
        video_filter16_scalar(endr, endg, endb, fboffset, num, hres, centercvg, fetchbugstate);
        return;
    }

    toleft = idx - 2;
    toright = idx + 2;
    leftup = idx - hres - 1;
    rightup = idx - hres + 1;
    if (fetchbugstate != 1)
    {
        leftdown = idx + hres - 1;
        rightdown = idx + hres + 1;
    }
    else
    {
        leftdown = toleft;
        rightdown = toright;
    }

    p0 = rdram16[leftup ^ WORD_ADDR_XOR];
    q0 = (rdram_hidden[leftup] == 3) & (int)(p0 & 1);
    p1 = rdram16[rightup ^ WORD_ADDR_XOR];
    q1 = (rdram_hidden[rightup] == 3) & (int)(p1 & 1);
    p2 = rdram16[toleft ^ WORD_ADDR_XOR];
    q2 = (rdram_hidden[toleft] == 3) & (int)(p2 & 1);
    p3 = rdram16[toright ^ WORD_ADDR_XOR];
    q3 = (rdram_hidden[toright] == 3) & (int)(p3 & 1);
    p4 = rdram16[leftdown ^ WORD_ADDR_XOR];
    q4 = (rdram_hidden[leftdown] == 3) & (int)(p4 & 1);
    p5 = rdram16[rightdown ^ WORD_ADDR_XOR];
    q5 = (rdram_hidden[rightdown] == 3) & (int)(p5 & 1);

    numoffull = 1 + q0 + q1 + q2 + q3 + q4 + q5;
    if (numoffull == 1)
        return;

#if defined(AL_VI_SSE2)
    {
        __m128i shl = _mm_set_epi16(0, 0, 0, 0, 0, 1024, 32, 1);
        __m128i m_f8 = _mm_set1_epi16(0xf8);
        __m128i center = _mm_set_epi16(0, 0, 0, 0, 0, (short)*endb, (short)*endg, (short)*endr);
        __m128i max1 = center;
        __m128i min1 = center;
        __m128i max2 = _mm_setzero_si128();
        __m128i min2 = _mm_set1_epi16(0x7fff);
        __m128i col;

#define VF_CAND(p, q) do { \
        __m128i c16 = _mm_and_si128(_mm_srli_epi16(_mm_mullo_epi16(_mm_set1_epi16((short)(p)), shl), 8), m_f8); \
        __m128i qm = _mm_set1_epi16((short)-(q)); \
        __m128i cmax = _mm_and_si128(c16, qm); \
        __m128i cmin = _mm_or_si128(_mm_and_si128(c16, qm), _mm_andnot_si128(qm, _mm_set1_epi16(0x7fff))); \
        max2 = _mm_max_epi16(_mm_min_epi16(max1, cmax), max2); \
        max1 = _mm_max_epi16(max1, cmax); \
        min2 = _mm_min_epi16(_mm_max_epi16(min1, cmin), min2); \
        min1 = _mm_min_epi16(min1, cmin); \
    } while (0)
        VF_CAND(p0, q0);
        VF_CAND(p1, q1);
        VF_CAND(p2, q2);
        VF_CAND(p3, q3);
        VF_CAND(p4, q4);
        VF_CAND(p5, q5);
#undef VF_CAND

        /* video_max_optimized never displaces its tracked extreme when
         * the center is the strict unique max (or min) of the set, so
         * its penultimate stays at the init value, the center itself;
         * replicate that: when no candidate strictly beat the center,
         * the penultimate is the center, not the true second extreme. */
        {
            __m128i eqx = _mm_cmpeq_epi16(max1, center);
            __m128i eqn = _mm_cmpeq_epi16(min1, center);
            max2 = _mm_or_si128(_mm_and_si128(eqx, max1), _mm_andnot_si128(eqx, max2));
            min2 = _mm_or_si128(_mm_and_si128(eqn, min1), _mm_andnot_si128(eqn, min2));
        }
        col = _mm_sub_epi16(_mm_add_epi16(min2, max2), _mm_slli_epi16(center, 1));
        col = _mm_add_epi16(_mm_srai_epi16(_mm_add_epi16(_mm_mullo_epi16(col, _mm_set1_epi16((short)(7 - centercvg))), _mm_set1_epi16(4)), 3), center);
        *endr = _mm_extract_epi16(col, 0) & 0xff;
        *endg = _mm_extract_epi16(col, 1) & 0xff;
        *endb = _mm_extract_epi16(col, 2) & 0xff;
    }
#else
    {
        static const int16_t vf_shl[8] = {0, 5, 10, 0, 0, 0, 0, 0};
        int16x8_t shl = vld1q_s16(vf_shl);
        int16x8_t m_f8 = vdupq_n_s16(0xf8);
        int16x8_t center = vsetq_lane_s16((int16_t)*endb,
            vsetq_lane_s16((int16_t)*endg,
            vdupq_n_s16((int16_t)*endr), 1), 2);
        int16x8_t max1 = center;
        int16x8_t min1 = center;
        int16x8_t max2 = vdupq_n_s16(0);
        int16x8_t min2 = vdupq_n_s16(0x7fff);
        int16x8_t col;

#define VF_CAND(p, q) do { \
        int16x8_t c16 = vandq_s16(vreinterpretq_s16_u16(vshrq_n_u16(vreinterpretq_u16_s16(vshlq_s16(vdupq_n_s16((int16_t)(p)), shl)), 8)), m_f8); \
        int16x8_t qm = vdupq_n_s16((int16_t)-(q)); \
        int16x8_t cmax = vandq_s16(c16, qm); \
        int16x8_t cmin = vorrq_s16(vandq_s16(c16, qm), vbicq_s16(vdupq_n_s16(0x7fff), qm)); \
        max2 = vmaxq_s16(vminq_s16(max1, cmax), max2); \
        max1 = vmaxq_s16(max1, cmax); \
        min2 = vminq_s16(vmaxq_s16(min1, cmin), min2); \
        min1 = vminq_s16(min1, cmin); \
    } while (0)
        VF_CAND(p0, q0);
        VF_CAND(p1, q1);
        VF_CAND(p2, q2);
        VF_CAND(p3, q3);
        VF_CAND(p4, q4);
        VF_CAND(p5, q5);
#undef VF_CAND

        {
            uint16x8_t eqx = vceqq_s16(max1, center);
            uint16x8_t eqn = vceqq_s16(min1, center);
            max2 = vbslq_s16(eqx, max1, max2);
            min2 = vbslq_s16(eqn, min1, min2);
        }
        col = vsubq_s16(vaddq_s16(min2, max2), vshlq_n_s16(center, 1));
        col = vaddq_s16(vshrq_n_s16(vaddq_s16(vmulq_s16(col, vdupq_n_s16((int16_t)(7 - centercvg))), vdupq_n_s16(4)), 3), center);
        *endr = vgetq_lane_s16(col, 0) & 0xff;
        *endg = vgetq_lane_s16(col, 1) & 0xff;
        *endb = vgetq_lane_s16(col, 2) & 0xff;
    }
#endif
}
#else
#define video_filter16 video_filter16_scalar
#endif


static STRICTINLINE void video_filter32(int* endr, int* endg, int* endb, uint32_t fboffset, uint32_t num, uint32_t hres, uint32_t centercvg, uint32_t fetchbugstate)
{
    int i;
    uint32_t penumaxr, penumaxg, penumaxb, penuminr, penuming, penuminb;
    uint32_t numoffull = 1;
    uint32_t pix = 0, pixcvg = 0;
    uint32_t r, g, b;
    uint32_t backr[7], backg[7], backb[7];

    r = *endr;
    g = *endg;
    b = *endb;

    backr[0] = r;
    backg[0] = g;
    backb[0] = b;

    uint32_t idx = (fboffset >> 2) + num;

    uint32_t toleft = idx - 2;
    uint32_t toright = idx + 2;

    uint32_t leftup, rightup, leftdown, rightdown;

    leftup = idx - hres - 1;
    rightup = idx - hres + 1;

    if (fetchbugstate != 1)
    {
        leftdown = idx + hres - 1;
        rightdown = idx + hres + 1;
    }
    else
    {
        leftdown = toleft;
        rightdown = toright;
    }

    const uint32_t dirs[] = {leftup, rightup, toleft, toright, leftdown, rightdown};

    for (i = 0; i < 6; i++)
    {
        pix = rdram_read_idx32(dirs[i]);
        pixcvg = (pix >> 5) & 7;
        if (pixcvg == 7)
        {
            backr[numoffull] = (pix >> 24) & 0xff;
            backg[numoffull] = (pix >> 16) & 0xff;
            backb[numoffull] = (pix >> 8) & 0xff;
            numoffull++;
        }
    }

    uint32_t colr, colg, colb;

    video_max_optimized(backr, &penuminr, &penumaxr, numoffull);
    video_max_optimized(backg, &penuming, &penumaxg, numoffull);
    video_max_optimized(backb, &penuminb, &penumaxb, numoffull);

    uint32_t coeff = 7 - centercvg;
    colr = penuminr + penumaxr - (r << 1);
    colg = penuming + penumaxg - (g << 1);
    colb = penuminb + penumaxb - (b << 1);

    colr = (((colr * coeff) + 4) >> 3) + r;
    colg = (((colg * coeff) + 4) >> 3) + g;
    colb = (((colb * coeff) + 4) >> 3) + b;

    *endr = colr & 0xff;
    *endg = colg & 0xff;
    *endb = colb & 0xff;
}
