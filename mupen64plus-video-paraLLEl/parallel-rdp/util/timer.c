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

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 199309L
#endif

#include "timer.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <retro_atomic.h>

/* The counter's rate is fixed at boot. The first thread to read it keeps
 * it; others racing it use their own reading. */
static double qpc_ns_per_tick;
static retro_atomic_int_t qpc_rate_state; /* 0 unread, 1 being kept, 2 kept */

static double granite_qpc_ns_per_tick(void)
{
	LARGE_INTEGER freq;
	double ns_per_tick;
	if (retro_atomic_load_acquire_int(&qpc_rate_state) == 2)
		return qpc_ns_per_tick;
	QueryPerformanceFrequency(&freq);
	ns_per_tick = 1e9 / (double)freq.QuadPart;
	if (retro_atomic_cas_int(&qpc_rate_state, 0, 1))
	{
		qpc_ns_per_tick = ns_per_tick;
		retro_atomic_store_release_int(&qpc_rate_state, 2);
	}
	return ns_per_tick;
}

int64_t granite_time_nsecs(void)
{
	LARGE_INTEGER li;
	if (!QueryPerformanceCounter(&li))
		return 0;
	return (int64_t)((double)li.QuadPart * granite_qpc_ns_per_tick());
}
#else
#include <time.h>

int64_t granite_time_nsecs(void)
{
	struct timespec ts;
#if defined(CLOCK_MONOTONIC_RAW) && !defined(ANDROID)
	if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) < 0)
#else
	if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
#endif
		return 0;
	return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}
#endif
