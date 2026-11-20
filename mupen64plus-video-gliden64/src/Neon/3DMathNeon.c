/* 3DMath.h's matrix and vector functions with NEON: the operations of the
 * C++ they replace, in the same order, so the results are the same to the
 * bit. Lanes are read with vget(q)_lane_f32 and vectors made from arrays,
 * not subscripted or brace-initialised, so MSVC's ARM64 compiler takes it.
 * C89. */
#include <arm_neon.h>
#include <retro_inline.h>

#include "3DMath.h"
#include "Types.h"

/* the sum of the squares of @lo's two lanes and @hi's first, in the first
 * lane */
static INLINE float32x2_t dot3(float32x2_t lo, float32x2_t hi)
{
	float32x2_t t = vmul_f32(lo, lo);
	t = vpadd_f32(t, t);
	return vmla_f32(t, hi, hi);
}

/* 1/sqrt(@t), the estimate and two Newton-Raphson steps */
static INLINE float32x2_t rsqrt2(float32x2_t t)
{
	float32x2_t est = vrsqrte_f32(t);
	float32x2_t prod = vmul_f32(est, t);
	float32x2_t recp = vrsqrts_f32(prod, est);
	est = vmul_f32(est, recp);
	prod = vmul_f32(est, t);
	recp = vrsqrts_f32(prod, est);
	return vmul_f32(est, recp);
}

static INLINE float32x4_t rsqrt4(float32x4_t t)
{
	float32x4_t est = vrsqrteq_f32(t);
	float32x4_t prod = vmulq_f32(est, t);
	float32x4_t recp = vrsqrtsq_f32(prod, est);
	est = vmulq_f32(est, recp);
	prod = vmulq_f32(est, t);
	recp = vrsqrtsq_f32(prod, est);
	return vmulq_f32(est, recp);
}

/* @p scaled to unit length by its first three lanes */
static INLINE float32x4_t normalized(float32x4_t p)
{
	const float32x2_t scale = rsqrt2(dot3(vget_low_f32(p), vget_high_f32(p)));
	return vmulq_n_f32(p, vget_lane_f32(scale, 0));
}

static INLINE void store3(float *dst, float32x4_t p)
{
	dst[0] = vgetq_lane_f32(p, 0);
	dst[1] = vgetq_lane_f32(p, 1);
	dst[2] = vgetq_lane_f32(p, 2);
}

/* the sums of squares of @n vectors, one a lane, the rest 0 */
static INLINE float32x4_t dots(const float32x4_t *p, unsigned n)
{
	float t[4] = { 0.f, 0.f, 0.f, 0.f };
	unsigned i;
	for (i = 0; i < n; ++i)
		t[i] = vget_lane_f32(dot3(vget_low_f32(p[i]), vget_high_f32(p[i])), 0);
	return vld1q_f32(t);
}

void gli_MultMatrix(float m0[4][4], float m1[4][4], float dest[4][4])
{
	float32x4x4_t a, b, d;
	a.val[0] = vld1q_f32(m0[0]);
	a.val[1] = vld1q_f32(m0[1]);
	a.val[2] = vld1q_f32(m0[2]);
	a.val[3] = vld1q_f32(m0[3]);
	b.val[0] = vld1q_f32(m1[0]);
	b.val[1] = vld1q_f32(m1[1]);
	b.val[2] = vld1q_f32(m1[2]);
	b.val[3] = vld1q_f32(m1[3]);

	d.val[0] = vmulq_n_f32(a.val[0], vgetq_lane_f32(b.val[0], 0));
	d.val[1] = vmulq_n_f32(a.val[0], vgetq_lane_f32(b.val[1], 0));
	d.val[2] = vmulq_n_f32(a.val[0], vgetq_lane_f32(b.val[2], 0));
	d.val[3] = vmulq_n_f32(a.val[0], vgetq_lane_f32(b.val[3], 0));
	d.val[0] = vmlaq_n_f32(d.val[0], a.val[1], vgetq_lane_f32(b.val[0], 1));
	d.val[1] = vmlaq_n_f32(d.val[1], a.val[1], vgetq_lane_f32(b.val[1], 1));
	d.val[2] = vmlaq_n_f32(d.val[2], a.val[1], vgetq_lane_f32(b.val[2], 1));
	d.val[3] = vmlaq_n_f32(d.val[3], a.val[1], vgetq_lane_f32(b.val[3], 1));
	d.val[0] = vmlaq_n_f32(d.val[0], a.val[2], vgetq_lane_f32(b.val[0], 2));
	d.val[1] = vmlaq_n_f32(d.val[1], a.val[2], vgetq_lane_f32(b.val[1], 2));
	d.val[2] = vmlaq_n_f32(d.val[2], a.val[2], vgetq_lane_f32(b.val[2], 2));
	d.val[3] = vmlaq_n_f32(d.val[3], a.val[2], vgetq_lane_f32(b.val[3], 2));
	d.val[0] = vmlaq_n_f32(d.val[0], a.val[3], vgetq_lane_f32(b.val[0], 3));
	d.val[1] = vmlaq_n_f32(d.val[1], a.val[3], vgetq_lane_f32(b.val[1], 3));
	d.val[2] = vmlaq_n_f32(d.val[2], a.val[3], vgetq_lane_f32(b.val[2], 3));
	d.val[3] = vmlaq_n_f32(d.val[3], a.val[3], vgetq_lane_f32(b.val[3], 3));

	vst1q_f32(dest[0], d.val[0]);
	vst1q_f32(dest[1], d.val[1]);
	vst1q_f32(dest[2], d.val[2]);
	vst1q_f32(dest[3], d.val[3]);
}

