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

#include <string.h>
#include "descriptor_set.h"
#include "hash.hpp"
#include "object_pool.hpp"
#include "vulkan_headers.hpp"
#include "sampler.hpp"
#include "limits.hpp"
#include "small_vector.hpp"
#include "cookie.hpp"

// The allocator is C (descriptor_set.h). These give the rest of Granite
// the names it had.
namespace Vulkan
{
class Device;

static_assert(VULKAN_NUM_BINDINGS == GRANITE_NUM_BINDINGS, "The C allocator has other bindings.");

static const unsigned VULKAN_NUM_SETS_PER_POOL = GRANITE_NUM_SETS_PER_POOL;
static const unsigned VULKAN_DESCRIPTOR_RING_SIZE = GRANITE_DESCRIPTOR_RING_SIZE;

}
