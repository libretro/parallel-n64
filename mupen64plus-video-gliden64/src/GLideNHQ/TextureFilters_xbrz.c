/* ****************************************************************************
 * This file is part of the HqMAME project. It is distributed under         *
 * GNU General Public License: http://www.gnu.org/licenses/gpl-3.0          *
 * Copyright (C) Zenju (zenju AT gmx DOT de) - All Rights Reserved          *
 *                                                                          *
 * Additionally and as a special exception, the author gives permission     *
 * to link the code of this program with the MAME library (or with modified *
 * versions of MAME that use the same license as MAME), and distribute      *
 * linked combinations including the two. You must obey the GNU General     *
 * Public License in all respects for all of the code used other than MAME. *
 * If you modify this file, you may extend this exception to your version   *
 * of the file, but you are not obligated to do so. If you do not wish to   *
 * do so, delete this exception statement from your version.                *
 * ****************************************************************************
 *
 * Minor modifications for GLideN64 project by Sergey Lipskiy (gonetz AT ngs
 * DOT ru): color formats changed from RGB/ARGB to BGR/ABGR, init() added.
 *
 * xBRZ, "scale by rules", as GLideNHQ uses it: ABGR texels, 2 to 6 times,
 * the whole image, the default configuration. C89. */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <retro_inline.h>
#include "TextureFilters.h"

#ifdef _MSC_VER
#define XBRZ_INLINE __forceinline
#elif defined(__GNUC__)
#define XBRZ_INLINE __attribute__((always_inline)) INLINE
#else
#define XBRZ_INLINE INLINE
#endif

/* the default ScalerCfg */
#define EQUAL_COLOR_TOLERANCE 30.0
#define DOMINANT_DIRECTION_THRESHOLD 3.6
#define STEEP_DIRECTION_THRESHOLD 2.2

#define GET_ALPHA(_p) ((unsigned char)((_p) >> 24))
#define GET_BLUE(_p) ((unsigned char)((_p) >> 16))
#define GET_GREEN(_p) ((unsigned char)((_p) >> 8))
#define GET_RED(_p) ((unsigned char)(_p))

/* find intermediate color between two colors with alpha channels (=> NO
 * alpha blending!!!): @_m / @_n of @_front over @_back */
static XBRZ_INLINE uint32 gradient_abgr(uint32 _front, uint32 _back, unsigned _m, unsigned _n)
{
	const unsigned weightFront = GET_ALPHA(_front) * _m;
	const unsigned weightBack = GET_ALPHA(_back) * (_n - _m);
	const unsigned weightSum = weightFront + weightBack;
	if (weightSum == 0)
		return 0;
#define CALC(_c) ((uint32)(unsigned char)((_c(_front) * weightFront + _c(_back) * weightBack) / weightSum))
	return ((uint32)(unsigned char)(weightSum / _n) << 24) | (CALC(GET_BLUE) << 16) | (CALC(GET_GREEN) << 8) |
		CALC(GET_RED);
#undef CALC
}

/* The YCbCr distances of every colour difference, each halved to a byte:
 * 64 MB, made once */
static float *s_dist;

void xbrz_init(void)
{
	/* ITU-R BT.2020 conversion */
	const double k_b = 0.0593;
	const double k_r = 0.2627;
	const double k_g = 1 - k_b - k_r;
	const double scale_b = 0.5 / (1 - k_b);
	const double scale_r = 0.5 / (1 - k_r);
	uint32 i;
	if (s_dist != NULL)
		return;
	s_dist = (float *)malloc(256 * 256 * 256 * sizeof(float));
	if (s_dist == NULL)
		return;
	for (i = 0; i < 256 * 256 * 256; ++i) {
		const int r_diff = (int)((i >> 16) & 0xff) * 2 - 255;
		const int g_diff = (int)((i >> 8) & 0xff) * 2 - 255;
		const int b_diff = (int)(i & 0xff) * 2 - 255;
		const double y = k_r * r_diff + k_g * g_diff + k_b * b_diff; /* [!], analog YCbCr! */
		const double c_b = scale_b * (b_diff - y);
		const double c_r = scale_r * (r_diff - y);
		s_dist[i] = (float)sqrt(y * y + c_b * c_b + c_r * c_r);
	}
}

