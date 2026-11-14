#include <stddef.h>

#include "Types.h"
#include "gSP.h"
#include "GBI.h"
#include "gli_tri_clip.h"

/* What the RSP clips a triangle against: the near plane, z >= -w, unless
 * the microcode has no near clipping (NoN); the far plane, z <= w; and the
 * four sides of the guard band, the screen scaled by the clip ratio, which
 * also keep every vertex in front of the eye */
#define NUM_PLANES 6

/* x to t: every attribute of a vertex is a float ahead of modify */
#define NUM_FLOATS (offsetof(SPVertex, modify) / sizeof(f32))

static f32 guard_ratio(void)
{
	return gSP.clipRatio > 1 ? (f32)gSP.clipRatio : 1.0f;
}

static f32 plane_distance(const SPVertex *_v, u32 _plane, f32 _ratio)
{
	switch (_plane) {
	case 0: return gli_GBI->NoN ? 1.0f : _v->z + _v->w;
	case 1: return _v->w - _v->z;
	case 2: return _ratio * _v->w - _v->x;
	case 3: return _ratio * _v->w + _v->x;
	case 4: return _ratio * _v->w - _v->y;
	default: return _ratio * _v->w + _v->y;
	}
}

/* The vertex at @_t from @_a to @_b, every attribute taken along the
 * edge in clip space, as the RSP makes the vertices of a clipped polygon */
static void lerp_vertex(SPVertex *_out, const SPVertex *_a, const SPVertex *_b, f32 _t)
{
	const f32 *a = &_a->x;
	const f32 *b = &_b->x;
	f32 *o = &_out->x;
	u32 i;
	*_out = *_a;
	for (i = 0; i < NUM_FLOATS; ++i)
		o[i] = a[i] + (b[i] - a[i]) * _t;
}

u32 gli_tri_clip_vertex_outside(const SPVertex *_v)
{
	const f32 r = guard_ratio();
	u32 plane;
	if (_v->modify != 0)
		return 0;
	for (plane = 0; plane < NUM_PLANES; ++plane)
		if (plane_distance(_v, plane, r) < 0.0f)
			return 1;
	return 0;
}

u32 gli_tri_clip_default_ratio(u32 _type)
{
	/* F3D builds load 1, the later microcodes 2; the game may set its own */
	return _type == F3D || _type == F3DGOLDEN ? 1 : 2;
}

u32 gli_tri_clip(const SPVertex *_vertices, const u16 *_elements, u32 _count, SPVertex *_out)
{
	/* a triangle against six planes has at most nine corners */
	SPVertex poly[2][3 + NUM_PLANES];
	const f32 r = guard_ratio();
	u32 written = 0;
	u32 tri;

	for (tri = 0; tri + 2 < _count; tri += 3) {
		u32 n = 3, cur = 0, plane, i;
		for (i = 0; i < 3; ++i)
			poly[0][i] = _vertices[_elements != NULL ? _elements[tri + i] : tri + i];

		if (poly[0][0].modify == 0 && poly[0][1].modify == 0 && poly[0][2].modify == 0) {
			for (plane = 0; plane < NUM_PLANES && n != 0; ++plane) {
				const SPVertex *in = poly[cur];
				SPVertex *out = poly[cur ^ 1];
				u32 m = 0, inside = 0;
				for (i = 0; i < n; ++i)
					inside += plane_distance(&in[i], plane, r) >= 0.0f;
				if (inside == n)
					continue;
				for (i = 0; i < n; ++i) {
					const SPVertex *a = &in[i];
					const SPVertex *b = &in[(i + 1) % n];
					const f32 da = plane_distance(a, plane, r);
					const f32 db = plane_distance(b, plane, r);
					if (da >= 0.0f)
						out[m++] = *a;
					if ((da >= 0.0f) != (db >= 0.0f))
						lerp_vertex(&out[m++], a, b, da / (da - db));
				}
				n = m;
				cur ^= 1;
			}
		}

		/* the polygon as a fan, wound as the triangle was */
		for (i = 1; i + 1 < n; ++i) {
			_out[written++] = poly[cur][0];
			_out[written++] = poly[cur][i];
			_out[written++] = poly[cur][i + 1];
		}
	}
	return written;
}
