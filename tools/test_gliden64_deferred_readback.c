/* test_gliden64_deferred_readback -- the order of GLideN64's asynchronous
 * copies to RDRAM (BufferCopy/DeferredReadback.h).
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../mupen64plus-video-gliden64/src/BufferCopy/DeferredReadback.h"

#define COPIES 40
#define NONE 0xFFFFFFFFu

typedef struct write_s
{
	u32 id;      /* the read the rows came from */
	u32 geom;    /* what they were written as */
} write_s;

typedef struct gpu_s
{
	u32 lag;                 /* copies until a read is finished */
	u32 now;                 /* the copy under way */
	u32 issuedAt[COPIES];
	u32 slotRead[GLI_READBACK_MAX_SLOTS]; /* the read in each slot, NONE for none */
	u32 waits;
	write_s writes[COPIES * 2];
	u32 numWrites;
	u32 geometry[GLI_READBACK_MAX_SLOTS]; /* what each slot was read for */
	u32 waited[GLI_READBACK_MAX_SLOTS];   /* the read last waited for in each slot */
	u32 early;                            /* writes of reads not landed */
} gpu_s;

static gpu_s gpu;

static int finished(u32 _slot)
{
	const u32 read = gpu.slotRead[_slot];
	return read == NONE || gpu.now >= gpu.issuedAt[read] + gpu.lag;
}

static int ready(void *_ctx, u32 _slot, int _wait)
{
	(void)_ctx;
	if (finished(_slot))
		return 1;
	if (_wait) {
		++gpu.waits;
		gpu.waited[_slot] = gpu.slotRead[_slot];
	}
	return _wait;
}

static void write_as(u32 _slot, u32 _geom)
{
	write_s *w = &gpu.writes[gpu.numWrites++];
	if (!finished(_slot) && gpu.waited[_slot] != gpu.slotRead[_slot])
		++gpu.early;
	w->id = gpu.slotRead[_slot];
	w->geom = _geom;
}

static void write_op(void *_ctx, u32 _slot)
{
	(void)_ctx;
	write_as(_slot, gpu.geometry[_slot]);
}

static const gli_readback_ops ops = { ready, write_op, NULL };

static int failures;
static int quiet;

static int fail(const char *_what, u32 _slots, u32 _lag, u32 _a, u32 _b)
{
	++failures;
	if (!quiet)
		fprintf(stderr, "slots %u lag %u: %s (%u, %u)\n", _slots, _lag, _what, _a, _b);
	return 0;
}

/* the copies: the current protocol, or the old one */
static gli_readback rb;
static u32 oldSlots, oldCur;

static void copy_current(u32 _k, int _sync)
{
	const u32 slot = _sync ? gli_readback_drain(&rb, &ops) : gli_readback_begin(&rb, &ops);
	gpu.slotRead[slot] = _k;
	gpu.issuedAt[_k] = _k;
	if (_sync) {
		ready(NULL, slot, 1);
		write_as(slot, _k);
	} else {
		gpu.geometry[slot] = _k;
		gli_readback_issued(&rb);
	}
}

/* What the copy did before: read into one buffer, wait for the next and
 * write it as this copy. */
static void copy_old(u32 _k, int _sync)
{
	gpu.slotRead[oldCur] = _k;
	gpu.issuedAt[_k] = _k;
	if (!_sync) {
		oldCur = (oldCur + 1) % oldSlots;
		ready(NULL, oldCur, 1);
	} else {
		ready(NULL, oldCur, 1);
	}
	write_as(oldCur, _k);
}

/* One run: copies, every syncEvery-th synchronous; returns the checks. */
static int run(int _old, u32 _slots, u32 _lag, u32 _syncEvery)
{
	int sync[COPIES], seen[COPIES], last = -1;
	u32 k, j, i;

	memset(&gpu, 0, sizeof(gpu));
	gpu.lag = _lag;
	for (i = 0; i < GLI_READBACK_MAX_SLOTS; ++i)
		gpu.slotRead[i] = gpu.waited[i] = NONE;
	gli_readback_reset(&rb, _slots);
	oldSlots = _slots;
	oldCur = 0;

	for (k = 0; k < COPIES; ++k) {
		gpu.now = k;
		sync[k] = _syncEvery && (k % _syncEvery) == _syncEvery - 1;
		if (_old)
			copy_old(k, sync[k]);
		else
			copy_current(k, sync[k]);
	}

	if (gpu.early)
		return fail("a read written before it landed", _slots, _lag, gpu.early, 0);
	/* every write is the read it was meant as, once, in order */
	memset(seen, 0, sizeof(seen));
	for (i = 0; i < gpu.numWrites; ++i) {
		const write_s *w = &gpu.writes[i];
		if (w->id == NONE)
			return fail("wrote a buffer nothing was read into", _slots, _lag, i, 0);
		if (w->id != w->geom)
			return fail("a read written as another copy", _slots, _lag, w->id, w->geom);
		if (seen[w->id])
			return fail("a read written twice", _slots, _lag, w->id, 0);
		if ((int)w->id <= last)
			return fail("a read written out of order", _slots, _lag, w->id, (u32)last);
		seen[w->id] = 1;
		last = (int)w->id;
	}
	/* a synchronous copy has everything before it, and itself, in RDRAM */
	for (k = 0; k < COPIES; ++k) {
		if (!sync[k])
			continue;
		for (j = 0; j <= k; ++j)
			if (!seen[j])
				return fail("a read missing at a synchronous copy", _slots, _lag, j, k);
	}
	/* no wait while the GPU keeps within the ring */
	if (_lag <= _slots && !_syncEvery && gpu.waits != 0)
		return fail("waits within the ring", _slots, _lag, gpu.waits, 0);
	return 1;
}

int main(void)
{
	unsigned runs = 0;
	u32 slots, lag, syncEvery;
	int current_failures;

	for (slots = 1; slots <= 3; ++slots)
		for (lag = 0; lag <= 5; ++lag)
			for (syncEvery = 0; syncEvery <= 4; ++syncEvery) {
				if (slots == 1 && syncEvery != 1)
					continue; /* one buffer is the synchronous mode */
				if (!run(0, slots, lag, syncEvery))
					fprintf(stderr, "  (syncEvery %u)\n", syncEvery);
				++runs;
			}
	current_failures = failures;

	/* the checks catch what the copy did before */
	failures = 0;
	quiet = 1;
	if (run(1, 2, 1, 0) || run(1, 2, 2, 0)) {
		fprintf(stderr, "the checks pass the old protocol\n");
		return 1;
	}

	/* reset bounds the slots */
	gli_readback_reset(&rb, 0);
	if (rb.slots != 1) {
		fprintf(stderr, "no slots taken as one\n");
		return 1;
	}
	rb.next = 2;
	gli_readback_reset(&rb, 9);
	if (rb.slots != GLI_READBACK_MAX_SLOTS || rb.next != 0 || gli_readback_pending(&rb, 0)) {
		fprintf(stderr, "too many slots taken as the most, none in flight\n");
		return 1;
	}
	gli_readback_issued(&rb);
	if (!gli_readback_pending(&rb, 0) || gli_readback_pending(&rb, 1)) {
		fprintf(stderr, "the read issued in flight, alone\n");
		return 1;
	}

	if (current_failures) {
		fprintf(stderr, "test_gliden64_deferred_readback: %d failures\n", current_failures);
		return 1;
	}
	printf("test_gliden64_deferred_readback: %u runs ok\n", runs);
	return 0;
}
