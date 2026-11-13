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
#include <string.h>
#include <assert.h>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

#include "memory_allocator.h"
#include "logging.h"

/* ---- the lists of blocks ---- */

static void list_insert_front(granite_mini_heap_t **list, granite_mini_heap_t *node)
{
	node->prev = NULL;
	node->next = *list;
	if (*list)
		(*list)->prev = node;
	*list = node;
}

static void list_erase(granite_mini_heap_t **list, granite_mini_heap_t *node)
{
	if (node->prev)
		node->prev->next = node->next;
	else
		*list = node->next;
	if (node->next)
		node->next->prev = node->prev;
}

static void list_move_to_front(granite_mini_heap_t **to, granite_mini_heap_t **from, granite_mini_heap_t *node)
{
	list_erase(from, node);
	list_insert_front(to, node);
}

/* 32 for none */
static INLINE uint32_t lowest_bit(uint32_t x)
{
	if (!x)
		return 32;
#if defined(_MSC_VER)
	{
		unsigned long r;
		_BitScanForward(&r, x);
		return (uint32_t)r;
	}
#else
	return (uint32_t)__builtin_ctz(x);
#endif
}

/* ---- the driver's blocks ---- */

static void heap_collect(granite_device_allocator_t *allocator, granite_memory_heap_t *heap)
{
	size_t i;
	for (i = 0; i < heap->count; i++)
	{
		allocator->table->vkFreeMemory(allocator->device, heap->blocks[i].memory, NULL);
		heap->size -= heap->blocks[i].size;
	}
	heap->count = 0;
}

static void budget_now(granite_device_allocator_t *allocator, granite_heap_budget_t *budgets)
{
	uint32_t num_heaps = allocator->mem_props.memoryHeapCount;
	uint32_t i;

	if (allocator->supports_memory_budget)
	{
		VkPhysicalDeviceMemoryProperties2 props;
		VkPhysicalDeviceMemoryBudgetPropertiesEXT budget_props;
		memset(&props, 0, sizeof(props));
		memset(&budget_props, 0, sizeof(budget_props));
		props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
		budget_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;
		props.pNext = &budget_props;
		vkGetPhysicalDeviceMemoryProperties2(allocator->gpu, &props);

		for (i = 0; i < num_heaps; i++)
		{
			budgets[i].max_size = allocator->mem_props.memoryHeaps[i].size;
			budgets[i].budget_size = budget_props.heapBudget[i];
			budgets[i].device_usage = budget_props.heapUsage[i];
			budgets[i].tracked_usage = allocator->heaps[i].size;
		}
	}
	else
	{
		for (i = 0; i < num_heaps; i++)
		{
			budgets[i].max_size = allocator->mem_props.memoryHeaps[i].size;
			/* three quarters of it */
			budgets[i].budget_size = budgets[i].max_size - (budgets[i].max_size / 4);
			budgets[i].tracked_usage = allocator->heaps[i].size;
			budgets[i].device_usage = allocator->heaps[i].size;
		}
	}
}

void granite_device_allocator_budget(granite_device_allocator_t *allocator, granite_heap_budget_t *heaps)
{
	budget_now(allocator, heaps);
}

static void log_heap_budget(uint32_t size, const granite_heap_budget_t *budget)
{
	LOGW("  Size: %u MiB.\n", (unsigned)(size / (1024 * 1024)));
	LOGW("  Device usage: %u MiB.\n", (unsigned)(budget->device_usage / (1024 * 1024)));
	LOGW("  Tracked usage: %u MiB.\n", (unsigned)(budget->tracked_usage / (1024 * 1024)));
	LOGW("  Budget size: %u MiB.\n", (unsigned)(budget->budget_size / (1024 * 1024)));
	LOGW("  Max size: %u MiB.\n", (unsigned)(budget->max_size / (1024 * 1024)));
}

/* A mode whose memory is mapped where the type allows: host mappable
 * always, the linear device ones on the chance that it does (it does on
 * unified memory). */
