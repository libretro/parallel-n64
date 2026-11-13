/* parallel-rdp's device memory allocator (vulkan/memory_allocator.c),
 * through a stand-in device table.
 *
 * Small allocations are cut from one block of the driver's and never
 * overlap, each on the alignment asked for and mapped where its block
 * is; freeing them all gives the block back, which is kept and handed out
 * again for the next request like it, and freed by a collect. Memory of
 * one type or mode is never cut from a block of another. What is too
 * large to cut is a block of its own, as is an image the driver wants
 * one for, and those are freed at
 * once. On a heap that must stay within its budget nothing is kept.
 *
 * When the driver is out of memory the blocks kept are freed, oldest
 * first, until the allocation fits: the loop that did this once tested
 * the wrong iterator and never ran, so the allocation failed with memory
 * still held. An allocation that cannot be made at all fails and leaves
 * nothing behind.
 *
 * Reading memory that is not coherent brings it in from the device first,
 * writing sends it after, in whole atoms. */
#include <stdio.h>
#include <string.h>
#include <boolean.h>

#include "memory_allocator.h"
#include "frame_garbage.h" /* GRANITE_VK_TO_BITS */

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_vk_memory_allocator: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

#define MIB ((uint64_t)1024 * 1024)
#define MAX_BLOCKS 64
/* where block n is "mapped": never read or written */
#define MAP_BASE(n) ((uint8_t *)((size_t)(n) * 0x8000000u))

/* ---- the stand-in device ---- */
static struct
{
	bool live;
	bool mapped;
	uint64_t size;
	uint32_t type;
	const void *dedicated_image;
	float priority;
} blocks[MAX_BLOCKS + 1];
static uint64_t capacity, used;
static unsigned allocate_calls, free_calls, map_calls, unmap_calls;
static unsigned freed_order[MAX_BLOCKS], num_freed;
static bool fail_map, prefer_dedicated;
static VkMappedMemoryRange invalidated, flushed;
static unsigned invalidate_calls, flush_calls;

static size_t block_of(VkDeviceMemory memory)
{
	return (size_t)GRANITE_VK_TO_BITS(memory);
}

static VkResult VKAPI_CALL fake_allocate(VkDevice d, const VkMemoryAllocateInfo *info,
		const VkAllocationCallbacks *a, VkDeviceMemory *memory)
{
	const VkBaseInStructure *next;
	size_t n;
	(void)d; (void)a;
	allocate_calls++;
	if (used + info->allocationSize > capacity)
		return VK_ERROR_OUT_OF_DEVICE_MEMORY;
	for (n = 1; n <= MAX_BLOCKS; n++)
		if (!blocks[n].live)
			break;
	if (n > MAX_BLOCKS)
		return VK_ERROR_OUT_OF_DEVICE_MEMORY;
	memset(&blocks[n], 0, sizeof(blocks[n]));
	blocks[n].live = true;
	blocks[n].size = info->allocationSize;
	blocks[n].type = info->memoryTypeIndex;
	blocks[n].priority = -1.0f;
	for (next = (const VkBaseInStructure *)info->pNext; next; next = next->pNext)
	{
		if (next->sType == VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO)
		{
			const VkMemoryDedicatedAllocateInfo *dedicated = (const VkMemoryDedicatedAllocateInfo *)next;
			blocks[n].dedicated_image = (const void *)(size_t)GRANITE_VK_TO_BITS(dedicated->image);
		}
		else if (next->sType == VK_STRUCTURE_TYPE_MEMORY_PRIORITY_ALLOCATE_INFO_EXT)
			blocks[n].priority = ((const VkMemoryPriorityAllocateInfoEXT *)next)->priority;
	}
	used += info->allocationSize;
	*memory = GRANITE_VK_FROM_BITS(VkDeviceMemory, n);
	return VK_SUCCESS;
}

