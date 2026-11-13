/* parallel-rdp's frame contexts (vulkan/frame_context.c), through a
 * stand-in device table.
 *
 * A device goes round a few frame contexts. Each has a command pool per
 * queue and recording thread, a list per queue of the ended command
 * buffers waiting for that queue's next submit, and everything that may
 * only be freed once the frame's GPU work is done. When its turn comes
 * again it first waits for that work, and only then takes what other
 * threads released to it (and to no other frame), resets its command
 * pools, gives its buffer blocks back to the pool each came from,
 * destroys or recycles its garbage and frees its memory. A waiting
 * command buffer is kept alive by the list and let go when the queue
 * has taken it. The lists keep their room from turn to turn. */
#include <stdio.h>
#include <string.h>
#include <boolean.h>

#include "frame_context.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_vk_frame_context: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

#define DEV ((VkDevice)(size_t)1)
#define SEM(n) GRANITE_VK_FROM_BITS(VkSemaphore, 0x300 + (n))
#define FENCE(n) GRANITE_VK_FROM_BITS(VkFence, 0x400 + (n))
#define PIPELINE(n) GRANITE_VK_FROM_BITS(VkPipeline, 0x500 + (n))
#define THREADS 2
#define MAX_POOLS 16
#define MAX_BUFFERS 64

/* ---- the stand-in device ---- */

/* set by the waits; everything a frame gives back checks it */
static bool waited, must_have_waited;
#define AFTER_WAIT() CHECK(!must_have_waited || waited, "nothing is given back before the frame's work is waited for")

static unsigned semaphore_waits, fence_waits;
static uint32_t waited_count;
static VkSemaphore waited_semaphore;
static uint64_t waited_value;
static VkFence waited_fence;

static VkResult VKAPI_CALL fake_wait_semaphores(VkDevice d, const VkSemaphoreWaitInfo *info, uint64_t timeout)
{
	(void)d; (void)timeout;
	semaphore_waits++;
	waited_count = info->semaphoreCount;
	waited_semaphore = info->pSemaphores[0];
	waited_value = info->pValues[0];
	waited = true;
	return VK_SUCCESS;
}

static VkResult VKAPI_CALL fake_wait_fences(VkDevice d, uint32_t count, const VkFence *fences, VkBool32 all,
		uint64_t timeout)
{
	(void)d; (void)all; (void)timeout;
	fence_waits++;
	waited_count = count;
	waited_fence = fences[0];
	waited = true;
	return VK_SUCCESS;
}

static unsigned pools_created, pools_destroyed, resets, release_resets, trims;
static uint32_t pool_family[MAX_POOLS + 1];
static unsigned pool_resets[MAX_POOLS + 1];
static unsigned cb_allocated, cb_freed;
static char cb_storage[MAX_BUFFERS];
static unsigned cb_pool[MAX_BUFFERS];

static VkResult VKAPI_CALL fake_create_pool(VkDevice d, const VkCommandPoolCreateInfo *info,
		const VkAllocationCallbacks *a, VkCommandPool *pool)
{
	(void)d; (void)a;
	if (pools_created == MAX_POOLS)
		return VK_ERROR_OUT_OF_HOST_MEMORY;
	pools_created++;
	pool_family[pools_created] = info->queueFamilyIndex;
	*pool = GRANITE_VK_FROM_BITS(VkCommandPool, pools_created);
	return VK_SUCCESS;
}

static void VKAPI_CALL fake_destroy_pool(VkDevice d, VkCommandPool pool, const VkAllocationCallbacks *a)
{
	(void)d; (void)pool; (void)a;
	AFTER_WAIT();
	pools_destroyed++;
}

static VkResult VKAPI_CALL fake_reset_pool(VkDevice d, VkCommandPool pool, VkCommandPoolResetFlags flags)
{
	(void)d;
	AFTER_WAIT();
	if (flags & VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT)
		release_resets++;
	else
	{
		resets++;
		pool_resets[(size_t)GRANITE_VK_TO_BITS(pool)]++;
	}
	return VK_SUCCESS;
}

static void VKAPI_CALL fake_trim_pool(VkDevice d, VkCommandPool pool, VkCommandPoolTrimFlags flags)
{
	(void)d; (void)pool; (void)flags;
	trims++;
}

