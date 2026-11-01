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
struct DefaultDelete
{
	void operator()(T *t) const
	{
		delete t;
	}
};

/* A reference count: relaxed increments, acq_rel decrements. */
struct AtomicCount
{
#if defined(_MSC_VER) && !defined(__clang__)
	volatile long value;
	inline void init(uint32_t v) { value = long(v); }
	inline void add() { _InterlockedIncrement(&value); }
	inline bool sub_is_last() { return _InterlockedDecrement(&value) == 0; }
#else
	uint32_t value;
	inline void init(uint32_t v) { __atomic_store_n(&value, v, __ATOMIC_RELAXED); }
	inline void add() { __atomic_fetch_add(&value, 1u, __ATOMIC_RELAXED); }
	inline bool sub_is_last() { return __atomic_fetch_sub(&value, 1u, __ATOMIC_ACQ_REL) == 1; }
#endif
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

/* Builtins of GCC, Clang and MSVC alike */
#define UTIL_UNDERLYING_TYPE(T) __underlying_type(T)
#define UTIL_IS_BASE_OF(B, D) __is_base_of(B, D)

/* Out-of-line slow paths, and the index of the highest set bit */
#if defined(_MSC_VER)
#define UTIL_NOINLINE_COLD __declspec(noinline)
static inline unsigned util_bsr32(uint32_t v)
{
	unsigned long r;
	_BitScanReverse(&r, v);
	return (unsigned)r;
}
#else
#define UTIL_NOINLINE_COLD __attribute__((noinline, cold))
static inline unsigned util_bsr32(uint32_t v)
{
	return 31u - (unsigned)__builtin_clz(v);
}
#endif
