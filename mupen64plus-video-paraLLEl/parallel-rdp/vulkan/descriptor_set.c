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
#include "descriptor_set.h"
#include "logging.h"

bool granite_set_allocator_init(granite_set_allocator_t *allocator, VkDevice device,
		const struct VolkDeviceTable *table, const granite_descriptor_set_layout_t *layout,
		const uint32_t *stages_for_bindings, const VkSampler *immutable_samplers, bool push,
		unsigned num_threads)
{
	/* which mask is which descriptor type, in the order the bindings
	 * have always been listed */
	static const VkDescriptorType types[9] = {
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
		VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_SAMPLER
	};
	VkDescriptorSetLayoutCreateInfo info;
	VkDescriptorSetLayoutBinding bindings[GRANITE_NUM_BINDINGS];
	VkSampler samplers[GRANITE_NUM_BINDINGS];
	uint32_t masks[9];
	unsigned num_bindings = 0;
	unsigned i, t;

	memset(allocator, 0, sizeof(*allocator));
	allocator->device = device;
	allocator->table = table;
	allocator->push = push;
	if (!push && num_threads)
	{
		allocator->per_thread = (granite_set_thread_t *)calloc(num_threads, sizeof(*allocator->per_thread));
		if (!allocator->per_thread)
			return false;
		allocator->num_per_thread = num_threads;
		for (i = 0; i < num_threads; i++)
			allocator->per_thread[i].should_begin = true;
	}

	masks[0] = layout->sampled_image_mask;
	masks[1] = layout->sampled_texel_buffer_mask;
	masks[2] = layout->storage_texel_buffer_mask;
	masks[3] = layout->storage_image_mask;
	masks[4] = layout->uniform_buffer_mask;
	masks[5] = layout->storage_buffer_mask;
	masks[6] = layout->input_attachment_mask;
	masks[7] = layout->separate_image_mask;
	masks[8] = layout->sampler_mask;

	for (i = 0; i < GRANITE_NUM_BINDINGS; i++)
	{
		if (stages_for_bindings[i] == 0)
			continue;
		/* (a binding is of one kind: a shader that says otherwise gets
		 * the first) */
		for (t = 0; t < 9; t++)
			if (masks[t] & (1u << i))
				break;
		if (t == 9)
			continue;

		bindings[num_bindings].binding = i;
		bindings[num_bindings].descriptorType = types[t];
		bindings[num_bindings].descriptorCount = layout->array_size[i];
		bindings[num_bindings].stageFlags = stages_for_bindings[i];
		bindings[num_bindings].pImmutableSamplers = NULL;
		/* a pushed uniform buffer has its offset in the descriptor */
		if (push && types[t] == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC)
			bindings[num_bindings].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		if (     (types[t] == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER || types[t] == VK_DESCRIPTOR_TYPE_SAMPLER)
				&& (layout->immutable_sampler_mask & (1u << i))
				&& immutable_samplers && immutable_samplers[i] != VK_NULL_HANDLE)
		{
			samplers[i] = immutable_samplers[i];
			bindings[num_bindings].pImmutableSamplers = &samplers[i];
		}
		num_bindings++;

		if (!push)
		{
			allocator->pool_size[allocator->num_pool_sizes].type = types[t];
			allocator->pool_size[allocator->num_pool_sizes].descriptorCount =
				(uint32_t)layout->array_size[i] * GRANITE_NUM_SETS_PER_POOL;
			allocator->num_pool_sizes++;
		}
	}

	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	if (num_bindings)
	{
		info.bindingCount = num_bindings;
		info.pBindings = bindings;
	}
	if (push)
		info.flags |= VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;

	if (table->vkCreateDescriptorSetLayout(device, &info, NULL, &allocator->set_layout) != VK_SUCCESS)
	{
		LOGE("Failed to create descriptor set layout.\n");
		allocator->set_layout = VK_NULL_HANDLE;
	}
	return true;
}

void granite_set_allocator_begin_frame(granite_set_allocator_t *allocator)
{
	unsigned i;
	for (i = 0; i < allocator->num_per_thread; i++)
		allocator->per_thread[i].should_begin = true;
}

static granite_set_node_t **bucket_of(granite_set_thread_t *state, uint64_t hash)
{
	return &state->buckets[(size_t)(hash ^ (hash >> 32)) & state->bucket_mask];
}

static void map_erase(granite_set_thread_t *state, granite_set_node_t *node)
{
	granite_set_node_t **link = bucket_of(state, node->hash);
	while (*link && *link != node)
		link = &(*link)->hash_next;
	if (*link)
		*link = node->hash_next;
}

/* Room in the map for the sets of one more pool: twice as many buckets
 * as sets. The sets in the rings are put back in. */
static bool map_grow(granite_set_thread_t *state)
{
	size_t wanted = (state->num_nodes + GRANITE_NUM_SETS_PER_POOL) * 2;
	size_t size = state->bucket_mask ? state->bucket_mask + 1 : 64;
	granite_set_node_t **buckets;
	unsigned i;

	if (state->buckets && size >= wanted)
		return true;
	while (size < wanted)
		size *= 2;
	buckets = (granite_set_node_t **)calloc(size, sizeof(*buckets));
	if (!buckets)
		return false;
	free(state->buckets);
	state->buckets = buckets;
	state->bucket_mask = size - 1;
	for (i = 0; i < GRANITE_DESCRIPTOR_RING_SIZE; i++)
	{
		granite_set_node_t *node;
		for (node = state->rings[i]; node; node = node->next)
		{
			granite_set_node_t **bucket = bucket_of(state, node->hash);
			node->hash_next = *bucket;
			*bucket = node;
		}
	}
	return true;
}

