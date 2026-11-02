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

#include <stddef.h>
#include "util_lite.hpp"

namespace Util
{
class SingleThreadCounter
{
public:
	inline void add_ref()
	{
		count++;
	}

	inline bool release()
	{
		return --count == 0;
	}

	inline bool is_one() const
	{
		return count == 1;
	}

private:
	uint32_t count = 1;
};

class MultiThreadCounter
{
public:
	MultiThreadCounter()
	{
		count.init(1);
	}

	inline void add_ref()
	{
		count.add();
	}

	inline bool release()
	{
		return count.sub_is_last();
	}

	inline bool is_one() const
	{
		return count.is_one();
	}

private:
	AtomicCount count;
};

template <typename T>
class IntrusivePtr;

template <typename T, typename Deleter = DefaultDelete<T>, typename ReferenceOps = SingleThreadCounter>
class IntrusivePtrEnabled
{
public:
	using IntrusivePtrType = IntrusivePtr<T>;
	using EnabledBase = T;
	using EnabledDeleter = Deleter;
	using EnabledReferenceOp = ReferenceOps;

	void release_reference()
	{
		if (reference_count.release())
			Deleter()(static_cast<T *>(this));
	}

	void add_reference()
	{
		reference_count.add_ref();
	}

	/* No other handle to it: none could be in use, or start to be. */
	bool only_reference() const
	{
		return reference_count.is_one();
	}

	IntrusivePtrEnabled() = default;

	IntrusivePtrEnabled(const IntrusivePtrEnabled &) = delete;

	void operator=(const IntrusivePtrEnabled &) = delete;

protected:
	Util::IntrusivePtr<T> reference_from_this();

private:
	ReferenceOps reference_count;
};

template <typename T>
class IntrusivePtr
{
public:
	template <typename U>
	friend class IntrusivePtr;

	IntrusivePtr() = default;

	explicit IntrusivePtr(T *handle)
		: data(handle)
	{
	}

	T &operator*()
	{
		return *data;
	}

	const T &operator*() const
	{
		return *data;
	}

	T *operator->()
	{
		return data;
	}

	const T *operator->() const
	{
		return data;
	}

	explicit operator bool() const
	{
		return data != nullptr;
	}

	bool operator==(const IntrusivePtr &other) const
	{
		return data == other.data;
	}

	bool operator!=(const IntrusivePtr &other) const
	{
		return data != other.data;
	}

	T *get()
	{
		return data;
	}

	const T *get() const
	{
		return data;
	}

	void reset()
	{
		using ReferenceBase = IntrusivePtrEnabled<
				typename T::EnabledBase,
				typename T::EnabledDeleter,
				typename T::EnabledReferenceOp>;

		// Static up-cast here to avoid potential issues with multiple intrusive inheritance.
		// Also makes sure that the pointer type actually inherits from this type.
		if (data)
			static_cast<ReferenceBase *>(data)->release_reference();
		data = nullptr;
	}

	template <typename U>
	IntrusivePtr &operator=(const IntrusivePtr<U> &other)
	{
		static_assert(UTIL_IS_BASE_OF(T, U),
		              "Cannot safely assign downcasted intrusive pointers.");

		using ReferenceBase = IntrusivePtrEnabled<
				typename T::EnabledBase,
				typename T::EnabledDeleter,
				typename T::EnabledReferenceOp>;

		reset();
		data = static_cast<T *>(other.data);

		// Static up-cast here to avoid potential issues with multiple intrusive inheritance.
		// Also makes sure that the pointer type actually inherits from this type.
		if (data)
			static_cast<ReferenceBase *>(data)->add_reference();
		return *this;
	}

	IntrusivePtr &operator=(const IntrusivePtr &other)
	{
		using ReferenceBase = IntrusivePtrEnabled<
				typename T::EnabledBase,
				typename T::EnabledDeleter,
				typename T::EnabledReferenceOp>;

		if (this != &other)
		{
			reset();
			data = other.data;
			if (data)
				static_cast<ReferenceBase *>(data)->add_reference();
		}
		return *this;
	}

	template <typename U>
	IntrusivePtr(const IntrusivePtr<U> &other)
	{
		*this = other;
	}

	IntrusivePtr(const IntrusivePtr &other)
	{
		*this = other;
	}

	~IntrusivePtr()
	{
		reset();
	}

	template <typename U>
	IntrusivePtr &operator=(IntrusivePtr<U> &&other) noexcept
	{
		reset();
		data = other.data;
		other.data = nullptr;
		return *this;
	}

	IntrusivePtr &operator=(IntrusivePtr &&other) noexcept
	{
		if (this != &other)
		{
			reset();
			data = other.data;
			other.data = nullptr;
		}
		return *this;
	}

	template <typename U>
	IntrusivePtr(IntrusivePtr<U> &&other) noexcept
	{
		*this = Util::move(other);
	}

	template <typename U>
	IntrusivePtr(IntrusivePtr &&other) noexcept
	{
		*this = Util::move(other);
	}

	T *release() &
	{
		T *ret = data;
		data = nullptr;
		return ret;
	}

	T *release() &&
	{
		T *ret = data;
		data = nullptr;
		return ret;
	}

private:
	T *data = nullptr;
};

// A reference held without a destructor, for objects of static storage:
// they are dropped by hand while the device is up, rather than by a
// destructor registered to run at exit, after it is gone. Zero is empty.
template <typename T>
struct HeldHandle
{
	T *data;

	// The reference goes first, and the slot is written after it,
	// so a temporary handed in is known empty when it dies.
	static void drop(T *object)
	{
		IntrusivePtr<T> last(object);
	}

	void reset()
	{
		drop(data);
		data = nullptr;
	}

	HeldHandle &operator=(const IntrusivePtr<T> &handle)
	{
		IntrusivePtr<T> keep(handle);
		drop(data);
		data = keep.release();
		return *this;
	}

	HeldHandle &operator=(IntrusivePtr<T> &&handle)
	{
		drop(data);
		data = handle.release();
		return *this;
	}

	explicit operator bool() const
	{
		return data != nullptr;
	}

	// a handle of its own to it
	operator IntrusivePtr<T>() const
	{
		IntrusivePtr<T> handle;
		IntrusivePtr<T> borrowed(data);
		handle = borrowed;
		borrowed.release();
		return handle;
	}
};

template <typename T, typename Deleter, typename ReferenceOps>
IntrusivePtr<T> IntrusivePtrEnabled<T, Deleter, ReferenceOps>::reference_from_this()
{
	add_reference();
	return IntrusivePtr<T>(static_cast<T *>(this));
}

template <typename Derived>
using DerivedIntrusivePtrType = IntrusivePtr<Derived>;

template <typename T, typename... P>
DerivedIntrusivePtrType<T> make_handle(P &&... p)
{
	return DerivedIntrusivePtrType<T>(Util::create_init<T>(Util::forward<P>(p)...));
}

template <typename Base, typename Derived, typename... P>
typename Base::IntrusivePtrType make_derived_handle(P &&... p)
{
	return typename Base::IntrusivePtrType(Util::create_init<Derived>(Util::forward<P>(p)...));
}

template <typename T>
using ThreadSafeIntrusivePtrEnabled = IntrusivePtrEnabled<T, DefaultDelete<T>, MultiThreadCounter>;
}
