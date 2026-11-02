/* parallel-rdp's slot pools (util/pool.c), for slot sizes that are not
 * powers of two as well as those that are.
 *
 * The lock-free pool finds a slot's index from its address by an exact
 * division done as a shift and a multiply: every slot of every block,
 * once released, must come back from a later alloc, no slot may be handed
 * out twice, and slots must be aligned and never overlap. Several
 * threads allocating and releasing at once must never hold the same slot.
 * The single-threaded pool must hand out distinct, aligned slots across
 * block growth and take every one back. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <boolean.h>
#include <retro_atomic.h>
#include <rthreads/rthreads.h>

#include "pool.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_granite_pool: FAIL: %s (size %u)\n", what, (unsigned)size); \
			failures++; \
		} \
	} while (0)

/* blocks of 64, 128, 256 and 512 slots, and into the fifth */
#define SLOTS 1000

static void *slots[SLOTS], *again[SLOTS];

static int cmp_ptr(const void *a, const void *b)
{
	const char *x = *(char *const *)a, *y = *(char *const *)b;
	return x < y ? -1 : x > y;
}

static void check_set(void **set, size_t size, size_t align)
{
	unsigned i;
	memcpy(again, set, sizeof(again));
	qsort(again, SLOTS, sizeof(*again), cmp_ptr);
	for (i = 0; i < SLOTS; i++)
	{
		CHECK(again[i] != NULL, "out of slots");
		CHECK(((size_t)(char *)again[i] & (align - 1)) == 0, "slot not aligned");
		if (i)
			CHECK((size_t)((char *)again[i] - (char *)again[i - 1]) >= size, "slots overlap or repeat");
	}
}

static void test_lf_pool(size_t size, size_t align)
{
	granite_lf_pool_t pool;
	unsigned i, round;

	granite_lf_pool_init(&pool, size, align);
	for (i = 0; i < SLOTS; i++)
	{
		slots[i] = granite_lf_pool_alloc(&pool);
		if (slots[i])
			memset(slots[i], (int)(i & 0xff), size);
	}
	check_set(slots, size, align);
	for (i = 0; i < SLOTS; i++)
	{
		unsigned j;
		const unsigned char *p = (const unsigned char *)slots[i];
		for (j = 0; p && j < size; j++)
			if (p[j] != (i & 0xff))
			{
				CHECK(0, "a slot was written through another");
				break;
			}
	}

	/* everything released comes back, and nothing new is made */
	for (round = 0; round < 3; round++)
	{
		for (i = 0; i < SLOTS; i++)
			granite_lf_pool_release(&pool, slots[(i * 7 + round) % SLOTS]);
		for (i = 0; i < SLOTS; i++)
			again[i] = granite_lf_pool_alloc(&pool);
		qsort(again, SLOTS, sizeof(*again), cmp_ptr);
		qsort(slots, SLOTS, sizeof(*slots), cmp_ptr);
		CHECK(memcmp(again, slots, sizeof(slots)) == 0, "released slots did not all come back");
	}
	CHECK(retro_atomic_load_acquire_int(&pool.block_count) == 5, "a released slot was lost, and a block added");
	granite_lf_pool_clear(&pool);
}

static void test_pool(size_t size, size_t align)
{
	granite_pool_t pool;
	unsigned i;

	granite_pool_init(&pool, size, align);
	for (i = 0; i < SLOTS; i++)
		slots[i] = granite_pool_alloc(&pool);
	check_set(slots, size, align);
	for (i = 0; i < SLOTS; i++)
		granite_pool_release(&pool, slots[i]);
	/* last in, first out */
	for (i = SLOTS; i-- > 0;)
		CHECK(granite_pool_alloc(&pool) == slots[i], "release order not kept");
	CHECK(pool.num_blocks == 5, "a block too many");
	granite_pool_clear(&pool);
}

/* ---- several threads at once ---- */

#define THREADS 6
#define OPS     20000
#define HOLD    8

static granite_lf_pool_t shared;
static retro_atomic_int_t collisions;

typedef struct worker
{
	unsigned id;
	unsigned seed;
} worker_t;

static void worker_run(void *data)
{
	worker_t *w = (worker_t *)data;
	unsigned *held[HOLD];
	unsigned n, i, k;

	for (i = 0; i < HOLD; i++)
		held[i] = NULL;
	for (n = 0; n < OPS; n++)
	{
		w->seed = w->seed * 1103515245u + 12345u;
		k = (w->seed >> 16) % HOLD;
		if (held[k])
		{
			/* still ours: nobody else was handed it meanwhile */
			if (held[k][0] != w->id || held[k][1] != held[k][2])
				retro_atomic_fetch_add_int(&collisions, 1);
			held[k][0] = 0;
			granite_lf_pool_release(&shared, held[k]);
			held[k] = NULL;
		}
		else
		{
			held[k] = (unsigned *)granite_lf_pool_alloc(&shared);
			if (!held[k])
				continue;
			held[k][0] = w->id;
			held[k][1] = n;
			held[k][2] = n;
		}
	}
	for (i = 0; i < HOLD; i++)
		if (held[i])
		{
			if (held[i][0] != w->id)
				retro_atomic_fetch_add_int(&collisions, 1);
			granite_lf_pool_release(&shared, held[i]);
		}
}

static void test_lf_pool_threads(void)
{
	/* 12 bytes: three words, not a power of two */
	size_t size = 3 * sizeof(unsigned);
	worker_t workers[THREADS];
	sthread_t *threads[THREADS];
	unsigned i;

	granite_lf_pool_init(&shared, size, sizeof(unsigned));
	retro_atomic_int_init(&collisions, 0);
	for (i = 0; i < THREADS; i++)
	{
		workers[i].id = i + 1;
		workers[i].seed = 0x9e3779b9u * (i + 1);
		threads[i] = sthread_create(worker_run, &workers[i]);
	}
	for (i = 0; i < THREADS; i++)
		if (threads[i])
			sthread_join(threads[i]);
		else
			worker_run(&workers[i]);
	CHECK(retro_atomic_load_acquire_int(&collisions) == 0, "two threads held one slot");
	granite_lf_pool_clear(&shared);
}

int main(void)
{
	static const size_t sizes[] = { 1, 4, 12, 24, 40, 56, 64, 72, 200, 4096 + 16 };
	static const size_t aligns[] = { 1, 4, 4, 8, 8, 8, 64, 8, 8, 16 };
	unsigned i;

	for (i = 0; i < sizeof(sizes) / sizeof(*sizes); i++)
	{
		test_lf_pool(sizes[i], aligns[i]);
		test_pool(sizes[i], aligns[i]);
	}
	test_lf_pool_threads();
	if (failures)
		return 1;
	printf("test_granite_pool: ok\n");
	return 0;
}
