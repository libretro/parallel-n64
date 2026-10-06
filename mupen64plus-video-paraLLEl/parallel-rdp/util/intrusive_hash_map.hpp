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

#include "hash.hpp"
#include "intrusive_list.hpp"
#include "object_pool.hpp"
#include <retro_atomic.h>
#include <assert.h>
#include <vector>

namespace Util
{
template <typename T>
class IntrusiveHashMapEnabled : public IntrusiveListEnabled<T>
{
public:
	IntrusiveHashMapEnabled() = default;
	IntrusiveHashMapEnabled(Util::Hash hash)
		: intrusive_hashmap_key(hash)
	{
	}

	void set_hash(Util::Hash hash)
	{
		intrusive_hashmap_key = hash;
	}

	Util::Hash get_hash() const
	{
		return intrusive_hashmap_key;
	}

private:
	Hash intrusive_hashmap_key = 0;
};

template <typename T>
struct IntrusivePODWrapper : public IntrusiveHashMapEnabled<IntrusivePODWrapper<T>>
{
	template <typename U>
	explicit IntrusivePODWrapper(U&& value_)
		: value(std::forward<U>(value_))
	{
	}

	IntrusivePODWrapper() = default;

	T& get()
	{
		return value;
	}

	const T& get() const
	{
		return value;
	}

	T value = {};
};

// This HashMap is non-owning. It just arranges a list of pointers.
// It's kind of special purpose container used by the Vulkan backend.
// Dealing with memory ownership is done through composition by a different class.
// T must inherit from IntrusiveHashMapEnabled<T>.
// Each instance of T can only be part of one hashmap.

template <typename T>
class IntrusiveHashMapHolder
{
public:
	enum { InitialSize = 16, InitialLoadCount = 3 };

	T *find(Hash hash) const
	{
		if (values.empty())
			return nullptr;

		Hash hash_mask = values.size() - 1;
		auto masked = hash & hash_mask;
		for (unsigned i = 0; i < load_count; i++)
		{
			if (values[masked] && get_hash(values[masked]) == hash)
				return values[masked];
			masked = (masked + 1) & hash_mask;
		}

		return nullptr;
	}

	template <typename P>
	bool find_and_consume_pod(Hash hash, P &p) const
	{
		T *t = find(hash);
		if (t)
		{
			p = t->get();
			return true;
		}
		else
			return false;
	}

	// Inserts, if value already exists, insertion does not happen.
	// Return value is the data which is not part of the hashmap.
	// It should be deleted or similar.
	// Returns nullptr if nothing was in the hashmap for this key.
	T *insert_yield(T *&value)
	{
		if (values.empty())
			grow();

		Hash hash_mask = values.size() - 1;
		auto hash = get_hash(value);
		auto masked = hash & hash_mask;

		for (unsigned i = 0; i < load_count; i++)
		{
			if (values[masked] && get_hash(values[masked]) == hash)
			{
				T *ret = value;
				value = values[masked];
				return ret;
			}
			else if (!values[masked])
			{
				values[masked] = value;
				list.insert_front(value);
				return nullptr;
			}
			masked = (masked + 1) & hash_mask;
		}

		grow();
		return insert_yield(value);
	}

	T *insert_replace(T *value)
	{
		if (values.empty())
			grow();

		Hash hash_mask = values.size() - 1;
		auto hash = get_hash(value);
		auto masked = hash & hash_mask;

		for (unsigned i = 0; i < load_count; i++)
		{
			if (values[masked] && get_hash(values[masked]) == hash)
			{
				std::swap(values[masked], value);
				list.erase(value);
				list.insert_front(values[masked]);
				return value;
			}
			else if (!values[masked])
			{
				assert(!values[masked]);
				values[masked] = value;
				list.insert_front(value);
				return nullptr;
			}
			masked = (masked + 1) & hash_mask;
		}

		grow();
		return insert_replace(value);
	}

