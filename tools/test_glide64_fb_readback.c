/* test_glide64_fb_readback -- Glide64's colour image copy to RDRAM,
 * converted as it is read.
 *
 * The copy used to read the whole window back, convert all of it to 565
 * in a window-sized buffer (flipping it), and then sample the N64 pixels
 * from that and convert them again. Now only the rect the N64 pixels fall
 * in is read, and each sampled pixel is converted once, straight from the
 * RGBA8 rows. This holds the RDRAM the copy writes to what the two-pass
 * copy wrote, at 1:1 and scaled, over window sizes, scales, offsets,
 * colour image sizes, 16- and 32-bit images and the transparent-black
 * rule, with the rows read back exactly as long as they are so a read
 * past them shows under the address sanitizer.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../glide2gl/src/Glide64/FBReadback.h"

/* the window, GL rows: the bottom row first */
static uint8_t *window;
static int win_w, win_h;

static void fill_window(unsigned seed)
{
   int i;
   for (i = 0; i < win_w * win_h * 4; i++)
   {
      seed = seed * 1103515245u + 12345u;
      window[i] = (uint8_t)(seed >> 16);
      /* black now and then, for the transparent-black rule */
      if ((seed >> 8) % 7 == 0)
         window[i] = 0;
   }
}

/* glReadPixels of the rect at upper-left (x, y), into its own buffer */
static uint8_t *read_rect(int x, int y, int w, int h)
{
   uint8_t *out = (uint8_t*)malloc((size_t)w * h * 4);
   int r;
   for (r = 0; r < h; r++)
   {
      int gl_row = (win_h - (y + h)) + r;
      memcpy(out + (size_t)r * w * 4, window + ((size_t)gl_row * win_w + x) * 4, (size_t)w * 4);
   }
   return out;
}

/* the old copy: the window in 565, upper-left origin */
static uint16_t *window_565(void)
{
   uint16_t *fb = (uint16_t*)malloc((size_t)win_w * win_h * 2);
   int i, j;
   for (j = 0; j < win_h; j++)
      for (i = 0; i < win_w; i++)
      {
         const uint8_t *p = window + ((size_t)j * win_w + i) * 4;
         fb[(win_h - j - 1) * win_w + i] = (uint16_t)(((p[0] >> 3) << 11) | ((p[1] >> 2) << 5) | (p[2] >> 3));
      }
   return fb;
}

static void old_direct(uint32_t ox, uint32_t oy, uint32_t width, uint32_t height,
      int read_alpha, int size16, uint8_t *dst)
{
   uint16_t *fb = window_565();
   uint32_t x, y;
   for (y = 0; y < height; y++)
      for (x = 0; x < width; x++)
      {
         uint16_t c = fb[(oy + y) * win_w + ox + x];
         if (read_alpha && c <= 0) {}
         else
            c = (c&0xFFC0) | ((c&0x001F) << 1) | 1;
         fb_store(dst, size16, width, x, y, c);
      }
   free(fb);
}

static void old_sampled(float scale_x, float scale_y, float offset_x, float offset_y,
      int x_start, int y_start, int x_end, int y_end, uint32_t width,
      int read_alpha, int size16, uint8_t *dst)
{
   uint16_t *fb = window_565();
   int x, y;
   for (y = y_start; y < y_end; y++)
      for (x = x_start; x < x_end; x++)
      {
         uint16_t c = fb[(int)(x*scale_x + offset_x) + (int)(y * scale_y + offset_y) * win_w];
         c = (c&0xFFC0) | ((c&0x001F) << 1) | 1;
         if (read_alpha && c == 1)
            c = 0;
         fb_store(dst, size16, width, (uint32_t)x, (uint32_t)y, c);
      }
   free(fb);
}

static int failures;

static void compare(const char *what, const uint8_t *a, const uint8_t *b, size_t n,
      int ww, int wh, uint32_t width, uint32_t height, int size16, int read_alpha)
{
   if (memcmp(a, b, n) != 0)
   {
      if (failures++ < 10)
         fprintf(stderr, "%s: window %dx%d image %ux%u %s alpha %d differs\n",
               what, ww, wh, width, height, size16 ? "16-bit" : "32-bit", read_alpha);
   }
}

