/* test_gliden64_ring_segments -- GLideN64's draw data ring cannot
 * overwrite what the GPU has yet to read.
 *
 * The persistently mapped vertex and index buffers used to wrap back to
 * the start when full, with nothing between the CPU's new writes and the
 * draws still queued on the old data: a GPU a frame or two behind read
 * vertices from the next lap. The ring is now in segments, fenced when the
 * writes leave one and waited for before they come back.
 *
 * A GPU is simulated that runs each draw only some draws after it was
 * issued, or when a fence after it is waited for. Every write is checked
 * against the bytes of the draws not yet run, over ring sizes, allocation
 * sizes and lags, and the waits are counted: there must be none while the
 * GPU keeps within a lap less a segment. The old wrap must fail the check.
 *
 * The draws index the ring in vertices (or indices), the buffered drawer
 * keeping the element its writes are at beside the byte offset: each
 * draw must read the element its data starts at. A move to a segment
 * start used to set the element to 0, and the segments did not start on
 * a vertex (8 MB / 3 is not a multiple of 32 or 44 bytes): GLideN64 drew
 * garbage triangles once a game went past the first segment. Over the
 * strides the drawer uses, every placement must start on an element and
 * the element kept must be the one the placement is at; the old cursor
 * must fail that.
 */
#include <stdio.h>
#include <stdlib.h>

#include "../mupen64plus-video-gliden64/src/Graphics/OpenGLContext/opengl_RingSegments.h"

#define DRAWS 4000

typedef struct draw
{
	u32 start, end; /* bytes it reads */
	int done;
} draw;

typedef struct gpu
{
	draw draws[DRAWS];
	u32 count;
	u32 fenceAt[GLI_RING_SEGMENTS]; /* draws issued when each segment was fenced */
	u32 lag;
	u32 waits;
	int failed;
} gpu;

static gpu g;

static void gpu_reset(u32 _lag)
{
	u32 i;
	g.count = 0;
	for (i = 0; i < GLI_RING_SEGMENTS; ++i)
		g.fenceAt[i] = 0;
	g.lag = _lag;
	g.waits = 0;
	g.failed = 0;
}

/* draws more than lag behind have run */
static void gpu_run(void)
{
	u32 i;
	for (i = 0; i + g.lag < g.count; ++i)
		g.draws[i].done = 1;
}

static void gpu_fence(void *_ctx, u32 _seg)
{
	(void)_ctx;
	g.fenceAt[_seg] = g.count;
}

static void gpu_wait(void *_ctx, u32 _seg)
{
	u32 i;
	int blocked = 0;
	(void)_ctx;
	for (i = 0; i < g.fenceAt[_seg]; ++i)
		if (!g.draws[i].done) {
			g.draws[i].done = 1;
			blocked = 1;
		}
	if (blocked)
		++g.waits;
}

static int gpu_overwrites(u32 _start, u32 _end)
{
	u32 i;
	for (i = 0; i < g.count; ++i)
		if (!g.draws[i].done && _start < g.draws[i].end && g.draws[i].start < _end)
			return 1;
	return 0;
}

static void gpu_issue(u32 _start, u32 _end)
{
	g.draws[g.count].start = _start;
	g.draws[g.count].end = _end;
	g.draws[g.count].done = 0;
	++g.count;
}

static const gli_ring_ops ops = { gpu_fence, gpu_wait, NULL };

static unsigned rng = 12345;
static u32 next_rand(u32 _n) { rng = rng * 1103515245u + 12345u; return (rng >> 8) % _n; }

/* whether every write kept clear of the GPU's data */
static int run(int _old, u32 _size, u32 _maxAlloc, u32 _lag, u32 *_waits)
{
	gli_ring ring;
	u32 offset = 0, k;
	gpu_reset(_lag);
	gli_ring_init(&ring, _size, 1);
	for (k = 0; k < DRAWS; ++k) {
		const u32 bytes = 4 + 4 * next_rand(_maxAlloc / 4);
		u32 start;
		gpu_run();
		if (_old)
			start = offset + bytes >= _size ? 0 : offset;
		else
			start = gli_ring_place(&ring, offset, bytes, &ops);
		if (gpu_overwrites(start, start + bytes))
			g.failed = 1;
		gpu_issue(start, start + bytes);
		offset = start + bytes;
	}
	*_waits = g.waits;
	return !g.failed;
}