	T *erase(Hash hash)
	{
		Hash hash_mask = values.size() - 1;
		auto masked = hash & hash_mask;

		for (unsigned i = 0; i < load_count; i++)
		{
			if (values[masked] && get_hash(values[masked]) == hash)
			{
				auto *value = values[masked];
				list.erase(value);
				values[masked] = nullptr;
				return value;
			}
			masked = (masked + 1) & hash_mask;
		}
		return nullptr;
	}

	void erase(T *value)
	{
		erase(get_hash(value));
	}

	void clear()
	{
		list.clear();
		values.clear();
		load_count = 0;
	}

	typename IntrusiveList<T>::Iterator begin() const
	{
		return list.begin();
	}

	typename IntrusiveList<T>::Iterator end() const
	{
		return list.end();
	}

	IntrusiveList<T> &inner_list()
	{
		return list;
	}

	const IntrusiveList<T> &inner_list() const
	{
		return list;
	}

private:

	inline bool compare_key(Hash masked, Hash hash) const
	{
		return get_key_for_index(masked) == hash;
	}

	inline Hash get_hash(const T *value) const
	{
		return static_cast<const IntrusiveHashMapEnabled<T> *>(value)->get_hash();
	}

	inline Hash get_key_for_index(Hash masked) const
	{
		return get_hash(values[masked]);
	}

	bool insert_inner(T *value)
	{
		Hash hash_mask = values.size() - 1;
		auto hash = get_hash(value);
		auto masked = hash & hash_mask;

		for (unsigned i = 0; i < load_count; i++)
		{
			if (!values[masked])
			{
				values[masked] = value;
				return true;
			}
			masked = (masked + 1) & hash_mask;
		}
		return false;
	}

	void grow()
	{
		bool success;
		do
		{
			for (auto &v : values)
				v = nullptr;

			if (values.empty())
			{
				values.resize(InitialSize);
				load_count = InitialLoadCount;
				//LOGI("Growing hashmap to %u elements.\n", InitialSize);
			}
			else
			{
				values.resize(values.size() * 2);
				//LOGI("Growing hashmap to %u elements.\n", unsigned(values.size()));
				load_count++;
			}

			// Re-insert.
			success = true;
			for (auto &t : list)
			{
				if (!insert_inner(&t))
				{
					success = false;
					break;
				}
			}
		} while (!success);
	}

	std::vector<T *> values;
	IntrusiveList<T> list;
	unsigned load_count = 0;
};

template <typename T>
class IntrusiveHashMap
{
public:
	~IntrusiveHashMap()
	{
		clear();
	}

	IntrusiveHashMap() = default;
	IntrusiveHashMap(const IntrusiveHashMap &) = delete;
	void operator=(const IntrusiveHashMap &) = delete;

	void clear()
	{
		auto &list = hashmap.inner_list();
		auto itr = list.begin();
		while (itr != list.end())
		{
			auto *to_free = itr.get();
			itr = list.erase(itr);
			pool.free(to_free);
		}

		hashmap.clear();
	}

	T *find(Hash hash) const
	{
		return hashmap.find(hash);
	}

	T &operator[](Hash hash)
	{
		auto *t = find(hash);
		if (!t)
			t = emplace_yield(hash);
		return *t;
	}

	template <typename P>
	bool find_and_consume_pod(Hash hash, P &p) const
	{
		return hashmap.find_and_consume_pod(hash, p);
	}

	void erase(T *value)
	{
		hashmap.erase(value);
		pool.free(value);
	}

	void erase(Hash hash)
	{
		auto *value = hashmap.erase(hash);
		if (value)
			pool.free(value);
	}

	template <typename... P>
	T *emplace_replace(Hash hash, P&&... p)
	{
		T *t = allocate(std::forward<P>(p)...);
		return insert_replace(hash, t);
	}

	template <typename... P>
	T *emplace_yield(Hash hash, P&&... p)
	{
		T *t = allocate(std::forward<P>(p)...);
		return insert_yield(hash, t);
	}

	template <typename... P>
	T *allocate(P&&... p)
	{
		return pool.allocate(std::forward<P>(p)...);
	}

	void free(T *value)
	{
		pool.free(value);
	}

