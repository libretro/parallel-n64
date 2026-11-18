/* test_gliden64_txhirescache -- GLideNHQ loads a Rice hi-res texture pack.
 *
 * TxHiResCache reads a pack (<pack>/<ident>/..., sub-directories too) by
 * Rice's names, ident#crc#fmt#siz[#palcrc]_<kind>.<png|bmp>: _rgb with
 * its _a merged in (the _a image's red as alpha) or made opaque, _all,
 * _ciByRGBA, _allciByRGBA and _ci; it rounds the alpha of fmt 0 siz 2 and
 * fmt 2 textures and paints their see-through texels with their opaque
 * neighbours (unless the texture artists may fly), puts Rice's I into the
 * alpha of an opaque fmt 4, makes too big a texture smaller, picks the
 * format (16 bits forced, or not), and keeps what it read, whole or
 * compressed, on disk too when asked. Files it cannot take are passed by.
 *
 * This writes such a pack and reads it through GLideNHQ's C API under
 * each of those options: each texture back, its size, formats and texels,
 * must be what GLideNHQ has always made of it (WANT, one CRC over all).
 *
 *   ./test_gliden64_txhirescache [dir, /tmp by default: what it writes there is removed after] [-v]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define rmdir_ _rmdir
#define mkdir_(p) _mkdir(p)
#else
#include <sys/stat.h>
#include <unistd.h>
#define rmdir_ rmdir
#define mkdir_(p) mkdir(p, 0755)
#endif

#include "GLideNHQ/Ext_TxFilter.h"
#include "GLideNHQ/TxImage.h"
#include "GLideNHQ/TxQuantize.h"
#include <encodings/crc32.h>

#define IDENT "HIRES TEST"
#define WANT 0x1fbdece0u

static int failures, verbose;
static char dir[512] = "/tmp";
static unsigned seed = 1;

static void check(int ok, const char *what)
{
   if (!ok)
   {
      printf("FAIL: %s\n", what);
      failures++;
   }
}

static unsigned rnd(void)
{
   seed = seed * 1103515245u + 12345u;
   return seed >> 8;
}

static const char *at(const char *name)
{
   static char path[1024];
   sprintf(path, "%s/txhirescache/%s", dir, name);
   return path;
}

/* @n RGBA8 texels: alpha from @alpha (-1 random, -2 0 or 255, -3 0, 128 or 255), grey within @grey */
static void texels(unsigned char *t, unsigned n, int alpha, int grey)
{
   unsigned i;
   for (i = 0; i < n; i++)
   {
      unsigned v = rnd();
      unsigned r = v & 0xff;
      t[4 * i + 0] = (unsigned char)r;
      t[4 * i + 1] = (unsigned char)(grey ? (r + (rnd() % grey)) & 0xff : (v >> 8) & 0xff);
      t[4 * i + 2] = (unsigned char)(grey ? (r + (rnd() % grey)) & 0xff : (v >> 16) & 0xff);
      if (alpha >= 0)
         t[4 * i + 3] = (unsigned char)alpha;
      else if (alpha == -1)
         t[4 * i + 3] = (unsigned char)rnd();
      else if (alpha == -2)
         t[4 * i + 3] = (rnd() & 3) ? 255 : 0;
      else
      {
         /* one draw after the other: C leaves the order of two calls in
          * one expression to the compiler */
         const unsigned level = rnd() % 3;
         const unsigned dip = rnd() % 3;
         t[4 * i + 3] = (unsigned char)(level * 128 - (dip == 2));
      }
   }
}

static void png(const char *name, int w, int h, int alpha, int grey)
{
   unsigned char *t = (unsigned char *)malloc((size_t)w * h * 4);
   texels(t, (unsigned)(w * h), alpha, grey);
   check(tximage_write_png(t, at(name), w, h, w * 4) != 0, name);
   free(t);
}

static void put32(unsigned char *p, unsigned v)
{
   p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
   p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}

