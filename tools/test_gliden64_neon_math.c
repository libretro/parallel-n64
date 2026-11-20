/* test_gliden64_neon_math: GLideN64's NEON matrix and vector code
 * (src/Neon/3DMathNeon.c, src/Neon/gSPNeon.c) against the C++ it
 * replaced, to the bit.
 *
 * Each function runs on the same pseudo-random matrices and vectors (with
 * zeros, negatives, tiny and huge values among them) and its results are
 * hashed. The golden hashes are those of the C++ (src/Neon/*.cpp as they
 * were, built with g++ for the same target), one set for 32-bit ARM and
 * one for AArch64: their NEON differ in how a multiply-accumulate rounds.
 * Built with -DPRINT_HASHES it prints its own, to make them from the C++.
 *
 * Runs on ARM, or on an x86 host through qemu-user:
 *   make check_gliden64_neon
 * C89. */
#include <stdio.h>
#include <string.h>

#include "Types.h"
#include "3DMath.h"
#include "gSP.h"
#include "gli_drawer.h"

/* what the vertex functions read and write */
gli_triangles gli_tris;
gSPInfo gSP;
static int s_hwl;
bool gli_isHWLightingAllowed(void) { return s_hwl != 0; }

#define ROUNDS 4000
#define FUNCS 11

static u32 s_seed = 12345u;

static u32 next(void)
{
	s_seed = s_seed * 1664525u + 1013904223u;
	return s_seed;
}

/* a choice of @n, from the generator's high bits: its low ones repeat
 * within a few calls */
static u32 pick(u32 n)
{
	return (next() >> 16) % n;
}

/* a float of every kind the transforms meet */
static float value(void)
{
	const u32 r = next();
	switch ((r >> 16) & 15) {
	case 0: return 0.f;
	case 1: return -0.f;
	case 2: return (float)((s32)(next() >> 8) - (1 << 23)) * 1e-30f;
	case 3: return (float)((s32)(next() >> 8) - (1 << 23)) * 1e10f;
	default: return (float)((s32)(next() >> 8) - (1 << 23)) / 65536.f;
	}
}

static void fill(float *f, unsigned n)
{
	unsigned i;
	for (i = 0; i < n; ++i)
		f[i] = value();
}

/* lighting's: normals and directions within one, colours within a fifth,
 * so that a vertex's sum is mostly under the 1 it is clamped to, or
 * within a scale of it */
static void fill_small(float *f, unsigned n, float scale)
{
	unsigned i;
	for (i = 0; i < n; ++i)
		f[i] = (float)((s32)(next() >> 8) - (1 << 23)) / 8388608.f * scale;
}

static void hash(u32 *h, const float *f, unsigned n)
{
	unsigned i;
	for (i = 0; i < n; ++i) {
		u32 bits;
		memcpy(&bits, &f[i], sizeof(bits));
		*h = (*h ^ bits) * 16777619u;
	}
}

/* the C++'s, built at -O2: at -O3 GCC rounds one of AArch64's otherwise
 * (gli_InverseTransformVectorNormalizeN's), in the C++ and in the C alike */
#if defined(__aarch64__) || defined(_M_ARM64)
static const u32 s_golden[FUNCS] = {
	0x86baf2ffu, 0x86baf2ffu, 0xf0df21d5u, 0xe5650468u,
	0x3a312e1fu, 0xd4f0b920u, 0x052ab919u, 0x76f9a883u,
	0xdf3c1ef4u, 0xfeec2c1au, 0xb97a1ee8u
};
#else
static const u32 s_golden[FUNCS] = {
	0x86baf2ffu, 0x86baf2ffu, 0x1984405eu, 0xee894c05u,
	0xa5e6863bu, 0xd7c8dc59u, 0x052ab919u, 0x76f9a883u,
	0xdf3c1ef4u, 0xfeec2c1au, 0x62915bc3u
};
#endif

static const char *const s_names[FUNCS] = {
	"gli_MultMatrix", "gli_MultMatrix2", "gli_TransformVectorNormalize",
	"gli_InverseTransformVectorNormalize", "gli_InverseTransformVectorNormalizeN",
	"gli_Normalize", "gli_CopyMatrix", "gSP transforms", "gSPTransformVertex4NEON",
	"gSPBillboardVertex4NEON", "gSPLightVertex_NEON"
};