static VkResult VKAPI_CALL fake_allocate_cb(VkDevice d, const VkCommandBufferAllocateInfo *info, VkCommandBuffer *out)
{
	(void)d;
	if (cb_allocated == MAX_BUFFERS)
		return VK_ERROR_OUT_OF_DEVICE_MEMORY;
	cb_pool[cb_allocated] = (unsigned)GRANITE_VK_TO_BITS(info->commandPool);
	*out = (VkCommandBuffer)&cb_storage[cb_allocated++];
	return VK_SUCCESS;
}

static void VKAPI_CALL fake_free_cb(VkDevice d, VkCommandPool pool, uint32_t count, const VkCommandBuffer *buffers)
{
	(void)d; (void)pool; (void)buffers;
	cb_freed += count;
}

/* the pool a command buffer came from */
static unsigned pool_of(VkCommandBuffer cmd)
{
	size_t n = (size_t)((const char *)cmd - cb_storage);
	return n < MAX_BUFFERS ? cb_pool[n] : 0;
}

/* and that pool's family */
static uint32_t family_of(VkCommandBuffer cmd)
{
	size_t n = (size_t)((const char *)cmd - cb_storage);
	return n < MAX_BUFFERS ? pool_family[cb_pool[n]] : 0xdeadu;
}

static unsigned pipelines_destroyed;
static uint64_t last_pipeline;
static void VKAPI_CALL fake_destroy_pipeline(VkDevice d, VkPipeline pipeline, const VkAllocationCallbacks *a)
{
	(void)d; (void)a;
	AFTER_WAIT();
	pipelines_destroyed++;
	last_pipeline = GRANITE_VK_TO_BITS(pipeline);
}

static unsigned fence_resets, fences_destroyed, semaphores_destroyed;
static VkResult VKAPI_CALL fake_reset_fences(VkDevice d, uint32_t count, const VkFence *fences)
{
	(void)d; (void)fences;
	AFTER_WAIT();
	fence_resets += count;
	return VK_SUCCESS;
}

static void VKAPI_CALL fake_destroy_fence(VkDevice d, VkFence fence, const VkAllocationCallbacks *a)
{
	(void)d; (void)fence; (void)a;
	fences_destroyed++;
}

static void VKAPI_CALL fake_destroy_semaphore(VkDevice d, VkSemaphore semaphore, const VkAllocationCallbacks *a)
{
	(void)d; (void)semaphore; (void)a;
	semaphores_destroyed++;
}

static unsigned memory_allocated, memory_freed;
static VkResult VKAPI_CALL fake_allocate_memory(VkDevice d, const VkMemoryAllocateInfo *info,
		const VkAllocationCallbacks *a, VkDeviceMemory *memory)
{
	(void)d; (void)info; (void)a;
	memory_allocated++;
	*memory = GRANITE_VK_FROM_BITS(VkDeviceMemory, memory_allocated);
	return VK_SUCCESS;
}

static void VKAPI_CALL fake_free_memory(VkDevice d, VkDeviceMemory memory, const VkAllocationCallbacks *a)
{
	(void)d; (void)memory; (void)a;
	AFTER_WAIT();
	memory_freed++;
}

static VkResult VKAPI_CALL fake_map(VkDevice d, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size,
		VkMemoryMapFlags flags, void **data)
{
	(void)d; (void)offset; (void)size; (void)flags;
	/* never read or written */
	*data = (void *)((size_t)GRANITE_VK_TO_BITS(memory) * 0x1000u);
	return VK_SUCCESS;
}

static void VKAPI_CALL fake_memory_properties2(VkPhysicalDevice gpu, VkPhysicalDeviceMemoryProperties2 *props)
{
	(void)gpu; (void)props;
}
PFN_vkGetPhysicalDeviceMemoryProperties2 vkGetPhysicalDeviceMemoryProperties2 = fake_memory_properties2;

/* ---- the buffers behind the blocks ---- */
static unsigned blocks_created, blocks_released;
static uint8_t block_memory[64];

static void *block_create(void *user, VkDeviceSize size, VkBufferUsageFlags usage, uint8_t **mapped)
{
	(void)user; (void)size; (void)usage;
	*mapped = block_memory;
	return &block_memory[++blocks_created];
}

static void block_release(void *user, void *handle)
{
	(void)user; (void)handle;
	blocks_released++;
}

static uint8_t *block_map(void *user, void *handle)
{
	(void)user; (void)handle;
	return block_memory;
}

