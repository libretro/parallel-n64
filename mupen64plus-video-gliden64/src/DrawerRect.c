#include <stddef.h>

#include "Types.h"
#include "GBI.h"
#include "N64.h"
#include "VI.h"
#include "gDP.h"
#include "gSP.h"
#include "gli_drawer.h"

/* The four corners of @_rect (ulx, uly, lrx, lry) in clip space */
static void positions(RectVertex *_v, const f32 *_rect, const f32 *_scale)
{
	const f32 z = gDP.otherMode.depthSource == G_ZS_PRIM ? gDP.primDepth.z : 0.0f;
	const f32 x0 = _rect[0] * (2.0f * _scale[0]) - 1.0f;
	const f32 y0 = _rect[1] * (2.0f * _scale[1]) - 1.0f;
	const f32 x1 = _rect[2] * (2.0f * _scale[0]) - 1.0f;
	const f32 y1 = _rect[3] * (2.0f * _scale[1]) - 1.0f;
	u32 i;
	_v[0].x = x0; _v[0].y = y0;
	_v[1].x = x1; _v[1].y = y0;
	_v[2].x = x0; _v[2].y = y1;
	_v[3].x = x1; _v[3].y = y1;
	for (i = 0; i < 4; ++i) {
		_v[i].z = z;
		_v[i].w = 1.0f;
	}
}

int gli_drawer_immersive_rect(const f32 *_rect, int *_fullScreen)
{
	f32 width, height;
	if (!gli_gSPImmersiveActive() || gDP.colorImage.address == gDP.depthImageAddress ||
			gDP.colorImage.width < VI.width * 98 / 100)
		return 0;
	width = (f32)gDP.colorImage.width;
	height = (f32)VI.height;
	*_fullScreen = _rect[0] <= 1.0f && _rect[1] <= 1.0f && _rect[2] >= width - 2.0f && _rect[3] >= height - 2.0f;
	return 1;
}

/* Rectangles are kept in clip space with w = 1, but the immersive placement
 * works in N64 pixels and returns homogeneous ones: converted there and back
 * with the same scales */
void gli_drawer_immersive_clip(RectVertex *_v, int _fullScreen, const f32 *_scale)
{
	u32 i;
	for (i = 0; i < 4; ++i) {
		_v[i].x = (_v[i].x + 1.0f) / (2.0f * _scale[0]);
		_v[i].y = (_v[i].y + 1.0f) / (2.0f * _scale[1]);
	}
	gli_gSPImmersiveRect(_v, 4, _fullScreen != 0);
	for (i = 0; i < 4; ++i) {
		_v[i].x = _v[i].x * 2.0f * _scale[0] - _v[i].w;
		_v[i].y = _v[i].y * 2.0f * _scale[1] - _v[i].w;
	}
}

int gli_drawer_fill_rect(RectVertex *_v, const s32 *_r, const f32 *_scale, int _adjustScreen, f32 _adjustScale)
{
	f32 rect[4];
	int fullScreen = 0;
	u32 i;

	for (i = 0; i < 4; ++i)
		rect[i] = (f32)_r[i];
	positions(_v, rect, _scale);

	if (_adjustScreen && gDP.colorImage.width > VI.width * 98 / 100 && (u32)(_r[2] - _r[0]) < VI.width * 9 / 10) {
		for (i = 0; i < 4; ++i)
			_v[i].x *= _adjustScale;
	}

	if (gli_drawer_immersive_rect(rect, &fullScreen)) {
		const s32 width = (s32)gDP.colorImage.width;
		const s32 height = (s32)VI.height;
		/* letterbox bars over the full width are not drawn */
		if (!gli_gSPImmersiveBackground() && _r[0] <= 16 && _r[2] >= width - 16 &&
				(_r[1] <= 16 || _r[3] >= height - 16) && (_r[3] - _r[1]) < height / 4)
			return 0;
		if (!fullScreen || gli_gSPImmersiveBackground() || gli_gSPImmersiveClearsShownBuffers())
			gli_drawer_immersive_clip(_v, fullScreen, _scale);
	}
	return 1;
}

