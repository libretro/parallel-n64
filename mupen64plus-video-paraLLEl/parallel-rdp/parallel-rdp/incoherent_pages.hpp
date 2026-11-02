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

#include <stdint.h>
#include "bitops.hpp"

namespace RDP
{
// The pages of RDRAM a GPU read covers, for a host whose RDRAM the GPU
// cannot see: count pages from start_page, wrapping at num_pages (a power
// of two), at most the whole ring. A page the GPU still has a readback
// pending for, or the CPU writes pending to, takes a masked copy; any
// other a direct one. A word of pages at a time: only the pages without a
// pending readback need their write counter looked at.
template <typename PendingWrites>
static inline void mark_incoherent_pages_for_read(uint32_t start_page, uint32_t count, uint32_t num_pages,
                                                  const uint32_t *pending_readback,
                                                  const PendingWrites &pending_writes,
                                                  uint32_t *masked_copy, uint32_t *direct_copy)
{
	if (count > num_pages)
		count = num_pages;
	uint32_t page = start_page & (num_pages - 1);

	while (count)
	{
		uint32_t word = page / 32;
		uint32_t bit = page & 31;
		uint32_t n = 32 - bit;
		if (n > count)
			n = count;
		if (n > num_pages - page)
			n = num_pages - page;
		uint32_t mask = (n == 32 ? ~0u : ((1u << n) - 1u)) << bit;

		uint32_t masked = pending_readback[word] & mask;
		for (uint32_t bits = mask & ~masked, b; Util::next_bit(bits, b);)
			if (pending_writes(32 * word + b))
				masked |= 1u << b;

		masked_copy[word] |= masked;
		direct_copy[word] |= mask & ~masked;

		count -= n;
		page = (page + n) & (num_pages - 1);
	}
}
}
