#include <stdlib.h>
#include <string.h>

#include "StereoJournal.h"

int stereo_journal_record(stereo_journal_t *journal, const uint8_t *rdram,
		uint32_t rdram_mask, uint32_t address, uint32_t size)
{
	struct stereo_journal_entry *entry;

	if (address > rdram_mask || size == 0)
		return 1;
	if (size > rdram_mask - address + 1)
		size = rdram_mask - address + 1;

	if (journal->count == journal->capacity)
	{
		uint32_t capacity = journal->capacity ? journal->capacity * 2 : 64;
		struct stereo_journal_entry *entries = (struct stereo_journal_entry*)
			realloc(journal->entries, capacity * sizeof(*entries));
		if (!entries)
			return 0;
		journal->entries  = entries;
		journal->capacity = capacity;
	}

	if (size > journal->data_capacity - journal->size)
	{
		uint32_t capacity = journal->data_capacity ? journal->data_capacity : 0x10000;
		uint8_t *data;
		while (capacity - journal->size < size)
		{
			if (capacity >= 0x80000000u)
				return 0;
			capacity *= 2;
		}
		data = (uint8_t*)realloc(journal->data, capacity);
		if (!data)
			return 0;
		journal->data          = data;
		journal->data_capacity = capacity;
	}

	entry          = &journal->entries[journal->count++];
	entry->address = address;
	entry->size    = size;
	entry->offset  = journal->size;
	memcpy(journal->data + journal->size, rdram + address, size);
	journal->size += size;
	return 1;
}

void stereo_journal_restore(stereo_journal_t *journal, uint8_t *rdram)
{
	/* Newest first: of a byte written twice, its oldest copy is the one kept */
	while (journal->count)
	{
		const struct stereo_journal_entry *entry = &journal->entries[--journal->count];
		memcpy(rdram + entry->address, journal->data + entry->offset, entry->size);
	}
	journal->size = 0;
}

void stereo_journal_clear(stereo_journal_t *journal)
{
	journal->count = 0;
	journal->size  = 0;
}

void stereo_journal_free(stereo_journal_t *journal)
{
	free(journal->entries);
	free(journal->data);
	journal->entries       = NULL;
	journal->data          = NULL;
	journal->count         = 0;
	journal->capacity      = 0;
	journal->size          = 0;
	journal->data_capacity = 0;
}
