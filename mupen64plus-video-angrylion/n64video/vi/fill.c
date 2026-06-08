/* Batched fill of one fetch-filter cache row for the 16-bit frame
 * buffer formats. The per-pixel cache ladder in vi_process pulls
 * entries through an indirect call one at a time; this fills the
 * row's contiguous index range in one pass instead. Pixels at full
 * coverage with the dither (restore) filter disabled - the
 * overwhelming share on every measured title - decode straight from
 * the frame buffer eight at a time: rdram16 words live pair-swapped
 * (idx ^ WORD_ADDR_XOR), so an even-aligned vector load followed by
 * a 16-bit pair swap yields the in-order pixels, the hidden bits
 * load contiguously for the coverage test, and the RGBA5551 decode
 * is three shift-and-masks per channel packed back to the b,g,r,a
 * byte layout of struct rgba. Any group holding a partial-coverage
 * pixel, and any range failing the single up-front bounds check,
 * falls back to vi_fetch_filter16 per pixel, which keeps the AA
 * filter and the out-of-range zero-fill semantics exactly. */
static void vi_fill_cache16(struct rgba* cache, int32_t lo, int32_t hi, uint32_t fboffset, uint32_t pixels, struct vi_reg_ctrl ctrl, uint32_t hres, uint32_t fetchstate)
{
    int32_t k = lo;
    uint32_t base = (fboffset >> 1) + pixels + (uint32_t)lo - 1;
    uint32_t last = base + (uint32_t)(hi - lo);
    int aa_reads_cvg = ctrl.aa_mode <= VI_AA_RESAMP_EXTRA;
    int vec_ok = hi >= lo
        && !ctrl.dither_filter_enable
        && base <= last
        && rdram_valid_idx16(last);

#if defined(AL_VI_SSE2)
    if (vec_ok)
    {
        if (base & 1)
        {
            vi_fetch_filter16(&cache[k], fboffset, pixels + (uint32_t)k - 1, ctrl, hres, fetchstate);
            k++;
            base++;
        }
        while (k + 7 <= hi)
        {
            __m128i raw = _mm_loadu_si128((const __m128i*)&rdram16[base]);
            __m128i pix = _mm_shufflehi_epi16(_mm_shufflelo_epi16(raw, 0xB1), 0xB1);
            __m128i a16 = _mm_set1_epi16(7);

            uint32_t fullbits = 0xffffu;
            if (aa_reads_cvg)
            {
                __m128i h8 = _mm_loadl_epi64((const __m128i*)&rdram_hidden[base]);
                __m128i h16 = _mm_unpacklo_epi8(h8, _mm_setzero_si128());
                __m128i cvg = _mm_or_si128(_mm_slli_epi16(_mm_and_si128(pix, _mm_set1_epi16(1)), 2), h16);
                __m128i full = _mm_cmpeq_epi16(cvg, a16);
                fullbits = (uint32_t)_mm_movemask_epi8(full);
            }

            {
                __m128i m = _mm_set1_epi16(0xf8);
                __m128i r16 = _mm_and_si128(_mm_srli_epi16(pix, 8), m);
                __m128i g16 = _mm_and_si128(_mm_srli_epi16(pix, 3), m);
                __m128i b16 = _mm_and_si128(_mm_slli_epi16(pix, 2), m);
                __m128i lo16 = _mm_or_si128(b16, _mm_slli_epi16(g16, 8));
                __m128i hi16 = _mm_or_si128(r16, _mm_slli_epi16(a16, 8));
                _mm_storeu_si128((__m128i*)&cache[k], _mm_unpacklo_epi16(lo16, hi16));
                _mm_storeu_si128((__m128i*)&cache[k + 4], _mm_unpackhi_epi16(lo16, hi16));
            }
            if (fullbits != 0xffffu)
            {
                int32_t e;
                for (e = 0; e < 8; e++)
                    if (((fullbits >> (2 * e)) & 3u) != 3u)
                        vi_fetch_filter16(&cache[k + e], fboffset, pixels + (uint32_t)(k + e) - 1, ctrl, hres, fetchstate);
            }
            k += 8;
            base += 8;
        }
    }
#elif defined(AL_VI_NEON)
    if (vec_ok)
    {
        if (base & 1)
        {
            vi_fetch_filter16(&cache[k], fboffset, pixels + (uint32_t)k - 1, ctrl, hres, fetchstate);
            k++;
            base++;
        }
        while (k + 7 <= hi)
        {
            uint16x8_t raw = vld1q_u16(&rdram16[base]);
            uint16x8_t pix = vrev32q_u16(raw);
            uint16x8_t a16 = vdupq_n_u16(7);

            uint16_t fullbuf[8];
            int allfull = 1;
            if (aa_reads_cvg)
            {
                uint16x8_t h16 = vmovl_u8(vld1_u8(&rdram_hidden[base]));
                uint16x8_t cvg = vorrq_u16(vshlq_n_u16(vandq_u16(pix, vdupq_n_u16(1)), 2), h16);
                uint16x8_t full = vceqq_u16(cvg, a16);
                allfull = vminvq_u16(full) == 0xffff;
                if (!allfull)
                    vst1q_u16(fullbuf, full);
            }

            {
                uint16x8_t m = vdupq_n_u16(0xf8);
                uint16x8_t r16 = vandq_u16(vshrq_n_u16(pix, 8), m);
                uint16x8_t g16 = vandq_u16(vshrq_n_u16(pix, 3), m);
                uint16x8_t b16 = vandq_u16(vshlq_n_u16(pix, 2), m);
                uint16x8_t lo16 = vorrq_u16(b16, vshlq_n_u16(g16, 8));
                uint16x8_t hi16 = vorrq_u16(r16, vshlq_n_u16(a16, 8));
                uint16x8x2_t z = vzipq_u16(lo16, hi16);
                vst1q_u8((uint8_t*)&cache[k], vreinterpretq_u8_u16(z.val[0]));
                vst1q_u8((uint8_t*)&cache[k + 4], vreinterpretq_u8_u16(z.val[1]));
            }
            if (!allfull)
            {
                int32_t e;
                for (e = 0; e < 8; e++)
                    if (fullbuf[e] != 0xffff)
                        vi_fetch_filter16(&cache[k + e], fboffset, pixels + (uint32_t)(k + e) - 1, ctrl, hres, fetchstate);
            }
            k += 8;
            base += 8;
        }
    }
#else
    (void)vec_ok;
    (void)aa_reads_cvg;
    (void)base;
    (void)last;
#endif

    for (; k <= hi; k++)
        vi_fetch_filter16(&cache[k], fboffset, pixels + (uint32_t)k - 1, ctrl, hres, fetchstate);
}


