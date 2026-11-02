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

#ifndef GRANITE_FRAME_GARBAGE_H
#define GRANITE_FRAME_GARBAGE_H

#include <stdint.h>
#include <stddef.h>
#include <boolean.h>
#include <retro_common_api.h>
#include <retro_inline.h>
#include "volk.h"
#include "sync_pools.h"

RETRO_BEGIN_DECLS

/* A non-dispatchable Vulkan handle as 64 bits, and back: a pointer on
 * 64-bit targets, a uint64_t on 32-bit ones */
#if VK_USE_64_BIT_PTR_DEFINES == 1
#define GRANITE_VK_TO_BITS(h)       ((uint64_t)(uintptr_t)(h))
#define GRANITE_VK_FROM_BITS(T, b)  ((T)(uintptr_t)(b))
#else
#define GRANITE_VK_TO_BITS(h)       ((uint64_t)(h))
#define GRANITE_VK_FROM_BITS(T, b)  ((T)(b))
#endif

/* What a frame context lets go of once its GPU work is done, destroyed
 * kind by kind in this order */
enum granite_garbage_kind
{
	GRANITE_GARBAGE_FRAMEBUFFER = 0,
	GRANITE_GARBAGE_SAMPLER,
	GRANITE_GARBAGE_PIPELINE,
	GRANITE_GARBAGE_IMAGE_VIEW,
	GRANITE_GARBAGE_BUFFER_VIEW,
	GRANITE_GARBAGE_IMAGE,
	GRANITE_GARBAGE_BUFFER,
	GRANITE_GARBAGE_SEMAPHORE,
	/* unsignalled, back to the semaphore pool rather than destroyed */
	GRANITE_GARBAGE_RECYCLE_SEMAPHORE,
	GRANITE_GARBAGE_KINDS
};

typedef struct granite_garbage_list
{
	uint64_t *items;
	size_t count;
	size_t capacity;
} granite_garbage_list_t;

/* Owned by the thread that owns the frame context; other threads hand
 * their releases to that thread first. Zeroed is empty. */
typedef struct granite_frame_garbage
{
	granite_garbage_list_t lists[GRANITE_GARBAGE_KINDS];
} granite_frame_garbage_t;

/* The append when the list is full. Out of memory ends the program, as
 * the GPU may still be using what it cannot keep. */
void granite_frame_garbage_grow_push(granite_frame_garbage_t *garbage,
		enum granite_garbage_kind kind, uint64_t handle);

static INLINE void granite_frame_garbage_push(granite_frame_garbage_t *garbage,
		enum granite_garbage_kind kind, uint64_t handle)
{
	granite_garbage_list_t *list = &garbage->lists[kind];
	if (list->count < list->capacity)
		list->items[list->count++] = handle;
	else
		granite_frame_garbage_grow_push(garbage, kind, handle);
}

bool granite_frame_garbage_has(const granite_frame_garbage_t *garbage,
		enum granite_garbage_kind kind, uint64_t handle);

/* Destroys (or recycles) everything held, and empties the lists */
void granite_frame_garbage_collect(granite_frame_garbage_t *garbage,
		VkDevice device, const struct VolkDeviceTable *table,
		granite_semaphore_pool_t *semaphores);

/* Frees the lists; collect first */
void granite_frame_garbage_free(granite_frame_garbage_t *garbage);

RETRO_END_DECLS

#endif
