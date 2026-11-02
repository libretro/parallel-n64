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

#if defined(_MSC_VER)
#include <intrin.h>
#endif
#include "cookie.h"

static granite_counter64_t granite_cookie;

#if defined(_MSC_VER) && !defined(__clang__)
void granite_counter64_init(granite_counter64_t *counter, uint64_t value)
{
	counter->value = (__int64)value;
}

uint64_t granite_counter64_add(granite_counter64_t *counter, uint64_t delta)
{
	__int64 old, cur = counter->value;
	do
	{
		old = cur;
		cur = _InterlockedCompareExchange64(&counter->value, old + (__int64)delta, old);
	} while (cur != old);
	return (uint64_t)(old + (__int64)delta);
}
#else
void granite_counter64_init(granite_counter64_t *counter, uint64_t value)
{
	__atomic_store_n(&counter->value, value, __ATOMIC_RELAXED);
}

uint64_t granite_counter64_add(granite_counter64_t *counter, uint64_t delta)
{
	return __atomic_add_fetch(&counter->value, delta, __ATOMIC_RELAXED);
}
#endif

uint64_t granite_cookie_next(void)
{
	return granite_counter64_add(&granite_cookie, 16);
}
