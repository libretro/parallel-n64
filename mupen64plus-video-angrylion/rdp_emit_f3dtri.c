/* Fast3D (GBI1) triangle setup for the angrylion HLE path.
 *
 * The 1996-97 Fast3D microcodes (Mario Kart 64; Rare's GoldenEye and
 * Perfect Dark builds) compute the RDP edge, shade, texture and depth
 * coefficients with a different algorithm from the F3DEX family that
 * rsp_tri_write models: the slopes come from single-precision reciprocals
 * of the edge heights, the attribute gradients use a Newton-refined
 * double-precision reciprocal of the cross product, the texture
 * coefficients are renormalized by the largest texture magnitude, and
 * the vertex clip W (times perspNorm) rather than 1/w selects the
 * texture weights. Rather than a closed form, this file evaluates the
 * setup the way the vector unit does: a small lane-level model of the
 * RSP vector unit (accumulator, carry/compare/clip flags, divide unit)
 * runs the setup over a local DMEM image holding the vertex records, the
 * microcode's constant rows, its state block and its scratch area, and
 * the coefficient block is read back from the local output buffer.
 *
 * Two revisions are modelled:
 *   F3D_TRI_REV_B: GoldenEye / Perfect Dark: scalar insertion sort on Y,
 *                  anchor fraction from the top vertex's Y register, depth
 *                  clamp from G_TEXTURE's parameter byte; the whole
 *                  triangle command is modelled -- the trivial reject on
 *                  the vertex clip flags, the six-plane clipper with its
 *                  rotated output list, the vertex write for generated
 *                  vertices and the fan into the triangle write;
 *   F3D_TRI_REV_A: Mario Kart 64: table-driven sort, cull and degenerate
 *                  tests on the saturated cross, anchor fraction from the
 *                  top vertex vector, texture normalizer scale from the
 *                  data row; the same clipper and fan, with the near
 *                  plane on z + w, z in the screen-plane outcodes and no
 *                  w-sign bits in the vertex flags.
 * GoldenEye and Mario Kart 64 take the flat-shade colour from a chosen
 * command vertex; Perfect Dark's build always interpolates.
 *
 * The vector unit semantics follow the public-domain cxd4 RSP model
 * (mupen64plus-rsp-cxd4/vu), against which the output was validated: the
 * vertex records and coefficient blocks of LLE runs, fed through this
 * model, come out word for word. */

#include <stdint.h>
#include <string.h>
#include <stddef.h>
/* F3D_NO_SSE2 selects the portable lane loops (the tests build both) */
#if !defined(F3D_NO_SSE2) && (defined(__SSE2__) || defined(_M_X64) \
    || defined(_M_AMD64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2))
#include <emmintrin.h>
#define F3D_SSE2 1
#endif
#include "rdp_emit_rsp.h"
#include "rdp_emit_f3dtri.h"

/* the vector ops inline into the setup, so each element select and
 * operation folds to its own short sequence */
#if defined(__GNUC__) || defined(__clang__)
#define VU_OP static __inline__ __attribute__((always_inline))
#elif defined(_MSC_VER)
#define VU_OP static __forceinline
#else
#define VU_OP static
#endif

/* ---- lane-level vector unit ------------------------------------------ */

typedef struct F3dVu
{
    int16_t v[32][8];
    /* the 48-bit accumulator as three 16-bit slices: high, middle, low */
    int16_t acch[8], accm[8], accl[8];
    /* flags, one 0/1 lane each */
    uint16_t co[8], ne[8];      /* VCO carry / not-equal */
    uint16_t comp[8], clip[8];  /* VCC compare / clip */
    uint16_t vce[8];
    int32_t divin, divout;
    int     dph;
    int     rev;                /* F3D_TRI_* build */
    uint8_t dmem[0x1000];
    /* past dmem, out of the setup image copy: the 64-byte DMEM blocks
     * written since the last setup, and which image the unit holds */
    uint64_t dirty;
    unsigned int image_gen;
} F3dVu;

#define DM_DIRTY(u, a) ((u)->dirty |= (uint64_t)1 << (((a) & 0xfffu) >> 6))

/* element select: whole vector, quarters, halves, or one lane */
static const uint8_t k_sel[16][8] =
{
    { 0, 1, 2, 3, 4, 5, 6, 7 }, { 0, 1, 2, 3, 4, 5, 6, 7 },
    { 0, 0, 2, 2, 4, 4, 6, 6 }, { 1, 1, 3, 3, 5, 5, 7, 7 },
    { 0, 0, 0, 0, 4, 4, 4, 4 }, { 1, 1, 1, 1, 5, 5, 5, 5 },
    { 2, 2, 2, 2, 6, 6, 6, 6 }, { 3, 3, 3, 3, 7, 7, 7, 7 },
    { 0, 0, 0, 0, 0, 0, 0, 0 }, { 1, 1, 1, 1, 1, 1, 1, 1 },
    { 2, 2, 2, 2, 2, 2, 2, 2 }, { 3, 3, 3, 3, 3, 3, 3, 3 },
    { 4, 4, 4, 4, 4, 4, 4, 4 }, { 5, 5, 5, 5, 5, 5, 5, 5 },
    { 6, 6, 6, 6, 6, 6, 6, 6 }, { 7, 7, 7, 7, 7, 7, 7, 7 }
};

#define vu_sel(e, i) (k_sel[(e) & 15][(i)])

#ifndef F3D_SSE2
/* the vt operand with its element select applied */
VU_OP void vu_tsel(const F3dVu *u, int16_t t[8], int vt, int e)
{
    const int16_t *tv = u->v[vt];
    const uint8_t *sl = k_sel[e & 15];
    int i;
    for (i = 0; i < 8; i++)
        t[i] = tv[sl[i]];
}
#endif

static int16_t acc_l(const F3dVu *u, int i) { return u->accl[i]; }
static int16_t acc_m(const F3dVu *u, int i) { return u->accm[i]; }
static int16_t acc_h(const F3dVu *u, int i) { return u->acch[i]; }

/* The multiplies, one function each, on the sliced accumulator: the
 * products, accumulated (vmad*) or not (vmud*), wrap at 48 bits; then
 * the low slice (vmudl/vmudn), the middle slice (vmudm) or the clamped
 * readouts. The lane loops keep to 16- and 32-bit lanes so they
 * vectorize. The result goes through r: vd may name a source. */

#ifdef F3D_SSE2
/* SSE2: the same lanes eight at a time. Unsigned compares go through a
 * sign flip; a carry is the sum's unsigned compare below an addend. */
#define VLD(p)     _mm_loadu_si128((const __m128i *)(const void *)(p))
#define VST(p, x)  _mm_storeu_si128((__m128i *)(void *)(p), (x))

VU_OP __m128i vx_sel(const F3dVu *u, int vt, int e)
{
    __m128i v = VLD(u->v[vt]), x;
    switch (e & 15)
    {
    case 2:  x = _mm_shufflelo_epi16(v, 0xa0); return _mm_shufflehi_epi16(x, 0xa0);
    case 3:  x = _mm_shufflelo_epi16(v, 0xf5); return _mm_shufflehi_epi16(x, 0xf5);
    case 4:  x = _mm_shufflelo_epi16(v, 0x00); return _mm_shufflehi_epi16(x, 0x00);
    case 5:  x = _mm_shufflelo_epi16(v, 0x55); return _mm_shufflehi_epi16(x, 0x55);
    case 6:  x = _mm_shufflelo_epi16(v, 0xaa); return _mm_shufflehi_epi16(x, 0xaa);
    case 7:  x = _mm_shufflelo_epi16(v, 0xff); return _mm_shufflehi_epi16(x, 0xff);
    case 8:  x = _mm_shufflelo_epi16(v, 0x00); return _mm_unpacklo_epi64(x, x);
    case 9:  x = _mm_shufflelo_epi16(v, 0x55); return _mm_unpacklo_epi64(x, x);
    case 10: x = _mm_shufflelo_epi16(v, 0xaa); return _mm_unpacklo_epi64(x, x);
    case 11: x = _mm_shufflelo_epi16(v, 0xff); return _mm_unpacklo_epi64(x, x);
    case 12: x = _mm_shufflehi_epi16(v, 0x00); return _mm_unpackhi_epi64(x, x);
    case 13: x = _mm_shufflehi_epi16(v, 0x55); return _mm_unpackhi_epi64(x, x);
    case 14: x = _mm_shufflehi_epi16(v, 0xaa); return _mm_unpackhi_epi64(x, x);
    case 15: x = _mm_shufflehi_epi16(v, 0xff); return _mm_unpackhi_epi64(x, x);
    default: return v;
    }
}

/* -1 where a + b carried out of 16 bits (sum = a + b) */
VU_OP __m128i vx_carry(__m128i sum, __m128i b)
{
    const __m128i k = _mm_set1_epi16((short)0x8000);
    return _mm_cmplt_epi16(_mm_xor_si128(sum, k), _mm_xor_si128(b, k));
}

/* signed clamp of bits 47..16 */
VU_OP __m128i vx_clamp_am(__m128i h, __m128i m)
{
    return _mm_packs_epi32(_mm_unpacklo_epi16(m, h), _mm_unpackhi_epi16(m, h));
}

/* the low slice, or 0x0000/0xffff where bits 47..16 clamp */
VU_OP __m128i vx_clamp_al(__m128i h, __m128i m, __m128i l)
{
    __m128i t = vx_clamp_am(h, m);
    __m128i c = _mm_cmpeq_epi16(t, m);
    __m128i f = _mm_xor_si128(t, _mm_set1_epi16((short)0x8000));
    return _mm_or_si128(_mm_and_si128(c, l), _mm_andnot_si128(c, f));
}

/* the 32-bit products as (hi, lo) slices: signed x signed, signed x
 * unsigned t, unsigned s x signed t */
#define VX_PROD_SS(s, t, hi, lo) \
    do { lo = _mm_mullo_epi16(s, t); hi = _mm_mulhi_epi16(s, t); } while (0)
#define VX_PROD_SU(s, t, hi, lo) \
    do { lo = _mm_mullo_epi16(s, t); \
         hi = _mm_add_epi16(_mm_mulhi_epi16(s, t), \
                            _mm_and_si128(s, _mm_srai_epi16(t, 15))); } while (0)
#define VX_PROD_US(s, t, hi, lo) VX_PROD_SU(t, s, hi, lo)

/* accumulator += (hi:lo), sign-extended from 32 bits */
VU_OP void vx_acc_add32(F3dVu *u, __m128i hi, __m128i lo)
{
    __m128i l = VLD(u->accl), m = VLD(u->accm), h = VLD(u->acch);
    __m128i l2 = _mm_add_epi16(l, lo);
    __m128i c1 = vx_carry(l2, lo);                  /* -1 on carry */
    __m128i m1 = _mm_add_epi16(m, hi);
    __m128i ca = vx_carry(m1, hi);
    __m128i m2 = _mm_sub_epi16(m1, c1);
    __m128i cb = _mm_and_si128(c1, _mm_cmpeq_epi16(m2, _mm_setzero_si128()));
    h = _mm_add_epi16(h, _mm_srai_epi16(hi, 15));
    h = _mm_sub_epi16(h, ca);
    h = _mm_sub_epi16(h, cb);
    VST(u->accl, l2);
    VST(u->accm, m2);
    VST(u->acch, h);
}

VU_OP void vx_acc_set32(F3dVu *u, __m128i hi, __m128i lo)
{
    VST(u->accl, lo);
    VST(u->accm, hi);
    VST(u->acch, _mm_srai_epi16(hi, 15));
}

VU_OP void vmudl_(F3dVu *u, int vd, int vs, int vt, int e)
{
    __m128i p = _mm_mulhi_epu16(VLD(u->v[vs]), vx_sel(u, vt, e));
    __m128i z = _mm_setzero_si128();
    VST(u->accl, p);
    VST(u->accm, z);
    VST(u->acch, z);
    VST(u->v[vd], p);
}