static void ring_insert_front(granite_set_thread_t *state, granite_set_node_t *node)
{
	granite_set_node_t **ring = &state->rings[state->index];
	node->index = state->index;
	node->prev = NULL;
	node->next = *ring;
	if (*ring)
		(*ring)->prev = node;
	*ring = node;
}

static void ring_erase(granite_set_thread_t *state, granite_set_node_t *node)
{
	if (node->prev)
		node->prev->next = node->next;
	else
		state->rings[node->index] = node->next;
	if (node->next)
		node->next->prev = node->prev;
}

/* The frame moves on: what was last asked for a ring ago is written for
 * nothing any more */
static void thread_begin_frame(granite_set_thread_t *state)
{
	granite_set_node_t *node, *next;
	state->index = (state->index + 1) & (GRANITE_DESCRIPTOR_RING_SIZE - 1);
	for (node = state->rings[state->index]; node; node = next)
	{
		next = node->next;
		map_erase(state, node);
		node->next = state->vacants;
		state->vacants = node;
	}
	state->rings[state->index] = NULL;
}

VkDescriptorSet granite_set_allocator_find(granite_set_allocator_t *allocator, unsigned thread_index,
		uint64_t hash, bool *cached)
{
	granite_set_thread_t *state;
	granite_set_node_t *node;

	*cached = false;
	if (thread_index >= allocator->num_per_thread)
		return VK_NULL_HANDLE;
	state = &allocator->per_thread[thread_index];
	if (state->should_begin)
	{
		thread_begin_frame(state);
		state->should_begin = false;
	}

	if (state->buckets)
	{
		for (node = *bucket_of(state, hash); node; node = node->hash_next)
		{
			if (node->hash != hash)
				continue;
			/* asked for again: it stays another ring */
			if (node->index != state->index)
			{
				ring_erase(state, node);
				ring_insert_front(state, node);
			}
			*cached = true;
			return node->set;
		}
	}

	if (!state->vacants)
	{
		VkDescriptorPoolCreateInfo info;
		VkDescriptorSetAllocateInfo alloc;
		VkDescriptorSet sets[GRANITE_NUM_SETS_PER_POOL];
		VkDescriptorSetLayout layouts[GRANITE_NUM_SETS_PER_POOL];
		granite_set_pool_t *pool;
		unsigned i;

		if (!map_grow(state))
			return VK_NULL_HANDLE;
		pool = (granite_set_pool_t *)calloc(1, sizeof(*pool));
		if (!pool)
			return VK_NULL_HANDLE;

		memset(&info, 0, sizeof(info));
		info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		info.maxSets = GRANITE_NUM_SETS_PER_POOL;
		if (allocator->num_pool_sizes)
		{
			info.poolSizeCount = allocator->num_pool_sizes;
			info.pPoolSizes = allocator->pool_size;
		}
		if (allocator->table->vkCreateDescriptorPool(allocator->device, &info, NULL, &pool->pool) != VK_SUCCESS)
		{
			LOGE("Failed to create descriptor pool.\n");
			free(pool);
			return VK_NULL_HANDLE;
		}

		for (i = 0; i < GRANITE_NUM_SETS_PER_POOL; i++)
			layouts[i] = allocator->set_layout;
		memset(&alloc, 0, sizeof(alloc));
		alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		alloc.descriptorPool = pool->pool;
		alloc.descriptorSetCount = GRANITE_NUM_SETS_PER_POOL;
		alloc.pSetLayouts = layouts;
		if (allocator->table->vkAllocateDescriptorSets(allocator->device, &alloc, sets) != VK_SUCCESS)
		{
			LOGE("Failed to allocate descriptor sets.\n");
			allocator->table->vkDestroyDescriptorPool(allocator->device, pool->pool, NULL);
			free(pool);
			return VK_NULL_HANDLE;
		}

		pool->next = state->pools;
		state->pools = pool;
		for (i = 0; i < GRANITE_NUM_SETS_PER_POOL; i++)
		{
			pool->nodes[i].set = sets[i];
			pool->nodes[i].next = state->vacants;
			state->vacants = &pool->nodes[i];
		}
		state->num_nodes += GRANITE_NUM_SETS_PER_POOL;
	}

	node = state->vacants;
	state->vacants = node->next;
	node->hash = hash;
	ring_insert_front(state, node);
	{
		granite_set_node_t **bucket = bucket_of(state, hash);
		node->hash_next = *bucket;
		*bucket = node;
	}
	return node->set;
}

void granite_set_allocator_clear(granite_set_allocator_t *allocator)
{
	unsigned i;
	for (i = 0; i < allocator->num_per_thread; i++)
	{
		granite_set_thread_t *state = &allocator->per_thread[i];
		granite_set_pool_t *pool, *next;
		for (pool = state->pools; pool; pool = next)
		{
			next = pool->next;
			allocator->table->vkResetDescriptorPool(allocator->device, pool->pool, 0);
			allocator->table->vkDestroyDescriptorPool(allocator->device, pool->pool, NULL);
			free(pool);
		}
		free(state->buckets);
		memset(state, 0, sizeof(*state));
		state->should_begin = true;
	}
}

void granite_set_allocator_free(granite_set_allocator_t *allocator)
{
	if (allocator->set_layout != VK_NULL_HANDLE)
		allocator->table->vkDestroyDescriptorSetLayout(allocator->device, allocator->set_layout, NULL);
	allocator->set_layout = VK_NULL_HANDLE;
	granite_set_allocator_clear(allocator);
	free(allocator->per_thread);
	allocator->per_thread = NULL;
	allocator->num_per_thread = 0;
}
