/* parallel-rdp's saved pipeline cache and pipeline record table
 * (vulkan/pipeline_cache.c), through a stand-in device table.
 *
 * A saved cache is the driver's data behind the driver's UUID and a
 * hash of the data. It is handed to the driver only when both match;
 * otherwise the driver starts from an empty cache. What is saved for a
 * cache reads back as that cache's data.
 *
 * The record table hands each pipeline built a record of its own, from
 * any thread, until it is full. A record holds what the pipeline was
 * compiled from: applied to a blank compile state it gives that state
 * again, for compute and for graphics pipelines. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <boolean.h>
#include <rthreads/rthreads.h>

#include "pipeline_cache.h"
#include "frame_garbage.h" /* GRANITE_VK_TO_BITS */

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_vk_pipeline_cache: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

#define DEV ((VkDevice)(size_t)1)
#define CACHE(n) GRANITE_VK_FROM_BITS(VkPipelineCache, 0x100 + (n))
#define HEADER GRANITE_PIPELINE_CACHE_HEADER_SIZE

/* ---- the stand-in driver ---- */
static unsigned caches_created, caches_destroyed;
static VkPipelineCache destroyed_cache;
static size_t created_size;
static uint8_t created_data[256];
static bool created_with_data, fail_create, fail_get;
static uint8_t driver_data[64];
static size_t driver_size;

static VkResult VKAPI_CALL fake_create(VkDevice d, const VkPipelineCacheCreateInfo *info,
		const VkAllocationCallbacks *a, VkPipelineCache *cache)
{
	(void)a;
	CHECK(d == DEV && info->sType == VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO && info->pNext == NULL &&
			info->flags == 0, "a pipeline cache, on the device given");
	if (fail_create)
	{
		*cache = CACHE(0x99);
		return VK_ERROR_OUT_OF_HOST_MEMORY;
	}
	created_size = info->initialDataSize;
	created_with_data = info->pInitialData != NULL;
	if (info->pInitialData && info->initialDataSize <= sizeof(created_data))
		memcpy(created_data, info->pInitialData, info->initialDataSize);
	caches_created++;
	*cache = CACHE(caches_created);
	return VK_SUCCESS;
}

static void VKAPI_CALL fake_destroy(VkDevice d, VkPipelineCache cache, const VkAllocationCallbacks *a)
{
	(void)d; (void)a;
	caches_destroyed++;
	destroyed_cache = cache;
}

static VkResult VKAPI_CALL fake_get_data(VkDevice d, VkPipelineCache cache, size_t *size, void *data)
{
	(void)d;
	CHECK(cache != VK_NULL_HANDLE, "of a cache");
	if (fail_get)
		return VK_ERROR_OUT_OF_HOST_MEMORY;
	if (!data)
	{
		*size = driver_size;
		return VK_SUCCESS;
	}
	if (*size < driver_size)
		return VK_INCOMPLETE;
	memcpy(data, driver_data, driver_size);
	*size = driver_size;
	return VK_SUCCESS;
}

static struct VolkDeviceTable table;
static const uint8_t uuid[VK_UUID_SIZE] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };

