/* Copyright (c) 2026 libretro team
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

#pragma once

/* What util/ took from <utility>, <type_traits>, <memory> and <atomic>,
 * without the standard library: the same code, through the compilers'
 * own builtins. */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include "aligned_alloc.h"
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif

namespace Util
{
template <typename T> struct RemoveReference { typedef T type; };
template <typename T> struct RemoveReference<T &> { typedef T type; };
template <typename T> struct RemoveReference<T &&> { typedef T type; };

template <typename T>
static inline typename RemoveReference<T>::type &&move(T &&t) noexcept
{
	return static_cast<typename RemoveReference<T>::type &&>(t);
}

template <typename T>
static inline T &&forward(typename RemoveReference<T>::type &t) noexcept
{
	return static_cast<T &&>(t);
}

template <typename T>
static inline T &&forward(typename RemoveReference<T>::type &&t) noexcept
{
	return static_cast<T &&>(t);
}

template <typename T>
static inline const T &min_of(const T &a, const T &b)
{
	return b < a ? b : a;
}

template <typename T>
static inline const T &max_of(const T &a, const T &b)
{
	return a < b ? b : a;
}

template <typename T>
static inline void swap(T &a, T &b)
{
	T t(Util::move(a));
	a = Util::move(b);
	b = Util::move(t);
}

/* Copies a C string, truncating to size - 1 bytes and terminating. */
static inline void copy_string(char *dst, size_t size, const char *src)
{
	size_t i = 0;
	if (!size)
		return;
	if (src)
		for (; i + 1 < size && src[i]; i++)
			dst[i] = src[i];
	dst[i] = '\0';
}

/* below: delete on the C heap */
template <typename T>
static inline void destroy(T *t);

template <typename T>
struct DefaultDelete
{
	void operator()(T *t) const
	{
		destroy(t);
	}
};

}

/* Placement new without <new>: a tag of our own picks this overload. */
namespace Util
{
struct PlacementTag
{
};
}

/* Not noexcept: a non-throwing allocation function makes every
   new-expression test its result for null. */
inline void *operator new(size_t, Util::PlacementTag, void *p)
{
	return p;
}

inline void operator delete(void *, Util::PlacementTag, void *) noexcept
{
}

#define UTIL_PLACEMENT_NEW(p) new (::Util::PlacementTag(), (p))

/* Objects on the C heap: malloc and free, with the constructor and the
   destructor run in place, so nothing calls the C++ runtime's operator
   new or delete. A type aligned past 8 bytes, which malloc does not
   promise everywhere (32-bit MSVC), comes from memalign. */
namespace Util
{
/* Clang and MSVC name it as the standard trait does; GCC up to 13 has
   only the older builtin, which Clang now warns of. */
#if defined(__clang__) || defined(_MSC_VER)
#define UTIL_IS_TRIVIALLY_DESTRUCTIBLE(T) __is_trivially_destructible(T)
#else
#define UTIL_IS_TRIVIALLY_DESTRUCTIBLE(T) __has_trivial_destructor(T)
#endif

template <typename T>
static inline void *heap_alloc(size_t size)
{
	return alignof(T) > 8 ? granite_heap_new_aligned(size, alignof(T)) : granite_heap_new(size);
}

template <typename T>
static inline void heap_free(void *p)
{
	if (alignof(T) > 8)
		granite_memalign_free(p);
	else
		free(p);
}

/* new T(p...) */
template <typename T, typename P0, typename... P>
static inline T *create(P0 &&p0, P &&... p)
{
	void *mem = heap_alloc<T>(sizeof(T));
	return UTIL_PLACEMENT_NEW(mem) T(forward<P0>(p0), forward<P>(p)...);
}

/* new T(p...) for any number of arguments: with none, T() (zeroed where T
   has no constructor of its own), as a template forwarding a pack spells it */
template <typename T, typename... P>
static inline T *create_init(P &&... p)
{
	void *mem = heap_alloc<T>(sizeof(T));
	return UTIL_PLACEMENT_NEW(mem) T(forward<P>(p)...);
}

/* new T: default-initialized, as a new-expression without () leaves it */
template <typename T>
static inline T *create()
{
	void *mem = heap_alloc<T>(sizeof(T));
	return UTIL_PLACEMENT_NEW(mem) T;
}

/* delete t: a destructor that does anything runs out of line, once per
   type, as a deleting destructor does, rather than at every call */
template <typename T>
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#elif defined(_MSC_VER)
__declspec(noinline)
#endif
void destroy_object(T *t)
{
	t->~T();
	heap_free<T>(t);
}

template <typename T>
static inline void destroy(T *t)
{
	if (!t)
		return;
	if (UTIL_IS_TRIVIALLY_DESTRUCTIBLE(T))
		heap_free<T>(t);
	else
		destroy_object(t);
}

/* new T[n] (default-initialized), and delete[] */
template <typename T>
static inline T *create_array(size_t n)
{
	size_t i;
	T *a = static_cast<T *>(heap_alloc<T>(n * sizeof(T)));
	for (i = 0; i < n; i++)
		UTIL_PLACEMENT_NEW(&a[i]) T;
	return a;
}

template <typename T>
static inline void destroy_array(T *a, size_t n)
{
	if (a)
	{
		while (n--)
			a[n].~T();
		heap_free<T>(a);
	}
}
}

/* Builtins of GCC, Clang and MSVC alike */
#define UTIL_UNDERLYING_TYPE(T) __underlying_type(T)
#define UTIL_IS_BASE_OF(B, D) __is_base_of(B, D)

/* Out-of-line slow paths, and the index of the highest set bit */
#if defined(_MSC_VER)
#define UTIL_NOINLINE_COLD __declspec(noinline)
#define UTIL_NOINLINE __declspec(noinline)
static inline unsigned util_bsr32(uint32_t v)
{
	unsigned long r;
	_BitScanReverse(&r, v);
	return (unsigned)r;
}
#else
#define UTIL_NOINLINE_COLD __attribute__((noinline, cold))
#define UTIL_NOINLINE __attribute__((noinline))
static inline unsigned util_bsr32(uint32_t v)
{
	return 31u - (unsigned)__builtin_clz(v);
}
#endif

namespace Util
{
/* Up to N owned objects, deleted on clear() and destruction. */
template <typename T, unsigned N>
class OwnedPtrArray
{
public:
	OwnedPtrArray() = default;
	~OwnedPtrArray()
	{
		clear();
	}
	OwnedPtrArray(const OwnedPtrArray &) = delete;
	void operator=(const OwnedPtrArray &) = delete;

	void push_back(T *p)
	{
		items[count++] = p;
	}

	void clear()
	{
		unsigned i;
		for (i = 0; i < count; i++)
			destroy(items[i]);
		count = 0;
	}

	size_t size() const { return count; }
	bool empty() const { return count == 0; }
	T *&operator[](size_t i) { return items[i]; }
	T *const &operator[](size_t i) const { return items[i]; }
	T **begin() { return items; }
	T **end() { return items + count; }
	T *const *begin() const { return items; }
	T *const *end() const { return items + count; }

private:
	T *items[N];
	unsigned count = 0;
};
}
