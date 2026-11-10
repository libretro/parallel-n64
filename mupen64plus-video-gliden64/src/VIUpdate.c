#include <math.h>

#include "Types.h"
#include "N64.h"
#include "GBI.h"
#include "Config.h"
#include "gli_vi_update.h"

void gli_vi_state_init(gli_vi_state *_s)
{
	_s->oldvstart = 0;
	_s->prevvicurrent = 0;
	_s->prevwasblank = 0;
	_s->prevserrate = 0;
	_s->oldlowerfield = 0;
	_s->emucontrolsvicurrent = -1;
}

/* From angrylion's VI */
int gli_vi_update(gli_vi_state *_s, gli_vi_result *_r)
{
	const s32 PRESCALE_WIDTH = 640;
	const s32 PRESCALE_HEIGHT = 625;

	const s32 x_add = _SHIFTR(*REG.VI_X_SCALE, 0, 12);
	const s32 y_add = _SHIFTR(*REG.VI_Y_SCALE, 0, 12);
	const u32 v_sync = _SHIFTR(*REG.VI_V_SYNC, 0, 10);
	const int ispal = v_sync > 550;
	const s32 x1 = _SHIFTR(*REG.VI_H_START, 16, 10);
	const s32 y1 = _SHIFTR(*REG.VI_V_START, 16, 10);
	const s32 x2 = _SHIFTR(*REG.VI_H_START, 0, 10);
	const s32 y2 = _SHIFTR(*REG.VI_V_START, 0, 10);

	const s32 delta_x = x2 - x1;
	const s32 delta_y = y2 - y1;
	const u32 vitype = _SHIFTR(*REG.VI_STATUS, 0, 2);

	const int serration_pulses = (*REG.VI_STATUS & 0x40) != 0;
	const int validinterlace = ((vitype & 2) != 0) && serration_pulses;
	int lowerfield = 0;
	int h_start_clamped, hres_clamped;
	s32 hres, vres, h_start, v_start, vactivelines;
	u32 x_start, y_start;

	if (validinterlace && _s->prevserrate && _s->emucontrolsvicurrent < 0)
		_s->emucontrolsvicurrent = (*REG.VI_V_CURRENT_LINE & 1) != _s->prevvicurrent ? 1 : 0;

	if (validinterlace) {
		if (_s->emucontrolsvicurrent == 1)
			lowerfield = (*REG.VI_V_CURRENT_LINE & 1) == 0;
		else if (!_s->emucontrolsvicurrent) {
			if (y1 == _s->oldvstart)
				lowerfield = !_s->oldlowerfield;
			else
				lowerfield = y1 < _s->oldvstart;
		}
	}

	_s->oldlowerfield = (u8)lowerfield;

	if (validinterlace) {
		_s->prevserrate = 1;
		_s->prevvicurrent = *REG.VI_V_CURRENT_LINE & 1;
		_s->oldvstart = y1;
	} else
		_s->prevserrate = 0;

	hres = delta_x;
	vres = delta_y;
	h_start = x1 - (ispal ? 128 : 108);
	v_start = (y1 - (ispal ? 44 : 34)) / 2;
	x_start = _SHIFTR(*REG.VI_X_SCALE, 16, 12);
	y_start = _SHIFTR(*REG.VI_Y_SCALE, 16, 12);

	h_start_clamped = h_start < 0;
	if (h_start < 0) {
		x_start -= x_add * h_start;
		hres += h_start;
		h_start = 0;
	}

	if (v_start < 0) {
		y_start += y_add * (u32)(-v_start);
		v_start = 0;
	}

	vres >>= 1;

	hres_clamped = hres + h_start > PRESCALE_WIDTH;
	if (hres_clamped)
		hres = PRESCALE_WIDTH - h_start;
	if (vres + v_start > PRESCALE_HEIGHT)
		vres = PRESCALE_HEIGHT - v_start;

	vactivelines = v_sync - (ispal ? 44 : 34);
	if (vactivelines > PRESCALE_HEIGHT || vactivelines < 0)
		return 0;

	if (hres <= 0 || vres <= 0 || ((vitype & 2) == 0 && _s->prevwasblank))
		return 0;

	if ((vitype & 2) == 0) {
		_s->prevwasblank = 1;
		return 0;
	}

	_s->prevwasblank = 0;

	_r->vi_hres = hres;
	_r->vi_vres = vres;
	_r->vi_ispal = (u8)ispal;
	_r->vi_h_start = h_start;
	_r->vi_v_start = v_start;
	_r->vi_x_start = x_start;
	_r->vi_y_start = y_start;
	_r->vi_x_add = x_add;
	_r->vi_y_add = y_add;
	_r->vi_minhpass = h_start_clamped ? 0 : 8;
	_r->vi_maxhpass = hres_clamped ? 0 : 7;
	_r->vi_width = _SHIFTR(*REG.VI_WIDTH, 0, 12);
	_r->vi_lowerfield = (u8)lowerfield;
	_r->vi_origin = _SHIFTR(*REG.VI_ORIGIN, 0, 24);
	_r->vi_fsaa = (*REG.VI_STATUS & 512) == 0;
	_r->vi_divot = (*REG.VI_STATUS & 16) != 0;
	return 1;
}

