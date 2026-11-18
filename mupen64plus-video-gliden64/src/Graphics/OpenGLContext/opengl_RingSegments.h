#ifndef GLI_GL_RING_SEGMENTS_H
#define GLI_GL_RING_SEGMENTS_H

/* Where the draw data goes in a persistently mapped ring: in
 * GLI_RING_SEGMENTS parts, each fenced when the writes move on from it and
 * waited for before they come back to it. A wait holds only for draws a
 * whole lap behind, and the ring cannot overwrite what the GPU has yet to
 * read. C89, inline for the drawer that streams through it. */

#include <boolean.h>
#include <Types.h>
#include <retro_inline.h>

#define GLI_RING_SEGMENTS 3

typedef struct gli_ring
{
	u32 size;
	u32 segSize;
	u32 align;
	u32 cur;
	bool fenced[GLI_RING_SEGMENTS];
} gli_ring;

/* What a placement does with the GPU: marks the draws so far when the
 * writes leave segment @_seg, and waits for that mark when they come back */
typedef struct gli_ring_ops
{
	void (*fence)(void *_ctx, u32 _seg);
	void (*wait)(void *_ctx, u32 _seg);
	void *ctx;
} gli_ring_ops;

/* @_align: the size of what the ring holds. The draws index the ring in
 * those (a vertex, an index), so every segment starts on one. */
static INLINE void gli_ring_init(gli_ring *_ring, u32 _size, u32 _align)
{
	u32 i;
	_ring->align = _align != 0 ? _align : 1;
	_ring->segSize = _size / GLI_RING_SEGMENTS / _ring->align * _ring->align;
	_ring->size = _ring->segSize * GLI_RING_SEGMENTS;
	_ring->cur = 0;
	for (i = 0; i < GLI_RING_SEGMENTS; ++i)
		_ring->fenced[i] = 0;
}

static INLINE u32 gli_ring_seg(const gli_ring *_ring, u32 _at)
{
	const u32 seg = _at / _ring->segSize;
	return seg < GLI_RING_SEGMENTS ? seg : GLI_RING_SEGMENTS - 1;
}

/* The offset for @_bytes (at most a segment) at @_offset, moved on to the
 * next segment when they would straddle two, so each draw reads from one */
static INLINE u32 gli_ring_place(gli_ring *_ring, u32 _offset, u32 _bytes, const gli_ring_ops *_ops)
{
	u32 start = _offset, target;
	bool round;
	if (gli_ring_seg(_ring, start) != gli_ring_seg(_ring, start + _bytes - 1))
		start = (gli_ring_seg(_ring, start) + 1) * _ring->segSize;
	if (start + _bytes > _ring->size || gli_ring_seg(_ring, start) != gli_ring_seg(_ring, start + _bytes - 1))
		start = 0;
	target = gli_ring_seg(_ring, start);
	/* going round to an earlier segment, or to this one from its end */
	round = start < _offset && target == _ring->cur;
	while (_ring->cur != target || round) {
		_ops->fence(_ops->ctx, _ring->cur);
		_ring->fenced[_ring->cur] = 1;
		_ring->cur = (_ring->cur + 1) % GLI_RING_SEGMENTS;
		if (_ring->fenced[_ring->cur]) {
			_ops->wait(_ops->ctx, _ring->cur);
			_ring->fenced[_ring->cur] = 0;
		}
		round = 0;
	}
	return start;
}

/* the element a placed offset is at: what a draw indexes the ring by */
static INLINE u32 gli_ring_element(const gli_ring *_ring, u32 _offset)
{
	return _offset / _ring->align;
}

#endif
