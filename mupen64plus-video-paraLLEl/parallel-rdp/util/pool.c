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

#include <stdlib.h>
#include <boolean.h>
#if defined(_MSC_VER)
#include <intrin.h>
#endif
#include "pool.h"
#include "aligned_alloc.h"

static size_t pool_block_align(size_t elem_align)
{
	return elem_align > 64 ? elem_align : 64;
}

/* ---- one thread at a time ---- */

void granite_pool_init(granite_pool_t *pool, size_t elem_size, size_t elem_align)
{
	unsigned k;
	pool->elem_size = elem_size;
	pool->elem_align = elem_align;
	for (k = 0; k < GRANITE_POOL_BLOCKS; k++)
		pool->blocks[k] = NULL;
	pool->num_blocks = 0;
	pool->vacants = NULL;
	pool->num_vacants = 0;
}

void granite_pool_clear(granite_pool_t *pool)
{
	unsigned k;
	for (k = 0; k < pool->num_blocks; k++)
	{
		granite_memalign_free(pool->blocks[k]);
		pool->blocks[k] = NULL;
	}
	free(pool->vacants);
	pool->vacants = NULL;
	pool->num_vacants = 0;
	pool->num_blocks = 0;
}

int granite_pool_grow(granite_pool_t *pool)
{
	size_t num_objects, total, i;
	char *objs;
	void **list;

	if (pool->num_blocks >= GRANITE_POOL_BLOCKS)
		return 0;
	num_objects = (size_t)64 << pool->num_blocks;
	total = ((size_t)64 << (pool->num_blocks + 1)) - 64;
	objs = (char *)granite_memalign_alloc(pool_block_align(pool->elem_align), num_objects * pool->elem_size);
	if (!objs)
		return 0;
	list = (void **)realloc(pool->vacants, total * sizeof(*list));
	if (!list)
	{
		granite_memalign_free(objs);
		return 0;
	}
	pool->vacants = list;
	for (i = 0; i < num_objects; i++)
		list[pool->num_vacants++] = objs + i * pool->elem_size;
	pool->blocks[pool->num_blocks++] = objs;
	return 1;
}

/* ---- any thread, lock-free ---- */

#define LF_IDX_BITS 18u
#define LF_IDX_MASK ((1u << LF_IDX_BITS) - 1u)

static unsigned lf_bsr32(uint32_t v)
{
#if defined(_MSC_VER)
	unsigned long r;
	_BitScanReverse(&r, v);
	return (unsigned)r;
#else
	return 31u - (unsigned)__builtin_clz(v);
#endif
}

static uint32_t lf_block_base(unsigned k)
{
	return 64u * ((1u << k) - 1u);
}

static uint32_t lf_block_cap(unsigned k)
{
	return 64u << k;
}

/* 1-based slot index -> block, and the index within it */
static unsigned lf_block_of(uint32_t idx, uint32_t *offset)
{
	uint32_t i = idx - 1;
	/* block k holds i in [64 * (2^k - 1), 64 * (2^(k+1) - 1)) */
	unsigned k = lf_bsr32((i >> 6) + 1);
	*offset = i - lf_block_base(k);
	return k;
}

static void *lf_slot(granite_lf_pool_t *pool, uint32_t idx)
{
	uint32_t off;
	unsigned k = lf_block_of(idx, &off);
	return (char *)retro_atomic_load_acquire_ptr(&pool->block_objs[k]) + (size_t)off * pool->elem_size;
}

static retro_atomic_int_t *lf_next_of(granite_lf_pool_t *pool, uint32_t idx)
{
	uint32_t off;
	unsigned k = lf_block_of(idx, &off);
	return &pool->block_next[k][off];
}

static int lf_pack(uint32_t tag, uint32_t idx)
{
	return (int)((tag << LF_IDX_BITS) | idx);
}

static uint32_t lf_pop(granite_lf_pool_t *pool)
{
	for (;;)
	{
		uint32_t h = (uint32_t)retro_atomic_load_acquire_int(&pool->head);
		uint32_t idx = h & LF_IDX_MASK;
		uint32_t next;
		if (!idx)
			return 0;
		next = (uint32_t)retro_atomic_load_relaxed_int(lf_next_of(pool, idx));
		if (retro_atomic_cas_int(&pool->head, (int)h, lf_pack((h >> LF_IDX_BITS) + 1, next)))
			return idx;
	}
}

/* Pushes first..last, already linked through next_of, in one CAS. */
static void lf_push_chain(granite_lf_pool_t *pool, uint32_t first, uint32_t last)
{
	for (;;)
	{
		uint32_t h = (uint32_t)retro_atomic_load_acquire_int(&pool->head);
		retro_atomic_store_relaxed_int(lf_next_of(pool, last), (int)(h & LF_IDX_MASK));
		if (retro_atomic_cas_int(&pool->head, (int)h, lf_pack((h >> LF_IDX_BITS) + 1, first)))
			return;
	}
}

#if defined(__GNUC__) || defined(__clang__)
#define LF_COLD __attribute__((noinline, cold))
#elif defined(_MSC_VER)
#define LF_COLD __declspec(noinline)
#else
#define LF_COLD
#endif

