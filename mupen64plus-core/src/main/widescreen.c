/* Game-specific widescreen (Hint) — registry.  See widescreen.h.
 * Author: libretroadmin */

#include <stddef.h>

#include "rompatch_common.h"
#include "widescreen.h"

/* Per-game verified word tables. */
#include "widescreen_sm64.h"

/* Registry: one entry per supported game/ratio.  Add new games here.
 *
 * Note: the SM64 widescreen patch is USA v1.0 only.  Empirically, fewer than
 * 2% of its hunk offsets carry the same bytes in the EU/JP/Shindou builds, so
 * it cannot be relocated to them automatically; those (and ultrawide ratios)
 * require their own authored, verified tables and drop in here as new entries. */
static const struct rompatch_profile widescreen_profiles[] = {
   {
      "Super Mario 64 (USA v1.0) 16:9",
      { 'N', 'S', 'M' },                       /* header 0x3B..0x3D */
      widescreen_sm64_us_words,
      sizeof(widescreen_sm64_us_words) /
         sizeof(widescreen_sm64_us_words[0]),
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
