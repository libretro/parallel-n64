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

#ifndef GRANITE_LEGION_ALLOCATOR_H
#define GRANITE_LEGION_ALLOCATOR_H

#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>
#include <retro_inline.h>

/* A buddy allocator over 32 sub-blocks. It is logical, and counts units,
 * not bytes. */
#define GRANITE_LEGION_SUB_BLOCKS 32u

RETRO_BEGIN_DECLS

typedef struct granite_legion
{
	/* free_blocks[n]: bit b set when sub-blocks b..b+n are all free */
	uint32_t free_blocks[GRANITE_LEGION_SUB_BLOCKS];
	uint32_t longest_run;
} granite_legion_t;

void granite_legion_init(granite_legion_t *legion);

/* num_blocks (1..32) contiguous sub-blocks; the caller knows from
 * granite_legion_longest_run that they are there */
void granite_legion_allocate(granite_legion_t *legion, uint32_t num_blocks,
		uint32_t *out_mask, uint32_t *out_offset);
void granite_legion_free(granite_legion_t *legion, uint32_t mask);

static INLINE bool granite_legion_full(const granite_legion_t *legion)
{
	return legion->free_blocks[0] == 0;
}

static INLINE bool granite_legion_empty(const granite_legion_t *legion)
{
	return legion->free_blocks[0] == ~0u;
}

static INLINE uint32_t granite_legion_longest_run(const granite_legion_t *legion)
{
	return legion->longest_run;
}

RETRO_END_DECLS

#endif
