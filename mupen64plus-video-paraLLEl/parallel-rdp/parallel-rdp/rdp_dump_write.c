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

#include <stdlib.h>
#include <string.h>
#include <streams/file_stream.h>
#include "rdp_dump_write.h"

enum
{
	RDP_DUMP_CMD_INVALID = 0,
	RDP_DUMP_CMD_UPDATE_DRAM = 1,
	RDP_DUMP_CMD_RDP_COMMAND = 2,
	RDP_DUMP_CMD_SET_VI_REGISTER = 3,
	RDP_DUMP_CMD_END_FRAME = 4,
	RDP_DUMP_CMD_SIGNAL_COMPLETE = 5,
	RDP_DUMP_CMD_EOF = 6,
	RDP_DUMP_CMD_UPDATE_DRAM_FLUSH = 7,
	RDP_DUMP_CMD_UPDATE_HIDDEN_DRAM = 8,
	RDP_DUMP_CMD_UPDATE_HIDDEN_DRAM_FLUSH = 9
};

#define RDP_DUMP_BLOCK_SIZE (4 * 1024)

struct rdp_dump_writer
{
	RFILE *file;
	uint32_t dram_size;
	uint32_t hidden_dram_size;
	/* One block: the RDRAM as last written, then the hidden RDRAM. */
	uint8_t *dram_cache;
	uint8_t *hidden_dram_cache;
};

static void write_u32(rdp_dump_writer_t *writer, uint32_t value)
{
	filestream_write(writer->file, &value, sizeof(value));
}

rdp_dump_writer_t *rdp_dump_writer_new(const char *path, uint32_t dram_size, uint32_t hidden_dram_size)
{
	rdp_dump_writer_t *writer = (rdp_dump_writer_t *)calloc(1, sizeof(*writer));

	if (!writer)
		return NULL;

	writer->dram_cache = (uint8_t *)calloc(1, (size_t)dram_size + hidden_dram_size);
	if (writer->dram_cache)
		writer->file = filestream_open(path, RETRO_VFS_FILE_ACCESS_WRITE, RETRO_VFS_FILE_ACCESS_HINT_NONE);
	if (!writer->file)
	{
		free(writer->dram_cache);
		free(writer);
		return NULL;
	}
	writer->hidden_dram_cache = writer->dram_cache + dram_size;
	writer->dram_size = dram_size;
	writer->hidden_dram_size = hidden_dram_size;

	filestream_write(writer->file, "RDPDUMP2", 8);
	write_u32(writer, dram_size);
	write_u32(writer, hidden_dram_size);
	return writer;
}

void rdp_dump_writer_free(rdp_dump_writer_t *writer)
{
	if (!writer)
		return;
	write_u32(writer, RDP_DUMP_CMD_EOF);
	filestream_close(writer->file);
	free(writer->dram_cache);
	free(writer);
}

/* One block, if it is not what the cache has */
static void flush_block(rdp_dump_writer_t *writer, const uint8_t *block, uint32_t offset, uint32_t size,
		uint32_t block_cmd, uint8_t *cache)
{
	if (!memcmp(block, cache + offset, size))
		return;
	write_u32(writer, block_cmd);
	write_u32(writer, offset);
	write_u32(writer, size);
	filestream_write(writer->file, block, size);
	memcpy(cache + offset, block, size);
}

static void flush(rdp_dump_writer_t *writer, const uint8_t *dram, uint32_t size, uint32_t limit,
		uint32_t block_cmd, uint32_t flush_cmd, uint8_t *cache)
{
	uint32_t i;

	if (size > limit)
		size = limit;
	for (i = 0; i < size; i += RDP_DUMP_BLOCK_SIZE)
	{
		uint32_t n = size - i < RDP_DUMP_BLOCK_SIZE ? size - i : RDP_DUMP_BLOCK_SIZE;
		flush_block(writer, dram + i, i, n, block_cmd, cache);
	}
	write_u32(writer, flush_cmd);
}

void rdp_dump_writer_flush_dram(rdp_dump_writer_t *writer, const void *dram, uint32_t size)
{
	flush(writer, (const uint8_t *)dram, size, writer->dram_size, RDP_DUMP_CMD_UPDATE_DRAM,
			RDP_DUMP_CMD_UPDATE_DRAM_FLUSH, writer->dram_cache);
}

void rdp_dump_writer_flush_hidden_dram(rdp_dump_writer_t *writer, const void *dram, uint32_t size)
{
	flush(writer, (const uint8_t *)dram, size, writer->hidden_dram_size, RDP_DUMP_CMD_UPDATE_HIDDEN_DRAM,
			RDP_DUMP_CMD_UPDATE_HIDDEN_DRAM_FLUSH, writer->hidden_dram_cache);
}

void rdp_dump_writer_flush_hidden_dram_wide(rdp_dump_writer_t *writer, const uint16_t *dram, uint32_t count)
{
	uint8_t narrow[RDP_DUMP_BLOCK_SIZE];
	uint32_t i, j;

	if (count > writer->hidden_dram_size)
		count = writer->hidden_dram_size;
	for (i = 0; i < count; i += RDP_DUMP_BLOCK_SIZE)
	{
		uint32_t n = count - i < RDP_DUMP_BLOCK_SIZE ? count - i : RDP_DUMP_BLOCK_SIZE;
		for (j = 0; j < n; j++)
			narrow[j] = (uint8_t)dram[i + j];
		flush_block(writer, narrow, i, n, RDP_DUMP_CMD_UPDATE_HIDDEN_DRAM, writer->hidden_dram_cache);
	}
	write_u32(writer, RDP_DUMP_CMD_UPDATE_HIDDEN_DRAM_FLUSH);
}

void rdp_dump_writer_signal_complete(rdp_dump_writer_t *writer)
{
	write_u32(writer, RDP_DUMP_CMD_SIGNAL_COMPLETE);
}

void rdp_dump_writer_emit_command(rdp_dump_writer_t *writer, uint32_t command, const uint32_t *cmd_data,
		uint32_t cmd_words)
{
	write_u32(writer, RDP_DUMP_CMD_RDP_COMMAND);
	write_u32(writer, command);
	write_u32(writer, cmd_words);
	filestream_write(writer->file, cmd_data, (int64_t)cmd_words * sizeof(*cmd_data));
}

void rdp_dump_writer_set_vi_register(rdp_dump_writer_t *writer, uint32_t vi_register, uint32_t value)
{
	write_u32(writer, RDP_DUMP_CMD_SET_VI_REGISTER);
	write_u32(writer, vi_register);
	write_u32(writer, value);
}

void rdp_dump_writer_end_frame(rdp_dump_writer_t *writer)
{
	write_u32(writer, RDP_DUMP_CMD_END_FRAME);
}
