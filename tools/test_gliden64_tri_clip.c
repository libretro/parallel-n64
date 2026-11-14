/* test_gliden64_tri_clip -- GLideN64 clipping triangles as the RSP does.
 *
 * GoldenEye's frigate: big water and hull triangles run from behind the
 * camera to the horizon. The RSP clips such a triangle against the near
 * plane and the guard band into new vertices, each attribute taken along
 * the edge in clip space, and the RDP interpolates shade between them in
 * screen space. GLideN64 left the clipping to the GPU and interpolated
 * shade with perspective, and a light wedge ran across the water to the
 * horizon. Shade is now interpolated in screen space like the RDP, which
 * only matches when the vertices are where the RSP puts them: across a
 * vertex behind the eye it has no screen position at all.
 *
 * gli_tri_clip is held to that: every vertex it writes is inside the near
 * and far planes, in front of the eye and inside the guard band, with
 * every attribute what the triangle has at that point of clip space;
 * triangles inside pass as they are, wound as before; a microcode without
 * near clipping is not clipped there; screen space vertices pass
 * untouched. The triangles are GoldenEye's, as the frigate level loads
 * them.
 *
 * The far plane, z <= w, is one of the RSP's too: a hull triangle reaching
 * past it at the horizon was left whole. And each task loads the
 * microcode's data segment, the clip ratio in it: 1 for F3D, 2 for the
 * later microcodes, until the game sets its own. GLideN64 kept the last
 * ratio set across tasks.
 */
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "../mupen64plus-video-gliden64/src/Types.h"
#include "../mupen64plus-video-gliden64/src/gSP.h"
#include "../mupen64plus-video-gliden64/src/GBI.h"
#include "../mupen64plus-video-gliden64/src/gli_tri_clip.h"

gSPInfo gSP;
static MicrocodeInfo ucode;
const MicrocodeInfo *gli_GBI = &ucode;

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

#define NUM_FLOATS (offsetof(SPVertex, modify) / sizeof(f32))

static SPVertex vtx(f32 x, f32 y, f32 z, f32 w, f32 c, f32 s, f32 t)
{
	SPVertex v;
	memset(&v, 0, sizeof(v));
	v.x = x; v.y = y; v.z = z; v.w = w;
	v.r = v.g = v.b = c; v.a = 1.0f - c * 0.5f;
	v.flat_r = 0.25f; v.flat_a = 1.0f;
	v.nx = c; v.ny = -c; v.nz = 0.5f;
	v.s = s; v.t = t;
	v.HWLight = 1;
	v.flag = 7;
	return v;
}

