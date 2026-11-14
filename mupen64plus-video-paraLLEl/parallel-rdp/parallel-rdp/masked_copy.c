/* Copyright (c) 2020 Themaister
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

#include <string.h>
#include "masked_copy.h"

/* (RDP_MASKED_COPY_SCALAR: the portable loop, for its test) */
#if !defined(RDP_MASKED_COPY_SCALAR) \
	&& (defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2))
#define RDP_MASKED_COPY_SSE2
#include <emmintrin.h>
#endif

#ifdef PARALLEL_RDP_VERIFY_UPLOADS
#include <stdio.h>
#include <stdlib.h>
#endif

void rdp_masked_copy(uint8_t *dst, const uint8_t *data, const uint8_t *mask, size_t size)
{
	size_t i;
#ifdef RDP_MASKED_COPY_SSE2
	for (i = 0; i < size; i += 16)
	{
		/* What the masked store would do, without it where it is all or
		 * nothing: the GPU wrote none of most chunks, or all of them. */
		const __m128i m = _mm_loadu_si128((const __m128i *)(mask + i));
		const int bits = _mm_movemask_epi8(m);
		__m128i d;
		if (!bits)
			continue;
		d = _mm_loadu_si128((const __m128i *)(data + i));
		if (bits == 0xffff)
			_mm_storeu_si128((__m128i *)(dst + i), d);
		else
			_mm_maskmoveu_si128(d, m, (char *)(dst + i));
	}
#else
	for (i = 0; i < size; i += 4)
	{
		/* (a byte is written where the top bit of its mask is set, as the
		 * masked store has it) */
		const unsigned bits = (mask[i] >> 7) | ((mask[i + 1] >> 7) << 1) | ((mask[i + 2] >> 7) << 2)
			| ((mask[i + 3] >> 7) << 3);
		unsigned j;
		if (bits == 0xf)
			memcpy(dst + i, data + i, 4);
		else if (bits)
		{
			/* Fairly rare path. */
			for (j = 0; j < 4; j++)
				if (bits & (1u << j))
					dst[i + j] = data[i + j];
		}
	}
#endif
}

void rdp_masked_copy_end(void)
{
#ifdef RDP_MASKED_COPY_SSE2
	/* (the masked store is non-temporal) */
	_mm_mfence();
#endif
}

#ifdef PARALLEL_RDP_VERIFY_UPLOADS
void rdp_masked_copy_verify(const uint8_t *dst, const uint8_t *data_src, const uint8_t *mask_src, size_t size)
{
	uint8_t *buf = (uint8_t *)malloc(4 * size);
	uint8_t *got, *want, *data, *mask;
	size_t i;

	if (!buf)
		abort();
	got  = buf;
	want = buf + size;
	data = buf + 2 * size;
	mask = buf + 3 * size;
	memcpy(got, dst, size);
	memcpy(want, got, size);
	memcpy(data, data_src, size);
	memcpy(mask, mask_src, size);
	rdp_masked_copy(got, data, mask, size);
	for (i = 0; i < size; i++)
		if (mask[i] & 0x80)
			want[i] = data[i];
	rdp_masked_copy_end();
	for (i = 0; i < size; i++)
		if (got[i] != want[i])
		{
			fprintf(stderr, "parallel-rdp: RDRAM byte 0x%x of a readback is not as the masked store leaves it\n",
					(unsigned)i);
			abort();
		}
	free(buf);
}
#endif
