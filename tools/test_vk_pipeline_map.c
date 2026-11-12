/* parallel-rdp's per-program pipeline map (vulkan/pipeline_map.c).
 *
 * A pipeline added for a hash is found by it, with its dynamic state; a
 * hash never added finds none. Adding for a hash that has one returns
 * the one there and leaves it there: the newcomer is the caller's to
 * destroy. Hashes that fall in one bucket are kept apart. Freeing calls
 * back once for each pipeline in the map and for no other, and leaves the
 * map empty and usable.
 *
 * From threads: several add the same hashes at once, with no lock. For
 * each hash exactly one of them wins, all are handed the winner's
 * pipeline, a lookup from any thread finds it from then on, and the map
 * ends with one pipeline a hash. */
#include <stdio.h>
#include <string.h>
#include <boolean.h>
#include <rthreads/rthreads.h>

#include "pipeline_map.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_vk_pipeline_map: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

#define HANDLE(x) ((VkPipeline)(size_t)(x))

static unsigned destroyed;
static size_t destroyed_sum;

static void count_destroy(void *userdata, const granite_pipeline_t *pipeline)
{
	CHECK(userdata == (void *)&destroyed, "the callback gets its userdata");
	destroyed++;
	destroyed_sum += (size_t)pipeline->pipeline;
}

static void test_single(void)
{
	granite_pipeline_map_t map;
	granite_pipeline_t p, r;
	unsigned i;

	granite_pipeline_map_init(&map);
	r = granite_pipeline_map_find(&map, 5);
	CHECK(r.pipeline == VK_NULL_HANDLE && r.dynamic_mask == 0, "an empty map finds nothing");

	p.pipeline = HANDLE(0x10);
	p.dynamic_mask = 3;
	r = granite_pipeline_map_add(&map, 5, &p);
	CHECK(r.pipeline == HANDLE(0x10) && r.dynamic_mask == 3, "the first for a hash is the map's");
	r = granite_pipeline_map_find(&map, 5);
	CHECK(r.pipeline == HANDLE(0x10) && r.dynamic_mask == 3, "and is found");

	p.pipeline = HANDLE(0x20);
	p.dynamic_mask = 9;
	r = granite_pipeline_map_add(&map, 5, &p);
	CHECK(r.pipeline == HANDLE(0x10) && r.dynamic_mask == 3, "a second for the hash yields to the first");
	r = granite_pipeline_map_find(&map, 5);
	CHECK(r.pipeline == HANDLE(0x10), "which stays");

	/* the same bucket, and hashes that differ in the high word alone */
	for (i = 1; i <= 8; i++)
	{
		uint64_t hash = 5 + (uint64_t)i * GRANITE_PIPELINE_MAP_BUCKETS;
		p.pipeline = HANDLE(0x100 + i);
		p.dynamic_mask = i;
		r = granite_pipeline_map_add(&map, hash, &p);
		CHECK(r.pipeline == HANDLE(0x100 + i), "another hash in the bucket is added");
	}
	p.pipeline = HANDLE(0x300);
	p.dynamic_mask = 0;
	r = granite_pipeline_map_add(&map, ((uint64_t)1 << 40) + 5, &p);
	CHECK(r.pipeline == HANDLE(0x300), "a hash that differs in its high word is another hash");
	for (i = 1; i <= 8; i++)
	{
		r = granite_pipeline_map_find(&map, 5 + (uint64_t)i * GRANITE_PIPELINE_MAP_BUCKETS);
		CHECK(r.pipeline == HANDLE(0x100 + i) && r.dynamic_mask == i, "each is found by its own hash");
	}
	r = granite_pipeline_map_find(&map, 5);
	CHECK(r.pipeline == HANDLE(0x10), "the first still by its");
	r = granite_pipeline_map_find(&map, 5 + 9 * GRANITE_PIPELINE_MAP_BUCKETS);
	CHECK(r.pipeline == VK_NULL_HANDLE, "a hash of the bucket never added finds nothing");

	destroyed = 0;
	destroyed_sum = 0;
	granite_pipeline_map_free(&map, count_destroy, &destroyed);
	CHECK(destroyed == 10, "freeing calls back for each pipeline in the map");
	CHECK(destroyed_sum == 0x10 + 0x300 + 8 * 0x100 + 36, "those and no yielded one");
	r = granite_pipeline_map_find(&map, 5);
	CHECK(r.pipeline == VK_NULL_HANDLE, "the map is empty");
	granite_pipeline_map_free(&map, count_destroy, &destroyed);
	CHECK(destroyed == 10, "and freeing it again does nothing");

	p.pipeline = HANDLE(0x40);
	r = granite_pipeline_map_add(&map, 5, &p);
	CHECK(r.pipeline == HANDLE(0x40), "it takes pipelines again");
	granite_pipeline_map_free(&map, NULL, NULL);
	r = granite_pipeline_map_find(&map, 5);
	CHECK(r.pipeline == VK_NULL_HANDLE, "and frees without a callback");
}