/* A tile's texel coordinate for the rect's first texel and its scale, by
 * the tile's shift: 11 to 15 shift left, 1 to 10 right */
static f32 shifted(s16 _c, u32 _shift, f32 *_shiftScale)
{
	*_shiftScale = 1.0f;
	if (_shift > 10) {
		const u32 sh = 16 - _shift;
		_c = (s16)(_c << sh);
		*_shiftScale = (f32)(1 << sh);
	} else if (_shift > 0) {
		_c = (s16)(_c >> _shift);
		*_shiftScale /= (f32)(1 << _shift);
	}
	return _FIXED2FLOAT(_c, 5);
}

/* Tile @_t's s0, t0, s1, t1; returns its clamp bits */
static u32 tile_st(u32 _t, const gli_texrect_params *_p, const gli_tex *_tex,
		f32 _offsetX, f32 _offsetY, f32 *_st)
{
	const gDPTile *tile = gSP.textureTile[_t];
	f32 shiftScaleS, shiftScaleT;
	const f32 uls = shifted(_p->s, tile->shifts, &shiftScaleS);
	const f32 lrs = uls + _offsetX * shiftScaleS;
	const f32 ult = shifted(_p->t, tile->shiftt, &shiftScaleT);
	const f32 lrt = ult + _offsetY * shiftScaleT;
	u32 clamp = 0;

	_st[0] = uls - tile->fuls;
	_st[2] = lrs - tile->fuls;
	_st[1] = ult - tile->fult;
	_st[3] = lrt - tile->fult;

	if (uls > lrs) {
		_st[0] -= _p->dsdx * shiftScaleS;
		_st[2] -= _p->dsdx * shiftScaleS;
	}
	if (ult > lrt) {
		_st[1] -= _p->dtdy * shiftScaleT;
		_st[3] -= _p->dtdy * shiftScaleT;
	}

	if (_tex->frameBufferTexture != GLI_TEX_FB_NONE) {
		_st[0] = _tex->offsetS + _st[0];
		_st[1] = _tex->offsetT + _st[1];
		_st[2] = _tex->offsetS + _st[2];
		_st[3] = _tex->offsetT + _st[3];
	}

	if (_tex->frameBufferTexture != GLI_TEX_FB_MULTI_SAMPLE) {
		const f32 w = (f32)_tex->width;
		const f32 h = (f32)_tex->height;
		/* inside the texture, or a non-masked S far out of range */
		if ((_tex->mirrorS == 0 && _tex->maskS == 0 &&
				(_st[0] < _st[2] ? _st[0] >= 0.0f && _st[2] <= w : _st[2] >= 0.0f && _st[0] <= w))
				|| (_tex->maskS == 0 && (_st[0] < -1024.0f || _st[2] > 1023.99f)))
			clamp |= 1;
		if (_tex->mirrorT == 0 &&
				(_st[1] < _st[3] ? _st[1] >= 0.0f && _st[3] <= h : _st[3] >= 0.0f && _st[1] <= h))
			clamp |= 2;
	}

	_st[0] *= _tex->scaleS;
	_st[1] *= _tex->scaleT;
	_st[2] *= _tex->scaleS;
	_st[3] *= _tex->scaleT;
	return clamp;
}

