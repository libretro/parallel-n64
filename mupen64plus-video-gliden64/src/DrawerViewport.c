#include <math.h>

#include "Types.h"
#include "N64.h"
#include "VI.h"
#include "gDP.h"
#include "gSP.h"
#include "Config.h"
#include "gli_drawer.h"

static s32 roundup(f32 _v, f32 _scale)
{
	return (s32)floorf(_v * _scale + 0.5f);
}

int gli_drawer_need_adjust(int _adjustScreen)
{
	return _adjustScreen &&
		gSP.viewport.width < gDP.colorImage.width &&
		(u32)(gSP.viewport.width + gSP.viewport.x * 2.0f) != gDP.colorImage.width &&
		gDP.colorImage.width > VI.width * 98 / 100;
}

void gli_drawer_scissor_rect(f32 _scaleX, f32 _scaleY, f32 _offsetX, f32 _offsetY,
		int _adjust, f32 _adjustScale, int _immersive, s32 *_rect)
{
	f32 SX0 = gDP.scissor.ulx + _offsetX;
	f32 SX1 = gDP.scissor.lrx + _offsetX;
	f32 SY0 = gDP.scissor.uly + _offsetY;
	f32 SY1 = gDP.scissor.lry + _offsetY;
	s32 w, h;

	if ((u32)SX1 == 512 && (config.generalEmulation.hacks & hack_RE2) != 0) {
		SX1 = (f32)*REG.VI_WIDTH;
		SY1 *= 512.0f / SX1;
	}

	if (_adjust) {
		const f32 halfX = gDP.colorImage.width / 2.0f;
		SX0 = (SX0 - halfX) * _adjustScale + halfX;
		SX1 = (SX1 - halfX) * _adjustScale + halfX;
	}

	/* Immersive mode draws the whole picture, including the border the game left */
	if (_immersive && gDP.colorImage.address != gDP.depthImageAddress &&
		gDP.colorImage.width >= VI.width * 98 / 100) {
		SX0 = _offsetX;
		SY0 = _offsetY;
		SX1 = _offsetX + (f32)gDP.colorImage.width;
		SY1 = _offsetY + (f32)VI.height;
	}

	w = roundup(SX1 - SX0, _scaleX);
	h = roundup(SY1 - SY0, _scaleY);
	_rect[0] = roundup(SX0, _scaleX);
	_rect[1] = roundup(SY0, _scaleY);
	_rect[2] = w > 0 ? w : 0;
	_rect[3] = h > 0 ? h : 0;
}

static f32 adjust_viewport_x(f32 _X0, f32 _adjustScale)
{
	const f32 halfX = gDP.colorImage.width / 2.0f;
	const f32 halfVP = gSP.viewport.width / 2.0f;
	return (_X0 + halfVP - halfX) * _adjustScale + halfX - halfVP;
}

void gli_drawer_viewport_rect(int _buffer, f32 _scaleX, f32 _scaleY, u32 _originX, u32 _originY,
		int _adjust, f32 _adjustScale, s32 *_rect)
{
	f32 Xf = gSP.viewport.vscale[0] < 0 ? (gSP.viewport.x + gSP.viewport.vscale[0] * 2.0f) : gSP.viewport.x;
	s32 w, h;
	if (!_buffer) {
		if (_adjust)
			Xf = adjust_viewport_x(Xf, _adjustScale);
		_rect[0] = (s32)(Xf * _scaleX);
		_rect[1] = (s32)(gSP.viewport.y * _scaleY);
		w = (s32)(gSP.viewport.width * _scaleX);
		h = (s32)(gSP.viewport.height * _scaleY);
	} else {
		f32 Yf;
		Xf += (f32)_originX;
		if (_adjust)
			Xf = adjust_viewport_x(Xf, _adjustScale);
		_rect[0] = roundup(Xf, _scaleX);
		Yf = gSP.viewport.vscale[1] < 0 ? (gSP.viewport.y + gSP.viewport.vscale[1] * 2.0f) : gSP.viewport.y;
		Yf += (f32)_originY;
		_rect[1] = roundup(Yf, _scaleY);
		w = roundup(gSP.viewport.width, _scaleX);
		h = roundup(gSP.viewport.height, _scaleY);
	}
	_rect[2] = w > 0 ? w : 0;
	_rect[3] = h > 0 ? h : 0;
}