/* Adds a block, returns one of its slots and pushes the rest. */
static LF_COLD uint32_t lf_grow(granite_lf_pool_t *pool)
{
	unsigned k;
	uint32_t cap, first, i;
	void *objs;
	retro_atomic_int_t *next;

	if (retro_atomic_load_acquire_int(&pool->block_count) >= GRANITE_LF_POOL_BLOCKS)
		return 0;
	/* counted before the block is taken, so a thread that sees every
	 * block taken also sees this grow in flight */
	retro_atomic_fetch_add_int(&pool->growing, 1);
	k = (unsigned)retro_atomic_fetch_add_int(&pool->block_count, 1);
	if (k >= GRANITE_LF_POOL_BLOCKS)
	{
		retro_atomic_fetch_add_int(&pool->growing, -1);
		return 0;
	}

	cap = lf_block_cap(k);
	objs = granite_memalign_alloc(pool_block_align(pool->elem_align), cap * pool->elem_size);
	if (!objs)
	{
		retro_atomic_fetch_add_int(&pool->growing, -1);
		return 0;
	}
	next = (retro_atomic_int_t *)malloc(cap * sizeof(*next));
	if (!next)
	{
		granite_memalign_free(objs);
		retro_atomic_fetch_add_int(&pool->growing, -1);
		return 0;
	}

	first = lf_block_base(k) + 1;
	for (i = 0; i + 1 < cap; i++)
		retro_atomic_int_init(&next[i], (int)(first + i + 1));
	retro_atomic_int_init(&next[cap - 1], 0);

	pool->block_next[k] = next;
	retro_atomic_store_release_ptr(&pool->block_objs[k], objs);

	if (cap > 1)
		lf_push_chain(pool, first + 1, first + cap - 1);
	retro_atomic_fetch_add_int(&pool->growing, -1);
	return first;
}

static uint32_t lf_index_of(granite_lf_pool_t *pool, void *ptr)
{
	unsigned n = (unsigned)retro_atomic_load_acquire_int(&pool->block_count);
	unsigned k;
	if (n > GRANITE_LF_POOL_BLOCKS)
		n = GRANITE_LF_POOL_BLOCKS;
	for (k = 0; k < n; k++)
	{
		char *objs = (char *)retro_atomic_load_acquire_ptr(&pool->block_objs[k]);
		uintptr_t at = (uintptr_t)ptr - (uintptr_t)objs;
		if (objs && (char *)ptr >= objs && at < (uintptr_t)lf_block_cap(k) * pool->elem_size)
			return lf_block_base(k) + (uint32_t)(((uint64_t)at >> pool->div_shift) * pool->div_inverse) + 1;
	}
	return 0; /* not ours; cannot happen for a slot this pool handed out */
}

void granite_lf_pool_init(granite_lf_pool_t *pool, size_t elem_size, size_t elem_align)
{
	uint64_t odd = elem_size;
	uint64_t inv;
	unsigned shift = 0, k;

	while (!(odd & 1))
	{
		odd >>= 1;
		shift++;
	}
	/* Newton's iteration for the inverse of odd modulo 2^64: odd is its
	 * own inverse to 3 bits, and each step doubles them */
	inv = odd;
	for (k = 0; k < 5; k++)
		inv *= 2 - odd * inv;

	pool->elem_size = elem_size;
	pool->elem_align = elem_align;
	pool->div_inverse = inv;
	pool->div_shift = shift;
	retro_atomic_int_init(&pool->head, 0);
	retro_atomic_int_init(&pool->block_count, 0);
	retro_atomic_int_init(&pool->growing, 0);
	for (k = 0; k < GRANITE_LF_POOL_BLOCKS; k++)
	{
		retro_atomic_ptr_init(&pool->block_objs[k], NULL);
		pool->block_next[k] = NULL;
	}
}

void granite_lf_pool_clear(granite_lf_pool_t *pool)
{
	unsigned k;
	for (k = 0; k < GRANITE_LF_POOL_BLOCKS; k++)
	{
		void *objs = retro_atomic_load_acquire_ptr(&pool->block_objs[k]);
		if (objs)
			granite_memalign_free(objs);
		free(pool->block_next[k]);
		pool->block_next[k] = NULL;
		retro_atomic_ptr_init(&pool->block_objs[k], NULL);
	}
	retro_atomic_int_init(&pool->head, 0);
	retro_atomic_int_init(&pool->block_count, 0);
	retro_atomic_int_init(&pool->growing, 0);
}

void *granite_lf_pool_alloc(granite_lf_pool_t *pool)
{
	uint32_t idx = lf_pop(pool);
	if (!idx)
		idx = lf_grow(pool);
	/* every block taken: other grows may still be pushing their slots. No
	 * new grow can start now, so once none is in flight before a pop that
	 * finds the list empty, the pool is truly full */
	while (!idx)
	{
		const int growing = retro_atomic_load_acquire_int(&pool->growing);
		idx = lf_pop(pool);
		if (!idx && !growing)
			return NULL;
	}
	return lf_slot(pool, idx);
}

void granite_lf_pool_release(granite_lf_pool_t *pool, void *slot)
{
	uint32_t idx = lf_index_of(pool, slot);
	if (idx)
		lf_push_chain(pool, idx, idx);
}
