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
#include "frame_sync.h"

void granite_frame_sync_init(granite_frame_sync_t *sync, const VkSemaphore *timelines)
{
	unsigned i;
	memset(sync, 0, sizeof(*sync));
	for (i = 0; i < GRANITE_FRAME_SYNC_QUEUES; i++)
		sync->timeline_semaphores[i] = timelines[i];
}

bool granite_frame_sync_add_fence(granite_frame_sync_t *sync, VkFence fence)
{
	if (sync->num_fences == GRANITE_FRAME_SYNC_FENCES)
		return false;
	sync->fences[sync->num_fences++] = fence;
	return true;
}

void granite_frame_sync_wait(granite_frame_sync_t *sync, VkDevice device, const struct VolkDeviceTable *table,
		bool timeline)
{
	unsigned i;

	for (i = 0; timeline && i < GRANITE_FRAME_SYNC_QUEUES; i++)
		if (sync->timeline_semaphores[i] == VK_NULL_HANDLE)
			timeline = false;

	if (timeline)
	{
		VkSemaphoreWaitInfo info;
		VkSemaphore semaphores[GRANITE_FRAME_SYNC_QUEUES];
		uint64_t values[GRANITE_FRAME_SYNC_QUEUES];

		memset(&info, 0, sizeof(info));
		info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
		for (i = 0; i < GRANITE_FRAME_SYNC_QUEUES; i++)
		{
			if (!sync->timeline_values[i])
				continue;
			semaphores[info.semaphoreCount] = sync->timeline_semaphores[i];
			values[info.semaphoreCount] = sync->timeline_values[i];
			info.semaphoreCount++;
		}

		if (info.semaphoreCount)
		{
			info.pSemaphores = semaphores;
			info.pValues = values;
			table->vkWaitSemaphoresKHR(device, &info, UINT64_MAX);
		}
	}

	/* (none on a device with timeline semaphores) */
	if (sync->num_fences)
	{
		table->vkWaitForFences(device, sync->num_fences, sync->fences, VK_TRUE, UINT64_MAX);
		sync->num_fences = 0;
	}
}

void granite_frame_sync_forget_fences(granite_frame_sync_t *sync)
{
	sync->num_fences = 0;
}

void granite_frame_sync_forget_timelines(granite_frame_sync_t *sync)
{
	unsigned i;
	for (i = 0; i < GRANITE_FRAME_SYNC_QUEUES; i++)
	{
		sync->timeline_semaphores[i] = VK_NULL_HANDLE;
		sync->timeline_values[i] = 0;
	}
}
