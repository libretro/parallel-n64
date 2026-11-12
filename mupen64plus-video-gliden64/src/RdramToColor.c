#include <string.h>

#include <retro_inline.h>

#include "Types.h"
#include "N64.h"
#include "GBI.h"
#include "gli_rdram_to_color.h"

/* 5 bits to 8, the top bits repeated in the low ones: 0 stays 0 and 31
 * becomes 255. A plain shift left white at 248 */
static INLINE u32 five_to_eight(u32 _c)
{
	return (_c << 3) | (_c >> 2);
}

static INLINE u32 rgba16_to_abgr32(u32 _col, int _fullAlpha)
{
	const u32 r = five_to_eight((_col >> 11) & 31);
	const u32 g = five_to_eight((_col >> 6) & 31);
	const u32 b = five_to_eight((_col >> 1) & 31);
	const u32 a = (_fullAlpha || (_col & 1) != 0) ? 0xFF : 0U;
	return (a << 24) | (b << 16) | (g << 8) | r;
}

static INLINE u32 rgba32_to_abgr32(u32 _col, int _fullAlpha)
{
	const u32 r = (_col >> 24) & 0xff;
	const u32 g = (_col >> 16) & 0xff;
	const u32 b = (_col >> 8) & 0xff;
	const u32 a = _fullAlpha ? 0xFF : (_col & 0xFF);
	return (a << 24) | (b << 16) | (g << 8) | r;
}

void gli_r2c_marks_init(gli_r2c_marks *_m)
{
	memset(_m, 0, sizeof(*_m));
}

void gli_r2c_marks_clear(gli_r2c_marks *_m)
{
	if (_m->count == 0)
		return;
	memset(_m->bits + _m->lo, 0, (_m->hi - _m->lo) * sizeof(u32));
	_m->lo = _m->hi = 0;
	_m->count = 0;
}

void gli_r2c_mark(gli_r2c_marks *_m, u32 _address)
{
	const u32 bit = _address >> 1;
	const u32 word = bit >> 5;
	if (_m->count == 0) {
		_m->minAddress = _m->maxAddress = _address;
		_m->lo = word;
		_m->hi = word + 1;
	} else {
		if (_address < _m->minAddress)
			_m->minAddress = _address;
		if (_address > _m->maxAddress)
			_m->maxAddress = _address;
	}
	_m->count = 1;
	/* past the largest RDRAM: kept in the bounds only, to fail the copy */
	if (word >= GLI_R2C_MARK_WORDS)
		return;
	if (word < _m->lo)
		_m->lo = word;
	if (word >= _m->hi)
		_m->hi = word + 1;
	_m->bits[word] |= 1u << (bit & 31);
}

int gli_r2c_copy_image(u32 _address, u32 _size, u32 *_dst, u32 _width, u32 _height, int _fullAlpha)
{
	u32 any = 0;
	u32 x, y;
	if (_size == G_IM_SIZ_16b) {
		const u16 *src = (const u16 *)(RDRAM + _address);
		const u32 bound = (RDRAMSize + 1 - _address) >> 1;
		for (y = 0; y < _height; ++y) {
			for (x = 0; x < _width; ++x) {
				const u32 idx = (x + y * _width) ^ 1;
				u32 col;
				if (idx >= bound)
					break;
				col = src[idx];
				any |= col;
				_dst[x + y * _width] = rgba16_to_abgr32(col, _fullAlpha);
			}
		}
	} else {
		const u32 *src = (const u32 *)(RDRAM + _address);
		const u32 bound = (RDRAMSize + 1 - _address) >> 2;
		for (y = 0; y < _height; ++y) {
			for (x = 0; x < _width; ++x) {
				const u32 idx = x + y * _width;
				u32 col;
				if (idx >= bound)
					break;
				col = src[idx];
				any |= col;
				_dst[idx] = rgba32_to_abgr32(col, _fullAlpha);
			}
		}
	}
	return any != 0;
}

int gli_r2c_copy_marked(const gli_r2c_marks *_m, u32 _address, u32 _size, u32 *_dst, u32 _width, u32 _height, int _fullAlpha)
{
	const u32 pixelShift = _size == G_IM_SIZ_16b ? 1 : 2;
	const u32 xorIdx = _size == G_IM_SIZ_16b ? 1 : 0;
	const u32 pixels = _width * _height;
	const u32 rdramEnd = RDRAMSize + 1;
	u32 any = 0;
	u32 w;

	memset(_dst, 0, pixels * sizeof(u32));
	if (_m->count == 0)
		return 0;
	/* a mark before the image, or more than a row past it, means the
	 * marks are not this image's: nothing to draw */
	if (_m->minAddress < _address ||
			((_m->maxAddress - _address) >> pixelShift) / _width > _height)
		return 0;

	for (w = _m->lo; w < _m->hi; ++w) {
		u32 bits = _m->bits[w];
		u32 b = 0;
		for (; bits != 0; bits >>= 1, ++b) {
			u32 address, idx, col;
			if ((bits & 1) == 0)
				continue;
			address = ((w << 5) + b) << 1;
			idx = (address - _address) >> pixelShift;
			if (address + (1u << pixelShift) > rdramEnd)
				continue;
			if (_size == G_IM_SIZ_16b) {
				col = ((const u16 *)(RDRAM + _address))[idx];
				any |= col;
				col = rgba16_to_abgr32(col, _fullAlpha);
			} else {
				col = ((const u32 *)(RDRAM + _address))[idx];
				any |= col;
				col = rgba32_to_abgr32(col, _fullAlpha);
			}
			/* the row just under the image counts, as it did, but is not
			 * in the upload */
			if ((idx ^ xorIdx) < pixels)
				_dst[idx ^ xorIdx] = col;
		}
	}
	return any != 0;
}

void gli_r2c_to_float(void *_buf, u32 _count)
{
	/* From the last channel down: the float for channel i lands on bytes
	 * 4i..4i+3, all past i and already read, and byte 0 is read before
	 * its float is written */
	u8 *bytes = (u8 *)_buf;
	u32 i;
	for (i = _count * 4; i-- > 0;) {
		const f32 f = bytes[i] / 255.0f;
		memcpy(bytes + i * 4, &f, sizeof(f));
	}
}

void gli_r2c_to_565(void *_buf, u32 _count)
{
	/* Forward: pixel i's two bytes land on bytes 2i..2i+1, at or before
	 * the four of pixel i, which are read first */
	u8 *bytes = (u8 *)_buf;
	u32 i;
	for (i = 0; i < _count; ++i) {
		u32 c;
		u16 p;
		memcpy(&c, bytes + i * 4, sizeof(c));
		p = (u16)(((c & 0xF8) << 8) | ((c >> 5) & 0x7E0) | ((c >> 19) & 0x1F));
		memcpy(bytes + i * 2, &p, sizeof(p));
	}
}
