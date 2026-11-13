/* parallel-rdp's deferred memory release (vulkan/memory_release.c), with
 * the real memory allocator through a stand-in device table.
 *
 * A frame context lists the device memory to free once the GPU is done
 * with it; collecting the list frees each piece once and leaves the
 * list empty with its room kept, so a steady frame allocates nothing.
 * Memory released from other threads waits, with no lock, on the stack
 * of the frame context it was released to, and reaches that frame's
 * list exactly once when it is drained, never another frame's. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <boolean.h>
#include <retro_atomic.h>
#include <rthreads/rthreads.h>

#include "memory_release.h"
#include "frame_garbage.h" /* GRANITE_VK_TO_BITS */

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_vk_memory_release: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

#define THREADS  4
#define RELEASES 3000
#define FRAMES   3
#define TOTAL    (THREADS * RELEASES)

/* ---- the stand-in device: every block is counted in and out ---- */
static unsigned allocated;
static unsigned char freed[TOTAL + 1024];
static unsigned free_calls, bad_frees;

static VkResult VKAPI_CALL fake_allocate(VkDevice d, const VkMemoryAllocateInfo *info,
		const VkAllocationCallbacks *a, VkDeviceMemory *memory)
{
	(void)d; (void)info; (void)a;
	allocated++;
	*memory = GRANITE_VK_FROM_BITS(VkDeviceMemory, allocated);
	return VK_SUCCESS;
}

