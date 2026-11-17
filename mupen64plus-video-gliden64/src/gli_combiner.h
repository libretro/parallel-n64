#ifndef GLI_COMBINER_H
#define GLI_COMBINER_H

/* GLideN64's combiners: the programs built for each combine key, the one
 * drawn with, and the shaders beside them (Combiner.c). C89.
 *
 * A program is one of the backend's C++ CombinerProgram classes; C sees it
 * as its gli_cprog base, the table of what its class does and its key. */

#include <boolean.h>
#include "Types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gli_cprog_ops
{
	void (*activate)(void *_program);
	void (*update)(void *_program, bool _force);
	bool (*usesLOD)(const void *_program);
	void (*free_program)(void *_program);
} gli_cprog_ops;

/* the C face of a CombinerProgram: what its class does, and its key (0 for
 * the shaders that are not a combine's) */
typedef struct gli_cprog
{
	const gli_cprog_ops *ops;
	u64 key;
} gli_cprog;

/* the programs by key: open addressing, sized at init for the most a game
 * builds, so a new combiner mid-game allocates nothing */
typedef struct gli_combiner_entry
{
	u64 key;
	gli_cprog *program;
} gli_combiner_entry;

typedef struct gli_combiner_table
{
	gli_combiner_entry *slots;
	u32 mask;  /* slots - 1, a power of two less one */
	u32 count;
} gli_combiner_table;

/* As many programs as the C++'s fixed pool of map nodes held, at most half
 * the slots full; past that (no game comes near) the table doubles. */
#define GLI_COMBINER_SLOTS 16384

/* @_slots empty slots, a power of two */
bool gli_combiners_make(gli_combiner_table *_table, u32 _slots);
/* every program freed; the slots kept */
void gli_combiners_clear(gli_combiner_table *_table);
/* the program of @_key, NULL for none */
gli_cprog *gli_combiners_find(const gli_combiner_table *_table, u64 _key);
/* @_program kept by its key, in place of any of the same key */
void gli_combiners_put(gli_combiner_table *_table, gli_cprog *_program);
/* the programs into @_out (count of them), by key, lowest first */
u32 gli_combiners_sorted(const gli_combiner_table *_table, gli_cprog **_out);

typedef struct gli_combiner_info
{
	gli_cprog *current;          /* drawn with; NULL before the first */
	gli_combiner_table table;
	struct combiner_journal *journal;
	gli_cprog *shadowmap;        /* the depth fog shader */
	gli_cprog *texrectCopy;      /* the texrect copy shader */
	u32 shadersLoaded;           /* programs read from the shader storage */
	bool changed;                /* the program drawn with changed */
	bool rectMode;
} gli_combiner_info;

extern gli_combiner_info gli_cmb;

void gli_combiner_init(void);
void gli_combiner_destroy(void);
/* the program of the current combine made the one drawn with */
void gli_combiner_update(void);
void gli_combiner_set_combine(u64 _mux);
void gli_combiner_update_parameters(void);
void gli_combiner_set_depth_fog(void);
void gli_combiner_set_rect_mode(bool _rect);
bool gli_combiner_shader_cache_supported(void);
/* a program built for @_key (with its mode bits), by the ubershader while
 * the driver compiles if @_mayDefer */
gli_cprog *gli_combiner_compile(u64 _key, bool _mayDefer);

#ifdef __cplusplus
}
#endif

#endif
