/* test_gliden64_txfilters -- GLideNHQ's texture enhancements and filters
 * draw what they drew.
 *
 * The filters (2xSaI, hq2x/hq2xS, lq2x/lq2xS, hq4x, the plain 2x, the
 * sharp and smooth filters, xBRZ at each scale, and deposterize before
 * them) went from C++ to C89, and deposterize from scratch buffers each
 * thread grew while a game ran to one pair made at init, each thread's
 * rows at their own place in it. Each mode here runs on a 32x24 texture
 * of hard edges, gradients and posterized bands, and the output's FNV-1a
 * must be what the C++ filters gave: the values below were taken from
 * them. The texture is also run as four bands of rows, each into its part
 * of the scratch buffers, as the threads run it, and must give the same.
 *
 *   ./test_gliden64_txfilters
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "GLideNHQ/TextureFilters.h"

#define W 32
#define H 24

static uint32 src[W * H];
static uint32 band_dst[W * H * 36];
static uint32 scratch0[W * H], scratch1[W * H];

static uint32 fnv(const uint32 *p, size_t n)
{
   const unsigned char *b = (const unsigned char *)p;
   uint32 h = 2166136261u;
   size_t i;
   for (i = 0; i < n * 4; i++)
      h = (h ^ b[i]) * 16777619u;
   return h;
}

static void make_texture(void)
{
   unsigned x, y;
   for (y = 0; y < H; y++)
      for (x = 0; x < W; x++)
      {
         uint32 r, g, b, a;
         if (x < 10)
         {
            /* hard edges: a checker and a diagonal */
            const int on = ((x >> 1) ^ (y >> 1)) & 1;
            r = on ? 0xf0 : 0x10;
            g = (x + y) % 7 < 3 ? 0xe0 : 0x20;
            b = x == y ? 0xff : 0x40;
         }
         else if (x < 22)
         {
            /* posterized bands, a few levels apart: deposterize's */
            r = 0x40 + ((x * 3) / 4) * 4;
            g = 0x80 + (y / 3) * 3;
            b = 0x60 + ((x + y) / 5) * 2;
         }
         else
         {
            /* a smooth gradient */
            r = x * 7;
            g = 255 - y * 9;
            b = (x * y) & 0xff;
         }
         a = (x + y * 3) % 11 ? 0xff : 0x80;
         src[y * W + x] = (a << 24) | (b << 16) | (g << 8) | r;
      }
}

static const struct
{
   const char *what;
   uint32 filter;
   unsigned scale;
   uint32 want;
} cases[] = {
   { "2x", X2_ENHANCEMENT, 2, 0x4f271ae9u },
   { "2xSaI", X2SAI_ENHANCEMENT, 2, 0xf3648bc2u },
   { "hq2x", HQ2X_ENHANCEMENT, 2, 0xea7d2b9au },
   { "lq2x", LQ2X_ENHANCEMENT, 2, 0x97cfdee5u },
   { "hq4x", HQ4X_ENHANCEMENT, 4, 0x6f777747u },
   { "hq2xS", HQ2XS_ENHANCEMENT, 2, 0x6a9d2c80u },
   { "lq2xS", LQ2XS_ENHANCEMENT, 2, 0xb22ed6f5u },
   { "xBRZ 2x", BRZ2X_ENHANCEMENT, 2, 0x059b43f4u },
   { "xBRZ 3x", BRZ3X_ENHANCEMENT, 3, 0xa38f369fu },
   { "xBRZ 4x", BRZ4X_ENHANCEMENT, 4, 0x4af75c1eu },
   { "xBRZ 5x", BRZ5X_ENHANCEMENT, 5, 0xe5fe4fb6u },
   { "xBRZ 6x", BRZ6X_ENHANCEMENT, 6, 0x8856ccc5u },
   { "smooth 1", SMOOTH_FILTER_1, 1, 0x56a2f816u },
   { "smooth 2", SMOOTH_FILTER_2, 1, 0x13e7a10eu },
   { "smooth 3", SMOOTH_FILTER_3, 1, 0xab2e743bu },
   { "smooth 4", SMOOTH_FILTER_4, 1, 0xd2721583u },
   { "sharp 1", SHARP_FILTER_1, 1, 0x10d3725fu },
   { "sharp 2", SHARP_FILTER_2, 1, 0xb6d13897u },
   { "deposterize, 2x", DEPOSTERIZE | X2_ENHANCEMENT, 2, 0xa7886e93u },
   { "deposterize, hq4x", DEPOSTERIZE | HQ4X_ENHANCEMENT, 4, 0xd1b0a8bfu },
   { "deposterize, smooth 1", DEPOSTERIZE | SMOOTH_FILTER_1, 1, 0x637c5f85u }
};

int main(int argc, char **argv)
{
   unsigned k;
   int failures = 0, print = argc > 1 && !strcmp(argv[1], "--print");

   make_texture();
   for (k = 0; k < sizeof(cases) / sizeof(cases[0]); k++)
   {
      const unsigned s = cases[k].scale;
      const size_t n = (size_t)W * H * s * s;
      uint32 h;
      /* exactly the output's size, its own allocation: a filter writing
       * past it shows (with the sanitizers on) */
      uint32 *out = (uint32 *)calloc(n, sizeof(uint32));
      if (!out)
         return 1;
      filter_8888(src, W, H, out, cases[k].filter, scratch0, scratch1);
      h = fnv(out, n);
      free(out);
      if (print)
         printf("%-24s 0x%08xu\n", cases[k].what, (unsigned)h);
      else if (h != cases[k].want)
      {
         printf("FAIL: %s: %08x, not %08x\n", cases[k].what, (unsigned)h, (unsigned)cases[k].want);
         failures++;
      }

      /* deposterize alone, with no other filter after it, writes nothing;
       * the rest as the threads run them: four bands of six rows, each
       * from and into its own rows, and its own rows of the scratch */
      if (cases[k].filter & DEPOSTERIZE)
      {
         unsigned b;
         uint32 hb;
         memset(band_dst, 0, sizeof(band_dst));
         for (b = 0; b < 4; b++)
            filter_8888(src + b * 6 * W, W, 6, band_dst + (size_t)b * 6 * W * s * s, cases[k].filter,
                  scratch0 + b * 6 * W, scratch1 + b * 6 * W);
         /* each band deposterizes alone (its edges its own), as threads
          * do: its result is its own, but every band must be what the
          * same rows give run alone */
         for (b = 0; b < 4; b++)
         {
            static uint32 one[6 * W * 16];
            memset(one, 0, sizeof(one));
            filter_8888(src + b * 6 * W, W, 6, one, cases[k].filter, scratch0, scratch1);
            hb = fnv(one, (size_t)6 * W * s * s);
            if (hb != fnv(band_dst + (size_t)b * 6 * W * s * s, (size_t)6 * W * s * s))
            {
               printf("FAIL: %s: band %u in its own scratch rows differs from it alone\n", cases[k].what, b);
               failures++;
            }
         }
      }
   }
   if (!print)
      printf("test_gliden64_txfilters: %s, %u modes\n", failures ? "FAIL" : "ok",
            (unsigned)(sizeof(cases) / sizeof(cases[0])));
   return failures != 0;
}
