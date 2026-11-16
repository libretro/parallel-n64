/* test_gliden64_txquantize -- GLideNHQ's quantizer gives what it gave.
 *
 * The quantizer (16-bit textures to 32-bit RGBA and back, rounded or
 * with the errors diffused; 8-bit indices through a palette) went from a
 * C++ class to C89 functions. Each case runs on a texture of noise, and
 * its texels' FNV-1a must be what the C++ one gave: the values below were
 * taken from it. The rows are split into blocks for the lanes there are,
 * and a block diffuses its errors on its own, so four lanes give their
 * own values; they are run on four threads and again with no pool, on
 * the caller, and must match either way.
 *
 *   ./test_gliden64_txquantize
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "GLideNHQ/TxQuantize.h"

static uint32 fnv(const uint8 *b, size_t n)
{
   uint32 h = 2166136261u;
   size_t i;
   for (i = 0; i < n; i++)
      h = (h ^ b[i]) * 16777619u;
   return h;
}

/* the C++ harness's noise: its int seed, wrapping, kept unsigned here */
static void fill(uint8 *t, int n, unsigned seed)
{
   int i;
   for (i = 0; i < n; i++)
   {
      int s;
      seed = seed * 1103515245u + 12345u;
      s = (int)seed;
      t[i] = (uint8)((i & 3) == 3 ? (s >> 16) : (i * 7 + (s >> 24) % 9));
   }
}

