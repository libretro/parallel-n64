/* test_gliden64_write_rdram -- GLideN64's RDRAM writeback.
 *
 * The colour and depth copies to RDRAM convert straight from the
 * read-back rows, stride pixels apart, one loop per format with its
 * conversion inlined (RdramWrite.c). This holds what they write to what
 * the compacting writeback they replaced wrote, through the same
 * conversions, over widths, strides, heights, partial first rows, odd
 * starts, pixel counts and every format, with the source exactly as long
 * as its last row and RDRAM exactly as long as the copy can reach, so that
 * a read or write past either shows under the address sanitizer.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../mupen64plus-video-gliden64/src/Types.h"
#include "../mupen64plus-video-gliden64/src/gli_rdram_write.h"

u8 *RDRAM;
u32 RDRAMSize;
struct { u32 width, height; } VI_dummy;
static u16 zlut[0x40000];
const u16 *gli_depth_zlut(void) { return zlut; }

typedef union { struct { u8 r, g, b, a; } c; u32 raw; } px;
static u32 ref32(u32 v) { px p; p.raw = v; return ((u32)p.c.r << 24) | ((u32)p.c.g << 16) | ((u32)p.c.b << 8) | p.c.a; }
static u16 ref16(u32 v) { px p; p.raw = v; return (u16)(((p.c.r >> 3) << 11) | ((p.c.g >> 3) << 6) | ((p.c.b >> 3) << 1) | (p.c.a == 0 ? 0 : 1)); }
static u16 refz(f32 z)
{
	u32 idx = 0x3FFFF;
	if (z < 0.0f)
		idx = 0;
	else if (z < 1.0f) {
		u32 v = (u32)floorf(z * 262144.0f + 0.5f);
		idx = v < 0x3FFFF ? v : 0x3FFFF;
	}
	return zlut[idx];
}

static u32 rng = 0x12345678u;
static u32 rnd(u32 n)
{
	rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
	return n ? rng % n : 0;
}

/* the writeback as it was, over compacted rows: kind 0 RGBA32, 1 RGBA16,
 * 2 8-bit, 3 depth */
static void reference(int kind, const void *src, u8 *dst8, u32 width, u32 height, u32 numPixels, u32 start, u32 buf, u32 size)
{
	const u32 xr = kind == 0 ? 0 : (kind == 2 ? 3 : 1);
	u32 chunkStart = ((start - buf) >> (size - 1)) % width;
	long d = 0;
	u32 numStored = 0, y = 0, dsty = 0, x;
	if (chunkStart % 2 != 0) { --chunkStart; d = -1; ++numPixels; }
#define PUT(i, k) do { \
		if (kind == 0) { u32 c = ((const u32 *)src)[k]; if (c != 0) ((u32 *)dst8)[d + (long)((i) ^ xr)] = ref32(c); } \
		else if (kind == 1) { u32 c = ((const u32 *)src)[k]; if (c != 0) ((u16 *)dst8)[d + (long)((i) ^ xr)] = ref16(c); } \
		else if (kind == 2) { u8 c = ((const u8 *)src)[k]; if (c != 0) dst8[d + (long)((i) ^ xr)] = c; } \
		else { f32 c = ((const f32 *)src)[k]; if (c != 2.0f) ((u16 *)dst8)[d + (long)((i) ^ xr)] = refz(c); } \
	} while (0)
	if (chunkStart > 0) {
		for (x = chunkStart; x < width; ++x) { PUT(numStored, x); ++numStored; }
		++y;
		d += numStored;
	}
	for (; y < height; ++y) {
		for (x = 0; x < width && numStored < numPixels; ++x) { PUT(x + dsty * width, x + y * width); ++numStored; }
		++dsty;
	}
#undef PUT
}

static int run(const char *name, int kind, u32 size, int cases)
{
	int fails = 0, n;
	const u32 bpp = 1u << (size - 1);
	for (n = 0; n < cases; ++n) {
		const u32 width = 2 + rnd(640);
		const u32 stride = width + (rnd(3) == 0 ? 0 : rnd(80));
		const u32 height = 1 + rnd(48);
		const u32 row = rnd(height), col = rnd(width);
		const u32 buf = 64;
		const u32 start = buf + (row * width + col) * bpp;
		const u32 numPixels = height * width - (rnd(2) ? rnd(width * height) : 0);
		const u32 srcLen = (height - 1) * stride + width;
		const u32 esz = kind == 2 ? 1 : (kind == 3 ? 4 : 4);
		const u32 span = start + (width * height + 8) * bpp;
		u8 *src = (u8 *)malloc(srcLen * esz), *compact = (u8 *)malloc(width * height * esz);
		u8 *a = (u8 *)malloc(span), *b = (u8 *)malloc(span);
		gli_rdram_write w;
		u32 x, y;
		for (y = 0; y < height; ++y)
			for (x = 0; x < width; ++x) {
				const u32 i = x * 131u + y * 977u + (u32)n;
				const u32 s = x + y * stride, c = x + y * width;
				if (kind == 2) { u8 v = (i % 5) == 0 ? 0 : (u8)(i * 37u + 11u); src[s] = compact[c] = v; }
				else if (kind == 3) { f32 v = (i % 6) == 0 ? 2.0f : (f32)((i * 97u) % 1200u) / 1000.0f - 0.1f; memcpy(src + s * 4, &v, 4); memcpy(compact + c * 4, &v, 4); }
				else { u32 v = (i % 7) == 0 ? 0 : i * 2654435761u; memcpy(src + s * 4, &v, 4); memcpy(compact + c * 4, &v, 4); }
			}
		memset(a, 0x77, span);
		memset(b, 0x77, span);
		reference(kind, compact, a + start, width, height, numPixels, start, buf, size);
		RDRAM = b;
		RDRAMSize = span - 1;
		w.srcStride = stride; w.width = width; w.height = height; w.numPixels = numPixels;
		w.startAddress = start; w.bufferAddress = buf; w.size = size;
		if (kind == 0) gli_rdram_write_rgba32((const u32 *)src, &w);
		else if (kind == 1) gli_rdram_write_rgba16((const u32 *)src, &w);
		else if (kind == 2) gli_rdram_write_r8(src, &w);
		else gli_rdram_write_depth((const f32 *)src, &w);
		if (memcmp(a, b, span)) {
			if (fails++ < 5)
				fprintf(stderr, "FAIL %s: width %u stride %u height %u start %u,%u pixels %u\n",
				        name, width, stride, height, row, col, numPixels);
		}
		free(src); free(compact); free(a); free(b);
	}
	printf("test_gliden64_write_rdram: %s: %d cases, %d differ\n", name, cases, fails);
	return fails;
}

int main(void)
{
	int fails = 0;
	u32 i;
	for (i = 0; i < 0x40000; ++i)
		zlut[i] = (u16)(i * 40503u >> 2);
	fails += run("RGBA32", 0, 3, 4000);
	fails += run("RGBA16", 1, 2, 4000);
	fails += run("R8", 2, 1, 4000);
	fails += run("depth", 3, 2, 4000);
	return fails ? 1 : 0;
}
