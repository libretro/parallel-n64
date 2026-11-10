#ifndef GLI_TEXLRU_H
#define GLI_TEXLRU_H

#include "Types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GLI_LRU_NONE 0xFFFFFFFFU

/* A fixed-capacity least-recently-used index of 64-bit keys to slots
 * 0..cap-1. Everything is allocated by gli_lru_init, nothing after. */
typedef struct gli_lru
{
	u32 cap, mask, count;
	u32 head, tail, freehead;
	u32 *prev, *next;
	u64 *keys;
	u32 *table; /* slot + 1, 0 when empty */
} gli_lru;

int gli_lru_init(gli_lru *lru, u32 cap);
void gli_lru_deinit(gli_lru *lru);
void gli_lru_reset(gli_lru *lru);

/* The slot of @key, or GLI_LRU_NONE */
u32 gli_lru_find(const gli_lru *lru, u64 key);

/* Makes @slot the most recently used */
void gli_lru_touch(gli_lru *lru, u32 slot);

/* Frees @slot and its key */
void gli_lru_remove(gli_lru *lru, u32 slot);

/* A slot for @key, most recently used; when full, the least recently
 * used one with its key dropped. A key already present keeps its slot. */
u32 gli_lru_add(gli_lru *lru, u64 key);

#ifdef __cplusplus
}
#endif

#endif