static void VKAPI_CALL fake_free(VkDevice d, VkDeviceMemory memory, const VkAllocationCallbacks *a)
{
	size_t n = block_of(memory);
	(void)d; (void)a;
	free_calls++;
	CHECK(n >= 1 && n <= MAX_BLOCKS && blocks[n].live, "a block is freed once");
	if (n < 1 || n > MAX_BLOCKS || !blocks[n].live)
		return;
	blocks[n].live = false;
	used -= blocks[n].size;
	if (num_freed < MAX_BLOCKS)
		freed_order[num_freed++] = (unsigned)n;
}

static VkResult VKAPI_CALL fake_map(VkDevice d, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size,
		VkMemoryMapFlags flags, void **data)
{
	size_t n = block_of(memory);
	(void)d; (void)flags; (void)size;
	map_calls++;
	if (fail_map)
		return VK_ERROR_MEMORY_MAP_FAILED;
	CHECK(blocks[n].live && !blocks[n].mapped && offset == 0, "a live block is mapped once, whole");
	blocks[n].mapped = true;
	*data = MAP_BASE(n);
	return VK_SUCCESS;
}

static void VKAPI_CALL fake_unmap(VkDevice d, VkDeviceMemory memory)
{
	size_t n = block_of(memory);
	(void)d;
	unmap_calls++;
	CHECK(blocks[n].live && blocks[n].mapped, "what is unmapped was mapped");
	blocks[n].mapped = false;
}

static VkResult VKAPI_CALL fake_invalidate(VkDevice d, uint32_t count, const VkMappedMemoryRange *ranges)
{
	(void)d; (void)count;
	invalidate_calls++;
	invalidated = ranges[0];
	return VK_SUCCESS;
}

static VkResult VKAPI_CALL fake_flush(VkDevice d, uint32_t count, const VkMappedMemoryRange *ranges)
{
	(void)d; (void)count;
	flush_calls++;
	flushed = ranges[0];
	return VK_SUCCESS;
}

static void VKAPI_CALL fake_image_requirements(VkDevice d, const VkImageMemoryRequirementsInfo2 *info,
		VkMemoryRequirements2 *reqs)
{
	VkMemoryDedicatedRequirements *dedicated = (VkMemoryDedicatedRequirements *)reqs->pNext;
	(void)d; (void)info;
	dedicated->prefersDedicatedAllocation = prefer_dedicated ? VK_TRUE : VK_FALSE;
	dedicated->requiresDedicatedAllocation = VK_FALSE;
}

/* VK_EXT_memory_budget, which the allocator asks the loader's global for */
static uint64_t reported_budget;
static void VKAPI_CALL fake_memory_properties2(VkPhysicalDevice gpu, VkPhysicalDeviceMemoryProperties2 *props)
{
	VkPhysicalDeviceMemoryBudgetPropertiesEXT *budget = (VkPhysicalDeviceMemoryBudgetPropertiesEXT *)props->pNext;
	unsigned i;
	(void)gpu;
	for (i = 0; i < VK_MAX_MEMORY_HEAPS; i++)
	{
		budget->heapBudget[i] = reported_budget;
		budget->heapUsage[i] = used;
	}
}
PFN_vkGetPhysicalDeviceMemoryProperties2 vkGetPhysicalDeviceMemoryProperties2 = fake_memory_properties2;

static unsigned live_blocks(void)
{
	unsigned n, count = 0;
	for (n = 1; n <= MAX_BLOCKS; n++)
		if (blocks[n].live)
			count++;
	return count;
}

/* ---- */

#define TYPE_DEVICE 0 /* device local */
#define TYPE_HOST   1 /* host visible, not coherent */
#define TYPE_BAR    2 /* both, on a small heap of its own */

static struct VolkDeviceTable table;
static granite_device_allocator_t allocator;