static const struct
{
   unsigned lanes;
   int w, h, srcformat, destformat, fast;
   uint32 want;
   const char *what;
} cases[] = {
   { 1, 64, 48, TXQ_RGB5_A1, TXQ_RGBA8, 0, 0x81fa1af5u, "RGB5_A1 to RGBA8" },
   { 1, 64, 48, TXQ_RGBA8, TXQ_RGB5_A1, 1, 0xcccba359u, "RGBA8 to RGB5_A1" },
   { 1, 64, 48, TXQ_RGBA8, TXQ_RGB5_A1, 0, 0xd6646b6fu, "RGBA8 to RGB5_A1, diffused" },
   { 1, 64, 48, TXQ_RGBA4, TXQ_RGBA8, 0, 0x58e64364u, "RGBA4 to RGBA8" },
   { 1, 64, 48, TXQ_RGBA8, TXQ_RGBA4, 1, 0x3a422eb2u, "RGBA8 to RGBA4" },
   { 1, 64, 48, TXQ_RGBA8, TXQ_RGBA4, 0, 0x36b8e888u, "RGBA8 to RGBA4, diffused" },
   { 1, 64, 48, TXQ_RGB565, TXQ_RGBA8, 0, 0x104c8c7cu, "RGB565 to RGBA8" },
   { 1, 64, 48, TXQ_RGBA8, TXQ_RGB565, 1, 0xc6bcd64au, "RGBA8 to RGB565" },
   { 1, 64, 48, TXQ_RGBA8, TXQ_RGB565, 0, 0xf2e23f9cu, "RGBA8 to RGB565, diffused" },
   { 1, 30, 20, TXQ_RGB5_A1, TXQ_RGBA8, 0, 0xab2ab15au, "RGB5_A1 to RGBA8" },
   { 1, 30, 20, TXQ_RGBA8, TXQ_RGB5_A1, 1, 0xac973a9bu, "RGBA8 to RGB5_A1" },
   { 1, 30, 20, TXQ_RGBA8, TXQ_RGB5_A1, 0, 0xc9963c15u, "RGBA8 to RGB5_A1, diffused" },
   { 1, 30, 20, TXQ_RGBA4, TXQ_RGBA8, 0, 0xe964b8f5u, "RGBA4 to RGBA8" },
   { 1, 30, 20, TXQ_RGBA8, TXQ_RGBA4, 1, 0x486d3824u, "RGBA8 to RGBA4" },
   { 1, 30, 20, TXQ_RGBA8, TXQ_RGBA4, 0, 0x7e198579u, "RGBA8 to RGBA4, diffused" },
   { 1, 30, 20, TXQ_RGB565, TXQ_RGBA8, 0, 0x5f93ac51u, "RGB565 to RGBA8" },
   { 1, 30, 20, TXQ_RGBA8, TXQ_RGB565, 1, 0x4ef1c971u, "RGBA8 to RGB565" },
   { 1, 30, 20, TXQ_RGBA8, TXQ_RGB565, 0, 0x6c1a89acu, "RGBA8 to RGB565, diffused" },
   { 1, 16, 4, TXQ_RGB5_A1, TXQ_RGBA8, 0, 0x9c84d55au, "RGB5_A1 to RGBA8" },
   { 1, 16, 4, TXQ_RGBA8, TXQ_RGB5_A1, 1, 0x41f6840du, "RGBA8 to RGB5_A1" },
   { 1, 16, 4, TXQ_RGBA8, TXQ_RGB5_A1, 0, 0x285628e1u, "RGBA8 to RGB5_A1, diffused" },
   { 1, 16, 4, TXQ_RGBA4, TXQ_RGBA8, 0, 0x5af19144u, "RGBA4 to RGBA8" },
   { 1, 16, 4, TXQ_RGBA8, TXQ_RGBA4, 1, 0x020f63e7u, "RGBA8 to RGBA4" },
   { 1, 16, 4, TXQ_RGBA8, TXQ_RGBA4, 0, 0x54b67d87u, "RGBA8 to RGBA4, diffused" },
   { 1, 16, 4, TXQ_RGB565, TXQ_RGBA8, 0, 0x634df1a5u, "RGB565 to RGBA8" },
   { 1, 16, 4, TXQ_RGBA8, TXQ_RGB565, 1, 0x4d3e2eafu, "RGBA8 to RGB565" },
   { 1, 16, 4, TXQ_RGBA8, TXQ_RGB565, 0, 0x59843e98u, "RGBA8 to RGB565, diffused" },
   { 4, 64, 48, TXQ_RGB5_A1, TXQ_RGBA8, 0, 0x81fa1af5u, "RGB5_A1 to RGBA8" },
   { 4, 64, 48, TXQ_RGBA8, TXQ_RGB5_A1, 1, 0xcccba359u, "RGBA8 to RGB5_A1" },
   { 4, 64, 48, TXQ_RGBA8, TXQ_RGB5_A1, 0, 0x5b09b901u, "RGBA8 to RGB5_A1, diffused" },
   { 4, 64, 48, TXQ_RGBA4, TXQ_RGBA8, 0, 0x58e64364u, "RGBA4 to RGBA8" },
   { 4, 64, 48, TXQ_RGBA8, TXQ_RGBA4, 1, 0x3a422eb2u, "RGBA8 to RGBA4" },
   { 4, 64, 48, TXQ_RGBA8, TXQ_RGBA4, 0, 0x0d5d7625u, "RGBA8 to RGBA4, diffused" },
   { 4, 64, 48, TXQ_RGB565, TXQ_RGBA8, 0, 0x104c8c7cu, "RGB565 to RGBA8" },
   { 4, 64, 48, TXQ_RGBA8, TXQ_RGB565, 1, 0xc6bcd64au, "RGBA8 to RGB565" },
   { 4, 64, 48, TXQ_RGBA8, TXQ_RGB565, 0, 0xc0ed9cc7u, "RGBA8 to RGB565, diffused" },
   { 4, 30, 20, TXQ_RGB5_A1, TXQ_RGBA8, 0, 0xab2ab15au, "RGB5_A1 to RGBA8" },
   { 4, 30, 20, TXQ_RGBA8, TXQ_RGB5_A1, 1, 0xac973a9bu, "RGBA8 to RGB5_A1" },
   { 4, 30, 20, TXQ_RGBA8, TXQ_RGB5_A1, 0, 0x9d3618ddu, "RGBA8 to RGB5_A1, diffused" },
   { 4, 30, 20, TXQ_RGBA4, TXQ_RGBA8, 0, 0xe964b8f5u, "RGBA4 to RGBA8" },
   { 4, 30, 20, TXQ_RGBA8, TXQ_RGBA4, 1, 0x486d3824u, "RGBA8 to RGBA4" },
   { 4, 30, 20, TXQ_RGBA8, TXQ_RGBA4, 0, 0x834a73b9u, "RGBA8 to RGBA4, diffused" },
   { 4, 30, 20, TXQ_RGB565, TXQ_RGBA8, 0, 0x5f93ac51u, "RGB565 to RGBA8" },
   { 4, 30, 20, TXQ_RGBA8, TXQ_RGB565, 1, 0x4ef1c971u, "RGBA8 to RGB565" },
   { 4, 30, 20, TXQ_RGBA8, TXQ_RGB565, 0, 0x71ed5330u, "RGBA8 to RGB565, diffused" },
   { 4, 16, 4, TXQ_RGB5_A1, TXQ_RGBA8, 0, 0x9c84d55au, "RGB5_A1 to RGBA8" },
   { 4, 16, 4, TXQ_RGBA8, TXQ_RGB5_A1, 1, 0x41f6840du, "RGBA8 to RGB5_A1" },
   { 4, 16, 4, TXQ_RGBA8, TXQ_RGB5_A1, 0, 0x285628e1u, "RGBA8 to RGB5_A1, diffused" },
   { 4, 16, 4, TXQ_RGBA4, TXQ_RGBA8, 0, 0x5af19144u, "RGBA4 to RGBA8" },
   { 4, 16, 4, TXQ_RGBA8, TXQ_RGBA4, 1, 0x020f63e7u, "RGBA8 to RGBA4" },
   { 4, 16, 4, TXQ_RGBA8, TXQ_RGBA4, 0, 0x54b67d87u, "RGBA8 to RGBA4, diffused" },
   { 4, 16, 4, TXQ_RGB565, TXQ_RGBA8, 0, 0x634df1a5u, "RGB565 to RGBA8" },
   { 4, 16, 4, TXQ_RGBA8, TXQ_RGB565, 1, 0x4d3e2eafu, "RGBA8 to RGB565" },
   { 4, 16, 4, TXQ_RGBA8, TXQ_RGB565, 0, 0x59843e98u, "RGBA8 to RGB565, diffused" },
};