#define THREADS 4
#define HASHES  20000

static granite_pipeline_map_t shared;
/* per thread: the hashes it won */
static unsigned char won[THREADS][HASHES];
static unsigned bad[THREADS];

static void adder(void *userdata)
{
	unsigned t = (unsigned)(size_t)userdata;
	unsigned i;

	for (i = 0; i < HASHES; i++)
	{
		/* few buckets' worth of spread, so chains are long and raced */
		uint64_t hash = (uint64_t)i * 7919u;
		granite_pipeline_t mine, got, found;

		mine.pipeline = HANDLE(((size_t)(t + 1) << 20) | i);
		mine.dynamic_mask = t + 1;
		got = granite_pipeline_map_add(&shared, hash, &mine);
		if (got.pipeline == mine.pipeline)
			won[t][i] = 1;
		/* whoever won, it is this hash's, whole */
		if (((size_t)got.pipeline & 0xfffff) != i || got.dynamic_mask != (uint32_t)((size_t)got.pipeline >> 20))
			bad[t]++;
		found = granite_pipeline_map_find(&shared, hash);
		if (found.pipeline != got.pipeline || found.dynamic_mask != got.dynamic_mask)
			bad[t]++;
	}
}

static void test_threads(void)
{
	sthread_t *threads[THREADS];
	unsigned t, i, winners, lost = 0;

	granite_pipeline_map_init(&shared);
	for (t = 0; t < THREADS; t++)
		threads[t] = sthread_create(adder, (void *)(size_t)t);
	for (t = 0; t < THREADS; t++)
		if (threads[t])
			sthread_join(threads[t]);
		else
			CHECK(0, "a thread starts");

	for (t = 0; t < THREADS; t++)
		CHECK(bad[t] == 0, "every thread is handed and finds the winner's pipeline");

	for (i = 0; i < HASHES; i++)
	{
		granite_pipeline_t r = granite_pipeline_map_find(&shared, (uint64_t)i * 7919u);

		winners = 0;
		for (t = 0; t < THREADS; t++)
			winners += won[t][i];
		if (winners != 1)
			lost++;
		else
		{
			for (t = 0; t < THREADS; t++)
				if (won[t][i] && r.pipeline != HANDLE(((size_t)(t + 1) << 20) | i))
					lost++;
		}
	}
	CHECK(lost == 0, "each hash has one winner, and its pipeline is the one in the map");

	destroyed = 0;
	destroyed_sum = 0;
	granite_pipeline_map_free(&shared, count_destroy, &destroyed);
	CHECK(destroyed == HASHES, "the map holds one pipeline a hash");
}

int main(void)
{
	test_single();
	test_threads();

	if (failures)
	{
		fprintf(stderr, "test_vk_pipeline_map: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_vk_pipeline_map: OK\n");
	return 0;
}
