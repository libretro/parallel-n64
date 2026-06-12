/* Game-specific widescreen (Hint).
 *
 * Registry of games whose widescreen ROM patch is known and byte-verified.
 * Gated by the 'parallel-n64-widescreen-hint' core option.  Each entry
 * recognises one exact ROM and is applied only if every patch word verifies;
 * unrecognised ROMs and ROM hacks are left untouched.
 *
 * When a patch is applied, the profile's aspect ratio is exposed via
 * widescreen_active_aspect() so the libretro layer can update A/V geometry
 * (e.g. 4:3 -> 16:9).  Future ultrawide profiles (21:9, 32:9) carry their own
 * ratio and table; they cannot be auto-derived from the 16:9 patch and must be
 * authored and verified per ROM.
 *
 * Author: libretroadmin
 */
#ifndef WIDESCREEN_H
#define WIDESCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Apply a verified widescreen patch to the z64-normalised ROM image in place
 * if it matches a known game; otherwise leave it untouched.
 * Returns 1 if a patch was applied, 0 if not. */
int widescreen_apply(unsigned char *rom, int size);

/* After widescreen_apply() returns 1, this yields the applied aspect ratio as
 * a float (e.g. 16.0/9.0).  Returns 0.0f if no widescreen patch is active. */
float widescreen_active_aspect(void);

/* Reset active state (call when freeing/reloading a ROM). */
void widescreen_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* WIDESCREEN_H */
