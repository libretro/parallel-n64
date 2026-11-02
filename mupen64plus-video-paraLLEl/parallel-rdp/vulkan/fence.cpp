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
			device->reset_fence_nolock(fence, granite_fence_seen(&wait_state));
		else
			device->reset_fence(fence, granite_fence_seen(&wait_state));
	}
}

bool FenceHolder::vk_wait(void *self_)
{
	auto *self = static_cast<FenceHolder *>(self_);
	auto &table = self->device->get_device_table();
	if (self->timeline_value != 0)
	{
		VK_ASSERT(self->timeline_semaphore);
		VkSemaphoreWaitInfoKHR info = { VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO_KHR };
		info.semaphoreCount = 1;
		info.pSemaphores = &self->timeline_semaphore;
		info.pValues = &self->timeline_value;
		return table.vkWaitSemaphoresKHR(self->device->get_device(), &info, UINT64_MAX) == VK_SUCCESS;
	}
	return table.vkWaitForFences(self->device->get_device(), 1, &self->fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS;
}

void FenceHolder::wait()
{
	if (!granite_fence_wait(&wait_state, device->fence_wait_ec_live ? &device->fence_wait_ec : nullptr,
	                        vk_wait, this))
	{
		if (timeline_value != 0)
			LOGE("Failed to wait for timeline semaphore!\n");
		else
			LOGE("Failed to wait for fence!\n");
	}
}

void FenceHolderDeleter::operator()(Vulkan::FenceHolder *fence)
{
	fence->device->handle_pool.fences.free(fence);
}
}
