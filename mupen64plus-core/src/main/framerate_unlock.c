/* Game-specific framerate unlock (Hint) — registry.  See framerate_unlock.h.
 * Author: libretroadmin */

#include <stddef.h>

#include "rompatch_common.h"
#include "framerate_unlock.h"

/* Per-game verified word tables. */
#include "framerate_unlock_sm64.h"

/* Registry: one entry per supported game.  Add new games here. */
static const struct rompatch_profile framerate_unlock_profiles[] = {
   {
      "Super Mario 64 (USA v1.0) 60fps",
      { 'N', 'S', 'M' },                       /* header 0x3B..0x3D */
      framerate_unlock_sm64_us_words,
      sizeof(framerate_unlock_sm64_us_words) /
         sizeof(framerate_unlock_sm64_us_words[0]),
      0, 0                                      /* no aspect change */
   },
};

int framerate_unlock_apply(unsigned char *rom, int size)
{
   const struct rompatch_profile *p = rompatch_apply_registry(rom, size,
         framerate_unlock_profiles,
         sizeof(framerate_unlock_profiles) /
            sizeof(framerate_unlock_profiles[0]),
         "framerate-unlock hint");
   return p ? 1 : 0;
}
