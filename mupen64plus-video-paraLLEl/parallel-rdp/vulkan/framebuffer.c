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
#include "framebuffer.h"
#include "logging.h"

static granite_framebuffer_t **bucket_of(granite_framebuffer_allocator_t *allocator, uint64_t hash)
{
	return &allocator->buckets[(hash ^ (hash >> 32)) & (GRANITE_FRAMEBUFFER_BUCKETS - 1)];
}

static void ring_remove(granite_framebuffer_allocator_t *allocator, granite_framebuffer_t *node)
{
	if (node->prev)
		node->prev->next = node->next;
	else
		allocator->rings[node->index] = node->next;
	if (node->next)
		node->next->prev = node->prev;
}

static void ring_insert(granite_framebuffer_allocator_t *allocator, granite_framebuffer_t *node)
{
	node->index = allocator->index;
	node->prev = NULL;
	node->next = allocator->rings[allocator->index];
	if (node->next)
		node->next->prev = node;
	allocator->rings[allocator->index] = node;
}

static void bucket_remove(granite_framebuffer_allocator_t *allocator, granite_framebuffer_t *node)
{
	granite_framebuffer_t **link = bucket_of(allocator, node->hash);

	while (*link != node)
		link = &(*link)->hash_next;
	*link = node->hash_next;
}

/* Gives the framebuffers of a ring to destroy and makes their nodes
 * vacant */
static void ring_retire(granite_framebuffer_allocator_t *allocator, unsigned index)
{
	granite_framebuffer_t *node = allocator->rings[index];

	allocator->rings[index] = NULL;
	while (node)
	{
		granite_framebuffer_t *next = node->next;

		bucket_remove(allocator, node);
		if (node->framebuffer != VK_NULL_HANDLE && allocator->destroy)
			allocator->destroy(allocator->userdata, node->framebuffer);
		node->framebuffer = VK_NULL_HANDLE;
		node->next = allocator->vacants;
		allocator->vacants = node;
		node = next;
	}
}

void granite_framebuffer_allocator_init(granite_framebuffer_allocator_t *allocator, VkDevice device,
		const struct VolkDeviceTable *table, void (*destroy)(void *userdata, VkFramebuffer framebuffer),
		void *userdata)
{
	memset(allocator, 0, sizeof(*allocator));
	allocator->device = device;
	allocator->table = table;
	allocator->destroy = destroy;
	allocator->userdata = userdata;
}

void granite_framebuffer_allocator_clear(granite_framebuffer_allocator_t *allocator)
{
	unsigned i;

	for (i = 0; i < GRANITE_FRAMEBUFFER_RING_SIZE; i++)
		ring_retire(allocator, i);

	if (allocator->fallback.framebuffer != VK_NULL_HANDLE && allocator->destroy)
		allocator->destroy(allocator->userdata, allocator->fallback.framebuffer);
	allocator->fallback.framebuffer = VK_NULL_HANDLE;
}

void granite_framebuffer_allocator_free(granite_framebuffer_allocator_t *allocator)
{
	granite_framebuffer_block_t *block;

	granite_framebuffer_allocator_clear(allocator);
	block = allocator->blocks;
	while (block)
	{
		granite_framebuffer_block_t *next = block->next;
		free(block);
		block = next;
	}
	allocator->blocks = NULL;
	allocator->vacants = NULL;
}

void granite_framebuffer_allocator_begin_frame(granite_framebuffer_allocator_t *allocator)
{
	allocator->index = (allocator->index + 1) & (GRANITE_FRAMEBUFFER_RING_SIZE - 1);
	ring_retire(allocator, allocator->index);
}

granite_framebuffer_t *granite_framebuffer_allocator_find(granite_framebuffer_allocator_t *allocator, uint64_t hash)
{
	granite_framebuffer_t *node = *bucket_of(allocator, hash);

	for (; node; node = node->hash_next)
	{
		if (node->hash != hash)
			continue;

		if (node->index != allocator->index)
		{
			ring_remove(allocator, node);
			ring_insert(allocator, node);
		}
		return node;
	}
	return NULL;
}

granite_framebuffer_t *granite_framebuffer_allocator_create(granite_framebuffer_allocator_t *allocator,
		uint64_t hash, VkRenderPass render_pass, const void *owner, const VkImageView *views,
		unsigned num_views, uint32_t width, uint32_t height)
{
	granite_framebuffer_t *node = allocator->vacants;
	VkFramebufferCreateInfo info;
	unsigned i;

	if (node)
		allocator->vacants = node->next;
	else
	{
		granite_framebuffer_block_t *block = (granite_framebuffer_block_t *)calloc(1, sizeof(*block));

		if (block)
		{
			block->next = allocator->blocks;
			allocator->blocks = block;
			for (i = 1; i < GRANITE_FRAMEBUFFER_BLOCK; i++)
			{
				block->nodes[i].next = allocator->vacants;
				allocator->vacants = &block->nodes[i];
			}
			node = &block->nodes[0];
		}
	}

	if (!node)
	{
		/* in no ring or bucket: the next one made takes its place */
		node = &allocator->fallback;
		if (node->framebuffer != VK_NULL_HANDLE && allocator->destroy)
			allocator->destroy(allocator->userdata, node->framebuffer);
	}

	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	info.renderPass = render_pass;
	info.attachmentCount = num_views;
	info.pAttachments = views;
	info.width = width;
	info.height = height;
	info.layers = 1;

	node->framebuffer = VK_NULL_HANDLE;
	if (allocator->table->vkCreateFramebuffer(allocator->device, &info, NULL, &node->framebuffer) != VK_SUCCESS)
	{
		LOGE("Failed to create framebuffer.\n");
		node->framebuffer = VK_NULL_HANDLE;
	}

	node->width = width;
	node->height = height;
	node->render_pass = owner;
	node->hash = hash;

	if (node != &allocator->fallback)
	{
		granite_framebuffer_t **bucket = bucket_of(allocator, hash);

		node->hash_next = *bucket;
		*bucket = node;
		ring_insert(allocator, node);
	}
	return node;
}