/* a BMP of @bits (4, 8 or 24) a pixel, its rows random */
static void bmp(const char *name, int width, int height, int bits)
{
   unsigned char f[4096];
   const size_t row = (((size_t)width * bits + 31) / 32) * 4;
   const size_t off = 14 + 40 + (bits <= 8 ? (size_t)(4u << bits) : 0);
   size_t i, n = off + row * height;
   FILE *fp;
   memset(f, 0, off);
   f[0] = 'B'; f[1] = 'M';
   put32(f + 2, (unsigned)n);
   put32(f + 10, (unsigned)off);
   put32(f + 14, 40);
   put32(f + 18, (unsigned)width);
   put32(f + 22, (unsigned)height);
   f[26] = 1;
   f[28] = (unsigned char)bits;
   for (i = off; i < n; i++)
      f[i] = (unsigned char)rnd();
   fp = fopen(at(name), "wb");
   if (fp)
   {
      fwrite(f, 1, n, fp);
      fclose(fp);
   }
}

/* what the pack holds, written and removed */
static const char *const files[] = {
   IDENT "/" IDENT "#00000001#0#2_all.png",           /* alpha rounded, edges painted */
   IDENT "/" IDENT "#00000002#3#2_rgb.png",           /* merged with its _a */
   IDENT "/" IDENT "#00000002#3#2_a.png",
   IDENT "/" IDENT "#00000003#4#1_rgb.bmp",           /* no _a: opaque, Rice's I in alpha */
   IDENT "/sub/" IDENT "#00000004#2#1#0000ABCD_ciByRGBA.png", /* in a sub-directory, a palette CRC */
   IDENT "/" IDENT "#00000005#2#1_ci.bmp",            /* 8-bit indices */
   IDENT "/" IDENT "#00000006#0#3_all.png",           /* too wide: made smaller */
   IDENT "/" IDENT "#00000007#0#2_rgb.png",           /* its _a another size: passed by */
   IDENT "/" IDENT "#00000007#0#2_a.png",
   IDENT "/" IDENT "#00000008#0#2_all.png",           /* 1 x 3: passed by */
   IDENT "/OTHER#00000009#0#2_all.png",               /* another game's */
   IDENT "/" IDENT "#00000000#0#2_all.png",           /* no CRC */
   IDENT "/" IDENT "#0000000A#0#2_all.dds",           /* not read */
   IDENT "/" IDENT "#0000000B#0#2_all.txt",           /* not an image */
   IDENT "/.hidden#0000000C#0#2_all.png",
   IDENT "/" IDENT "#0000000C#0#2_all.png",           /* opaque */
   IDENT "/" IDENT "#0000000D#1#2_all.png",           /* 8-bit alpha */
   IDENT "/" IDENT "#0000000E#3#2_a.png",             /* an _a with no _rgb */
   IDENT "/" IDENT "#0000000F#2#0_ci.bmp",            /* 4-bit indices */
   IDENT "/" IDENT "#00000010#3#1_allciByRGBA.png",   /* 0, 128 or 255 alpha */
   IDENT "/" IDENT "#00000011#0#2_all.png",           /* tall: made smaller */
   IDENT "/" IDENT "#00000012#4#1_all.png"            /* fmt 4 with alpha */
};

static void write_pack(void)
{
   seed = 1;
   mkdir_(at(""));
   mkdir_(at(IDENT));
   mkdir_(at(IDENT "/sub"));
   png(files[0], 9, 7, -3, 0);
   png(files[1], 8, 8, 0x40, 0);
   png(files[2], 8, 8, 0x11, 0);
   bmp(files[3], 6, 4, 24);
   png(files[4], 5, 5, -2, 0);
   bmp(files[5], 8, 2, 8);
   png(files[6], 600, 40, 255, 0);
   png(files[7], 4, 4, 255, 0);
   png(files[8], 4, 2, 255, 0);
   png(files[9], 1, 3, 255, 0);
   png(files[10], 4, 4, 255, 0);
   png(files[11], 4, 4, 255, 0);
   png(files[12], 4, 4, 255, 0);
   png(files[13], 4, 4, 255, 0);
   png(files[14], 4, 4, 255, 0);
   png(files[15], 7, 5, 255, 0);
   png(files[16], 6, 6, -1, 0);
   png(files[17], 4, 4, 255, 0);
   bmp(files[18], 7, 3, 4);
   png(files[19], 6, 3, -3, 6);
   png(files[20], 10, 700, -2, 0);
   png(files[21], 5, 4, -1, 12);
}