VU_OP void vmudm_(F3dVu *u, int vd, int vs, int vt, int e)
{
    __m128i s = VLD(u->v[vs]), t = vx_sel(u, vt, e), hi, lo;
    VX_PROD_SU(s, t, hi, lo);
    vx_acc_set32(u, hi, lo);
    VST(u->v[vd], hi);
}

VU_OP void vmudn_(F3dVu *u, int vd, int vs, int vt, int e)
{
    __m128i s = VLD(u->v[vs]), t = vx_sel(u, vt, e), hi, lo;
    VX_PROD_US(s, t, hi, lo);
    vx_acc_set32(u, hi, lo);
    VST(u->v[vd], lo);
}

VU_OP void vmudh_(F3dVu *u, int vd, int vs, int vt, int e)
{
    __m128i s = VLD(u->v[vs]), t = vx_sel(u, vt, e), hi, lo;
    VX_PROD_SS(s, t, hi, lo);
    VST(u->accl, _mm_setzero_si128());
    VST(u->accm, lo);
    VST(u->acch, hi);
    VST(u->v[vd], vx_clamp_am(hi, lo));
}

VU_OP void vmadl_(F3dVu *u, int vd, int vs, int vt, int e)
{
    __m128i p = _mm_mulhi_epu16(VLD(u->v[vs]), vx_sel(u, vt, e));
    vx_acc_add32(u, _mm_setzero_si128(), p);
    VST(u->v[vd], vx_clamp_al(VLD(u->acch), VLD(u->accm), VLD(u->accl)));
}

VU_OP void vmadm_(F3dVu *u, int vd, int vs, int vt, int e)
{
    __m128i s = VLD(u->v[vs]), t = vx_sel(u, vt, e), hi, lo;
    VX_PROD_SU(s, t, hi, lo);
    vx_acc_add32(u, hi, lo);
    VST(u->v[vd], vx_clamp_am(VLD(u->acch), VLD(u->accm)));
}

VU_OP void vmadn_(F3dVu *u, int vd, int vs, int vt, int e)
{
    __m128i s = VLD(u->v[vs]), t = vx_sel(u, vt, e), hi, lo;
    VX_PROD_US(s, t, hi, lo);
    vx_acc_add32(u, hi, lo);
    VST(u->v[vd], vx_clamp_al(VLD(u->acch), VLD(u->accm), VLD(u->accl)));
}