u32 gli_drawer_texrect_vertices(RectVertex *_v, const gli_texrect_params *_p,
		const gli_tex *const *_tex, const f32 *_scale, int _adjustScreen, f32 _adjustScale)
{
	/* s0, t0, s1, t1 per tile */
	f32 st[2][4];
	f32 offsetX, offsetY;
	u32 t, clamp = 0;

	positions(_v, &_p->ulx, _scale);

	if (_p->flip) {
		offsetX = (_p->lry - _p->uly) * _p->dsdx;
		offsetY = (_p->lrx - _p->ulx) * _p->dtdy;
	} else {
		offsetX = (_p->lrx - _p->ulx) * _p->dsdx;
		offsetY = (_p->lry - _p->uly) * _p->dtdy;
	}

	for (t = 0; t < 2; ++t) {
		if (_tex[t] != NULL)
			clamp |= tile_st(t, _p, _tex[t], offsetX, offsetY, st[t]) << (2 * t);
		else
			st[t][0] = st[t][1] = st[t][2] = st[t][3] = 0.0f;
	}

	_v[0].s0 = st[0][0]; _v[0].t0 = st[0][1]; _v[0].s1 = st[1][0]; _v[0].t1 = st[1][1];
	_v[3].s0 = st[0][2]; _v[3].t0 = st[0][3]; _v[3].s1 = st[1][2]; _v[3].t1 = st[1][3];
	if (_p->flip) {
		_v[1].s0 = st[0][0]; _v[1].t0 = st[0][3]; _v[1].s1 = st[1][0]; _v[1].t1 = st[1][3];
		_v[2].s0 = st[0][2]; _v[2].t0 = st[0][1]; _v[2].s1 = st[1][2]; _v[2].t1 = st[1][1];
	} else {
		_v[1].s0 = st[0][2]; _v[1].t0 = st[0][1]; _v[1].s1 = st[1][2]; _v[1].t1 = st[1][1];
		_v[2].s0 = st[0][0]; _v[2].t0 = st[0][3]; _v[2].s1 = st[1][0]; _v[2].t1 = st[1][3];
	}

	if (_adjustScreen && (_p->forceAjustScale ||
			(gDP.colorImage.width > VI.width * 98 / 100 && (u32)(_p->lrx - _p->ulx) < VI.width * 9 / 10))) {
		for (t = 0; t < 4; ++t)
			_v[t].x *= _adjustScale;
	}
	return clamp;
}

void gli_drawer_copy_rect(RectVertex *_v, const gli_copy_rect *_p)
{
	const f32 scaleX = 1.0f / (f32)_p->dstWidth;
	const f32 scaleY = 1.0f / (f32)_p->dstHeight;
	const f32 scaleS = 1.0f / (f32)_p->srcWidth;
	const f32 scaleT = 1.0f / (f32)_p->srcHeight;
	f32 x0 = (f32)_p->dstX0 * (2.0f * scaleX) - 1.0f;
	f32 y0 = (f32)_p->dstY0 * (2.0f * scaleY) - 1.0f;
	f32 x1 = (f32)_p->dstX1 * (2.0f * scaleX) - 1.0f;
	f32 y1 = (f32)_p->dstY1 * (2.0f * scaleY) - 1.0f;
	const f32 s0 = (f32)_p->srcX0 * scaleS;
	const f32 s1 = (f32)_p->srcX1 * scaleS;
	const f32 t0 = (f32)_p->srcY0 * scaleT;
	const f32 t1 = (f32)_p->srcY1 * scaleT;
	u32 i;

	if (_p->invertX) {
		x0 = -x0;
		x1 = -x1;
	}
	if (_p->invertY) {
		y0 = -y0;
		y1 = -y1;
	}

	_v[0].x = x0; _v[0].y = y0; _v[0].s0 = s0; _v[0].t0 = t0;
	_v[1].x = x1; _v[1].y = y0; _v[1].s0 = s1; _v[1].t0 = t0;
	_v[2].x = x0; _v[2].y = y1; _v[2].s0 = s0; _v[2].t0 = t1;
	_v[3].x = x1; _v[3].y = y1; _v[3].s0 = s1; _v[3].t0 = t1;
	for (i = 0; i < 4; ++i) {
		_v[i].z = 0.0f;
		_v[i].w = 1.0f;
	}
}
