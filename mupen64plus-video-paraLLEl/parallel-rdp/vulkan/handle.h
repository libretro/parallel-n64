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

#ifndef GRANITE_HANDLE_H
#define GRANITE_HANDLE_H

#include <stddef.h>
#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>
#include <retro_inline.h>
#include <retro_atomic.h>
#include "volk.h"
#include "memory_allocator.h"
#include "frame_garbage.h"

RETRO_BEGIN_DECLS

/* What the objects the device hands out (buffers, images, ...) need of
 * it when the last reference to one goes: the GPU may still be using
 * what they hold, so it is the device that destroys it, once the frame
 * it was last used in is done. */
typedef struct granite_handle_owner
{
	void *user;
	/* @kind: GRANITE_GARBAGE_*, @handle: the Vulkan handle's bits.
	 * @owner_thread: called on the thread that owns the device, which
	 * needs no hand-over; otherwise from any thread. */
	void (*destroy)(void *user, unsigned kind, uint64_t handle, bool owner_thread);
	void (*free_memory)(void *user, const granite_allocation_t *alloc, bool owner_thread);
} granite_handle_owner_t;

/* A count of references, from any thread. An object starts with one. */
static INLINE void granite_refs_init(retro_atomic_int_t *refs)
{
	retro_atomic_int_init(refs, 1);
}

static INLINE void granite_refs_add(retro_atomic_int_t *refs)
{
	retro_atomic_fetch_add_int(refs, 1);
}

/* True for the last one: the caller disposes of the object */
static INLINE bool granite_refs_drop(retro_atomic_int_t *refs)
{
	return retro_atomic_fetch_sub_int(refs, 1) == 1;
}

/* No other reference: nobody else could be using the object or start to */
static INLINE bool granite_refs_only(retro_atomic_int_t *refs)
{
	return retro_atomic_load_acquire_int(refs) == 1;
}

RETRO_END_DECLS

#endif
