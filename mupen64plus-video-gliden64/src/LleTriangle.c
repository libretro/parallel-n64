#include <math.h>

#include "Types.h"
#include "GBI.h"
#include "gSP.h"
#include "gli_lle_tri.h"

#define Z_SCALE 4294901760.0 /* 0xffff0000 */

void gli_lle_corner_at(const gli_lle_plane *_p, f64 _x, f64 _y, gli_lle_corner *_c)
{
	const f64 dy = _y - _p->y0;
	const f64 dx = _x - (_p->x0 + _p->dxhdy * dy);
	f64 *o = &_c->r;
	u32 i;
	_c->x = _x;
	_c->y = _y;
	for (i = 0; i < 8; ++i)
		o[i] = _p->v[i] + _p->dvdx[i] * dx + _p->dvde[i] * dy;
}

/* Shade to the RDP's 8 bits */
static f32 color(f64 _c)
{
	if (_c > (f64)0xffc000)
		_c = (f64)0xffc000;
	if (_c < 0.0)
		_c = 0.0;
	return (f32)floor(_c / 65536.0) * FIXED2FLOATRECIPCOLOR8;
}

/* A point where W is positive: S/W and T/W at it, and W itself for the
 * GPU to interpolate them by, as the RDP divides them */
static void vertex(const gli_lle_corner *_p, const gli_lle_mode *_m, SPVertex *_v)
{
	if (_m->shade) {
		_v->r = color(_p->r);
		_v->g = color(_p->g);
		_v->b = color(_p->b);
		_v->a = color(_p->a);
	}
	if (_m->texture) {
		if (_m->persp) {
			const f64 w = _p->w != 0.0 ? _p->w : 1.0;
			_v->s = (f32)(_p->s / w * 1024.0);
			_v->t = (f32)(_p->t / w * 1024.0);
		} else {
			_v->s = (f32)(_p->s / 2097152.0);
			_v->t = (f32)(_p->t / 2097152.0);
		}
	}
	_v->x = (f32)_p->x;
	_v->y = (f32)_p->y;
	_v->z = _m->zprim ? _m->primZ : (f32)(_p->z / Z_SCALE);
	_v->w = _m->texture && _m->persp && _p->w > 0.0 ? (f32)(Z_SCALE / _p->w) : 1.0f;
}

static void lerp(const gli_lle_corner *_a, const gli_lle_corner *_b, f64 _t, gli_lle_corner *_o)
{
	const f64 *a = &_a->x, *b = &_b->x;
	f64 *o = &_o->x;
	u32 i;
	for (i = 0; i < sizeof(gli_lle_corner) / sizeof(f64); ++i)
		o[i] = a[i] + (b[i] - a[i]) * _t;
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
			vertex(&_c[i], _mode, &_out[i]);
		return _n;
	}

	/* each triangle of the strip, cut where W falls below the least */
	for (k = 0; k + 2 < _n; ++k) {
		gli_lle_corner out[4];
		u32 m = 0;
		for (i = 0; i < 3; ++i) {
			const gli_lle_corner *a = &_c[k + i], *b = &_c[k + (i + 1) % 3];
			const f64 da = a->w - GLI_LLE_MIN_W, db = b->w - GLI_LLE_MIN_W;
			if (da >= 0.0)
				out[m++] = *a;
			if ((da >= 0.0) != (db >= 0.0))
				lerp(a, b, da / (da - db), &out[m++]);
		}
		for (i = 1; i + 1 < m; ++i) {
			vertex(&out[0], _mode, &_out[written++]);
			vertex(&out[i], _mode, &_out[written++]);
			vertex(&out[i + 1], _mode, &_out[written++]);
		}
	}
	return written;
}
