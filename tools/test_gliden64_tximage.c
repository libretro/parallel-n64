/* test_gliden64_tximage -- GLideNHQ's hi-res texture files, read and written.
 *
 * TxImage (PNG through rpng, uncompressed Windows BMP by hand) went from a
 * C++ class to C89 functions. This writes a PNG with padded rows and reads
 * it back texel for texel; builds BMPs of 4, 8, 24 and 32 bits a pixel
 * and holds what is read to what GLideNHQ has always made of them (rows
 * turned top-down, 4-bit indices stuffed into bytes, the width the padded
 * row's, 24 bits copied into RGBA with a full alpha); and holds files it
 * cannot take (compressed, top-down, short, not a BMP, none there) to
 * nothing read.
 *
 *   ./test_gliden64_tximage [dir, /tmp by default: its files are removed after]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "GLideNHQ/TxImage.h"

static int failures;
static char dir[512] = "/tmp";

static void check(int ok, const char *what)
{
   if (!ok)
   {
      printf("FAIL: %s\n", what);
      failures++;
   }
}

static const char *at(const char *name)
{
   static char path[600];
   sprintf(path, "%s/%s", dir, name);
   return path;
}

static void put32(unsigned char *p, unsigned v)
{
   p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
   p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}

/* a BMP: the two headers and @rows rows of @row_bytes, bottom row first */
static size_t bmp(unsigned char *f, int width, int height, int bits, unsigned compression,
      const unsigned char *rows, size_t row_bytes, unsigned nrows)
{
   const size_t off = 14 + 40 + (bits <= 8 ? (size_t)(4u << bits) : 0);
   memset(f, 0, off);
   f[0] = 'B'; f[1] = 'M';
   put32(f + 2, (unsigned)(off + row_bytes * nrows));
   put32(f + 10, (unsigned)off);
   put32(f + 14, 40);
   put32(f + 18, (unsigned)width);
   put32(f + 22, (unsigned)height);
   f[26] = 1;
   f[28] = (unsigned char)bits;
   put32(f + 30, compression);
   memcpy(f + off, rows, row_bytes * nrows);
   return off + row_bytes * nrows;
}

static void save(const char *name, const unsigned char *b, size_t n)
{
   FILE *f = fopen(at(name), "wb");
   if (f)
   {
      fwrite(b, 1, n, f);
      fclose(f);
   }
}

static uint8 *read_bmp(const char *name, int *w, int *h, int *fmt)
{
   *w = *h = -1;
   *fmt = -1;
   return tximage_read_bmp(at(name), w, h, fmt);
}

static void none_read(const char *name, const char *what)
{
   int w, h, fmt;
   uint8 *img = read_bmp(name, &w, &h, &fmt);
   check(!img && w == 0 && h == 0 && fmt == TXI_NONE, what);
   free(img);
}

