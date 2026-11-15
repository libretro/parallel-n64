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


#ifndef PARALLEL_INTERNAL_H
#define PARALLEL_INTERNAL_H

/* What parallel.c and parallel_hle.c share. C89. */

#include <stdint.h>

/* Runs the RDP's command list, where the DPC registers point */
void parallel_process_commands(void);

/* The list comes from the HLE emitter's host buffer, at
 * [base_byte_addr, base_byte_addr + len_bytes) of the RDP's addresses,
 * for the next parallel_process_commands(); NULL: from RDRAM again */
void parallel_set_hle_cmd_buffer(const uint8_t *buf, uint32_t base_byte_addr, uint32_t len_bytes);

/* The display list RSP-HLE handed over, through the HLE command emitter */
void parallel_hle_process_dlist(void);

#endif
