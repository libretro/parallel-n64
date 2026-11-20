/* gSP's vertex and vector transforms and its lighting with NEON, for
 * gSP.c and gSP_vertex.inc: the operations of the C++ they replace, in the
 * same order, so the results are the same to the bit. C89. */
#include <arm_neon.h>

#include "Types.h"
#include "gSP.h"
#include "3DMath.h"
#include "Config.h"
#include "gli_drawer.h"

/* std::min's: @b where it is less than @a */
static float min_of(float a, float b)
{
	return b < a ? b : a;
}

void gSPTransformVertex4NEON(u32 v, float mtx[4][4])
{
	SPVertex *vtx = &gli_tris.vertices[v];
	float32x4x4_t m, out;
	float32x4_t in[4];
	unsigned i;

	for (i = 0; i < 4; ++i)
		in[i] = vld1q_f32(&vtx[i].x);
	m.val[0] = vld1q_f32(mtx[0]);
	m.val[1] = vld1q_f32(mtx[1]);
	m.val[2] = vld1q_f32(mtx[2]);
	m.val[3] = vld1q_f32(mtx[3]);
	for (i = 0; i < 4; ++i)
		out.val[i] = vmlaq_n_f32(m.val[3], m.val[0], vgetq_lane_f32(in[i], 0));
	for (i = 0; i < 4; ++i)
		out.val[i] = vmlaq_n_f32(out.val[i], m.val[1], vgetq_lane_f32(in[i], 1));
	for (i = 0; i < 4; ++i)
		out.val[i] = vmlaq_n_f32(out.val[i], m.val[2], vgetq_lane_f32(in[i], 2));
	for (i = 0; i < 4; ++i)
		vst1q_f32(&vtx[i].x, out.val[i]);
}

/* vertices @v to @v + 3 moved by vertex 0 */
void gSPBillboardVertex4NEON(u32 v)
{
	const float32x4_t origin = vld1q_f32(&gli_tris.vertices[0].x);
	SPVertex *vtx = &gli_tris.vertices[v];
	float32x4_t p[4];
	unsigned i;

	for (i = 0; i < 4; ++i)
		p[i] = vld1q_f32(&vtx[i].x);
	for (i = 0; i < 4; ++i)
		vst1q_f32(&vtx[i].x, vaddq_f32(p[i], origin));
}

void gSPTransformVector_NEON(float vtx[4], float mtx[4][4])
{
	const float32x4_t v = vld1q_f32(vtx);
	float32x4_t out = vmlaq_n_f32(vld1q_f32(mtx[3]), vld1q_f32(mtx[0]), vgetq_lane_f32(v, 0));
	out = vmlaq_n_f32(out, vld1q_f32(mtx[1]), vgetq_lane_f32(v, 1));
	out = vmlaq_n_f32(out, vld1q_f32(mtx[2]), vgetq_lane_f32(v, 2));
	vst1q_f32(vtx, out);
}

void gSPInverseTransformVector_NEON(float vec[3], float mtx[4][4])
{
	/* the matrix's columns */
	const float32x4x4_t m = vld4q_f32(mtx[0]);
	float32x4_t out = vmulq_n_f32(m.val[0], vec[0]);
	out = vmlaq_n_f32(out, m.val[1], vec[1]);
	out = vmlaq_n_f32(out, m.val[2], vec[2]);
	vec[0] = vgetq_lane_f32(out, 0);
	vec[1] = vgetq_lane_f32(out, 1);
	vec[2] = vgetq_lane_f32(out, 2);
}

/* the light of seven lights (@dirs, their colours @rgb) on the normal @n,
 * each facing it, added to @out: four lights, two and one */