int main(void)
{
	u32 h[FUNCS];
	unsigned round, i, failures = 0;

	for (i = 0; i < FUNCS; ++i)
		h[i] = 2166136261u;
	for (round = 0; round < ROUNDS; ++round) {
		float a[4][4], b[4][4], d[4][4], v[4], w[4];
		float src[20][3], dst[20][3];
		u32 count;

		fill(&a[0][0], 16);
		fill(&b[0][0], 16);
		memset(d, 0, sizeof(d));
		gli_MultMatrix(a, b, d);
		hash(&h[0], &d[0][0], 16);
		gli_MultMatrix2(a, b);
		hash(&h[1], &a[0][0], 16);

		fill(v, 4);
		gli_TransformVectorNormalize(v, b);
		hash(&h[2], v, 3);
		fill(v, 4);
		memset(w, 0, sizeof(w));
		gli_InverseTransformVectorNormalize(v, w, b);
		hash(&h[3], w, 3);

		/* each run of 7, of 4 and of 1 */
		count = pick(21);
		fill(&src[0][0], 60);
		memset(dst, 0, sizeof(dst));
		gli_InverseTransformVectorNormalizeN(src, dst, b, count);
		hash(&h[4], &dst[0][0], 60);
		hash(&h[4], &src[0][0], 60);

		fill(v, 4);
		gli_Normalize(v);
		hash(&h[5], v, 4);

		fill(&a[0][0], 16);
		gli_CopyMatrix(d, a);
		hash(&h[6], &d[0][0], 16);

		fill(v, 4);
		gSPTransformVector_NEON(v, b);
		hash(&h[7], v, 4);
		fill(v, 4);
		gSPInverseTransformVector_NEON(v, b);
		hash(&h[7], v, 4);

		/* four vertices, all of each */
		{
			const u32 at = 1 + pick(8);
			fill(&gli_tris.vertices[0].x, (sizeof(SPVertex) / sizeof(float)) * 16);
			gSPTransformVertex4NEON(at, b);
			hash(&h[8], &gli_tris.vertices[0].x, (sizeof(SPVertex) / sizeof(float)) * 16);
			gSPBillboardVertex4NEON(at);
			hash(&h[9], &gli_tris.vertices[0].x, (sizeof(SPVertex) / sizeof(float)) * 16);
		}
		/* lit by none to eleven lights, by the GPU or here; the normals
		 * and lights of every size and sign */
		{
			SPVertex *vtx = gli_tris.vertices;
			u32 k;
			s_hwl = (int)pick(2);
			gSP.numLights = pick(12);
			gSP.matrix.modelViewi = pick(32);
			fill(&gSP.matrix.modelView[gSP.matrix.modelViewi][0][0], 16);
			/* now and then bright enough to be clamped */
			fill_small(&gSP.lights.rgb[0][0], 36, pick(4) ? 0.2f : 1.5f);
			fill_small(&gSP.lights.i_xyz[0][0], 36, 1.f);
			fill_small(&vtx[0].x, (sizeof(SPVertex) / sizeof(float)) * 8, 1.f);
			for (k = 0; k < 8; ++k)
				vtx[k].HWLight = 0xAA;
			{
				const u32 n = 1 + pick(4);
				gSPLightVertex_NEON(n, pick(4), vtx);
			}
			for (k = 0; k < 8; ++k) {
				hash(&h[10], &vtx[k].x, 16);
				h[10] = (h[10] ^ vtx[k].HWLight) * 16777619u;
			}
		}
	}

	for (i = 0; i < FUNCS; ++i) {
#ifdef PRINT_HASHES
		printf("0x%08xu,%s", (unsigned)h[i], (i & 3) == 3 || i + 1 == FUNCS ? "\n" : " ");
#else
		if (h[i] != s_golden[i]) {
			printf("FAIL %s: %08x, the C++ %08x\n", s_names[i], (unsigned)h[i], (unsigned)s_golden[i]);
			failures++;
		}
#endif
	}
#ifndef PRINT_HASHES
	if (failures) {
		printf("test_gliden64_neon_math: %u failures\n", failures);
		return 1;
	}
	printf("test_gliden64_neon_math: ok (%u rounds)\n", (unsigned)ROUNDS);
#endif
	return 0;
}
