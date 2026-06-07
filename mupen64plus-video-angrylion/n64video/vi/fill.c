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