static void test_blob(void)
{
	uint8_t blob[128];
	uint8_t other[VK_UUID_SIZE];
	const uint8_t *payload;
	size_t payload_size, size;
	uint64_t hash;
	VkPipelineCache cache = VK_NULL_HANDLE;
	unsigned i;

	/* the hash is the one the files on disk were written with */
	CHECK(granite_pipeline_cache_hash(NULL, 0) == (((uint64_t)0xcbf29ce4u << 32) | 0x84222325u), "the hash of nothing");
	CHECK(granite_pipeline_cache_hash((const uint8_t *)"a", 1) == (((uint64_t)0xaf63bd4cu << 32) | 0x8601b7beu),
			"the hash of a byte");
	CHECK(granite_pipeline_cache_hash((const uint8_t *)"ab", 2) != granite_pipeline_cache_hash((const uint8_t *)"ba", 2),
			"order matters");

	/* what the driver has to save */
	for (i = 0; i < 40; i++)
		driver_data[i] = (uint8_t)(0xa0 + i);
	driver_size = 40;

	CHECK(granite_pipeline_cache_blob_size(DEV, &table, VK_NULL_HANDLE) == 0, "no cache, nothing to save");
	CHECK(granite_pipeline_cache_init(DEV, &table, uuid, NULL, 0, &cache) && cache == CACHE(1) && !created_with_data &&
			created_size == 0 && caches_destroyed == 0, "without a saved cache the driver starts from nothing");

	size = granite_pipeline_cache_blob_size(DEV, &table, cache);
	CHECK(size == 40 + HEADER, "a cache is saved as its data and a header");
	memset(blob, 0xee, sizeof(blob));
	CHECK(!granite_pipeline_cache_blob(DEV, &table, cache, uuid, blob, HEADER - 1), "no room for the header: not saved");
	CHECK(!granite_pipeline_cache_blob(DEV, &table, cache, uuid, blob, size - 1) && blob[0] == 0xee,
			"no room for the data: not saved, nothing written in front");
	CHECK(!granite_pipeline_cache_blob(DEV, &table, VK_NULL_HANDLE, uuid, blob, size), "no cache: not saved");
	CHECK(granite_pipeline_cache_blob(DEV, &table, cache, uuid, blob, size), "saved");
	CHECK(memcmp(blob, uuid, VK_UUID_SIZE) == 0, "the driver's UUID first");
	memcpy(&hash, blob + VK_UUID_SIZE, sizeof(hash));
	CHECK(hash == granite_pipeline_cache_hash(driver_data, 40), "then the hash of the data");
	CHECK(memcmp(blob + HEADER, driver_data, 40) == 0 && blob[size] == 0xee, "then the data, and nothing after");

	/* more room than the driver needs */
	memset(blob, 0xee, sizeof(blob));
	CHECK(granite_pipeline_cache_blob(DEV, &table, cache, uuid, blob, sizeof(blob)), "saved with room to spare");
	memcpy(&hash, blob + VK_UUID_SIZE, sizeof(hash));
	CHECK(hash == granite_pipeline_cache_hash(driver_data, 40), "the hash is of what the driver wrote");

	/* reading it back */
	payload = granite_pipeline_cache_payload(uuid, blob, size, &payload_size);
	CHECK(payload == blob + HEADER && payload_size == 40, "what was saved reads back as the driver's data");
	CHECK(granite_pipeline_cache_init(DEV, &table, uuid, blob, size, &cache) && cache == CACHE(2) && created_with_data &&
			created_size == 40 && memcmp(created_data, driver_data, 40) == 0, "which the next cache is made from");
	CHECK(caches_destroyed == 1 && destroyed_cache == CACHE(1), "the cache it replaces is destroyed");

	/* blobs that are not handed to the driver */
	CHECK(granite_pipeline_cache_payload(uuid, NULL, size, &payload_size) == NULL && payload_size == 0, "no blob");
	payload_size = 7;
	CHECK(granite_pipeline_cache_payload(uuid, blob, HEADER - 1, &payload_size) == NULL && payload_size == 0,
			"one too short for a header");
	memcpy(other, uuid, sizeof(other));
	other[15] ^= 1;
	payload_size = 7;
	CHECK(granite_pipeline_cache_payload(other, blob, size, &payload_size) == NULL && payload_size == 0,
			"another driver's");
	blob[HEADER + 13] ^= 0x10;
	payload_size = 7;
	CHECK(granite_pipeline_cache_payload(uuid, blob, size, &payload_size) == NULL && payload_size == 0, "a damaged one");
	CHECK(granite_pipeline_cache_init(DEV, &table, uuid, blob, size, &cache) && cache == CACHE(3) && !created_with_data &&
			created_size == 0, "from a damaged blob the driver starts from nothing");
	blob[HEADER + 13] ^= 0x10;
	CHECK(granite_pipeline_cache_payload(uuid, blob, size - 1, &payload_size) == NULL, "a truncated one");
	blob[VK_UUID_SIZE] ^= 1;
	CHECK(granite_pipeline_cache_payload(uuid, blob, size, &payload_size) == NULL, "one with a damaged hash");
	blob[VK_UUID_SIZE] ^= 1;

	/* a header and no data */
	hash = granite_pipeline_cache_hash(NULL, 0);
	memcpy(blob + VK_UUID_SIZE, &hash, sizeof(hash));
	payload = granite_pipeline_cache_payload(uuid, blob, HEADER, &payload_size);
	CHECK(payload == blob + HEADER && payload_size == 0, "an empty cache reads back as empty");

	/* a driver that fails */
	fail_get = true;
	CHECK(granite_pipeline_cache_blob_size(DEV, &table, cache) == 0 &&
			!granite_pipeline_cache_blob(DEV, &table, cache, uuid, blob, sizeof(blob)),
			"a driver that gives no data: nothing to save");
	fail_get = false;
	fail_create = true;
	CHECK(!granite_pipeline_cache_init(DEV, &table, uuid, NULL, 0, &cache) && cache == VK_NULL_HANDLE &&
			caches_destroyed == 3, "a driver that makes no cache: the old one is gone and there is none");
	fail_create = false;
}

