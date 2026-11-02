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

#include "semaphore.hpp"
#include "device.hpp"

#ifndef _WIN32
#include <unistd.h>
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace Vulkan
{
SemaphoreHolder::~SemaphoreHolder()
{
	recycle_semaphore();
}

void SemaphoreHolder::recycle_semaphore()
{
	if (!owned)
		return;

	VK_ASSERT(semaphore);

	if (internal_sync)
	{
		if (semaphore_type == VK_SEMAPHORE_TYPE_TIMELINE_KHR || external_compatible_features || is_signalled())
			device->destroy_semaphore_nolock(semaphore);
		else
			device->recycle_semaphore_nolock(semaphore);
	}
	else
	{
		if (semaphore_type == VK_SEMAPHORE_TYPE_TIMELINE_KHR || external_compatible_features || is_signalled())
			device->destroy_semaphore(semaphore);
		else
			device->recycle_semaphore(semaphore);
	}
}

void SemaphoreHolderDeleter::operator()(Vulkan::SemaphoreHolder *semaphore)
{
	semaphore->device->handle_pool.semaphores.free(semaphore);
}
}
