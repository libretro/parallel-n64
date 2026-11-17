/* The RDP's coverage, for the renderers that keep none (rdp_coverage.h).
 * C89. The arithmetic follows angrylion's (n64video/rdp blender.c and
 * fbuffer.c). */
#include "rdp_coverage.h"

/* set_other_modes' low word */
#define OM_ALPHA_COMPARE  0x0001u
#define OM_Z_COMPARE      0x0010u
#define OM_IMAGE_READ     0x0040u
#define OM_COLOR_ON_CVG   0x0080u
#define OM_FORCE_BLEND    0x4000u
/* the first cycle's blender inputs: P (2a), A (1b), M (2a), B (2b) */
#define BL_P(l) (((l) >> 30) & 3u)
#define BL_A(l) (((l) >> 26) & 3u)
#define BL_M(l) (((l) >> 22) & 3u)
#define BL_B(l) (((l) >> 18) & 3u)
#define BL_CLR_IN  0u
#define BL_CLR_BL  2u
#define BL_ZERO    3u  /* as A */
#define BL_A_MEM   1u  /* as B */

/* RDRAM keeps each 32-bit word in host order */
static void write8(uint8_t *_rdram, uint32_t _size, uint32_t _addr, uint8_t _v)
{
	if (_addr < _size)
		_rdram[_addr ^ 3] = _v;
}

static void write16(uint8_t *_rdram, uint32_t _size, uint32_t _addr, uint16_t _v)
{
	if (_addr + 1 < _size) {
		_rdram[_addr ^ 3] = (uint8_t)(_v >> 8);
		_rdram[(_addr + 1) ^ 3] = (uint8_t)_v;
	}
}

static void write32(uint8_t *_rdram, uint32_t _size, uint32_t _addr, uint32_t _v)
{
	write16(_rdram, _size, _addr, (uint16_t)(_v >> 16));
	write16(_rdram, _size, _addr + 2, (uint16_t)_v);
}

int rdpcvg_draws_coverage(uint32_t _l)
{
	return (_l & OM_FORCE_BLEND) != 0 && (_l & OM_IMAGE_READ) != 0 &&
		BL_A(_l) == BL_ZERO && BL_M(_l) == BL_CLR_BL && BL_B(_l) == BL_A_MEM;
}

void rdpcvg_fill(uint8_t *_rdram, uint32_t _rdramSize, uint32_t _address, uint32_t _width,
		uint32_t _size, int32_t _ulx, int32_t _uly, int32_t _lrx, int32_t _lry, uint32_t _rgb)
{
	/* Memory alpha of full coverage is 0xe0, which as the blender's B is
	 * 31 and multiplies by 32: the blend color, whole. Coverage written
	 * back is full: the 16-bit alpha bit, the 32-bit alpha's top three. */
	const uint32_t r = (_rgb >> 16) & 0xff, g = (_rgb >> 8) & 0xff, b = _rgb & 0xff;
	const uint16_t c16 = (uint16_t)(((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | 1);
	const uint32_t c32 = (r << 24) | (g << 16) | (b << 8) | 0xe0;
	int32_t x, y;
	if (_ulx < 0)
		_ulx = 0;
	if (_uly < 0)
		_uly = 0;
	if (_lrx > (int32_t)_width)
		_lrx = (int32_t)_width;
	for (y = _uly; y < _lry; ++y) {
		const uint32_t row = (uint32_t)y * _width;
		for (x = _ulx; x < _lrx; ++x) {
			const uint32_t px = row + (uint32_t)x;
			if (_size == 2)
				write16(_rdram, _rdramSize, _address + (px << 1), c16);
			else if (_size == 3)
				write32(_rdram, _rdramSize, _address + (px << 2), c32);
			else if (_size == 1)
				write8(_rdram, _rdramSize, _address + px, (uint8_t)(((_address + px) & 1) ? g : r));
		}
	}
}

/* a 5-bit channel widened as the texture unit widens it */
static uint32_t widen5(uint32_t _c)
{
	return (_c << 3) | (_c >> 2);
}

void rdpcvg_texrect_8(uint8_t *_rdram, uint32_t _rdramSize, uint32_t _address, uint32_t _width,
		int32_t _ulx, int32_t _uly, int32_t _lrx, int32_t _lry, int32_t _s, int32_t _t,
		uint32_t _tileWidth, uint32_t _tileHeight, int _ia, rdpcvg_texel_fn _fetch, const void *_ctx)
{
	int32_t x, y;
	if (_tileWidth == 0 || _tileHeight == 0)
		return;
	if (_ulx < 0) {
		_s -= _ulx;
		_ulx = 0;
	}
	if (_uly < 0) {
		_t -= _uly;
		_uly = 0;
	}
	if (_lrx > (int32_t)_width)
		_lrx = (int32_t)_width;
	for (y = _uly; y < _lry; ++y) {
		int32_t t = _t + (y - _uly);
		const uint32_t row = (uint32_t)y * _width;
		if (t < 0)
			t = 0;
		if (t >= (int32_t)_tileHeight)
			t = (int32_t)_tileHeight - 1;
		for (x = _ulx; x < _lrx; ++x) {
			int32_t s = _s + (x - _ulx);
			uint16_t texel;
			uint32_t r, g;
			const uint32_t addr = _address + row + (uint32_t)x;
			if (s < 0)
				s = 0;
			if (s >= (int32_t)_tileWidth)
				s = (int32_t)_tileWidth - 1;
			texel = _fetch(_ctx, (uint32_t)s, (uint32_t)t);
			if (_ia)
				r = g = (uint32_t)(texel >> 8);
			else {
				r = widen5((uint32_t)(texel >> 11) & 0x1f);
				g = widen5((uint32_t)(texel >> 6) & 0x1f);
			}
			write8(_rdram, _rdramSize, addr, (uint8_t)((addr & 1) ? g : r));
		}
	}
}

int rdpcvg_is_texel_copy(uint32_t _cycleType, uint32_t _l, uint32_t _mulRgb1, uint32_t _addRgb1, uint32_t _ciSize,
		uint32_t _tileSize, uint32_t _tileFormat, double _dsdx, double _dtdy, int _flip)
{
	if (_cycleType != 0 || _ciSize != 1 || _tileSize != 2 || (_tileFormat != 0 && _tileFormat != 3))
		return 0;
	if (_dsdx != 1.0 || _dtdy != 1.0 || _flip)
		return 0;
	if ((_l & (OM_FORCE_BLEND | OM_IMAGE_READ | OM_ALPHA_COMPARE | OM_Z_COMPARE | OM_COLOR_ON_CVG)) != 0 ||
			BL_P(_l) != BL_CLR_IN)
		return 0;
	/* one cycle reads the second cycle's combiner: TEXEL0 added to a
	 * product by zero */
	return _mulRgb1 >= 16 && _addRgb1 == 1;
}