static void remove_pack(void)
{
   unsigned i;
   for (i = 0; i < sizeof(files) / sizeof(files[0]); i++)
      remove(at(files[i]));
   remove(at("cache/" IDENT "_HIRESTEXTURES.htc"));
   rmdir_(at("cache"));
   rmdir_(at(IDENT "/sub"));
   rmdir_(at(IDENT));
   rmdir_(at(""));
}

static const uint64 keys[] = {
   1, 2, 3, ((uint64)0xABCD << 32) | 4, 4, 5, 6, 7, 8, 9, 0, 10, 11, 12, 13, 14, 15, 16, 17, 18,
   ((uint64)0x1234 << 32) | 5, ((uint64)0x1234 << 32) | 15
};

/* every key's texture as GLideNHQ hands it back, into @crc */
static uint32 textures(const char *what)
{
   static uint16 palette[256];
   uint32 crc = 0;
   unsigned i;
   for (i = 0; i < 256; i++)
      palette[i] = (uint16)(i * 0x0101 ^ 0x5a5a);
   for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
   {
      GHQTexInfo info;
      char line[200];
      boolean got;
      memset(&info, 0, sizeof(info));
      /* the palette's: converted to 16 bits and kept; once more without it, kept */
      got = txfilter_hirestex(0, keys[i], (keys[i] >> 32) == 0x1234 ? palette : NULL, &info);
      if (got && (keys[i] >> 32) == 0x1234)
      {
         memset(&info, 0, sizeof(info));
         got = txfilter_hirestex(0, keys[i], NULL, &info);
      }
      if (got)
      {
         unsigned bytes = info.format == 0x80E5 ? 1 : info.format == 0x8058 ? 4 : 2;
         sprintf(line, "%08x%08x %dx%d %04x %04x %04x %d %08x", (unsigned)(keys[i] >> 32), (unsigned)keys[i],
               info.width, info.height, (unsigned)info.format, info.texture_format, info.pixel_type, info.is_hires_tex,
               (unsigned)encoding_crc32(0, info.data, (size_t)info.width * info.height * bytes));
      }
      else
         sprintf(line, "%08x%08x none", (unsigned)(keys[i] >> 32), (unsigned)keys[i]);
      if (verbose)
         printf("%s: %s\n", what, line);
      crc = encoding_crc32(crc, (const uint8 *)line, strlen(line));
   }
   return crc;
}

static uint32 run(int maxbpp, int options, const char *what)
{
   uint32 crc;
   char pack[600], cache[600], name[200];
   sprintf(pack, "%s", at(""));
   sprintf(cache, "%s", at("cache"));
   if (!txfilter_init(256, 256, maxbpp, options, 0, cache, cache, pack, IDENT))
   {
      sprintf(name, "%s: txfilter_init", what);
      check(0, name);
      return 0;
   }
   crc = textures(what);
   if (!(options & DUMP_HIRESTEXCACHE))
   {
      uint32 again;
      sprintf(name, "%s: read again", what);
      check(txfilter_reloadhirestex() != 0, name);
      again = textures(name);
      sprintf(name, "%s: read again, the same", what);
      check(again == crc, name);
   }
   txfilter_shutdown();
   return crc;
}

/* one texture dumped, 5 x 3 in rows of 7 texels, as GLideNHQ names it; what is
 * read back must be its texels, made RGBA8 */
static void dumped(uint16 gfmt, int n64fmt, uint64 crc, const char *name, const char *what)
{
   static uint32 tex[7 * 3], want[7 * 3];
   char path[200], line[200];
   unsigned i;
   int w, h, fmt;
   uint8 *img;
   for (i = 0; i < 7 * 3; i++)
   {
      /* the low bits drawn first, as above */
      const uint32 lo = rnd();
      tex[i] = lo | (rnd() << 24);
   }
   if (gfmt == 0x8058)
      memcpy(want, tex, sizeof(want));
   else
      txquantize((uint8 *)tex, (uint8 *)want, 7, 3, TXQ_RGB5_A1, TXQ_RGBA8, 1, NULL, 1);
   sprintf(line, "%s: dumped", what);
   check(txfilter_dmptx((uint8 *)tex, 5, 3, 7, gfmt, (uint16)n64fmt, crc) != 0, line);
   sprintf(path, "dump/" IDENT "/GLideNHQ/" IDENT "%s", name);
   img = tximage_read_png(at(path), &w, &h, &fmt);
   sprintf(line, "%s: as %s", what, name);
   check(img != NULL && w == 5 && h == 3, line);
   if (img)
   {
      int ok = 1, y;
      for (y = 0; y < 3; y++)
         ok &= !memcmp(img + y * 20, want + y * 7, 20);
      sprintf(line, "%s: its texels", what);
      check(ok, line);
   }
   free(img);
   remove(at(path));
}

