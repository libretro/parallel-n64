#ifndef COMBINERKEY_H
#define COMBINERKEY_H

#include <boolean.h>
#include "Types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The combiner key: the mux with the mode flags in muxs0's high byte */
u64 gli_combiner_key(u64 _mux);
u32 gli_combiner_key_cycle_type(u64 _key);
u32 gli_combiner_key_bilerp(u64 _key);
bool gli_combiner_key_is_rect(u64 _key);
bool gli_combiner_key_hwl(u64 _key);

/* Combiner.cpp */
bool gli_combiner_is_rect_mode(void);
/* whether the current combiner reads the LOD */
bool gli_combiner_uses_lod(void);

#ifdef __cplusplus
}
#endif

#endif /* COMBINERKEY_H */
