/* test_gliden64_lle_tri -- GLideN64 drawing RDP triangles with W at or
 * below zero.
 *
 * GoldenEye builds the frigate level's water and sky as RDP triangles on
 * the CPU. Near the horizon their W, the RDP's perspective divisor, falls
 * to zero and below inside the triangle. The RDP steps S/W and T/W across
 * the screen and never meets it. GLideN64 gave the GPU each corner's own
 * S/W, T/W and W to interpolate with perspective: with W at or below zero
 * at a corner that is no longer what the RDP has inside the triangle, and
 * the texture warped across the water, split along the strip's diagonal.
 *
 * Now such a strip is cut, each value along screen space as the RDP steps
 * it, to where W is at least GLI_LLE_MIN_W. Held here: a strip with W
 * positive throughout comes out as it always did; a cut strip covers just
 * the part of each triangle where W is at least GLI_LLE_MIN_W; every
 * vertex there has the S/W, T/W, W, shade and depth the RDP has at that
 * point; and the cut never writes more than GLI_LLE_MAX_VERTICES.
 */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "../mupen64plus-video-gliden64/src/Types.h"
#include "../mupen64plus-video-gliden64/src/GBI.h"
#include "../mupen64plus-video-gliden64/src/gSP.h"
#include "../mupen64plus-video-gliden64/src/gli_lle_tri.h"

#define Z_SCALE 4294901760.0

static int failures;

static void check(int _cond, const char *_fmt, ...)
{
	va_list ap;
	if (_cond)
		return;
	++failures;
	printf("FAIL: ");
	va_start(ap, _fmt);
	vprintf(_fmt, ap);
	va_end(ap);
	printf("\n");
}

static gli_lle_corner corner(f32 x, f32 y, s32 c, u32 z, s32 s, s32 t, s32 w)
{
	gli_lle_corner k;
	k.x = x; k.y = y;
	k.r = c; k.g = c / 2; k.b = 0x3ff0000 - c; k.a = 0x3fc0000;
	k.z = z;
	k.s = s; k.t = t; k.w = w;
	return k;
}

/* The weights of the corners @_a @_b @_c of a strip triangle at screen
 * point @_x @_y */
static void barycentric(const gli_lle_corner *_a, const gli_lle_corner *_b, const gli_lle_corner *_c,
	f64 _x, f64 _y, f64 _l[3])
{
	const f64 d = ((f64)_b->y - _c->y) * ((f64)_a->x - _c->x) + ((f64)_c->x - _b->x) * ((f64)_a->y - _c->y);
	_l[0] = (((f64)_b->y - _c->y) * (_x - _c->x) + ((f64)_c->x - _b->x) * (_y - _c->y)) / d;
	_l[1] = (((f64)_c->y - _a->y) * (_x - _c->x) + ((f64)_a->x - _c->x) * (_y - _c->y)) / d;
	_l[2] = 1.0 - _l[0] - _l[1];
}

static f64 area2(f64 ax, f64 ay, f64 bx, f64 by, f64 cx, f64 cy)
{
	return (bx - ax) * (cy - ay) - (cx - ax) * (by - ay);
}

static int near_value(f64 _got, f64 _want, f64 _rel)
{
	return fabs(_got - _want) <= _rel * (1.0 + fabs(_want));
}

/* The strip of @_n corners @_c, cut: of each triangle the part where W is
 * at least the least, every vertex what the RDP has there */
