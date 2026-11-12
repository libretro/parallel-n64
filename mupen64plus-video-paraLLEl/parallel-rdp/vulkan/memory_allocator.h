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

#ifndef GRANITE_MEMORY_ALLOCATOR_H
#define GRANITE_MEMORY_ALLOCATOR_H

#include <stddef.h>
#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>
#include <retro_inline.h>
#include "volk.h"
#include "legion_allocator.h"
#include "pool.h"

RETRO_BEGIN_DECLS

/* Device memory for buffers and images.
 *
 * Memory comes from the driver in blocks, and small allocations are cut
 * from those: a block is split into 32 sub-blocks by a buddy allocator
 * ("legion"), and there are four sizes of them, each cut from a sub-block
 * run of the next size up. An allocation of up to 32 sub-blocks of 128
 * bytes comes from the smallest, and so on up to 64 MiB blocks; anything
 * larger is a block of its own. Each memory type and allocation mode has
 * its own set, so memory of one use is never cut from a block of another.
 *
 * A block the allocator no longer needs is kept for the next request of
 * the same size, type and mode, until granite_device_allocator_collect
 * or a failed allocation frees it.
 *
 * Belongs to the thread that owns the device: no locking. */

typedef enum granite_memory_class
{
	GRANITE_MEMORY_CLASS_SMALL = 0,
	GRANITE_MEMORY_CLASS_MEDIUM,
	GRANITE_MEMORY_CLASS_LARGE,
	GRANITE_MEMORY_CLASS_HUGE,
	GRANITE_MEMORY_CLASS_COUNT
} granite_memory_class_t;

typedef enum granite_allocation_mode
{
	GRANITE_ALLOCATION_LINEAR_HOST_MAPPABLE = 0,
	GRANITE_ALLOCATION_LINEAR_DEVICE,
	GRANITE_ALLOCATION_LINEAR_DEVICE_HIGH_PRIORITY,
	GRANITE_ALLOCATION_OPTIMAL_RESOURCE,
	GRANITE_ALLOCATION_OPTIMAL_RENDER_TARGET,
	GRANITE_ALLOCATION_EXTERNAL,
	GRANITE_ALLOCATION_MODE_COUNT
} granite_allocation_mode_t;

#define GRANITE_MEMORY_ACCESS_WRITE_BIT 1u
#define GRANITE_MEMORY_ACCESS_READ_BIT  2u

struct granite_class_allocator;
struct granite_mini_heap;
struct granite_device_allocator;

typedef struct granite_allocation
{
	VkDeviceMemory base;
	/* where it is mapped, or NULL */
	uint8_t *host_base;
	/* what it was cut from: NULL for a block of its own */
	struct granite_class_allocator *alloc;
	struct granite_mini_heap *heap;
	uint32_t offset;
	uint32_t mask;
	uint32_t size;
	VkExternalMemoryHandleTypeFlags exportable_types;
	/* granite_allocation_mode_t */
	uint8_t mode;
	uint8_t memory_type;
} granite_allocation_t;

/* A block of 32 sub-blocks and the allocation behind it */
typedef struct granite_mini_heap
{
	struct granite_mini_heap *prev;
	struct granite_mini_heap *next;
	granite_allocation_t allocation;
	granite_legion_t heap;
} granite_mini_heap_t;

/* The blocks of one sub-block size */
typedef struct granite_class_allocator
{
	/* by longest free run - 1; those in use entirely */
	granite_mini_heap_t *heaps[GRANITE_LEGION_SUB_BLOCKS];
	granite_mini_heap_t *full_heaps;
	uint32_t heap_availability_mask;
	granite_pool_t *object_pool;
	uint32_t sub_block_size;
	uint32_t sub_block_size_log2;
	/* its blocks are cut from this one's; NULL: from the driver */
	struct granite_class_allocator *parent;
	struct granite_device_allocator *global_allocator;
	uint32_t memory_type;
	uint8_t global_allocator_mode;
} granite_class_allocator_t;

/* All of one memory type */
typedef struct granite_type_allocator
{
	granite_class_allocator_t classes[GRANITE_MEMORY_CLASS_COUNT][GRANITE_ALLOCATION_MODE_COUNT];
	struct granite_device_allocator *global_allocator;
	uint32_t memory_type;
} granite_type_allocator_t;

typedef struct granite_heap_budget
{
	VkDeviceSize max_size;
	VkDeviceSize budget_size;
	VkDeviceSize tracked_usage;
	VkDeviceSize device_usage;
} granite_heap_budget_t;

