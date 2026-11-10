#include "Types.h"
#include "GBI.h"
#include "N64.h"
#include "VI.h"
#include "gDP.h"
#include "gSP.h"
#include "gli_drawer.h"

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
	const f32 z = gDP.otherMode.depthSource == G_ZS_PRIM ? gDP.primDepth.z : 0.0f;
	const f32 x0 = (f32)_r[0] * (2.0f * _scale[0]) - 1.0f;
	const f32 y0 = (f32)_r[1] * (2.0f * _scale[1]) - 1.0f;
	const f32 x1 = (f32)_r[2] * (2.0f * _scale[0]) - 1.0f;
	const f32 y1 = (f32)_r[3] * (2.0f * _scale[1]) - 1.0f;
	f32 rect[4];
	int fullScreen = 0;
	u32 i;

	_v[0].x = x0; _v[0].y = y0;
	_v[1].x = x1; _v[1].y = y0;
	_v[2].x = x0; _v[2].y = y1;
	_v[3].x = x1; _v[3].y = y1;
	for (i = 0; i < 4; ++i) {
		_v[i].z = z;
		_v[i].w = 1.0f;
	}

	if (_adjustScreen && gDP.colorImage.width > VI.width * 98 / 100 && (u32)(_r[2] - _r[0]) < VI.width * 9 / 10) {
		for (i = 0; i < 4; ++i)
			_v[i].x *= _adjustScale;
	}

	for (i = 0; i < 4; ++i)
		rect[i] = (f32)_r[i];
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
