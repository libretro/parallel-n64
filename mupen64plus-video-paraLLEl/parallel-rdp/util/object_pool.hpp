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

#include <memory>
#include <new>
#include <vector>
#include <stdint.h>
#include <retro_atomic.h>
#include <algorithm>
#include <stdlib.h>
#include "aligned_alloc.hpp"

//#define OBJECT_POOL_DEBUG

namespace Util
{
template<typename T>
class ObjectPool
{
public:
	template<typename... P>
	T *allocate(P &&... p)
	{
#ifndef OBJECT_POOL_DEBUG
		if (vacants.empty())
		{
			unsigned num_objects = 64u << memory.size();
			T *ptr = static_cast<T *>(memalign_alloc(std::max(size_t(64), alignof(T)),
			                                         num_objects * sizeof(T)));
			if (!ptr)
				return nullptr;

			for (unsigned i = 0; i < num_objects; i++)
				vacants.push_back(&ptr[i]);

			memory.emplace_back(ptr);
		}

		T *ptr = vacants.back();
		vacants.pop_back();
		new(ptr) T(std::forward<P>(p)...);
		return ptr;
#else
		return new T(std::forward<P>(p)...);
#endif
	}

	void free(T *ptr)
	{
#ifndef OBJECT_POOL_DEBUG
		ptr->~T();
		vacants.push_back(ptr);
#else
		delete ptr;
#endif
	}

	void clear()
	{
#ifndef OBJECT_POOL_DEBUG
		vacants.clear();
		memory.clear();
#endif
	}

	// Pooled types befriend ObjectPool<T> for their private constructors;
	// ThreadSafeObjectPool constructs and destroys through these.
	template<typename... P>
	static T *construct_at(void *mem, P &&... p)
	{
		return new(mem) T(std::forward<P>(p)...);
	}

	static void destroy(T *ptr)
	{
		ptr->~T();
	}

protected:
#ifndef OBJECT_POOL_DEBUG
	std::vector<T *> vacants;

	struct MallocDeleter
	{
		void operator()(T *ptr)
		{
			memalign_free(ptr);
		}
	};

	std::vector<std::unique_ptr<T, MallocDeleter>> memory;
#endif
};

// Lock-free pool: any thread may allocate and free.
//
// Slots live in blocks that are never moved or freed until clear(); block
// k holds 64 << k slots. Free slots form a Treiber stack threaded through
// a separate per-block array of next indices (so a free slot's memory is
// never touched by the list). The stack head is one retro_atomic int:
// the low bits are a 1-based slot index (0 = empty), the high bits a tag
// bumped on every successful CAS, so a slot that is popped and pushed
// back between another thread's read and CAS cannot be mistaken for the
// old head (ABA). 18 index bits give 262,080 slots; the 14-bit tag would
// have to wrap 16,384 times inside one preempted CAS window to fool it.
template<typename T>
class ThreadSafeObjectPool
{
public:
	ThreadSafeObjectPool()
	{
		retro_atomic_int_init(&head, 0);
		retro_atomic_int_init(&block_count, 0);
		for (unsigned k = 0; k < MAX_BLOCKS; k++)
		{
			retro_atomic_ptr_init(&block_objs[k], nullptr);
			block_next[k] = nullptr;
		}
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
		uint32_t idx = pop();
		if (!idx)
			idx = grow();
		if (!idx)
			return nullptr;
		return ObjectPool<T>::construct_at(slot(idx), std::forward<P>(p)...);
#else
		return ObjectPool<T>::construct_at(::operator new(sizeof(T)), std::forward<P>(p)...);
#endif
	}

	void free(T *ptr)
	{
#ifndef OBJECT_POOL_DEBUG
		ObjectPool<T>::destroy(ptr);
		push_chain(index_of(ptr), index_of(ptr));
#else
		ObjectPool<T>::destroy(ptr);
		::operator delete(ptr);
#endif
	}

