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

#include <string.h>
#include "buffer_create.h"
#include "logging.h"

#define HOST_VISIBLE VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
#define HOST_COHERENT VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
#define HOST_CACHED VK_MEMORY_PROPERTY_HOST_CACHED_BIT
#define DEVICE_LOCAL VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT

uint32_t granite_find_memory_type(const VkPhysicalDeviceMemoryProperties *memory, uint32_t required, uint32_t mask)
{
	uint32_t i;

	for (i = 0; i < memory->memoryTypeCount; i++)
		if ((mask & ((uint32_t)1 << i)) && (memory->memoryTypes[i].propertyFlags & required) == required)
			return i;
	return GRANITE_NO_MEMORY_TYPE;
}

uint32_t granite_find_buffer_memory_type(const VkPhysicalDeviceMemoryProperties *memory,
		granite_buffer_domain_t domain, uint32_t mask, bool force_host_cached)
{
	/* what to ask for, most wanted first */
	uint32_t prio[3];
	unsigned i;

	if (force_host_cached)
	{
		switch (domain)
		{
			case GRANITE_BUFFER_DOMAIN_LINKED_DEVICE_HOST_PREFER_DEVICE:
				domain = GRANITE_BUFFER_DOMAIN_DEVICE;
				break;
			case GRANITE_BUFFER_DOMAIN_LINKED_DEVICE_HOST:
			case GRANITE_BUFFER_DOMAIN_HOST:
			case GRANITE_BUFFER_DOMAIN_CACHED_COHERENT_HOST_PREFER_COHERENT:
				domain = GRANITE_BUFFER_DOMAIN_CACHED_COHERENT_HOST_PREFER_CACHED;
				break;
			default:
				break;
		}
	}

	switch (domain)
	{
		case GRANITE_BUFFER_DOMAIN_LINKED_DEVICE_HOST:
			prio[0] = HOST_VISIBLE | DEVICE_LOCAL | HOST_COHERENT;
			prio[1] = HOST_VISIBLE | HOST_COHERENT;
			prio[2] = prio[1];
			break;
		case GRANITE_BUFFER_DOMAIN_LINKED_DEVICE_HOST_PREFER_DEVICE:
			prio[0] = HOST_VISIBLE | DEVICE_LOCAL | HOST_COHERENT;
			prio[1] = DEVICE_LOCAL;
			prio[2] = prio[1];
			break;
		case GRANITE_BUFFER_DOMAIN_HOST:
			prio[0] = HOST_VISIBLE | HOST_COHERENT;
			prio[1] = HOST_VISIBLE;
			prio[2] = prio[1];
			break;
		case GRANITE_BUFFER_DOMAIN_CACHED_HOST:
			prio[0] = HOST_VISIBLE | HOST_CACHED;
			prio[1] = HOST_VISIBLE;
			prio[2] = prio[1];
			break;
		case GRANITE_BUFFER_DOMAIN_CACHED_COHERENT_HOST_PREFER_CACHED:
			prio[0] = HOST_VISIBLE | HOST_CACHED | HOST_COHERENT;
			prio[1] = HOST_VISIBLE | HOST_CACHED;
			prio[2] = HOST_VISIBLE;
			break;
		case GRANITE_BUFFER_DOMAIN_CACHED_COHERENT_HOST_PREFER_COHERENT:
			prio[0] = HOST_VISIBLE | HOST_CACHED | HOST_COHERENT;
			prio[1] = HOST_VISIBLE | HOST_COHERENT;
			prio[2] = HOST_VISIBLE;
			break;
		case GRANITE_BUFFER_DOMAIN_DEVICE:
		default:
			/* any type will do if none is device local */
			prio[0] = DEVICE_LOCAL;
			prio[1] = 0;
			prio[2] = 0;
			break;
	}

	for (i = 0; i < 3; i++)
	{
		uint32_t index = granite_find_memory_type(memory, prio[i], mask);

		if (index != GRANITE_NO_MEMORY_TYPE)
			return index;
	}
	return GRANITE_NO_MEMORY_TYPE;
}

uint32_t granite_find_image_memory_type(const VkPhysicalDeviceMemoryProperties *memory, uint32_t mask)
{
	uint32_t index = granite_find_memory_type(memory, DEVICE_LOCAL, mask);

	if (index == GRANITE_NO_MEMORY_TYPE)
		index = granite_find_memory_type(memory, 0, mask);
	return index;
}

uint32_t granite_unique_queue_families(const uint32_t *families, unsigned count, uint32_t *unique)
{
	uint32_t num = 0;
	unsigned i;
	uint32_t j;

	for (i = 0; i < count; i++)
	{
		if (families[i] == VK_QUEUE_FAMILY_IGNORED)
			continue;
		for (j = 0; j < num; j++)
			if (unique[j] == families[i])
				break;
		if (j == num)
			unique[num++] = families[i];
	}
	return num;
}

