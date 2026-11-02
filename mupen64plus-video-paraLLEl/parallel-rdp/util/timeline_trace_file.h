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

#ifndef GRANITE_TIMELINE_TRACE_FILE_H
#define GRANITE_TIMELINE_TRACE_FILE_H

#include <stdint.h>
#include <retro_common_api.h>

RETRO_BEGIN_DECLS

/* A Chrome trace (JSON) of timed intervals. Any thread records; one I/O
 * thread writes, without a lock. */
typedef struct granite_trace_file granite_trace_file_t;

/* NULL when the I/O thread cannot start; a file that cannot be opened
 * still takes events, and drops them */
granite_trace_file_t *granite_trace_file_new(const char *path);

/* Writes out what is queued, joins the I/O thread, closes the file */
void granite_trace_file_free(granite_trace_file_t *file);

void granite_trace_file_record(granite_trace_file_t *file,
		const char *desc, const char *tid, uint32_t pid,
		uint64_t start_ns, uint64_t end_ns);

RETRO_END_DECLS

#endif
