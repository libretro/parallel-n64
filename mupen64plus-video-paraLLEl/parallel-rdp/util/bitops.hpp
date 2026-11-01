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

#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace Util
{
#ifdef __GNUC__
#define leading_zeroes(x) ((x) == 0 ? 32 : __builtin_clz(x))
#define trailing_zeroes(x) ((x) == 0 ? 32 : __builtin_ctz(x))
#define trailing_ones(x) __builtin_ctz(~uint32_t(x))
#elif defined(_MSC_VER)
namespace Internal
{
static inline uint32_t clz(uint32_t x)
{
	unsigned long result;
	if (_BitScanReverse(&result, x))
		return 31 - result;
	else
		return 32;
}

static inline uint32_t ctz(uint32_t x)
{
	unsigned long result;
	if (_BitScanForward(&result, x))
		return result;
	else
		return 32;
}
}

#define leading_zeroes(x) ::Util::Internal::clz(x)
#define trailing_zeroes(x) ::Util::Internal::ctz(x)
#define trailing_ones(x) ::Util::Internal::ctz(~uint32_t(x))
#else
#error "Implement me."
#endif

// Iterates set bits, or runs of set bits:
//   for (uint32_t bits = mask, bit; next_bit(bits, bit);)
//   for (uint32_t bits = mask, base = 0, off, count; next_bit_range(bits, base, off, count);)
static inline bool next_bit(uint32_t &bits, uint32_t &bit)
{
	if (!bits)
		return false;
	bit = trailing_zeroes(bits);
	bits &= bits - 1;
	return true;
}

static inline bool next_bit_range(uint32_t &bits, uint32_t &base, uint32_t &offset, uint32_t &count)
{
	uint32_t bit;
	if (!bits)
		return false;
	if (bits == ~0u)
	{
		offset = 0;
		count = 32;
		bits = 0;
		return true;
	}
	bit = trailing_zeroes(bits);
	base += bit;
	bits >>= bit;
	count = trailing_ones(bits);
	offset = base;
	bits &= ~((1u << count) - 1);
	return true;
}

template <typename T>
inline bool is_pow2(T value)
{
	return (value & (value - T(1))) == T(0);
}

inline uint32_t next_pow2(uint32_t v)
{
	v--;
	v |= v >> 16;
	v |= v >> 8;
	v |= v >> 4;
	v |= v >> 2;
	v |= v >> 1;
	return v + 1;
}

inline uint32_t prev_pow2(uint32_t v)
{
	return next_pow2(v + 1) >> 1;
}

inline uint32_t floor_log2(uint32_t v)
{
	return 31 - leading_zeroes(v);
}
}
