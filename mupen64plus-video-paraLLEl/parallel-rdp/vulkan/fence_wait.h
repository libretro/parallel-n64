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

#ifndef GRANITE_FENCE_WAIT_H
#define GRANITE_FENCE_WAIT_H

#include <boolean.h>
#include <retro_common_api.h>
#include <retro_inline.h>
#include <retro_atomic.h>
#include <rthreads/retro_eventcount.h>

RETRO_BEGIN_DECLS

/* Waiting on a fence, or a timeline semaphore value, from several threads
 * at once. Granite keeps waits on one object to one thread at a time
 * (drivers have had trouble with parallel waits on the same one), so the
 * first waiter claims it with a CAS and the rest park on an eventcount
 * until it is done. No lock.
 *
 * The state is one int, starting at GRANITE_FENCE_UNWAITED. */
#define GRANITE_FENCE_UNWAITED 0
#define GRANITE_FENCE_WAITING  1
#define GRANITE_FENCE_SEEN     2 /* signalled, and a waiter saw it */

/* The object's own wait, without bound; true once it is signalled */
typedef bool (*granite_fence_wait_fn)(void *ctx);

/* Returns once the object is signalled: true, or false if the claimed
 * wait failed (the object is handed back, and a later wait tries again).
 * ec: notified whenever the state changes; may be NULL, and the threads
 * that did not claim the wait then wait on the object themselves. */
bool granite_fence_wait(retro_atomic_int_t *state, retro_eventcount_t *ec,
		granite_fence_wait_fn wait, void *ctx);

/* Some wait has seen it signalled */
static INLINE bool granite_fence_seen(retro_atomic_int_t *state)
{
	return retro_atomic_load_acquire_int(state) == GRANITE_FENCE_SEEN;
}

RETRO_END_DECLS

#endif
