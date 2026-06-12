/* Game-specific widescreen (Hint) — registry.  See widescreen.h.
 * Author: libretroadmin */

#include <stddef.h>

#include "rompatch_common.h"
#include "widescreen.h"

/* Per-game verified word tables. */
#include "widescreen_sm64.h"
#include "widescreen_sm64_eu.h"

/* Registry: one entry per supported game/ratio.  Add new games here.
 *
 * Each profile is authored and verified against one exact ROM.  US and EU SM64
 * share the cartridge ID "NSM", so the per-word signature (the 'orig' words) is
 * what selects the right profile: rompatch applies a profile only if every word
 * verifies, and an EU table's orig words do not match a US image (or vice versa),
 * so the correct one self-selects.  The US table is gamemasterplc's full asm
 * widescreen patch; the EU table is a single-hunk aspect change (see its header). */
static const struct rompatch_profile widescreen_profiles[] = {
   {
      "Super Mario 64 (USA v1.0) 16:9",
      { 'N', 'S', 'M' },                       /* header 0x3B..0x3D */
      widescreen_sm64_us_words,
      sizeof(widescreen_sm64_us_words) /
         sizeof(widescreen_sm64_us_words[0]),
      16, 9                                     /* aspect ratio */
   },
   {
      "Super Mario 64 (Europe) 16:9",
      { 'N', 'S', 'M' },                       /* header 0x3B..0x3D */
      widescreen_sm64_eu_words,
      sizeof(widescreen_sm64_eu_words) /
         sizeof(widescreen_sm64_eu_words[0]),
      16, 9                                     /* aspect ratio */
   },
};

static float widescreen_aspect = 0.0f;

void widescreen_reset(void)
{
   widescreen_aspect = 0.0f;
}

float widescreen_active_aspect(void)
{
   return widescreen_aspect;
}

int widescreen_apply(unsigned char *rom, int size)
{
   const struct rompatch_profile *p = rompatch_apply_registry(rom, size,
         widescreen_profiles,
         sizeof(widescreen_profiles) / sizeof(widescreen_profiles[0]),
         "widescreen hint");

   if (p && p->aspect_den != 0)
   {
      widescreen_aspect = (float)p->aspect_num / (float)p->aspect_den;
      return 1;
   }
   return 0;
}
