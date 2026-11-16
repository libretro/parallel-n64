/* test_gliden64_txresample -- GLideNHQ's resampler gives what it gave.
 *
 * The resampler (padding a texture out to powers of two, the 3dfx way or
 * not, at 32 and 8 bits a texel; shrinking a hi-res texture by 2 or 4
 * with a Kaiser window) went from a C++ class of no state to C89
 * functions. Each case here runs on a texture of edges and gradients,
 * and the size it gives and its texels' FNV-1a must be what the C++ one
 * gave: the values below were taken from it.
 *
 *   ./test_gliden64_txresample
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "GLideNHQ/TxReSample.h"

static uint32 fnv(const uint8 *b, size_t n)
{
   uint32 h = 2166136261u;
   size_t i;
   for (i = 0; i < n; i++)
      h = (h ^ b[i]) * 16777619u;
   return h;
}

static uint8 *texture(int w, int h, int bpp)
{
   const int bytes = (w * h * bpp) >> 3;
   uint8 *t = (uint8 *)malloc(bytes);
   int i;
   if (!t)
      return NULL;
   for (i = 0; i < bytes; i++)
   {
      const int x = (i * 8 / bpp) % w, y = (i * 8 / bpp) / w;
      t[i] = (uint8)((x < w / 3) ? (((x ^ y) & 4) ? 0xf0 : 0x10) : (x * 37 + y * 11 + (i & 3) * 64));
   }
   return t;
}

static const struct
{
   const char *what;
   int minify;      /* the ratio, or 0: padded to powers of two */
   int w, h, bpp, use_3dfx;
   int want_w, want_h;
   uint32 want;
} cases[] = {
   { "pow2 30x17 32bpp", 0, 30, 17, 32, 0, 32, 16, 0xa002afc5u },
   { "pow2 30x17 32bpp 3dfx", 0, 30, 17, 32, 1, 32, 16, 0xa002afc5u },
   { "pow2 130x9 32bpp", 0, 130, 9, 32, 0, 128, 8, 0x5cb37965u },
   { "pow2 130x9 32bpp 3dfx", 0, 130, 9, 32, 1, 128, 16, 0xc12c3585u },
   { "pow2 20x33 8bpp", 0, 20, 33, 8, 0, 32, 32, 0x4d391665u },
   { "pow2 20x33 8bpp 3dfx", 0, 20, 33, 8, 1, 32, 32, 0x4d391665u },
   { "pow2 64x64 32bpp", 0, 64, 64, 32, 0, 64, 64, 0x85701c45u },
   { "pow2 67x8 32bpp", 0, 67, 8, 32, 0, 64, 8, 0x3916cc15u },
   { "minify 64x48 by 2", 2, 64, 48, 32, 0, 32, 24, 0x982d4b6fu },
   { "minify 64x48 by 4", 4, 64, 48, 32, 0, 16, 12, 0xbd59cd9du },
   { "minify 40x24 by 2", 2, 40, 24, 32, 0, 20, 12, 0x88176114u }
};

int main(int argc, char **argv)
{
   unsigned k;
   int failures = 0, print = argc > 1 && !strcmp(argv[1], "--print");

   for (k = 0; k < sizeof(cases) / sizeof(cases[0]); k++)
   {
      int w = cases[k].w, h = cases[k].h;
      uint8 *t = texture(w, h, cases[k].bpp);
      int ok;
      uint32 hash;
      if (!t)
         return 1;
      if (cases[k].minify)
         ok = txresample_minify(&t, &w, &h, cases[k].minify);
      else
         ok = txresample_next_pow2(&t, &w, &h, cases[k].bpp, (boolean)cases[k].use_3dfx);
      hash = ok ? fnv(t, (size_t)((w * h * cases[k].bpp) >> 3)) : 0;
      if (print)
         printf("%-24s %d %d 0x%08xu\n", cases[k].what, w, h, (unsigned)hash);
      else if (!ok || w != cases[k].want_w || h != cases[k].want_h || hash != cases[k].want)
      {
         printf("FAIL: %s: %s %dx%d %08x, not %dx%d %08x\n", cases[k].what, ok ? "ok" : "failed", w, h,
               (unsigned)hash, cases[k].want_w, cases[k].want_h, (unsigned)cases[k].want);
         failures++;
      }
      free(t);
   }
   /* the power of two at or above */
   if (!print && (txresample_pow2(1) != 1 || txresample_pow2(17) != 32 || txresample_pow2(64) != 64 ||
            txresample_pow2(1025) != 2048))
   {
      printf("FAIL: powers of two: %d %d %d %d\n", txresample_pow2(1), txresample_pow2(17),
            txresample_pow2(64), txresample_pow2(1025));
      failures++;
   }
   if (!print)
      printf("test_gliden64_txresample: %s, %u cases\n", failures ? "FAIL" : "ok",
            (unsigned)(sizeof(cases) / sizeof(cases[0])));
   return failures != 0;
}
