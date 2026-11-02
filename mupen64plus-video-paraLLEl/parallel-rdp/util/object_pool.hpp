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

#pragma once

#include <stdint.h>
#include <retro_atomic.h>
#include <stdlib.h>
#include "util_lite.hpp"
#include "aligned_alloc.h"
#include "pool.h"

//#define OBJECT_POOL_DEBUG

namespace Util
{
// Typed front ends to pool.c: these construct and destroy, the C pools
// hold the slots.
template<typename T>
class ObjectPool
{
public:
	template<typename... P>
	T *allocate(P &&... p)
	{
#ifndef OBJECT_POOL_DEBUG
		void *mem = granite_pool_alloc(&pool);
		if (!mem)
			return nullptr;
		return UTIL_PLACEMENT_NEW(mem) T(Util::forward<P>(p)...);
#else
		return Util::create_init<T>(Util::forward<P>(p)...);
#endif
	}

	void free(T *ptr)
	{
#ifndef OBJECT_POOL_DEBUG
		ptr->~T();
		granite_pool_release(&pool, ptr);
#else
		Util::destroy(ptr);
#endif
	}

	void clear()
	{
		granite_pool_clear(&pool);
	}

	ObjectPool()
	{
		granite_pool_init(&pool, sizeof(T), alignof(T));
	}

	ObjectPool(const ObjectPool &) = delete;
	void operator=(const ObjectPool &) = delete;

	~ObjectPool()
	{
		clear();
	}

	// Pooled types befriend ObjectPool<T> for their private constructors;
	// ThreadSafeObjectPool constructs and destroys through these.
	template<typename... P>
	static T *construct_at(void *mem, P &&... p)
	{
		return UTIL_PLACEMENT_NEW(mem) T(Util::forward<P>(p)...);
	}

	static void destroy(T *ptr)
	{
		ptr->~T();
	}

private:
	granite_pool_t pool;
};

// Any thread may allocate and free, without a lock.
template<typename T>
class ThreadSafeObjectPool
{
public:
	ThreadSafeObjectPool()
	{
		granite_lf_pool_init(&pool, sizeof(T), alignof(T));
	}

	~ThreadSafeObjectPool()
	{
		clear();
	}

	ThreadSafeObjectPool(const ThreadSafeObjectPool &) = delete;
	void operator=(const ThreadSafeObjectPool &) = delete;

	template<typename... P>
	T *allocate(P &&... p)
	{
#ifndef OBJECT_POOL_DEBUG
		void *mem = granite_lf_pool_alloc(&pool);
		if (!mem)
			return nullptr;
		return ObjectPool<T>::construct_at(mem, Util::forward<P>(p)...);
#else
		return ObjectPool<T>::construct_at(Util::heap_alloc<T>(sizeof(T)), Util::forward<P>(p)...);
#endif
	}

	void free(T *ptr)
	{
		ObjectPool<T>::destroy(ptr);
#ifndef OBJECT_POOL_DEBUG
		granite_lf_pool_release(&pool, ptr);
#else
		Util::heap_free<T>(ptr);
#endif
	}

	// Releases every block. Outstanding objects are not destroyed. Not
	// thread-safe: only for teardown.
	void clear()
	{
		granite_lf_pool_clear(&pool);
	}

private:
	granite_lf_pool_t pool;
};
}
