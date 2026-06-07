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

            if (aa_reads_cvg)
            {
                __m128i h8 = _mm_loadl_epi64((const __m128i*)&rdram_hidden[base]);
                __m128i h16 = _mm_unpacklo_epi8(h8, _mm_setzero_si128());
                __m128i cvg = _mm_or_si128(_mm_slli_epi16(_mm_and_si128(pix, _mm_set1_epi16(1)), 2), h16);
                __m128i full = _mm_cmpeq_epi16(cvg, a16);
                if (_mm_movemask_epi8(full) != 0xffff)
                {
                    int32_t e;
                    for (e = 0; e < 8; e++, k++)
                        vi_fetch_filter16(&cache[k], fboffset, pixels + (uint32_t)k - 1, ctrl, hres, fetchstate);
                    base += 8;
                    continue;
                }
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

            if (aa_reads_cvg)
            {
                uint16x8_t h16 = vmovl_u8(vld1_u8(&rdram_hidden[base]));
                uint16x8_t cvg = vorrq_u16(vshlq_n_u16(vandq_u16(pix, vdupq_n_u16(1)), 2), h16);
                uint16x8_t full = vceqq_u16(cvg, a16);
                if (vminvq_u16(full) != 0xffff)
                {
                    int32_t e;
                    for (e = 0; e < 8; e++, k++)
                        vi_fetch_filter16(&cache[k], fboffset, pixels + (uint32_t)k - 1, ctrl, hres, fetchstate);
                    base += 8;
                    continue;
                }
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
                __m128i d = _mm_set_epi32(*(const int32_t*)&srcn[lx3], *(const int32_t*)&srcn[lx2],
                                          *(const int32_t*)&srcn[lx1], *(const int32_t*)&srcn[lx0]);
                __m128i dn = _mm_set_epi32(*(const int32_t*)&srcn[lx3 + 1], *(const int32_t*)&srcn[lx2 + 1],
                                           *(const int32_t*)&srcn[lx1 + 1], *(const int32_t*)&srcn[lx0 + 1]);
                __m128i xf_lo = _mm_set_epi16((short)xf1, (short)xf1, (short)xf1, (short)xf1,
                                              (short)xf0, (short)xf0, (short)xf0, (short)xf0);
                __m128i xf_hi = _mm_set_epi16((short)xf3, (short)xf3, (short)xf3, (short)xf3,
                                              (short)xf2, (short)xf2, (short)xf2, (short)xf2);
#define VI_LERP16(a, b, f) _mm_add_epi16(a, _mm_srai_epi16(_mm_add_epi16(_mm_mullo_epi16(_mm_sub_epi16(b, a), f), rnd), 5))
                __m128i c_lo = _mm_unpacklo_epi8(c, zero);
                __m128i c_hi = _mm_unpackhi_epi8(c, zero);
                __m128i cn_lo = _mm_unpacklo_epi8(cn, zero);
                __m128i cn_hi = _mm_unpackhi_epi8(cn, zero);
                __m128i d_lo = _mm_unpacklo_epi8(d, zero);
                __m128i d_hi = _mm_unpackhi_epi8(d, zero);
                __m128i dn_lo = _mm_unpacklo_epi8(dn, zero);
                __m128i dn_hi = _mm_unpackhi_epi8(dn, zero);
                __m128i cy_lo = VI_LERP16(c_lo, d_lo, yf);
                __m128i cy_hi = VI_LERP16(c_hi, d_hi, yf);
                __m128i ny_lo = VI_LERP16(cn_lo, dn_lo, yf);
                __m128i ny_hi = VI_LERP16(cn_hi, dn_hi, yf);
                __m128i o_lo = VI_LERP16(cy_lo, ny_lo, xf_lo);
                __m128i o_hi = VI_LERP16(cy_hi, ny_hi, xf_hi);
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
                uint32x4_t d = vsetq_lane_u32(*(const uint32_t*)&srcn[lx3],
                    vsetq_lane_u32(*(const uint32_t*)&srcn[lx2],
                    vsetq_lane_u32(*(const uint32_t*)&srcn[lx1],
                    vdupq_n_u32(*(const uint32_t*)&srcn[lx0]), 1), 2), 3);
                uint32x4_t dn = vsetq_lane_u32(*(const uint32_t*)&srcn[lx3 + 1],
                    vsetq_lane_u32(*(const uint32_t*)&srcn[lx2 + 1],
                    vsetq_lane_u32(*(const uint32_t*)&srcn[lx1 + 1],
                    vdupq_n_u32(*(const uint32_t*)&srcn[lx0 + 1]), 1), 2), 3);
                int16x8_t xf_lo = vcombine_s16(vdup_n_s16((int16_t)xf0), vdup_n_s16((int16_t)xf1));
                int16x8_t xf_hi = vcombine_s16(vdup_n_s16((int16_t)xf2), vdup_n_s16((int16_t)xf3));
#define VI_LERP16(a, b, f) vaddq_s16(a, vshrq_n_s16(vaddq_s16(vmulq_s16(vsubq_s16(b, a), f), rnd), 5))
                int16x8_t c_lo = vreinterpretq_s16_u16(vmovl_u8(vget_low_u8(vreinterpretq_u8_u32(c))));
                int16x8_t c_hi = vreinterpretq_s16_u16(vmovl_u8(vget_high_u8(vreinterpretq_u8_u32(c))));
                int16x8_t cn_lo = vreinterpretq_s16_u16(vmovl_u8(vget_low_u8(vreinterpretq_u8_u32(cn))));
                int16x8_t cn_hi = vreinterpretq_s16_u16(vmovl_u8(vget_high_u8(vreinterpretq_u8_u32(cn))));
                int16x8_t d_lo = vreinterpretq_s16_u16(vmovl_u8(vget_low_u8(vreinterpretq_u8_u32(d))));
                int16x8_t d_hi = vreinterpretq_s16_u16(vmovl_u8(vget_high_u8(vreinterpretq_u8_u32(d))));
                int16x8_t dn_lo = vreinterpretq_s16_u16(vmovl_u8(vget_low_u8(vreinterpretq_u8_u32(dn))));
                int16x8_t dn_hi = vreinterpretq_s16_u16(vmovl_u8(vget_high_u8(vreinterpretq_u8_u32(dn))));
                int16x8_t cy_lo = VI_LERP16(c_lo, d_lo, yf);
                int16x8_t cy_hi = VI_LERP16(c_hi, d_hi, yf);
                int16x8_t ny_lo = VI_LERP16(cn_lo, dn_lo, yf);
                int16x8_t ny_hi = VI_LERP16(cn_hi, dn_hi, yf);
                int16x8_t o_lo = VI_LERP16(cy_lo, ny_lo, xf_lo);
                int16x8_t o_hi = VI_LERP16(cy_hi, ny_hi, xf_hi);
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
            vi_vl_lerp(&color, srcn[lx], yfrac);
            vi_vl_lerp(&nextcolor, srcn[lx + 1], yfrac);
            vi_vl_lerp(&color, nextcolor, (uint32_t)xf);
        }
        row[x] = color;
    }
}
