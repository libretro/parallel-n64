#include <string.h>

#include "Types.h"
#include "N64.h"
#include "VI.h"
#include "gDP.h"
#include "gli_fb_rdram.h"

static const u32 fingerprint[4] = { 2, 6, 4, 3 };

u32 cutHeight(u32 _address, u32 _height, u32 _stride)
{
	if (_address > RDRAMSize)
		return 0;
	if (_address + _stride * _height > (RDRAMSize + 1))
		return (RDRAMSize + 1 - _address) / _stride;
	return _height;
}

void gli_fb_rdram_mark(u32 _startAddress, u32 _dataSize)
{
	const u32 twoPercent = _dataSize / 200 > 4 ? _dataSize / 200 : 4;
	u32 *pData = (u32*)RDRAM + (_startAddress >> 2);
	u32 i;
	for (i = 0; i < twoPercent; ++i)
		pData[i] = i < 4 ? fingerprint[i] : 0;
}

int gli_fb_rdram_cleared_valid(const gli_fb *_fb)
{
	const u32 testColor = _fb->m_clearParams.fillcolor & 0xFFFEFFFE;
	const u32 stride = _fb->m_width << _fb->m_size >> 1;
	const s32 ulx = _fb->m_clearParams.ulx;
	const s32 uly = _fb->m_clearParams.uly;
	const s32 lrx = _fb->m_clearParams.lrx;
	const s32 lry = (s32)cutHeight(_fb->m_startAddress, (u32)_fb->m_clearParams.lry, stride);
	const u32 ci_width_in_dwords = _fb->m_width >> (3 - _fb->m_size);
	const u32 *dst;
	u32 wrongPixels = 0;
	s32 x, y;
	if (lry == 0)
		return 0;
	dst = (const u32*)RDRAM + (_fb->m_startAddress >> 2) + uly * ci_width_in_dwords;
	for (y = uly; y < lry; ++y) {
		for (x = ulx; x < lrx; ++x) {
			if ((dst[x] & 0xFFFEFFFE) != testColor)
				++wrongPixels;
		}
		dst += ci_width_in_dwords;
	}
	return wrongPixels < (_fb->m_endAddress - _fb->m_startAddress) / 400;
}

int gli_fb_rdram_mark_valid(u32 _startAddress)
{
	const u32 *pData = (const u32*)RDRAM + (_startAddress >> 2);
	u32 i;
	for (i = 0; i < 4; ++i)
		if ((pData[i] & 0xFFFEFFFE) != (fingerprint[i] & 0xFFFEFFFE))
			return 0;
	return 1;
}

int gli_fb_rdram_copy_valid(u32 _startAddress, const u8 *_copy, u32 _size)
{
	const u32 *pData = (const u32*)RDRAM + (_startAddress >> 2);
	const u32 *pCopy = (const u32*)_copy;
	const u32 size_dwords = _size >> 2;
	u32 wrongPixels = 0;
	u32 i;
	/* Nothing written since the copy is the usual case: then there is
	 * nothing to count, and memcmp finds that out the fastest */
	if (memcmp(pData, pCopy, size_dwords << 2) == 0)
		return 0 < _size / 400;
	for (i = 0; i < size_dwords; ++i) {
		if ((pData[i] & 0xFFFEFFFE) != (pCopy[i] & 0xFFFEFFFE))
			++wrongPixels;
	}
	return wrongPixels < _size / 400;
}

/* std::min(std::max(v, lo), hi), comparisons in the same order */
static f32 clampf(f32 _v, f32 _lo, f32 _hi)
{
	const f32 m = _v < _lo ? _lo : _v;
	return _hi < m ? _hi : m;
}

