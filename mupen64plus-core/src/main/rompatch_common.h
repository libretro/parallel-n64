/* Shared ROM-patch verification machinery for "game-specific hint" core
 * options (framerate unlock, widescreen, ...).
 *
 * Each hint category is its own core option and its own translation unit, but
 * they all share the same profile/registry/verify model defined here: a hint
 * holds a registry of per-game profiles; each profile recognises one exact ROM
 * (cartridge-ID gate + per-word signature) and carries verified patch words.
 * A patch is applied only if EVERY word verifies; otherwise the image is left
 * byte-for-byte untouched.
 *
 * Author: libretroadmin
 */
#ifndef ROMPATCH_COMMON_H
#define ROMPATCH_COMMON_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Slot may already differ from 'orig' in the wild (e.g. a recomputed header
 * checksum); written in pass 2 but never fails verification. */
#define ROMPATCH_WF_NONE     0u
#define ROMPATCH_WF_NOVERIFY 1u

struct rompatch_word
{
   uint32_t off;    /* byte offset into the z64-normalised ROM image */
   uint32_t orig;   /* expected original word (signature)            */
   uint32_t patch;  /* replacement word                             */
   uint32_t flags;  /* ROMPATCH_WF_*                                 */
};

/* One recognised, patchable ROM within a hint category. */
struct rompatch_profile
{
   const char                 *name;        /* human label for logging */
   char                        cart[3];     /* header 0x3B..0x3D, e.g. "NSM" */
   const struct rompatch_word *words;
   size_t                      word_count;
   /* Optional aspect ratio this patch produces, as a fraction (e.g. 16/9).
    * Both zero means "no aspect change" (e.g. a framerate patch).  Categories
    * that care (widescreen) read these after a successful apply. */
   int                         aspect_num;
   int                         aspect_den;
};

/* Try each profile in 'profiles' against the z64-normalised ROM image.
 * On the first profile that fully verifies, applies it in place and returns
 * a pointer to that profile (so the caller can read e.g. its aspect ratio).
 * Returns NULL if none matched (image left untouched).  'tag' is for logging. */
const struct rompatch_profile *rompatch_apply_registry(
      unsigned char *rom, int size,
      const struct rompatch_profile *profiles, size_t profile_count,
      const char *tag);

#ifdef __cplusplus
}
#endif

#endif /* ROMPATCH_COMMON_H */
