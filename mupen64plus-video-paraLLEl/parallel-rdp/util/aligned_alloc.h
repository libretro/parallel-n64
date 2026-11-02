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

#ifndef GRANITE_ALIGNED_ALLOC_H
#define GRANITE_ALIGNED_ALLOC_H

#include <stddef.h>
#include <retro_common_api.h>

#if defined(__GNUC__) || defined(__clang__)
#define GRANITE_MALLOC_ATTRS __attribute__((malloc, returns_nonnull, alloc_size(1)))
#else
#define GRANITE_MALLOC_ATTRS
#endif

RETRO_BEGIN_DECLS

/* boundary is a power of two; NULL when out of memory */
void *granite_memalign_alloc(size_t boundary, size_t size);
void granite_memalign_free(void *ptr);

/* What a new-expression's operator new does: never NULL, out of memory
 * ends the program. Out of line, as operator new is, so a call site is a
 * call and nothing else. */
GRANITE_MALLOC_ATTRS void *granite_heap_new(size_t size);
/* for types aligned past what malloc promises */
GRANITE_MALLOC_ATTRS void *granite_heap_new_aligned(size_t size, size_t align);

RETRO_END_DECLS

#endif
