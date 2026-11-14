#include <math.h>

#include "Types.h"
#include "GBI.h"
#include "gSP.h"
#include "gli_lle_tri.h"

#define Z_SCALE 4294901760.0 /* 0xffff0000 */

/* The corner as it always was: shade to 8 bits, S and T divided by W in
 * fixed point, W offset as GLideN64 has it */
static void corner_vertex(const gli_lle_corner *_c, const gli_lle_mode *_m, SPVertex *_v)
{
	if (_m->shade) {
#define CSCALE(c) _FIXED2FLOATCOLOR((((c) > 0x3ff0000 ? 0x3ff0000 : ((c) < 0 ? 0 : (c))) >> 18), 8)
		_v->r = CSCALE(_c->r);
		_v->g = CSCALE(_c->g);
		_v->b = CSCALE(_c->b);
		_v->a = CSCALE(_c->a);
#undef CSCALE
	}
	if (_m->texture) {
		if (_m->persp) {
			const s64 w = _c->w != 0 ? _c->w : 1;
			_v->s = (f32)((((s64)_c->s) << 20) / w) / 1024.0f;
			_v->t = (f32)((((s64)_c->t) << 20) / w) / 1024.0f;
		} else {
			_v->s = (f32)_c->s / (1 << 21);
			_v->t = (f32)_c->t / (1 << 21);
		}
	}
	_v->x = _c->x;
	_v->y = _c->y;
	_v->z = _m->zprim ? _m->primZ : (f32)((f64)_c->z / Z_SCALE);
	_v->w = _m->persp ? 1.0f / (f32)((f64)(u32)((u32)_c->w + 0x10000u) / Z_SCALE) : 1.0f;
}

/* A point of a cut triangle, every value along screen space */
typedef struct
{
	f64 x, y, r, g, b, a, z, s, t, w;
} point;

static void to_point(const gli_lle_corner *_c, point *_p)
{
	_p->x = _c->x; _p->y = _c->y;
	_p->r = _c->r; _p->g = _c->g; _p->b = _c->b; _p->a = _c->a;
	_p->z = _c->z;
	_p->s = _c->s; _p->t = _c->t; _p->w = _c->w;
}

static void lerp(const point *_a, const point *_b, f64 _t, point *_o)
{
	const f64 *a = &_a->x, *b = &_b->x;
	f64 *o = &_o->x;
	u32 i;
	for (i = 0; i < sizeof(point) / sizeof(f64); ++i)
		o[i] = a[i] + (b[i] - a[i]) * _t;
}

static f32 color(f64 _c)
{
	if (_c > (f64)0x3ff0000)
		_c = (f64)0x3ff0000;
	if (_c < 0.0)
		_c = 0.0;
	return (f32)floor(_c / 262144.0) * FIXED2FLOATRECIPCOLOR8;
}

/* A point where W is positive: S/W and T/W at it, and W itself for the
 * GPU to interpolate them by, as the RDP divides them */
static void point_vertex(const point *_p, const gli_lle_mode *_m, SPVertex *_v)
{
	if (_m->shade) {
		_v->r = color(_p->r);
		_v->g = color(_p->g);
		_v->b = color(_p->b);
		_v->a = color(_p->a);
	}
	_v->s = (f32)(_p->s / _p->w * 1024.0);
	_v->t = (f32)(_p->t / _p->w * 1024.0);
	_v->x = (f32)_p->x;
	_v->y = (f32)_p->y;
	_v->z = _m->zprim ? _m->primZ : (f32)(_p->z / Z_SCALE);
	_v->w = (f32)(Z_SCALE / _p->w);
}

u32 gli_lle_vertices(const gli_lle_corner *_c, u32 _n, const gli_lle_mode *_mode,
	SPVertex *_out, int *_list)
{
	u32 i, k, written = 0;
	int cut = 0;

	if (_mode->texture && _mode->persp)
		for (i = 0; i < _n; ++i)
			cut |= _c[i].w < GLI_LLE_MIN_W;

	*_list = cut;
	if (!cut) {
		for (i = 0; i < _n; ++i)
			corner_vertex(&_c[i], _mode, &_out[i]);
		return _n;
	}

	/* each triangle of the strip, cut where W falls below the least */
	for (k = 0; k + 2 < _n; ++k) {
		point in[3], out[4];
		u32 m = 0;
		for (i = 0; i < 3; ++i)
			to_point(&_c[k + i], &in[i]);
		for (i = 0; i < 3; ++i) {
			const point *a = &in[i], *b = &in[(i + 1) % 3];
			const f64 da = a->w - GLI_LLE_MIN_W, db = b->w - GLI_LLE_MIN_W;
			if (da >= 0.0)
				out[m++] = *a;
			if ((da >= 0.0) != (db >= 0.0))
				lerp(a, b, da / (da - db), &out[m++]);
		}
		for (i = 1; i + 1 < m; ++i) {
			point_vertex(&out[0], _mode, &_out[written++]);
			point_vertex(&out[i], _mode, &_out[written++]);
			point_vertex(&out[i + 1], _mode, &_out[written++]);
		}
	}
	return written;
}
