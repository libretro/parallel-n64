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