static void block_unmap(void *user, void *handle)
{
	(void)user; (void)handle;
}

static const granite_buffer_pool_ops_t block_ops = { block_create, block_release, block_map, block_unmap };

/* ---- what the frames share ---- */
static struct VolkDeviceTable table;
static granite_device_allocator_t allocator;
static granite_fence_pool_t fence_pool;
static granite_semaphore_pool_t semaphore_pool;
static granite_buffer_pool_t vbo, ubo;
static granite_release_queue_t releases;
static granite_memory_release_queue_t memory_releases;
static granite_frame_env_t env;
static granite_lf_pool_t cb_objects;
static uint8_t host_memory[4096];

static const uint32_t families[GRANITE_FRAME_QUEUES] = { 5, 7, VK_QUEUE_FAMILY_IGNORED };
static VkSemaphore timelines[GRANITE_FRAME_QUEUES];

static granite_allocation_t new_memory(void)
{
	granite_allocation_t alloc;
	memset(&alloc, 0, sizeof(alloc));
	CHECK(granite_device_allocator_import_host(&allocator, 4096, 0, host_memory, &alloc), "a block of memory is made");
	return alloc;
}

/* a command buffer object with the one reference its recorder holds */
static granite_command_buffer_t *new_cb(VkCommandBuffer cmd)
{
	granite_command_buffer_t *cb = (granite_command_buffer_t *)granite_lf_pool_alloc(&cb_objects);
	memset(cb, 0, sizeof(*cb));
	granite_refs_init(&cb->refs);
	cb->pool = &cb_objects;
	cb->cmd = cmd;
	return cb;
}

static int refs_of(granite_command_buffer_t *cb)
{
	return (int)retro_atomic_load_acquire_int(&cb->refs);
}

/* a used block of the pool's size goes on the frame's list */
static void use_block(granite_buffer_pool_t *pool, granite_buffer_block_list_t *list)
{
	granite_buffer_block_t block;
	memset(&block, 0, sizeof(block));
	granite_buffer_pool_request(pool, 16, &block);
	CHECK(block.handle != NULL, "a block is to be had");
	CHECK(granite_buffer_block_allocate(&block, 16).host != NULL, "and written to");
	granite_buffer_pool_exchange(pool, &block, 0, list);
}

static void test_init_and_request(void)
{
	granite_frame_context_t frame;
	VkCommandBuffer a, b, c, d;
	unsigned i, before;

	before = pools_created;
	CHECK(granite_frame_context_init(&frame, &env, 3, families, timelines, THREADS), "a frame context comes up");
	CHECK(frame.env == &env && frame.index == 3 && frame.threads == THREADS, "it knows its place");
	CHECK(pools_created == before + 2 * THREADS, "a command pool per thread, for each queue the device has");
	CHECK(pool_family[before + 1] == 5 && pool_family[before + 2] == 5
			&& pool_family[before + 3] == 7 && pool_family[before + 4] == 7, "of the queue's family");
	for (i = 0; i < GRANITE_FRAME_QUEUES; i++)
	{
		CHECK(frame.sync.timeline_semaphores[i] == timelines[i], "it waits on the queues' timelines");
		CHECK(frame.num_submissions[i] == 0 && frame.submission_capacity[i] >= 16 && frame.submissions[i],
				"and has room for submissions from the start");
	}

	a = granite_frame_context_request(&frame, 0, 0);
	b = granite_frame_context_request(&frame, 0, 1);
	c = granite_frame_context_request(&frame, 1, 1);
	d = granite_frame_context_request(&frame, 0, 0);
	CHECK(a && b && c && d && a != b && a != c && b != c && a != d, "command buffers are handed out");
	CHECK(family_of(a) == 5 && family_of(b) == 5 && family_of(c) == 7, "from the queue's pool");
	CHECK(pool_of(a) != pool_of(b) && pool_of(a) == pool_of(d), "each thread from its own");
	CHECK(granite_frame_context_request(&frame, 2, 0) == VK_NULL_HANDLE, "none for a queue the device lacks");

	/* the frame's turn comes again: the same ones, without asking the driver */
	before = cb_allocated;
	granite_frame_context_begin(&frame);
	CHECK(granite_frame_context_request(&frame, 0, 0) == a && granite_frame_context_request(&frame, 0, 0) == d
			&& granite_frame_context_request(&frame, 0, 1) == b && granite_frame_context_request(&frame, 1, 1) == c
			&& cb_allocated == before, "after begin the command buffers are used again");

	before = trims;
	i = cb_freed;
	granite_frame_context_trim(&frame);
	CHECK(trims == before + 2 * THREADS && cb_freed == i + 4, "trim gives every pool's memory back");

	a = granite_frame_context_request(&frame, 1, 0);
	before = pools_destroyed;
	i = cb_freed;
	granite_frame_context_free(&frame);
	CHECK(pools_destroyed == before + 2 * THREADS && cb_freed == i + 1, "free destroys the pools and what they hold");
	CHECK(frame.env == NULL && frame.cmd_pools[0] == NULL && frame.submissions[0] == NULL, "and leaves nothing");
	before = pools_destroyed;
	granite_frame_context_free(&frame);
	CHECK(pools_destroyed == before, "freeing one that is not up does nothing");
}