VkPipelineStageFlags granite_buffer_usage_stages(VkBufferUsageFlags usage)
{
	VkPipelineStageFlags flags = 0;

	if (usage & (VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT))
		flags |= VK_PIPELINE_STAGE_TRANSFER_BIT;
	if (usage & (VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT))
		flags |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
	if (usage & VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)
		flags |= VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
	if (usage & (VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT |
				VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT))
		flags |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	if (usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
		flags |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	return flags;
}

VkAccessFlags granite_buffer_usage_access(VkBufferUsageFlags usage)
{
	VkAccessFlags flags = 0;

	if (usage & (VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT))
		flags |= VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
	if (usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
		flags |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
	if (usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT)
		flags |= VK_ACCESS_INDEX_READ_BIT;
	if (usage & VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)
		flags |= VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
	if (usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
		flags |= VK_ACCESS_UNIFORM_READ_BIT;
	if (usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
		flags |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
	return flags;
}

bool granite_buffer_create(const granite_buffer_env_t *env, granite_buffer_domain_t domain, VkDeviceSize size,
		VkBufferUsageFlags usage, VkBuffer *buffer, granite_allocation_t *allocation, uint32_t *memory_type)
{
	const struct VolkDeviceTable *table = env->table;
	VkBufferCreateInfo info;
	VkMemoryRequirements2 reqs;
	VkBufferMemoryRequirementsInfo2 req_info;
	uint32_t sharing_indices[8];
	granite_allocation_mode_t mode;
	uint32_t type;

	*buffer = VK_NULL_HANDLE;
	*memory_type = GRANITE_NO_MEMORY_TYPE;
	if (env->num_queue_families > 8)
		return false;

	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	info.size = size;
	info.usage = usage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	info.queueFamilyIndexCount = granite_unique_queue_families(env->queue_families, env->num_queue_families,
			sharing_indices);
	if (info.queueFamilyIndexCount > 1)
	{
		info.sharingMode = VK_SHARING_MODE_CONCURRENT;
		info.pQueueFamilyIndices = sharing_indices;
	}
	else
		info.queueFamilyIndexCount = 0;

	if (table->vkCreateBuffer(env->device, &info, NULL, buffer) != VK_SUCCESS)
	{
		*buffer = VK_NULL_HANDLE;
		return false;
	}

	memset(&reqs, 0, sizeof(reqs));
	reqs.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
	memset(&req_info, 0, sizeof(req_info));
	req_info.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2;
	req_info.buffer = *buffer;
	table->vkGetBufferMemoryRequirements2(env->device, &req_info, &reqs);

	type = granite_find_buffer_memory_type(env->memory, domain, reqs.memoryRequirements.memoryTypeBits,
			env->force_host_cached);
	if (type == GRANITE_NO_MEMORY_TYPE)
	{
		LOGE("Failed to find memory type.\n");
		goto fail;
	}

	/* what shaders write to is worth keeping in VRAM under pressure */
	if (domain == GRANITE_BUFFER_DOMAIN_DEVICE &&
			(usage & (VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)))
		mode = GRANITE_ALLOCATION_LINEAR_DEVICE_HIGH_PRIORITY;
	else if (domain == GRANITE_BUFFER_DOMAIN_DEVICE || domain == GRANITE_BUFFER_DOMAIN_LINKED_DEVICE_HOST_PREFER_DEVICE)
		mode = GRANITE_ALLOCATION_LINEAR_DEVICE;
	else
		mode = GRANITE_ALLOCATION_LINEAR_HOST_MAPPABLE;

	if (!granite_device_allocator_allocate(env->allocator, (uint32_t)reqs.memoryRequirements.size,
				(uint32_t)reqs.memoryRequirements.alignment, mode, type, allocation))
	{
		/* device memory the host can map is scarce: do without one or
		 * the other */
		granite_buffer_domain_t fallback;

		if (domain == GRANITE_BUFFER_DOMAIN_LINKED_DEVICE_HOST)
			fallback = GRANITE_BUFFER_DOMAIN_HOST;
		else if (domain == GRANITE_BUFFER_DOMAIN_LINKED_DEVICE_HOST_PREFER_DEVICE)
			fallback = GRANITE_BUFFER_DOMAIN_DEVICE;
		else
		{
			LOGE("Failed to allocate buffer memory.\n");
			goto fail;
		}

		type = granite_find_buffer_memory_type(env->memory, fallback, reqs.memoryRequirements.memoryTypeBits,
				env->force_host_cached);
		if (type == GRANITE_NO_MEMORY_TYPE ||
				!granite_device_allocator_allocate(env->allocator, (uint32_t)reqs.memoryRequirements.size,
					(uint32_t)reqs.memoryRequirements.alignment, mode, type, allocation))
		{
			LOGE("Failed to allocate fallback memory.\n");
			goto fail;
		}
	}

	if (table->vkBindBufferMemory(env->device, *buffer, allocation->base, allocation->offset) != VK_SUCCESS)
	{
		granite_allocation_free(allocation, env->allocator);
		goto fail;
	}

	*memory_type = type;
	return true;

fail:
	table->vkDestroyBuffer(env->device, *buffer, NULL);
	*buffer = VK_NULL_HANDLE;
	return false;
}
