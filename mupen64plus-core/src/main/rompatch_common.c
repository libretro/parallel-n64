/* Shared ROM-patch verification machinery.  See rompatch_common.h.
 * Author: libretroadmin */

#include <stddef.h>
#include <stdint.h>

#include "api/callbacks.h"
#include "rompatch_common.h"

static uint32_t rompatch_rd32(const unsigned char *p)
{
   return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
        | ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
}

static void rompatch_wr32(unsigned char *p, uint32_t v)
{
   p[0] = (unsigned char)(v >> 24);
   p[1] = (unsigned char)(v >> 16);
   p[2] = (unsigned char)(v >>  8);
   p[3] = (unsigned char)(v);
}

/* cart[] is the 3-byte family code at header 0x3B..0x3D ("NSM" for SM64). */
/* Recompute the N64 ROM header checksum (CIC-NUS-6102 algorithm, used by SM64
 * and most retail carts) over the boot region 0x1000..0x101000 and write the
 * two CRC words at header 0x10/0x14.  Required after patching code in that
 * region, otherwise the in-game self-check leaves the game on a black screen.
 * Returns nothing; assumes size >= 0x101000. */
static uint32_t rompatch_rol(uint32_t v, uint32_t b)
{
   b &= 31u;
   return (v << b) | (v >> (32u - b));
}

static void rompatch_fix_crc(unsigned char *rom, int size)
{
   uint32_t t1, t2, t3, t4, t5, t6, d, r;
   int i;
   const uint32_t SEED = 0xF8CA4DDCu;

   if (size < 0x101000)
      return;

   t1 = t2 = t3 = t4 = t5 = t6 = SEED;
   for (i = 0x1000; i < 0x101000; i += 4)
   {
      d = rompatch_rd32(rom + i);
      if (((t6 + d) & 0xFFFFFFFFu) < t6)
         t4 = (t4 + 1u) & 0xFFFFFFFFu;
      t6 = (t6 + d) & 0xFFFFFFFFu;
      t3 ^= d;
      r = rompatch_rol(d, d & 0x1Fu);
      t5 = (t5 + r) & 0xFFFFFFFFu;
      if (t2 > d)
         t2 ^= r;
      else
         t2 ^= (t6 ^ d) & 0xFFFFFFFFu;
      t1 = (t1 + ((t5 ^ d) & 0xFFFFFFFFu)) & 0xFFFFFFFFu;
   }
   rompatch_wr32(rom + 0x10, (t6 ^ t4 ^ t3) & 0xFFFFFFFFu);
   rompatch_wr32(rom + 0x14, (t5 ^ t2 ^ t1) & 0xFFFFFFFFu);
}

static int rompatch_cart_match(const unsigned char *rom, const char cart[3])
{
   return rom[0x3B] == (unsigned char)cart[0]
       && rom[0x3C] == (unsigned char)cart[1]
       && rom[0x3D] == (unsigned char)cart[2];
}

static int rompatch_try_profile(unsigned char *rom, int size,
      const struct rompatch_profile *prof, const char *tag)
{
   size_t i;
   uint32_t hi;

   if (size < 0x40)
      return 0;
   if (!rompatch_cart_match(rom, prof->cart))
      return 0;
   if (prof->word_count == 0)
      return 0;

   /* Bounds: highest touched offset must fit. */
   hi = prof->words[prof->word_count - 1].off;
   if ((int)(hi + 4) > size)
   {
      DebugMessage(M64MSG_INFO,
            "%s: '%s' image too small (0x%x > 0x%x), skipping",
            tag, prof->name, (unsigned)(hi + 4), (unsigned)size);
      return 0;
   }

   /* PASS 1 — verify every word; no writes. */
   for (i = 0; i < prof->word_count; i++)
   {
      const struct rompatch_word *w = &prof->words[i];
      uint32_t have;

      if (w->flags & ROMPATCH_WF_NOVERIFY)
         continue;

      have = rompatch_rd32(rom + w->off);
      if (have != w->orig)
      {
         DebugMessage(M64MSG_INFO,
               "%s: '%s' signature mismatch at 0x%06x (have %08x want %08x), "
               "leaving ROM untouched",
               tag, prof->name, (unsigned)w->off,
               (unsigned)have, (unsigned)w->orig);
         return 0;
      }
   }

   /* PASS 2 — all verified; apply. */
   for (i = 0; i < prof->word_count; i++)
      rompatch_wr32(rom + prof->words[i].off, prof->words[i].patch);

   /* Patched code in the IPL/boot region invalidates the header checksum;
    * recompute it so the game's self-check passes. */
   rompatch_fix_crc(rom, size);

   DebugMessage(M64MSG_INFO, "%s: applied '%s' (%u words)",
         tag, prof->name, (unsigned)prof->word_count);
   return 1;
}


const struct rompatch_profile *rompatch_apply_registry(
      unsigned char *rom, int size,
      const struct rompatch_profile *profiles, size_t profile_count,
      const char *tag)
{
   size_t i;
   for (i = 0; i < profile_count; i++)
   {
      if (rompatch_try_profile(rom, size, &profiles[i], tag))
         return &profiles[i];
   }
   return NULL;
}