/* ---- the record table ---- */
#define THREADS 4
#define PER_THREAD 1500
static granite_pipeline_record_table_t shared_table;
static unsigned thread_claimed[THREADS];

static void claim_thread(void *arg)
{
	unsigned index = (unsigned)(size_t)arg;
	unsigned i;

	for (i = 0; i < PER_THREAD; i++)
	{
		pipeline_record_t *record = granite_pipeline_record_table_claim(&shared_table);
		if (!record)
			continue;
		/* nobody else has this one */
		if (record->pipeline_hash != 0)
			record->kind = 0xbad;
		record->pipeline_hash = ((uint64_t)(index + 1) << 32) | (i + 1);
		granite_pipeline_record_table_commit(&shared_table);
		thread_claimed[index]++;
	}
}

static void test_table(void)
{
	granite_pipeline_record_table_t t;
	const pipeline_record_t *records;
	pipeline_record_t *a, *b;
	sthread_t *threads[THREADS];
	unsigned i, total = 0, bad = 0;

	CHECK(granite_pipeline_record_table_init(&t) && t.records, "a table comes up");
	CHECK(granite_pipeline_record_table_get(&t, &records) == 0 && records == t.records, "empty");
	a = granite_pipeline_record_table_claim(&t);
	b = granite_pipeline_record_table_claim(&t);
	CHECK(a == &t.records[0] && b == &t.records[1], "records are handed out in order");
	CHECK(a->pipeline_hash == 0 && a->kind == 0 && a->shader_hashes[1] == 0, "blank");
	CHECK(granite_pipeline_record_table_get(&t, &records) == 0, "a record being filled in is not counted yet");
	granite_pipeline_record_table_commit(&t);
	CHECK(granite_pipeline_record_table_get(&t, &records) == 1, "one written");
	granite_pipeline_record_table_commit(&t);
	CHECK(granite_pipeline_record_table_get(&t, &records) == 2 && records == t.records, "two");
	for (i = 2; i < GRANITE_PIPELINE_RECORDS; i++)
	{
		if (!granite_pipeline_record_table_claim(&t))
			break;
		granite_pipeline_record_table_commit(&t);
	}
	CHECK(i == GRANITE_PIPELINE_RECORDS && granite_pipeline_record_table_get(&t, &records) == GRANITE_PIPELINE_RECORDS,
			"the table fills up");
	CHECK(granite_pipeline_record_table_claim(&t) == NULL && granite_pipeline_record_table_claim(&t) == NULL,
			"a full table hands out nothing");
	CHECK(granite_pipeline_record_table_get(&t, &records) == GRANITE_PIPELINE_RECORDS, "and counts no more");
	granite_pipeline_record_table_free(&t);
	CHECK(t.records == NULL && granite_pipeline_record_table_claim(&t) == NULL &&
			granite_pipeline_record_table_get(&t, &records) == 0, "a freed table records nothing");

	/* a table that never came up */
	granite_pipeline_record_table_init(&t);
	granite_pipeline_record_table_free(&t);
	CHECK(granite_pipeline_record_table_claim(&t) == NULL && granite_pipeline_record_table_claim(&t) == NULL,
			"a table without records hands out nothing, however often asked");

	/* from several threads: more asked for than there are */
	granite_pipeline_record_table_init(&shared_table);
	for (i = 0; i < THREADS; i++)
		threads[i] = sthread_create(claim_thread, (void *)(size_t)i);
	for (i = 0; i < THREADS; i++)
		sthread_join(threads[i]);
	for (i = 0; i < THREADS; i++)
		total += thread_claimed[i];
	CHECK(total == GRANITE_PIPELINE_RECORDS, "threads between them get every record, and no more");
	CHECK(granite_pipeline_record_table_get(&shared_table, &records) == GRANITE_PIPELINE_RECORDS, "all written");
	for (i = 0; i < GRANITE_PIPELINE_RECORDS; i++)
		if (records[i].pipeline_hash == 0 || records[i].kind == 0xbad)
			bad++;
	CHECK(bad == 0, "each record went to one thread");
	granite_pipeline_record_table_free(&shared_table);
}