static const struct { int w, h; uint32 want; } p8[] = {
   { 16, 4, 0xefe8ae43u },
   { 30, 20, 0x0dc8f4d6u },
   { 64, 48, 0xcd420bf0u },
};

/* the C++ harness's seeds, by the 16-bit format's place */
static int place(int f) { return f == TXQ_RGB5_A1 ? 0 : f == TXQ_RGBA4 ? 1 : 2; }

int main(void)
{
   txpool_t *pool = txpool_new(4);
   unsigned k, pass;
   int failures = 0;

   for (pass = 0; pass < 2; pass++)
      for (k = 0; k < sizeof(cases) / sizeof(cases[0]); k++)
      {
         const int w = cases[k].w, h = cases[k].h;
         const int to32 = cases[k].destformat == TXQ_RGBA8;
         const int f = place(to32 ? cases[k].srcformat : cases[k].destformat);
         uint8 *src = (uint8 *)malloc((size_t)w * h * 4), *dst = (uint8 *)malloc((size_t)w * h * 4);
         uint32 hash;
         int ok;
         if (!src || !dst)
            return 1;
         fill(src, w * h * (to32 ? 2 : 4), to32 ? 17u + f : 91u + f);
         memset(dst, 0xcd, (size_t)w * h * 4);
         /* the second pass runs the lanes on the caller */
         ok = txquantize(src, dst, w, h, cases[k].srcformat, cases[k].destformat, (boolean)cases[k].fast,
               pass ? NULL : pool, cases[k].lanes);
         hash = ok ? fnv(dst, (size_t)w * h * (to32 ? 4 : 2)) : 0;
         if (hash != cases[k].want)
         {
            printf("FAIL: %s, %dx%d, %u lanes%s: %08x, not %08x\n", cases[k].what, w, h, cases[k].lanes,
                  pass ? " on the caller" : "", (unsigned)hash, (unsigned)cases[k].want);
            failures++;
         }
         free(src);
         free(dst);
      }

   for (k = 0; k < sizeof(p8) / sizeof(p8[0]); k++)
   {
      const int w = p8[k].w, h = p8[k].h;
      uint8 *src = (uint8 *)malloc((size_t)w * h), *dst = (uint8 *)malloc((size_t)w * h * 4);
      uint16 pal[256];
      if (!src || !dst)
         return 1;
      fill(src, w * h, 5u);
      fill(dst, w * h * 4, 6u);
      fill((uint8 *)pal, 512, 7u);
      txquantize_p8_16bpp(src, (uint16 *)dst, w, h, pal);
      if (fnv(dst, (size_t)w * h * 2) != p8[k].want)
      {
         printf("FAIL: palette, %dx%d: %08x, not %08x\n", w, h, (unsigned)fnv(dst, (size_t)w * h * 2),
               (unsigned)p8[k].want);
         failures++;
      }
      free(src);
      free(dst);
   }

   /* pairs it does not convert */
   {
      uint8 a[64], b[64];
      if (txquantize(a, b, 4, 4, TXQ_RGB5_A1, TXQ_RGBA4, 1, NULL, 1)
            || txquantize(a, b, 4, 4, TXQ_RGBA8, TXQ_RGBA8, 1, NULL, 1))
      {
         printf("FAIL: a pair it cannot convert was taken\n");
         failures++;
      }
   }

   txpool_free(pool);
   printf("test_gliden64_txquantize: %s, %u cases\n", failures ? "FAIL" : "ok",
         (unsigned)(2 * sizeof(cases) / sizeof(cases[0]) + sizeof(p8) / sizeof(p8[0])));
   return failures != 0;
}
