/* Standalone self-check for mupen64plus-video-gliden64/src/StereoJournal.c,
 * which lets the right eye pass of a stereoscopic task start from the RDRAM
 * the left eye pass started from.
 *
 * Build & run from the repo root:
 *   cc -std=c89 -pedantic -Wall -Wextra -g -fsanitize=address,undefined \
 *      -Imupen64plus-video-gliden64/src -o test_stereo_journal \
 *      tools/test_stereo_journal.c mupen64plus-video-gliden64/src/StereoJournal.c \
 *      && ./test_stereo_journal
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "StereoJournal.h"

#define RDRAM_SIZE 0x800000u
#define RDRAM_MASK (RDRAM_SIZE - 1u)

static uint8_t *rdram;
static uint8_t *reference;
static int failures;

#define CHECK(cond) do { if (!(cond)) { \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static void fill_pattern(void)
{
	uint32_t i;
	for (i = 0; i < RDRAM_SIZE; ++i)
		rdram[i] = (uint8_t)(i * 7u + (i >> 11));
	memcpy(reference, rdram, RDRAM_SIZE);
}

/* A task that steps state kept in RDRAM in place: each run reads it and
 * writes it back advanced by one, as an RSP task animating vertices does */
static void step_task(stereo_journal_t *journal, uint32_t address, uint32_t size)
{
	uint32_t i;
	if (journal)
		CHECK(stereo_journal_record(journal, rdram, RDRAM_MASK, address, size));
	for (i = 0; i < size; ++i)
		rdram[address + i] = (uint8_t)(rdram[address + i] + 1u);
}

static void test_one_step_per_task(void)
{
	stereo_journal_t journal;
	uint32_t i;
	memset(&journal, 0, sizeof(journal));
	fill_pattern();

	/* Left pass, undone, then right pass: the state advances once */
	step_task(&journal, 0x1000, 256);
	step_task(&journal, 0x2000, 4);
	stereo_journal_restore(&journal, rdram);
	CHECK(memcmp(rdram, reference, RDRAM_SIZE) == 0);
	step_task(NULL, 0x1000, 256);
	step_task(NULL, 0x2000, 4);

	for (i = 0; i < 256; ++i)
		CHECK(rdram[0x1000 + i] == (uint8_t)(reference[0x1000 + i] + 1u));
	for (i = 0; i < 4; ++i)
		CHECK(rdram[0x2000 + i] == (uint8_t)(reference[0x2000 + i] + 1u));
	CHECK(journal.count == 0 && journal.size == 0);
	stereo_journal_free(&journal);
}

static void test_overlapping_writes(void)
{
	stereo_journal_t journal;
	memset(&journal, 0, sizeof(journal));
	fill_pattern();

	/* Two writes over the same bytes: the first copy holds the task's input */
	CHECK(stereo_journal_record(&journal, rdram, RDRAM_MASK, 0x4000, 64));
	memset(rdram + 0x4000, 0x11, 64);
	CHECK(stereo_journal_record(&journal, rdram, RDRAM_MASK, 0x4020, 64));
	memset(rdram + 0x4020, 0x22, 64);
	CHECK(stereo_journal_record(&journal, rdram, RDRAM_MASK, 0x4000, 128));
	memset(rdram + 0x4000, 0x33, 128);

	stereo_journal_restore(&journal, rdram);
	CHECK(memcmp(rdram, reference, RDRAM_SIZE) == 0);
	stereo_journal_free(&journal);
}

static void test_end_of_rdram(void)
{
	stereo_journal_t journal;
	memset(&journal, 0, sizeof(journal));
	fill_pattern();

	/* A write running past the end of RDRAM keeps only what is in it */
	CHECK(stereo_journal_record(&journal, rdram, RDRAM_MASK, RDRAM_SIZE - 16, 4096));
	CHECK(journal.count == 1 && journal.entries[0].size == 16);
	memset(rdram + RDRAM_SIZE - 16, 0, 16);

	/* Past the end, and empty: nothing to keep */
	CHECK(stereo_journal_record(&journal, rdram, RDRAM_MASK, RDRAM_SIZE, 16));
	CHECK(stereo_journal_record(&journal, rdram, RDRAM_MASK, 0x100, 0));
	CHECK(journal.count == 1);

	stereo_journal_restore(&journal, rdram);
	CHECK(memcmp(rdram, reference, RDRAM_SIZE) == 0);
	stereo_journal_free(&journal);
}

static void test_growth(void)
{
	stereo_journal_t journal;
	uint32_t i;
	memset(&journal, 0, sizeof(journal));
	fill_pattern();

	/* More writes than the first allocation holds, and a full colour image */
	for (i = 0; i < 1000; ++i)
	{
		const uint32_t address = (i * 0x1234u) & RDRAM_MASK & ~3u;
		CHECK(stereo_journal_record(&journal, rdram, RDRAM_MASK, address, 4));
		rdram[address] ^= 0xFF;
	}
	CHECK(stereo_journal_record(&journal, rdram, RDRAM_MASK, 0x100000, 640 * 480 * 4));
	memset(rdram + 0x100000, 0, 640 * 480 * 4);
	CHECK(journal.count == 1001);

	stereo_journal_restore(&journal, rdram);
	CHECK(memcmp(rdram, reference, RDRAM_SIZE) == 0);
	stereo_journal_free(&journal);
	CHECK(journal.entries == NULL && journal.data == NULL && journal.capacity == 0);
}

static void test_clear(void)
{
	stereo_journal_t journal;
	memset(&journal, 0, sizeof(journal));
	fill_pattern();

	/* A task that was not drawn twice keeps its writes */
	CHECK(stereo_journal_record(&journal, rdram, RDRAM_MASK, 0x8000, 32));
	memset(rdram + 0x8000, 0x5A, 32);
	stereo_journal_clear(&journal);
	stereo_journal_restore(&journal, rdram);
	CHECK(rdram[0x8000] == 0x5A && rdram[0x801F] == 0x5A);
	stereo_journal_free(&journal);
}

int main(void)
{
	rdram     = (uint8_t*)malloc(RDRAM_SIZE);
	reference = (uint8_t*)malloc(RDRAM_SIZE);
	if (!rdram || !reference)
		return 2;

	test_one_step_per_task();
	test_overlapping_writes();
	test_end_of_rdram();
	test_growth();
	test_clear();

	free(rdram);
	free(reference);
	if (failures)
	{
		fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}
	printf("stereo journal: all checks passed\n");
	return 0;
}