/* the distance of two colours with alpha channels: with a1, a2 in [0, 1],
 * a1 * the colours' distance if a1 = a2, a2 * 255 if a1 = 0 */
static XBRZ_INLINE double dist_abgr(uint32 _pix1, uint32 _pix2)
{
	const double a1 = GET_ALPHA(_pix1) / 255.0;
	const double a2 = GET_ALPHA(_pix2) / 255.0;
	const int r_diff = (int)GET_RED(_pix1) - GET_RED(_pix2);
	const int g_diff = (int)GET_GREEN(_pix1) - GET_GREEN(_pix2);
	const int b_diff = (int)GET_BLUE(_pix1) - GET_BLUE(_pix2);
	const double d = s_dist[(((b_diff + 255) / 2) << 16) | (((g_diff + 255) / 2) << 8) | ((r_diff + 255) / 2)];
	if (a1 < a2)
		return a1 * d + 255 * (a2 - a1);
	return a2 * d + 255 * (a1 - a2);
}

#define BLEND_NONE 0
#define BLEND_NORMAL 1   /* a normal indication to blend */
#define BLEND_DOMINANT 2 /* a strong indication to blend */

/*
input kernel area naming convention:
-----------------
| A | B | C | D |
----|---|---|---|
| E | F | G | H |   evaluate the four corners between F, G, J, K
----|---|---|---|   input pixel is at position F
| I | J | K | L |
----|---|---|---|
| M | N | O | P |
-----------------
*/
enum { KA, KB, KC, KD, KE, KF, KG, KH, KI, KJ, KK, KL, KM, KN, KO, KP };

/* the blend direction of the F, G, J, K corners, two bits each */
typedef struct blend_result
{
	unsigned char f, g, j, k;
} blend_result;

static XBRZ_INLINE blend_result pre_process_corners(const uint32 *_k)
{
	blend_result result;
	double jg, fk;
	result.f = result.g = result.j = result.k = BLEND_NONE;

	if ((_k[KF] == _k[KG] && _k[KJ] == _k[KK]) || (_k[KF] == _k[KJ] && _k[KG] == _k[KK]))
		return result;

	jg = dist_abgr(_k[KI], _k[KF]) + dist_abgr(_k[KF], _k[KC]) + dist_abgr(_k[KN], _k[KK]) +
		dist_abgr(_k[KK], _k[KH]) + 4 * dist_abgr(_k[KJ], _k[KG]);
	fk = dist_abgr(_k[KE], _k[KJ]) + dist_abgr(_k[KJ], _k[KO]) + dist_abgr(_k[KB], _k[KG]) +
		dist_abgr(_k[KG], _k[KL]) + 4 * dist_abgr(_k[KF], _k[KK]);

	if (jg < fk) {
		const unsigned char blend = DOMINANT_DIRECTION_THRESHOLD * jg < fk ? BLEND_DOMINANT : BLEND_NORMAL;
		if (_k[KF] != _k[KG] && _k[KF] != _k[KJ])
			result.f = blend;
		if (_k[KK] != _k[KJ] && _k[KK] != _k[KG])
			result.k = blend;
	} else if (fk < jg) {
		const unsigned char blend = DOMINANT_DIRECTION_THRESHOLD * fk < jg ? BLEND_DOMINANT : BLEND_NORMAL;
		if (_k[KJ] != _k[KF] && _k[KJ] != _k[KK])
			result.j = blend;
		if (_k[KG] != _k[KF] && _k[KG] != _k[KK])
			result.g = blend;
	}
	return result;
}