/* ---- records ---- */
static void fill_compile(granite_pipeline_compile_t *c)
{
	unsigned i;

	memset(c, 0, sizeof(*c));
	c->hash = ((uint64_t)0x11223344u << 32) | 0x55667788u;
	for (i = 0; i < 4; i++)
		c->static_state.words[i] = 0x1000 + i;
	for (i = 0; i < GRANITE_NUM_TOTAL_SPEC_CONSTANTS; i++)
		c->potential_static_state.spec_constants[i] = 0x2000 + i;
	c->potential_static_state.spec_constant_mask = 0xa5;
	c->potential_static_state.internal_spec_constant_mask = 0x0c;
	for (i = 0; i < 4; i++)
		c->potential_static_state.blend_constants[i] = 0.25f * (float)(i + 1);
	c->subgroup_size_tag = 0x0804;
	c->subpass_index = 0;
	for (i = 0; i < GRANITE_NUM_VERTEX_ATTRIBS; i++)
	{
		c->attribs[i].binding = i & 3;
		c->attribs[i].format = (VkFormat)(VK_FORMAT_R8_UNORM + i);
		c->attribs[i].offset = 4 * i;
	}
	for (i = 0; i < GRANITE_NUM_VERTEX_BUFFERS; i++)
	{
		c->strides[i] = 16 + i;
		c->input_rates[i] = (i & 1) ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
	}
	c->cache = CACHE(50);
}

