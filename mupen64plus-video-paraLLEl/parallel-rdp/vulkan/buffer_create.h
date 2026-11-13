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

#ifndef GRANITE_BUFFER_CREATE_H
#define GRANITE_BUFFER_CREATE_H

#include <stddef.h>
#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>
#include "volk.h"
#include "memory_allocator.h"

RETRO_BEGIN_DECLS

#define GRANITE_NO_MEMORY_TYPE 0xffffffffu

/* Where a buffer's memory should be */
typedef enum granite_buffer_domain
{
	/* device local; probably not visible from the CPU */
	GRANITE_BUFFER_DOMAIN_DEVICE = 0,
	/* device memory the CPU can map (on a desktop card, VRAM over PCI) */
	GRANITE_BUFFER_DOMAIN_LINKED_DEVICE_HOST,
	/* that, or device local if there is none */
	GRANITE_BUFFER_DOMAIN_LINKED_DEVICE_HOST_PREFER_DEVICE,
	/* host memory; to be copied to the GPU, unless it is one with it */
	GRANITE_BUFFER_DOMAIN_HOST,
	GRANITE_BUFFER_DOMAIN_CACHED_HOST,
	/* cached and coherent; coherent if it cannot be both */
	GRANITE_BUFFER_DOMAIN_CACHED_COHERENT_HOST_PREFER_COHERENT,
	/* cached and coherent; cached if it cannot be both */
	GRANITE_BUFFER_DOMAIN_CACHED_COHERENT_HOST_PREFER_CACHED
} granite_buffer_domain_t;

/* What creating a buffer needs of the device */
typedef struct granite_buffer_env
{
	VkDevice device;
	const struct VolkDeviceTable *table;
	granite_device_allocator_t *allocator;
	const VkPhysicalDeviceMemoryProperties *memory;
	/* the family of each of the device's queues; VK_QUEUE_FAMILY_IGNORED
	 * for one it does not have */
	const uint32_t *queue_families;
	unsigned num_queue_families;
	/* give host memory that is cached, whatever is asked for: for a
	 * driver whose uncached memory is slow to read back */
	bool force_host_cached;
} granite_buffer_env_t;

/* The first memory type of @mask with all of @required, or
 * GRANITE_NO_MEMORY_TYPE */
uint32_t granite_find_memory_type(const VkPhysicalDeviceMemoryProperties *memory, uint32_t required, uint32_t mask);
/* The best memory type of @mask for @domain: what it asks for, or what
 * comes closest. GRANITE_NO_MEMORY_TYPE when none will do. */
uint32_t granite_find_buffer_memory_type(const VkPhysicalDeviceMemoryProperties *memory,
		granite_buffer_domain_t domain, uint32_t mask, bool force_host_cached);
/* The type for an image's memory: device local, or whatever it is
 * allowed */
uint32_t granite_find_image_memory_type(const VkPhysicalDeviceMemoryProperties *memory, uint32_t mask);

/* The distinct families among @families, in @unique (room for @count);
 * returns how many. More than one: what is shared between the queues is
 * created VK_SHARING_MODE_CONCURRENT with them. */
uint32_t granite_unique_queue_families(const uint32_t *families, unsigned count, uint32_t *unique);

/* The stages and accesses a buffer of @usage can be used in */
VkPipelineStageFlags granite_buffer_usage_stages(VkBufferUsageFlags usage);
VkAccessFlags granite_buffer_usage_access(VkBufferUsageFlags usage);

/* Creates a buffer of @size for @usage (and for transfers both ways),
 * shared between the device's queue families, with memory for @domain
 * bound to it. When that memory has run out, a linked device and host
 * domain falls back on host or device memory. @memory_type gets the
 * type it got. False, and nothing left behind, when it cannot be made. */
bool granite_buffer_create(const granite_buffer_env_t *env, granite_buffer_domain_t domain, VkDeviceSize size,
		VkBufferUsageFlags usage, VkBuffer *buffer, granite_allocation_t *allocation, uint32_t *memory_type);

/* A buffer of @size for @usage over the host's own memory at
 * @host_pointer (VK_EXT_external_memory_host), which stays the host's:
 * what the device writes there the host reads, with no copy.
 * @host_alignment is the device's minImportedHostPointerAlignment.
 * @domain is a host one. False, and nothing left behind, when the
 * pointer is not aligned, the driver cannot import it, or it cannot be
 * made. */
bool granite_buffer_create_host_import(const granite_buffer_env_t *env, granite_buffer_domain_t domain,
		VkDeviceSize size, VkBufferUsageFlags usage, VkDeviceSize host_alignment, void *host_pointer,
		VkBuffer *buffer, granite_allocation_t *allocation);

RETRO_END_DECLS

#endif