VU_OP void vmadh_(F3dVu *u, int vd, int vs, int vt, int e)
{
    __m128i s = VLD(u->v[vs]), t = vx_sel(u, vt, e), hi, lo;
    __m128i m = VLD(u->accm), h = VLD(u->acch), m2;
    VX_PROD_SS(s, t, hi, lo);
    m2 = _mm_add_epi16(m, lo);
    h = _mm_sub_epi16(_mm_add_epi16(h, hi), vx_carry(m2, lo));
    VST(u->accm, m2);
    VST(u->acch, h);
    VST(u->v[vd], vx_clamp_am(h, m2));
}
#else
/* signed clamp of the accumulator's bits 47..16 */
VU_OP void vu_clamp_am(const F3dVu *u, int16_t r[8])
{
    int i;
    for (i = 0; i < 8; i++)
    {
        int32_t x = (int32_t)(((uint32_t)(uint16_t)u->acch[i] << 16)
                              | (uint16_t)u->accm[i]);
        r[i] = (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
    }
}

/* the low slice, or 0x0000/0xffff where bits 47..16 clamp */
VU_OP void vu_clamp_al(const F3dVu *u, int16_t r[8])
{
    int16_t t[8];
    int i;
    vu_clamp_am(u, t);
    for (i = 0; i < 8; i++)
        r[i] = (t[i] != u->accm[i]) ? (int16_t)(t[i] ^ 0x8000) : u->accl[i];
}

/* accumulator = p (a 32-bit product), sign-extended */
VU_OP void vu_acc_set32(F3dVu *u, const int32_t p[8])
{
    int i;
    for (i = 0; i < 8; i++)
    {
        u->accl[i] = (int16_t)p[i];
        u->accm[i] = (int16_t)(p[i] >> 16);
        u->acch[i] = (int16_t)(p[i] >> 31);
    }
}

/* accumulator += p, p sign-extended from 32 bits */
VU_OP void vu_acc_add32(F3dVu *u, const int32_t p[8])
{
    int i;
    for (i = 0; i < 8; i++)
    {
        uint32_t lo = (uint32_t)(uint16_t)u->accl[i] + ((uint32_t)p[i] & 0xffffu);
        uint32_t md = (uint32_t)(uint16_t)u->accm[i] + ((uint32_t)p[i] >> 16)
                    + (lo >> 16);
        u->accl[i] = (int16_t)lo;
        u->accm[i] = (int16_t)md;
        u->acch[i] = (int16_t)(u->acch[i] + (int16_t)(md >> 16) - (p[i] < 0));
    }
}

VU_OP void vmudl_(F3dVu *u, int vd, int vs, int vt, int e)
{
    int16_t t[8], r[8];
    const int16_t *s = u->v[vs];
    int i;
    vu_tsel(u, t, vt, e);
    for (i = 0; i < 8; i++)
    {
        r[i] = (int16_t)(((uint32_t)(uint16_t)s[i] * (uint16_t)t[i]) >> 16);
        u->accl[i] = r[i];
        u->accm[i] = 0;
        u->acch[i] = 0;
    }
    memcpy(u->v[vd], r, sizeof(r));
}

VU_OP void vmudm_(F3dVu *u, int vd, int vs, int vt, int e)
{
    int16_t t[8];
    int32_t p[8];
    const int16_t *s = u->v[vs];
    int i;
    vu_tsel(u, t, vt, e);
    for (i = 0; i < 8; i++)
        p[i] = (int32_t)s[i] * (int32_t)(uint16_t)t[i];
    vu_acc_set32(u, p);
    memcpy(u->v[vd], u->accm, sizeof(u->accm));
}

VU_OP void vmudn_(F3dVu *u, int vd, int vs, int vt, int e)
{
    int16_t t[8];
    int32_t p[8];
    const int16_t *s = u->v[vs];
    int i;
    vu_tsel(u, t, vt, e);
    for (i = 0; i < 8; i++)
        p[i] = (int32_t)(uint16_t)s[i] * (int32_t)t[i];
    vu_acc_set32(u, p);
    memcpy(u->v[vd], u->accl, sizeof(u->accl));
}

VU_OP void vmudh_(F3dVu *u, int vd, int vs, int vt, int e)
{
    int16_t t[8], r[8];
    const int16_t *s = u->v[vs];
    int i;
    vu_tsel(u, t, vt, e);
    for (i = 0; i < 8; i++)
    {
        int32_t p = (int32_t)s[i] * t[i];
        u->accl[i] = 0;
        u->accm[i] = (int16_t)p;
        u->acch[i] = (int16_t)(p >> 16);
        r[i] = (int16_t)(p > 32767 ? 32767 : p < -32768 ? -32768 : p);
    }
    memcpy(u->v[vd], r, sizeof(r));
}

VU_OP void vmadl_(F3dVu *u, int vd, int vs, int vt, int e)
{
    int16_t t[8], r[8];
    int32_t p[8];
    const int16_t *s = u->v[vs];
    int i;
    vu_tsel(u, t, vt, e);
    for (i = 0; i < 8; i++)
        p[i] = (int32_t)(((uint32_t)(uint16_t)s[i] * (uint16_t)t[i]) >> 16);
    vu_acc_add32(u, p);
    vu_clamp_al(u, r);
    memcpy(u->v[vd], r, sizeof(r));
}

VU_OP void vmadm_(F3dVu *u, int vd, int vs, int vt, int e)
{
    int16_t t[8], r[8];
    int32_t p[8];
    const int16_t *s = u->v[vs];
    int i;
    vu_tsel(u, t, vt, e);
    for (i = 0; i < 8; i++)
        p[i] = (int32_t)s[i] * (int32_t)(uint16_t)t[i];
    vu_acc_add32(u, p);
    vu_clamp_am(u, r);
    memcpy(u->v[vd], r, sizeof(r));
}

VU_OP void vmadn_(F3dVu *u, int vd, int vs, int vt, int e)
{
    int16_t t[8], r[8];
    int32_t p[8];
    const int16_t *s = u->v[vs];
    int i;
    vu_tsel(u, t, vt, e);
    for (i = 0; i < 8; i++)
        p[i] = (int32_t)(uint16_t)s[i] * (int32_t)t[i];
    vu_acc_add32(u, p);
    vu_clamp_al(u, r);
    memcpy(u->v[vd], r, sizeof(r));
}

VU_OP void vmadh_(F3dVu *u, int vd, int vs, int vt, int e)
{
    int16_t t[8], r[8];
    const int16_t *s = u->v[vs];
    int i;
    vu_tsel(u, t, vt, e);
    for (i = 0; i < 8; i++)
    {
        int32_t p = (int32_t)s[i] * t[i];
        uint32_t md = (uint32_t)(uint16_t)u->accm[i] + ((uint32_t)p & 0xffffu);
        u->accm[i] = (int16_t)md;
        u->acch[i] = (int16_t)(u->acch[i] + (int16_t)(p >> 16) + (int16_t)(md >> 16));
    }
    vu_clamp_am(u, r);
    memcpy(u->v[vd], r, sizeof(r));
}

#endif

#define vmudl(d, s, t, e) vmudl_(u, d, s, t, e)
#define vmudm(d, s, t, e) vmudm_(u, d, s, t, e)
#define vmudn(d, s, t, e) vmudn_(u, d, s, t, e)
#define vmudh(d, s, t, e) vmudh_(u, d, s, t, e)
#define vmadl(d, s, t, e) vmadl_(u, d, s, t, e)
#define vmadm(d, s, t, e) vmadm_(u, d, s, t, e)
#define vmadn(d, s, t, e) vmadn_(u, d, s, t, e)
#define vmadh(d, s, t, e) vmadh_(u, d, s, t, e)

#ifdef F3D_SSE2
/* the flag lanes as 0/1 vectors and back */
#define VFLAG(m)   _mm_and_si128((m), _mm_set1_epi16(1))

/* vadd/vsub: exact 32-bit sum (carry in), saturated; the low slice takes
 * it raw; flags cleared */
VU_OP void vx_addsub(F3dVu *u, int sub, int vd, int vs, int vt, int e)
{
    __m128i s = VLD(u->v[vs]), t = vx_sel(u, vt, e), c = VLD(u->co);
    __m128i z = _mm_setzero_si128();
    __m128i ss = _mm_srai_epi16(s, 15), ts = _mm_srai_epi16(t, 15);
    __m128i s0 = _mm_unpacklo_epi16(s, ss), s1 = _mm_unpackhi_epi16(s, ss);
    __m128i t0 = _mm_unpacklo_epi16(t, ts), t1 = _mm_unpackhi_epi16(t, ts);
    __m128i c0 = _mm_unpacklo_epi16(c, z), c1 = _mm_unpackhi_epi16(c, z);
    __m128i lo;
    if (sub)
    {
        s0 = _mm_sub_epi32(_mm_sub_epi32(s0, t0), c0);
        s1 = _mm_sub_epi32(_mm_sub_epi32(s1, t1), c1);
        lo = _mm_sub_epi16(_mm_sub_epi16(s, t), c);
    }
    else
    {
        s0 = _mm_add_epi32(_mm_add_epi32(s0, t0), c0);
        s1 = _mm_add_epi32(_mm_add_epi32(s1, t1), c1);
        lo = _mm_add_epi16(_mm_add_epi16(s, t), c);
    }
    VST(u->accl, lo);
    VST(u->v[vd], _mm_packs_epi32(s0, s1));
    VST(u->co, z);
    VST(u->ne, z);
}

VU_OP void vadd_(F3dVu *u, int vd, int vs, int vt, int e)
{
    vx_addsub(u, 0, vd, vs, vt, e);
}

VU_OP void vsub_(F3dVu *u, int vd, int vs, int vt, int e)
{
    vx_addsub(u, 1, vd, vs, vt, e);
}

/* vaddc: unsigned sum, carry out */
VU_OP void vaddc_(F3dVu *u, int vd, int vs, int vt, int e)
{
    __m128i s = VLD(u->v[vs]), t = vx_sel(u, vt, e);
    __m128i x = _mm_add_epi16(s, t);
    VST(u->accl, x);
    VST(u->co, VFLAG(vx_carry(x, t)));
    VST(u->ne, _mm_setzero_si128());
    VST(u->v[vd], x);
}

/* vsubc: unsigned difference, borrow and not-equal out */
VU_OP void vsubc_(F3dVu *u, int vd, int vs, int vt, int e)
{
    const __m128i k = _mm_set1_epi16((short)0x8000);
    __m128i s = VLD(u->v[vs]), t = vx_sel(u, vt, e);
    __m128i x = _mm_sub_epi16(s, t);
    __m128i b = _mm_cmplt_epi16(_mm_xor_si128(s, k), _mm_xor_si128(t, k));
    __m128i eq = _mm_cmpeq_epi16(s, t);
    VST(u->accl, x);
    VST(u->co, VFLAG(b));
    VST(u->ne, _mm_andnot_si128(eq, _mm_set1_epi16(1)));
    VST(u->v[vd], x);
}
#else
/* the adds: the accumulator's low slice takes the raw sum */
#define VU_ADD_BEGIN \
    int16_t t[8], r[8]; \
    const int16_t *s = u->v[vs]; \
    int i; \
    vu_tsel(u, t, vt, e); \
    for (i = 0; i < 8; i++) \
    {
#define VU_ADD_END \
        u->accl[i] = (int16_t)x; \
    } \
    memcpy(u->v[vd], r, sizeof(r));

/* vadd: carry in, saturated result, flags cleared */
VU_OP void vadd_(F3dVu *u, int vd, int vs, int vt, int e)
{
    VU_ADD_BEGIN
        int32_t x = (int32_t)s[i] + t[i] + u->co[i];
        r[i] = (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
        u->co[i] = 0;
        u->ne[i] = 0;
    VU_ADD_END
}

/* vsub: borrow in */
VU_OP void vsub_(F3dVu *u, int vd, int vs, int vt, int e)
{
    VU_ADD_BEGIN
        int32_t x = (int32_t)s[i] - t[i] - u->co[i];
        r[i] = (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
        u->co[i] = 0;
        u->ne[i] = 0;
    VU_ADD_END
}

/* vaddc: unsigned sum, carry out */
VU_OP void vaddc_(F3dVu *u, int vd, int vs, int vt, int e)
{
    VU_ADD_BEGIN
        int32_t x = (int32_t)(uint16_t)s[i] + (uint16_t)t[i];
        r[i] = (int16_t)x;
        u->co[i] = (uint8_t)((x >> 16) & 1);
        u->ne[i] = 0;
    VU_ADD_END
}

/* vsubc: unsigned difference, borrow and not-equal out */
VU_OP void vsubc_(F3dVu *u, int vd, int vs, int vt, int e)
{
    VU_ADD_BEGIN
        int32_t x = (int32_t)(uint16_t)s[i] - (uint16_t)t[i];
        r[i] = (int16_t)x;
        u->co[i] = (uint8_t)(x < 0);
        u->ne[i] = (uint8_t)(x != 0);
    VU_ADD_END
}

#endif

#define vadd(d, s, t, e)  vadd_(u, d, s, t, e)
#define vsub(d, s, t, e)  vsub_(u, d, s, t, e)
#define vaddc(d, s, t, e) vaddc_(u, d, s, t, e)
#define vsubc(d, s, t, e) vsubc_(u, d, s, t, e)

VU_OP void set_accl(F3dVu *u, int i, int16_t x)
{
    u->accl[i] = x;
}

/* vabs as used here: the sign source equals the value source */
VU_OP void vabs(F3dVu *u, int vd, int vs, int vt, int e)
{
    int16_t r[8];
    int i;
    for (i = 0; i < 8; i++)
    {
        int16_t s = u->v[vs][i], t = u->v[vt][vu_sel(e, i)];
        int16_t x;
        if (s < 0)
            x = (t == -32768) ? 32767 : (int16_t)-t;
        else if (s > 0)
            x = t;
        else
            x = 0;
        set_accl(u, i, x);
        r[i] = x;
    }
    memcpy(u->v[vd], r, sizeof(r));
}

#ifdef F3D_SSE2
/* -x as a 0/-1 mask from a 0/1 flag vector */
#define VMASK(p)   _mm_sub_epi16(_mm_setzero_si128(), VLD(p))

VU_OP void vu_cmp(F3dVu *u, int ge, int vd, int vs, int vt, int e)
{
    __m128i s = VLD(u->v[vs]), t = vx_sel(u, vt, e);
    __m128i eq = _mm_cmpeq_epi16(s, t);
    __m128i nc = _mm_and_si128(VMASK(u->ne), VMASK(u->co));
    __m128i c, r, z = _mm_setzero_si128();
    if (ge)
        c = _mm_or_si128(_mm_cmpgt_epi16(s, t), _mm_andnot_si128(nc, eq));
    else
        c = _mm_or_si128(_mm_cmplt_epi16(s, t), _mm_and_si128(nc, eq));
    r = _mm_or_si128(_mm_and_si128(c, s), _mm_andnot_si128(c, t));
    VST(u->comp, VFLAG(c));
    VST(u->clip, z);
    VST(u->co, z);
    VST(u->ne, z);
    VST(u->accl, r);
    VST(u->v[vd], r);
}
#else
VU_OP void vu_cmp(F3dVu *u, int ge, int vd, int vs, int vt, int e)
{
    int16_t r[8];
    int i;
    for (i = 0; i < 8; i++)
    {
        int16_t s = u->v[vs][i], t = u->v[vt][vu_sel(e, i)];
        int eq = (s == t);
        int c;
        if (ge)
            c = (s > t) || (eq && !(u->ne[i] & u->co[i]));
        else
            c = (s < t) || (eq && (u->ne[i] & u->co[i]));
        u->comp[i] = (uint8_t)c;
        u->clip[i] = 0;
        u->co[i] = 0;
        u->ne[i] = 0;
        r[i] = c ? s : t;
        set_accl(u, i, r[i]);
    }
    memcpy(u->v[vd], r, sizeof(r));
}

#endif

#define vlt(d, s, t, e) vu_cmp(u, 0, d, s, t, e)
#define vge(d, s, t, e) vu_cmp(u, 1, d, s, t, e)

#ifdef F3D_SSE2
VU_OP void vmrg(F3dVu *u, int vd, int vs, int vt, int e)
{
    __m128i c = VMASK(u->comp);
    __m128i r = _mm_or_si128(_mm_and_si128(c, VLD(u->v[vs])),
                             _mm_andnot_si128(c, vx_sel(u, vt, e)));
    VST(u->accl, r);
    VST(u->v[vd], r);
}
#else
VU_OP void vmrg(F3dVu *u, int vd, int vs, int vt, int e)
{
    int16_t r[8];
    int i;
    for (i = 0; i < 8; i++)
    {
        r[i] = u->comp[i] ? u->v[vs][i] : u->v[vt][vu_sel(e, i)];
        set_accl(u, i, r[i]);
    }
    memcpy(u->v[vd], r, sizeof(r));
}

#endif

#ifdef F3D_SSE2
VU_OP void vu_logic(F3dVu *u, int op, int vd, int vs, int vt, int e)
{
    __m128i s = VLD(u->v[vs]), t = vx_sel(u, vt, e);
    __m128i r = op == 0 ? _mm_and_si128(s, t) : op == 1 ? _mm_or_si128(s, t)
                                                         : _mm_xor_si128(s, t);
    VST(u->accl, r);
    VST(u->v[vd], r);
}
#else
VU_OP void vu_logic(F3dVu *u, int op, int vd, int vs, int vt, int e)
{
    int16_t r[8];
    int i;
    for (i = 0; i < 8; i++)
    {
        int16_t s = u->v[vs][i], t = u->v[vt][vu_sel(e, i)];
        r[i] = (int16_t)(op == 0 ? (s & t) : op == 1 ? (s | t) : (s ^ t));
        set_accl(u, i, r[i]);
    }
    memcpy(u->v[vd], r, sizeof(r));
}

#endif

#define vand(d, s, t, e) vu_logic(u, 0, d, s, t, e)
#define vor(d, s, t, e)  vu_logic(u, 1, d, s, t, e)
#define vxor(d, s, t, e) vu_logic(u, 2, d, s, t, e)

/* vcr: one's-complement clip against +-|t| */
VU_OP void vcr(F3dVu *u, int vd, int vs, int vt, int e)
{
    int16_t r[8];
    int i;
    for (i = 0; i < 8; i++)
    {
        int16_t s = u->v[vs][i], t = u->v[vt][vu_sel(e, i)];
        int16_t sn = (int16_t)(((s ^ t) < 0) ? ~0 : 0);
        int le = (t <= (int16_t)~(s & sn));
        int ge = ((int16_t)(s | sn) >= t);
        int16_t c = (int16_t)(t ^ sn);
        int sel = sn ? le : ge;
        r[i] = sel ? c : s;
        set_accl(u, i, r[i]);
        u->co[i] = 0;
        u->ne[i] = 0;
        u->clip[i] = (uint8_t)ge;
        u->comp[i] = (uint8_t)le;
        u->vce[i] = 0;
    }
    memcpy(u->v[vd], r, sizeof(r));
}

#ifdef F3D_SSE2
VU_OP void vch(F3dVu *u, int vd, int vs, int vt, int e)
{
    const __m128i one = _mm_set1_epi16(1), z = _mm_setzero_si128();
    __m128i s = VLD(u->v[vs]), t = vx_sel(u, vt, e);
    __m128i cch = _mm_cmpeq_epi16(t, _mm_set1_epi16((short)0x8000));
    __m128i sn = _mm_srai_epi16(_mm_xor_si128(s, t), 15);
    __m128i c = _mm_xor_si128(t, sn);
    __m128i vcev = _mm_and_si128(_mm_cmpeq_epi16(s, c), sn);
    __m128i eq, ge, le, sel, r;
    c = _mm_sub_epi16(c, _mm_andnot_si128(cch, sn));
    eq = _mm_or_si128(_mm_andnot_si128(cch, _mm_cmpeq_epi16(s, c)), vcev);
    ge = _mm_andnot_si128(_mm_cmpgt_epi16(t, _mm_or_si128(sn, s)), _mm_set1_epi16(-1));
    le = _mm_or_si128(_mm_and_si128(sn, _mm_andnot_si128(
                          _mm_cmpgt_epi16(z, _mm_sub_epi16(c, s)), _mm_set1_epi16(-1))),
                      _mm_andnot_si128(sn, _mm_cmpgt_epi16(z, t)));
    sel = _mm_or_si128(_mm_and_si128(sn, le), _mm_andnot_si128(sn, ge));
    r = _mm_or_si128(_mm_and_si128(sel, c), _mm_andnot_si128(sel, s));
    VST(u->accl, r);
    VST(u->clip, VFLAG(ge));
    VST(u->comp, VFLAG(le));
    VST(u->ne, _mm_andnot_si128(eq, one));
    VST(u->co, VFLAG(sn));
    VST(u->vce, VFLAG(vcev));
    VST(u->v[vd], r);
}
#else
VU_OP void vch(F3dVu *u, int vd, int vs, int vt, int e)
{
    int16_t r[8];
    int i;
    for (i = 0; i < 8; i++)
    {
        int16_t s = u->v[vs][i], t = u->v[vt][vu_sel(e, i)];
        int16_t cch = (int16_t)((t == -32768) ? ~0 : 0);
        int16_t sn = (int16_t)(((s ^ t) < 0) ? ~0 : 0);
        int16_t c = (int16_t)(t ^ sn);
        int vcev = (s == c) & (sn != 0);
        int eq, ge, le, sb;
        c = (int16_t)(c - (sn & ~cch));
        eq = (s == c) & !cch;
        eq |= vcev;
        ge = ((int16_t)(sn | s) >= t);
        sb = (sn != 0);
        le = sb ? ((int16_t)(c - s) >= 0) : (t < 0);
        u->comp[i] = (uint8_t)(sb ? le : ge);
        r[i] = u->comp[i] ? c : s;
        set_accl(u, i, r[i]);
        u->clip[i] = (uint8_t)ge;
        u->comp[i] = (uint8_t)le;
        u->ne[i] = (uint8_t)(eq ^ 1);
        u->co[i] = (uint8_t)sb;
        u->vce[i] = (uint8_t)vcev;
    }
    memcpy(u->v[vd], r, sizeof(r));
}

#endif

#ifdef F3D_SSE2
VU_OP void vcl(F3dVu *u, int vd, int vs, int vt, int e)
{
    const __m128i k = _mm_set1_epi16((short)0x8000), z = _mm_setzero_si128();
    __m128i vb = VLD(u->v[vs]), tt = vx_sel(u, vt, e);
    __m128i eq = _mm_cmpeq_epi16(VLD(u->ne), z);
    __m128i sn = VMASK(u->co);
    __m128i vce = VMASK(u->vce);
    __m128i vc = _mm_sub_epi16(_mm_xor_si128(tt, sn), sn);   /* sn ? -tt : tt */
    __m128i lz = _mm_cmpeq_epi16(vb, vc);
    __m128i uz = _mm_xor_si128(vx_carry(_mm_add_epi16(vb, tt), tt), _mm_set1_epi16(-1));
    __m128i gen = _mm_and_si128(_mm_or_si128(lz, uz), vce);
    __m128i len = _mm_or_si128(_mm_andnot_si128(vce, _mm_and_si128(lz, uz)), gen);
    __m128i gen2 = _mm_andnot_si128(_mm_cmplt_epi16(_mm_xor_si128(vb, k),
                                                    _mm_xor_si128(vc, k)),
                                    _mm_set1_epi16(-1));
    __m128i es = _mm_and_si128(eq, sn), en = _mm_andnot_si128(sn, eq);
    __m128i le = _mm_or_si128(_mm_and_si128(es, len), _mm_andnot_si128(es, VMASK(u->comp)));
    __m128i ge = _mm_or_si128(_mm_and_si128(en, gen2), _mm_andnot_si128(en, VMASK(u->clip)));
    __m128i sel = _mm_or_si128(_mm_and_si128(sn, le), _mm_andnot_si128(sn, ge));
    __m128i r = _mm_or_si128(_mm_and_si128(sel, vc), _mm_andnot_si128(sel, vb));
    VST(u->accl, r);
    VST(u->co, z);
    VST(u->ne, z);
    VST(u->vce, z);
    VST(u->clip, VFLAG(ge));
    VST(u->comp, VFLAG(le));
    VST(u->v[vd], r);
}
#else
VU_OP void vcl(F3dVu *u, int vd, int vs, int vt, int e)
{
    int16_t r[8];
    int i;
    for (i = 0; i < 8; i++)
    {
        uint16_t vb = (uint16_t)u->v[vs][i];
        int16_t tt = u->v[vt][vu_sel(e, i)];
        uint16_t vc = (uint16_t)tt;
        int eq = u->ne[i] ^ 1;
        int sn = u->co[i];
        int lz, uz, gen, len, le, ge;
        if (sn)
            vc = (uint16_t)(-(int)vc);
        lz = ((uint16_t)(vb - vc) == 0);
        uz = ((int32_t)vb + (uint16_t)tt - 65536) < 0;
        gen = (lz | uz) & u->vce[i];
        len = ((lz & uz) & (u->vce[i] ^ 1)) | gen;
        gen = (vb >= vc);
        le = (eq & sn) ? len : u->comp[i];
        ge = (eq & !sn) ? gen : u->clip[i];
        r[i] = (sn ? le : ge) ? (int16_t)vc : (int16_t)vb;
        set_accl(u, i, r[i]);
        u->co[i] = 0;
        u->ne[i] = 0;
        u->clip[i] = (uint8_t)ge;
        u->comp[i] = (uint8_t)le;
        u->vce[i] = 0;
    }
    memcpy(u->v[vd], r, sizeof(r));
}

#endif

/* vsar: e 8/9/10 read the accumulator high/middle/low slice */
VU_OP void vsar(F3dVu *u, int vd, int e)
{
    int i;
    for (i = 0; i < 8; i++)
        u->v[vd][i] = (e == 8) ? acc_h(u, i) : (e == 9) ? acc_m(u, i)
                    : acc_l(u, i);
}

#ifdef F3D_SSE2
VU_OP void vu_acc_vt(F3dVu *u, int vt, int e)
{
    VST(u->accl, vx_sel(u, vt, e));
}
#else
VU_OP void vu_acc_vt(F3dVu *u, int vt, int e)
{
    int i;
    for (i = 0; i < 8; i++)
        set_accl(u, i, u->v[vt][vu_sel(e, i)]);
}

#endif

/* single-lane moves and the divide unit: dest lane dl, source lane e & 7 */
VU_OP void vmov(F3dVu *u, int vd, int dl, int vt, int e)
{
    int16_t x = u->v[vt][e & 7];
    vu_acc_vt(u, vt, e);
    u->v[vd][dl] = x;
}

static void vrcp(F3dVu *u, int vd, int dl, int vt, int e)
{
    u->divin = u->v[vt][e & 7];
    u->divout = rsp_rcp16(u->divin);
    vu_acc_vt(u, vt, e);
    u->v[vd][dl] = (int16_t)u->divout;
    u->dph = 0;
}

static void vrcph(F3dVu *u, int vd, int dl, int vt, int e)
{
    int16_t x = (int16_t)(u->divout >> 16);
    u->divin = (int32_t)((uint32_t)(uint16_t)u->v[vt][e & 7] << 16);
    vu_acc_vt(u, vt, e);
    u->v[vd][dl] = x;
    u->dph = 1;
}

static void vrcpl(F3dVu *u, int vd, int dl, int vt, int e)
{
    if (u->dph)
        u->divin |= (uint16_t)u->v[vt][e & 7];
    else
        u->divin = u->v[vt][e & 7];
    u->divout = rsp_rcp32_dp(u->divin);
    vu_acc_vt(u, vt, e);
    u->v[vd][dl] = (int16_t)u->divout;
    u->dph = 0;
}

/* ---- loads and stores (byte-indexed vector elements) ----------------- */

VU_OP void vbyte_set(F3dVu *u, int vt, int b, uint8_t x)
{
    int16_t *l = &u->v[vt][(b >> 1) & 7];
    if (b & 1)
        *l = (int16_t)((*l & 0xff00) | x);
    else
        *l = (int16_t)((*l & 0x00ff) | (x << 8));
}

VU_OP uint8_t vbyte_get(const F3dVu *u, int vt, int b)
{
    uint16_t l = (uint16_t)u->v[vt][(b >> 1) & 7];
    return (uint8_t)((b & 1) ? l : (l >> 8));
}

VU_OP void vload(F3dVu *u, int vt, int e, unsigned int addr, int n)
{
    int k;
    if (((e | n) & 1) == 0)
    {
        /* whole lanes */
        for (k = 0; k < n; k += 2)
            u->v[vt][((e + k) >> 1) & 7] =
                (int16_t)((u->dmem[(addr + k) & 0xfff] << 8)
                          | u->dmem[(addr + k + 1) & 0xfff]);
        return;
    }
    for (k = 0; k < n; k++)
        vbyte_set(u, vt, (e + k) & 15, u->dmem[(addr + k) & 0xfff]);
}

VU_OP void vstore(F3dVu *u, int vt, int e, unsigned int addr, int n)
{
    int k;
    DM_DIRTY(u, addr);
    DM_DIRTY(u, addr + (unsigned int)n - 1u);
    if (((e | n) & 1) == 0)
    {
        for (k = 0; k < n; k += 2)
        {
            uint16_t l = (uint16_t)u->v[vt][((e + k) >> 1) & 7];
            u->dmem[(addr + k) & 0xfff] = (uint8_t)(l >> 8);
            u->dmem[(addr + k + 1) & 0xfff] = (uint8_t)l;
        }
        return;
    }
    for (k = 0; k < n; k++)
        u->dmem[(addr + k) & 0xfff] = vbyte_get(u, vt, (e + k) & 15);
}

#define lsv(t, e, a) vload(u, t, e, a, 2)
#define llv(t, e, a) vload(u, t, e, a, 4)
#define ldv(t, e, a) vload(u, t, e, a, 8)
#define lqv(t, a)    vload(u, t, 0, a, 16)
#define ssv(t, e, a) vstore(u, t, e, a, 2)
#define slv(t, e, a) vstore(u, t, e, a, 4)
#define sdv(t, e, a) vstore(u, t, e, a, 8)

/* luv from an 8-byte aligned address: each byte to bits 14..7 */
VU_OP void luv(F3dVu *u, int vt, unsigned int addr)
{
    int i;
    for (i = 0; i < 8; i++)
        u->v[vt][i] = (int16_t)(u->dmem[(addr + i) & 0xfff] << 7);
}

VU_OP int32_t mfc2(const F3dVu *u, int vt, int lane)
{
    return (int32_t)u->v[vt][lane];
}

VU_OP void mtc2(F3dVu *u, int vt, int lane, int32_t x)
{
    u->v[vt][lane] = (int16_t)(x & 0xffff);
}

VU_OP void dm_w16(F3dVu *u, unsigned int a, uint32_t x)
{
    DM_DIRTY(u, a);
    DM_DIRTY(u, a + 1u);
    u->dmem[a & 0xfff] = (uint8_t)(x >> 8);
    u->dmem[(a + 1) & 0xfff] = (uint8_t)x;
}

VU_OP void dm_w32(F3dVu *u, unsigned int a, uint32_t x)
{
    dm_w16(u, a, x >> 16);
    dm_w16(u, a + 2, x & 0xffff);
}

VU_OP int32_t dm_lh(const F3dVu *u, unsigned int a)
{
    return (int16_t)((u->dmem[a & 0xfff] << 8) | u->dmem[(a + 1) & 0xfff]);
}

VU_OP uint32_t dm_lw(const F3dVu *u, unsigned int a)
{
    return ((uint32_t)(uint16_t)dm_lh(u, a) << 16) | (uint16_t)dm_lh(u, a + 2);
}

/* element field values */
#define Q0 2
#define Q1 3
#define H0 4
#define H1 5
#define H3 7
#define E0 8
#define E1 9
#define E2 10
#define E3 11
#define E4 12
#define E5 13
#define E6 14
#define E7 15

/* ---- local DMEM layout ------------------------------------------------ */

#define D_V31     0x030   /* constant rows */
#define D_V30     0x040
#define D_TWOS    0x060
#define D_SP      0x110   /* state block */
#define D_OUT     0x400   /* coefficient block */
#define D_VTX     0x600   /* three 0x28-byte vertex records */
#define D_SCR     0x920   /* setup scratch */
#define D_CLAMP   0x028   /* screen clamp row */
#define D_SIGN    0x050   /* viewport sign row */
#define D_PLANES  0x070   /* clip plane rows; lane 3 doubles as the guard */
#define D_GUARD   0x076
#define D_VIEWPORT 0x260  /* vscale, vtrans */
#define D_FOG     0x270   /* fog multiplier, offset, clamp */
#define D_LIST    0x980   /* clip polygon lists (two, 0x14 apart) */
#define D_GEN     0xdf8   /* clip-generated vertex records */

static const int16_t k_v31[8] = { 0, 1, 2, -1, 0x4000, 4, 0x0633, 0x0200 };
static const int16_t k_v30[8] = { 0x7fff, -8, 8, 0x40, 0x20, -32768,
                                  0x01cc, -13108 /* 0xcccc */ };

static void put_vtx(F3dVu *u, unsigned int a, const RspTriVtx *v)
{
    dm_w16(u, a + 0x00, (uint32_t)v->cx >> 16);
    dm_w16(u, a + 0x08, (uint32_t)v->cx & 0xffff);
    dm_w16(u, a + 0x02, (uint32_t)v->cy >> 16);
    dm_w16(u, a + 0x0a, (uint32_t)v->cy & 0xffff);
    dm_w16(u, a + 0x04, (uint32_t)v->cz >> 16);
    dm_w16(u, a + 0x0c, (uint32_t)v->cz & 0xffff);
    dm_w16(u, a + 0x06, (uint32_t)v->cw >> 16);
    dm_w16(u, a + 0x0e, (uint32_t)v->cw & 0xffff);
    dm_w16(u, a + 0x24, (uint32_t)v->f3d_flags & 0xffff);
    DM_DIRTY(u, a + 0x10);
    DM_DIRTY(u, a + 0x13);
    u->dmem[a + 0x10] = (uint8_t)v->r;
    u->dmem[a + 0x11] = (uint8_t)v->g;
    u->dmem[a + 0x12] = (uint8_t)v->b;
    u->dmem[a + 0x13] = (uint8_t)v->a;
    dm_w16(u, a + 0x14, (uint32_t)v->s);
    dm_w16(u, a + 0x16, (uint32_t)v->t);
    dm_w16(u, a + 0x18, (uint16_t)v->x);
    dm_w16(u, a + 0x1a, (uint16_t)v->y);
    dm_w16(u, a + 0x1c, (uint32_t)v->z >> 16);
    dm_w16(u, a + 0x1e, (uint32_t)v->z & 0xffff);
    dm_w16(u, a + 0x20, (uint32_t)v->invw >> 16);
    dm_w16(u, a + 0x22, (uint32_t)v->invw & 0xffff);
}

/* Double-precision reciprocal of lanes 3 and 7 of v29:v28 (integer:
 * fraction), one Newton step, result in v27:v26. */
static void recip_lanes37(F3dVu *u)
{
    vrcph(u, 27, 3, 29, E3);
    vrcpl(u, 26, 3, 28, E3);
    vrcph(u, 27, 3, 29, E7);
    vrcpl(u, 26, 7, 28, E7);
    vrcph(u, 27, 7, 31, E0);
    vmudn(26, 26, 31, E2);
    vmadh(27, 27, 31, E2);
    vmadn(26, 31, 31, E0);
    lqv(23, D_TWOS);
    vxor(22, 31, 31, 0);
    vmudl(24, 26, 28, 0);
    vmadm(24, 27, 28, 0);
    vmadn(24, 26, 29, 0);
    vmadh(25, 27, 29, 0);
    vsubc(24, 22, 24, 0);
    vsub(25, 23, 25, 0);
    vmudl(22, 26, 24, 0);
    vmadm(23, 27, 24, 0);
    vmadn(26, 26, 25, 0);
    vmadh(27, 27, 25, 0);
}

/* Zeroed vector unit and the DMEM image the setup expects: the
 * constant rows, the clip plane rows, the viewport and fog rows and the
 * state block. */
static void vu_setup_image(F3dVu *u, const F3dTriState *st);

/* vu_setup builds the image once per state and copies it after */
static void vu_setup(F3dVu *u, const F3dTriState *st)
{
    static F3dVu image;
    static F3dTriState image_st;
    static unsigned int image_gen;
    uint64_t d;
    unsigned int b;
    if (image_gen == 0 || memcmp(&image_st, st, sizeof(*st)) != 0)
    {
        vu_setup_image(&image, st);
        image_st = *st;
        image_gen++;
    }
    if (u->image_gen != image_gen)
    {
        memcpy(u, &image, offsetof(F3dVu, dirty));
        u->image_gen = image_gen;
        u->dirty = 0;
        return;
    }
    /* the same image: the registers and flags, and only the DMEM blocks
     * the last setup wrote */
    memcpy(u, &image, offsetof(F3dVu, dmem));
    for (d = u->dirty, b = 0; d != 0; d >>= 1, b++)
        if (d & 1)
            memcpy(u->dmem + b * 64u, image.dmem + b * 64u, 64);
    u->dirty = 0;
}

static void vu_setup_image(F3dVu *u, const F3dTriState *st)
{
    static const int16_t planes[24] =
    {
        1, 0, 0, 1,   0, 1, 0, 1,   1, 0, 0, -1,
        0, 1, 0, -1,  0, 0, 1, -1,  0, 0, 0, 1
    };
    int i;
    memset(u, 0, sizeof(*u));
    u->rev = st->rev;
    for (i = 0; i < 8; i++)
    {
        dm_w16(u, D_V31 + 2 * i, (uint16_t)k_v31[i]);
        dm_w16(u, D_V30 + 2 * i, (uint16_t)k_v30[i]);
        dm_w16(u, D_TWOS + 2 * i, 2);
        dm_w16(u, D_SIGN + 2 * i, (i & 3) == 1 ? 0xffff : 1);
    }
    for (i = 0; i < 24; i++)
        dm_w16(u, D_PLANES + 2 * i, (uint16_t)planes[i]);
    /* gSPClipRatio rewrites the w lanes of the four side planes; lane 3
     * of the first row is also the vertex write's guard factor */
    dm_w16(u, D_PLANES + 0x06, (uint16_t)st->guard);
    dm_w16(u, D_PLANES + 0x0e, (uint16_t)st->guard);
    dm_w16(u, D_PLANES + 0x16, (uint16_t)-st->guard);
    dm_w16(u, D_PLANES + 0x1e, (uint16_t)-st->guard);
    /* the near plane: w (revision B) or z + w */
    if (st->rev == F3D_TRI_REV_A)
        dm_w16(u, D_PLANES + 0x2c, 1);
    dm_w16(u, D_CLAMP + 0, (uint16_t)st->clamp_hi);
    dm_w16(u, D_CLAMP + 2, (uint16_t)st->clamp_lo);
    dm_w16(u, D_CLAMP + 4, 0x7fff);
    for (i = 0; i < 4; i++)
    {
        dm_w16(u, D_VIEWPORT + 2 * i, (uint16_t)st->vs[i]);
        dm_w16(u, D_VIEWPORT + 8 + 2 * i, (uint16_t)st->vt[i]);
    }
    dm_w16(u, D_FOG + 0, (uint16_t)st->fog_m);
    dm_w16(u, D_FOG + 2, (uint16_t)st->fog_o);
    dm_w16(u, D_FOG + 4, 0xff);
    if (st->rev == F3D_TRI_REV_A)
        dm_w16(u, D_V30 + 6, (uint16_t)st->tex_scale_a);
    dm_w16(u, D_SP + 0x02, st->persp_norm & 0xffff);
    dm_w32(u, D_SP + 0x04, st->geometry_mode);
    u->dmem[D_SP + 0x11] = (uint8_t)st->zclamp;
    u->dmem[D_SP + 0x12] = (uint8_t)((st->level << 3) | (st->tile & 7));
    lqv(31, D_V31);
    lqv(30, D_V30);
}

/* set when tri_core culls the triangle on its face; see
 * rsp_tri_f3d_face_culled */
static int s_face_culled;

/* The triangle write proper, on the vertex records at at, v0, v1 (in
 * command order) with the flat-shade vertex at a0; the coefficient block
 * goes to ew. Returns the word count, 0 when culled. */
static int tri_core(F3dVu *u, uint32_t at, uint32_t v0, uint32_t v1,
                    uint32_t a0, const F3dTriState *st, int32_t *ew)
{
    uint32_t a1, a2, a3, t0, t1, t2, t3, t4, t5, t6, t7;
    uint32_t s0, s1, s2, s7, tmp;
    int i, bytes;
    const int rev_a = (st->rev == F3D_TRI_REV_A);
    /* the top vertex's y fraction: v2[0] (revision B) or v2[1] */
    const int ye = rev_a ? E1 : E0;

    s7 = D_OUT;
    t0 = D_SCR;

    /* ---- screen positions, cross product, sort by Y ---- */
    llv(13, 0, at + 0x18);
    llv(14, 0, v0 + 0x18);
    llv(15, 0, v1 + 0x18);
    t5 = dm_lw(u, D_SP + 4);
    lsv(21, 0, D_SP + 2);
    lsv(5, 0, at + 0x06);
    vsub(10, 14, 13, 0);
    lsv(6, 0, at + 0x0e);
    vsub(9, 15, 13, 0);
    lsv(5, 2, v0 + 0x06);
    vsub(12, 13, 14, 0);
    lsv(6, 2, v0 + 0x0e);
    lsv(5, 4, v1 + 0x06);
    lsv(6, 4, v1 + 0x0e);
    vmudh(16, 9, 10, E1);
    t1 = (uint32_t)dm_lh(u, at + 0x1a);
    if (rev_a)
    {
        /* revision A: both cross terms accumulate before readout; the
         * Y order comes from a table indexed by three compares, the
         * cull and degenerate tests read the clamped cross */
        static const uint8_t perm_v1[8] = { 4, 4, 0, 4, 2, 2, 0, 2 };
        static const uint8_t perm_v0[8] = { 2, 2, 4, 0, 0, 4, 2, 0 };
        static const uint8_t perm_at[8] = { 0, 0, 2, 2, 4, 0, 4, 0 };
        static const uint8_t parity[8]  = { 0, 0, 0, 1, 0, 1, 1, 0 };
        vmadh(16, 12, 9, E1);
        t2 = (uint32_t)dm_lh(u, v0 + 0x1a);
        vsar(u, 28, 9);
        t3 = (uint32_t)dm_lh(u, v1 + 0x1a);
        vsar(u, 29, 8);
        t7 = t5 << 18;
        s1 = (uint32_t)mfc2(u, 16, 0);
        dm_w16(u, t0 + 0, at);
        dm_w16(u, t0 + 2, v0);
        t7 = s1 & t7;
        dm_w16(u, t0 + 4, v1);
        if ((int32_t)t7 < 0)
        {
            s_face_culled = 1;
            return 0;
        }
        a3 = (uint32_t)((int32_t)t3 < (int32_t)t2);
        t4 = (uint32_t)((int32_t)t2 < (int32_t)t1);
        a3 = a3 * 2 + t4;
        t4 = (uint32_t)((int32_t)t1 < (int32_t)t3);
        a3 = a3 * 2 + t4;
        v1 = (uint32_t)dm_lh(u, t0 + perm_v1[a3]) & 0xffff;
        v0 = (uint32_t)dm_lh(u, t0 + perm_v0[a3]) & 0xffff;
        at = (uint32_t)dm_lh(u, t0 + perm_at[a3]) & 0xffff;
        t4 = parity[a3];
        llv(15, 0, v1 + 0x18);
        llv(14, 0, v0 + 0x18);
        llv(13, 0, at + 0x18);
        if ((int32_t)t4 > 0)
        {
            vsubc(28, 31, 28, 0);
            vsub(29, 31, 29, 0);
        }
        vsub(4, 15, 14, 0);
        if (s1 == 0)
            return 0;
        t6 = t5 << 19;
        vsub(10, 14, 13, 0);
        t6 = ~t6;
        vsub(9, 15, 13, 0);
        t6 = s1 | t6;
        vmov(u, 29, 3, 29, E0);
        vmov(u, 28, 3, 28, E0);
        if ((int32_t)t6 >= 0)
        {
            s_face_culled = 1;
            return 0;
        }
        ssv(15, 2, s7 + 2);
        vmov(u, 4, 2, 10, E0);
        ssv(14, 2, s7 + 4);
        vmov(u, 4, 3, 10, E1);
        ssv(13, 2, s7 + 6);
        vmov(u, 4, 4, 9, E0);
        a2 = (uint32_t)(int16_t)((vbyte_get(u, 29, 5) << 8)
                                 | vbyte_get(u, 29, 6));
        vmov(u, 4, 5, 9, E1);
        vmudn(2, 13, 31, E4);
        recip_lanes37(u);
        a1 = (uint32_t)(int32_t)(int8_t)u->dmem[D_SP + 7];
        a2 &= 0x80;
        goto edges;
    }
    vsar(u, 18, 9);
    t2 = (uint32_t)dm_lh(u, v0 + 0x1a);
    vsar(u, 17, 8);
    t3 = (uint32_t)dm_lh(u, v1 + 0x1a);
    vmudh(16, 12, 9, E1);
    t6 = t5 & 0x1000;
    vsar(u, 20, 9);
    t7 = t5 & 0x2000;
    vsar(u, 19, 8);
    t4 = 0;
    for (;;)
    {
        if ((int32_t)t2 < (int32_t)t1)
        {
            tmp = t2; t2 = t1; t1 = tmp;
            tmp = v0; v0 = at; at = tmp;
            t4 ^= 1;
        }
        vaddc(28, 18, 20, 0);
        vadd(29, 17, 19, 0);
        if ((int32_t)t3 < (int32_t)t2)
        {
            tmp = t3; t3 = t2; t2 = tmp;
            tmp = v1; v1 = v0; v0 = tmp;
            t4 ^= 1;
            continue;
        }
        break;
    }
    vlt(27, 29, 31, E0);
    llv(15, 0, v1 + 0x18);
    vor(26, 29, 28, 0);
    llv(14, 0, v0 + 0x18);
    llv(13, 0, at + 0x18);
    vsub(4, 15, 14, 0);
    if ((int32_t)t4 > 0)
    {
        vmudn(28, 28, 31, E3);
        vmadh(29, 29, 31, E3);
        vmadn(28, 31, 31, E0);
    }
    vsub(10, 14, 13, 0);
    s1 = (uint32_t)mfc2(u, 27, 0);
    vsub(9, 15, 13, 0);
    s0 = (uint32_t)mfc2(u, 26, 0);
    s1 = (uint32_t)((int32_t)s1 >> 31);
    vmov(u, 29, 3, 29, E0);
    t7 &= s1;
    vmov(u, 28, 3, 28, E0);
    vmov(u, 4, 2, 10, E0);
    if (s0 == 0)
        return 0;
    s1 ^= 0xffff;
    vlt(27, 29, 31, E0);
    t6 &= s1;
    vmov(u, 4, 3, 10, E1);
    s0 = t7 | t6;
    vmov(u, 4, 4, 9, E0);
    vmov(u, 4, 5, 9, E1);
    if ((int32_t)s0 > 0)
    {
        s_face_culled = 1;
        return 0;
    }
    a3 = (uint32_t)mfc2(u, 27, 0);
    a2 = 0x80;
    recip_lanes37(u);
    a1 = (uint32_t)(int32_t)(int8_t)u->dmem[D_SP + 7];
    if ((int32_t)a3 >= 0)
        a2 = 0;

    /* ---- edges ---- */
edges:
    vmudm(9, 4, 31, E4);
    vmadn(10, 31, 31, E0);
    vrcp(u, 8, 1, 4, E1);
    vrcph(u, 7, 1, 31, E0);
    a1 |= 0xc8;
    a3 = (uint32_t)(int32_t)(int8_t)u->dmem[D_SP + 0x12];
    vrcp(u, 8, 3, 4, E3);
    vrcph(u, 7, 3, 31, E0);
    vrcp(u, 8, 5, 4, E5);
    vrcph(u, 7, 5, 31, E0);
    a2 |= a3;
    vmudl(8, 8, 30, E4);
    DM_DIRTY(u, s7);
    DM_DIRTY(u, s7 + 1);
    u->dmem[s7 + 0] = (uint8_t)a1;
    vmadm(7, 7, 30, E4);
    u->dmem[s7 + 1] = (uint8_t)a2;
    vmadn(8, 31, 31, E0);
    vmudh(4, 4, 31, E5);
    lsv(12, 0, v0 + 0x18);
    vmudl(6, 6, 21, E0);
    lsv(12, 4, at + 0x18);
    vmadm(5, 5, 21, E0);
    lsv(12, 8, at + 0x18);
    vmadn(6, 31, 31, E0);
    a3 = t1 << 14;
    vmudl(1, 8, 10, Q0);
    vmadm(1, 7, 10, Q0);
    vmadn(1, 8, 9, Q0);
    vmadh(0, 7, 9, Q0);
    if (!rev_a)
        mtc2(u, 2, 0, (int32_t)a3);
    vmadn(1, 31, 31, E0);
    dm_w32(u, t0 + 0, v1);
    vmudl(8, 8, 31, E4);
    vmadm(7, 7, 31, E4);
    vmadn(8, 31, 31, E0);
    vmudl(1, 1, 31, E4);
    vmadm(0, 0, 31, E4);
    vmadn(1, 31, 31, E0);
    if (!rev_a)
        dm_w16(u, s7 + 2, t3);
    vand(16, 1, 30, E1);
    if (!rev_a)
        dm_w16(u, s7 + 6, t1);
    vmudm(12, 12, 31, E4);
    dm_w32(u, t0 + 4, v0);
    vmadn(13, 31, 31, E0);
    dm_w32(u, t0 + 8, at);
    if (!rev_a)
        dm_w16(u, s7 + 4, t2);
    vcr(u, 0, 0, 30, E6);
    ssv(12, 0, s7 + 0x08);
    vmudl(11, 16, 2, ye);
    ssv(13, 0, s7 + 0x0a);
    vmadm(10, 0, 2, ye);
    ssv(0, 2, s7 + 0x0c);
    vmadn(11, 31, 31, E0);
    ssv(1, 2, s7 + 0x0e);
    a3 = a1 & 2;
    t7 = t0 + 8;
    s0 = t0 + 0x10;
    vsubc(3, 13, 11, Q1);
    ssv(0, 10, s7 + 0x14);
    vsub(9, 12, 10, Q1);
    ssv(1, 10, s7 + 0x16);
    vsubc(21, 6, 6, E1);
    ssv(0, 6, s7 + 0x1c);
    vlt(19, 5, 5, E1);
    ssv(1, 6, s7 + 0x1e);
    vmrg(u, 20, 6, 6, E1);
    ssv(9, 8, s7 + 0x10);
    vsubc(21, 20, 6, E2);
    ssv(3, 8, s7 + 0x12);
    vlt(19, 19, 5, E2);
    ssv(9, 4, s7 + 0x18);
    vmrg(u, 20, 20, 6, E2);
    ssv(3, 4, s7 + 0x1a);
    s7 += 0x20;
    vmudl(20, 20, 30, E5);

    /* ---- texture weights: S, T, W per vertex over the smallest W ---- */
    if ((int32_t)a3 > 0)
    {
        t6 = dm_lw(u, t7 + 0);
        vmadm(19, 19, 30, E5);
        s1 = dm_lw(u, t7 - 4);
        vmadn(20, 31, 31, E0);
        s2 = dm_lw(u, t7 - 8);
        llv(9, 0, t6 + 0x14);
        llv(9, 8, s1 + 0x14);
        llv(22, 0, s2 + 0x14);
        lsv(11, 0, t6 + 0x22);
        lsv(12, 0, t6 + 0x20);
        lsv(11, 8, s1 + 0x22);
        vmov(u, 9, 2, 30, E0);
        lsv(12, 8, s1 + 0x20);
        vmov(u, 9, 6, 30, E0);
        lsv(24, 0, s2 + 0x22);
        vmov(u, 22, 2, 30, E0);
        lsv(25, 0, s2 + 0x20);
        vmudl(6, 11, 20, E0);
        vmadm(6, 12, 20, E0);
        ssv(19, 0, t0 + 0x44);
        vmadn(6, 11, 19, E0);
        ssv(20, 0, t0 + 0x4c);
        vmadh(5, 12, 19, E0);
        vmudl(16, 24, 20, E0);
        vmadm(16, 25, 20, E0);
        vmadn(20, 24, 19, E0);
        vmadh(19, 25, 19, E0);
        vmudm(16, 9, 6, H0);
        vmadh(9, 9, 5, H0);
        vmadn(10, 31, 31, E0);
        vmudm(16, 22, 20, E0);
        vmadh(22, 22, 19, E0);
        vmadn(23, 31, 31, E0);
        sdv(9, 8, s0 + 0x10);
        sdv(10, 8, s0 + 0x18);
        sdv(9, 0, s0 + 0x00);
        sdv(10, 0, s0 + 0x08);
        sdv(22, 0, s0 + 0x20);
        sdv(23, 0, s0 + 0x28);
        vabs(u, 9, 9, 9, 0);
        llv(19, 0, s0 + 0x10);
        vabs(u, 22, 22, 22, 0);
        llv(20, 0, s0 + 0x18);
        vabs(u, 19, 19, 19, 0);
        vge(17, 9, 22, 0);
        vmrg(u, 18, 10, 23, 0);
        vge(17, 17, 19, 0);
        vmrg(u, 18, 18, 20, 0);
    }
    slv(17, 0, t0 + 0x40);
    slv(18, 0, t0 + 0x48);

    /* ---- shade, texture and depth coefficients ---- */
    a3 = a1 & 7;
    vxor(18, 31, 31, 0);
    if ((int32_t)a3 > 0)
    {
        luv(u, 25, v1 + 0x10);
        vadd(16, 18, 30, E5);
        luv(u, 15, at + 0x10);
        vadd(24, 18, 30, E5);
        if (st->flat_select)
        {
            a3 = t5 & 0x200;
            vadd(5, 18, 30, E5);
            luv(u, 23, v0 + 0x10);
            if ((int32_t)a3 <= 0)
            {
                luv(u, 25, a0 + 0x10);
                luv(u, 15, a0 + 0x10);
                luv(u, 23, a0 + 0x10);
            }
        }
        else
        {
            luv(u, 23, v0 + 0x10);
            vadd(5, 18, 30, E5);
        }
        vmudm(25, 25, 31, E7);
        vmudm(15, 15, 31, E7);
        vmudm(23, 23, 31, E7);
        ldv(16, 8, t0 + 0x18);
        ldv(15, 8, t0 + 0x10);
        ldv(24, 8, t0 + 0x28);
        ldv(23, 8, t0 + 0x20);
        ldv(5, 8, t0 + 0x38);
        ldv(25, 8, t0 + 0x30);
        lsv(16, 14, at + 0x1e);
        lsv(15, 14, at + 0x1c);
        lsv(24, 14, v0 + 0x1e);
        lsv(23, 14, v0 + 0x1c);
        lsv(5, 14, v1 + 0x1e);
        lsv(25, 14, v1 + 0x1c);
        vsubc(12, 24, 16, 0);
        vsub(11, 23, 15, 0);
        vsubc(20, 16, 5, 0);
        vsub(19, 15, 25, 0);
        vsubc(10, 5, 16, 0);
        vsub(9, 25, 15, 0);
        vsubc(22, 16, 24, 0);
        vsub(21, 15, 23, 0);
        vmudn(6, 10, 4, E3);
        vmadh(6, 9, 4, E3);
        vmadn(6, 22, 4, E5);
        vmadh(6, 21, 4, E5);
        vsar(u, 9, 8);
        vsar(u, 10, 9);
        vmudn(6, 12, 4, E4);
        vmadh(6, 11, 4, E4);
        vmadn(6, 20, 4, E2);
        vmadh(6, 19, 4, E2);
        vsar(u, 11, 8);
        vsar(u, 12, 9);
        vmudl(6, 10, 26, E3);
        vmadm(6, 9, 26, E3);
        vmadn(10, 10, 27, E3);
        vmadh(9, 9, 27, E3);
        vmudl(6, 12, 26, E3);
        vmadm(6, 11, 26, E3);
        vmadn(12, 12, 27, E3);
        sdv(9, 0, s7 + 0x08);
        vmadh(11, 11, 27, E3);
        sdv(10, 0, s7 + 0x18);
        vmudn(6, 12, 31, E1);
        vmadh(6, 11, 31, E1);
        vmadl(6, 10, 1, E5);
        vmadm(6, 9, 1, E5);
        vmadn(14, 10, 0, E5);
        sdv(11, 0, s7 + 0x28);
        vmadh(13, 9, 0, E5);
        sdv(12, 0, s7 + 0x38);
        vmudl(28, 14, 2, ye);
        sdv(13, 0, s7 + 0x20);
        vmadm(6, 13, 2, ye);
        sdv(14, 0, s7 + 0x30);
        vmadn(28, 31, 31, E0);
        vsubc(18, 16, 28, 0);
        vsub(17, 15, 6, 0);
        if (a1 & 4)
        {
            s7 += 0x40;
            sdv(17, 0, s7 - 0x40);
            sdv(18, 0, s7 - 0x30);
        }
        if (a1 & 2)
        {
            mtc2(u, 19, 0, 0x800);
            vabs(u, 24, 9, 9, 0);
            ldv(20, 8, t0 + 0x40);
            vabs(u, 25, 11, 11, 0);
            ldv(21, 8, t0 + 0x48);
            vmudm(24, 24, 19, E0);
            vmadn(26, 31, 31, E0);
            vmudm(25, 25, 19, E0);
            vmadn(27, 31, 31, E0);
            vmudl(21, 21, 19, E0);
            vmadm(20, 20, 19, E0);
            vmadn(21, 31, 31, E0);
            vmudn(26, 26, 31, E2);
            vmadh(24, 24, 31, E2);
            vmadn(26, 31, 31, E0);
            vmadn(23, 27, 31, E1);
            vmadh(22, 25, 31, E1);
            vmadn(6, 21, 31, E1);
            if (!rev_a)
                mtc2(u, 19, 0, 0x40);
            vmadh(5, 20, 31, E1);
            vsubc(23, 6, 6, E5);
            vge(5, 5, 5, E5);
            vmrg(u, 6, 6, 6, E5);
            vsubc(23, 6, 6, E6);
            vge(5, 5, 5, E6);
            vmrg(u, 6, 6, 6, E6);
            if (rev_a)
            {
                vmudl(6, 6, 30, E3);
                vmadm(5, 5, 30, E3);
            }
            else
            {
                vmudl(6, 6, 19, E0);
                vmadm(5, 5, 19, E0);
            }
            vmadn(6, 31, 31, E0);
            vrcph(u, 23, 0, 5, E4);
            vrcpl(u, 6, 0, 6, E4);
            vrcph(u, 5, 0, 31, E0);
            vmudn(6, 6, 31, E2);
            vmadh(5, 5, 31, E2);
            vlt(5, 5, 31, E1);
            vmrg(u, 6, 6, 31, E0);
            vmudl(20, 18, 6, E0);
            vmadm(20, 17, 6, E0);
            vmadn(20, 18, 5, E0);
            vmadh(19, 17, 5, E0);
            vmudl(22, 10, 6, E0);
            vmadm(22, 9, 6, E0);
            vmadn(22, 10, 5, E0);
            sdv(19, 8, s7 + 0x00);
            vmadh(21, 9, 5, E0);
            sdv(20, 8, s7 + 0x10);
            vmudl(24, 12, 6, E0);
            vmadm(24, 11, 6, E0);
            vmadn(24, 12, 5, E0);
            sdv(21, 8, s7 + 0x08);
            vmadh(23, 11, 5, E0);
            sdv(22, 8, s7 + 0x18);
            vmudl(26, 14, 6, E0);
            vmadm(26, 13, 6, E0);
            vmadn(26, 14, 5, E0);
            sdv(23, 8, s7 + 0x28);
            vmadh(25, 13, 5, E0);
            sdv(24, 8, s7 + 0x38);
            s7 += 0x40;
            sdv(25, 8, s7 - 0x20);
            sdv(26, 8, s7 - 0x10);
        }
        if (a1 & 1)
        {
            vmudn(14, 14, 30, E4);
            vmadh(13, 13, 30, E4);
            vmadn(14, 31, 31, E0);
            vmudn(16, 16, 30, E4);
            vmadh(15, 15, 30, E4);
            vmadn(16, 31, 31, E0);
            ssv(13, 14, s7 + 0x08);
            vmudn(10, 10, 30, E4);
            ssv(14, 14, s7 + 0x0a);
            vmadh(9, 9, 30, E4);
            vmadn(10, 31, 31, E0);
            vmudn(12, 12, 30, E4);
            vmadh(11, 11, 30, E4);
            vmadn(12, 31, 31, E0);
            if (!rev_a)
            {
                a3 = (uint32_t)-(int32_t)u->dmem[D_SP + 0x11];
                mtc2(u, 6, 0, (int32_t)a3);
                if (a3 != 0)
                {
                    vch(u, 11, 11, 6, E0);
                    vcl(u, 12, 12, 31, E0);
                }
            }
            ssv(9, 14, s7 + 0x04);
            vmudl(28, 14, 2, ye);
            ssv(10, 14, s7 + 0x06);
            vmadm(6, 13, 2, ye);
            ssv(11, 14, s7 + 0x0c);
            vmadn(28, 31, 31, E0);
            ssv(12, 14, s7 + 0x0e);
            vsubc(18, 16, 28, 0);
            vsub(17, 15, 6, 0);
            s7 += 0x10;
            ssv(17, 14, s7 - 0x10);
            ssv(18, 14, s7 - 0x0e);
        }
    }

    bytes = (int)(s7 - D_OUT);
    for (i = 0; i < bytes / 4; i++)
        ew[i] = (int32_t)dm_lw(u, D_OUT + 4 * i);
    (void)t0; (void)t4; (void)tmp;
    return bytes / 4;
}

int rsp_tri_write_f3d(int32_t *ew, const RspTriVtx *v1c,
                      const RspTriVtx *v2c, const RspTriVtx *v3c,
                      const F3dTriState *st)
{
    static F3dVu vu;
    F3dVu *u = &vu;
    vu_setup(u, st);
    put_vtx(u, D_VTX + 0x00, v1c);
    put_vtx(u, D_VTX + 0x28, v2c);
    put_vtx(u, D_VTX + 0x50, v3c);
    return tri_core(u, D_VTX, D_VTX + 0x28, D_VTX + 0x50,
                    D_VTX + 0x28 * (unsigned int)(st->flat_vtx % 3), st, ew);
}

/* ---- vertex flags, clipper and fan ------------------------------------ */

VU_OP void vu_flag_clear(F3dVu *u, int i)
{
    u->co[i] = 0;
    u->ne[i] = 0;
    u->clip[i] = 0;
}

VU_OP void veq(F3dVu *u, int vd, int vs, int vt, int e)
{
    int16_t r[8];
    int i;
    for (i = 0; i < 8; i++)
    {
        int16_t s = u->v[vs][i], t = u->v[vt][vu_sel(e, i)];
        u->comp[i] = (uint8_t)((s == t) && !u->ne[i]);
        vu_flag_clear(u, i);
        r[i] = t;
        set_accl(u, i, t);
    }
    memcpy(u->v[vd], r, sizeof(r));
}

VU_OP void vne(F3dVu *u, int vd, int vs, int vt, int e)
{
    int16_t r[8];
    int i;
    for (i = 0; i < 8; i++)
    {
        int16_t s = u->v[vs][i], t = u->v[vt][vu_sel(e, i)];
        u->comp[i] = (uint8_t)((s != t) || u->ne[i]);
        vu_flag_clear(u, i);
        r[i] = s;
        set_accl(u, i, s);
    }
    memcpy(u->v[vd], r, sizeof(r));
}

VU_OP void vnxor(F3dVu *u, int vd, int vs, int vt, int e)
{
    int16_t r[8];
    int i;
    for (i = 0; i < 8; i++)
    {
        r[i] = (int16_t)~(u->v[vs][i] ^ u->v[vt][vu_sel(e, i)]);
        set_accl(u, i, r[i]);
    }
    memcpy(u->v[vd], r, sizeof(r));
}

/* suv to an 8-byte aligned address: bits 14..7 of each lane */
VU_OP void suv(F3dVu *u, int vt, unsigned int addr)
{
    int i;
    for (i = 0; i < 8; i++)
    {
        DM_DIRTY(u, addr + (unsigned int)i);
        u->dmem[(addr + i) & 0xfff] = (uint8_t)((uint16_t)u->v[vt][i] >> 7);
    }
}

VU_OP uint32_t vcc_word(const F3dVu *u)
{
    uint32_t w = 0;
    int i;
    for (i = 0; i < 8; i++)
        w |= ((uint32_t)u->comp[i] << i) | ((uint32_t)u->clip[i] << (8 + i));
    return w;
}

/* The vertex write's outcode block: lanes 0..3 and 4..7 of v29:v28 hold
 * the two clip positions. Returns the 32-bit flags word whose low half
 * is the first vertex's record halfword and high half the second's.
 * Revision B leaves z out of the screen-plane bits and packs both w
 * signs into the second vertex's halfword; revision A keeps z and packs
 * no signs. */
static uint32_t vtx_outcodes(F3dVu *u)
{
    uint32_t t0, t5, t6 = 0;
    const int rev_b = (u->rev == F3D_TRI_REV_B);
    lsv(21, 0, D_GUARD);
    vmudn(20, 28, 21, E0);
    if (rev_b)
        t5 = ((uint32_t)mfc2(u, 29, 3) & 0x8000u) >> 13;
    vmadh(21, 29, 21, E0);
    if (rev_b)
    {
        t6 = ((uint32_t)mfc2(u, 29, 7) & 0x8000u) >> 9;
        t6 |= t5;
    }
    vch(u, 3, 29, 29, H3);
    vcl(u, 3, 28, 28, H3);
    t5 = vcc_word(u);
    vch(u, 3, 29, 21, H3);
    vcl(u, 3, 28, 20, H3);
    t0 = t5 & (rev_b ? 0x703u : 0x707u);
    t5 = (t5 & (rev_b ? 0x7030u : 0x7070u)) | t6;
    t0 <<= 4;
    t5 <<= 16;
    t5 |= t0;
    t6 = vcc_word(u);
    t0 = t6 & 0x707;
    t6 = (t6 & 0x7070) << 12;
    return t0 | t6 | t5;
}

VU_OP void load_pos(F3dVu *u, int lane0, const int32_t p[4])
{
    int i;
    for (i = 0; i < 4; i++)
    {
        u->v[29][lane0 + i] = (int16_t)((uint32_t)p[i] >> 16);
        u->v[28][lane0 + i] = (int16_t)((uint32_t)p[i] & 0xffff);
    }
}

void f3d_vtx_flags(int rev, const int32_t a[4], const int32_t *b, int guard,
                   int32_t *fa, int32_t *fb)
{
    static F3dVu vu;
    F3dVu *u = &vu;
    static const int32_t zero[4] = { 0, 0, 0, 0 };
    uint32_t w;
    /* every register, flag and accumulator lane the outcodes read is
     * written first, so the unit needs no clearing */
    u->rev = rev;
    dm_w16(u, D_GUARD, (uint16_t)guard);
    load_pos(u, 0, a);
    load_pos(u, 4, b ? b : zero);
    w = vtx_outcodes(u);
    if (fa)
        *fa = (int32_t)(w & 0xffff);
    if (fb)
        *fb = (int32_t)(w >> 16);
}

/* The segment the vertex write runs from IMEM 0x4c0: viewport rows into
 * v0 (scale, y negated) and v1 (translate), perspNorm into v19[0]. */
static void vtx_setup_regs(F3dVu *u)
{
    lqv(3, D_SIGN);
    lsv(19, 0, D_SP + 2);
    ldv(0, 0, D_VIEWPORT);
    ldv(1, 0, D_VIEWPORT + 8);
    ldv(0, 8, D_VIEWPORT);
    ldv(1, 8, D_VIEWPORT + 8);
    vmudh(0, 0, 3, 0);
}

/* The vertex write for one vertex at a3 whose clip position is in lanes
 * 0..3 of v29:v28, packed colour t7 and texel S/T in v18 lanes 0..1. */
static void vtx_finish(F3dVu *u, uint32_t a3, uint32_t t7)
{
    uint32_t t0 = vtx_outcodes(u);
    int fog = (int)((dm_lw(u, D_SP + 4) >> 16) & 1);
    vadd(21, 29, 31, E0);
    vadd(20, 28, 31, E0);
    vmudl(28, 28, 19, E0);
    vmadm(29, 29, 19, E0);
    vmadn(28, 31, 31, E0);
    dm_w16(u, a3 + 0x24, t0 & 0xffff);
    recip_lanes37(u);
    vge(6, 27, 31, E0);
    sdv(21, 0, a3 + 0x00);
    vmrg(u, 6, 27, 30, E0);
    sdv(20, 0, a3 + 0x08);
    vmudl(5, 20, 26, H3);
    vmadm(5, 21, 26, H3);
    vmadn(5, 20, 6, H3);
    vmadh(4, 21, 6, H3);
    vmudl(5, 5, 19, E0);
    vmadm(4, 4, 19, E0);
    vmadn(5, 31, 31, E0);
    vmudh(7, 1, 31, E1);
    vmadn(7, 5, 0, 0);
    ldv(29, 0, D_CLAMP);
    vmadh(6, 4, 0, 0);
    ldv(29, 8, D_CLAMP);
    vmadn(7, 31, 31, E0);
    vge(6, 6, 29, Q1);
    dm_w32(u, a3 + 0x10, t7);
    vlt(6, 6, 29, Q0);
    if (fog)
    {
        lqv(3, D_FOG);
        vmudn(5, 5, 3, E0);
        vmadh(4, 4, 3, E0);
        vadd(4, 4, 3, E1);
        vge(4, 4, 31, E0);
        vlt(4, 4, 3, E2);
    {
        DM_DIRTY(u, a3 + 0x13);
        u->dmem[(a3 + 0x13) & 0xfff] = vbyte_get(u, 4, 5);
    }
    }
    slv(18, 0, a3 + 0x14);
    sdv(6, 0, a3 + 0x18);
    ssv(7, 4, a3 + 0x1e);
    ssv(27, 6, a3 + 0x20);
    ssv(26, 6, a3 + 0x22);
}

/* The intersection of the edge from the outside vertex t2 to the inside
 * vertex s4 with clip plane a2, written as a new vertex record at a3. */
static void clip_intersect(F3dVu *u, uint32_t t2, uint32_t s4, uint32_t a2,
                           uint32_t a3)
{
    int32_t t0;
    ldv(9, 0, t2 + 0x00);
    ldv(10, 0, t2 + 0x08);
    ldv(4, 0, s4 + 0x00);
    ldv(5, 0, s4 + 0x08);
    ldv(1, 0, D_PLANES + (a2 << 2));
    vmudh(0, 1, 31, E3);
    vmudn(12, 5, 1, 0);
    vmadh(11, 4, 1, 0);
    vmadn(12, 31, 31, E0);
    vmadn(28, 10, 0, 0);
    vmadh(29, 9, 0, 0);
    vmadn(28, 31, 31, E0);
    vaddc(26, 28, 28, Q0);
    vadd(27, 29, 29, Q0);
    vaddc(28, 26, 26, H1);
    vadd(29, 27, 27, H1);
    t0 = mfc2(u, 29, 3);
    vrcph(u, 7, 3, 29, E3);
    vrcpl(u, 3, 3, 28, E3);
    vrcph(u, 7, 3, 31, E0);
    vmudn(3, 3, 31, E2);
    vmadh(7, 7, 31, E2);
    if (t0 < 0)
    {
        vmudn(3, 3, 31, E3);
        vmadh(7, 7, 31, E3);
    }
    veq(u, 7, 7, 31, E0);
    vmrg(u, 3, 3, 31, E3);
    vmudl(28, 28, 3, E3);
    vmadm(29, 29, 3, E3);
    vmadn(28, 31, 31, E0);
    recip_lanes37(u);
    vaddc(28, 12, 12, Q0);
    vadd(29, 11, 11, Q0);
    vaddc(12, 28, 28, H1);
    vadd(11, 29, 29, H1);
    vmudl(15, 12, 26, 0);
    vmadm(15, 11, 26, 0);
    vmadn(15, 12, 27, 0);
    vmadh(8, 11, 27, 0);
    vmudl(28, 31, 31, E5);
    vmadl(15, 15, 3, E3);
    vmadm(8, 8, 3, E3);
    vmadn(15, 31, 31, E0);
    veq(u, 8, 8, 31, E0);
    vmrg(u, 15, 15, 31, E3);
    vne(u, 15, 15, 31, E0);
    vmrg(u, 15, 15, 31, E1);
    vnxor(u, 8, 15, 31, E0);
    vaddc(8, 8, 31, E1);
    vadd(29, 29, 29, 0);
    vmudl(28, 5, 8, H3);
    vmadm(29, 4, 8, H3);
    vmadl(28, 10, 15, H3);
    vmadm(29, 9, 15, H3);
    vmadn(28, 31, 31, E0);
    luv(u, 12, t2 + 0x10);
    luv(u, 11, s4 + 0x10);
    llv(12, 8, t2 + 0x14);
    llv(11, 8, s4 + 0x14);
    vmudm(18, 12, 15, E3);
    vmadm(18, 11, 8, E3);
    suv(u, 18, a3);
    sdv(18, 8, a3 + 0x08);
    ldv(18, 0, a3 + 0x08);
    vtx_setup_regs(u);
    vtx_finish(u, a3, dm_lw(u, a3));
}

/* whether the last rsp_tri_f3d call was culled on its face by the direct
 * write (not by the trivial reject, a zero area or inside the clipper's
 * fan): Mario Kart 64's quad handler skips its second half then */
static int s_last_face_cull;

int rsp_tri_f3d_face_culled(void)
{
    return s_last_face_cull;
}

int rsp_tri_f3d(int32_t *ew, int max_words, const RspTriVtx *v1c,
                const RspTriVtx *v2c, const RspTriVtx *v3c,
                const F3dTriState *st)
{
    /* plane masks by the plane counter before its decrement (0x0c..0x02):
     * near (w), far (z - w), y - w, x - w, y + w, x + w */
    static const uint32_t plane_mask[7] = { 0, 0x1, 0x2, 0x100, 0x200,
                                            0x4000, 0x40 };
    static F3dVu vu;
    F3dVu *u = &vu;
    uint32_t at = D_VTX, v0 = D_VTX + 0x28, v1 = D_VTX + 0x50, a0;
    uint32_t a1, a2, a3, fp, t0, t2, t3, t6, s1, s2, s4, ret;
    uint32_t f1, f2, f3;
    int words = 0;

    vu_setup(u, st);
    put_vtx(u, at, v1c);
    put_vtx(u, v0, v2c);
    put_vtx(u, v1, v3c);
    a0 = D_VTX + 0x28 * (unsigned int)(st->flat_vtx % 3);

    s_last_face_cull = 0;
    f1 = (uint32_t)v1c->f3d_flags;
    f2 = (uint32_t)v2c->f3d_flags;
    f3 = (uint32_t)v3c->f3d_flags;
    if ((f1 & f2 & f3 & (st->rev == F3D_TRI_REV_B ? 0x7030u : 0x7070u)) != 0)
        return 0;
    if (((f1 | f2 | f3) & 0x4343u) == 0)
    {
        if (max_words < 44)
            return 0;
        s_face_culled = 0;
        words = tri_core(u, at, v0, v1, a0, st, ew);
        s_last_face_cull = s_face_culled;
        return words;
    }

    /* clip: the polygon list starts in reverse command order */
    dm_w16(u, D_LIST + 0, v1);
    dm_w16(u, D_LIST + 2, v0);
    dm_w16(u, D_LIST + 4, at);
    dm_w16(u, D_LIST + 6, 0);
    a3 = D_GEN;
    fp = D_LIST;
    a2 = 0x0c;
    t2 = 0;
    s4 = 0;
    u->v[13][0] = 0;

next_plane_swap:
    a1 = fp;
    fp ^= 0x14;
next_plane:
    if (a2 == 0)
        goto draw;
    t3 = plane_mask[a2 >> 1];
    a2 -= 2;
    s1 = 0;
    s2 = 0;
    {
        uint32_t v0c = a1;
        t6 = fp + 2;
        for (;;)
        {
            /* 80c: the previous vertex is copied out */
            s4 = t2;
            dm_w16(u, t6, t2);
            t6 += 2;
        fetch:
            t2 = (uint32_t)(dm_lh(u, v0c) & 0xffff);
            if (t2 == 0)
            {
                if (s1 > 2)
                {
                    v0c = a1;           /* second pass from the start */
                    goto fetch;
                }
                if (s1 == 2)
                    goto next_plane;    /* entirely inside: list kept */
                return words;           /* entirely outside */
            }
            t0 = (uint32_t)dm_lh(u, t2 + 0x24) & t3;
            v0c += 2;
            if (t0 != s2)
                continue;
            /* a transition */
            s2 ^= t3;
            s1 += 2;
            if (s1 == 2)
            {
                /* first inside vertex: restart the walk */
                v0c = a1;
                t6 = fp + 2;
                continue;
            }
            if (s1 == 4)
            {
                /* first outside vertex after it: restart the output */
                t6 = fp + 2;
                continue;
            }
            if (s1 == 6)
            {
                /* entering: the intersection goes first in the list */
                uint32_t cur = t2;
                u->v[13][0] = (int16_t)cur;
                t2 = s4;
                s4 = (uint32_t)(int32_t)u->v[13][0] & 0xffff;
                t6 = fp;
                ret = 1;
            }
            else if (s1 == 8)
                ret = 0;
            else
                return words;
            a3 += 0x28;
            dm_w16(u, t6, a3);
            dm_w16(u, t6 + 2, 0);
            clip_intersect(u, t2, s4, a2, a3);
            t2 = (uint32_t)(int32_t)u->v[13][0] & 0xffff;
            if (ret == 0)
                goto next_plane_swap;
            t6 = fp + 2;
        }
    }

draw:
    {
        uint32_t pivot = (uint32_t)dm_lh(u, a1) & 0xffff;
        uint32_t cur = a1;
        for (;;)
        {
            uint32_t b = (uint32_t)dm_lh(u, cur + 2) & 0xffff;
            uint32_t c = (uint32_t)dm_lh(u, cur + 4) & 0xffff;
            int n;
            cur += 2;
            if (c == 0 || words + 44 > max_words)
                break;
            n = tri_core(u, c, b, pivot, a0, st, ew + words);
            if (n > 0)
                words += n;
        }
    }
    return words;
}
