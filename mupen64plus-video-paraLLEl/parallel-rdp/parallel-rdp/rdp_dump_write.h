/* Copyright (c) 2021 Themaister
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

#ifndef RDP_DUMP_WRITE_H
#define RDP_DUMP_WRITE_H

#include <stddef.h>
#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>

RETRO_BEGIN_DECLS

/* Writes what the RDP is given to a file another parallel-RDP can play
 * back: the RDRAM and hidden RDRAM as they change, in 4 KiB blocks, the
 * RDP commands, the VI registers and the frame ends. For finding
 * rendering bugs; off unless asked for. */
typedef struct rdp_dump_writer rdp_dump_writer_t;

/* NULL when the file cannot be made or there is no memory */
rdp_dump_writer_t *rdp_dump_writer_new(const char *path, uint32_t dram_size, uint32_t hidden_dram_size);
/* Ends the dump and closes the file. NULL is fine. */
void rdp_dump_writer_free(rdp_dump_writer_t *writer);

/* The blocks of the first @size bytes of RDRAM that changed since the
 * last time; @size is no more than the size the dump was made with */
void rdp_dump_writer_flush_dram(rdp_dump_writer_t *writer, const void *dram, uint32_t size);
/* The same for hidden RDRAM, a byte an entry */
void rdp_dump_writer_flush_hidden_dram(rdp_dump_writer_t *writer, const void *dram, uint32_t size);
/* The same for hidden RDRAM kept 16 bits an entry: @count entries, the
 * low byte of each */
void rdp_dump_writer_flush_hidden_dram_wide(rdp_dump_writer_t *writer, const uint16_t *dram, uint32_t count);
/* A SyncFull */
void rdp_dump_writer_signal_complete(rdp_dump_writer_t *writer);
void rdp_dump_writer_emit_command(rdp_dump_writer_t *writer, uint32_t command, const uint32_t *cmd_data,
		uint32_t cmd_words);
void rdp_dump_writer_set_vi_register(rdp_dump_writer_t *writer, uint32_t vi_register, uint32_t value);
void rdp_dump_writer_end_frame(rdp_dump_writer_t *writer);

RETRO_END_DECLS

#endif
