/* Copyright (c) 2017-2022 Hans-Kristian Arntzen
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef GRANITE_POOL_H
#define GRANITE_POOL_H

#include <stddef.h>
#include <stdint.h>
#include <retro_common_api.h>
#include <retro_inline.h>
#include <retro_atomic.h>

RETRO_BEGIN_DECLS

/* Fixed-size slots, handed out and taken back without a call to malloc
 * or free after warm-up. Slots live in blocks that are never moved or
 * freed until the pool is cleared; block k holds 64 << k slots. The pool
 * neither constructs nor destroys what is in a slot. */

/* ---- one thread at a time ---- */

#define GRANITE_POOL_BLOCKS 26

typedef struct granite_pool
{
	size_t elem_size;
	size_t elem_align;
	void *blocks[GRANITE_POOL_BLOCKS];
	unsigned num_blocks;
	/* every slot is either live or in vacants, which holds room for all
	 * of them, so a release never grows it */
	void **vacants;
	size_t num_vacants;
} granite_pool_t;

void granite_pool_init(granite_pool_t *pool, size_t elem_size, size_t elem_align);
/* Releases every block; live slots go with them. */
void granite_pool_clear(granite_pool_t *pool);
/* false when out of memory or blocks */
int granite_pool_grow(granite_pool_t *pool);

/* NULL when out of memory */
static INLINE void *granite_pool_alloc(granite_pool_t *pool)
{
	if (!pool->num_vacants && !granite_pool_grow(pool))
		return NULL;
	return pool->vacants[--pool->num_vacants];
}

static INLINE void granite_pool_release(granite_pool_t *pool, void *slot)
{
	pool->vacants[pool->num_vacants++] = slot;
}

/* ---- any thread, lock-free ----
 * Free slots form a Treiber stack threaded through a separate per-block
 * array of next indices, so a free slot's memory is never touched by the
 * list. The head is one int: the low bits a 1-based slot index (0 is
 * empty), the high bits a tag bumped on every successful CAS, so a slot
 * popped and pushed back between another thread's read and CAS cannot
 * pass for the old head (ABA). 18 index bits give 262,080 slots; the
 * 14-bit tag repeats after 16,384 successful CASes, so a thread
 * preempted between its read and its CAS for exactly a multiple of that
 * many could still be fooled.
 *
 * A thread that finds the list empty grows the pool by the next block,
 * and several may grow at once. Once every block is taken, a thread
 * finding the list empty retries while another thread is still pushing
 * a block's slots, so a slow big block cannot make an allocation fail
 * that its slots would have served. */

#define GRANITE_LF_POOL_BLOCKS 12 /* 64 * (2^12 - 1) = 262080 slots */

typedef struct granite_lf_pool
{
	size_t elem_size;
	size_t elem_align;
	/* a slot's index from its byte offset in its block, without dividing:
	 * (offset >> div_shift) * div_inverse, exact as offsets are multiples
	 * of elem_size */
	uint64_t div_inverse;
	unsigned div_shift;
	retro_atomic_int_t head;
	retro_atomic_int_t block_count;
	/* grows started and not yet pushed */
	retro_atomic_int_t growing;
	retro_atomic_ptr_t block_objs[GRANITE_LF_POOL_BLOCKS];
	/* written before block_objs[k] is published, read only after it is
	 * seen */
	retro_atomic_int_t *block_next[GRANITE_LF_POOL_BLOCKS];
} granite_lf_pool_t;

void granite_lf_pool_init(granite_lf_pool_t *pool, size_t elem_size, size_t elem_align);
/* Releases every block; live slots go with them. Teardown only: no other
 * thread may be using the pool. */
void granite_lf_pool_clear(granite_lf_pool_t *pool);
/* NULL when out of memory or slots */
void *granite_lf_pool_alloc(granite_lf_pool_t *pool);
/* slot: from granite_lf_pool_alloc on this pool */
void granite_lf_pool_release(granite_lf_pool_t *pool, void *slot);

RETRO_END_DECLS

#endif
