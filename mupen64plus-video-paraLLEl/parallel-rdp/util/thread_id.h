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

#ifndef GRANITE_THREAD_ID_H
#define GRANITE_THREAD_ID_H

#include <retro_common_api.h>
#include <retro_inline.h>

#if defined(_MSC_VER)
#define GRANITE_THREAD_LOCAL __declspec(thread)
#else
#define GRANITE_THREAD_LOCAL __thread
#endif

RETRO_BEGIN_DECLS

/* The calling thread's index for per-thread state; ~0u until registered */
extern GRANITE_THREAD_LOCAL unsigned granite_thread_index_slot;

/* Reports a thread that was never registered, and gives it index 0 */
unsigned granite_thread_index_missing(void);

static INLINE unsigned granite_thread_index(void)
{
	unsigned index = granite_thread_index_slot;
	return index != ~0u ? index : granite_thread_index_missing();
}

static INLINE void granite_register_thread_index(unsigned index)
{
	granite_thread_index_slot = index;
}

RETRO_END_DECLS

#endif