static bool mode_request_host_mapping(unsigned mode)
{
	return mode == GRANITE_ALLOCATION_LINEAR_HOST_MAPPABLE
		|| mode == GRANITE_ALLOCATION_LINEAR_DEVICE
		|| mode == GRANITE_ALLOCATION_LINEAR_DEVICE_HIGH_PRIORITY;
}

/* A block from the driver: a cached one of the same size, type and mode,
 * or a new one. @host_memory: where to map it, or NULL. @dedicated_object:
 * the image or buffer (@object_type) it is for alone, or 0. */
static bool internal_allocate(granite_device_allocator_t *allocator, uint32_t size, uint32_t memory_type,
		unsigned mode, VkDeviceMemory *memory, uint8_t **host_memory, VkObjectType object_type,
		uint64_t dedicated_object)
{
	uint32_t heap_index = allocator->mem_props.memoryTypes[memory_type].heapIndex;
	granite_memory_heap_t *heap = &allocator->heaps[heap_index];
	bool host_visible = (allocator->mem_props.memoryTypes[memory_type].propertyFlags
			& VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0 && host_memory != NULL;
	VkMemoryAllocateInfo info;
	VkMemoryDedicatedAllocateInfo dedicated;
	VkMemoryPriorityAllocateInfoEXT priority_info;
	VkDeviceMemory device_memory = VK_NULL_HANDLE;
	VkResult res;
	size_t i;

	/* (few and large: a search through them all is nothing) */
	if (dedicated_object == 0)
	{
		for (i = 0; i < heap->count; i++)
		{
			const granite_cached_allocation_t *block = &heap->blocks[i];
			if (size != block->size || memory_type != block->type || mode != block->mode)
				continue;
			if (host_visible && allocator->table->vkMapMemory(allocator->device, block->memory, 0,
						VK_WHOLE_SIZE, 0, (void **)host_memory) != VK_SUCCESS)
				return false;
			*memory = block->memory;
			memmove(&heap->blocks[i], &heap->blocks[i + 1], (heap->count - i - 1) * sizeof(*block));
			heap->count--;
			return true;
		}
	}

	{
		granite_heap_budget_t budgets[VK_MAX_MEMORY_HEAPS];
		budget_now(allocator, budgets);

		/* about to go over: give back what is only kept */
		if (budgets[heap_index].device_usage + size >= budgets[heap_index].budget_size)
		{
			LOGW("Will exceed memory budget, cleaning up ...\n");
			log_heap_budget(size, &budgets[heap_index]);
			heap_collect(allocator, heap);
		}

		budget_now(allocator, budgets);
		if (budgets[heap_index].device_usage + size >= budgets[heap_index].budget_size)
		{
			LOGW("Even after garbage collection, we will exceed budget ...\n");
			if (allocator->memory_heap_is_budget_critical[heap_index])
				return false;
			log_heap_budget(size, &budgets[heap_index]);
		}
	}

	memset(&info, 0, sizeof(info));
	memset(&dedicated, 0, sizeof(dedicated));
	memset(&priority_info, 0, sizeof(priority_info));
	info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	info.allocationSize = size;
	info.memoryTypeIndex = memory_type;
	dedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
	priority_info.sType = VK_STRUCTURE_TYPE_MEMORY_PRIORITY_ALLOCATE_INFO_EXT;

	if (dedicated_object != 0)
	{
		if (object_type == VK_OBJECT_TYPE_IMAGE)
			dedicated.image = (VkImage)dedicated_object;
		else if (object_type == VK_OBJECT_TYPE_BUFFER)
			dedicated.buffer = (VkBuffer)dedicated_object;
		info.pNext = &dedicated;
	}

	if (allocator->memory_priority)
	{
		switch (mode)
		{
			case GRANITE_ALLOCATION_LINEAR_DEVICE_HIGH_PRIORITY:
			case GRANITE_ALLOCATION_OPTIMAL_RENDER_TARGET:
				priority_info.priority = 1.0f;
				break;
			case GRANITE_ALLOCATION_LINEAR_DEVICE:
			case GRANITE_ALLOCATION_OPTIMAL_RESOURCE:
				priority_info.priority = 0.5f;
				break;
			default:
				priority_info.priority = 0.0f;
				break;
		}
		priority_info.pNext = info.pNext;
		info.pNext = &priority_info;
	}

	res = allocator->table->vkAllocateMemory(allocator->device, &info, NULL, &device_memory);

	/* Out of memory: free cached blocks, of whatever kind, oldest first,
	 * until it fits. */
	if (res != VK_SUCCESS)
	{
		size_t freed = 0;
		while (res != VK_SUCCESS && freed < heap->count)
		{
			allocator->table->vkFreeMemory(allocator->device, heap->blocks[freed].memory, NULL);
			heap->size -= heap->blocks[freed].size;
			freed++;
			res = allocator->table->vkAllocateMemory(allocator->device, &info, NULL, &device_memory);
		}
		memmove(heap->blocks, heap->blocks + freed, (heap->count - freed) * sizeof(*heap->blocks));
		heap->count -= freed;
	}
	if (res != VK_SUCCESS)
		return false;

	heap->size += size;
	if (host_visible && allocator->table->vkMapMemory(allocator->device, device_memory, 0, VK_WHOLE_SIZE,
				0, (void **)host_memory) != VK_SUCCESS)
	{
		allocator->table->vkFreeMemory(allocator->device, device_memory, NULL);
		heap->size -= size;
		return false;
	}
	*memory = device_memory;
	return true;
}

/* Keeps the block for the next request like it */
static void internal_free(granite_device_allocator_t *allocator, uint32_t size, uint32_t memory_type,
		unsigned mode, VkDeviceMemory memory, bool is_mapped)
{
	uint32_t heap_index = allocator->mem_props.memoryTypes[memory_type].heapIndex;
	granite_memory_heap_t *heap = &allocator->heaps[heap_index];
	granite_cached_allocation_t *block;

	if (is_mapped)
		allocator->table->vkUnmapMemory(allocator->device, memory);

	if (heap->count == heap->capacity)
	{
		size_t capacity = heap->capacity ? heap->capacity * 2 : 16;
		granite_cached_allocation_t *grown =
			(granite_cached_allocation_t *)realloc(heap->blocks, capacity * sizeof(*grown));
		/* (nowhere to keep it) */
		if (!grown)
		{
			allocator->table->vkFreeMemory(allocator->device, memory, NULL);
			heap->size -= size;
			return;
		}
		heap->blocks = grown;
		heap->capacity = capacity;
	}
	block = &heap->blocks[heap->count++];
	block->memory = memory;
	block->size = size;
	block->type = memory_type;
	block->mode = (uint8_t)mode;
	if (allocator->memory_heap_is_budget_critical[heap_index])
		heap_collect(allocator, heap);
}

static void internal_free_no_recycle(granite_device_allocator_t *allocator, uint32_t size,
		uint32_t memory_type, VkDeviceMemory memory)
{
	granite_memory_heap_t *heap = &allocator->heaps[allocator->mem_props.memoryTypes[memory_type].heapIndex];
	allocator->table->vkFreeMemory(allocator->device, memory, NULL);
	heap->size -= size;
}

/* ---- blocks cut into sub-blocks ---- */

static bool class_allocate(granite_class_allocator_t *c, uint32_t size, granite_allocation_t *alloc);
static void class_free(granite_class_allocator_t *c, granite_mini_heap_t *heap, uint32_t mask);

/* The memory behind a new block: a run of the next size up, or a block
 * from the driver */
static bool class_allocate_backing(granite_class_allocator_t *c, granite_allocation_t *alloc)
{
	uint32_t alloc_size = c->sub_block_size * GRANITE_LEGION_SUB_BLOCKS;

	if (c->parent)
		return class_allocate(c->parent, alloc_size, alloc);

	alloc->offset = 0;
	alloc->host_base = NULL;
	alloc->mode = c->global_allocator_mode;
	alloc->memory_type = (uint8_t)c->memory_type;
	return internal_allocate(c->global_allocator, alloc_size, c->memory_type, c->global_allocator_mode,
			&alloc->base, mode_request_host_mapping(c->global_allocator_mode) ? &alloc->host_base : NULL,
			VK_OBJECT_TYPE_DEVICE, 0);
}

static void class_free_backing(granite_class_allocator_t *c, granite_allocation_t *alloc)
{
	if (c->parent)
	{
		if (alloc->alloc)
			class_free(alloc->alloc, alloc->heap, alloc->mask);
	}
	else if (alloc->base != VK_NULL_HANDLE)
		internal_free(c->global_allocator, c->sub_block_size * GRANITE_LEGION_SUB_BLOCKS, c->memory_type,
				alloc->mode, alloc->base, alloc->host_base != NULL);
	alloc->alloc = NULL;
	alloc->base = VK_NULL_HANDLE;
	alloc->mask = 0;
	alloc->offset = 0;
}

/* @num_blocks sub-blocks of @heap, which has them */
static void class_take(granite_class_allocator_t *c, granite_allocation_t *alloc, granite_mini_heap_t *heap,
		uint32_t num_blocks)
{
	uint32_t mask, offset;
	granite_legion_allocate(&heap->heap, num_blocks, &mask, &offset);
	offset <<= c->sub_block_size_log2;

	alloc->base = heap->allocation.base;
	alloc->offset = offset + heap->allocation.offset;
	alloc->mask = mask;
	alloc->size = num_blocks << c->sub_block_size_log2;
	alloc->host_base = heap->allocation.host_base ? heap->allocation.host_base + offset : NULL;
	alloc->mode = c->global_allocator_mode;
	alloc->memory_type = (uint8_t)c->memory_type;
	alloc->alloc = c;
	alloc->heap = heap;
}

static bool class_allocate(granite_class_allocator_t *c, uint32_t size, granite_allocation_t *alloc)
{
	uint32_t num_blocks = (size + c->sub_block_size - 1) >> c->sub_block_size_log2;
	uint32_t size_mask, index;
	granite_mini_heap_t *heap;

	if (!num_blocks)
		num_blocks = 1;
	size_mask = (1u << (num_blocks - 1)) - 1;
	/* the block with the shortest free run that is long enough */
	index = lowest_bit(c->heap_availability_mask & ~size_mask);

	if (index < GRANITE_LEGION_SUB_BLOCKS)
	{
		uint32_t new_index;
		heap = c->heaps[index];
		class_take(c, alloc, heap, num_blocks);

		if (granite_legion_full(&heap->heap))
		{
			list_move_to_front(&c->full_heaps, &c->heaps[index], heap);
			if (!c->heaps[index])
				c->heap_availability_mask &= ~(1u << index);
		}
		else if ((new_index = granite_legion_longest_run(&heap->heap) - 1) != index)
		{
			list_move_to_front(&c->heaps[new_index], &c->heaps[index], heap);
			c->heap_availability_mask |= 1u << new_index;
			if (!c->heaps[index])
				c->heap_availability_mask &= ~(1u << index);
		}
		return true;
	}

	/* none has room: a new block */
	heap = (granite_mini_heap_t *)granite_pool_alloc(c->object_pool);
	if (!heap)
		return false;
	memset(heap, 0, sizeof(*heap));
	granite_legion_init(&heap->heap);
	if (!class_allocate_backing(c, &heap->allocation))
	{
		granite_pool_release(c->object_pool, heap);
		return false;
	}

	class_take(c, alloc, heap, num_blocks);
	if (granite_legion_full(&heap->heap))
		list_insert_front(&c->full_heaps, heap);
	else
	{
		uint32_t new_index = granite_legion_longest_run(&heap->heap) - 1;
		list_insert_front(&c->heaps[new_index], heap);
		c->heap_availability_mask |= 1u << new_index;
	}
	return true;
}

static void class_free(granite_class_allocator_t *c, granite_mini_heap_t *heap, uint32_t mask)
{
	granite_legion_t *block = &heap->heap;
	bool was_full = granite_legion_full(block);
	/* (a full block has no run: it is on no list of them) */
	uint32_t index = was_full ? 0 : granite_legion_longest_run(block) - 1;
	uint32_t new_index;

	granite_legion_free(block, mask);
	new_index = granite_legion_longest_run(block) - 1;

	if (granite_legion_empty(block))
	{
		/* all of it free: back to where it came from */
		class_free_backing(c, &heap->allocation);
		if (was_full)
			list_erase(&c->full_heaps, heap);
		else
		{
			list_erase(&c->heaps[index], heap);
			if (!c->heaps[index])
				c->heap_availability_mask &= ~(1u << index);
		}
		granite_pool_release(c->object_pool, heap);
	}
	else if (was_full)
	{
		list_move_to_front(&c->heaps[new_index], &c->full_heaps, heap);
		c->heap_availability_mask |= 1u << new_index;
	}
	else if (index != new_index)
	{
		list_move_to_front(&c->heaps[new_index], &c->heaps[index], heap);
		c->heap_availability_mask |= 1u << new_index;
		if (!c->heaps[index])
			c->heap_availability_mask &= ~(1u << index);
	}
}

/* ---- a memory type ---- */

static void type_allocator_init(granite_type_allocator_t *t, granite_device_allocator_t *allocator,
		uint32_t memory_type)
{
	/* 128 bytes, 4 KiB, 128 KiB, 2 MiB */
	static const uint32_t sub_block_log2[GRANITE_MEMORY_CLASS_COUNT] = { 7, 12, 17, 21 };
	unsigned i, j;

	memset(t, 0, sizeof(*t));
	t->global_allocator = allocator;
	t->memory_type = memory_type;
	for (i = 0; i < GRANITE_MEMORY_CLASS_COUNT; i++)
	{
		for (j = 0; j < GRANITE_ALLOCATION_MODE_COUNT; j++)
		{
			granite_class_allocator_t *c = &t->classes[i][j];
			c->object_pool = &allocator->object_pool;
			c->sub_block_size_log2 = sub_block_log2[i];
			c->sub_block_size = 1u << sub_block_log2[i];
			c->parent = i + 1 < GRANITE_MEMORY_CLASS_COUNT ? &t->classes[i + 1][j] : NULL;
			c->global_allocator = allocator;
			c->global_allocator_mode = (uint8_t)j;
			c->memory_type = memory_type;
		}
	}
}

/* A block of its own, never kept for reuse */
static bool type_allocate_dedicated(granite_type_allocator_t *t, uint32_t size, unsigned mode,
		granite_allocation_t *alloc, VkObjectType object_type, uint64_t object)
{
	alloc->host_base = NULL;
	if (!internal_allocate(t->global_allocator, size, t->memory_type, mode, &alloc->base,
				mode_request_host_mapping(mode) ? &alloc->host_base : NULL, object_type, object))
		return false;
	alloc->mode = (uint8_t)mode;
	alloc->alloc = NULL;
	alloc->heap = NULL;
	alloc->offset = 0;
	alloc->mask = 0;
	alloc->memory_type = (uint8_t)t->memory_type;
	alloc->size = size;
	return true;
}

static bool type_allocate(granite_type_allocator_t *t, uint32_t size, uint32_t alignment, unsigned mode,
		granite_allocation_t *alloc)
{
	unsigned i;
	if (!alignment)
		alignment = 1;
	for (i = 0; i < GRANITE_MEMORY_CLASS_COUNT; i++)
	{
		granite_class_allocator_t *c = &t->classes[i][mode];
		uint32_t max_size = c->sub_block_size * GRANITE_LEGION_SUB_BLOCKS;
		uint32_t aligned_offset;

		/* the smallest size it fits */
		if (size > max_size)
			continue;
		if (alignment > c->sub_block_size)
		{
			/* (its sub-blocks are aligned to their size: room to move up) */
			uint64_t padded_size = (uint64_t)size + (alignment - c->sub_block_size);
			if (padded_size > max_size)
				continue;
			size = (uint32_t)padded_size;
		}

		if (!class_allocate(c, size, alloc))
			return false;
		aligned_offset = (alloc->offset + alignment - 1) & ~(alignment - 1);
		if (alloc->host_base)
			alloc->host_base += aligned_offset - alloc->offset;
		alloc->offset = aligned_offset;
		return true;
	}

	return type_allocate_dedicated(t, size, mode, alloc, VK_OBJECT_TYPE_DEVICE, 0);
}

/* ---- the device ---- */

static void free_allocators(granite_device_allocator_t *allocator)
{
	unsigned i, j, k, n;
	bool leaked = false;
	for (i = 0; i < allocator->num_allocators; i++)
	{
		granite_type_allocator_t *t = allocator->allocators[i];
		for (j = 0; t && j < GRANITE_MEMORY_CLASS_COUNT; j++)
			for (k = 0; k < GRANITE_ALLOCATION_MODE_COUNT; k++)
			{
				if (t->classes[j][k].full_heaps)
					leaked = true;
				for (n = 0; n < GRANITE_LEGION_SUB_BLOCKS; n++)
					if (t->classes[j][k].heaps[n])
						leaked = true;
			}
		free(t);
		allocator->allocators[i] = NULL;
	}
	allocator->num_allocators = 0;
	if (leaked)
		LOGE("Memory leaked in class allocator!\n");
}

void granite_device_allocator_free(granite_device_allocator_t *allocator)
{
	unsigned i;
	if (!allocator->initialized)
		return;
	for (i = 0; i < VK_MAX_MEMORY_HEAPS; i++)
	{
		heap_collect(allocator, &allocator->heaps[i]);
		free(allocator->heaps[i].blocks);
		memset(&allocator->heaps[i], 0, sizeof(allocator->heaps[i]));
	}
	free_allocators(allocator);
	granite_pool_clear(&allocator->object_pool);
	allocator->initialized = false;
}

bool granite_device_allocator_init(granite_device_allocator_t *allocator, VkDevice device,
		VkPhysicalDevice gpu, const struct VolkDeviceTable *table,
		const VkPhysicalDeviceMemoryProperties *mem_props, VkDeviceSize atom_alignment,
		bool supports_memory_budget, bool memory_priority)
{
	const VkMemoryPropertyFlags pinned_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		| VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
	VkMemoryPropertyFlags combined_flags[VK_MAX_MEMORY_HEAPS];
	bool has_host_only_heap = false;
	bool has_device_only_heap = false;
	VkDeviceSize host_heap_size = 0;
	VkDeviceSize device_heap_size = 0;
	uint32_t i;

	granite_device_allocator_free(allocator);
	memset(allocator, 0, sizeof(*allocator));
	granite_pool_init(&allocator->object_pool, sizeof(granite_mini_heap_t), sizeof(void *) > 8 ? sizeof(void *) : 8);
	allocator->device = device;
	allocator->gpu = gpu;
	allocator->table = table;
	allocator->mem_props = *mem_props;
	allocator->atom_alignment = atom_alignment ? atom_alignment : 1;
	allocator->supports_memory_budget = supports_memory_budget;
	allocator->memory_priority = memory_priority;
	allocator->initialized = true;

	for (i = 0; i < mem_props->memoryTypeCount; i++)
	{
		allocator->allocators[i] = (granite_type_allocator_t *)malloc(sizeof(granite_type_allocator_t));
		if (!allocator->allocators[i])
		{
			granite_device_allocator_free(allocator);
			return false;
		}
		allocator->num_allocators = i + 1;
		type_allocator_init(allocator->allocators[i], allocator, i);
	}

	/* A heap both device local and host visible, smaller than a heap of
	 * each alone, is a PCI BAR (256 MiB without resizable BAR): going
	 * over its budget makes the system unstable. */
	memset(combined_flags, 0, sizeof(combined_flags));
	for (i = 0; i < mem_props->memoryTypeCount; i++)
		combined_flags[mem_props->memoryTypes[i].heapIndex] |= mem_props->memoryTypes[i].propertyFlags;
	for (i = 0; i < mem_props->memoryHeapCount; i++)
	{
		VkDeviceSize size = mem_props->memoryHeaps[i].size;
		if ((combined_flags[i] & pinned_flags) == VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
		{
			has_host_only_heap = true;
			if (size > host_heap_size)
				host_heap_size = size;
		}
		else if ((combined_flags[i] & pinned_flags) == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
		{
			has_device_only_heap = true;
			if (size > device_heap_size)
				device_heap_size = size;
		}
	}
	if (has_host_only_heap && has_device_only_heap)
		for (i = 0; i < mem_props->memoryHeapCount; i++)
			if (     (combined_flags[i] & pinned_flags) == pinned_flags
					&& mem_props->memoryHeaps[i].size < host_heap_size
					&& mem_props->memoryHeaps[i].size < device_heap_size)
				allocator->memory_heap_is_budget_critical[i] = true;
	return true;
}

bool granite_device_allocator_allocate(granite_device_allocator_t *allocator, uint32_t size, uint32_t alignment,
		granite_allocation_mode_t mode, uint32_t memory_type, granite_allocation_t *alloc)
{
	if (memory_type >= allocator->num_allocators || (unsigned)mode >= GRANITE_ALLOCATION_MODE_COUNT)
		return false;
	return type_allocate(allocator->allocators[memory_type], size, alignment, (unsigned)mode, alloc);
}

bool granite_device_allocator_allocate_image(granite_device_allocator_t *allocator, uint32_t size,
		uint32_t alignment, granite_allocation_mode_t mode, uint32_t memory_type, VkImage image,
		granite_allocation_t *alloc)
{
	VkImageMemoryRequirementsInfo2 info;
	VkMemoryDedicatedRequirements dedicated_req;
	VkMemoryRequirements2 mem_req;

	if (memory_type >= allocator->num_allocators || (unsigned)mode >= GRANITE_ALLOCATION_MODE_COUNT)
		return false;
	memset(&info, 0, sizeof(info));
	memset(&dedicated_req, 0, sizeof(dedicated_req));
	memset(&mem_req, 0, sizeof(mem_req));
	info.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2;
	info.image = image;
	dedicated_req.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS;
	mem_req.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
	mem_req.pNext = &dedicated_req;
	allocator->table->vkGetImageMemoryRequirements2(allocator->device, &info, &mem_req);

	if (dedicated_req.prefersDedicatedAllocation || dedicated_req.requiresDedicatedAllocation)
		return type_allocate_dedicated(allocator->allocators[memory_type], size, (unsigned)mode, alloc,
				VK_OBJECT_TYPE_IMAGE, (uint64_t)image);
	return type_allocate(allocator->allocators[memory_type], size, alignment, (unsigned)mode, alloc);
}

bool granite_device_allocator_import_host(granite_device_allocator_t *allocator, uint32_t size,
		uint32_t memory_type, void *host_pointer, granite_allocation_t *alloc)
{
	VkMemoryAllocateInfo info;
	VkImportMemoryHostPointerInfoEXT import;
	granite_memory_heap_t *heap;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	void *mapped = NULL;

	if (memory_type >= allocator->num_allocators)
		return false;
	heap = &allocator->heaps[allocator->mem_props.memoryTypes[memory_type].heapIndex];

	memset(&info, 0, sizeof(info));
	memset(&import, 0, sizeof(import));
	info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	info.allocationSize = size;
	info.memoryTypeIndex = memory_type;
	info.pNext = &import;
	import.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT;
	import.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
	import.pHostPointer = host_pointer;
	if (allocator->table->vkAllocateMemory(allocator->device, &info, NULL, &memory) != VK_SUCCESS)
		return false;
	if (allocator->table->vkMapMemory(allocator->device, memory, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS)
	{
		allocator->table->vkFreeMemory(allocator->device, memory, NULL);
		return false;
	}

	/* counted like any block of its own: freeing it takes it off again */
	heap->size += size;
	memset(alloc, 0, sizeof(*alloc));
	alloc->base = memory;
	alloc->host_base = (uint8_t*)mapped;
	alloc->size = size;
	alloc->memory_type = (uint8_t)memory_type;
	return true;
}

void granite_allocation_free(granite_allocation_t *alloc, granite_device_allocator_t *allocator)
{
	if (alloc->alloc)
	{
		class_free(alloc->alloc, alloc->heap, alloc->mask);
		alloc->alloc = NULL;
		alloc->heap = NULL;
		alloc->base = VK_NULL_HANDLE;
		alloc->mask = 0;
		alloc->offset = 0;
	}
	else if (alloc->base != VK_NULL_HANDLE)
	{
		internal_free_no_recycle(allocator, alloc->size, alloc->memory_type, alloc->base);
		alloc->base = VK_NULL_HANDLE;
	}
}

void granite_device_allocator_collect(granite_device_allocator_t *allocator)
{
	unsigned i;
	for (i = 0; i < VK_MAX_MEMORY_HEAPS; i++)
		heap_collect(allocator, &allocator->heaps[i]);
}

/* The range of @alloc the device has to hear about, on whole atoms */
static void incoherent_range(const granite_device_allocator_t *allocator, const granite_allocation_t *alloc,
		VkDeviceSize offset, VkDeviceSize length, VkMappedMemoryRange *range)
{
	VkDeviceSize atom = allocator->atom_alignment;
	VkDeviceSize end;
	offset += alloc->offset;
	end = offset + length;
	offset &= ~(atom - 1);
	range->sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
	range->pNext = NULL;
	range->memory = alloc->base;
	range->offset = offset;
	range->size = (end - offset + atom - 1) & ~(atom - 1);
}

void *granite_device_allocator_map(granite_device_allocator_t *allocator, const granite_allocation_t *alloc,
		uint32_t flags, VkDeviceSize offset, VkDeviceSize length)
{
	/* (a type that is device local only) */
	if (!alloc->host_base)
		return NULL;
	if (     (flags & GRANITE_MEMORY_ACCESS_READ_BIT)
			&& !(allocator->mem_props.memoryTypes[alloc->memory_type].propertyFlags
				& VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
	{
		VkMappedMemoryRange range;
		incoherent_range(allocator, alloc, offset, length, &range);
		allocator->table->vkInvalidateMappedMemoryRanges(allocator->device, 1, &range);
	}
	return alloc->host_base + offset;
}

void granite_device_allocator_unmap(granite_device_allocator_t *allocator, const granite_allocation_t *alloc,
		uint32_t flags, VkDeviceSize offset, VkDeviceSize length)
{
	if (!alloc->host_base)
		return;
	if (     (flags & GRANITE_MEMORY_ACCESS_WRITE_BIT)
			&& !(allocator->mem_props.memoryTypes[alloc->memory_type].propertyFlags
				& VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
	{
		VkMappedMemoryRange range;
		incoherent_range(allocator, alloc, offset, length, &range);
		allocator->table->vkFlushMappedMemoryRanges(allocator->device, 1, &range);
	}
}
