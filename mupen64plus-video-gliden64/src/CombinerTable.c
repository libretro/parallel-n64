/* GLideN64's combiner programs by key (gli_combiner.h). C89. */
#include <stdlib.h>
#include "gli_combiner.h"

static u32 slot_of(u64 _key, u32 _mask)
{
	/* Fibonacci hashing: the key's bits mixed into the top, which index */
	const u64 h = _key * 0x9E3779B97F4A7C15ULL;
	return (u32)(h >> 32) & _mask;
}

gli_cprog *gli_combiners_find(const gli_combiner_table *_table, u64 _key)
{
	u32 i;
	if (_table->slots == NULL)
		return NULL;
	for (i = slot_of(_key, _table->mask); _table->slots[i].program != NULL; i = (i + 1) & _table->mask)
		if (_table->slots[i].key == _key)
			return _table->slots[i].program;
	return NULL;
}

bool gli_combiners_make(gli_combiner_table *_table, u32 _slots)
{
	_table->slots = (gli_combiner_entry *)calloc(_slots, sizeof(gli_combiner_entry));
	/* at most half the slots are filled */
	_table->order = (u32 *)malloc((_slots / 2 + 1) * sizeof(u32));
	_table->mask = _slots - 1;
	_table->count = 0;
	if (_table->slots == NULL || _table->order == NULL) {
		free(_table->slots);
		free(_table->order);
		_table->slots = NULL;
		_table->order = NULL;
		return false;
	}
	return true;
}

static void table_insert(gli_combiner_table *_table, gli_cprog *_program)
{
	u32 i;
	for (i = slot_of(_program->key, _table->mask); _table->slots[i].program != NULL; i = (i + 1) & _table->mask)
		if (_table->slots[i].key == _program->key) {
			_table->slots[i].program = _program;
			return;
		}
	_table->slots[i].key = _program->key;
	_table->slots[i].program = _program;
	_table->order[_table->count++] = i;
}

void gli_combiners_put(gli_combiner_table *_table, gli_cprog *_program)
{
	if (_table->slots == NULL && !gli_combiners_make(_table, GLI_COMBINER_SLOTS))
		return;
	if ((_table->count + 1) * 2 > _table->mask + 1) {
		gli_combiner_table bigger;
		u32 i;
		if (!gli_combiners_make(&bigger, (_table->mask + 1) * 2))
			return;
		for (i = 0; i < _table->count; i++)
			table_insert(&bigger, _table->slots[_table->order[i]].program);
		free(_table->slots);
		free(_table->order);
		*_table = bigger;
	}
	table_insert(_table, _program);
}

static int by_key(const void *_a, const void *_b)
{
	const u64 a = (*(gli_cprog * const *)_a)->key, b = (*(gli_cprog * const *)_b)->key;
	return a < b ? -1 : a > b ? 1 : 0;
}

u32 gli_combiners_sorted(const gli_combiner_table *_table, gli_cprog **_out)
{
	u32 i, n = 0;
	for (i = 0; i < _table->count; i++)
		_out[n++] = _table->slots[_table->order[i]].program;
	qsort(_out, n, sizeof(*_out), by_key);
	return n;
}

/* every program freed; the slots kept for the next content */
void gli_combiners_clear(gli_combiner_table *_table)
{
	u32 i;
	if (_table->slots == NULL)
		return;
	for (i = 0; i < _table->count; i++) {
		gli_combiner_entry *e = &_table->slots[_table->order[i]];
		e->program->ops->free_program(e->program);
		e->program = NULL;
	}
	_table->count = 0;
}