/* four blend types in a byte */
#define TOP_R(_b) (((_b) >> 2) & 3)
#define BOTTOM_R(_b) (((_b) >> 4) & 3)
#define BOTTOM_L(_b) (((_b) >> 6) & 3)

/* the output block's (@_i, @_j) of @_n x @_n, clockwise @_rot quarter
 * turns: its place before the turn */
#define REF(_i, _j) (*(_out + ((_rot) == 0 ? (_j) + (_i) * _w : \
	(_rot) == 1 ? (_i) + (N - 1 - (_j)) * _w : \
	(_rot) == 2 ? (N - 1 - (_j)) + (N - 1 - (_i)) * _w : \
	(N - 1 - (_i)) + (_j) * _w)))
#define GRAD(_m, _n, _i, _j) REF(_i, _j) = gradient_abgr(_col, REF(_i, _j), _m, _n)
#define SET(_i, _j) REF(_i, _j) = _col

#define BLEND_ARGS uint32 _col, uint32 *_out, int _w, int _rot

#define N 2
static XBRZ_INLINE void shallow2(BLEND_ARGS) { GRAD(1, 4, N - 1, 0); GRAD(3, 4, N - 1, 1); }
static XBRZ_INLINE void steep2(BLEND_ARGS) { GRAD(1, 4, 0, N - 1); GRAD(3, 4, 1, N - 1); }
static XBRZ_INLINE void steep_shallow2(BLEND_ARGS)
{
	GRAD(1, 4, 1, 0); GRAD(1, 4, 0, 1);
	GRAD(5, 6, 1, 1); /* [!] fixes 7/8 used in xBR */
}
static XBRZ_INLINE void diagonal2(BLEND_ARGS) { GRAD(1, 2, 1, 1); }
static XBRZ_INLINE void corner2(BLEND_ARGS)
{
	/* model a round corner */
	GRAD(21, 100, 1, 1); /* exact: 1 - pi/4 = 0.2146018366 */
}
#undef N

#define N 3
static XBRZ_INLINE void shallow3(BLEND_ARGS)
{
	GRAD(1, 4, N - 1, 0); GRAD(1, 4, N - 2, 2);
	GRAD(3, 4, N - 1, 1); SET(N - 1, 2);
}
static XBRZ_INLINE void steep3(BLEND_ARGS)
{
	GRAD(1, 4, 0, N - 1); GRAD(1, 4, 2, N - 2);
	GRAD(3, 4, 1, N - 1); SET(2, N - 1);
}
static XBRZ_INLINE void steep_shallow3(BLEND_ARGS)
{
	GRAD(1, 4, 2, 0); GRAD(1, 4, 0, 2);
	GRAD(3, 4, 2, 1); GRAD(3, 4, 1, 2);
	SET(2, 2);
}
static XBRZ_INLINE void diagonal3(BLEND_ARGS)
{
	GRAD(1, 8, 1, 2); /* conflict with other rotations for this odd scale */
	GRAD(1, 8, 2, 1);
	GRAD(7, 8, 2, 2);
}
static XBRZ_INLINE void corner3(BLEND_ARGS)
{
	/* model a round corner */
	GRAD(45, 100, 2, 2); /* exact: 0.4545939598 */
}
#undef N

