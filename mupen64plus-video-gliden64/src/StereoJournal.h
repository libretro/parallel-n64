#ifndef STEREO_JOURNAL_H
#define STEREO_JOURNAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct stereo_journal_entry
{
	uint32_t address;
	uint32_t size;
	uint32_t offset;
};

/* What the left eye pass of a task changed in place in RDRAM, kept so the
 * right eye pass starts from the RDRAM the left one started from */
typedef struct stereo_journal
{
	struct stereo_journal_entry *entries;
	uint8_t *data;
	uint32_t count;
	uint32_t capacity;
	uint32_t size;
	uint32_t data_capacity;
} stereo_journal_t;

/* Keeps RDRAM[address .. address + size), clamped to rdram_mask + 1 bytes.
 * Returns 0 when memory runs out: that write then cannot be undone. */
int stereo_journal_record(stereo_journal_t *journal, const uint8_t *rdram,
		uint32_t rdram_mask, uint32_t address, uint32_t size);

/* Puts back what was kept, newest first, and empties the journal */
void stereo_journal_restore(stereo_journal_t *journal, uint8_t *rdram);

void stereo_journal_clear(stereo_journal_t *journal);
void stereo_journal_free(stereo_journal_t *journal);

#ifdef __cplusplus
}
#endif

#endif