int main(void)
{
   static const int wins[][2] = { { 320, 240 }, { 640, 480 }, { 1280, 960 }, { 960, 720 }, { 1600, 1200 } };
   static const uint32_t images[][2] = { { 320, 240 }, { 256, 224 }, { 640, 480 }, { 160, 120 }, { 32, 32 } };
   unsigned wi, ii, runs = 0;

   for (wi = 0; wi < sizeof(wins) / sizeof(wins[0]); wi++)
   {
      win_w = wins[wi][0];
      win_h = wins[wi][1];
      window = (uint8_t*)malloc((size_t)win_w * win_h * 4);
      fill_window(wi * 77 + 1);

      for (ii = 0; ii < sizeof(images) / sizeof(images[0]); ii++)
      {
         const uint32_t width = images[ii][0], height = images[ii][1];
         int size16, read_alpha, bar;
         for (size16 = 0; size16 <= 1; size16++)
            for (read_alpha = 0; read_alpha <= 1; read_alpha++)
            {
               const size_t bytes = (size_t)width * height * (size16 ? 2 : 4) + 4;
               uint8_t *a = (uint8_t*)malloc(bytes), *b = (uint8_t*)malloc(bytes);

               /* 1:1, at an offset that keeps the image inside the window */
               if (width <= (uint32_t)win_w && height <= (uint32_t)win_h)
               {
                  uint32_t ox = (win_w - width) / 2, oy = (win_h - height) / 3;
                  int32_t rx = (int32_t)ox, ry = (int32_t)oy, rw = (int32_t)width, rh = (int32_t)height;
                  uint8_t *rows = read_rect(rx, ry, rw, rh);
                  memset(a, 0x5A, bytes);
                  memset(b, 0x5A, bytes);
                  old_direct(ox, oy, width, height, read_alpha, size16, a);
                  fb_readback_direct(rows, (uint32_t)rw * 4, width, height, width, read_alpha, size16, b);
                  compare("1:1", a, b, bytes, win_w, win_h, width, height, size16, read_alpha);
                  free(rows);
                  ++runs;
               }

               /* scaled to fill the window, with or without the BAR rect */
               for (bar = 0; bar <= 1; bar++)
               {
                  float offset_x = (float)(win_w / 16), offset_y = (float)(win_h / 24);
                  float scale_x = (win_w - offset_x*2.0f) / width;
                  float scale_y = (win_h - offset_y*2.0f) / height;
                  int x_start = 0, y_start = 0, x_end = (int)width, y_end = (int)height;
                  int32_t sx0, sy0, rx, ry, rw, rh;
                  uint8_t *rows;
                  if (bar)
                  {
                     if (width < 240 || height < 86)
                        continue;
                     x_start = 80; y_start = 24; x_end = 240; y_end = 86;
                  }
                  sx0 = (int)(x_start*scale_x + offset_x);
                  sy0 = (int)(y_start * scale_y + offset_y);
                  rx = sx0; ry = sy0;
                  rw = (int)((x_end - 1)*scale_x + offset_x) - sx0 + 1;
                  rh = (int)((y_end - 1) * scale_y + offset_y) - sy0 + 1;
                  rows = read_rect(rx, ry, rw, rh);
                  memset(a, 0x5A, bytes);
                  memset(b, 0x5A, bytes);
                  old_sampled(scale_x, scale_y, offset_x, offset_y, x_start, y_start, x_end, y_end,
                        width, read_alpha, size16, a);
                  fb_readback_sampled(rows, (uint32_t)rw * 4, rx, ry, rw, rh, scale_x, scale_y,
                        offset_x, offset_y, x_start, y_start, x_end, y_end, width, read_alpha, size16, b);
                  compare(bar ? "scaled BAR" : "scaled", a, b, bytes, win_w, win_h, width, height, size16, read_alpha);
                  free(rows);
                  ++runs;
               }
               free(a);
               free(b);
            }
      }
      free(window);
   }

   if (failures)
   {
      fprintf(stderr, "test_glide64_fb_readback: %d failures\n", failures);
      return 1;
   }
   printf("test_glide64_fb_readback: %u runs ok\n", runs);
   return 0;
}
