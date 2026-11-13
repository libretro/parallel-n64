/* parallel-rdp's vertex and uniform block pool (vulkan/buffer_pool.c),
 * with stand-in buffers.
 *
 * A block hands out its bytes from the start, each piece aligned; a
 * piece of uniform data is bound with at least the spill size, as far
 * as the block goes. A block without room, or no block, hands out
 * nothing.
 *
 * A request is served from the blocks the pool keeps, mapped again and
 * rewound, or by a new buffer; one for more than the block size always
 * by a new buffer of that size. When no buffer can be made, or a kept
 * one cannot be mapped, there is no block.
 *
 * Giving a block back keeps it, up to the number the pool keeps;
 * beyond that, and for a block of an odd size, the buffer is released.
 * Exchanging a block sends what was written to the device; an untouched
 * block goes straight back to the pool, a used one waits in the frame's
 * list until that is recycled, an odd-sized one is released. Every
 * buffer made is released exactly once. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <boolean.h>

#include "buffer_pool.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_vk_buffer_pool: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

/* ---- stand-in buffers ---- */
typedef struct fake_buffer
{
	uint8_t *memory;
	VkDeviceSize size;
	VkBufferUsageFlags usage;
	unsigned maps, unmaps, releases;
} fake_buffer_t;

static fake_buffer_t buffers[512];
static unsigned num_buffers, live_buffers, total_maps, total_unmaps;
static bool fail_create, fail_map, create_unmapped;
static int user_token;

static void *fake_create(void *user, VkDeviceSize size, VkBufferUsageFlags usage, uint8_t **mapped)
{
	fake_buffer_t *b;
	CHECK(user == &user_token, "the owner's pointer comes back");
	if (fail_create || num_buffers == 512)
		return NULL;
	b = &buffers[num_buffers++];
	b->memory = (uint8_t *)malloc((size_t)size);
	b->size = size;
	b->usage = usage;
	live_buffers++;
	*mapped = create_unmapped ? NULL : b->memory;
	return b;
}

static void fake_release(void *user, void *handle)
{
	fake_buffer_t *b = (fake_buffer_t *)handle;
	(void)user;
	b->releases++;
	CHECK(b->releases == 1, "a buffer is released once");
	live_buffers--;
}

static uint8_t *fake_map(void *user, void *handle)
{
	fake_buffer_t *b = (fake_buffer_t *)handle;
	(void)user;
	if (fail_map)
		return NULL;
	b->maps++;
	total_maps++;
	return b->memory;
}

static void fake_unmap(void *user, void *handle)
{
	fake_buffer_t *b = (fake_buffer_t *)handle;
	(void)user;
	b->unmaps++;
	total_unmaps++;
}

static const granite_buffer_pool_ops_t ops = { fake_create, fake_release, fake_map, fake_unmap };

static void free_buffers(void)
{
	unsigned i;
	for (i = 0; i < num_buffers; i++)
	{
		CHECK(buffers[i].releases == 1, "every buffer made is released");
		free(buffers[i].memory);
	}
	memset(buffers, 0, sizeof(buffers));
	num_buffers = 0;
}

static void test_block(void)
{
	granite_buffer_pool_t pool;
	granite_buffer_block_t block, none;
	granite_buffer_block_allocation_t a;

	CHECK(granite_buffer_pool_init(&pool, &ops, &user_token, 1024, 16, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, 256, 4),
			"a pool comes up");
	granite_buffer_pool_request(&pool, 100, &block);
	CHECK(block.handle == &buffers[0] && block.mapped == buffers[0].memory && block.size == 1024 &&
			block.offset == 0 && block.alignment == 16 && block.spill_size == 256, "a block of the pool's size");
	CHECK(buffers[0].size == 1024 && buffers[0].usage == VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, "over a buffer of its usage");

	a = granite_buffer_block_allocate(&block, 100);
	CHECK(a.host == block.mapped && a.offset == 0 && a.padded_size == 256, "the first piece is at the start, spilled");
	a = granite_buffer_block_allocate(&block, 20);
	CHECK(a.host == block.mapped + 112 && a.offset == 112 && a.padded_size == 256 && block.offset == 132,
			"the next is aligned after it");
	a = granite_buffer_block_allocate(&block, 600);
	CHECK(a.offset == 144 && a.padded_size == 600, "a piece larger than the spill is bound as it is");
	a = granite_buffer_block_allocate(&block, 200);
	CHECK(a.offset == 752 && a.padded_size == 256, "spilled again");
	a = granite_buffer_block_allocate(&block, 40);
	CHECK(a.offset == 960 && a.padded_size == 64, "near the end the spill stops at the block's end");
	a = granite_buffer_block_allocate(&block, 40);
	CHECK(a.host == NULL && block.offset == 1000, "a piece that does not fit is not handed out");
	a = granite_buffer_block_allocate(&block, 16);
	CHECK(a.host != NULL && a.offset == 1008 && a.padded_size == 16 && block.offset == 1024, "one that just fits is");
	a = granite_buffer_block_allocate(&block, 1);
	CHECK(a.host == NULL, "a full block hands out nothing");
	a = granite_buffer_block_allocate(&block, 0);
	CHECK(a.host == block.mapped + 1024 && a.padded_size == 0, "but nothing still fits");

	memset(&none, 0, sizeof(none));
	a = granite_buffer_block_allocate(&none, 16);
	CHECK(a.host == NULL && a.offset == 0 && a.padded_size == 0, "no block hands out nothing");
	a = granite_buffer_block_allocate(&none, 0);
	CHECK(a.host == NULL, "not even nothing");

	/* (a huge request does not wrap) */
	granite_buffer_pool_recycle(&pool, &block);
	granite_buffer_pool_request(&pool, 16, &block);
	granite_buffer_block_allocate(&block, 16);
	a = granite_buffer_block_allocate(&block, ~(VkDeviceSize)0 - 8);
	CHECK(a.host == NULL && block.offset == 16, "a size that would wrap is refused");

	/* (a block that is not being written) */
	none = block;
	none.mapped = NULL;
	a = granite_buffer_block_allocate(&none, 16);
	CHECK(a.host == NULL && none.offset == 16, "a block that is not mapped hands out nothing");

	granite_buffer_pool_recycle(&pool, &block);
	granite_buffer_pool_free(&pool);
	CHECK(live_buffers == 0, "freeing the pool releases what it kept");
	free_buffers();
}

