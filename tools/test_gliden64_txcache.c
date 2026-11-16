/* test_gliden64_txcache -- GLideNHQ's texture cache keeps the filtered texture.
 *
 * A texture GLideNHQ filters (HQ2X here) is cached, so the next time the
 * game draws it the filtered texels come back without filtering again.
 * The filter wrote into one of its two texture buffers and the cache
 * copied that into one of its own; the filter's last step now writes into
 * the buffer the cache keeps. This filters a texture of 16-bit texels
 * (47 x 15, an odd count: its last texel too), and filters it again: the
 * second time must come from the cache as the very buffer the first
 * returned, holding what HQ2X makes of the texture. With the cache
 * compressing, the texels must still come back the same.
 *
 *   ./test_gliden64_txcache
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "GLideNHQ/Ext_TxFilter.h"
#include "GLideNHQ/TextureFilters.h"
#include "GLideNHQ/TxQuantize.h"

#define W 47
#define H 15
/* GL_RGB5_A1, as GLideN64 hands GLideNHQ its 16-bit textures */
#define FMT_RGB5_A1 0x8057

static int failures;

static void check(int ok, const char *what)
{
   if (!ok)
   {
      printf("FAIL: %s\n", what);
      failures++;
   }
}

static int run(int options, const char *what)
{
   static uint16 tex[W * H];
   static uint32 rgba[W * H], want[W * H * 4];
   static uint32 s0[W * H * 4], s1[W * H * 4];
   GHQTexInfo a, b;
   unsigned i, seed = 7;
   int ok;
   char name[160];

   for (i = 0; i < W * H; i++)
   {
      seed = seed * 1103515245u + 12345u;
      tex[i] = (uint16)(seed >> 16);
   }
   /* what HQ2X makes of it */
   txquantize((uint8 *)tex, (uint8 *)rgba, W, H, TXQ_RGB5_A1, TXQ_RGBA8, 1, NULL, 1);
   filter_8888(rgba, W, H, want, HQ2X_ENHANCEMENT, s0, s1);

   if (!txfilter_init(256, 256, 32, options | HQ2X_ENHANCEMENT, 1 << 20, "/tmp", "/tmp", "/tmp", "TXCACHE TEST"))
   {
      check(0, "txfilter_init");
      return 0;
   }
   memset(&a, 0, sizeof(a));
   memset(&b, 0, sizeof(b));
   ok = txfilter_filter((uint8 *)tex, W, H, FMT_RGB5_A1, 0x1234, &a) && a.data && a.width == 2 * W && a.height == 2 * H;
   sprintf(name, "%s: filtered, 2x the size", what);
   check(ok, name);
   if (ok)
   {
      sprintf(name, "%s: what HQ2X makes of it, its last texels too", what);
      check(!memcmp(a.data, want, sizeof(want)), name);
   }
   ok = ok && txfilter_filter((uint8 *)tex, W, H, FMT_RGB5_A1, 0x1234, &b) && b.data && b.width == 2 * W;
   if (ok && !(options & GZ_TEXCACHE))
   {
      sprintf(name, "%s: again, from the cache, the buffer the filter wrote", what);
      check(b.data == a.data, name);
   }
   if (ok)
   {
      sprintf(name, "%s: again, from the cache, the same texels", what);
      check(!memcmp(b.data, want, sizeof(want)), name);
   }
   txfilter_shutdown();
   return ok;
}

int main(void)
{
   run(0, "the cache uncompressed");
   run(GZ_TEXCACHE, "the cache compressed");
   printf("test_gliden64_txcache: %s\n", failures ? "FAIL" : "ok");
   return failures != 0;
}
