/* test_gliden64_txcache_store -- GLideNHQ's texture cache, as it was.
 *
 * GLideNHQ keeps the textures it filtered, and those of hi-res packs, in a
 * cache by checksum: kept to a size, the least used given up past it, or
 * all of them; whole, or compressed; saved to disk and loaded back. It
 * went from a C++ class on std::map and std::list to C89 with a hash and
 * a list of its own. This runs the same scenario through it as through
 * the C++ class: adds (copied and taken), gets, the eviction they cause,
 * deletes, a checksum held already, a save and loads of it (with the
 * wrong config and the right one) into a cache of its own, with and
 * without a size, compressed and not. Each step's result, what the cache
 * then holds and the bytes it gives back, and the file it saves, go into
 * a log, whose FNV-1a must be what the C++ class's run gave.
 *
 *   ./test_gliden64_txcache_store [-v]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "GLideNHQ/TxCache.h"

#define TEXW(k) (4 << ((k) % 3))
#define TEXH(k) (4 << (((k) + 1) % 3))
static unsigned fnv_step(unsigned h, const void *p, size_t n)
{
   const unsigned char *b = (const unsigned char *)p;
   size_t i;
   for (i = 0; i < n; i++)
      h = (h ^ b[i]) * 16777619u;
   return h;
}
static void texels(uint8 *t, int n, unsigned seed)
{
   int i;
   for (i = 0; i < n; i++)
   {
      seed = seed * 1103515245u + 12345u;
      /* runs, so the compressing cache compresses */
      t[i] = (uint8)((seed >> 28) & 3);
   }
}

/* the C++ class's run of this scenario */
#define WANT 0xe53f49c4u
#define DIR "/tmp"

static unsigned h = 2166136261u;
static char line[256];
static int verbose;

static void log_line(void)
{
   h = fnv_step(h, line, strlen(line));
   if (verbose)
      puts(line);
}

static void state(txcache *c, const char *what)
{
   unsigned k;
   size_t n = (size_t)sprintf(line, "%s: total %d cached", what, c->total_size);
   for (k = 1; k <= 24; k++)
      if (txcache_is_cached(c, k))
         n += (size_t)sprintf(line + n, " %u", k);
   log_line();
}

static void add(txcache *c, unsigned k, int fmt16, int owned)
{
   GHQTexInfo info;
   const int w = TEXW(k), hh = TEXH(k), bytes = w * hh * (fmt16 ? 2 : 4);
   uint8 *t = (uint8 *)malloc(bytes);
   boolean r;
   texels(t, bytes, k * 77);
   memset(&info, 0, sizeof(info));
   info.data = t;
   info.width = w;
   info.height = hh;
   /* setTextureFormat's: GL_RGB8 as 565, GL_RGBA8 */
   info.format = fmt16 ? TXFMT_RGB8 : TXFMT_RGBA8;
   info.texture_format = fmt16 ? 0x1907 : 0x1908;
   info.pixel_type = fmt16 ? 0x8363 : 0x1401;
   r = txcache_add(c, k, &info, 0, (boolean)owned);
   sprintf(line, "add %u %dx%d %s%s: %d", k, w, hh, fmt16 ? "565" : "8888", owned ? " owned" : "", r);
   log_line();
   if (!owned || !r)
      free(t);
}

static void get(txcache *c, unsigned k)
{
   GHQTexInfo info;
   boolean r;
   memset(&info, 0, sizeof(info));
   r = txcache_get(c, k, &info);
   sprintf(line, "get %u: %d %dx%d fmt %x data %08x", k, r, info.width, info.height, info.format,
         r ? fnv_step(2166136261u, info.data, txutil_texture_bytes(info.width, info.height, info.format)) : 0u);
   log_line();
}

static void run(int options, int size, const char *name)
{
   txcache *c = txcache_new(options, size, DIR, "SCENARIO"), *d;
   unsigned k;
   sprintf(line, "== %s", name);
   log_line();
   for (k = 1; k <= 8; k++)
   {
      add(c, k, k & 1, k == 5);
      if (k == 4)
      {
         get(c, 2);
         get(c, 1);
      }
   }
   state(c, "after 8");
   get(c, 3);
   get(c, 7);
   get(c, 20);
   for (k = 9; k <= 14; k++)
      add(c, k, 0, k & 1);
   state(c, "after 14");
   add(c, 13, 0, 0);
   add(c, 0, 0, 0);
   {
      const int d1 = txcache_del(c, 12), d2 = txcache_del(c, 12), d3 = txcache_del(c, 99);
      sprintf(line, "del 12: %d, del 12: %d, del 99: %d", d1, d2, d3);
      log_line();
   }
   state(c, "after del");
   for (k = 1; k <= 14; k++)
      get(c, k);
   sprintf(line, "save: %d", txcache_save(c, DIR, "txcache_store.htc", 0x5a5a));
   log_line();
   {
      static uint8 buf[1 << 20];
      FILE *f = fopen(DIR "/txcache_store.htc", "rb");
      const size_t n = f ? fread(buf, 1, sizeof(buf), f) : 0;
      if (f)
         fclose(f);
      sprintf(line, "file %lu bytes %08x", (unsigned long)n, fnv_step(2166136261u, buf, n));
      log_line();
   }
   d = txcache_new(options, size, DIR, "SCENARIO");
   sprintf(line, "load wrong config: %d", txcache_load(d, DIR, "txcache_store.htc", 0x1234, 0));
   log_line();
   sprintf(line, "load: %d", txcache_load(d, DIR, "txcache_store.htc", 0x5a5a, 0));
   log_line();
   state(d, "loaded");
   for (k = 1; k <= 14; k++)
      get(d, k);
   txcache_free(d);
   txcache_free(c);
   remove(DIR "/txcache_store.htc");
}

int main(int argc, char **argv)
{
   verbose = argc > 1 && !strcmp(argv[1], "-v");
   txmembuf_init(64, 64);
   run(0, 2000, "evicting, uncompressed");
   run(GZ_TEXCACHE, 260, "evicting, compressed");
   run(0, 0, "keeping all, uncompressed");
   run(GZ_HIRESTEXCACHE, 0, "keeping all, compressed");
   txmembuf_shutdown();
   if (h != WANT)
   {
      printf("FAIL: the run's log %08x, not the C++ class's %08x (-v shows it)\n", h, WANT);
      return 1;
   }
   printf("test_gliden64_txcache_store: ok\n");
   return 0;
}