static void test_pool(void)
{
	granite_buffer_pool_t pool;
	granite_buffer_block_t block, b2, b3, big;
	granite_buffer_block_list_t used;
	void *first;
	unsigned i;

	memset(&used, 0, sizeof(used));
	total_maps = total_unmaps = 0;
	CHECK(granite_buffer_pool_init(&pool, &ops, &user_token, 4096, 16, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, 0, 2),
			"a pool that keeps two blocks");

	/* request, recycle, request again */
	granite_buffer_pool_request(&pool, 1, &block);
	first = block.handle;
	CHECK(num_buffers == 1 && block.spill_size == 0, "an empty pool makes a buffer");
	granite_buffer_block_allocate(&block, 100);
	granite_buffer_pool_recycle(&pool, &block);
	CHECK(block.handle == NULL && block.mapped == NULL && pool.count == 1 && live_buffers == 1,
			"given back, it is kept; the holder has no block");
	CHECK(pool.blocks[0].handle == first && pool.blocks[0].mapped == NULL, "kept unmapped");
	granite_buffer_pool_request(&pool, 4096, &block);
	CHECK(block.handle == first && num_buffers == 1 && block.offset == 0 && block.mapped == buffers[0].memory &&
			buffers[0].maps == 1 && pool.count == 0, "and handed out again, mapped and rewound");

	/* more than the block size */
	granite_buffer_pool_recycle(&pool, &block);
	granite_buffer_pool_request(&pool, 5000, &big);
	CHECK(num_buffers == 2 && big.size == 5000 && buffers[1].size == 5000 && pool.count == 1,
			"a larger request gets a buffer of its own size, kept blocks or not");
	granite_buffer_pool_recycle(&pool, &big);
	CHECK(big.handle == NULL && buffers[1].releases == 1 && pool.count == 1, "which is not kept");

	/* the pool keeps two */
	granite_buffer_pool_request(&pool, 1, &block);
	granite_buffer_pool_request(&pool, 1, &b2);
	granite_buffer_pool_request(&pool, 1, &b3);
	CHECK(num_buffers == 4 && live_buffers == 3, "three blocks out");
	granite_buffer_pool_recycle(&pool, &block);
	granite_buffer_pool_recycle(&pool, &b2);
	granite_buffer_pool_recycle(&pool, &b3);
	CHECK(pool.count == 2 && live_buffers == 2 && b3.handle == NULL, "two are kept, the third released");
	granite_buffer_pool_recycle(&pool, &b3);
	CHECK(pool.count == 2 && live_buffers == 2, "giving back no block does nothing");
	granite_buffer_pool_request(&pool, 1, &block);
	memset(&b3, 0, sizeof(b3));
	b3.size = 4096;
	granite_buffer_pool_recycle(&pool, &b3);
	CHECK(pool.count == 1, "a block without a buffer is not kept, whatever its size");
	granite_buffer_pool_recycle(&pool, &block);

	/* exchange: untouched */
	total_unmaps = 0;
	granite_buffer_pool_request(&pool, 1, &block);
	first = block.handle;
	granite_buffer_pool_exchange(&pool, &block, 0, &used);
	CHECK(block.handle == NULL && used.count == 0 && pool.count == 2 && total_unmaps == 1,
			"an untouched block goes back to the pool, unmapped");
	granite_buffer_pool_exchange(&pool, &block, 0, &used);
	CHECK(total_unmaps == 1 && block.handle == NULL, "exchanging no block for none does nothing");

	/* exchange: used, for another */
	granite_buffer_pool_exchange(&pool, &block, 64, &used);
	CHECK(block.handle == first && block.mapped != NULL, "exchanging nothing for a block gets one");
	granite_buffer_block_allocate(&block, 64);
	granite_buffer_pool_exchange(&pool, &block, 64, &used);
	CHECK(used.count == 1 && used.blocks[0].handle == first && used.blocks[0].mapped == NULL && total_unmaps == 2 &&
			block.handle != NULL && block.handle != first && block.offset == 0,
			"a used block waits in the frame's list; the holder gets another");
	CHECK(((fake_buffer_t *)first)->releases == 0, "it is not released: the GPU reads it");

	/* exchange: used, odd size */
	granite_buffer_block_allocate(&block, 64);
	granite_buffer_pool_exchange(&pool, &block, 10000, &used);
	CHECK(used.count == 2 && block.size == 10000, "a large request in exchange");
	granite_buffer_block_allocate(&block, 9000);
	i = live_buffers;
	granite_buffer_pool_exchange(&pool, &block, 0, &used);
	CHECK(used.count == 2 && live_buffers == i - 1 && block.handle == NULL,
			"a used block of an odd size is released, not listed");
	granite_buffer_pool_request(&pool, 10000, &block);
	i = live_buffers;
	granite_buffer_pool_exchange(&pool, &block, 0, &used);
	CHECK(live_buffers == i - 1 && used.count == 2, "an untouched one too");

	/* the frame comes round */
	CHECK(pool.count == 0, "(the pool is empty)");
	granite_buffer_block_list_recycle(&used, &pool);
	CHECK(used.count == 0 && pool.count == 2 && used.capacity != 0, "the frame's list goes back to the pool and keeps its room");
	granite_buffer_pool_request(&pool, 1, &block);
	CHECK(block.offset == 0 && block.mapped != NULL, "its blocks are handed out rewound");
	granite_buffer_pool_recycle(&pool, &block);

	/* many used blocks: the list grows, and beyond the pool's two they are released */
	for (i = 0; i < 40; i++)
	{
		granite_buffer_pool_exchange(&pool, &block, 8, &used);
		granite_buffer_block_allocate(&block, 8);
	}
	granite_buffer_pool_exchange(&pool, &block, 0, &used);
	CHECK(used.count == 40, "forty used blocks listed");
	i = live_buffers;
	granite_buffer_block_list_recycle(&used, &pool);
	CHECK(used.count == 0 && pool.count == 2 && live_buffers == 2 && i == 40, "two kept, the rest released");

	/* release a list */
	granite_buffer_pool_exchange(&pool, &block, 8, &used);
	granite_buffer_block_allocate(&block, 8);
	granite_buffer_pool_exchange(&pool, &block, 0, &used);
	CHECK(used.count == 1 && pool.count == 1 && live_buffers == 2, "one listed, one kept");
	granite_buffer_block_list_release(&used, &pool);
	CHECK(used.count == 0 && live_buffers == 1 && pool.count == 1, "releasing the list releases its blocks, not the pool's");
	granite_buffer_block_list_free(&used);
	CHECK(used.blocks == NULL && used.capacity == 0, "the list's room is freed");

	/* failures */
	fail_map = true;
	granite_buffer_pool_request(&pool, 8, &block);
	CHECK(block.handle != NULL && block.mapped != NULL && pool.count == 0 && live_buffers == 1,
			"a kept block that cannot be mapped is released and a new one made");
	fail_map = false;
	granite_buffer_pool_recycle(&pool, &block);
	granite_buffer_pool_reset(&pool);
	CHECK(pool.count == 0 && live_buffers == 0, "a reset releases what is kept");

	fail_create = true;
	granite_buffer_pool_request(&pool, 8, &block);
	CHECK(block.handle == NULL && block.mapped == NULL && block.size == 0, "no buffer to be had: no block");
	CHECK(granite_buffer_block_allocate(&block, 8).host == NULL, "which hands out nothing");
	granite_buffer_pool_exchange(&pool, &block, 8, &used);
	CHECK(block.handle == NULL, "nor does an exchange get one");
	fail_create = false;

	create_unmapped = true;
	granite_buffer_pool_request(&pool, 8, &block);
	CHECK(block.handle == NULL && live_buffers == 0, "a buffer that cannot be written is released: no block");
	create_unmapped = false;

	granite_buffer_pool_free(&pool);
	CHECK(pool.blocks == NULL && live_buffers == 0, "the pool is freed");
	free_buffers();

	/* a pool that keeps nothing */
	CHECK(granite_buffer_pool_init(&pool, &ops, &user_token, 64, 4, 0, 0, 0), "a pool that keeps no block");
	granite_buffer_pool_request(&pool, 8, &block);
	granite_buffer_pool_recycle(&pool, &block);
	CHECK(live_buffers == 0 && pool.count == 0, "releases what is given back");
	granite_buffer_pool_free(&pool);
	free_buffers();
}

int main(void)
{
	test_block();
	test_pool();

	if (failures)
	{
		fprintf(stderr, "test_vk_buffer_pool: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_vk_buffer_pool: OK\n");
	return 0;
}