static void test_submissions(void)
{
	granite_frame_context_t frame;
	granite_command_buffer_t *cbs[40], *other;
	granite_command_buffer_t **room;
	unsigned i, capacity;
	bool ok;

	CHECK(granite_frame_context_init(&frame, &env, 0, families, timelines, THREADS), "a frame context comes up");

	for (i = 0; i < 40; i++)
		cbs[i] = new_cb((VkCommandBuffer)&cb_storage[i]);
	other = new_cb((VkCommandBuffer)&cb_storage[50]);

	for (i = 0; i < 40; i++)
		granite_frame_context_submit(&frame, 1, cbs[i]);
	granite_frame_context_submit(&frame, 0, other);
	CHECK(frame.num_submissions[1] == 40 && frame.num_submissions[0] == 1 && frame.num_submissions[2] == 0,
			"each queue has its own list, which grows");
	ok = true;
	for (i = 0; i < 40; i++)
		if (frame.submissions[1][i] != cbs[i] || refs_of(cbs[i]) != 2)
			ok = false;
	CHECK(ok && frame.submissions[0][0] == other, "in the order submitted, each kept alive by the list");

	/* the recorder lets go: the list's reference is the last */
	for (i = 0; i < 40; i++)
		granite_cb_unref(cbs[i]);
	CHECK(refs_of(cbs[0]) == 1 && refs_of(cbs[39]) == 1 && frame.submissions[1][39]->cmd == (VkCommandBuffer)&cb_storage[39],
			"still there for the queue");

	granite_frame_context_submitted(&frame, 1);
	CHECK(frame.num_submissions[1] == 0 && refs_of(cbs[0]) == 0 && refs_of(cbs[39]) == 0, "the queue took them: let go");
	CHECK(frame.num_submissions[0] == 1 && refs_of(other) == 2, "the other queue's wait on");
	CHECK((granite_command_buffer_t *)granite_lf_pool_alloc(&cb_objects) != NULL, "their objects are free again");

	/* as many again: no new room */
	room = frame.submissions[1];
	capacity = frame.submission_capacity[1];
	for (i = 0; i < 40; i++)
	{
		cbs[i] = new_cb((VkCommandBuffer)&cb_storage[i]);
		granite_frame_context_submit(&frame, 1, cbs[i]);
		granite_cb_unref(cbs[i]);
	}
	CHECK(frame.submissions[1] == room && frame.submission_capacity[1] == capacity && capacity >= 40,
			"the list keeps its room");
	granite_frame_context_submitted(&frame, 1);
	granite_frame_context_submitted(&frame, 1);
	CHECK(refs_of(cbs[5]) == 0, "nothing is let go twice");

	granite_frame_context_submitted(&frame, 0);
	CHECK(refs_of(other) == 1, "the recorder's reference is its own");
	granite_cb_unref(other);

	granite_frame_context_free(&frame);
}

