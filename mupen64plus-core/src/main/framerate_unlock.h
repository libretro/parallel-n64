/* Game-specific framerate unlock (Hint).
 *
 * Registry of games whose framerate-unlock (e.g. 60fps) ROM patch is known and
 * byte-verified.  Gated by the 'parallel-n64-framerate-unlock-hint' core
 * option.  Each entry recognises one exact ROM and is applied only if every
 * patch word verifies; unrecognised ROMs and ROM hacks are left untouched.
 *
 * To add a game: add its verified word table header and one profile entry in
 * framerate_unlock.c.  (A sibling category, e.g. widescreen, is a separate
 * option and translation unit sharing rompatch_common.)
 *
 * Author: libretroadmin
 */
#ifndef FRAMERATE_UNLOCK_H
#define FRAMERATE_UNLOCK_H

#ifdef __cplusplus
extern "C" {
#endif

/* Apply a verified framerate-unlock patch to the z64-normalised ROM image in
 * place if it matches a known game; otherwise leave it untouched.
 * Returns 1 if a patch was applied, 0 if not. */
int framerate_unlock_apply(unsigned char *rom, int size);

#ifdef __cplusplus
}
#endif

#endif /* FRAMERATE_UNLOCK_H */