static void dumps(void)
{
   char cache[600], dump[600];
   sprintf(cache, "%s", at("cache"));
   sprintf(dump, "%s", at("dump"));
   check(txfilter_init(256, 256, 32, DUMP_TEX, 0, cache, dump, at(""), IDENT) != 0, "dumping: txfilter_init");
   dumped(0x8058, 0x002, ((uint64)0xABCD << 32) | 0x1234, "#00001234#0#2_all.png", "RGBA8, fmt 0");
   dumped(0x8057, 0x201, ((uint64)0xABCD << 32) | 0x5678, "#00005678#2#1#0000ABCD_ciByRGBA.png", "16 bits, fmt 2");
   txfilter_shutdown();
   rmdir_(at("dump/" IDENT "/GLideNHQ"));
   rmdir_(at("dump/" IDENT));
   rmdir_(at("dump"));
}

int main(int argc, char **argv)
{
   uint32 crc = 0, plain, forced, dumped;
   int i;
   FILE *f;

   for (i = 1; i < argc; i++)
      if (!strcmp(argv[i], "-v"))
         verbose = 1;
      else
         sprintf(dir, "%s", argv[i]);

   write_pack();

   plain = run(32, RICE_HIRESTEXTURES, "32 bits");
   crc ^= plain;
   forced = run(32, RICE_HIRESTEXTURES | FORCE16BPP_HIRESTEX, "16 bits forced");
   crc = crc * 31 + forced;
   crc = crc * 31 + run(16, RICE_HIRESTEXTURES, "16 bits at most");
   crc = crc * 31 + run(32, RICE_HIRESTEXTURES | FORCE16BPP_HIRESTEX | LET_TEXARTISTS_FLY, "texture artists flying");
   crc = crc * 31 + run(32, RICE_HIRESTEXTURES | LET_TEXARTISTS_FLY, "texture artists flying, 32 bits");
   check(run(32, RICE_HIRESTEXTURES | GZ_HIRESTEXCACHE, "kept compressed") == plain, "kept compressed: the same textures");

   /* kept on disk, then read from there: the same textures */
   dumped = run(32, RICE_HIRESTEXTURES | DUMP_HIRESTEXCACHE, "kept on disk");
   check(dumped == plain, "kept on disk: the same textures");
   f = fopen(at("cache/" IDENT "_HIRESTEXTURES.htc"), "rb");
   check(f != NULL, "kept on disk: the file written");
   if (f)
      fclose(f);
   /* one kept with other options is not read: the pack is, and kept */
   check(run(32, RICE_HIRESTEXTURES | FORCE16BPP_HIRESTEX | DUMP_HIRESTEXCACHE, "kept with 16 bits forced") == forced,
         "kept on disk, 16 bits forced: not what was kept with 32");
   check(run(32, RICE_HIRESTEXTURES | DUMP_HIRESTEXCACHE, "kept on disk again") == plain,
         "kept on disk again: not what was kept with 16 bits forced");
   remove(at(files[0]));
   check(run(32, RICE_HIRESTEXTURES | DUMP_HIRESTEXCACHE, "read from disk") == plain,
         "read from disk, not the pack: the same textures");

   dumps();
   remove_pack();

   if (crc != WANT)
   {
      printf("FAIL: the textures as read: %08x, want %08x\n", (unsigned)crc, WANT);
      failures++;
      /* each texture as read, to see which one differs */
      if (!verbose)
      {
         verbose = 1;
         write_pack();
         run(32, RICE_HIRESTEXTURES, "32 bits");
         remove_pack();
      }
   }
   printf("test_gliden64_txhirescache: %s\n", failures ? "FAIL" : "ok");
   return failures != 0;
}
