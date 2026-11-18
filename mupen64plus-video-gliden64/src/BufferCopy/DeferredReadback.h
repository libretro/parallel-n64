#ifndef DeferredReadback_H
#define DeferredReadback_H

/* The order of the asynchronous copies to RDRAM. Each read goes into the
 * next of a ring of slots and keeps what it was read for, so it lands
 * where it was meant to go. A read is written to RDRAM at a later copy
 * once the GPU has finished it, oldest first. The copy waits only when
 * the slot it is about to reuse is still in flight, i.e. when the GPU is
 * a whole ring of copies behind. C89; inline.
 *
 * What a slot was read for is the copy's own, kept by slot: it is set for
 * the slot begin hands out before issued, and write reads it. */

#include <retro_inline.h>
#include "../Types.h"

#define GLI_READBACK_MAX_SLOTS 3

typedef struct gli_readback_ops
{
	/* whether @_slot's read has landed, waiting for it when @_wait */
	int (*ready)(void *_ctx, u32 _slot, int _wait);
	/* @_slot's read, landed, written to RDRAM */
	void (*write)(void *_ctx, u32 _slot);
	void *ctx;
} gli_readback_ops;

typedef struct gli_readback
{
	u32 slots;
	u32 next;
	u8 pending[GLI_READBACK_MAX_SLOTS];
} gli_readback;

/* @_slots slots, one to GLI_READBACK_MAX_SLOTS, none in flight */
static INLINE void gli_readback_reset(gli_readback *_r, u32 _slots)
{
	u32 i;
	_r->slots = _slots == 0 ? 1 : (_slots > GLI_READBACK_MAX_SLOTS ? GLI_READBACK_MAX_SLOTS : _slots);
	_r->next = 0;
	for (i = 0; i < GLI_READBACK_MAX_SLOTS; ++i)
		_r->pending[i] = 0;
}

static INLINE void gli_readback_retire(gli_readback *_r, const gli_readback_ops *_ops, u32 _slot)
{
	_r->pending[_slot] = 0;
	_ops->write(_ops->ctx, _slot);
}

/* Writes what has finished and hands back the slot to read into. */
static INLINE u32 gli_readback_begin(gli_readback *_r, const gli_readback_ops *_ops)
{
	u32 i;
	for (i = 0; i < _r->slots; ++i) {
		const u32 slot = (_r->next + i) % _r->slots;
		if (!_r->pending[slot])
			continue;
		if (!_ops->ready(_ops->ctx, slot, 0))
			break;
		gli_readback_retire(_r, _ops, slot);
	}
	if (_r->pending[_r->next]) {
		_ops->ready(_ops->ctx, _r->next, 1);
		gli_readback_retire(_r, _ops, _r->next);
	}
	return _r->next;
}

/* The read begin handed out is on its way. */
static INLINE void gli_readback_issued(gli_readback *_r)
{
	_r->pending[_r->next] = 1;
	_r->next = (_r->next + 1) % _r->slots;
}

/* Writes every read in flight, waiting for each; the next slot is free
 * afterwards, for a read that is waited for at once. */
static INLINE u32 gli_readback_drain(gli_readback *_r, const gli_readback_ops *_ops)
{
	u32 i;
	for (i = 0; i < _r->slots; ++i) {
		const u32 slot = (_r->next + i) % _r->slots;
		if (_r->pending[slot]) {
			_ops->ready(_ops->ctx, slot, 1);
			gli_readback_retire(_r, _ops, slot);
		}
	}
	return _r->next;
}

static INLINE int gli_readback_pending(const gli_readback *_r, u32 _slot)
{
	return _r->pending[_slot];
}

#endif
