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
 * The draws index the ring in vertices (or indices), BufferedDrawer
 * keeping the element its writes are at beside the byte offset: each
 * draw must read the element its data starts at. A move to a segment
 * start used to set the element to 0, and the segments did not start on
 * a vertex (8 MB / 3 is not a multiple of 32 or 44 bytes): GLideN64 drew
 * garbage triangles once a game went past the first segment. Over the
 * strides BufferedDrawer uses, every placement must start on an element
 * and the element kept must be the one the placement is at; the old
 * cursor must fail that.
 */
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "../mupen64plus-video-gliden64/src/Graphics/OpenGLContext/opengl_RingSegments.h"

struct Draw
{
	u32 start, end; /* bytes it reads */
	bool done;
};

struct Gpu
{
	std::vector<Draw> draws;
	std::vector<size_t> fenceAt; /* draws issued when each segment was fenced */
	u32 lag;
	u32 waits;
	bool failed;

	Gpu(u32 _lag, u32 _segments) : fenceAt(_segments, 0), lag(_lag), waits(0), failed(false) {}

	void run()
	{
		/* draws more than lag behind have run */
		if (draws.size() > lag)
			for (size_t i = 0; i + lag < draws.size(); ++i)
				draws[i].done = true;
	}
	void fence(u32 _seg) { fenceAt[_seg] = draws.size(); }
	void wait(u32 _seg)
	{
		bool blocked = false;
		for (size_t i = 0; i < fenceAt[_seg]; ++i)
			if (!draws[i].done) {
				draws[i].done = true;
				blocked = true;
			}
		if (blocked)
			++waits;
	}
	bool overwrites(u32 _start, u32 _end) const
	{
		for (const Draw &d : draws)
			if (!d.done && _start < d.end && d.start < _end)
				return true;
		return false;
	}
};

struct Ops
{
	Gpu &gpu;
	void fence(u32 _seg) { gpu.fence(_seg); }
	void wait(u32 _seg) { gpu.wait(_seg); }
};

static unsigned rng = 12345;
static u32 next_rand(u32 _n) { rng = rng * 1103515245u + 12345u; return (rng >> 8) % _n; }

/* returns whether every write kept clear of the GPU's data */
template <bool Old>
static bool run(u32 _size, u32 _maxAlloc, u32 _lag, u32 &_waits)
{
	opengl::RingSegments<3> ring;
	Gpu gpu(_lag, 3);
	Ops ops = { gpu };
	u32 offset = 0;
	ring.init(_size);
	for (u32 k = 0; k < 4000; ++k) {
		const u32 bytes = 4 + 4 * next_rand(_maxAlloc / 4);
		gpu.run();
		u32 start;
		if (Old)
			start = offset + bytes >= _size ? 0 : offset;
		else
			start = ring.place(offset, bytes, ops);
		if (gpu.overwrites(start, start + bytes))
			gpu.failed = true;
		Draw d = { start, start + bytes, false };
		gpu.draws.push_back(d);
		offset = start + bytes;
	}
	_waits = gpu.waits;
	return !gpu.failed;
}

/* BufferedDrawer's cursor over a ring of _stride byte elements: whether
 * every draw read the elements it wrote */
template <bool Old>
static bool run_cursor(u32 _size, u32 _stride, u32 _maxCount)
{
	opengl::RingSegments<3> ring;
	Gpu gpu(16, 3);
	Ops ops = { gpu };
	u32 offset = 0, pos = 0;
	ring.init(_size, Old ? 1 : _stride);
	if (!Old && ring.size() % (3 * _stride) != 0)
		return false;
	for (u32 k = 0; k < 4000; ++k) {
		const u32 count = 1 + next_rand(_maxCount);
		const u32 bytes = count * _stride;
		gpu.run();
		const u32 start = ring.place(offset, bytes, ops);
		if (start != offset) {
			offset = start;
			pos = Old ? 0 : ring.element(start);
		}
		/* the draw reads from element pos: its data is at offset */
		if (start % _stride != 0 || pos * _stride != start || start + bytes > _size)
			return false;
		Draw d = { start, start + bytes, false };
		gpu.draws.push_back(d);
		offset += bytes;
		pos += count;
	}
	return true;
}

int main()
{
	int failures = 0;
	unsigned runs = 0;
	const u32 sizes[] = { 3000, 4096, 9000, 65536 };
	for (u32 size : sizes)
		for (u32 maxAlloc = 8; maxAlloc <= size / 3; maxAlloc *= 4)
			for (u32 lag = 0; lag <= 64; lag += 8) {
				u32 waits = 0;
				++runs;
				if (!run<false>(size, maxAlloc, lag, waits)) {
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

	/* RectVertex, Vertex, GLushort: what BufferedDrawer's rings hold */
	const u32 strides[] = { 32, 44, 2 };
	const u32 cursorSizes[] = { 49152, 50000, 1u << 20, 8u << 20 };
	for (u32 size : cursorSizes)
		for (u32 stride : strides) {
			const u32 maxCount = size / 3 / stride < 256 ? size / 3 / stride : 256;
			++runs;
			if (!run_cursor<false>(size, stride, maxCount)) {
				fprintf(stderr, "size %u stride %u: a draw read other elements than it wrote\n",
				        size, stride);
				++failures;
			}
		}
	if (run_cursor<true>(8u << 20, 44, 256) || run_cursor<true>(49152, 32, 256)) {
		fprintf(stderr, "the check passes the old cursor\n");
		return 1;
	}

	/* the check catches the old wrap */
	{
		u32 waits = 0;
		if (run<true>(4096, 256, 32, waits)) {
			fprintf(stderr, "the check passes the old wrap\n");
			return 1;
		}
	}

	if (failures) {
		fprintf(stderr, "test_gliden64_ring_segments: %d failures\n", failures);
		return 1;
	}
	printf("test_gliden64_ring_segments: %u runs ok\n", runs);
	return 0;
}