/* the drawer's cursor over a ring of _stride byte elements: whether every
 * draw read the elements it wrote */
static int run_cursor(int _old, u32 _size, u32 _stride, u32 _maxCount)
{
	gli_ring ring;
	u32 offset = 0, pos = 0, k;
	gpu_reset(16);
	gli_ring_init(&ring, _size, _old ? 1 : _stride);
	if (!_old && ring.size % (GLI_RING_SEGMENTS * _stride) != 0)
		return 0;
	for (k = 0; k < DRAWS; ++k) {
		const u32 count = 1 + next_rand(_maxCount);
		const u32 bytes = count * _stride;
		u32 start;
		gpu_run();
		start = gli_ring_place(&ring, offset, bytes, &ops);
		if (start != offset) {
			offset = start;
			pos = _old ? 0 : gli_ring_element(&ring, start);
		}
		/* the draw reads from element pos: its data is at offset */
		if (start % _stride != 0 || pos * _stride != start || start + bytes > _size)
			return 0;
		gpu_issue(start, start + bytes);
		offset += bytes;
		pos += count;
	}
	return 1;
}

int main(void)
{
	static const u32 sizes[] = { 3000, 4096, 9000, 65536 };
	/* RectVertex, the triangles' vertex, GLushort: what the drawer's rings hold */
	static const u32 strides[] = { 32, 44, 2 };
	static const u32 cursorSizes[] = { 49152, 50000, 1u << 20, 8u << 20 };
	int failures = 0;
	unsigned runs = 0;
	u32 i, j, maxAlloc, lag, waits;

	for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
		const u32 size = sizes[i];
		for (maxAlloc = 8; maxAlloc <= size / 3; maxAlloc *= 4)
			for (lag = 0; lag <= 64; lag += 8) {
				waits = 0;
				++runs;
				if (!run(0, size, maxAlloc, lag, &waits)) {
					fprintf(stderr, "size %u alloc %u lag %u: a write overwrote data a draw had yet to read\n",
					        size, maxAlloc, lag);
					++failures;
				}
				/* lag draws of at most maxAlloc fit two segments: no wait */
				if ((lag + 1) * maxAlloc <= 2 * (size / 3) - maxAlloc && waits != 0) {
					fprintf(stderr, "size %u alloc %u lag %u: %u waits within the ring\n",
					        size, maxAlloc, lag, waits);
					++failures;
				}
			}
	}

	for (i = 0; i < sizeof(cursorSizes) / sizeof(cursorSizes[0]); ++i)
		for (j = 0; j < sizeof(strides) / sizeof(strides[0]); ++j) {
			const u32 size = cursorSizes[i], stride = strides[j];
			const u32 maxCount = size / 3 / stride < 256 ? size / 3 / stride : 256;
			++runs;
			if (!run_cursor(0, size, stride, maxCount)) {
				fprintf(stderr, "size %u stride %u: a draw read other elements than it wrote\n",
				        size, stride);
				++failures;
			}
		}
	if (run_cursor(1, 8u << 20, 44, 256) || run_cursor(1, 49152, 32, 256)) {
		fprintf(stderr, "the check passes the old cursor\n");
		return 1;
	}

	/* the check catches the old wrap */
	if (run(1, 4096, 256, 32, &waits)) {
		fprintf(stderr, "the check passes the old wrap\n");
		return 1;
	}

	if (failures) {
		fprintf(stderr, "test_gliden64_ring_segments: %d failures\n", failures);
		return 1;
	}
	printf("test_gliden64_ring_segments: %u runs ok\n", runs);
	return 0;
}
