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

#include <assert.h>
#if defined(_MSC_VER)
#include <intrin.h>
#endif
#include "legion_allocator.h"

/* x is never 0 here */
static INLINE uint32_t legion_ctz(uint32_t x)
{
#if defined(_MSC_VER)
	unsigned long r;
	_BitScanForward(&r, x);
	return (uint32_t)r;
#else
	return (uint32_t)__builtin_ctz(x);
#endif
}

static void legion_update_longest_run(granite_legion_t *legion)
{
	uint32_t f = legion->free_blocks[0];
	uint32_t run = 0;
	while (f)
	{
		legion->free_blocks[run++] = f;
		f &= f >> 1;
	}
	legion->longest_run = run;
}

void granite_legion_init(granite_legion_t *legion)
{
	unsigned i;
	for (i = 0; i < GRANITE_LEGION_SUB_BLOCKS; i++)
		legion->free_blocks[i] = ~0u;
	legion->longest_run = GRANITE_LEGION_SUB_BLOCKS;
}

void granite_legion_allocate(granite_legion_t *legion, uint32_t num_blocks,
		uint32_t *out_mask, uint32_t *out_offset)
{
	uint32_t block_mask;
	uint32_t b;
	uint32_t sb;

	assert(num_blocks != 0 && num_blocks <= GRANITE_LEGION_SUB_BLOCKS);
	block_mask = num_blocks == GRANITE_LEGION_SUB_BLOCKS ? ~0u : ((1u << num_blocks) - 1u);
	b = legion_ctz(legion->free_blocks[num_blocks - 1]);
	assert(((legion->free_blocks[0] >> b) & block_mask) == block_mask);

	sb = block_mask << b;
	legion->free_blocks[0] &= ~sb;
	legion_update_longest_run(legion);

	*out_mask = sb;
	*out_offset = b;
}

void granite_legion_free(granite_legion_t *legion, uint32_t mask)
{
	assert((legion->free_blocks[0] & mask) == 0);
	legion->free_blocks[0] |= mask;
	legion_update_longest_run(legion);
}