	T *insert_replace(Hash hash, T *value)
	{
		static_cast<IntrusiveHashMapEnabled<T> *>(value)->set_hash(hash);
		T *to_delete = hashmap.insert_replace(value);
		if (to_delete)
			pool.free(to_delete);
		return value;
	}

	T *insert_yield(Hash hash, T *value)
	{
		static_cast<IntrusiveHashMapEnabled<T> *>(value)->set_hash(hash);
		T *to_delete = hashmap.insert_yield(value);
		if (to_delete)
			pool.free(to_delete);
		return value;
	}

	typename IntrusiveList<T>::Iterator begin() const
	{
		return hashmap.begin();
	}

	typename IntrusiveList<T>::Iterator end() const
	{
		return hashmap.end();
	}

	IntrusiveHashMap &get_thread_unsafe()
	{
		return *this;
	}

	const IntrusiveHashMap &get_thread_unsafe() const
	{
		return *this;
	}

private:
	IntrusiveHashMapHolder<T> hashmap;
	ObjectPool<T> pool;
};

template <typename T>
using IntrusiveHashMapWrapper = IntrusiveHashMap<IntrusivePODWrapper<T>>;

// A special purpose hashmap which is split into a read-only, immutable portion and a thread-safe one.
// User can move read-write thread-safe portion to read-only portion when user knows it's safe to do so.
//
// The thread-safe portion takes no lock. It is insert-only between
// move_to_read_only()/clear() calls: a fixed array of buckets, each a
// singly linked chain whose head is a retro_atomic pointer. A lookup walks
// the chain with acquire loads and never blocks. An insert scans the
// chain for the hash, then publishes a new link at the head with one CAS;
// if the CAS loses, only the links added since the scan are checked
// again, and if one of them has the same hash the newcomer yields (it is
// freed and the existing value returned), exactly as insert_yield did
// under the lock.
template <typename T>
class ThreadSafeIntrusiveHashMapReadCached
{
	struct Link;

public:
	ThreadSafeIntrusiveHashMapReadCached()
	{
		for (auto &b : buckets)
			retro_atomic_ptr_init(&b, nullptr);
	}

	~ThreadSafeIntrusiveHashMapReadCached()
	{
		clear();
	}

	ThreadSafeIntrusiveHashMapReadCached(const ThreadSafeIntrusiveHashMapReadCached &) = delete;
	void operator=(const ThreadSafeIntrusiveHashMapReadCached &) = delete;

	T *find(Hash hash) const
	{
		T *t = read_only.find(hash);
		if (t)
			return t;
		return find_rw(hash);
	}

	// Not thread-safe: only when the caller knows nobody else is using the map.
	void move_to_read_only()
	{
		for (auto &b : buckets)
		{
			Link *l = static_cast<Link *>(retro_atomic_load_acquire_ptr(&b));
			retro_atomic_ptr_init(&b, nullptr);
			while (l)
			{
				Link *next = static_cast<Link *>(retro_atomic_load_relaxed_ptr(&l->next));
				T *to_move = l->value;
				T *to_delete = read_only.insert_yield(to_move);
				if (to_delete)
					object_pool.free(to_delete);
				delete l;
				l = next;
			}
		}
	}

	template <typename P>
	bool find_and_consume_pod(Hash hash, P &p) const
	{
		if (read_only.find_and_consume_pod(hash, p))
			return true;
		T *t = find_rw(hash);
		if (!t)
			return false;
		p = t->get();
		return true;
	}

	// Not thread-safe: teardown only.
	void clear()
	{
		auto &list = read_only.inner_list();
		auto itr = list.begin();
		while (itr != list.end())
		{
			auto *to_free = itr.get();
			itr = list.erase(itr);
			object_pool.free(to_free);
		}
		read_only.clear();

		for (auto &b : buckets)
		{
			Link *l = static_cast<Link *>(retro_atomic_load_acquire_ptr(&b));
			retro_atomic_ptr_init(&b, nullptr);
			while (l)
			{
				Link *next = static_cast<Link *>(retro_atomic_load_relaxed_ptr(&l->next));
				object_pool.free(l->value);
				delete l;
				l = next;
			}
		}
	}

