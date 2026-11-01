/* test_gliden64_write_rdram -- GLideN64's RDRAM writeback reading rows in
 * place.
 *
 * The colour and depth copies to RDRAM used to compact what the GPU read
 * back into a buffer of their own, rows width pixels apart, before
 * converting it into RDRAM. They now convert straight from the read-back
 * rows, which lie stride pixels apart. This holds the converted RDRAM to
 * what the old compacting path wrote, over widths, strides, heights,
 * partial first rows, pixel counts, XOR swizzles and every source type,
 * with the source exactly as long as its last row so that a read past it
 * shows under the address sanitizer.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "../mupen64plus-video-gliden64/src/BufferCopy/WriteToRDRAM.h"

/* the writeback as it was, over compacted rows */
template <typename TSrc, typename TDst>
static void writeToRdramCompact(TSrc* _src, TDst* _dst, TDst(*converter)(TSrc _c), TSrc _testValue, u32 _xor, u32 _width, u32 _height, u32 _numPixels, u32 _startAddress, u32 _bufferAddress, u32 _bufferSize)
{
	u32 chunkStart = ((_startAddress - _bufferAddress) >> (_bufferSize - 1)) % _width;
	if (chunkStart % 2 != 0) {
		--chunkStart;
		--_dst;
		++_numPixels;
	}

	u32 numStored = 0;
	u32 y = 0;
	TSrc c;
	if (chunkStart > 0) {
		for (u32 x = chunkStart; x < _width; ++x) {
			c = _src[x];
			if (c != _testValue)
				_dst[numStored ^ _xor] = converter(c);
			++numStored;
		}
		++y;
		_dst += numStored;
	}

	u32 dsty = 0;
	for (; y < _height; ++y) {
		for (u32 x = 0; x < _width && numStored < _numPixels; ++x) {
			c = _src[x + y *_width];
			if (c != _testValue)
				_dst[(x + dsty*_width) ^ _xor] = converter(c);
			++numStored;
		}
		++dsty;
	}
}

static u32 rgba32(u32 c) { return (c >> 8) | (c << 24); }
static u16 rgba16(u32 c) { return (u16)(((c >> 3) & 0x1f) << 11 | ((c >> 11) & 0x1f) << 6 | ((c >> 19) & 0x1f) << 1 | (c >> 31)); }
static u8 r8(u8 c) { return (u8)(c ^ 0x5a); }
static u16 depth16(f32 z) { return (u16)(z * 65535.0f); }

static u32 rng = 0x12345678u;
static u32 rnd(u32 n)
{
	rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
	return n ? rng % n : 0;
}

template <typename TSrc> static TSrc value(u32 i, TSrc test);
template <> u32 value<u32>(u32 i, u32 test) { return (i % 7) == 0 ? test : (i * 2654435761u); }
template <> u8 value<u8>(u32 i, u8 test) { return (i % 5) == 0 ? test : (u8)(i * 37u + 11u); }
template <> f32 value<f32>(u32 i, f32 test) { return (i % 6) == 0 ? test : (f32)((i * 97u) % 1000u) / 1000.0f; }

template <typename TSrc, typename TDst>
static int run(const char *name, TDst(*conv)(TSrc), TSrc test, u32 size, u32 xr, int cases)
{
	int fails = 0;
	for (int n = 0; n < cases; ++n) {
		const u32 width = 2 + rnd(640);
		const u32 stride = width + (rnd(3) == 0 ? 0 : rnd(80));
		const u32 height = 1 + rnd(48);
		const u32 bpp = 1u << (size - 1);
		const u32 row = rnd(height);
		const u32 col = rnd(width);
		const u32 bufferAddress = 0x100000;
		const u32 startAddress = bufferAddress + (row * width + col) * bpp;
		u32 numPixels = height * width - (rnd(2) ? rnd(width * height) : 0);

		/* the read-back rows, the last one no longer than the width */
		std::vector<TSrc> src((height - 1) * stride + width);
		std::vector<TSrc> compact(width * height);
		for (u32 y = 0; y < height; ++y)
			for (u32 x = 0; x < width; ++x)
				compact[x + y * width] = src[x + y * stride] = value<TSrc>(x * 131u + y * 977u + n, test);

		const size_t dstLen = (size_t)width * height + 16;
		std::vector<TDst> a(dstLen + 8, (TDst)0x77), b(dstLen + 8, (TDst)0x77);
		writeToRdramCompact<TSrc, TDst>(compact.data(), a.data() + 8, conv, test, xr, width, height, numPixels, startAddress, bufferAddress, size);
		writeToRdram<TSrc, TDst>(src.data(), stride, b.data() + 8, conv, test, xr, width, height, numPixels, startAddress, bufferAddress, size);
		if (memcmp(a.data(), b.data(), a.size() * sizeof(TDst))) {
			if (fails++ < 5)
				fprintf(stderr, "FAIL %s: width %u stride %u height %u start %u,%u pixels %u\n",
				        name, width, stride, height, row, col, numPixels);
		}
	}
	printf("test_gliden64_write_rdram: %s: %d cases, %d differ\n", name, cases, fails);
	return fails;
}

int main()
{
	int fails = 0;
	fails += run<u32, u32>("RGBA32", rgba32, 0u, 3, 0, 4000);
	fails += run<u32, u16>("RGBA16", rgba16, 0u, 2, 1, 4000);
	fails += run<u8, u8>("R8", r8, (u8)0, 1, 3, 4000);
	fails += run<f32, u16>("depth", depth16, 2.0f, 2, 1, 4000);
	return fails ? 1 : 0;
}