#define N 4
static XBRZ_INLINE void shallow4(BLEND_ARGS)
{
	GRAD(1, 4, N - 1, 0); GRAD(1, 4, N - 2, 2);
	GRAD(3, 4, N - 1, 1); GRAD(3, 4, N - 2, 3);
	SET(N - 1, 2); SET(N - 1, 3);
}
static XBRZ_INLINE void steep4(BLEND_ARGS)
{
	GRAD(1, 4, 0, N - 1); GRAD(1, 4, 2, N - 2);
	GRAD(3, 4, 1, N - 1); GRAD(3, 4, 3, N - 2);
	SET(2, N - 1); SET(3, N - 1);
}
static XBRZ_INLINE void steep_shallow4(BLEND_ARGS)
{
	GRAD(3, 4, 3, 1); GRAD(3, 4, 1, 3);
	GRAD(1, 4, 3, 0); GRAD(1, 4, 0, 3);
	GRAD(1, 3, 2, 2); /* [!] fixes 1/4 used in xBR */
	SET(3, 3); SET(3, 2); SET(2, 3);
}
static XBRZ_INLINE void diagonal4(BLEND_ARGS)
{
	GRAD(1, 2, N - 1, N / 2); GRAD(1, 2, N - 2, N / 2 + 1);
	SET(N - 1, N - 1);
}
static XBRZ_INLINE void corner4(BLEND_ARGS)
{
	/* model a round corner */
	GRAD(68, 100, 3, 3); /* exact: 0.6848532563 */
	GRAD(9, 100, 3, 2);  /* 0.08677704501 */
	GRAD(9, 100, 2, 3);  /* 0.08677704501 */
}
#undef N

#define N 5
static XBRZ_INLINE void shallow5(BLEND_ARGS)
{
	GRAD(1, 4, N - 1, 0); GRAD(1, 4, N - 2, 2); GRAD(1, 4, N - 3, 4);
	GRAD(3, 4, N - 1, 1); GRAD(3, 4, N - 2, 3);
	SET(N - 1, 2); SET(N - 1, 3); SET(N - 1, 4); SET(N - 2, 4);
}
static XBRZ_INLINE void steep5(BLEND_ARGS)
{
	GRAD(1, 4, 0, N - 1); GRAD(1, 4, 2, N - 2); GRAD(1, 4, 4, N - 3);
	GRAD(3, 4, 1, N - 1); GRAD(3, 4, 3, N - 2);
	SET(2, N - 1); SET(3, N - 1); SET(4, N - 1); SET(4, N - 2);
}
static XBRZ_INLINE void steep_shallow5(BLEND_ARGS)
{
	GRAD(1, 4, 0, N - 1); GRAD(1, 4, 2, N - 2); GRAD(3, 4, 1, N - 1);
	GRAD(1, 4, N - 1, 0); GRAD(1, 4, N - 2, 2); GRAD(3, 4, N - 1, 1);
	GRAD(2, 3, 3, 3);
	SET(2, N - 1); SET(3, N - 1); SET(4, N - 1);
	SET(N - 1, 2); SET(N - 1, 3);
}
static XBRZ_INLINE void diagonal5(BLEND_ARGS)
{
	GRAD(1, 8, N - 1, N / 2); /* conflict with other rotations for this odd scale */
	GRAD(1, 8, N - 2, N / 2 + 1);
	GRAD(1, 8, N - 3, N / 2 + 2);
	GRAD(7, 8, 4, 3); GRAD(7, 8, 3, 4);
	SET(4, 4);
}
static XBRZ_INLINE void corner5(BLEND_ARGS)
{
	/* model a round corner */
	GRAD(86, 100, 4, 4); /* exact: 0.8631434088 */
	GRAD(23, 100, 4, 3); /* 0.2306749731 */
	GRAD(23, 100, 3, 4); /* 0.2306749731 */
}
#undef N