static void test_begin(void)
{
	granite_frame_context_t frames[2];
	granite_allocation_t alloc;
	unsigned before_pipelines, before_memory, before_resets, before_waits;
	size_t vbo_kept, ubo_kept, fences_kept, semaphores_kept;
	unsigned first_pool;

	first_pool = pools_created + 1;
	CHECK(granite_frame_context_init(&frames[0], &env, 0, families, timelines, THREADS)
			&& granite_frame_context_init(&frames[1], &env, 1, families, timelines, THREADS), "two frame contexts");

	/* frame 1 did work on queue 1 and holds things; another thread
	 * released things to it, and to frame 0 */
	frames[1].sync.timeline_values[1] = 9;
	CHECK(granite_frame_context_request(&frames[1], 1, 1) != VK_NULL_HANDLE, "a command buffer is recorded");
	use_block(&vbo, &frames[1].vbo_blocks);
	use_block(&ubo, &frames[1].ubo_blocks);
	use_block(&ubo, &frames[1].ubo_blocks);
	granite_frame_garbage_push(&frames[1].garbage, GRANITE_GARBAGE_PIPELINE, GRANITE_VK_TO_BITS(PIPELINE(1)));
	granite_frame_garbage_push(&frames[1].garbage, GRANITE_GARBAGE_RECYCLE_FENCE, GRANITE_VK_TO_BITS(FENCE(1)));
	granite_frame_garbage_push(&frames[1].garbage, GRANITE_GARBAGE_RECYCLE_SEMAPHORE, GRANITE_VK_TO_BITS(SEM(9)));
	alloc = new_memory();
	granite_memory_garbage_push(&frames[1].memory, &alloc);

	granite_release_queue_set_frame(&releases, 1);
	granite_release_queue_push(&releases, GRANITE_GARBAGE_PIPELINE, GRANITE_VK_TO_BITS(PIPELINE(2)));
	alloc = new_memory();
	granite_memory_release_queue_push(&memory_releases, 1, &alloc);
	granite_release_queue_set_frame(&releases, 0);
	granite_release_queue_push(&releases, GRANITE_GARBAGE_PIPELINE, GRANITE_VK_TO_BITS(PIPELINE(3)));
	alloc = new_memory();
	granite_memory_release_queue_push(&memory_releases, 0, &alloc);

	before_pipelines = pipelines_destroyed;
	before_memory = memory_freed;
	before_resets = resets;
	before_waits = semaphore_waits;
	vbo_kept = vbo.count;
	ubo_kept = ubo.count;
	fences_kept = fence_pool.count;
	semaphores_kept = semaphore_pool.count;
	waited = false;
	must_have_waited = true;
	granite_frame_context_begin(&frames[1]);
	must_have_waited = false;

	CHECK(semaphore_waits == before_waits + 1 && waited_count == 1 && waited_semaphore == timelines[1]
			&& waited_value == 9, "begin waits for the frame's work");
	CHECK(pipelines_destroyed == before_pipelines + 2 && last_pipeline != GRANITE_VK_TO_BITS(PIPELINE(3)),
			"then destroys its garbage and what was released to it, not to another frame");
	CHECK(memory_freed == before_memory + 2, "frees its memory, and the memory released to it");
	CHECK(resets == before_resets + 1 && pool_resets[first_pool + 2 * THREADS + THREADS + 1] == 1,
			"resets the command pool that was used");
	CHECK(frames[1].vbo_blocks.count == 0 && frames[1].ubo_blocks.count == 0
			&& vbo.count == vbo_kept + 1 && ubo.count == ubo_kept + 2, "gives each block back to its own pool");
	CHECK(fence_resets == 1 && fence_pool.count == fences_kept + 1 && semaphore_pool.count == semaphores_kept + 1,
			"and its fences and semaphores to theirs");
	CHECK(frames[1].garbage.lists[GRANITE_GARBAGE_PIPELINE].count == 0 && frames[1].memory.count == 0,
			"its lists are empty");

	/* nothing happened since: nothing to wait for but the same value, nothing to give back */
	before_pipelines = pipelines_destroyed;
	before_memory = memory_freed;
	before_resets = resets;
	granite_frame_context_begin(&frames[1]);
	CHECK(pipelines_destroyed == before_pipelines && memory_freed == before_memory && resets == before_resets,
			"a second begin gives nothing back twice");

	/* a device without timeline semaphores: the frame's fences */
	env.timeline = false;
	before_waits = semaphore_waits;
	CHECK(granite_frame_sync_add_fence(&frames[0].sync, FENCE(4)), "a fence covers the frame");
	frames[0].sync.timeline_values[0] = 3;
	granite_frame_context_begin(&frames[0]);
	CHECK(semaphore_waits == before_waits && fence_waits == 1 && waited_count == 1 && waited_fence == FENCE(4),
			"without timeline semaphores begin waits for the fences");
	env.timeline = true;
	CHECK(pipelines_destroyed == before_pipelines + 1 && last_pipeline == GRANITE_VK_TO_BITS(PIPELINE(3))
			&& memory_freed == before_memory + 1, "frame 0 takes what was released to it");

	/* free waits and gives back too */
	frames[0].sync.timeline_values[0] = 0;
	frames[1].sync.timeline_values[1] = 11;
	granite_release_queue_set_frame(&releases, 1);
	granite_release_queue_push(&releases, GRANITE_GARBAGE_PIPELINE, GRANITE_VK_TO_BITS(PIPELINE(4)));
	use_block(&vbo, &frames[1].vbo_blocks);
	alloc = new_memory();
	granite_memory_garbage_push(&frames[1].memory, &alloc);
	vbo_kept = vbo.count;
	before_memory = memory_freed;
	waited = false;
	must_have_waited = true;
	granite_frame_context_free(&frames[1]);
	must_have_waited = false;
	CHECK(waited && waited_value == 11 && last_pipeline == GRANITE_VK_TO_BITS(PIPELINE(4))
			&& memory_freed == before_memory + 1 && vbo.count == vbo_kept + 1, "free begins the frame first");
	granite_frame_context_free(&frames[0]);
}