/* Batched divot pass over one cache row: divot[k] is the per-channel
 * median of viaa[k-1], viaa[k], viaa[k+1] unless all three carry
 * full coverage, in which case the center passes through; the alpha
 * channel always carries the center's coverage. The branchy
 * pick-left/pick-right chain in divot_filter selects exactly the
 * median value (ties included), so the vector form is a byte-wise
 * max(min(L,C), min(max(L,C), R)) with a per-pixel alpha-lane
 * compare for the full-coverage gate. Four pixels per iteration;
 * the head and tail run the scalar filter. */
static void vi_fill_divot_row(struct rgba* dcache, const struct rgba* acache, int32_t lo, int32_t hi)
{
    int32_t k = lo;

#if defined(AL_VI_SSE2)
    while (k + 3 <= hi)
    {
        __m128i c = _mm_loadu_si128((const __m128i*)&acache[k]);
        __m128i l = _mm_loadu_si128((const __m128i*)&acache[k - 1]);
        __m128i r = _mm_loadu_si128((const __m128i*)&acache[k + 1]);
        __m128i mn = _mm_min_epu8(l, c);
        __m128i mx = _mm_max_epu8(l, c);
        __m128i med = _mm_max_epu8(mn, _mm_min_epu8(mx, r));
        __m128i and3 = _mm_and_si128(_mm_and_si128(c, l), r);
        __m128i amask = _mm_set1_epi32(0xff000000);
        __m128i keep = _mm_cmpeq_epi32(_mm_and_si128(and3, amask), _mm_set1_epi32(0x07000000));
        __m128i res = _mm_or_si128(_mm_and_si128(keep, c), _mm_andnot_si128(keep, med));
        res = _mm_or_si128(_mm_andnot_si128(amask, res), _mm_and_si128(c, amask));
        _mm_storeu_si128((__m128i*)&dcache[k], res);
        k += 4;
    }
#elif defined(AL_VI_NEON)
    while (k + 3 <= hi)
    {
        uint8x16_t c = vld1q_u8((const uint8_t*)&acache[k]);
        uint8x16_t l = vld1q_u8((const uint8_t*)&acache[k - 1]);
        uint8x16_t r = vld1q_u8((const uint8_t*)&acache[k + 1]);
        uint8x16_t mn = vminq_u8(l, c);
        uint8x16_t mx = vmaxq_u8(l, c);
        uint8x16_t med = vmaxq_u8(mn, vminq_u8(mx, r));
        uint32x4_t and3 = vandq_u32(vandq_u32(vreinterpretq_u32_u8(c), vreinterpretq_u32_u8(l)), vreinterpretq_u32_u8(r));
        uint32x4_t amask = vdupq_n_u32(0xff000000);
        uint32x4_t keep = vceqq_u32(vandq_u32(and3, amask), vdupq_n_u32(0x07000000));
        uint8x16_t res = vbslq_u8(vreinterpretq_u8_u32(keep), c, med);
        res = vbslq_u8(vreinterpretq_u8_u32(amask), c, res);
        vst1q_u8((uint8_t*)&dcache[k], res);
        k += 4;
    }
#endif

    for (; k <= hi; k++)
        divot_filter(&dcache[k], acache[k], acache[k - 1], acache[k + 1]);
}