#define N 6
static XBRZ_INLINE void shallow6(BLEND_ARGS)
{
	GRAD(1, 4, N - 1, 0); GRAD(1, 4, N - 2, 2); GRAD(1, 4, N - 3, 4);
	GRAD(3, 4, N - 1, 1); GRAD(3, 4, N - 2, 3); GRAD(3, 4, N - 3, 5);
	SET(N - 1, 2); SET(N - 1, 3); SET(N - 1, 4); SET(N - 1, 5);
	SET(N - 2, 4); SET(N - 2, 5);
}
static XBRZ_INLINE void steep6(BLEND_ARGS)
{
	GRAD(1, 4, 0, N - 1); GRAD(1, 4, 2, N - 2); GRAD(1, 4, 4, N - 3);
	GRAD(3, 4, 1, N - 1); GRAD(3, 4, 3, N - 2); GRAD(3, 4, 5, N - 3);
	SET(2, N - 1); SET(3, N - 1); SET(4, N - 1); SET(5, N - 1);
	SET(4, N - 2); SET(5, N - 2);
}
static XBRZ_INLINE void steep_shallow6(BLEND_ARGS)
{
	GRAD(1, 4, 0, N - 1); GRAD(1, 4, 2, N - 2); GRAD(3, 4, 1, N - 1); GRAD(3, 4, 3, N - 2);
	GRAD(1, 4, N - 1, 0); GRAD(1, 4, N - 2, 2); GRAD(3, 4, N - 1, 1); GRAD(3, 4, N - 2, 3);
	SET(2, N - 1); SET(3, N - 1); SET(4, N - 1); SET(5, N - 1);
	SET(4, N - 2); SET(5, N - 2);
	SET(N - 1, 2); SET(N - 1, 3);
}
static XBRZ_INLINE void diagonal6(BLEND_ARGS)
{
	GRAD(1, 2, N - 1, N / 2); GRAD(1, 2, N - 2, N / 2 + 1); GRAD(1, 2, N - 3, N / 2 + 2);
	SET(N - 2, N - 1); SET(N - 1, N - 1); SET(N - 1, N - 2);
}
static XBRZ_INLINE void corner6(BLEND_ARGS)
{
	/* model a round corner */
	GRAD(97, 100, 5, 5); /* exact: 0.9711013910 */
	GRAD(42, 100, 4, 5); /* 0.4236372243 */
	GRAD(42, 100, 5, 4); /* 0.4236372243 */
	GRAD(6, 100, 5, 3);  /* 0.05652034508 */
	GRAD(6, 100, 3, 5);  /* 0.05652034508 */
}
#undef N

#undef BLEND_ARGS
#undef SET
#undef GRAD
#undef REF

/* each blend of each scale */
#define SCALER(_name, _scale, _col, _out, _w, _rot) do { switch (_scale) { \
	case 2: _name##2(_col, _out, _w, _rot); break; \
	case 3: _name##3(_col, _out, _w, _rot); break; \
	case 4: _name##4(_col, _out, _w, _rot); break; \
	case 5: _name##5(_col, _out, _w, _rot); break; \
	default: _name##6(_col, _out, _w, _rot); break; } } while (0)

/* the 3x3 kernel turned @_rot quarter turns: where each of b to i is
 * (a is not read) */
static const unsigned char s_rotation[4][8] = {
	{ 1, 2, 3, 4, 5, 6, 7, 8 },
	{ 3, 0, 7, 4, 1, 8, 5, 2 },
	{ 7, 6, 5, 4, 3, 2, 1, 0 },
	{ 5, 8, 1, 4, 7, 0, 3, 6 }
};