void gli_fb_rdram_fill(s32 _ulx, s32 _uly, s32 _lrx, s32 _lry, s32 *_rect)
{
	u32 stride, lowerBound, ci_width_in_dwords;
	u32 *dst;
	s32 x, y;

	_ulx = (s32)clampf((f32)_ulx, gDP.scissor.ulx, gDP.scissor.lrx);
	_lrx = (s32)clampf((f32)_lrx, gDP.scissor.ulx, gDP.scissor.lrx);
	_uly = (s32)clampf((f32)_uly, gDP.scissor.uly, gDP.scissor.lry);
	_lry = (s32)clampf((f32)_lry, gDP.scissor.uly, gDP.scissor.lry);

	stride = gDP.colorImage.width << gDP.colorImage.size >> 1;
	lowerBound = gDP.colorImage.address + _lry * stride;
	if (lowerBound > RDRAMSize)
		_lry -= (lowerBound - RDRAMSize) / stride;
	ci_width_in_dwords = gDP.colorImage.width >> (3 - gDP.colorImage.size);
	_ulx >>= (3 - gDP.colorImage.size);
	_lrx >>= (3 - gDP.colorImage.size);
	dst = (u32*)(RDRAM + gDP.colorImage.address);
	dst += _uly * ci_width_in_dwords;
	for (y = _uly; y < _lry; ++y) {
		for (x = _ulx; x < _lrx; ++x)
			dst[x] = gDP.fillColor.color;
		dst += ci_width_in_dwords;
	}

	_rect[0] = _ulx;
	_rect[1] = _uly;
	_rect[2] = _lrx;
	_rect[3] = _lry;
}

int gli_fb_overlapping(const gli_fb *_fb1, const gli_fb *_fb2)
{
	u32 diff, stride;
	if (!(_fb1->m_endAddress < _fb2->m_endAddress && _fb1->m_width == _fb2->m_width && _fb1->m_size == _fb2->m_size))
		return 0;
	diff = _fb1->m_endAddress - _fb2->m_startAddress + 1;
	stride = _fb1->m_width << _fb1->m_size >> 1;
	return (diff % stride == 0) && (diff / stride < 5);
}

int gli_fb_is_sub_buffer(const gli_fb *_fb, u32 _swapCount, u32 _address, u32 _width, u32 _size)
{
	u32 stride;
	if (_fb->m_swapCount != _swapCount || _fb->m_cfb || _fb->m_width != _width || _fb->m_size != _size)
		return 0;
	stride = _width << _size >> 1;
	if ((_address - _fb->m_startAddress) % stride != 0)
		return 1;
	return (_fb->m_endAddress - _address + 1) / stride > 5;
}

int gli_fb_runs_into(const gli_fb *_fb, u32 _address, u32 _width, u32 _size)
{
	if (_fb->m_width != _width || _fb->m_size != _size)
		return 0;
	return (_fb->m_endAddress - _address + 1) / (_width << _size >> 1) < 5;
}

void gli_fb_sub_origin(u32 _bufStart, u32 _address, u32 _width, u32 _size, u32 *_originX, u32 *_originY)
{
	const u32 stride = _width << _size >> 1;
	const u32 addrOffset = _address - _bufStart;
	*_originX = (addrOffset % stride) >> (_size - 1);
	*_originY = addrOffset / stride;
}

void gli_fb_update_end_address(gli_fb *_fb)
{
	const u32 height = _fb->m_height > 1 ? _fb->m_height : 1;
	const u32 end = _fb->m_startAddress + (((_fb->m_width * height) << _fb->m_size >> 1) - 1);
	_fb->m_endAddress = end < RDRAMSize ? end : RDRAMSize;
}

int gli_fb_is_auxiliary(const gli_fb *_fb)
{
	return _fb->m_width != VI.width;
}

void gli_fb_set_clear_params(gli_fb *_fb, u32 _fillcolor, s32 _ulx, s32 _uly, s32 _lrx, s32 _lry)
{
	_fb->m_cleared = 1;
	_fb->m_clearParams.fillcolor = _fillcolor;
	_fb->m_clearParams.ulx = _ulx;
	_fb->m_clearParams.lrx = _lrx;
	_fb->m_clearParams.uly = _uly;
	_fb->m_clearParams.lry = _lry;
}

int gli_fb_is_valid(gli_fb *_fb, int _force, u32 _swapCount, const u8 *_copy, u32 _copySize)
{
	if (!_force) {
		if (_fb->m_validityChecked == _swapCount)
			return 1; /* already checked */
		_fb->m_validityChecked = _swapCount;
	}
	if (_fb->m_cleared)
		return gli_fb_rdram_cleared_valid(_fb) != 0;
	if (_fb->m_fingerprint)
		return gli_fb_rdram_mark_valid(_fb->m_startAddress) != 0;
	if (_copySize != 0)
		return gli_fb_rdram_copy_valid(_fb->m_startAddress, _copy, _copySize) != 0;
	return 1; /* no data to decide */
}