static void test_records(void)
{
	granite_pipeline_compile_t compile, back, expect;
	granite_pipeline_record_ids_t ids;
	pipeline_record_pass_t pass;
	pipeline_record_t record;
	unsigned i;
	uint32_t bits;

	fill_compile(&compile);
	memset(&pass, 0, sizeof(pass));
	pass.num_color = 1;
	pass.formats[0] = VK_FORMAT_R8G8B8A8_UNORM;
	pass.samples[0] = 1;
	pass.depth_format = VK_FORMAT_D16_UNORM;
	pass.recordable = 1;
	memset(&ids, 0, sizeof(ids));
	ids.program_hash = 0xaaaa;
	ids.shader_hashes[0] = 0xbbbb;
	ids.shader_hashes[1] = 0xcccc;
	ids.render_pass_hash = 0xdddd;
	ids.pass = &pass;

	/* graphics */
	memset(&record, 0, sizeof(record));
	granite_pipeline_record_fill(&record, PIPELINE_RECORD_GRAPHICS, &compile, &ids);
	CHECK(record.kind == PIPELINE_RECORD_GRAPHICS && record.pipeline_hash == compile.hash &&
			record.program_hash == 0xaaaa && record.shader_hashes[0] == 0xbbbb && record.shader_hashes[1] == 0xcccc &&
			record.render_pass_hash == 0xdddd, "a graphics record names its pipeline, program, shaders and pass");
	CHECK(record.static_words[0] == 0x1000 && record.static_words[3] == 0x1003 && record.spec_constants[0] == 0x2000 &&
			record.spec_constants[GRANITE_NUM_TOTAL_SPEC_CONSTANTS - 1] == 0x2000 + GRANITE_NUM_TOTAL_SPEC_CONSTANTS - 1,
			"its static state and specialization constants");
	CHECK(record.spec_masks == (0xa5 | (0x0c << 8)) && record.subgroup_size_tag == 0x0804 && record.subpass_index == 0,
			"the masks packed, the subgroup sizes, the subpass");
	memcpy(&bits, &compile.potential_static_state.blend_constants[2], sizeof(bits));
	CHECK(record.blend_constants[2] == bits, "the blend constants, bit for bit");
	CHECK(record.attribs[5][0] == 1 && record.attribs[5][1] == (uint32_t)(VK_FORMAT_R8_UNORM + 5) &&
			record.attribs[5][2] == 20 && record.strides[3] == 19 &&
			record.input_rates[1] == VK_VERTEX_INPUT_RATE_INSTANCE && record.input_rates[2] == VK_VERTEX_INPUT_RATE_VERTEX,
			"the vertex layout");
	CHECK(memcmp(&record.pass, &pass, sizeof(pass)) == 0, "and what its pass is made from");

	memset(&back, 0xff, sizeof(back));
	granite_pipeline_record_apply(&record, &back, CACHE(7));
	expect = compile;
	expect.hash = 0;
	expect.cache = CACHE(7);
	CHECK(back.hash == 0 && back.cache == CACHE(7), "applied: no hash yet, the cache to build into");
	CHECK(memcmp(&back.static_state, &expect.static_state, sizeof(back.static_state)) == 0 &&
			memcmp(back.potential_static_state.spec_constants, expect.potential_static_state.spec_constants,
				sizeof(back.potential_static_state.spec_constants)) == 0 &&
			back.potential_static_state.spec_constant_mask == 0xa5 &&
			back.potential_static_state.internal_spec_constant_mask == 0x0c &&
			memcmp(back.potential_static_state.blend_constants, expect.potential_static_state.blend_constants,
				sizeof(back.potential_static_state.blend_constants)) == 0 &&
			back.subgroup_size_tag == 0x0804 && back.subpass_index == 0, "the state it was compiled from");
	for (i = 0; i < GRANITE_NUM_VERTEX_ATTRIBS; i++)
		CHECK(back.attribs[i].binding == expect.attribs[i].binding && back.attribs[i].format == expect.attribs[i].format &&
				back.attribs[i].offset == expect.attribs[i].offset, "each vertex attribute");
	for (i = 0; i < GRANITE_NUM_VERTEX_BUFFERS; i++)
		CHECK(back.strides[i] == expect.strides[i] && back.input_rates[i] == expect.input_rates[i], "each vertex buffer");

	/* compute: only what a compute pipeline has */
	memset(&record, 0, sizeof(record));
	granite_pipeline_record_fill(&record, PIPELINE_RECORD_COMPUTE, &compile, &ids);
	CHECK(record.kind == PIPELINE_RECORD_COMPUTE && record.pipeline_hash == compile.hash &&
			record.program_hash == 0xaaaa && record.shader_hashes[0] == 0xbbbb, "a compute record names its shader");
	CHECK(record.shader_hashes[1] == 0 && record.render_pass_hash == 0 && record.attribs[5][2] == 0 &&
			record.strides[3] == 0 && record.blend_constants[2] == 0 && record.pass.num_color == 0,
			"and has no second shader, pass or vertex layout");
	CHECK(record.static_words[2] == 0x1002 && record.spec_masks == (0xa5 | (0x0c << 8)) &&
			record.subgroup_size_tag == 0x0804, "but the state and constants");

	memset(&back, 0xff, sizeof(back));
	granite_pipeline_record_apply(&record, &back, CACHE(8));
	CHECK(back.cache == CACHE(8) && back.static_state.words[2] == 0x1002 &&
			back.potential_static_state.spec_constants[3] == 0x2003 && back.subgroup_size_tag == 0x0804,
			"applied: the state and constants");
	CHECK(back.attribs[5].offset == 0 && back.strides[3] == 0 && back.potential_static_state.blend_constants[2] == 0.0f &&
			back.subpass_index == 0 && back.hash == 0, "and nothing else");
}

int main(void)
{
	memset(&table, 0, sizeof(table));
	table.vkCreatePipelineCache = fake_create;
	table.vkDestroyPipelineCache = fake_destroy;
	table.vkGetPipelineCacheData = fake_get_data;

	test_blob();
	test_table();
	test_records();

	if (failures)
	{
		fprintf(stderr, "test_vk_pipeline_cache: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_vk_pipeline_cache: OK\n");
	return 0;
}