/*
input kernel area naming convention:
-------------
| A | B | C |
----|---|---|
| D | E | F | input pixel is at position E
----|---|---|
| G | H | I |
-------------
*/
static XBRZ_INLINE void blend_pixel(int _scale, int _rot, const uint32 *_ker, uint32 *_target, int _trgWidth,
	unsigned char _blendInfo)
{
	const unsigned char *r = s_rotation[_rot];
	const uint32 b = _ker[r[0]], c = _ker[r[1]];
	const uint32 d = _ker[r[2]], e = _ker[r[3]], f = _ker[r[4]];
	const uint32 g = _ker[r[5]], h = _ker[r[6]], i = _ker[r[7]];
	/* the corners turned with the kernel */
	const unsigned blend = _rot == 0 ? _blendInfo :
		(((unsigned)_blendInfo << (2 * _rot)) | ((unsigned)_blendInfo >> (8 - 2 * _rot))) & 0xff;
	int doLineBlend;
	uint32 px;

	if (BOTTOM_R(blend) < BLEND_NORMAL)
		return;

#define EQ(_p1, _p2) (dist_abgr(_p1, _p2) < EQUAL_COLOR_TOLERANCE)
	if (BOTTOM_R(blend) >= BLEND_DOMINANT)
		doLineBlend = 1;
	/* make sure there is no second blending in an adjacent rotation for
	 * this pixel: handles insular pixels, mario eyes; but support
	 * double-blending for 90 degree corners */
	else if (TOP_R(blend) != BLEND_NONE && !EQ(e, g))
		doLineBlend = 0;
	else if (BOTTOM_L(blend) != BLEND_NONE && !EQ(e, c))
		doLineBlend = 0;
	/* no full blending for L-shapes; blend corner only (handles "mario
	 * mushroom eyes") */
	else if (!EQ(e, i) && EQ(g, h) && EQ(h, i) && EQ(i, f) && EQ(f, c))
		doLineBlend = 0;
	else
		doLineBlend = 1;
#undef EQ

	/* choose most similar color */
	px = dist_abgr(e, f) <= dist_abgr(e, h) ? f : h;

	if (doLineBlend) {
		const double fg = dist_abgr(f, g);
		const double hc = dist_abgr(h, c);
		const int haveShallowLine = STEEP_DIRECTION_THRESHOLD * fg <= hc && e != g && d != g;
		const int haveSteepLine = STEEP_DIRECTION_THRESHOLD * hc <= fg && e != c && b != c;
		if (haveShallowLine) {
			if (haveSteepLine)
				SCALER(steep_shallow, _scale, px, _target, _trgWidth, _rot);
			else
				SCALER(shallow, _scale, px, _target, _trgWidth, _rot);
		} else {
			if (haveSteepLine)
				SCALER(steep, _scale, px, _target, _trgWidth, _rot);
			else
				SCALER(diagonal, _scale, px, _target, _trgWidth, _rot);
		}
	} else
		SCALER(corner, _scale, px, _target, _trgWidth, _rot);
}

#define MAX(_a, _b) ((_a) > (_b) ? (_a) : (_b))
#define MIN(_a, _b) ((_a) < (_b) ? (_a) : (_b))