void gli_MultMatrix2(float m0[4][4], float m1[4][4])
{
	gli_MultMatrix(m0, m1, m0);
}

void gli_TransformVectorNormalize(float *vec, float mtx[4][4])
{
	float32x4_t p = vmulq_n_f32(vld1q_f32(mtx[0]), vec[0]);
	p = vmlaq_n_f32(p, vld1q_f32(mtx[1]), vec[1]);
	p = vmlaq_n_f32(p, vld1q_f32(mtx[2]), vec[2]);
	store3(vec, normalized(p));
}

void gli_InverseTransformVectorNormalize(float *src, float *dst, float mtx[4][4])
{
	/* the matrix's columns */
	const float32x4x4_t m = vld4q_f32(mtx[0]);
	float32x4_t p = vmulq_n_f32(m.val[0], src[0]);
	p = vmlaq_n_f32(p, m.val[1], src[1]);
	p = vmlaq_n_f32(p, m.val[2], src[2]);
	store3(dst, normalized(p));
}

/* @n of them, 4 or 7, each scaled by its own lane of one estimate */
static void inverse_transform_normalize(float src[][3], float dst[][3], float mtx[4][4], unsigned n)
{
	const float32x4x4_t m = vld4q_f32(mtx[0]);
	float32x4_t p[8];
	float32x4_t scale[2];
	unsigned i;

	for (i = 0; i < n; ++i)
		p[i] = vmulq_n_f32(m.val[0], src[i][0]);
	for (i = 0; i < n; ++i)
		p[i] = vmlaq_n_f32(p[i], m.val[1], src[i][1]);
	for (i = 0; i < n; ++i)
		p[i] = vmlaq_n_f32(p[i], m.val[2], src[i][2]);
	scale[0] = rsqrt4(dots(p, 4));
	if (n > 4)
		scale[1] = rsqrt4(dots(p + 4, n - 4));
	for (i = 0; i < n; ++i) {
		float s;
		switch (i & 3) {
		case 0: s = vgetq_lane_f32(scale[i >> 2], 0); break;
		case 1: s = vgetq_lane_f32(scale[i >> 2], 1); break;
		case 2: s = vgetq_lane_f32(scale[i >> 2], 2); break;
		default: s = vgetq_lane_f32(scale[i >> 2], 3); break;
		}
		store3(dst[i], vmulq_n_f32(p[i], s));
	}
}

void gli_Normalize(float *v)
{
	float in[4];
	in[0] = v[0];
	in[1] = v[1];
	in[2] = v[2];
	in[3] = 0.f;
	store3(v, normalized(vld1q_f32(in)));
}

void gli_InverseTransformVectorNormalizeN(float src[][3], float dst[][3], float mtx[4][4], u32 count)
{
	while (count >= 7) {
		inverse_transform_normalize(src + count - 7, dst + count - 7, mtx, 7);
		count -= 7;
	}
	while (count >= 4) {
		inverse_transform_normalize(src + count - 4, dst + count - 4, mtx, 4);
		count -= 4;
	}
	while (count >= 1) {
		gli_InverseTransformVectorNormalize(src[count - 1], dst[count - 1], mtx);
		count--;
	}
}

void gli_CopyMatrix(float m0[4][4], float m1[4][4])
{
	/* 16 floats, through registers */
	vst4q_f32(m0[0], vld4q_f32(m1[0]));
}