int main(int argc, char **argv)
{
   static unsigned char file[1 << 16];
   int w, h, fmt;
   uint8 *img;
   unsigned i;

   if (argc > 1)
      sprintf(dir, "%s", argv[1]);

   /* PNG: 7 x 5 RGBA8 rows 36 bytes apart, back as 7 x 5 */
   {
      unsigned char src[36 * 5];
      int ok = 1, x, y;
      for (i = 0; i < sizeof(src); i++)
         src[i] = (unsigned char)(i * 37 + (i >> 3));
      check(tximage_write_png(src, at("tximage.png"), 7, 5, 36) != 0, "a PNG written");
      img = tximage_read_png(at("tximage.png"), &w, &h, &fmt);
      check(img && w == 7 && h == 5 && fmt == TXI_RGBA8, "the PNG read back as 7 x 5 RGBA8");
      if (img)
         for (y = 0; y < 5; y++)
            for (x = 0; x < 7 * 4; x++)
               ok &= img[y * 28 + x] == src[y * 36 + x];
      check(img && ok, "the PNG's texels as written");
      free(img);
      img = tximage_read_png(at("tximage-none.png"), &w, &h, &fmt);
      check(!img && w == 0 && h == 0 && fmt == TXI_NONE, "no PNG there, nothing read");
   }

   /* 24 bits, 3 x 2: rows of 9 bytes padded to 12, bottom first */
   {
      unsigned char rows[24];
      static const unsigned char want[24] = {
         21, 22, 23, 255, 24, 25, 26, 255, 27, 28, 29, 255,
         1, 2, 3, 255, 4, 5, 6, 255, 7, 8, 9, 255 };
      memset(rows, 0xee, sizeof(rows));
      for (i = 0; i < 9; i++)
      {
         rows[i] = (unsigned char)(1 + i);
         rows[12 + i] = (unsigned char)(21 + i);
      }
      save("tximage24.bmp", file, bmp(file, 3, 2, 24, 0, rows, 12, 2));
      img = read_bmp("tximage24.bmp", &w, &h, &fmt);
      check(img && w == 3 && h == 2 && fmt == TXI_RGBA8 && !memcmp(img, want, 24),
            "24 bits: top row first, the bytes as they are, alpha full");
      free(img);
   }

   /* 32 bits, 2 x 2: the rows copied, top first */
   {
      unsigned char rows[16];
      for (i = 0; i < 16; i++)
         rows[i] = (unsigned char)(100 + i);
      save("tximage32.bmp", file, bmp(file, 2, 2, 32, 0, rows, 8, 2));
      img = read_bmp("tximage32.bmp", &w, &h, &fmt);
      check(img && w == 2 && h == 2 && fmt == TXI_RGBA8 && !memcmp(img, rows + 8, 8) && !memcmp(img + 8, rows, 8),
            "32 bits: the rows turned");
      free(img);
   }

   /* 8 bits, 5 x 2: rows padded to 8, the width read is the row's */
   {
      unsigned char rows[16];
      for (i = 0; i < 16; i++)
         rows[i] = (unsigned char)(i * 3);
      save("tximage8.bmp", file, bmp(file, 5, 2, 8, 0, rows, 8, 2));
      img = read_bmp("tximage8.bmp", &w, &h, &fmt);
      check(img && w == 8 && h == 2 && fmt == TXI_COLOR_INDEX8 && !memcmp(img, rows + 8, 8) && !memcmp(img + 8, rows, 8),
            "8 bits: indices, the rows turned, as wide as the padded row");
      free(img);
   }

   /* 4 bits, 3 x 2: a 4-byte row, each byte two indices, low nibble first */
   {
      static const unsigned char rows[8] = { 0x21, 0x43, 0x65, 0x87, 0xa9, 0xcb, 0xed, 0x0f };
      static const unsigned char want[16] = {
         9, 10, 11, 12, 13, 14, 15, 0, 1, 2, 3, 4, 5, 6, 7, 8 };
      save("tximage4.bmp", file, bmp(file, 3, 2, 4, 0, rows, 4, 2));
      img = read_bmp("tximage4.bmp", &w, &h, &fmt);
      check(img && w == 8 && h == 2 && fmt == TXI_COLOR_INDEX8 && !memcmp(img, want, 16),
            "4 bits: each nibble a byte, low first, the rows turned");
      free(img);
   }

   /* files it does not take */
   {
      unsigned char rows[24];
      size_t n;
      memset(rows, 1, sizeof(rows));
      save("tximage-rle.bmp", file, bmp(file, 3, 2, 24, 1, rows, 12, 2));
      none_read("tximage-rle.bmp", "a compressed BMP not read");
      n = bmp(file, 3, 2, 24, 0, rows, 12, 2);
      put32(file + 22, (unsigned)-2);
      save("tximage-topdown.bmp", file, n);
      none_read("tximage-topdown.bmp", "a top-down BMP not read");
      n = bmp(file, 3, 2, 24, 0, rows, 12, 2);
      save("tximage-short.bmp", file, n - 1);
      none_read("tximage-short.bmp", "a BMP a byte short not read");
      n = bmp(file, 3, 2, 16, 0, rows, 8, 2);
      save("tximage16.bmp", file, n);
      none_read("tximage16.bmp", "a 16-bit BMP not read");
      n = bmp(file, 3, 2, 24, 0, rows, 12, 2);
      file[0] = 'X';
      save("tximage-notbm.bmp", file, n);
      none_read("tximage-notbm.bmp", "a file not a BMP not read");
      none_read("tximage-none.bmp", "no BMP there, nothing read");
   }

   {
      static const char *const made[] = { "tximage.png", "tximage24.bmp", "tximage32.bmp", "tximage8.bmp",
         "tximage4.bmp", "tximage-rle.bmp", "tximage-topdown.bmp", "tximage-short.bmp", "tximage16.bmp",
         "tximage-notbm.bmp" };
      for (i = 0; i < sizeof(made) / sizeof(made[0]); i++)
         remove(at(made[i]));
   }

   printf("test_gliden64_tximage: %s\n", failures ? "FAIL" : "ok");
   return failures != 0;
}
