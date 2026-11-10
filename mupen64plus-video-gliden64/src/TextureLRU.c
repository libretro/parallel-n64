#include <stdlib.h>
#include <string.h>

#include "gli_texlru.h"

/* The keys are hashes already */
static u32 bucket(const gli_lru *lru, u64 key)
{
	return (u32)(key ^ (key >> 32)) & lru->mask;
}

int gli_lru_init(gli_lru *lru, u32 cap)
{
	u32 size = 1;
	memset(lru, 0, sizeof(*lru));
	while (size < cap * 2)
		size <<= 1;
	lru->cap = cap;
	lru->mask = size - 1;
	lru->prev = (u32*)malloc(cap * sizeof(u32));
	lru->next = (u32*)malloc(cap * sizeof(u32));
	lru->keys = (u64*)malloc(cap * sizeof(u64));
	lru->table = (u32*)malloc(size * sizeof(u32));
	if (lru->prev == NULL || lru->next == NULL || lru->keys == NULL || lru->table == NULL) {
		gli_lru_deinit(lru);
		return 0;
	}
	gli_lru_reset(lru);
	return 1;
}

void gli_lru_deinit(gli_lru *lru)
{
	free(lru->prev);
	free(lru->next);
	free(lru->keys);
	free(lru->table);
	memset(lru, 0, sizeof(*lru));
}

void gli_lru_reset(gli_lru *lru)
{
	u32 i;
	if (lru->table == NULL)
		return;
	memset(lru->table, 0, (lru->mask + 1) * sizeof(u32));
	for (i = 0; i < lru->cap; ++i)
		lru->next[i] = i + 1 < lru->cap ? i + 1 : GLI_LRU_NONE;
	lru->freehead = lru->cap ? 0 : GLI_LRU_NONE;
	lru->head = lru->tail = GLI_LRU_NONE;
	lru->count = 0;
}

static u32 find_bucket(const gli_lru *lru, u64 key)
{
	u32 b = bucket(lru, key);
	while (lru->table[b] != 0) {
		if (lru->keys[lru->table[b] - 1] == key)
			return b;
		b = (b + 1) & lru->mask;
	}
	return GLI_LRU_NONE;
}

u32 gli_lru_find(const gli_lru *lru, u64 key)
{
	const u32 b = find_bucket(lru, key);
	return b == GLI_LRU_NONE ? GLI_LRU_NONE : lru->table[b] - 1;
}

/* Linear probing with backward-shift deletion: no tombstones */
static void drop_key(gli_lru *lru, u64 key, u32 slot)
{
	u32 b = find_bucket(lru, key);
	u32 n;
	if (b == GLI_LRU_NONE || lru->table[b] - 1 != slot)
		return;
	n = (b + 1) & lru->mask;
	while (lru->table[n] != 0) {
		const u32 home = bucket(lru, lru->keys[lru->table[n] - 1]);
		/* moves back unless its home lies in (b, n] */
		if (((n - home) & lru->mask) >= ((n - b) & lru->mask)) {
			lru->table[b] = lru->table[n];
			b = n;
		}
		n = (n + 1) & lru->mask;
	}
	lru->table[b] = 0;
}

static void unlink_slot(gli_lru *lru, u32 slot)
{
	const u32 p = lru->prev[slot];
	const u32 n = lru->next[slot];
	if (p != GLI_LRU_NONE)
		lru->next[p] = n;
	else
		lru->head = n;
	if (n != GLI_LRU_NONE)
		lru->prev[n] = p;
	else
		lru->tail = p;
}

static void link_front(gli_lru *lru, u32 slot)
{
	lru->prev[slot] = GLI_LRU_NONE;
	lru->next[slot] = lru->head;
	if (lru->head != GLI_LRU_NONE)
		lru->prev[lru->head] = slot;
	else
		lru->tail = slot;
	lru->head = slot;
}

void gli_lru_touch(gli_lru *lru, u32 slot)
{
	if (lru->head == slot)
		return;
	unlink_slot(lru, slot);
	link_front(lru, slot);
}

void gli_lru_remove(gli_lru *lru, u32 slot)
{
	unlink_slot(lru, slot);
	drop_key(lru, lru->keys[slot], slot);
	lru->next[slot] = lru->freehead;
	lru->freehead = slot;
	lru->count--;
}

u32 gli_lru_add(gli_lru *lru, u64 key)
{
	u32 slot, b;
	if (lru->cap == 0)
		return GLI_LRU_NONE;
	if (lru->count >= lru->cap) {
		slot = lru->tail;
		unlink_slot(lru, slot);
		drop_key(lru, lru->keys[slot], slot);
	} else {
		slot = lru->freehead;
		lru->freehead = lru->next[slot];
		lru->count++;
	}
	lru->keys[slot] = key;
	link_front(lru, slot);
	if (find_bucket(lru, key) == GLI_LRU_NONE) {
		b = bucket(lru, key);
		while (lru->table[b] != 0)
			b = (b + 1) & lru->mask;
		lru->table[b] = slot + 1;
	}
	return slot;
}
