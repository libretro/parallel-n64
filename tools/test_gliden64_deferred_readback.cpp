/* test_gliden64_deferred_readback -- the order of GLideN64's asynchronous
 * colour copies to RDRAM.
 *
 * The asynchronous copy used to read into one pixel buffer and write the
 * other: the read before, whatever it had been read for, at the address
 * and size of the copy at hand, and on the first copy a buffer nothing had
 * been read into. It also waited for that read on every copy. Now each
 * read keeps what it was read for, is written once the GPU has finished
 * it, oldest first, and a copy waits only when the slot it reuses is still
 * in flight.
 *
 * A GPU is simulated that finishes each read a number of copies after it
 * was issued, over ring sizes, lags and synchronous copies in between. The
 * checks are run on a model of the old protocol too, which must fail them.
 */
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "../mupen64plus-video-gliden64/src/BufferCopy/DeferredReadback.h"

struct Geometry
{
	u32 id;
};

struct Write
{
	u32 id;      /* the read the rows came from */
	u32 geom;    /* what they were written as */
};

struct Gpu
{
	u32 lag;                 /* copies until a read is finished */
	u32 now;                 /* the copy under way */
	std::vector<u32> issuedAt;
	std::vector<int> slotRead; /* the read in each slot, -1 for none */
	u32 waits;
	std::vector<Write> writes;

	Gpu(u32 _lag, u32 _slots) : lag(_lag), now(0), slotRead(_slots, -1), waits(0) {}

	bool finished(u32 _slot) const
	{
		const int read = slotRead[_slot];
		return read < 0 || now >= issuedAt[read] + lag;
	}
};

struct Ops : DeferredReadback<Geometry>::Ops
{
	Gpu &gpu;
	explicit Ops(Gpu &_gpu) : gpu(_gpu) {}
	bool ready(u32 _slot, bool _wait) override
	{
		if (gpu.finished(_slot))
			return true;
		if (_wait)
			++gpu.waits;
		return _wait;
	}
	void write(u32 _slot, const Geometry &_g) override
	{
		Write w;
		w.id = gpu.slotRead[_slot] < 0 ? ~0u : u32(gpu.slotRead[_slot]);
		w.geom = _g.id;
		gpu.writes.push_back(w);
	}
};

static int failures;
static bool quiet;

#define CHECK(cond, ...) do { if (!(cond)) { ++failures; if (!quiet) { fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } return false; } } while (0)

/* One run: copies, every syncEvery-th synchronous; returns the checks. */
template <typename Protocol>
static bool run(u32 _slots, u32 _lag, u32 _syncEvery)
{
	const u32 copies = 40;
	Gpu gpu(_lag, _slots);
	Protocol p(_slots);
	std::vector<bool> sync(copies, false);

	for (u32 k = 0; k < copies; ++k) {
		gpu.now = k;
		sync[k] = _syncEvery && (k % _syncEvery) == _syncEvery - 1;
		p.copy(gpu, k, sync[k]);
	}

	/* every write is the read it was meant as, once, in order */
	int last = -1;
	std::vector<bool> seen(copies, false);
	for (const Write &w : gpu.writes) {
		CHECK(w.id != ~0u, "slots %u lag %u: wrote a buffer nothing was read into", _slots, _lag);
		CHECK(w.id == w.geom, "slots %u lag %u: read %u written as copy %u", _slots, _lag, w.id, w.geom);
		CHECK(!seen[w.id], "slots %u lag %u: read %u written twice", _slots, _lag, w.id);
		CHECK(int(w.id) > last, "slots %u lag %u: read %u written after %d", _slots, _lag, w.id, last);
		seen[w.id] = true;
		last = int(w.id);
	}
	/* a synchronous copy has everything before it, and itself, in RDRAM */
	for (u32 k = 0; k < copies; ++k) {
		if (!sync[k])
			continue;
		for (u32 j = 0; j <= k; ++j)
			CHECK(seen[j], "slots %u lag %u: read %u missing at synchronous copy %u", _slots, _lag, j, k);
	}
	/* no wait while the GPU keeps within the ring */
	if (_lag <= _slots && !_syncEvery)
		CHECK(gpu.waits == 0, "slots %u lag %u: %u waits", _slots, _lag, gpu.waits);
	return true;
}

struct Current
{
	DeferredReadback<Geometry> r;
	explicit Current(u32 _slots) { r.reset(_slots); }
	void copy(Gpu &gpu, u32 k, bool _sync)
	{
		Ops ops(gpu);
		const u32 slot = _sync ? r.drain(ops) : r.begin(ops);
		gpu.slotRead[slot] = int(k);
		gpu.issuedAt.push_back(k);
		Geometry g = { k };
		if (_sync) {
			ops.ready(slot, true);
			ops.write(slot, g);
		} else {
			r.issued(g);
		}
	}
};

/* What the copy did before: read into one buffer, wait for the next and
 * write it as this copy. */
struct Old
{
	u32 slots, cur;
	explicit Old(u32 _slots) : slots(_slots), cur(0) {}
	void copy(Gpu &gpu, u32 k, bool _sync)
	{
		Ops ops(gpu);
		gpu.slotRead[cur] = int(k);
		gpu.issuedAt.push_back(k);
		Geometry g = { k };
		if (!_sync) {
			cur = (cur + 1) % slots;
			ops.ready(cur, true);
		} else {
			ops.ready(cur, true);
		}
		ops.write(cur, g);
	}
};

int main()
{
	unsigned runs = 0;
	for (u32 slots = 1; slots <= 3; ++slots)
		for (u32 lag = 0; lag <= 5; ++lag)
			for (u32 syncEvery = 0; syncEvery <= 4; ++syncEvery) {
				if (slots == 1 && syncEvery != 1)
					continue; /* one buffer is the synchronous mode */
				if (!run<Current>(slots, lag, syncEvery)) {
					fprintf(stderr, "  (syncEvery %u)\n", syncEvery);
				}
				++runs;
			}
	const int current_failures = failures;

	/* the checks catch what the copy did before */
	failures = 0;
	quiet = true;
	if (run<Old>(2, 1, 0) || run<Old>(2, 2, 0)) {
		fprintf(stderr, "the checks pass the old protocol\n");
		return 1;
	}
	failures = 0;

	if (current_failures) {
		fprintf(stderr, "test_gliden64_deferred_readback: %d failures\n", current_failures);
		return 1;
	}
	printf("test_gliden64_deferred_readback: %u runs ok\n", runs);
	return 0;
}
