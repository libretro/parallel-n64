//
// arch/x86_64/rsp/vor.h
//
// This file is subject to the terms and conditions defined in
// 'LICENSE', which is part of this source code package.
//

static inline __m128i rsp_vor(__m128i vs, __m128i vt) {
  return _mm_or_si128(vs, vt);
}

static inline __m128i rsp_vnor(__m128i vs, __m128i vt) {
  __m128i vd = _mm_or_si128(vs, vt);
  return _mm_xor_si128(vd, _mm_set1_epi32(0xffffffffu));
}