void gli_vi_source(const gli_vi_result *_r, u32 _bufStart, u32 _bufWidth, u32 _bufHeight,
		u32 _bufSize, f32 _bufScale, u32 _texRealHeight, gli_vi_source_rect *_o)
{
	const u32 addrOffset = ((_r->vi_origin - _bufStart) << 1 >> _bufSize);
	s32 srcY0 = addrOffset / _bufWidth;
	s32 dstY0 = _r->vi_v_start;
	s32 XoffsetLeft = 0, XoffsetRight = 0;
	u32 srcWidth;
	s32 srcHeight;

	if ((addrOffset != 0) && (_bufWidth == addrOffset * 2))
		srcY0 = 1;

	if ((_r->vi_width != addrOffset * 2) && (addrOffset % _r->vi_width != 0))
		XoffsetRight = _r->vi_width - addrOffset % _r->vi_width;
	if ((u32)XoffsetRight == _bufWidth) {
		XoffsetRight = 0;
	} else if (XoffsetRight > (s32)(_bufWidth / 2)) {
		XoffsetRight = 0;
		XoffsetLeft = addrOffset % _r->vi_width;
	}

	if (!_r->vi_lowerfield) {
		if (srcY0 > 0 && (_bufWidth > 320 || _bufHeight > 240))
			--srcY0;
		if (dstY0 > 0)
			--dstY0;
	}

	if ((config.generalEmulation.hacks & hack_LodeRunner) != 0) {
		srcY0 = 1;
		XoffsetRight = XoffsetLeft = 0;
	}

	srcWidth = (_r->vi_hres * _r->vi_x_add) >> 10;
	if (_r->vi_width < srcWidth)
		srcWidth = _r->vi_width;
	srcHeight = _r->vi_width * ((_r->vi_vres * _r->vi_y_add + _r->vi_y_start) >> 10) / _bufWidth;

	/* Resident Evil 2: four more rows so the second subtitle line is not cut,
	 * as far as the texture holds them */
	if ((config.generalEmulation.hacks & hack_RE2) != 0) {
		if ((s32)ceilf((f32)(srcY0 + srcHeight + 4) * _bufScale) <= (s32)_texRealHeight)
			srcHeight += 4;
	}

	_o->srcY0 = srcY0;
	_o->dstY0 = dstY0;
	_o->XoffsetLeft = XoffsetLeft;
	_o->XoffsetRight = XoffsetRight;
	_o->srcWidth = (s32)srcWidth;
	_o->srcHeight = srcHeight;
}
