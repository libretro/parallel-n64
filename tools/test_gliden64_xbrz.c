/* test_gliden64_xbrz: GLideNHQ's xBRZ filter (TextureFilters_xbrz.c).
 *
 * Images of every shape the filter takes care over (one texel, one row,
 * one column, odd and even sizes) and of contents that take each of its
 * paths: noise, few colours (equal neighbours, dominant and normal
 * blends, lines shallow, steep and both, corners), alpha all, none and
 * between, hard edges, diagonals, checkers and gradients; each scaled 2
 * to 6 times. Every output is GLideN64's C++ xBRZ's bit for bit (held by
 * its hash, made from the C++ under the same test), and nothing past the
 * output is written; an empty image writes nothing. Its colour distances
 * are the C++ table's. Its table is made on
 * first use, as before; where it cannot be, the image is scaled by
 * blocks. C89. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the filter itself, its table's allocation able to fail; or, to make
 * the hashes, the C++ one it was, linked (XBRZ_CXX) */
#ifndef XBRZ_CXX
static int s_failAlloc;
static void *test_malloc(size_t _size) { return s_failAlloc ? NULL : malloc(_size); }
#define malloc(_size) test_malloc(_size)
#include "GLideNHQ/TextureFilters_xbrz.c"
#undef malloc
#else
#include "GLideNHQ/TextureFilters.h"
#endif

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static unsigned s_rand;
static unsigned rnd(void)
{
	s_rand = s_rand * 1103515245u + 12345u;
	return s_rand >> 8;
}

/* image @_kind, @_w x @_h */
static void make(uint32 *_p, int _w, int _h, int _kind)
{
	static const uint32 few[6] = { 0xff000000u, 0xffffffffu, 0xff2040e0u, 0x80e04020u, 0x00000000u, 0xff20e040u };
	int x, y;
	for (y = 0; y < _h; ++y)
		for (x = 0; x < _w; ++x) {
			uint32 v;
			switch (_kind) {
			case 0: v = (rnd() << 8) ^ rnd(); break;                                 /* noise */
			case 1: v = few[rnd() % 3] | 0xff000000u; break;                         /* three opaque colours */
			case 2: v = few[rnd() % 6]; break;                                       /* six, some clear */
			case 3: v = (x * 3 + y * 5) % 11 < 5 ? few[0] : few[1]; break;          /* shallow and steep lines */
			case 4: v = ((x ^ y) & 1) ? few[2] : few[5]; break;                      /* checkers */
			case 5: v = (x > y) ? few[1] : few[0]; break;                            /* a diagonal */
			case 6: v = (x * 2 > y * 3) ? few[2] : few[3]; break;                    /* a shallow edge */
			case 7: v = ((uint32)(x * 255 / (_w > 1 ? _w - 1 : 1)) << 24) | 0x00808080u; break; /* alpha ramp */
			case 8: v = ((uint32)(rnd() & 0xff) << 24) | (few[rnd() % 3] & 0xffffffu); break; /* random alpha */
			case 9: v = (x / 3 + y / 2) % 2 ? few[1] : few[4]; break;               /* opaque against clear */
			case 10: v = 0xff000000u | ((uint32)(x * 9) << 16) | ((uint32)(y * 7) << 8) | (uint32)((x + y) * 3); break;
			case 11: v = ((x / 2 + y / 2) & 1) ? few[0] : few[1]; break;            /* 2x2 checkers: weights tie */
			case 12: v = (x % 4 == 1 || y % 4 == 1) ? few[2] : few[5]; break;       /* a grid */
			default: v = (rnd() % 7 == 0) ? few[rnd() % 6] : few[1]; break;          /* specks */
			}
			_p[y * _w + x] = v;
		}
}
#define KINDS 14

static const int s_sizes[][2] = {
	{ 17, 13 }, { 1, 1 }, { 1, 7 }, { 7, 1 }, { 2, 2 }, { 3, 5 }, { 8, 8 }, { 32, 9 }, { 64, 64 }
};
#define SIZES (int)(sizeof(s_sizes) / sizeof(s_sizes[0]))