/* A freed block kept for reuse */
typedef struct granite_cached_allocation
{
	VkDeviceMemory memory;
	uint32_t size;
	uint32_t type;
	uint8_t mode;
} granite_cached_allocation_t;

typedef struct granite_memory_heap
{
	/* allocated from the driver, cached blocks included */
	uint64_t size;
	granite_cached_allocation_t *blocks;
	size_t count;
	size_t capacity;
} granite_memory_heap_t;

/* Memory shared with another API or process */
typedef struct granite_external_memory
{
	/* a handle to import: the allocation takes it. Otherwise the memory
	 * is made exportable as @handle_type. */
	bool import;
#ifdef _WIN32
	void *handle;
#else
	int handle;
#endif
	VkExternalMemoryHandleTypeFlagBits handle_type;
} granite_external_memory_t;

typedef struct granite_device_allocator
{
	granite_pool_t object_pool;
	granite_type_allocator_t *allocators[VK_MAX_MEMORY_TYPES];
	unsigned num_allocators;
	VkDevice device;
	VkPhysicalDevice gpu;
	const struct VolkDeviceTable *table;
	VkPhysicalDeviceMemoryProperties mem_props;
	VkDeviceSize atom_alignment;
	/* VK_EXT_memory_budget, VK_EXT_memory_priority */
	bool supports_memory_budget;
	bool memory_priority;
	bool initialized;
	granite_memory_heap_t heaps[VK_MAX_MEMORY_HEAPS];
	/* a small heap both device local and host visible beside larger ones
	 * of each alone (a 256 MiB PCI BAR): never over its budget, nothing
	 * cached */
	bool memory_heap_is_budget_critical[VK_MAX_MEMORY_HEAPS];
} granite_device_allocator_t;

/* @allocator: zeroed, or initialised before (it is freed first).
 * @atom_alignment: the device's nonCoherentAtomSize. False when out of
 * memory. */
bool granite_device_allocator_init(granite_device_allocator_t *allocator, VkDevice device,
		VkPhysicalDevice gpu, const struct VolkDeviceTable *table,
		const VkPhysicalDeviceMemoryProperties *mem_props, VkDeviceSize atom_alignment,
		bool supports_memory_budget, bool memory_priority);
/* Frees the cached blocks and the allocator's own memory. Every
 * allocation must have been freed. */
void granite_device_allocator_free(granite_device_allocator_t *allocator);

/* Each fills @alloc and returns true, or returns false.
 * @alignment: a power of two. */
bool granite_device_allocator_allocate(granite_device_allocator_t *allocator, uint32_t size, uint32_t alignment,
		granite_allocation_mode_t mode, uint32_t memory_type, granite_allocation_t *alloc);
/* For @buffer: its own block when the mode is external */
bool granite_device_allocator_allocate_buffer(granite_device_allocator_t *allocator, uint32_t size,
		uint32_t alignment, granite_allocation_mode_t mode, uint32_t memory_type, VkBuffer buffer,
		granite_allocation_t *alloc, granite_external_memory_t *external);
/* For @image: its own block when the driver prefers that, or the mode is
 * external, unless @force_no_dedicated */
bool granite_device_allocator_allocate_image(granite_device_allocator_t *allocator, uint32_t size,
		uint32_t alignment, granite_allocation_mode_t mode, uint32_t memory_type, VkImage image,
		bool force_no_dedicated, granite_allocation_t *alloc, granite_external_memory_t *external);
/* Gives @alloc's memory back: to the block it was cut from, or, a block
 * of its own, to the driver. */
void granite_allocation_free(granite_allocation_t *alloc, granite_device_allocator_t *allocator);

/* Frees the cached blocks */
void granite_device_allocator_collect(granite_device_allocator_t *allocator);

/* @offset, @length: within the allocation. @flags: GRANITE_MEMORY_ACCESS_*:
 * what is read is brought in from the device before, what was written
 * sent to it after, where the memory type needs that. NULL when the
 * allocation is not mapped. */
void *granite_device_allocator_map(granite_device_allocator_t *allocator, const granite_allocation_t *alloc,
		uint32_t flags, VkDeviceSize offset, VkDeviceSize length);
void granite_device_allocator_unmap(granite_device_allocator_t *allocator, const granite_allocation_t *alloc,
		uint32_t flags, VkDeviceSize offset, VkDeviceSize length);

/* heaps[memoryHeapCount] */
void granite_device_allocator_budget(granite_device_allocator_t *allocator, granite_heap_budget_t *heaps);

RETRO_END_DECLS

#endif
