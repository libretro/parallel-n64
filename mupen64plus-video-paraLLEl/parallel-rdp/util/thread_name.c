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

#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "thread_name.h"

#if defined(__linux__)
#include <pthread.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

void granite_set_thread_name(const char *name)
{
#if defined(__linux__)
	pthread_setname_np(pthread_self(), name);
#elif defined(_WIN32) && !defined(_XBOX) && !(defined(WINAPI_FAMILY) && WINAPI_FAMILY != WINAPI_FAMILY_DESKTOP_APP)
	/* Windows 10 1607 on; looked up, as older systems and headers lack it. */
	typedef HRESULT (WINAPI *set_description_t)(HANDLE, PCWSTR);
	WCHAR wide[64];
	unsigned i;
	HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
	set_description_t set_description = kernel
		? (set_description_t)(void *)GetProcAddress(kernel, "SetThreadDescription") : NULL;
	if (!set_description)
		return;
	for (i = 0; name[i] && i < 63; i++)
		wide[i] = (WCHAR)(unsigned char)name[i];
	wide[i] = 0;
	set_description(GetCurrentThread(), wide);
#else
	(void)name;
#endif
}