static XBRZ_INLINE void scale_image(int _scale, const uint32 *_src, uint32 *_trg, int _srcWidth, int _srcHeight)
{
	const int trgWidth = _srcWidth * _scale;
	/* "use" space at the end of the image as temporary buffer for "on the
	 * fly preprocessing" */
	const int bufferSize = _srcWidth;
	unsigned char *preProcBuffer;
	int x, y;

	if (_srcHeight <= 0 || _srcWidth <= 0)
		return;

	preProcBuffer = (unsigned char *)(_trg + _srcHeight * _scale * trgWidth) - bufferSize;
	memset(preProcBuffer, 0, (size_t)bufferSize);

	for (y = 0; y < _srcHeight; ++y) {
		uint32 *out = _trg + _scale * y * trgWidth;
		const uint32 *s_m1 = _src + _srcWidth * MAX(y - 1, 0);
		const uint32 *s_0 = _src + _srcWidth * y; /* center line */
		const uint32 *s_p1 = _src + _srcWidth * MIN(y + 1, _srcHeight - 1);
		const uint32 *s_p2 = _src + _srcWidth * MIN(y + 2, _srcHeight - 1);
		unsigned char blend_xy1 = 0; /* corner blending for current (x, y + 1) position */

		for (x = 0; x < _srcWidth; ++x, out += _scale) {
			const int x_m1 = MAX(x - 1, 0);
			const int x_p1 = MIN(x + 1, _srcWidth - 1);
			const int x_p2 = MIN(x + 2, _srcWidth - 1);
			uint32 ker4[16];
			unsigned char blend_xy; /* for current (x, y) position */
			int by, bx;

			ker4[KA] = s_m1[x_m1]; ker4[KB] = s_m1[x]; ker4[KC] = s_m1[x_p1]; ker4[KD] = s_m1[x_p2];
			ker4[KE] = s_0[x_m1]; ker4[KF] = s_0[x]; ker4[KG] = s_0[x_p1]; ker4[KH] = s_0[x_p2];
			ker4[KI] = s_p1[x_m1]; ker4[KJ] = s_p1[x]; ker4[KK] = s_p1[x_p1]; ker4[KL] = s_p1[x_p2];
			ker4[KM] = s_p2[x_m1]; ker4[KN] = s_p2[x]; ker4[KO] = s_p2[x_p1]; ker4[KP] = s_p2[x_p2];

			/* evaluate the four corners on bottom-right of current pixel */
			{
				const blend_result res = pre_process_corners(ker4);
				blend_xy = preProcBuffer[x];
				/* all four corners of (x, y) have been determined at this point
				 * due to processing sequence! */
				blend_xy |= (unsigned char)(res.f << 4);
				/* set 2nd known corner for (x, y + 1) */
				blend_xy1 |= (unsigned char)(res.j << 2);
				/* store on current buffer position for use on next row */
				preProcBuffer[x] = blend_xy1;
				/* set 1st known corner for (x + 1, y + 1) and buffer for use on
				 * next column */
				blend_xy1 = res.k;
				/* set 3rd known corner for (x + 1, y) */
				if (x + 1 < bufferSize)
					preProcBuffer[x + 1] |= (unsigned char)(res.g << 6);
			}

			/* fill block of size scale * scale with the given color, *after*
			 * preprocessing, to not overwrite the results while processing
			 * the last pixel! */
			for (by = 0; by < _scale; ++by)
				for (bx = 0; bx < _scale; ++bx)
					out[by * trgWidth + bx] = ker4[KF];

			/* blend four corners of current pixel */
			if (blend_xy != 0) {
				uint32 ker3[9];
				ker3[0] = ker4[KA]; ker3[1] = ker4[KB]; ker3[2] = ker4[KC];
				ker3[3] = ker4[KE]; ker3[4] = ker4[KF]; ker3[5] = ker4[KG];
				ker3[6] = ker4[KI]; ker3[7] = ker4[KJ]; ker3[8] = ker4[KK];
				blend_pixel(_scale, 0, ker3, out, trgWidth, blend_xy);
				blend_pixel(_scale, 1, ker3, out, trgWidth, blend_xy);
				blend_pixel(_scale, 2, ker3, out, trgWidth, blend_xy);
				blend_pixel(_scale, 3, ker3, out, trgWidth, blend_xy);
			}
		}
	}
}

static void scale2(const uint32 *_src, uint32 *_dst, int _width, int _height) { scale_image(2, _src, _dst, _width, _height); }
static void scale3(const uint32 *_src, uint32 *_dst, int _width, int _height) { scale_image(3, _src, _dst, _width, _height); }
static void scale4(const uint32 *_src, uint32 *_dst, int _width, int _height) { scale_image(4, _src, _dst, _width, _height); }
static void scale5(const uint32 *_src, uint32 *_dst, int _width, int _height) { scale_image(5, _src, _dst, _width, _height); }
static void scale6(const uint32 *_src, uint32 *_dst, int _width, int _height) { scale_image(6, _src, _dst, _width, _height); }

void xbrz_scale_abgr(unsigned factor, const uint32 *src, uint32 *dst, int width, int height)
{
	/* made on first use where xbrz_init was not called, as ever */
	if (s_dist == NULL)
		xbrz_init();
	if (s_dist == NULL) {
		/* without the distances' 64 MB, each texel as a block of its own */
		const int w = width * (int)factor;
		int x, y;
		for (y = 0; y < height * (int)factor; ++y)
			for (x = 0; x < w; ++x)
				dst[y * w + x] = src[(y / (int)factor) * width + x / (int)factor];
		return;
	}
	switch (factor) {
	case 2: scale2(src, dst, width, height); break;
	case 3: scale3(src, dst, width, height); break;
	case 4: scale4(src, dst, width, height); break;
	case 5: scale5(src, dst, width, height); break;
	case 6: scale6(src, dst, width, height); break;
	}
}