static void dot_product_max7(const float *n, float dirs[7][3], float rgb[7][3], float *out)
{
	const float32x4x3_t d0 = vld3q_f32(dirs[0]);
	const float32x2x3_t d1 = vld3_f32(dirs[4]);
	float32x2x3_t d2 = vld3_dup_f32(dirs[6]);
	float32x4x3_t c0 = vld3q_f32(rgb[0]);
	float32x2x3_t c1 = vld3_f32(rgb[4]);
	const float32x2x3_t c2 = vld3_dup_f32(rgb[6]);
	const float32x4_t zero = vmovq_n_f32(0.f);
	const float32x2_t zero2 = vmov_n_f32(0.f);
	float32x4_t p0;
	float32x2_t p1, p2, s0, s1, s2;

	d2.val[0] = vset_lane_f32(0.f, d2.val[0], 1);
	d2.val[1] = vset_lane_f32(0.f, d2.val[1], 1);
	d2.val[2] = vset_lane_f32(0.f, d2.val[2], 1);

	p0 = vmulq_n_f32(d0.val[0], n[0]);
	p1 = vmul_n_f32(d1.val[0], n[0]);
	p2 = vmul_n_f32(d2.val[0], n[0]);
	p0 = vmlaq_n_f32(p0, d0.val[1], n[1]);
	p1 = vmla_n_f32(p1, d1.val[1], n[1]);
	p2 = vmla_n_f32(p2, d2.val[1], n[1]);
	p0 = vmlaq_n_f32(p0, d0.val[2], n[2]);
	p1 = vmla_n_f32(p1, d1.val[2], n[2]);
	p2 = vmla_n_f32(p2, d2.val[2], n[2]);

	p0 = vmaxq_f32(p0, zero);
	p1 = vmax_f32(p1, zero2);
	p2 = vmax_f32(p2, zero2);

	c0.val[0] = vmulq_f32(c0.val[0], p0);
	c1.val[0] = vmul_f32(c1.val[0], p1);
	c1.val[0] = vmla_f32(c1.val[0], c2.val[0], p2);
	c0.val[1] = vmulq_f32(c0.val[1], p0);
	c1.val[1] = vmul_f32(c1.val[1], p1);
	c1.val[1] = vmla_f32(c1.val[1], c2.val[1], p2);
	c0.val[2] = vmulq_f32(c0.val[2], p0);
	c1.val[2] = vmul_f32(c1.val[2], p1);
	c1.val[2] = vmla_f32(c1.val[2], c2.val[2], p2);

	s0 = vadd_f32(vget_high_f32(c0.val[0]), vget_low_f32(c0.val[0]));
	s1 = vadd_f32(vget_high_f32(c0.val[1]), vget_low_f32(c0.val[1]));
	s2 = vadd_f32(vget_high_f32(c0.val[2]), vget_low_f32(c0.val[2]));
	s0 = vadd_f32(s0, c1.val[0]);
	s1 = vadd_f32(s1, c1.val[1]);
	s2 = vadd_f32(s2, c1.val[2]);
	s0 = vpadd_f32(s0, s0);
	s1 = vpadd_f32(s1, s1);
	s2 = vpadd_f32(s2, s2);

	out[0] += vget_lane_f32(s0, 0);
	out[1] += vget_lane_f32(s1, 0);
	out[2] += vget_lane_f32(s2, 0);
}

/* the same of four */
static void dot_product_max4(const float *n, float dirs[4][3], float rgb[4][3], float *out)
{
	const float32x4x3_t d = vld3q_f32(dirs[0]);
	float32x4x3_t c = vld3q_f32(rgb[0]);
	float32x4_t p;
	float32x2_t s0, s1, s2;

	p = vmulq_n_f32(d.val[0], n[0]);
	p = vmlaq_n_f32(p, d.val[1], n[1]);
	p = vmlaq_n_f32(p, d.val[2], n[2]);
	p = vmaxq_f32(p, vmovq_n_f32(0.f));

	c.val[0] = vmulq_f32(c.val[0], p);
	c.val[1] = vmulq_f32(c.val[1], p);
	c.val[2] = vmulq_f32(c.val[2], p);

	s0 = vadd_f32(vget_high_f32(c.val[0]), vget_low_f32(c.val[0]));
	s1 = vadd_f32(vget_high_f32(c.val[1]), vget_low_f32(c.val[1]));
	s2 = vadd_f32(vget_high_f32(c.val[2]), vget_low_f32(c.val[2]));
	s0 = vpadd_f32(s0, s0);
	s1 = vpadd_f32(s1, s1);
	s2 = vpadd_f32(s2, s2);

	out[0] += vget_lane_f32(s0, 0);
	out[1] += vget_lane_f32(s1, 0);
	out[2] += vget_lane_f32(s2, 0);
}

void gSPLightVertex_NEON(u32 vnum, u32 v, SPVertex *spVtx)
{
	u32 j;
	if (!gli_isHWLightingAllowed()) {
		for (j = 0; j < vnum; ++j) {
			SPVertex *vtx = &spVtx[v + j];
			s32 count = (s32)(gSP.numLights - 1);
			vtx->r = gSP.lights.rgb[gSP.numLights][R];
			vtx->g = gSP.lights.rgb[gSP.numLights][G];
			vtx->b = gSP.lights.rgb[gSP.numLights][B];
			vtx->HWLight = 0;

			while (count >= 6) {
				dot_product_max7(&vtx->nx, (float (*)[3])gSP.lights.i_xyz[gSP.numLights - count - 1],
					(float (*)[3])gSP.lights.rgb[gSP.numLights - count - 1], &vtx->r);
				count -= 7;
			}
			while (count >= 3) {
				dot_product_max4(&vtx->nx, (float (*)[3])gSP.lights.i_xyz[gSP.numLights - count - 1],
					(float (*)[3])gSP.lights.rgb[gSP.numLights - count - 1], &vtx->r);
				count -= 4;
			}
			while (count >= 0) {
				const f32 intensity = DotProduct(&vtx->nx, gSP.lights.i_xyz[gSP.numLights - count - 1]);
				if (intensity > 0.0f) {
					vtx->r += gSP.lights.rgb[gSP.numLights - count - 1][R] * intensity;
					vtx->g += gSP.lights.rgb[gSP.numLights - count - 1][G] * intensity;
					vtx->b += gSP.lights.rgb[gSP.numLights - count - 1][B] * intensity;
				}
				count -= 1;
			}
			vtx->r = min_of(1.0f, vtx->r);
			vtx->g = min_of(1.0f, vtx->g);
			vtx->b = min_of(1.0f, vtx->b);
		}
	} else {
		for (j = 0; j < vnum; ++j) {
			SPVertex *vtx = &spVtx[v + j];
			gli_TransformVectorNormalize(&vtx->r, gSP.matrix.modelView[gSP.matrix.modelViewi]);
			vtx->HWLight = (u8)gSP.numLights;
		}
	}
}