	template <typename... P>
	T *allocate(P&&... p)
	{
		return object_pool.allocate(std::forward<P>(p)...);
	}

	void free(T *ptr)
	{
		object_pool.free(ptr);
	}

	T *insert_yield(Hash hash, T *value)
	{
		static_cast<IntrusiveHashMapEnabled<T> *>(value)->set_hash(hash);

		Link *link = new Link;
		link->hash = hash;
		link->value = value;

		retro_atomic_ptr_t &bucket = buckets[hash & (NUM_BUCKETS - 1)];
		Link *head = static_cast<Link *>(retro_atomic_load_acquire_ptr(&bucket));
		Link *scanned_to = nullptr;

		for (;;)
		{
			// Only links newer than the last scan can hold a duplicate.
			for (Link *l = head; l != scanned_to; l = static_cast<Link *>(retro_atomic_load_acquire_ptr(&l->next)))
			{
				if (l->hash == hash)
				{
					delete link;
					object_pool.free(value);
					return l->value;
				}
			}
			scanned_to = head;

			retro_atomic_store_relaxed_ptr(&link->next, head);
			if (retro_atomic_cas_ptr(&bucket, head, link))
				return value;
			head = static_cast<Link *>(retro_atomic_load_acquire_ptr(&bucket));
		}
	}

	template <typename... P>
	T *emplace_yield(Hash hash, P&&... p)
	{
		T *t = allocate(std::forward<P>(p)...);
		return insert_yield(hash, t);
	}

	IntrusiveHashMapHolder<T> &get_read_only()
	{
		return read_only;
	}

	// Iterates the thread-safe portion. Safe against concurrent inserts:
	// it sees every value published before it reaches that bucket.
	class ReadWriteRange
	{
	public:
		class Iterator
		{
		public:
			Iterator(const ThreadSafeIntrusiveHashMapReadCached *map_, unsigned bucket_)
				: map(map_), bucket(bucket_), link(nullptr)
			{
				advance_bucket();
			}

			T &operator*() const { return *link->value; }
			T *operator->() const { return link->value; }
			bool operator!=(const Iterator &other) const { return link != other.link || bucket != other.bucket; }
			bool operator==(const Iterator &other) const { return !(*this != other); }

			Iterator &operator++()
			{
				link = static_cast<Link *>(retro_atomic_load_acquire_ptr(&link->next));
				if (!link)
				{
					bucket++;
					advance_bucket();
				}
				return *this;
			}

		private:
			const ThreadSafeIntrusiveHashMapReadCached *map;
			unsigned bucket;
			Link *link;

			void advance_bucket()
			{
				while (bucket < NUM_BUCKETS)
				{
					link = static_cast<Link *>(retro_atomic_load_acquire_ptr(
							const_cast<retro_atomic_ptr_t *>(&map->buckets[bucket])));
					if (link)
						return;
					bucket++;
				}
				link = nullptr;
			}
		};

		explicit ReadWriteRange(const ThreadSafeIntrusiveHashMapReadCached *map_) : map(map_) {}
		Iterator begin() const { return Iterator(map, 0); }
		Iterator end() const { return Iterator(map, NUM_BUCKETS); }

	private:
		const ThreadSafeIntrusiveHashMapReadCached *map;
	};

	ReadWriteRange get_read_write()
	{
		return ReadWriteRange(this);
	}

private:
	enum { NUM_BUCKETS = 256 };

	struct Link
	{
		retro_atomic_ptr_t next;
		Hash hash;
		T *value;
	};

	IntrusiveHashMapHolder<T> read_only;
	retro_atomic_ptr_t buckets[NUM_BUCKETS];
	ThreadSafeObjectPool<T> object_pool;

	T *find_rw(Hash hash) const
	{
		Link *l = static_cast<Link *>(retro_atomic_load_acquire_ptr(
				const_cast<retro_atomic_ptr_t *>(&buckets[hash & (NUM_BUCKETS - 1)])));
		for (; l; l = static_cast<Link *>(retro_atomic_load_acquire_ptr(&l->next)))
			if (l->hash == hash)
				return l->value;
		return nullptr;
	}
};
}
