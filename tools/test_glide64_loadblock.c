/* test_glide64_loadblock -- Glide64's LoadBlock and LoadTile copies
 * (glide2gl/src/Glide64/TexLoadBlock.c).
 *
 * LoadBlock copies 64-bit words out of RDRAM, byte-swapped, and swaps
 * the two halves of the words on every other line of the texture. Where
 * a line ends comes from adding the command's dxt for each word and
 * looking at the top bit of the sum, which is meant to wrap: with a
 * signed sum that was undefined behaviour (and stopped the sanitized
 * build) for any dxt whose sums pass 2^31. Each word must come out
 * swapped exactly when the top bit of (index * dxt) mod 2^32 is set.
 *
 * LoadTile copies whole lines and swaps the halves on every other one.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../glide2gl/src/Glide64/TexLoadBlock.h"

static int failures;

#define WORDS 64

static uint32_t swap32(uint32_t x)
{
   return (x >> 24) | ((x >> 8) & 0xff00) | ((x << 8) & 0xff0000) | (x << 24);
}

static void block(int dxt, int cnt)
{
   uint32_t src[2 * WORDS + 2];
   uint32_t dst[2 * WORDS + 2];
   int i;

   for (i = 0; i < 2 * WORDS + 2; i++)
   {
      src[i] = 0x01020304u * (uint32_t)(i + 1) + (uint32_t)i;
      dst[i] = 0xdeadbeefu;
   }

   loadBlock(src, dst, 0, dxt, cnt);

   for (i = 0; i < cnt; i++)
   {
      const int swapped = i > 0 && (((uint32_t)i * (uint32_t)dxt) & 0x80000000u) != 0;
      const uint32_t lo = swap32(src[2 * i]), hi = swap32(src[2 * i + 1]);
      if (dst[2 * i] != (swapped ? hi : lo) || dst[2 * i + 1] != (swapped ? lo : hi))
      {
         fprintf(stderr, "loadBlock dxt %08x: word %d of %d is %08x %08x, wanted %s\n", (unsigned)dxt, i, cnt,
               (unsigned)dst[2 * i], (unsigned)dst[2 * i + 1], swapped ? "swapped" : "in order");
         failures++;
         return;
      }
   }
   if (dst[2 * cnt] != 0xdeadbeefu || dst[2 * cnt + 1] != 0xdeadbeefu)
   {
      fprintf(stderr, "loadBlock dxt %08x: wrote past its %d words\n", (unsigned)dxt, cnt);
      failures++;
   }
}

static void tile(void)
{
   uint32_t src[64];
   uint32_t dst[64];
   int i, row;

   for (i = 0; i < 64; i++)
   {
      src[i] = 0x11223344u + (uint32_t)i * 0x01010101u;
      dst[i] = 0xdeadbeefu;
   }

   /* 3 lines of 2 words, 24 bytes apart in the source */
   loadTile(src, dst, 2, 3, 24, 8, dst + 63);

   for (row = 0; row < 3; row++)
      for (i = 0; i < 2; i++)
      {
         const uint32_t *s = src + 2 + row * 6 + 2 * i;
         const uint32_t lo = swap32(s[0]), hi = swap32(s[1]);
         const uint32_t *d = dst + row * 4 + 2 * i;
         if (d[0] != ((row & 1) ? hi : lo) || d[1] != ((row & 1) ? lo : hi))
         {
            fprintf(stderr, "loadTile: line %d word %d is %08x %08x\n", row, i, (unsigned)d[0], (unsigned)d[1]);
            failures++;
            return;
         }
      }
   if (dst[12] != 0xdeadbeefu)
   {
      fprintf(stderr, "loadTile: wrote past its lines\n");
      failures++;
   }
}

int main(void)
{
   /* the step the sanitized build stopped on: its eighth sum is 2^31 */
   block(0x10000000, 32);
   /* a line every 4 words, every 2, every word */
   block(0x20000000, WORDS);
   block(0x40000000, WORDS);
   block((int)0x80000000u, WORDS);
   /* steps that do not divide 2^32, a negative one, and none */
   block(0x0aaaaaab, WORDS);
   block(0x7fffffff, WORDS);
   block(-0x12345678, WORDS);
   block(0, WORDS);
   block(0x800, 16);
   block(0x10000000, 1);
   block(0x10000000, 0);
   tile();

   if (failures)
   {
      fprintf(stderr, "test_glide64_loadblock: %d failure(s)\n", failures);
      return 1;
   }
   printf("test_glide64_loadblock: ok\n");
   return 0;
}