/* Batched resample/lerp pass producing one output row from the
 * prefilled (and possibly divot-filtered) cache rows. vi_vl_lerp's
 * blend is the identity at frac 0, so the per-pixel lerping test
 * vanishes and the bilinear runs branchless: the three blends
 * compute a + (((b - a) * frac + 16) >> 5) in signed 16-bit lanes
 * (the result provably stays in 0..255, so the byte pack is exact),
 * with the alpha channel carried from the center source exactly as
 * the scalar path leaves it untouched. Source pixels gather through
 * the resample stepping, so any x_add works; VI_AA_REPLICATE
 * reduces to a gathered copy. Pixels outside the horizontal pass
 * bounds zero r/g/b in place and preserve the prescale alpha, as
 * the scalar store does. */
static void vi_lerp_row(struct rgba* row, int32_t hres, int32_t minh, int32_t maxh, const struct rgba* src, const struct rgba* srcn, uint32_t x_offs0, uint32_t x_add, uint32_t yfrac, int do_lerp)
{
    int32_t lo = minh > 0 ? minh : 0;
    int32_t hi = maxh < hres ? maxh : hres;
    int32_t x;
    uint32_t xo;

    for (x = 0; x < lo && x < hres; x++)
        row[x].r = row[x].g = row[x].b = 0;
    for (x = hi > lo ? hi : lo; x < hres; x++)
        row[x].r = row[x].g = row[x].b = 0;

    x = lo;
    xo = x_offs0 + (uint32_t)lo * x_add;

    /* When the x stepping and start offset carry no fractional bits
     * and the row's y fraction is zero, every per-pixel blend is the
     * identity; demote to the copy path instead of computing it. */
    if (do_lerp && yfrac == 0 && (x_offs0 & 0x3ff) == 0 && (x_add & 0x3ff) == 0)
        do_lerp = 0;

    /* 1:1 stepping copies a contiguous source span. */
    if (!do_lerp && x_add == 0x400 && x < hi)
    {
        memcpy(&row[x], &src[(int32_t)(xo >> 10) + 1], (size_t)(hi - x) * sizeof(struct rgba));
        return;
    }

#if defined(AL_VI_SSE2)
    {
        __m128i zero = _mm_setzero_si128();
        __m128i yf = _mm_set1_epi16((short)yfrac);
        __m128i rnd = _mm_set1_epi16(16);
        __m128i amask = _mm_set1_epi32(0xff000000);
        while (x + 3 < hi)
        {
            int32_t lx0 = (int32_t)(xo >> 10) + 1;
            int32_t xf0 = (int32_t)((xo >> 5) & 0x1f);
            int32_t lx1 = (int32_t)((xo + x_add) >> 10) + 1;
            int32_t xf1 = (int32_t)(((xo + x_add) >> 5) & 0x1f);
            int32_t lx2 = (int32_t)((xo + 2 * x_add) >> 10) + 1;
            int32_t xf2 = (int32_t)(((xo + 2 * x_add) >> 5) & 0x1f);
            int32_t lx3 = (int32_t)((xo + 3 * x_add) >> 10) + 1;
            int32_t xf3 = (int32_t)(((xo + 3 * x_add) >> 5) & 0x1f);
            __m128i c = _mm_set_epi32(*(const int32_t*)&src[lx3], *(const int32_t*)&src[lx2],
                                      *(const int32_t*)&src[lx1], *(const int32_t*)&src[lx0]);
            if (do_lerp)
            {
                __m128i cn = _mm_set_epi32(*(const int32_t*)&src[lx3 + 1], *(const int32_t*)&src[lx2 + 1],
                                           *(const int32_t*)&src[lx1 + 1], *(const int32_t*)&src[lx0 + 1]);
                __m128i xf_lo = _mm_set_epi16((short)xf1, (short)xf1, (short)xf1, (short)xf1,
                                              (short)xf0, (short)xf0, (short)xf0, (short)xf0);
                __m128i xf_hi = _mm_set_epi16((short)xf3, (short)xf3, (short)xf3, (short)xf3,
                                              (short)xf2, (short)xf2, (short)xf2, (short)xf2);
#define VI_LERP16(a, b, f) _mm_add_epi16(a, _mm_srai_epi16(_mm_add_epi16(_mm_mullo_epi16(_mm_sub_epi16(b, a), f), rnd), 5))
                __m128i c_lo = _mm_unpacklo_epi8(c, zero);
                __m128i c_hi = _mm_unpackhi_epi8(c, zero);
                __m128i cn_lo = _mm_unpacklo_epi8(cn, zero);
                __m128i cn_hi = _mm_unpackhi_epi8(cn, zero);
                __m128i cy_lo, cy_hi, ny_lo, ny_hi, o_lo, o_hi;
                /* yfrac == 0 keeps the vertical blends as identities and
                 * the next-scanline cache unread (it may be unfilled). */
                if (yfrac == 0)
                {
                    cy_lo = c_lo;
                    cy_hi = c_hi;
                    ny_lo = cn_lo;
                    ny_hi = cn_hi;
                }
                else
                {
                    __m128i d = _mm_set_epi32(*(const int32_t*)&srcn[lx3], *(const int32_t*)&srcn[lx2],
                                              *(const int32_t*)&srcn[lx1], *(const int32_t*)&srcn[lx0]);
                    __m128i dn = _mm_set_epi32(*(const int32_t*)&srcn[lx3 + 1], *(const int32_t*)&srcn[lx2 + 1],
                                               *(const int32_t*)&srcn[lx1 + 1], *(const int32_t*)&srcn[lx0 + 1]);
                    __m128i d_lo = _mm_unpacklo_epi8(d, zero);
                    __m128i d_hi = _mm_unpackhi_epi8(d, zero);
                    __m128i dn_lo = _mm_unpacklo_epi8(dn, zero);
                    __m128i dn_hi = _mm_unpackhi_epi8(dn, zero);
                    cy_lo = VI_LERP16(c_lo, d_lo, yf);
                    cy_hi = VI_LERP16(c_hi, d_hi, yf);
                    ny_lo = VI_LERP16(cn_lo, dn_lo, yf);
                    ny_hi = VI_LERP16(cn_hi, dn_hi, yf);
                }
                o_lo = VI_LERP16(cy_lo, ny_lo, xf_lo);
                o_hi = VI_LERP16(cy_hi, ny_hi, xf_hi);
#undef VI_LERP16
                __m128i out = _mm_packus_epi16(o_lo, o_hi);
                out = _mm_or_si128(_mm_andnot_si128(amask, out), _mm_and_si128(c, amask));
                _mm_storeu_si128((__m128i*)&row[x], out);
            }
            else
            {
                _mm_storeu_si128((__m128i*)&row[x], c);
            }
            xo += 4 * x_add;
            x += 4;
        }
    }
#elif defined(AL_VI_NEON)
    {
        int16x8_t yf = vdupq_n_s16((int16_t)yfrac);
        int16x8_t rnd = vdupq_n_s16(16);
        uint32x4_t amask = vdupq_n_u32(0xff000000);
        while (x + 3 < hi)
        {
            int32_t lx0 = (int32_t)(xo >> 10) + 1;
            int32_t xf0 = (int32_t)((xo >> 5) & 0x1f);
            int32_t lx1 = (int32_t)((xo + x_add) >> 10) + 1;
            int32_t xf1 = (int32_t)(((xo + x_add) >> 5) & 0x1f);
            int32_t lx2 = (int32_t)((xo + 2 * x_add) >> 10) + 1;
            int32_t xf2 = (int32_t)(((xo + 2 * x_add) >> 5) & 0x1f);
            int32_t lx3 = (int32_t)((xo + 3 * x_add) >> 10) + 1;
            int32_t xf3 = (int32_t)(((xo + 3 * x_add) >> 5) & 0x1f);
            uint32x4_t c = vsetq_lane_u32(*(const uint32_t*)&src[lx3],
                vsetq_lane_u32(*(const uint32_t*)&src[lx2],
                vsetq_lane_u32(*(const uint32_t*)&src[lx1],
                vdupq_n_u32(*(const uint32_t*)&src[lx0]), 1), 2), 3);
            if (do_lerp)
            {
                uint32x4_t cn = vsetq_lane_u32(*(const uint32_t*)&src[lx3 + 1],
                    vsetq_lane_u32(*(const uint32_t*)&src[lx2 + 1],
                    vsetq_lane_u32(*(const uint32_t*)&src[lx1 + 1],
                    vdupq_n_u32(*(const uint32_t*)&src[lx0 + 1]), 1), 2), 3);
                int16x8_t xf_lo = vcombine_s16(vdup_n_s16((int16_t)xf0), vdup_n_s16((int16_t)xf1));
                int16x8_t xf_hi = vcombine_s16(vdup_n_s16((int16_t)xf2), vdup_n_s16((int16_t)xf3));
#define VI_LERP16(a, b, f) vaddq_s16(a, vshrq_n_s16(vaddq_s16(vmulq_s16(vsubq_s16(b, a), f), rnd), 5))
                int16x8_t c_lo = vreinterpretq_s16_u16(vmovl_u8(vget_low_u8(vreinterpretq_u8_u32(c))));
                int16x8_t c_hi = vreinterpretq_s16_u16(vmovl_u8(vget_high_u8(vreinterpretq_u8_u32(c))));
                int16x8_t cn_lo = vreinterpretq_s16_u16(vmovl_u8(vget_low_u8(vreinterpretq_u8_u32(cn))));
                int16x8_t cn_hi = vreinterpretq_s16_u16(vmovl_u8(vget_high_u8(vreinterpretq_u8_u32(cn))));
                int16x8_t cy_lo, cy_hi, ny_lo, ny_hi, o_lo, o_hi;
                /* yfrac == 0 keeps the vertical blends as identities and
                 * the next-scanline cache unread (it may be unfilled). */
                if (yfrac == 0)
                {
                    cy_lo = c_lo;
                    cy_hi = c_hi;
                    ny_lo = cn_lo;
                    ny_hi = cn_hi;
                }
                else
                {
                    uint32x4_t d = vsetq_lane_u32(*(const uint32_t*)&srcn[lx3],
                        vsetq_lane_u32(*(const uint32_t*)&srcn[lx2],
                        vsetq_lane_u32(*(const uint32_t*)&srcn[lx1],
                        vdupq_n_u32(*(const uint32_t*)&srcn[lx0]), 1), 2), 3);
                    uint32x4_t dn = vsetq_lane_u32(*(const uint32_t*)&srcn[lx3 + 1],
                        vsetq_lane_u32(*(const uint32_t*)&srcn[lx2 + 1],
                        vsetq_lane_u32(*(const uint32_t*)&srcn[lx1 + 1],
                        vdupq_n_u32(*(const uint32_t*)&srcn[lx0 + 1]), 1), 2), 3);
                    int16x8_t d_lo = vreinterpretq_s16_u16(vmovl_u8(vget_low_u8(vreinterpretq_u8_u32(d))));
                    int16x8_t d_hi = vreinterpretq_s16_u16(vmovl_u8(vget_high_u8(vreinterpretq_u8_u32(d))));
                    int16x8_t dn_lo = vreinterpretq_s16_u16(vmovl_u8(vget_low_u8(vreinterpretq_u8_u32(dn))));
                    int16x8_t dn_hi = vreinterpretq_s16_u16(vmovl_u8(vget_high_u8(vreinterpretq_u8_u32(dn))));
                    cy_lo = VI_LERP16(c_lo, d_lo, yf);
                    cy_hi = VI_LERP16(c_hi, d_hi, yf);
                    ny_lo = VI_LERP16(cn_lo, dn_lo, yf);
                    ny_hi = VI_LERP16(cn_hi, dn_hi, yf);
                }
                o_lo = VI_LERP16(cy_lo, ny_lo, xf_lo);
                o_hi = VI_LERP16(cy_hi, ny_hi, xf_hi);
#undef VI_LERP16
                uint8x16_t out = vcombine_u8(vqmovun_s16(o_lo), vqmovun_s16(o_hi));
                out = vbslq_u8(vreinterpretq_u8_u32(amask), vreinterpretq_u8_u32(c), out);
                vst1q_u8((uint8_t*)&row[x], out);
            }
            else
            {
                vst1q_u8((uint8_t*)&row[x], vreinterpretq_u8_u32(c));
            }
            xo += 4 * x_add;
            x += 4;
        }
    }
#endif

    for (; x < hi; x++, xo += x_add)
    {
        int32_t lx = (int32_t)(xo >> 10) + 1;
        int32_t xf = (int32_t)((xo >> 5) & 0x1f);
        struct rgba color = src[lx];
        if (do_lerp && (xf || yfrac))
        {
            struct rgba nextcolor = src[lx + 1];
            if (yfrac != 0)
            {
                vi_vl_lerp(&color, srcn[lx], yfrac);
                vi_vl_lerp(&nextcolor, srcn[lx + 1], yfrac);
            }
            vi_vl_lerp(&color, nextcolor, (uint32_t)xf);
        }
        row[x] = color;
    }
}


