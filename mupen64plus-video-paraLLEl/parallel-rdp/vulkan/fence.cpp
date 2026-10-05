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

#include "fence.hpp"
#include "device.hpp"

namespace Vulkan
{
FenceHolder::~FenceHolder()
{
	if (fence != VK_NULL_HANDLE)
	{
		if (internal_sync)
			device->reset_fence_nolock(fence, observed_wait());
		else
			device->reset_fence(fence, observed_wait());
	}
}

VkFence FenceHolder::get_fence() const
{
	return fence;
}

bool FenceHolder::vk_wait(uint64_t timeout)
{
	auto &table = device->get_device_table();
	if (timeline_value != 0)
	{
		VK_ASSERT(timeline_semaphore);
		VkSemaphoreWaitInfoKHR info = { VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO_KHR };
		info.semaphoreCount = 1;
		info.pSemaphores = &timeline_semaphore;
		info.pValues = &timeline_value;
		return table.vkWaitSemaphoresKHR(device->get_device(), &info, timeout) == VK_SUCCESS;
	}
	return table.vkWaitForFences(device->get_device(), 1, &fence, VK_TRUE, timeout) == VK_SUCCESS;
}

void FenceHolder::publish_wait_state(int state)
{
	retro_atomic_store_release_int(&wait_state, state);
	if (device->fence_wait_ec_live)
		retro_eventcount_notify(&device->fence_wait_ec);
}

void FenceHolder::wait()
{
	// Waiting for the same VkFence in parallel is not allowed, and there seems to be some shenanigans on Intel
	// when waiting for a timeline semaphore in parallel with same value as well.
	for (;;)
	{
		int state = retro_atomic_load_acquire_int(&wait_state);
		if (state == 2)
			return;

		if (state == 0 && retro_atomic_cas_int(&wait_state, 0, 1))
		{
			if (vk_wait(UINT64_MAX))
				publish_wait_state(2);
			else
			{
				if (timeline_value != 0)
					LOGE("Failed to wait for timeline semaphore!\n");
				else
					LOGE("Failed to wait for fence!\n");
				// Hand the fence back so a later wait can try again.
				publish_wait_state(0);
			}
			return;
		}

		if (state == 1 && device->fence_wait_ec_live)
		{
			int key = retro_eventcount_prepare_wait(&device->fence_wait_ec);
			if (retro_atomic_load_acquire_int(&wait_state) == 1)
				retro_eventcount_commit_wait(&device->fence_wait_ec, key);
			else
				retro_eventcount_cancel_wait(&device->fence_wait_ec);
		}
		else if (state == 1)
		{
			// No eventcount (its allocation failed): wait on the object
			// directly rather than spin.
			if (vk_wait(UINT64_MAX))
				publish_wait_state(2);
			return;
		}
	}
}

bool FenceHolder::wait_timeout(uint64_t timeout)
{
	if (observed_wait())
		return true;
	// A bounded probe; it does not claim the fence, as before.
	bool ret = vk_wait(timeout);
	if (ret)
		publish_wait_state(2);
	return ret;
}

void FenceHolderDeleter::operator()(Vulkan::FenceHolder *fence)
{
	fence->device->handle_pool.fences.free(fence);
}
}