	// Releases every block. Outstanding objects are not destroyed, as
	// before. Not thread-safe: only for teardown.
	void clear()
	{
		for (unsigned k = 0; k < MAX_BLOCKS; k++)
		{
			void *objs = retro_atomic_load_acquire_ptr(&block_objs[k]);
			if (objs)
				memalign_free(objs);
			delete[] block_next[k];
			block_next[k] = nullptr;
			retro_atomic_ptr_init(&block_objs[k], nullptr);
		}
		retro_atomic_int_init(&head, 0);
		retro_atomic_int_init(&block_count, 0);
	}

private:
	enum : uint32_t
	{
		IDX_BITS = 18,
		IDX_MASK = (1u << IDX_BITS) - 1,
		MAX_BLOCKS = 12 // 64 * (2^12 - 1) = 262080 <= IDX_MASK
	};

	retro_atomic_int_t head;
	retro_atomic_int_t block_count;
	retro_atomic_ptr_t block_objs[MAX_BLOCKS];
	// Written before block_objs[k] is published, read only after it is seen.
	retro_atomic_int_t *block_next[MAX_BLOCKS];

	static uint32_t block_base(unsigned k) { return 64u * ((1u << k) - 1u); }
	static uint32_t block_cap(unsigned k) { return 64u << k; }

	// 1-based slot index -> block and offset.
	static unsigned block_of(uint32_t idx, uint32_t &offset)
	{
		uint32_t i = idx - 1;
		unsigned k = 0;
		while (i >= block_base(k + 1))
			k++;
		offset = i - block_base(k);
		return k;
	}

	T *slot(uint32_t idx)
	{
		uint32_t off;
		unsigned k = block_of(idx, off);
		return static_cast<T *>(retro_atomic_load_acquire_ptr(&block_objs[k])) + off;
	}

	retro_atomic_int_t *next_of(uint32_t idx)
	{
		uint32_t off;
		unsigned k = block_of(idx, off);
		return &block_next[k][off];
	}

	static int pack(uint32_t tag, uint32_t idx)
	{
		return int((tag << IDX_BITS) | idx);
	}

	uint32_t pop()
	{
		for (;;)
		{
			uint32_t h = uint32_t(retro_atomic_load_acquire_int(&head));
			uint32_t idx = h & IDX_MASK;
			if (!idx)
				return 0;
			uint32_t next = uint32_t(retro_atomic_load_relaxed_int(next_of(idx)));
			if (retro_atomic_cas_int(&head, int(h), pack((h >> IDX_BITS) + 1, next)))
				return idx;
		}
	}

	// Pushes first..last, already linked through next_of, in one CAS.
	void push_chain(uint32_t first, uint32_t last)
	{
		for (;;)
		{
			uint32_t h = uint32_t(retro_atomic_load_acquire_int(&head));
			retro_atomic_store_relaxed_int(next_of(last), int(h & IDX_MASK));
			if (retro_atomic_cas_int(&head, int(h), pack((h >> IDX_BITS) + 1, first)))
				return;
		}
	}

	// Adds a block, returns one of its slots and pushes the rest.
	uint32_t grow()
	{
		unsigned k = unsigned(retro_atomic_fetch_add_int(&block_count, 1));
		if (k >= MAX_BLOCKS)
			return 0;

		uint32_t cap = block_cap(k);
		void *objs = memalign_alloc(alignof(T) > 64 ? alignof(T) : 64, cap * sizeof(T));
		if (!objs)
			return 0;
		retro_atomic_int_t *next = new (std::nothrow) retro_atomic_int_t[cap];
		if (!next)
		{
			memalign_free(objs);
			return 0;
		}

		uint32_t first = block_base(k) + 1;
		for (uint32_t i = 0; i + 1 < cap; i++)
			retro_atomic_int_init(&next[i], int(first + i + 1));
		retro_atomic_int_init(&next[cap - 1], 0);

		block_next[k] = next;
		retro_atomic_store_release_ptr(&block_objs[k], objs);

		if (cap > 1)
			push_chain(first + 1, first + cap - 1);
		return first;
	}

	uint32_t index_of(T *ptr)
	{
		unsigned n = unsigned(retro_atomic_load_acquire_int(&block_count));
		if (n > MAX_BLOCKS)
			n = MAX_BLOCKS;
		for (unsigned k = 0; k < n; k++)
		{
			T *objs = static_cast<T *>(retro_atomic_load_acquire_ptr(&block_objs[k]));
			if (objs && ptr >= objs && ptr < objs + block_cap(k))
				return block_base(k) + uint32_t(ptr - objs) + 1;
		}
		return 0; // not ours; cannot happen for a pointer this pool handed out
	}
};
}
