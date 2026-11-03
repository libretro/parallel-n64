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
#include "frame_garbage.h"

static void garbage_release(enum granite_garbage_kind kind, uint64_t h,
		VkDevice device, const struct VolkDeviceTable *t,
		granite_semaphore_pool_t *semaphores)
{
	switch (kind)
	{
		case GRANITE_GARBAGE_FRAMEBUFFER:
			t->vkDestroyFramebuffer(device, GRANITE_VK_FROM_BITS(VkFramebuffer, h), NULL);
			break;
		case GRANITE_GARBAGE_SAMPLER:
			t->vkDestroySampler(device, GRANITE_VK_FROM_BITS(VkSampler, h), NULL);
			break;
		case GRANITE_GARBAGE_PIPELINE:
			t->vkDestroyPipeline(device, GRANITE_VK_FROM_BITS(VkPipeline, h), NULL);
			break;
		case GRANITE_GARBAGE_IMAGE_VIEW:
			t->vkDestroyImageView(device, GRANITE_VK_FROM_BITS(VkImageView, h), NULL);
			break;
		case GRANITE_GARBAGE_BUFFER_VIEW:
			t->vkDestroyBufferView(device, GRANITE_VK_FROM_BITS(VkBufferView, h), NULL);
			break;
		case GRANITE_GARBAGE_IMAGE:
			t->vkDestroyImage(device, GRANITE_VK_FROM_BITS(VkImage, h), NULL);
			break;
		case GRANITE_GARBAGE_BUFFER:
			t->vkDestroyBuffer(device, GRANITE_VK_FROM_BITS(VkBuffer, h), NULL);
			break;
		case GRANITE_GARBAGE_SEMAPHORE:
			t->vkDestroySemaphore(device, GRANITE_VK_FROM_BITS(VkSemaphore, h), NULL);
			break;
		case GRANITE_GARBAGE_RECYCLE_SEMAPHORE:
			granite_semaphore_pool_recycle(semaphores, GRANITE_VK_FROM_BITS(VkSemaphore, h));
			break;
		default:
			break;
	}
}

void granite_frame_garbage_grow_push(granite_frame_garbage_t *garbage,
		enum granite_garbage_kind kind, uint64_t handle)
{
	granite_garbage_list_t *list = &garbage->lists[kind];
	size_t cap = list->capacity ? list->capacity * 2 : 16;
	uint64_t *grown = (uint64_t *)realloc(list->items, cap * sizeof(*grown));
	if (!grown)
		abort();
	list->items = grown;
	list->capacity = cap;
	list->items[list->count++] = handle;
}

bool granite_frame_garbage_has(const granite_frame_garbage_t *garbage,
		enum granite_garbage_kind kind, uint64_t handle)
{
	const granite_garbage_list_t *list = &garbage->lists[kind];
	size_t i;
	for (i = 0; i < list->count; i++)
		if (list->items[i] == handle)
			return true;
	return false;
}

/* in one vkResetFences call per batch, then back to the pool */
static void garbage_recycle_fences(granite_garbage_list_t *list,
		VkDevice device, const struct VolkDeviceTable *t, granite_fence_pool_t *fences)
{
	VkFence batch[32];
	size_t i = 0;
	while (i < list->count)
	{
		uint32_t n = 0, k;
		while (n < 32 && i < list->count)
			batch[n++] = GRANITE_VK_FROM_BITS(VkFence, list->items[i++]);
		t->vkResetFences(device, n, batch);
		for (k = 0; k < n; k++)
			granite_fence_pool_recycle(fences, batch[k]);
	}
	list->count = 0;
}

void granite_frame_garbage_collect(granite_frame_garbage_t *garbage,
		VkDevice device, const struct VolkDeviceTable *table,
		granite_semaphore_pool_t *semaphores, granite_fence_pool_t *fences)
{
	unsigned kind;
	for (kind = 0; kind < GRANITE_GARBAGE_KINDS; kind++)
	{
		granite_garbage_list_t *list = &garbage->lists[kind];
		size_t i;
		if (kind == GRANITE_GARBAGE_RECYCLE_FENCE)
		{
			garbage_recycle_fences(list, device, table, fences);
			continue;
		}
		for (i = 0; i < list->count; i++)
			garbage_release((enum granite_garbage_kind)kind, list->items[i], device, table, semaphores);
		list->count = 0;
	}
}

void granite_frame_garbage_free(granite_frame_garbage_t *garbage)
{
	unsigned kind;
	for (kind = 0; kind < GRANITE_GARBAGE_KINDS; kind++)
	{
		free(garbage->lists[kind].items);
		garbage->lists[kind].items = NULL;
		garbage->lists[kind].count = 0;
		garbage->lists[kind].capacity = 0;
	}
}
