#include <math.h>

#include <retro_inline.h>

#include "Types.h"
#include "N64.h"
#include "GBI.h"
#include "VI.h"
#include "gli_textures.h"
#include "gli_rdram_write.h"

/* One loop per format with its conversion inlined, so a pixel costs no
 * call. A copy starting mid-row writes the rest of that row first; an
 * odd start backs up a pixel to keep the swapped pairs whole */
#define DEFINE_WRITE(name, TSrc, TDst, XOR, TEST, CONV) \
void name(const TSrc *_src, const gli_rdram_write *_w) \
{ \
	TDst *dst = (TDst *)(RDRAM + _w->startAddress); \
	u32 numPixels = _w->numPixels; \
	u32 chunkStart = ((_w->startAddress - _w->bufferAddress) >> (_w->size - 1)) % _w->width; \
	u32 numStored = 0, y = 0, dsty = 0, x; \
	TSrc c; \
	if (chunkStart % 2 != 0) { \
		--chunkStart; \
		--dst; \
		++numPixels; \
	} \
	if (chunkStart > 0) { \
		for (x = chunkStart; x < _w->width; ++x) { \
			c = _src[x]; \
			if (c != (TEST)) \
				dst[numStored ^ (XOR)] = CONV(c); \
			++numStored; \
		} \
		++y; \
		dst += numStored; \
	} \
	for (; y < _w->height; ++y) { \
		for (x = 0; x < _w->width && numStored < numPixels; ++x) { \
			c = _src[x + y * _w->srcStride]; \
			if (c != (TEST)) \
				dst[(x + dsty * _w->width) ^ (XOR)] = CONV(c); \
			++numStored; \
		} \
		++dsty; \
	} \
}

typedef union
{
	struct
	{
		u8 r, g, b, a;
	} c;
	u32 raw;
} rgba_px;

static INLINE u32 to_rgba32(u32 _c)
{
	rgba_px p;
	p.raw = _c;
	return ((u32)p.c.r << 24) | ((u32)p.c.g << 16) | ((u32)p.c.b << 8) | p.c.a;
}

static INLINE u16 to_rgba16(u32 _c)
{
	rgba_px p;
	p.raw = _c;
	return (u16)(((p.c.r >> 3) << 11) | ((p.c.g >> 3) << 6) | ((p.c.b >> 3) << 1) | (p.c.a == 0 ? 0 : 1));
}

#define TO_R8(c) (c)

u16 gli_depth_to_n64(f32 _z)
{
	static const u16 *zLUT = NULL;
	u32 idx = 0x3FFFF;
	if (zLUT == NULL)
		zLUT = gli_depth_zlut();
	if (_z < 0.0f) {
		idx = 0;
	} else if (_z < 1.0f) {
		u32 v;
		_z *= 262144.0f;
		v = (u32)(f32)floor((double)(_z + 0.5f));
		idx = v < 0x3FFFFU ? v : 0x3FFFFU;
	}
	return zLUT[idx];
}

DEFINE_WRITE(gli_rdram_write_rgba32, u32, u32, 0, 0, to_rgba32)
DEFINE_WRITE(gli_rdram_write_rgba16, u32, u16, 1, 0, to_rgba16)
DEFINE_WRITE(gli_rdram_write_r8, u8, u8, 3, 0, TO_R8)
DEFINE_WRITE(gli_rdram_write_depth, f32, u16, 1, 2.0f, gli_depth_to_n64)

void gli_rdram_write_white(u32 _address, u32 _size)
{
	u32 x, y;
	if (_size == G_IM_SIZ_32b) {
		u32 *dst = (u32 *)(RDRAM + _address);
		for (y = 0; y < VI.height; ++y)
			for (x = 0; x < VI.width; ++x)
				dst[x + y * VI.width] = 0xFFFFFFFF;
	} else {
		u16 *dst = (u16 *)(RDRAM + _address);
		for (y = 0; y < VI.height; ++y)
			for (x = 0; x < VI.width; ++x)
				dst[(x + y * VI.width) ^ 1] = 0xFFFF;
	}
}
