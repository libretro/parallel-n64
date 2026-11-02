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
#define _POSIX_C_SOURCE 200112L
#endif

#include <stdlib.h>
#ifdef _WIN32
#include <malloc.h>
#endif
#include "aligned_alloc.h"

void *granite_memalign_alloc(size_t boundary, size_t size)
{
#ifdef _WIN32
	return _aligned_malloc(size, boundary);
#else
	void *ptr = NULL;
	/* posix_memalign wants a multiple of sizeof(void *) */
	if (boundary < sizeof(void *))
		boundary = sizeof(void *);
	if (posix_memalign(&ptr, boundary, size) != 0)
		return NULL;
	return ptr;
#endif
}

void granite_memalign_free(void *ptr)
{
#ifdef _WIN32
	_aligned_free(ptr);
#else
	free(ptr);
#endif
}

void *granite_heap_new(size_t size)
{
	void *ptr = malloc(size);
	if (!ptr)
		abort();
	return ptr;
}

void *granite_heap_new_aligned(size_t size, size_t align)
{
	void *ptr = granite_memalign_alloc(align, size);
	if (!ptr)
		abort();
	return ptr;
}
