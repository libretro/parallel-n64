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

#include "fence_wait.h"

static void fence_publish(retro_atomic_int_t *state, retro_eventcount_t *ec, int value)
{
	retro_atomic_store_release_int(state, value);
	if (ec)
		retro_eventcount_notify(ec);
}

bool granite_fence_wait(retro_atomic_int_t *state, retro_eventcount_t *ec,
		granite_fence_wait_fn wait, void *ctx)
{
	for (;;)
	{
		int s = retro_atomic_load_acquire_int(state);
		if (s == GRANITE_FENCE_SEEN)
			return true;

		if (s == GRANITE_FENCE_UNWAITED)
		{
			if (!retro_atomic_cas_int(state, GRANITE_FENCE_UNWAITED, GRANITE_FENCE_WAITING))
				continue;
			if (wait(ctx))
			{
				fence_publish(state, ec, GRANITE_FENCE_SEEN);
				return true;
			}
			fence_publish(state, ec, GRANITE_FENCE_UNWAITED);
			return false;
		}

		/* GRANITE_FENCE_WAITING: another thread has it */
		if (ec)
		{
			int key = retro_eventcount_prepare_wait(ec);
			if (retro_atomic_load_acquire_int(state) == GRANITE_FENCE_WAITING)
				retro_eventcount_commit_wait(ec, key);
			else
				retro_eventcount_cancel_wait(ec);
		}
		else
		{
			/* No eventcount (its allocation failed): wait on the object
			 * as well, rather than spin. */
			if (!wait(ctx))
				return false;
			retro_atomic_store_release_int(state, GRANITE_FENCE_SEEN);
			return true;
		}
	}
}
