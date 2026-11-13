/* parallel-RDP's dump writer (parallel-rdp/rdp_dump_write.c): the file
 * it writes, read back.
 *
 * A dump starts with "RDPDUMP2" and the sizes of the RDRAM and hidden
 * RDRAM. A flush writes only the 4 KiB blocks that differ from what was
 * written last, each with its offset and length, then a flush marker;
 * RDRAM and hidden RDRAM are kept apart. Hidden RDRAM kept 16 bits an
 * entry is written as the low byte of each. A flush never reads past
 * the size given, nor writes past the size the dump was made with.
 * Commands, VI registers, completed frames and SyncFulls follow in the
 * order given, and closing the dump ends it with an EOF marker. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <boolean.h>

#include "rdp_dump_write.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_rdp_dump_write: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

#define PATH "test_rdp_dump_write.tmp"
#define DRAM_SIZE (3 * 4096)
#define HIDDEN_SIZE (4096 + 100)

static uint8_t *contents;
static size_t length, pos;

static void load(void)
{
	FILE *f = fopen(PATH, "rb");
	free(contents);
	contents = NULL;
	length = pos = 0;
	if (!f)
		return;
	fseek(f, 0, SEEK_END);
	length = (size_t)ftell(f);
	fseek(f, 0, SEEK_SET);
	contents = (uint8_t *)malloc(length + 1);
	if (fread(contents, 1, length, f) != length)
		length = 0;
	fclose(f);
}

static uint32_t next_u32(void)
{
	uint32_t value = 0xdeadbeefu;
	if (pos + 4 <= length)
		memcpy(&value, contents + pos, 4);
	pos += 4;
	return value;
}

/* the next thing is a block of @cmd at @offset, @size bytes of @value */
static bool next_block(uint32_t cmd, uint32_t offset, uint32_t size, uint8_t value)
{
	uint32_t i;
	if (next_u32() != cmd || next_u32() != offset || next_u32() != size || pos + size > length)
		return false;
	for (i = 0; i < size; i++)
		if (contents[pos + i] != value)
			return false;
	pos += size;
	return true;
}

int main(void)
{
	static uint8_t dram[DRAM_SIZE + 4096];
	static uint8_t hidden[HIDDEN_SIZE];
	static uint16_t wide[HIDDEN_SIZE + 4096];
	static const uint32_t command[3] = { 0x11111111u, 0x22222222u, 0x33333333u };
	rdp_dump_writer_t *writer;
	unsigned i;

	CHECK(!rdp_dump_writer_new("no-such-directory/dump.rdp", DRAM_SIZE, HIDDEN_SIZE), "a file that cannot be made: no dump");
	rdp_dump_writer_free(NULL);

	writer = rdp_dump_writer_new(PATH, DRAM_SIZE, HIDDEN_SIZE);
	CHECK(writer != NULL, "a dump is started");
	if (!writer)
		return 1;

	/* nothing changed from the zeroes a dump starts with */
	rdp_dump_writer_flush_dram(writer, dram, DRAM_SIZE);
	/* the middle block, and one byte of the last */
	memset(dram + 4096, 0x5a, 4096);
	rdp_dump_writer_flush_dram(writer, dram, DRAM_SIZE);
	rdp_dump_writer_flush_dram(writer, dram, DRAM_SIZE);
	/* beyond the dump's size nothing is written, whatever is there */
	memset(dram + DRAM_SIZE, 0x77, 4096);
	rdp_dump_writer_flush_dram(writer, dram, DRAM_SIZE + 4096);

	/* hidden RDRAM: its own cache, and a last block that is not whole */
	memset(hidden, 0x21, sizeof(hidden));
	rdp_dump_writer_flush_hidden_dram(writer, hidden, HIDDEN_SIZE);
	rdp_dump_writer_flush_hidden_dram(writer, hidden, HIDDEN_SIZE);
	for (i = 0; i < HIDDEN_SIZE; i++)
		wide[i] = (uint16_t)(0xab00u | (i < 4096 ? 0x21u : 0x43u));
	for (i = HIDDEN_SIZE; i < HIDDEN_SIZE + 4096; i++)
		wide[i] = 0x9999;
	rdp_dump_writer_flush_hidden_dram_wide(writer, wide, HIDDEN_SIZE + 4096);
	memset(hidden, 0x65, sizeof(hidden));
	rdp_dump_writer_flush_hidden_dram(writer, hidden, 100);

	/* (the RDRAM written last is untouched by all that) */
	rdp_dump_writer_flush_dram(writer, dram, DRAM_SIZE);

	rdp_dump_writer_emit_command(writer, 0x24, command, 3);
	rdp_dump_writer_signal_complete(writer);
	rdp_dump_writer_set_vi_register(writer, 7, 0x12345678u);
	rdp_dump_writer_end_frame(writer);
	rdp_dump_writer_free(writer);

	load();
	CHECK(length >= 16 && !memcmp(contents, "RDPDUMP2", 8), "the file starts with its magic");
	pos = 8;
	CHECK(next_u32() == DRAM_SIZE && next_u32() == HIDDEN_SIZE, "and the two sizes");

	CHECK(next_u32() == 7, "unchanged RDRAM: only the flush marker");
	CHECK(next_block(1, 4096, 4096, 0x5a), "a changed block is written with its offset and length");
	CHECK(next_u32() == 7, "and nothing else before the marker");
	CHECK(next_u32() == 7, "the same again: nothing");
	CHECK(next_u32() == 7, "more than the dump holds: nothing past its size");

	CHECK(next_block(8, 0, 4096, 0x21), "hidden RDRAM has blocks of its own");
	CHECK(next_block(8, 4096, 100, 0x21), "its last block as long as what is left");
	CHECK(next_u32() == 9, "and its own marker");
	CHECK(next_u32() == 9, "the same again: nothing");
	CHECK(next_block(8, 4096, 100, 0x43), "16-bit entries are written as their low bytes, and only where they differ");
	CHECK(next_u32() == 9, "then the marker");
	CHECK(next_block(8, 0, 100, 0x65), "a shorter flush reads no further than it was given");
	CHECK(next_u32() == 9, "then the marker");

	CHECK(next_u32() == 7, "RDRAM is kept apart from hidden RDRAM");

	CHECK(next_u32() == 2 && next_u32() == 0x24 && next_u32() == 3 && next_u32() == 0x11111111u
			&& next_u32() == 0x22222222u && next_u32() == 0x33333333u, "a command with its words");
	CHECK(next_u32() == 5, "a SyncFull");
	CHECK(next_u32() == 3 && next_u32() == 7 && next_u32() == 0x12345678u, "a VI register");
	CHECK(next_u32() == 4, "the end of a frame");
	CHECK(next_u32() == 6 && pos == length, "and the end of the dump, with nothing after it");

	free(contents);
	remove(PATH);

	if (failures)
	{
		fprintf(stderr, "test_rdp_dump_write: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_rdp_dump_write: ok\n");
	return 0;
}