static void VKAPI_CALL fake_free(VkDevice d, VkDeviceMemory memory, const VkAllocationCallbacks *a)
{
	size_t n = (size_t)GRANITE_VK_TO_BITS(memory);
	(void)d; (void)a;
	free_calls++;
	if (n < 1 || n > allocated || n >= sizeof(freed) || freed[n])
		bad_frees++;
	else
		freed[n] = 1;
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

static struct VolkDeviceTable table;
static granite_device_allocator_t allocator;
static uint8_t host_memory[4096];

/* a block of its own, as the allocator counts it */
static granite_allocation_t new_block(void)
{
	granite_allocation_t alloc;
	memset(&alloc, 0, sizeof(alloc));
	CHECK(granite_device_allocator_import_host(&allocator, 4096, 0, host_memory, &alloc), "a block is made");
	return alloc;
}

static uint64_t tracked(void)
{
	granite_heap_budget_t budgets[VK_MAX_MEMORY_HEAPS];
	granite_device_allocator_budget(&allocator, budgets);
	return budgets[0].tracked_usage;
}

static void test_garbage(void)
{
	granite_memory_garbage_t garbage;
	granite_allocation_t a, b;
	granite_allocation_t *room;
	size_t capacity;
	unsigned i, before_calls, first;

	memset(&garbage, 0, sizeof(garbage));
	granite_memory_garbage_collect(&garbage, &allocator);
	CHECK(garbage.count == 0 && garbage.items == NULL, "collecting an empty list does nothing");

	first = allocated + 1;
	a = new_block();
	b = new_block();
	before_calls = free_calls;
	granite_memory_garbage_push(&garbage, &a);
	granite_memory_garbage_push(&garbage, &b);
	CHECK(garbage.count == 2 && garbage.items[0].base == a.base && garbage.items[1].base == b.base
			&& free_calls == before_calls, "listed, in order, and not freed yet");
	CHECK(tracked() == 2 * 4096, "still counted");
	granite_memory_garbage_collect(&garbage, &allocator);
	CHECK(free_calls == before_calls + 2 && freed[first] && freed[first + 1] && bad_frees == 0,
			"collected: each is freed once");
	CHECK(garbage.count == 0 && tracked() == 0, "and the list is empty");
	granite_memory_garbage_collect(&garbage, &allocator);
	CHECK(free_calls == before_calls + 2, "nothing is freed twice");

	/* more than it has room for, then the same again: no new room */
	for (i = 0; i < 100; i++)
	{
		a = new_block();
		granite_memory_garbage_push(&garbage, &a);
	}
	CHECK(garbage.count == 100 && garbage.capacity >= 100, "the list grows");
	before_calls = free_calls;
	granite_memory_garbage_collect(&garbage, &allocator);
	CHECK(free_calls == before_calls + 100 && bad_frees == 0 && tracked() == 0, "all hundred are freed");
	room = garbage.items;
	capacity = garbage.capacity;
	for (i = 0; i < 100; i++)
	{
		a = new_block();
		granite_memory_garbage_push(&garbage, &a);
	}
	CHECK(garbage.items == room && garbage.capacity == capacity, "a frame like the last one allocates nothing");
	granite_memory_garbage_collect(&garbage, &allocator);

	granite_memory_garbage_free(&garbage);
	CHECK(garbage.items == NULL && garbage.count == 0 && garbage.capacity == 0, "freed, nothing is left");
	granite_memory_garbage_free(&garbage);
}

static void test_queue_frames(void)
{
	granite_memory_release_queue_t queue;
	granite_memory_garbage_t garbage;
	granite_allocation_t a, b, c;

	memset(&garbage, 0, sizeof(garbage));
	granite_memory_release_queue_init(&queue);
	a = new_block();
	b = new_block();
	c = new_block();
	granite_memory_release_queue_push(&queue, 1, &a);
	granite_memory_release_queue_push(&queue, 2, &b);
	granite_memory_release_queue_push(&queue, 1 + GRANITE_RELEASE_FRAMES, &c);

	granite_memory_release_queue_drain(&queue, 0, &garbage);
	CHECK(garbage.count == 0, "a frame nothing was released to gets nothing");
	granite_memory_release_queue_drain(&queue, 2, &garbage);
	CHECK(garbage.count == 1 && garbage.items[0].base == b.base && garbage.items[0].size == 4096
			&& garbage.items[0].host_base == b.host_base, "a frame gets what was released to it, whole");
	granite_memory_release_queue_drain(&queue, 1, &garbage);
	CHECK(garbage.count == 3 && (garbage.items[1].base == a.base || garbage.items[2].base == a.base)
			&& (garbage.items[1].base == c.base || garbage.items[2].base == c.base),
			"the frame index wraps onto the same stacks");
	granite_memory_release_queue_drain(&queue, 1, &garbage);
	granite_memory_release_queue_drain(&queue, 2, &garbage);
	CHECK(garbage.count == 3, "and nothing is drained twice");

	/* the nodes go back to their pool: three more need no new ones */
	CHECK(retro_atomic_load_acquire_int(&queue.nodes.fresh) == 3, "three nodes were handed out");
	granite_memory_release_queue_push(&queue, 0, &a);
	granite_memory_release_queue_push(&queue, 0, &b);
	granite_memory_release_queue_push(&queue, 0, &c);
	CHECK(retro_atomic_load_acquire_int(&queue.nodes.fresh) == 3, "drained nodes are used again");
	garbage.count = 0;
	granite_memory_release_queue_drain(&queue, 0, &garbage);
	CHECK(garbage.count == 3, "and carry the next releases");

	granite_memory_garbage_collect(&garbage, &allocator);
	granite_memory_garbage_free(&garbage);
	granite_memory_release_queue_free(&queue);
}

/* ---- several threads release while the owner goes round ---- */
static granite_memory_release_queue_t shared_queue;
static granite_allocation_t blocks[THREADS][RELEASES];
static retro_atomic_int_t current_frame, producers_done;

static void producer(void *data)
{
	unsigned thread = (unsigned)(size_t)data;
	unsigned seq;
	for (seq = 0; seq < RELEASES; seq++)
		granite_memory_release_queue_push(&shared_queue,
				(unsigned)retro_atomic_load_acquire_int(&current_frame), &blocks[thread][seq]);
	retro_atomic_fetch_add_int(&producers_done, 1);
}

static void test_queue_threads(void)
{
	static granite_memory_garbage_t garbage[FRAMES];
	sthread_t *threads[THREADS];
	unsigned t, s, frame = 0, first, before_calls, missing = 0;

	first = allocated + 1;
	for (t = 0; t < THREADS; t++)
		for (s = 0; s < RELEASES; s++)
			blocks[t][s] = new_block();
	before_calls = free_calls;

	granite_memory_release_queue_init(&shared_queue);
	retro_atomic_int_init(&current_frame, 0);
	retro_atomic_int_init(&producers_done, 0);
	for (t = 0; t < THREADS; t++)
		threads[t] = sthread_create(producer, (void *)(size_t)t);

	/* the owner: next frame context, take and free what was released to
	 * it, then point releases at it */
	while (retro_atomic_load_acquire_int(&producers_done) < THREADS)
	{
		frame = (frame + 1) % FRAMES;
		granite_memory_release_queue_drain(&shared_queue, frame, &garbage[frame]);
		granite_memory_garbage_collect(&garbage[frame], &allocator);
		retro_atomic_store_release_int(&current_frame, (int)frame);
	}
	for (t = 0; t < THREADS; t++)
	{
		if (threads[t])
			sthread_join(threads[t]);
		else
			producer((void *)(size_t)t);
	}
	for (frame = 0; frame < FRAMES; frame++)
	{
		granite_memory_release_queue_drain(&shared_queue, frame, &garbage[frame]);
		granite_memory_garbage_collect(&garbage[frame], &allocator);
		granite_memory_garbage_free(&garbage[frame]);
	}

	for (s = 0; s < TOTAL; s++)
		if (!freed[first + s])
			missing++;
	CHECK(missing == 0, "a release was lost");
	CHECK(bad_frees == 0 && free_calls == before_calls + TOTAL, "a release was freed twice");
	CHECK(tracked() == 0, "the allocator's count is back to nothing");
	granite_memory_release_queue_free(&shared_queue);
}

int main(void)
{
	VkPhysicalDeviceMemoryProperties props;

	memset(&table, 0, sizeof(table));
	table.vkAllocateMemory = fake_allocate;
	table.vkFreeMemory = fake_free;
	table.vkMapMemory = fake_map;

	memset(&props, 0, sizeof(props));
	props.memoryHeapCount = 1;
	props.memoryHeaps[0].size = (VkDeviceSize)1 << 40;
	props.memoryTypeCount = 1;
	props.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	CHECK(granite_device_allocator_init(&allocator, (VkDevice)(size_t)1, (VkPhysicalDevice)(size_t)2, &table, &props,
				64, false, false), "the allocator comes up");

	test_garbage();
	test_queue_frames();
	test_queue_threads();

	granite_device_allocator_free(&allocator);

	if (failures)
	{
		fprintf(stderr, "test_vk_memory_release: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_vk_memory_release: ok\n");
	return 0;
}