#define GUARD 64
#define CASES (SIZES * KINDS * 5)
#include "test_gliden64_xbrz.h"

int main(int argc, char **argv)
{
	const int hashes = argc > 1 && strcmp(argv[1], "hashes") == 0;
	static uint32 src[64 * 64];
	static uint32 dst[64 * 64 * 36 + GUARD];
	int s, k, f, n = 0;

#ifndef XBRZ_CXX
	if (!hashes) {
		/* the table not to be had: each texel a block */
		static const uint32 four[4] = { 0x11223344u, 0x55667788u, 0x99aabbccu, 0xddeeff00u };
		s_failAlloc = 1;
		xbrz_scale_abgr(3, four, dst, 2, 2);
		for (s = 0; s < 36; ++s)
			CHECK(dst[s] == four[(s / 18) * 2 + (s % 6) / 3]);
		CHECK(s_dist == NULL);
		s_failAlloc = 0;
		/* empty images: nothing written, before or after */
		for (s = 0; s < 3 * GUARD; ++s)
			dst[s] = 0xdeadbeefu;
		for (f = 2; f <= 6; ++f) {
			xbrz_scale_abgr((unsigned)f, src, dst + GUARD, 4, 0);
			xbrz_scale_abgr((unsigned)f, src, dst + GUARD, 0, 4);
		}
		for (s = 0; s < 3 * GUARD; ++s)
			CHECK(dst[s] == 0xdeadbeefu);
		/* the colour distances, each as the C++ table held it (hashed from
		 * it) */
		{
			unsigned h = 2166136261u;
			size_t i;
			for (i = 0; i < 256 * 256 * 256; ++i) {
				unsigned u;
				int b;
				memcpy(&u, &s_dist[i], sizeof(u));
				for (b = 0; b < 4; ++b)
					h = (h ^ ((u >> (8 * b)) & 0xff)) * 16777619u;
			}
			CHECK(h == 0x338fcb91u);
		}
	}
#endif
	if (hashes)
		printf("static const unsigned s_want[%d] = {\n", CASES);
	for (s = 0; s < SIZES; ++s)
		for (k = 0; k < KINDS; ++k)
			for (f = 2; f <= 6; ++f, ++n) {
				const int w = s_sizes[s][0], h = s_sizes[s][1];
				const size_t count = (size_t)(w * f) * (size_t)(h * f);
				unsigned hash = 2166136261u;
				size_t i;
				s_rand = (unsigned)(s * 131 + k * 7 + 1);
				make(src, w, h, k);
				for (i = 0; i < count + GUARD; ++i)
					dst[i] = 0xdeadbeefu;
				/* its table made on first use, then made again for nothing */
				if (n == 1)
					xbrz_init();
				xbrz_scale_abgr((unsigned)f, src, dst, w, h);
				for (i = 0; i < count; ++i) {
					hash = (hash ^ (dst[i] & 0xff)) * 16777619u;
					hash = (hash ^ ((dst[i] >> 8) & 0xff)) * 16777619u;
					hash = (hash ^ ((dst[i] >> 16) & 0xff)) * 16777619u;
					hash = (hash ^ (dst[i] >> 24)) * 16777619u;
				}
				for (i = count; i < count + GUARD; ++i)
					CHECK(dst[i] == 0xdeadbeefu);
				if (hashes)
					printf("\t0x%08xu,\n", hash);
				else if (hash != s_want[n]) {
					printf("FAIL %dx%d kind %d x%d: %08x\n", w, h, k, f, hash);
					++fails;
				}
			}
	if (hashes) {
		printf("};\n");
		return 0;
	}
	if (fails) {
		printf("test_gliden64_xbrz: %d failures\n", fails);
		return 1;
	}
	printf("test_gliden64_xbrz: ok\n");
	return 0;
}
