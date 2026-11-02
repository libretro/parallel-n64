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

#ifndef GRANITE_COOKIE_H
#define GRANITE_COOKIE_H

#include <stdint.h>
#include <retro_common_api.h>

RETRO_BEGIN_DECLS

/* A 64-bit counter, atomic on 32-bit targets too */
typedef struct granite_counter64
{
#if defined(_MSC_VER) && !defined(__clang__)
	volatile __int64 value;
#else
	uint64_t value __attribute__((aligned(8)));
#endif
} granite_counter64_t;

void granite_counter64_init(granite_counter64_t *counter, uint64_t value);
/* Adds, and returns the sum; relaxed */
uint64_t granite_counter64_add(granite_counter64_t *counter, uint64_t delta);

/* A value no other object has had in this process, from any thread.
 * Steps of 16: the low bits are kept for special purposes. */
uint64_t granite_cookie_next(void);

RETRO_END_DECLS

#endif
