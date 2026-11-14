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

#ifndef RDP_MASKED_COPY_H
#define RDP_MASKED_COPY_H

#include <stddef.h>
#include <stdint.h>
#include <retro_common_api.h>

RETRO_BEGIN_DECLS

/* What the GPU wrote, read back into RDRAM: @data's bytes go to @dst
 * where @mask's byte is set (0xff: the GPU writes whole bytes of mask),
 * the others are left as they are, not written at all: the emulation may
 * be writing them. @size is a multiple of 16 and none of the three
 * overlap. */
void rdp_masked_copy(uint8_t *dst, const uint8_t *data, const uint8_t *mask, size_t size);

/* After the copies of one readback: orders them before what follows on
 * other cores, where the stores they use need it */
void rdp_masked_copy_end(void);

#ifdef PARALLEL_RDP_VERIFY_UPLOADS
/* rdp_masked_copy against a masked store of every chunk, on copies of
 * what it reads and writes: the same bytes, or the build stops here */
void rdp_masked_copy_verify(const uint8_t *dst, const uint8_t *data, const uint8_t *mask, size_t size);
#endif

RETRO_END_DECLS

#endif
