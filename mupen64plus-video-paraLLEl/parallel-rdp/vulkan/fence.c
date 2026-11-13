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
#include "fence.h"
#include "logging.h"

void granite_fence_pool_slots_init(granite_lf_pool_t *pool)
{
	granite_lf_pool_init(pool, sizeof(granite_fence_t), 8);
}

granite_fence_t *granite_fence_new(granite_lf_pool_t *pool, const granite_handle_owner_t *owner,
		const granite_fence_env_t *env, VkFence fence, VkSemaphore timeline, uint64_t value)
{
	granite_fence_t *object = (granite_fence_t *)granite_lf_pool_alloc(pool);

	if (!object)
		return NULL;
	memset(object, 0, sizeof(*object));
	granite_refs_init(&object->refs);
	retro_atomic_int_init(&object->wait_state, GRANITE_FENCE_UNWAITED);
	object->owner = owner;
	object->pool = pool;
	object->env = *env;
	if (value != 0)
	{
		object->timeline = timeline;
		object->value = value;
	}
	else
		object->fence = fence;
	return object;
}

/* The wait itself, by the one thread that claimed it */
static bool fence_vk_wait(void *ctx)
{
	granite_fence_t *fence = (granite_fence_t *)ctx;
	const struct VolkDeviceTable *table = fence->env.table;

	if (fence->value != 0)
	{
		VkSemaphoreWaitInfoKHR info;

		memset(&info, 0, sizeof(info));
		info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO_KHR;
		info.semaphoreCount = 1;
		info.pSemaphores = &fence->timeline;
		info.pValues = &fence->value;
		return table->vkWaitSemaphoresKHR(fence->env.device, &info, UINT64_MAX) == VK_SUCCESS;
	}
	return table->vkWaitForFences(fence->env.device, 1, &fence->fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS;
}

bool granite_fence_wait_for(granite_fence_t *fence)
{
	if (granite_fence_wait(&fence->wait_state, fence->env.wait_ec, fence_vk_wait, fence))
		return true;
	if (fence->value != 0)
		LOGE("Failed to wait for timeline semaphore!\n");
	else
		LOGE("Failed to wait for fence!\n");
	return false;
}

void granite_fence_dispose(granite_fence_t *fence)
{
	const granite_handle_owner_t *owner = fence->owner;

	if (fence->fence != VK_NULL_HANDLE)
		owner->recycle_fence(owner->user, fence->fence, granite_fence_seen(&fence->wait_state),
				fence->internal_sync);
	granite_lf_pool_release(fence->pool, fence);
}
