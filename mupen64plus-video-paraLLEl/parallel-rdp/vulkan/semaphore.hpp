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

#pragma once

#include "vulkan_common.hpp"
#include "vulkan_headers.hpp"
#include "semaphore.h"

namespace Vulkan
{
// Semaphores are C (semaphore.h). No members of its own: a
// granite_semaphore_t by another name.
class SemaphoreHolder : public granite_semaphore_t
{
public:
	SemaphoreHolder() = delete;

	void add_reference()
	{
		granite_semaphore_ref(this);
	}

	void release_reference()
	{
		granite_semaphore_unref(this);
	}

	void set_internal_sync_object()
	{
		internal_sync = true;
	}
};

using Semaphore = Util::IntrusivePtr<SemaphoreHolder>;
}