static void check_cut(const char *_name, const gli_lle_corner *_c, u32 _n, const gli_lle_mode *_m)
{
	SPVertex out[GLI_LLE_MAX_VERTICES(8)];
	f64 kept[8], want_area[8];
	u32 written, i, k;
	int list = 0;

	memset(out, 0, sizeof(out));
	written = gli_lle_vertices(_c, _n, _m, out, &list);
	check(list, "%s: not cut", _name);
	check(written % 3 == 0 && written <= GLI_LLE_MAX_VERTICES(_n), "%s: %u vertices", _name, written);

	/* each strip triangle's area where W is at least the least, by sampling */
	for (k = 0; k + 2 < _n; ++k) {
		const gli_lle_corner *a = &_c[k], *b = &_c[k + 1], *c = &_c[k + 2];
		const f64 full = fabs(area2(a->x, a->y, b->x, b->y, c->x, c->y)) * 0.5;
		u32 in = 0, all = 0, sx, sy;
		f64 minx = a->x, maxx = a->x, miny = a->y, maxy = a->y;
		if (b->x < minx) minx = b->x; if (b->x > maxx) maxx = b->x;
		if (c->x < minx) minx = c->x; if (c->x > maxx) maxx = c->x;
		if (b->y < miny) miny = b->y; if (b->y > maxy) maxy = b->y;
		if (c->y < miny) miny = c->y; if (c->y > maxy) maxy = c->y;
		for (sy = 0; sy < 200; ++sy)
			for (sx = 0; sx < 200; ++sx) {
				f64 l[3];
				const f64 x = minx + (maxx - minx) * (sx + 0.5) / 200.0;
				const f64 y = miny + (maxy - miny) * (sy + 0.5) / 200.0;
				barycentric(a, b, c, x, y, l);
				if (l[0] < 0.0 || l[1] < 0.0 || l[2] < 0.0)
					continue;
				++all;
				in += l[0] * a->w + l[1] * b->w + l[2] * c->w >= GLI_LLE_MIN_W;
			}
		want_area[k] = all != 0 ? full * in / all : 0.0;
		kept[k] = 0.0;
	}

	for (i = 0; i + 2 < written; i += 3) {
		const SPVertex *v = &out[i];
		const f64 cx = (v[0].x + v[1].x + v[2].x) / 3.0, cy = (v[0].y + v[1].y + v[2].y) / 3.0;
		u32 j, owner = _n;
		/* the strip triangle it is part of */
		for (k = 0; k + 2 < _n && owner == _n; ++k) {
			f64 l[3];
			barycentric(&_c[k], &_c[k + 1], &_c[k + 2], cx, cy, l);
			if (l[0] >= -1e-6 && l[1] >= -1e-6 && l[2] >= -1e-6)
				owner = k;
		}
		check(owner != _n, "%s: triangle %u outside the strip", _name, i / 3);
		if (owner == _n)
			continue;
		kept[owner] += fabs(area2(v[0].x, v[0].y, v[1].x, v[1].y, v[2].x, v[2].y)) * 0.5;
		for (j = 0; j < 3; ++j) {
			const gli_lle_corner *a = &_c[owner], *b = &_c[owner + 1], *c = &_c[owner + 2];
			f64 l[3], w, s, t, z, r;
			barycentric(a, b, c, v[j].x, v[j].y, l);
			check(l[0] >= -1e-4 && l[1] >= -1e-4 && l[2] >= -1e-4, "%s: vertex %u off its triangle", _name, i + j);
			w = l[0] * a->w + l[1] * b->w + l[2] * c->w;
			s = l[0] * a->s + l[1] * b->s + l[2] * c->s;
			t = l[0] * a->t + l[1] * b->t + l[2] * c->t;
			z = l[0] * a->z + l[1] * b->z + l[2] * c->z;
			r = l[0] * a->r + l[1] * b->r + l[2] * c->r;
			check(w >= GLI_LLE_MIN_W - 1.0, "%s: vertex %u where W is %g", _name, i + j, w);
			check(v[j].w > 0.0f && near_value(v[j].w, Z_SCALE / w, 1e-4), "%s: vertex %u W %g, the RDP has %g", _name, i + j, v[j].w, Z_SCALE / w);
			check(near_value(v[j].s, s / w * 1024.0, 1e-4) && near_value(v[j].t, t / w * 1024.0, 1e-4),
				"%s: vertex %u S/W %g T/W %g, the RDP has %g %g", _name, i + j, v[j].s, v[j].t, s / w * 1024.0, t / w * 1024.0);
			if (_m->zprim)
				check(v[j].z == _m->primZ, "%s: vertex %u depth not the primitive's", _name, i + j);
			else
				check(near_value(v[j].z, z / Z_SCALE, 1e-5), "%s: vertex %u depth %g, the RDP has %g", _name, i + j, v[j].z, z / Z_SCALE);
			if (_m->shade)
				check(fabs(v[j].r - floor(r / 262144.0) / 255.0) < 1e-5 && v[j].a > 0.99f,
					"%s: vertex %u shade %g, the RDP has %g", _name, i + j, v[j].r, floor(r / 262144.0) / 255.0);
		}
	}
	for (k = 0; k + 2 < _n; ++k)
		check(fabs(kept[k] - want_area[k]) <= 0.02 * want_area[k] + 0.5,
			"%s: strip triangle %u keeps area %g, %g has W at least the least", _name, k, kept[k], want_area[k]);
}

