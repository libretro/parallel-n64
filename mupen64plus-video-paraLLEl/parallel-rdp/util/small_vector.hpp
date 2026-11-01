/* Copyright (c) 2019-2022 Hans-Kristian Arntzen
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
#include <stdlib.h>
#include "util_lite.hpp"

namespace Util
{
// std::aligned_storage does not support size == 0, so roll our own.
template <typename T, size_t N>
class AlignedBuffer
{
public:
	T *data()
	{
		return reinterpret_cast<T *>(aligned_char);
	}

private:
	alignas(T) char aligned_char[sizeof(T) * N];
};

template <typename T>
class AlignedBuffer<T, 0>
{
public:
	T *data()
	{
		return nullptr;
	}
};

// An immutable version of SmallVector which erases type information about storage.
template <typename T>
class VectorView
{
public:
	T &operator[](size_t i)
	{
		return ptr[i];
	}

	const T &operator[](size_t i) const
	{
		return ptr[i];
	}

	bool empty() const
	{
		return buffer_size == 0;
	}

	size_t size() const
	{
		return buffer_size;
	}

	T *data()
	{
		return ptr;
	}

	const T *data() const
	{
		return ptr;
	}

	T *begin()
	{
		return ptr;
	}

	T *end()
	{
		return ptr + buffer_size;
	}

	const T *begin() const
	{
		return ptr;
	}

	const T *end() const
	{
		return ptr + buffer_size;
	}

	T &front()
	{
		return ptr[0];
	}

	const T &front() const
	{
		return ptr[0];
	}

	T &back()
	{
		return ptr[buffer_size - 1];
	}

	const T &back() const
	{
		return ptr[buffer_size - 1];
	}

	// Avoid sliced copies. Base class should only be read as a reference.
	VectorView(const VectorView &) = delete;
	void operator=(const VectorView &) = delete;

protected:
	VectorView() = default;
	T *ptr = nullptr;
	size_t buffer_size = 0;
};

// Simple vector which supports up to N elements inline, without malloc/free.
// We use a lot of throwaway vectors all over the place which triggers allocations.
// This class only implements the subset of std::vector we need in SPIRV-Cross.
// It is *NOT* a drop-in replacement in general projects.
template <typename T, size_t N = 8>
class SmallVector : public VectorView<T>
{
public:
	SmallVector()
	{
		this->ptr = stack_storage.data();
		buffer_capacity = N;
	}

	SmallVector(const T *arg_list_begin, const T *arg_list_end)
	    : SmallVector()
	{
		auto count = size_t(arg_list_end - arg_list_begin);
		reserve(count);
		for (size_t i = 0; i < count; i++, arg_list_begin++)
			UTIL_PLACEMENT_NEW(&this->ptr[i]) T(*arg_list_begin);
		this->buffer_size = count;
	}

	SmallVector(SmallVector &&other) noexcept : SmallVector()
	{
		*this = Util::move(other);
	}

	SmallVector &operator=(SmallVector &&other) noexcept
	{
		clear();
		if (other.ptr != other.stack_storage.data())
		{
			// Pilfer allocated pointer.
			if (this->ptr != stack_storage.data())
				free(this->ptr);
			this->ptr = other.ptr;
			this->buffer_size = other.buffer_size;
			buffer_capacity = other.buffer_capacity;
			other.ptr = nullptr;
			other.buffer_size = 0;
			other.buffer_capacity = 0;
		}
		else
		{
			// Need to move the stack contents individually.
			reserve(other.buffer_size);
			for (size_t i = 0; i < other.buffer_size; i++)
			{
				UTIL_PLACEMENT_NEW(&this->ptr[i]) T(Util::move(other.ptr[i]));
				other.ptr[i].~T();
			}
			this->buffer_size = other.buffer_size;
			other.buffer_size = 0;
		}
		return *this;
	}

	SmallVector(const SmallVector &other)
	    : SmallVector()
	{
		*this = other;
	}

	SmallVector &operator=(const SmallVector &other)
	{
		clear();
		reserve(other.buffer_size);
		for (size_t i = 0; i < other.buffer_size; i++)
			UTIL_PLACEMENT_NEW(&this->ptr[i]) T(other.ptr[i]);
		this->buffer_size = other.buffer_size;
		return *this;
	}

	explicit SmallVector(size_t count)
	    : SmallVector()
	{
		resize(count);
	}

	~SmallVector()
	{
		clear();
		if (this->ptr != stack_storage.data())
			free(this->ptr);
	}

	void clear()
	{
		for (size_t i = 0; i < this->buffer_size; i++)
			this->ptr[i].~T();
		this->buffer_size = 0;
	}

	void push_back(const T &t)
	{
		reserve(this->buffer_size + 1);
		UTIL_PLACEMENT_NEW(&this->ptr[this->buffer_size]) T(t);
		this->buffer_size++;
	}

	void push_back(T &&t)
	{
		reserve(this->buffer_size + 1);
		UTIL_PLACEMENT_NEW(&this->ptr[this->buffer_size]) T(Util::move(t));
		this->buffer_size++;
	}

	void pop_back()
	{
		// Work around false positive warning on GCC 8.3.
		// Calling pop_back on empty vector is undefined.
		if (!this->empty())
			resize(this->buffer_size - 1);
	}

	template <typename... Ts>
	void emplace_back(Ts &&... ts)
	{
		reserve(this->buffer_size + 1);
		UTIL_PLACEMENT_NEW(&this->ptr[this->buffer_size]) T(Util::forward<Ts>(ts)...);
		this->buffer_size++;
	}

	void reserve(size_t count)
	{
		if (count > buffer_capacity)
		{
			size_t target_capacity = buffer_capacity;
			if (target_capacity == 0)
				target_capacity = 1;
			if (target_capacity < N)
				target_capacity = N;

			while (target_capacity < count)
				target_capacity <<= 1u;

			T *new_buffer =
			    target_capacity > N ? static_cast<T *>(malloc(target_capacity * sizeof(T))) : stack_storage.data();

			if (!new_buffer)
				abort();

			// In case for some reason two allocations both come from same stack.
			if (new_buffer != this->ptr)
			{
				// We don't deal with types which can throw in move constructor.
				for (size_t i = 0; i < this->buffer_size; i++)
				{
					UTIL_PLACEMENT_NEW(&new_buffer[i]) T(Util::move(this->ptr[i]));
					this->ptr[i].~T();
				}
			}

			if (this->ptr != stack_storage.data())
				free(this->ptr);
			this->ptr = new_buffer;
			buffer_capacity = target_capacity;
		}
	}

	void insert(T *itr, const T *insert_begin, const T *insert_end)
	{
		auto count = size_t(insert_end - insert_begin);
		if (itr == this->end())
		{
			reserve(this->buffer_size + count);
			for (size_t i = 0; i < count; i++, insert_begin++)
				UTIL_PLACEMENT_NEW(&this->ptr[this->buffer_size + i]) T(*insert_begin);
			this->buffer_size += count;
		}
		else
		{
			if (this->buffer_size + count > buffer_capacity)
			{
				auto target_capacity = this->buffer_size + count;
				if (target_capacity == 0)
					target_capacity = 1;
				if (target_capacity < N)
					target_capacity = N;

				while (target_capacity < count)
					target_capacity <<= 1u;

				// Need to allocate new buffer. Move everything to a new buffer.
				T *new_buffer =
				    target_capacity > N ? static_cast<T *>(malloc(target_capacity * sizeof(T))) : stack_storage.data();
				if (!new_buffer)
					abort();

				// First, move elements from source buffer to new buffer.
				// We don't deal with types which can throw in move constructor.
				auto *target_itr = new_buffer;
				auto *original_source_itr = this->begin();

				if (new_buffer != this->ptr)
				{
					while (original_source_itr != itr)
					{
						UTIL_PLACEMENT_NEW(target_itr) T(Util::move(*original_source_itr));
						original_source_itr->~T();
						++original_source_itr;
						++target_itr;
					}
				}

				// Copy-construct new elements.
				for (auto *source_itr = insert_begin; source_itr != insert_end; ++source_itr, ++target_itr)
					UTIL_PLACEMENT_NEW(target_itr) T(*source_itr);

				// Move over the other half.
				if (new_buffer != this->ptr || insert_begin != insert_end)
				{
					while (original_source_itr != this->end())
					{
						UTIL_PLACEMENT_NEW(target_itr) T(Util::move(*original_source_itr));
						original_source_itr->~T();
						++original_source_itr;
						++target_itr;
					}
				}

				if (this->ptr != stack_storage.data())
					free(this->ptr);
				this->ptr = new_buffer;
				buffer_capacity = target_capacity;
			}
			else
			{
				// Move in place, need to be a bit careful about which elements are constructed and which are not.
				// Move the end and construct the new elements.
				auto *target_itr = this->end() + count;
				auto *source_itr = this->end();
				while (target_itr != this->end() && source_itr != itr)
				{
					--target_itr;
					--source_itr;
					UTIL_PLACEMENT_NEW(target_itr) T(Util::move(*source_itr));
				}

				// For already constructed elements we can move-assign.
				while (source_itr != itr)
					*--target_itr = Util::move(*--source_itr);

				// For the inserts which go to already constructed elements, we can do a plain copy.
				while (itr != this->end() && insert_begin != insert_end)
					*itr++ = *insert_begin++;

				// For inserts into newly allocated memory, we must copy-construct instead.
				while (insert_begin != insert_end)
				{
					UTIL_PLACEMENT_NEW(itr) T(*insert_begin);
					++itr;
					++insert_begin;
				}
			}

			this->buffer_size += count;
		}
	}

	void insert(T *itr, const T &value)
	{
		insert(itr, &value, &value + 1);
	}

	T *erase(T *itr)
	{
		T *dst;
		for (dst = itr; dst + 1 != this->end(); dst++)
			*dst = Util::move(dst[1]);
		this->ptr[--this->buffer_size].~T();
		return itr;
	}

	void erase(T *start_erase, T *end_erase)
	{
		if (end_erase == this->end())
		{
			resize(size_t(start_erase - this->begin()));
		}
		else
		{
			auto new_size = this->buffer_size - (end_erase - start_erase);
			T *src;
			for (src = end_erase; src != this->end(); src++, start_erase++)
				*start_erase = Util::move(*src);
			resize(new_size);
		}
	}

	void resize(size_t new_size)
	{
		if (new_size < this->buffer_size)
		{
			for (size_t i = new_size; i < this->buffer_size; i++)
				this->ptr[i].~T();
		}
		else if (new_size > this->buffer_size)
		{
			reserve(new_size);
			for (size_t i = this->buffer_size; i < new_size; i++)
				UTIL_PLACEMENT_NEW(&this->ptr[i]) T();
		}

		this->buffer_size = new_size;
	}

private:
	size_t buffer_capacity = 0;
	AlignedBuffer<T, N> stack_storage;
};
}
