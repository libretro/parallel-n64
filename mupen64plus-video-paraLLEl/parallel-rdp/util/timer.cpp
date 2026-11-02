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

#include "timer.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif

namespace Util
{
#ifdef _WIN32
struct QPCFreq
{
	QPCFreq()
	{
		LARGE_INTEGER freq;
		QueryPerformanceFrequency(&freq);
		inv_freq = 1e9 / double(freq.QuadPart);
	}

	double inv_freq;
} static static_qpc_freq;
#endif

int64_t get_current_time_nsecs()
{
#ifdef _WIN32
	LARGE_INTEGER li;
	if (!QueryPerformanceCounter(&li))
		return 0;
	return int64_t(double(li.QuadPart) * static_qpc_freq.inv_freq);
#else
	struct timespec ts = {};
#ifdef ANDROID
	constexpr auto timebase = CLOCK_MONOTONIC;
#else
	constexpr auto timebase = CLOCK_MONOTONIC_RAW;
#endif
	if (clock_gettime(timebase, &ts) < 0)
		return 0;
	return ts.tv_sec * 1000000000ll + ts.tv_nsec;
#endif
}

}