int main(void)
{
	/* GoldenEye's water: W falls through zero towards the horizon */
	gli_lle_corner water[4], ahead[4], flat[4];
	SPVertex out[GLI_LLE_MAX_VERTICES(8)];
	gli_lle_mode mode;
	u32 n, i;
	int list;

	water[0] = corner(10.0f, 120.0f, 0x2000000, 0xfff00000u, 0x400000, 0x200000, 0x30000);
	water[1] = corner(300.0f, 120.0f, 0x1000000, 0xfff80000u, 0x1800000, 0x200000, 0x28000);
	water[2] = corner(10.0f, 239.0f, 0x3000000, 0x80000000u, -0x200000, 0x800000, -0x12000);
	water[3] = corner(300.0f, 239.0f, 0x0400000, 0x80100000u, 0x900000, -0x400000, -0x8000);
	memcpy(ahead, water, sizeof(ahead));
	for (i = 0; i < 4; ++i)
		ahead[i].w = 0x8000 + (s32)i * 0x1000;

	memset(&mode, 0, sizeof(mode));
	mode.shade = 1;
	mode.texture = 1;
	mode.persp = 1;

	/* W positive throughout: the strip as it always was */
	memset(out, 0, sizeof(out));
	n = gli_lle_vertices(ahead, 4, &mode, out, &list);
	check(n == 4 && !list, "ahead: %u vertices, list %d", n, list);
	for (i = 0; i < 4 && n == 4; ++i) {
		const f32 s = (f32)((((s64)ahead[i].s) << 20) / ahead[i].w) / 1024.0f;
		const f32 w = 1.0f / (f32)((f64)(u32)((u32)ahead[i].w + 0x10000u) / Z_SCALE);
		check(out[i].x == ahead[i].x && out[i].y == ahead[i].y && out[i].s == s && out[i].w == w,
			"ahead: vertex %u changed (s %g want %g, w %g want %g)", i, out[i].s, s, out[i].w, w);
		check(out[i].r == _FIXED2FLOATCOLOR(ahead[i].r >> 18, 8), "ahead: vertex %u shade changed", i);
	}

	/* W through zero: cut where it falls below the least */
	check_cut("water", water, 4, &mode);
	mode.zprim = 1;
	mode.primZ = 0.25f;
	check_cut("water, primitive depth", water, 4, &mode);
	mode.zprim = 0;
	/* one triangle, the strip's first two corners behind */
	check_cut("water, first triangle", &water[1], 3, &mode);

	/* without perspective W is not used, and nothing is cut */
	mode.persp = 0;
	n = gli_lle_vertices(water, 4, &mode, out, &list);
	check(n == 4 && !list, "no perspective: cut (%u vertices)", n);
	for (i = 0; i < 4 && n == 4; ++i)
		check(out[i].s == (f32)water[i].s / (1 << 21) && out[i].w == 1.0f, "no perspective: vertex %u changed", i);
	/* nor without texture */
	mode.persp = 1;
	mode.texture = 0;
	n = gli_lle_vertices(water, 4, &mode, out, &list);
	check(n == 4 && !list, "no texture: cut (%u vertices)", n);
	mode.texture = 1;

	/* wholly behind: nothing drawn */
	memcpy(flat, water, sizeof(flat));
	for (i = 0; i < 4; ++i)
		flat[i].w = -0x10000;
	n = gli_lle_vertices(flat, 4, &mode, out, &list);
	check(n == 0 && list, "behind: %u vertices", n);

	if (failures != 0) {
		printf("%d failure(s)\n", failures);
		return 1;
	}
	printf("test_gliden64_lle_tri: ok\n");
	return 0;
}