int main(void)
{
	VkPhysicalDeviceMemoryProperties props;
	unsigned i;

	memset(&table, 0, sizeof(table));
	table.vkWaitSemaphoresKHR = fake_wait_semaphores;
	table.vkWaitForFences = fake_wait_fences;
	table.vkCreateCommandPool = fake_create_pool;
	table.vkDestroyCommandPool = fake_destroy_pool;
	table.vkResetCommandPool = fake_reset_pool;
	table.vkTrimCommandPool = fake_trim_pool;
	table.vkAllocateCommandBuffers = fake_allocate_cb;
	table.vkFreeCommandBuffers = fake_free_cb;
	table.vkDestroyPipeline = fake_destroy_pipeline;
	table.vkResetFences = fake_reset_fences;
	table.vkDestroyFence = fake_destroy_fence;
	table.vkDestroySemaphore = fake_destroy_semaphore;
	table.vkAllocateMemory = fake_allocate_memory;
	table.vkFreeMemory = fake_free_memory;
	table.vkMapMemory = fake_map;

	memset(&props, 0, sizeof(props));
	props.memoryHeapCount = 1;
	props.memoryHeaps[0].size = (VkDeviceSize)1 << 40;
	props.memoryTypeCount = 1;
	props.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	CHECK(granite_device_allocator_init(&allocator, DEV, (VkPhysicalDevice)(size_t)2, &table, &props,
				64, false, false), "the allocator comes up");
	granite_fence_pool_init(&fence_pool, DEV, &table);
	granite_semaphore_pool_init(&semaphore_pool, DEV, &table);
	CHECK(granite_buffer_pool_init(&vbo, &block_ops, NULL, 64, 16, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, 0, 8)
			&& granite_buffer_pool_init(&ubo, &block_ops, NULL, 64, 16, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, 0, 8),
			"the buffer pools come up");
	granite_release_queue_init(&releases);
	granite_memory_release_queue_init(&memory_releases);
	granite_lf_pool_init(&cb_objects, sizeof(granite_command_buffer_t), 16);
	for (i = 0; i < GRANITE_FRAME_QUEUES; i++)
		timelines[i] = SEM(i);

	env.device = DEV;
	env.table = &table;
	env.timeline = true;
	env.memory = &allocator;
	env.fences = &fence_pool;
	env.semaphores = &semaphore_pool;
	env.vbo = &vbo;
	env.ubo = &ubo;
	env.releases = &releases;
	env.memory_releases = &memory_releases;

	test_init_and_request();
	test_submissions();
	test_begin();

	CHECK(memory_freed == memory_allocated, "all memory went back");
	granite_lf_pool_clear(&cb_objects);
	granite_memory_release_queue_free(&memory_releases);
	granite_release_queue_free(&releases);
	granite_buffer_pool_free(&vbo);
	granite_buffer_pool_free(&ubo);
	granite_semaphore_pool_free(&semaphore_pool);
	granite_fence_pool_free(&fence_pool);
	granite_device_allocator_free(&allocator);

	if (failures)
	{
		fprintf(stderr, "test_vk_frame_context: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_vk_frame_context: ok\n");
	return 0;
}