/* Batched gamma stage over the in-bounds span of one output row.
 * The dither-only mode is exact saturating-add SIMD: the scalar
 * guard if (c < 255) c += dith with dith in {0,1} is precisely a
 * saturating byte add, with the random bits spread to their channel
 * byte lanes. The gamma modes are bound by their per-channel table
 * gathers, so they run as tight four-wide unrolled loops over the
 * whole-pixel words, keeping the LCG advance inline and exact. The
 * seed advances once per pixel in row order in every mode, matching
 * the scalar sequence. */
static void vi_gamma_row(struct rgba* row, int32_t lo, int32_t hi, bool gamma, bool gdither, uint32_t* rstate)
{
    int32_t x = lo;
    uint32_t st = *rstate;

    switch (((int)gamma << 1) | (int)gdither)
    {
    case 0:
        return;

    case 1:
#if defined(AL_VI_SSE2)
        {
            while (x + 3 < hi)
            {
                __m128i pix = _mm_loadu_si128((__m128i*)&row[x]);
                uint32_t c0, c1, c2, c3;
                __m128i cd, dith;
                st = st * 0x343fd + 0x269ec3;
                c0 = (st >> 16) & 0x7fff;
                st = st * 0x343fd + 0x269ec3;
                c1 = (st >> 16) & 0x7fff;
                st = st * 0x343fd + 0x269ec3;
                c2 = (st >> 16) & 0x7fff;
                st = st * 0x343fd + 0x269ec3;
                c3 = (st >> 16) & 0x7fff;
                cd = _mm_set_epi32((int)c3, (int)c2, (int)c1, (int)c0);
                /* cdith bit 0 -> r (byte 2), bit 1 -> g (byte 1), bit 2 -> b
                 * (byte 0); each bit isolated before its shift */
                dith = _mm_or_si128(_mm_slli_epi32(_mm_and_si128(cd, _mm_set1_epi32(1)), 16),
                       _mm_or_si128(_mm_slli_epi32(_mm_and_si128(cd, _mm_set1_epi32(2)), 7),
                                    _mm_srli_epi32(_mm_and_si128(cd, _mm_set1_epi32(4)), 2)));
                pix = _mm_adds_epu8(pix, dith);
                _mm_storeu_si128((__m128i*)&row[x], pix);
                x += 4;
            }
        }
#elif defined(AL_VI_NEON)
        {
            while (x + 3 < hi)
            {
                uint8x16_t pix = vld1q_u8((uint8_t*)&row[x]);
                uint32_t c0, c1, c2, c3;
                uint32x4_t cd, dith;
                st = st * 0x343fd + 0x269ec3;
                c0 = (st >> 16) & 0x7fff;
                st = st * 0x343fd + 0x269ec3;
                c1 = (st >> 16) & 0x7fff;
                st = st * 0x343fd + 0x269ec3;
                c2 = (st >> 16) & 0x7fff;
                st = st * 0x343fd + 0x269ec3;
                c3 = (st >> 16) & 0x7fff;
                cd = vsetq_lane_u32(c3, vsetq_lane_u32(c2, vsetq_lane_u32(c1, vdupq_n_u32(c0), 1), 2), 3);
                dith = vorrq_u32(vshlq_n_u32(vandq_u32(cd, vdupq_n_u32(1)), 16),
                       vorrq_u32(vshlq_n_u32(vandq_u32(cd, vdupq_n_u32(2)), 7),
                                 vshrq_n_u32(vandq_u32(cd, vdupq_n_u32(4)), 2)));
                pix = vqaddq_u8(pix, vreinterpretq_u8_u32(dith));
                vst1q_u8((uint8_t*)&row[x], pix);
                x += 4;
            }
        }
#endif
        for (; x < hi; x++)
        {
            uint32_t cdith = (uint32_t)((st = st * 0x343fd + 0x269ec3) >> 16) & 0x7fff;
            uint32_t dith = cdith & 1;
            if (row[x].r < 255)
                row[x].r += dith;
            dith = (cdith >> 1) & 1;
            if (row[x].g < 255)
                row[x].g += dith;
            dith = (cdith >> 2) & 1;
            if (row[x].b < 255)
                row[x].b += dith;
        }
        break;

    case 2:
        for (; x + 3 < hi; x += 4)
        {
            row[x].r = gamma_table[row[x].r];
            row[x].g = gamma_table[row[x].g];
            row[x].b = gamma_table[row[x].b];
            row[x + 1].r = gamma_table[row[x + 1].r];
            row[x + 1].g = gamma_table[row[x + 1].g];
            row[x + 1].b = gamma_table[row[x + 1].b];
            row[x + 2].r = gamma_table[row[x + 2].r];
            row[x + 2].g = gamma_table[row[x + 2].g];
            row[x + 2].b = gamma_table[row[x + 2].b];
            row[x + 3].r = gamma_table[row[x + 3].r];
            row[x + 3].g = gamma_table[row[x + 3].g];
            row[x + 3].b = gamma_table[row[x + 3].b];
        }
        for (; x < hi; x++)
        {
            row[x].r = gamma_table[row[x].r];
            row[x].g = gamma_table[row[x].g];
            row[x].b = gamma_table[row[x].b];
        }
        break;

    case 3:
        for (; x < hi; x++)
        {
            uint32_t cdith = (uint32_t)((st = st * 0x343fd + 0x269ec3) >> 16) & 0x7fff;
            row[x].r = gamma_dither_table[((uint32_t)row[x].r << 6) | (cdith & 0x3f)];
            row[x].g = gamma_dither_table[((uint32_t)row[x].g << 6) | ((cdith >> 6) & 0x3f)];
            row[x].b = gamma_dither_table[((uint32_t)row[x].b << 6) | (((cdith >> 9) & 0x38) | (cdith & 7))];
        }
        break;
    }

    *rstate = st;
}