static void device_init(bool budget)
{
	VkPhysicalDeviceMemoryProperties props;
	memset(&props, 0, sizeof(props));
	props.memoryHeapCount = 3;
	props.memoryHeaps[0].size = 1024 * 1024 * MIB;
	props.memoryHeaps[1].size = 1024 * 1024 * MIB;
	props.memoryHeaps[2].size = 256 * MIB;
	props.memoryTypeCount = 3;
	props.memoryTypes[TYPE_DEVICE].heapIndex = 0;
	props.memoryTypes[TYPE_DEVICE].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	props.memoryTypes[TYPE_HOST].heapIndex = 1;
	props.memoryTypes[TYPE_HOST].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
	props.memoryTypes[TYPE_BAR].heapIndex = 2;
	props.memoryTypes[TYPE_BAR].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		| VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
	capacity = 1024 * 1024 * MIB;
	CHECK(granite_device_allocator_init(&allocator, (VkDevice)(size_t)1, (VkPhysicalDevice)(size_t)2, &table,
				&props, 64, budget, true), "the allocator comes up");
}

#define MANY 400

int main(void)
{
	static granite_allocation_t many[MANY];
	static uint32_t many_align[MANY];
	static uint32_t many_size[MANY];
	granite_allocation_t a, b, c;
	uint32_t seed = 12345;
	unsigned i, j, before;
	bool ok;

	memset(&table, 0, sizeof(table));
	table.vkAllocateMemory = fake_allocate;
	table.vkFreeMemory = fake_free;
	table.vkMapMemory = fake_map;
	table.vkUnmapMemory = fake_unmap;
	table.vkInvalidateMappedMemoryRanges = fake_invalidate;
	table.vkFlushMappedMemoryRanges = fake_flush;
	table.vkGetImageMemoryRequirements2 = fake_image_requirements;
	memset(&allocator, 0, sizeof(allocator));
	device_init(false);
	CHECK(allocator.memory_heap_is_budget_critical[2] && !allocator.memory_heap_is_budget_critical[0]
			&& !allocator.memory_heap_is_budget_critical[1],
			"the small heap that is both device local and host visible is the one to stay within");

	/* ---- many small ones, of every size cut from a block ---- */
	for (i = 0; i < MANY; i++)
	{
		uint32_t size;
		seed = seed * 1664525u + 1013904223u;
		/* 1 byte to 6 MiB, most of them small */
		size = 1u + ((seed >> 8) % (1u << (4 + (seed >> 28) % 20)));
		many_align[i] = 1u << ((seed >> 4) % 13);
		many_size[i] = size;
		memset(&many[i], 0, sizeof(many[i]));
		CHECK(granite_device_allocator_allocate(&allocator, size, many_align[i],
					GRANITE_ALLOCATION_LINEAR_HOST_MAPPABLE, TYPE_HOST, &many[i]), "a small allocation");
		CHECK(many[i].size >= size, "of at least the size asked for");
		/* (free some as it goes: holes are filled again) */
		if (i % 7 == 3)
		{
			granite_allocation_free(&many[i - 2], &allocator);
			CHECK(many[i - 2].base == VK_NULL_HANDLE, "a freed allocation has no memory");
		}
	}
	ok = true;
	for (i = 0; i < MANY; i++)
	{
		if (many[i].base == VK_NULL_HANDLE)
			continue;
		if (     (many[i].offset & (many_align[i] - 1))
				|| many[i].host_base != MAP_BASE(block_of(many[i].base)) + many[i].offset
				|| many[i].memory_type != TYPE_HOST || many[i].mode != GRANITE_ALLOCATION_LINEAR_HOST_MAPPABLE
				|| (uint64_t)many[i].offset + many_size[i] > blocks[block_of(many[i].base)].size)
			ok = false;
		for (j = 0; j < i; j++)
			if (     many[j].base == many[i].base
					&& many[j].offset < many[i].offset + many_size[i]
					&& many[i].offset < many[j].offset + many_size[j])
				ok = false;
	}
	CHECK(ok, "each on its alignment, mapped where its block is, none over another");
	CHECK(free_calls == 0, "nothing given back to the driver meanwhile");
	before = allocate_calls;
	CHECK(before >= 1 && before <= 8, "all cut from a few 64 MiB blocks");
	for (i = 1; i <= MAX_BLOCKS; i++)
		if (blocks[i].live && (blocks[i].size != 64 * MIB || blocks[i].type != TYPE_HOST || !blocks[i].mapped))
			ok = false;
	CHECK(ok, "of the type asked for, mapped");

	for (i = 0; i < MANY; i++)
		granite_allocation_free(&many[i], &allocator);
	CHECK(free_calls == 0 && unmap_calls == before && live_blocks() == before,
			"all freed: the blocks are unmapped and kept");
	memset(&a, 0, sizeof(a));
	CHECK(granite_device_allocator_allocate(&allocator, 100, 1, GRANITE_ALLOCATION_LINEAR_HOST_MAPPABLE, TYPE_HOST, &a)
			&& allocate_calls == before && a.host_base != NULL,
			"the next allocation like them takes a kept block, mapped again");
	granite_device_allocator_collect(&allocator);
	CHECK(free_calls == before - 1 && live_blocks() == 1, "a collect frees the blocks kept, not the one in use");
	granite_allocation_free(&a, &allocator);
	granite_device_allocator_collect(&allocator);
	CHECK(live_blocks() == 0 && used == 0 && allocator.heaps[1].size == 0, "and then that one: nothing is left");

	/* ---- one use is never cut from another's block ---- */
	memset(&a, 0, sizeof(a));
	memset(&b, 0, sizeof(b));
	memset(&c, 0, sizeof(c));
	CHECK(granite_device_allocator_allocate(&allocator, 5000, 16, GRANITE_ALLOCATION_OPTIMAL_RESOURCE, TYPE_DEVICE, &a)
			&& granite_device_allocator_allocate(&allocator, 5000, 16, GRANITE_ALLOCATION_OPTIMAL_RENDER_TARGET, TYPE_DEVICE, &b)
			&& granite_device_allocator_allocate(&allocator, 5000, 16, GRANITE_ALLOCATION_LINEAR_DEVICE, TYPE_HOST, &c),
			"three of different modes and types");
	CHECK(a.base != b.base && a.base != c.base && b.base != c.base && live_blocks() == 3,
			"are in three blocks");
	CHECK(a.host_base == NULL && b.host_base == NULL && !blocks[block_of(a.base)].mapped,
			"memory that is not host visible is not mapped");
	CHECK(c.host_base != NULL, "linear device memory is, where its type can be");
	CHECK(blocks[block_of(a.base)].priority == 0.5f && blocks[block_of(b.base)].priority == 1.0f,
			"render targets are given the highest priority, other resources half");
	CHECK(granite_device_allocator_map(&allocator, &a, GRANITE_MEMORY_ACCESS_READ_BIT, 0, 16) == NULL,
			"and there is nothing to map of what is not");

	/* ---- not coherent: in and out in whole atoms (64 bytes here) ---- */
	invalidate_calls = flush_calls = 0;
	CHECK(granite_device_allocator_map(&allocator, &c, GRANITE_MEMORY_ACCESS_WRITE_BIT, 10, 100) == c.host_base + 10
			&& invalidate_calls == 0, "mapping to write brings nothing in");
	CHECK(granite_device_allocator_map(&allocator, &c, GRANITE_MEMORY_ACCESS_READ_BIT, 10, 100) == c.host_base + 10
			&& invalidate_calls == 1 && invalidated.memory == c.base
			&& invalidated.offset == ((c.offset + 10) & ~(VkDeviceSize)63)
			&& invalidated.offset + invalidated.size >= c.offset + 110
			&& invalidated.size % 64 == 0 && invalidated.size <= 192,
			"mapping to read brings in the atoms the range is in");
	granite_device_allocator_unmap(&allocator, &c, GRANITE_MEMORY_ACCESS_READ_BIT, 10, 100);
	CHECK(flush_calls == 0, "unmapping after a read sends nothing");
	granite_device_allocator_unmap(&allocator, &c, GRANITE_MEMORY_ACCESS_WRITE_BIT, 10, 100);
	CHECK(flush_calls == 1 && flushed.memory == c.base && flushed.offset == invalidated.offset
			&& flushed.size == invalidated.size, "after a write, the same atoms");

	{
		/* a third on the device's heap, for what follows */
		granite_allocation_t d;
		unsigned first = (unsigned)block_of(a.base), second = (unsigned)block_of(b.base);
		memset(&d, 0, sizeof(d));
		CHECK(granite_device_allocator_allocate(&allocator, 5000, 16, GRANITE_ALLOCATION_LINEAR_DEVICE, TYPE_DEVICE, &d),
				"a fourth");
		granite_allocation_free(&a, &allocator);
		granite_allocation_free(&b, &allocator);
		granite_allocation_free(&c, &allocator);
		granite_allocation_free(&d, &allocator);
		CHECK(live_blocks() == 4 && free_calls == before, "freed, their four blocks are kept");

		/* ---- out of memory: the kept blocks go, oldest first, until it fits ---- */
		/* the device holds the four kept 64 MiB blocks and nothing more;
		 * 96 MiB of its own needs two of the three on its heap gone */
		capacity = 256 * MIB;
		num_freed = 0;
		before = allocate_calls;
		memset(&a, 0, sizeof(a));
		CHECK(granite_device_allocator_allocate(&allocator, (uint32_t)(96 * MIB), 1,
					GRANITE_ALLOCATION_OPTIMAL_RESOURCE, TYPE_DEVICE, &a),
				"an allocation the device has no room for succeeds");
		CHECK(num_freed == 2 && allocate_calls == before + 3 && live_blocks() == 3,
				"by freeing kept blocks until it fits, and no more than that");
		CHECK(freed_order[0] == first && freed_order[1] == second, "the oldest first");
	}
	CHECK(a.alloc == NULL && a.size == 96 * MIB && a.offset == 0, "too large to cut: a block of its own");
	granite_allocation_free(&a, &allocator);
	CHECK(live_blocks() == 2 && num_freed == 3, "which is freed at once, not kept");

	/* nothing kept can make room: everything on the heap goes, and it fails */
	capacity = 128 * MIB;
	num_freed = 0;
	CHECK(!granite_device_allocator_allocate(&allocator, (uint32_t)(96 * MIB), 1, GRANITE_ALLOCATION_OPTIMAL_RESOURCE,
				TYPE_DEVICE, &a) && num_freed == 1 && live_blocks() == 1 && used == 64 * MIB,
			"one that cannot fit fails with everything kept on its heap freed");
	num_freed = 0;
	before = allocate_calls;
	CHECK(!granite_device_allocator_allocate(&allocator, (uint32_t)(96 * MIB), 1, GRANITE_ALLOCATION_OPTIMAL_RESOURCE,
				TYPE_DEVICE, &a) && num_freed == 0 && allocate_calls == before + 1,
			"and with nothing kept, after the one try");
	/* a small one whose block cannot be had */
	capacity = 0;
	memset(&a, 0, sizeof(a));
	CHECK(!granite_device_allocator_allocate(&allocator, 100, 1, GRANITE_ALLOCATION_LINEAR_HOST_MAPPABLE, TYPE_HOST, &a)
			&& live_blocks() == 0, "a small allocation without a block to cut it from fails");
	capacity = 1024 * 1024 * MIB;
	CHECK(granite_device_allocator_allocate(&allocator, 100, 1, GRANITE_ALLOCATION_LINEAR_HOST_MAPPABLE, TYPE_HOST, &a),
			"and succeeds once there is room: the failure left nothing half made");
	granite_allocation_free(&a, &allocator);
	granite_device_allocator_collect(&allocator);

	/* a block that cannot be mapped */
	fail_map = true;
	CHECK(!granite_device_allocator_allocate(&allocator, 100, 1, GRANITE_ALLOCATION_LINEAR_HOST_MAPPABLE, TYPE_HOST, &a)
			&& live_blocks() == 0 && used == 0 && allocator.heaps[1].size == 0,
			"a block that cannot be mapped is freed, and the allocation fails");
	fail_map = false;

	/* ---- images and buffers ---- */
	memset(&a, 0, sizeof(a));
	memset(&b, 0, sizeof(b));
	prefer_dedicated = false;
	CHECK(granite_device_allocator_allocate_image(&allocator, 4096, 4096, GRANITE_ALLOCATION_OPTIMAL_RESOURCE,
				TYPE_DEVICE, (VkImage)GRANITE_VK_FROM_BITS(VkImage, 0x77), &a)
			&& a.alloc != NULL && blocks[block_of(a.base)].dedicated_image == NULL,
			"an image the driver wants no block for is cut from one");
	prefer_dedicated = true;
	CHECK(granite_device_allocator_allocate_image(&allocator, 4096, 4096, GRANITE_ALLOCATION_OPTIMAL_RESOURCE,
				TYPE_DEVICE, (VkImage)GRANITE_VK_FROM_BITS(VkImage, 0x77), &b)
			&& b.alloc == NULL && blocks[block_of(b.base)].dedicated_image == (const void *)(size_t)0x77
			&& blocks[block_of(b.base)].size == 4096,
			"one it prefers a block for gets a block of its own, made for it");
	CHECK(blocks[block_of(b.base)].priority == 0.5f, "with the priority of its mode");
	before = live_blocks();
	granite_allocation_free(&b, &allocator);
	CHECK(live_blocks() == before - 1, "freed at once");
	granite_allocation_free(&a, &allocator);
	prefer_dedicated = false;
	granite_device_allocator_collect(&allocator);
	CHECK(live_blocks() == 0, "nothing left");

	/* ---- the heap to stay within keeps nothing ---- */
	memset(&a, 0, sizeof(a));
	CHECK(granite_device_allocator_allocate(&allocator, 100, 1, GRANITE_ALLOCATION_LINEAR_HOST_MAPPABLE, TYPE_BAR, &a)
			&& live_blocks() == 1, "an allocation on the small heap");
	granite_allocation_free(&a, &allocator);
	CHECK(live_blocks() == 0, "freed, its block goes back to the driver");
	memset(&a, 0, sizeof(a));
	CHECK(!granite_device_allocator_allocate(&allocator, (uint32_t)(200 * MIB), 1, GRANITE_ALLOCATION_LINEAR_HOST_MAPPABLE,
				TYPE_BAR, &a) && live_blocks() == 0, "and one over its budget is refused");

	/* ---- what the allocator cannot take ---- */
	CHECK(!granite_device_allocator_allocate(&allocator, 100, 1, GRANITE_ALLOCATION_LINEAR_DEVICE, 9, &a),
			"a memory type the device does not have");

	/* ---- the driver's budget: what is only kept goes before it is passed ---- */
	device_init(true);
	reported_budget = 100 * MIB;
	memset(&a, 0, sizeof(a));
	memset(&b, 0, sizeof(b));
	CHECK(granite_device_allocator_allocate(&allocator, 100, 1, GRANITE_ALLOCATION_OPTIMAL_RESOURCE, TYPE_DEVICE, &a),
			"within the budget the driver reports");
	granite_allocation_free(&a, &allocator);
	CHECK(live_blocks() == 1, "its block is kept");
	num_freed = 0;
	CHECK(granite_device_allocator_allocate(&allocator, 100, 1, GRANITE_ALLOCATION_OPTIMAL_RENDER_TARGET, TYPE_DEVICE, &b)
			&& num_freed == 1 && live_blocks() == 1,
			"a block that would pass the budget is allocated after the kept one is freed");
	granite_allocation_free(&b, &allocator);
	reported_budget = 1024 * 1024 * MIB;

	/* ---- again, and down ---- */
	device_init(false);
	memset(&a, 0, sizeof(a));
	CHECK(granite_device_allocator_allocate(&allocator, 3000, 256, GRANITE_ALLOCATION_LINEAR_DEVICE, TYPE_DEVICE, &a)
			&& (a.offset & 255) == 0, "initialised again, it allocates");
	granite_allocation_free(&a, &allocator);
	granite_device_allocator_free(&allocator);
	CHECK(live_blocks() == 0 && used == 0, "freeing the allocator frees the blocks it kept");
	granite_device_allocator_free(&allocator);

	if (failures)
	{
		fprintf(stderr, "test_vk_memory_allocator: %u failed\n", failures);
		return 1;
	}
	printf("test_vk_memory_allocator: ok\n");
	return 0;
}