static f32 det3(f32 m[3][3])
{
	return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
		- m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
		+ m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

/* The weights of @_tri's corners that give @_p's x, y and w; with them
 * every other attribute of @_p must follow */
static int weights(const SPVertex *_tri, const SPVertex *_p, f32 _out[3])
{
	f32 m[3][3], k[3][3], rhs[3];
	f32 d;
	int i, j;
	rhs[0] = _p->x;
	rhs[1] = _p->y;
	rhs[2] = _p->w;
	for (i = 0; i < 3; ++i) {
		m[0][i] = _tri[i].x;
		m[1][i] = _tri[i].y;
		m[2][i] = _tri[i].w;
	}
	d = det3(m);
	if (fabs(d) < 1e-6)
		return 0;
	for (j = 0; j < 3; ++j) {
		memcpy(k, m, sizeof(k));
		for (i = 0; i < 3; ++i)
			k[i][j] = rhs[i];
		_out[j] = det3(k) / d;
	}
	return 1;
}

static void check_clipped(const char *name, const SPVertex *tri, u32 near)
{
	SPVertex out[3 * GLI_TRI_CLIP_MAX];
	const f32 r = (f32)gSP.clipRatio;
	u32 n, i, f;

	n = gli_tri_clip(tri, NULL, 3, out);
	check(n % 3 == 0 && n <= 3 * GLI_TRI_CLIP_MAX, "%s: %u vertices", name, n);
	check(n != 0, "%s: nothing left", name);
	for (i = 0; i < n; ++i) {
		const SPVertex *p = &out[i];
		const f32 *a = &p->x;
		f32 wt[3];
		check(p->w > 0.0f, "%s: vertex %u behind the eye (w %g)", name, i, p->w);
		if (near)
			check(p->z + p->w > -1e-3f * p->w, "%s: vertex %u past the near plane", name, i);
		check(fabs(p->x) <= r * p->w * 1.0001f && fabs(p->y) <= r * p->w * 1.0001f,
			"%s: vertex %u outside the guard band (%g %g w %g)", name, i, p->x, p->y, p->w);
		check(weights(tri, p, wt), "%s: vertex %u off the triangle", name, i);
		check(fabs(wt[0] + wt[1] + wt[2] - 1.0f) < 1e-3f, "%s: vertex %u off the triangle's plane", name, i);
		for (f = 0; f < NUM_FLOATS; ++f) {
			const f32 want = wt[0] * (&tri[0].x)[f] + wt[1] * (&tri[1].x)[f] + wt[2] * (&tri[2].x)[f];
			check(fabs(a[f] - want) <= 1e-3f * (1.0f + fabs(want)),
				"%s: vertex %u attribute %u is %g, the triangle has %g there", name, i, f, a[f], want);
		}
		check(p->modify == 0 && p->HWLight == 1 && p->flag == 7, "%s: vertex %u lost its flags", name, i);
	}
	/* the fan keeps the winding the visible part of the triangle has */
	for (i = 0; i + 2 < n; i += 3) {
		const SPVertex *a = &out[i], *b = &out[i + 1], *c = &out[i + 2];
		const f32 area = (b->x / b->w - a->x / a->w) * (c->y / c->w - a->y / a->w)
			- (c->x / c->w - a->x / a->w) * (b->y / b->w - a->y / a->w);
		const f32 first = (out[1].x / out[1].w - out[0].x / out[0].w) * (out[2].y / out[2].w - out[0].y / out[0].w)
			- (out[2].x / out[2].w - out[0].x / out[0].w) * (out[1].y / out[1].w - out[0].y / out[0].w);
		check((area >= 0.0f) == (first >= 0.0f) || fabs(area) < 1e-6f, "%s: triangle %u wound the other way", name, i / 3);
	}
}

int main(void)
{
	SPVertex hull[3], side[3], boat[3], wide[3], inside[3], nearTri[3], eyeTri[3], farTri[3], frigate[3];
	SPVertex out[3 * GLI_TRI_CLIP_MAX];
	static const u16 elements[6] = { 2, 1, 0, 0, 1, 2 };
	u32 n, i;

	/* GoldenEye's water and hull, a corner behind the eye */
	hull[0] = vtx(325.887f, -1557.94f, -279.902f, -249.104f, 1.0f, 60.6875f, 0.0f);
	hull[1] = vtx(246.189f, -122.858f, 559.358f, 587.634f, 0.0f, 46.625f, 31.875f);
	hull[2] = vtx(246.189f, -1162.96f, 699.953f, 727.806f, 1.0f, 46.625f, 0.0f);
	side[0] = vtx(246.189f, 913.478f, 419.272f, 447.971f, 0.84f, 131.312f, 30.9688f);
	side[1] = vtx(246.189f, -122.858f, 559.358f, 587.634f, 0.84f, 131.25f, -0.46875f);
	side[2] = vtx(378.979f, -512.898f, -418.101f, -386.886f, 0.84f, 150.594f, -0.78125f);
	/* a boat triangle between the eye and the near plane */
	boat[0] = vtx(-86.6705f, -187.686f, -25.3766f, 4.65386f, 0.996f, 32.0f, 32.0f);
	boat[1] = vtx(-81.3733f, -85.6106f, 168.7f, 198.147f, 0.5f, -0.4375f, 30.5938f);
	boat[2] = vtx(-97.8323f, -139.856f, 182.002f, 211.409f, 0.2f, 0.0f, 0.0f);
	/* past the guard band only, in front of the eye */
	wide[0] = vtx(-50.0f, -10.0f, 5.0f, 10.0f, 0.0f, 0.0f, 0.0f);
	wide[1] = vtx(90.0f, 0.0f, 5.0f, 10.0f, 1.0f, 64.0f, 0.0f);
	wide[2] = vtx(0.0f, 70.0f, 5.0f, 10.0f, 0.5f, 0.0f, 64.0f);
	inside[0] = vtx(-5.0f, -5.0f, 1.0f, 10.0f, 0.0f, 0.0f, 0.0f);
	inside[1] = vtx(5.0f, -5.0f, 1.0f, 10.0f, 1.0f, 1.0f, 0.0f);
	inside[2] = vtx(0.0f, 5.0f, 1.0f, 10.0f, 0.5f, 0.0f, 1.0f);
	/* past the near plane only */
	nearTri[0] = vtx(1.0f, 1.0f, -6.0f, 2.0f, 1.0f, 0.0f, 0.0f);
	nearTri[1] = vtx(5.0f, -5.0f, 1.0f, 10.0f, 0.0f, 1.0f, 0.0f);
	nearTri[2] = vtx(-5.0f, -5.0f, 1.0f, 10.0f, 0.5f, 0.0f, 1.0f);
	/* behind the eye, though not past the near plane */
	eyeTri[0] = vtx(0.5f, 0.5f, 10.0f, -2.0f, 1.0f, 0.0f, 0.0f);
	eyeTri[1] = nearTri[1];
	eyeTri[2] = nearTri[2];
	/* past the far plane only */
	farTri[0] = vtx(1.0f, 1.0f, 30.0f, 12.0f, 1.0f, 0.0f, 0.0f);
	farTri[1] = nearTri[1];
	farTri[2] = nearTri[2];
	/* GoldenEye's hull at the horizon, past the guard band only */
	frigate[0] = vtx(1643.79f, 2595.94f, 2787.04f, 2808.63f, 0.88f, 75.5625f, 31.875f);
	frigate[1] = vtx(471.742f, -150.748f, 493.952f, 522.478f, 0.88f, 131.25f, -0.625f);
	frigate[2] = vtx(471.001f, 836.18f, 263.465f, 292.633f, 0.88f, 131.312f, 30.9688f);

	gSP.clipRatio = 2;
	ucode.NoN = false;

	check_clipped("hull", hull, 1);
	check_clipped("side", side, 1);
	check_clipped("boat", boat, 1);
	check_clipped("wide", wide, 1);
	check_clipped("frigate", frigate, 1);
	check(gli_tri_clip_vertex_outside(&farTri[0]), "far: vertex past the far plane called inside");
	check_clipped("far", farTri, 1);
	gSP.clipRatio = 1;
	check_clipped("wide, clip ratio 1", wide, 1);
	check_clipped("frigate, clip ratio 1", frigate, 1);
	gSP.clipRatio = 2;

	/* the ratio each task starts with */
	check(gli_tri_clip_default_ratio(F3D) == 1 && gli_tri_clip_default_ratio(F3DGOLDEN) == 1,
		"F3D tasks start with a clip ratio other than 1");
	check(gli_tri_clip_default_ratio(F3DEX) == 2 && gli_tri_clip_default_ratio(F3DEX2) == 2,
		"F3DEX tasks start with a clip ratio other than 2");

	/* inside: as it is, in order, through the indices too */
	n = gli_tri_clip(inside, elements, 6, out);
	check(n == 6, "inside: %u vertices", n);
	for (i = 0; i < n && n == 6; ++i)
		check(memcmp(&out[i], &inside[elements[i]], sizeof(SPVertex)) == 0, "inside: vertex %u changed", i);
	for (i = 0; i < 3; ++i)
		check(!gli_tri_clip_vertex_outside(&inside[i]), "inside: vertex %u called outside", i);
	check(gli_tri_clip_vertex_outside(&boat[0]) && gli_tri_clip_vertex_outside(&hull[0]) &&
		gli_tri_clip_vertex_outside(&wide[1]), "outside vertices called inside");

	/* no near clipping in the microcode: a corner past the near plane,
	 * in front of the eye and inside the guard band stays */
	check(gli_tri_clip_vertex_outside(&nearTri[0]), "near: vertex past the near plane called inside");
	check_clipped("near", nearTri, 1);
	ucode.NoN = true;
	check(!gli_tri_clip_vertex_outside(&nearTri[0]), "NoN: near plane still clipped");
	n = gli_tri_clip(nearTri, NULL, 3, out);
	check(n == 3 && memcmp(out, nearTri, sizeof(nearTri)) == 0, "NoN: clipped (%u vertices)", n);
	check_clipped("NoN hull", hull, 0);
	check_clipped("NoN behind the eye", eyeTri, 0);
	check_clipped("NoN far", farTri, 0);
	ucode.NoN = false;

	/* screen space vertices pass as they are */
	{
		SPVertex s[3];
		memcpy(s, hull, sizeof(s));
		s[0].modify = s[1].modify = s[2].modify = 3;
		check(!gli_tri_clip_vertex_outside(&s[0]), "screen space vertex called outside");
		n = gli_tri_clip(s, NULL, 3, out);
		check(n == 3 && memcmp(out, s, sizeof(s)) == 0, "screen space triangle clipped");
	}

	/* wholly behind the eye: nothing */
	{
		SPVertex b[3];
		memcpy(b, inside, sizeof(b));
		for (i = 0; i < 3; ++i)
			b[i].w = -b[i].w;
		n = gli_tri_clip(b, NULL, 3, out);
		check(n == 0, "behind the eye: %u vertices", n);
	}

	if (failures != 0) {
		printf("%d failure(s)\n", failures);
		return 1;
	}
	printf("test_gliden64_tri_clip: ok\n");
	return 0;
}
