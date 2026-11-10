#include <string.h>

#include "Types.h"
#include "N64.h"
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

int gli_fb_rdram_cleared_valid(u32 _startAddress, u32 _endAddress, u32 _width,
		u32 _size, u32 _fillcolor, s32 _ulx, s32 _uly, s32 _lrx, s32 _lry)
{
	const u32 testColor = _fillcolor & 0xFFFEFFFE;
	const u32 stride = _width << _size >> 1;
	const s32 lry = (s32)cutHeight(_startAddress, (u32)_lry, stride);
	const u32 ci_width_in_dwords = _width >> (3 - _size);
	const u32 *dst;
	u32 wrongPixels = 0;
	s32 x, y;
	if (lry == 0)
		return 0;
	dst = (const u32*)RDRAM + (_startAddress >> 2) + _uly * ci_width_in_dwords;
	for (y = _uly; y < lry; ++y) {
		for (x = _ulx; x < _lrx; ++x) {
			if ((dst[x] & 0xFFFEFFFE) != testColor)
				++wrongPixels;
		}
		dst += ci_width_in_dwords;
	}
	return wrongPixels < (_endAddress - _startAddress) / 400;
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
