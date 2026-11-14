#ifndef GLIDE64_FB_READBACK_H
#define GLIDE64_FB_READBACK_H

/* The colour image copied back to RDRAM from what the window shows: each
 * pixel read back is turned into RDRAM's format as it is read, with no
 * window-sized copy in between. The rows are RGBA8, stride bytes apart,
 * the bottom row first, as glReadPixels leaves them. */

#include <stddef.h>
#include <stdint.h>
#include "../../../Graphics/image_convert.h"

static INLINE void fb_store(uint8_t *dst, int size16, uint32_t width,
      uint32_t x, uint32_t y, uint16_t c)
{
   if (size16)
      ((uint16_t*)dst)[(x + y * width)^1] = c;
   else
      ((uint32_t*)dst)[x + y * width] = RGBA16toRGBA32(c);
}

/* At 1:1: the w x h rows, as 565 and then the colour image's 5551, black
 * kept transparent with read_alpha. */
static INLINE void fb_readback_direct(const uint8_t *rows, uint32_t stride,
      uint32_t w, uint32_t h, uint32_t width, int read_alpha, int size16,
      uint8_t *dst)
{
   uint32_t y, x;
   for (y = 0; y < h; y++)
   {
      const uint8_t *p = rows + (size_t)(h - 1 - y) * stride;
      for (x = 0; x < w; x++, p += 4)
      {
         uint16_t c = (uint16_t)(((p[0] >> 3) << 11) | ((p[1] >> 2) << 5) | (p[2] >> 3));
         if (read_alpha && c == 0) {}
         else
            c = (c&0xFFC0) | ((c&0x001F) << 1) | 1;
         fb_store(dst, size16, width, x, y, c);
      }
   }
}

/* Scaled: the window pixel each N64 pixel of [x_start, x_end) x [y_start,
 * y_end) falls on, from the rect (rx, ry, rw, rh) read back. */
static INLINE void fb_readback_sampled(const uint8_t *rows, uint32_t stride,
      int32_t rx, int32_t ry, int32_t rw, int32_t rh,
      float scale_x, float scale_y, float offset_x, float offset_y,
      int x_start, int y_start, int x_end, int y_end,
      uint32_t width, int read_alpha, int size16, uint8_t *dst)
{
   int y, x;
   for (y = y_start; y < y_end; y++)
   {
      int sy = (int)(y * scale_y + offset_y) - ry;
      const uint8_t *row;
      if (sy < 0)
         sy = 0;
      else if (sy >= rh)
         sy = rh - 1;
      row = rows + (size_t)(rh - 1 - sy) * stride;
      for (x = x_start; x < x_end; x++)
      {
         int sx = (int)(x*scale_x + offset_x) - rx;
         const uint8_t *p;
         uint16_t c;
         if (sx < 0)
            sx = 0;
         else if (sx >= rw)
            sx = rw - 1;
         p = row + (size_t)sx * 4;
         c = (uint16_t)(((p[0] >> 3) << 11) | ((p[1] >> 3) << 6) | ((p[2] >> 3) << 1) | 1);
         if (read_alpha && c == 1)
            c = 0;
         fb_store(dst, size16, width, (uint32_t)x, (uint32_t)y, c);
      }
   }
}

/* Scaled on the GPU first: N64 pixel (x, y) is pixel (x, y) of the rh
 * rows read back, the bottom row first. */
static INLINE void fb_readback_scaled(const uint8_t *rows, uint32_t stride, uint32_t rh,
      int x_start, int y_start, int x_end, int y_end,
      uint32_t width, int read_alpha, int size16, uint8_t *dst)
{
   int y, x;
   for (y = y_start; y < y_end; y++)
   {
      const uint8_t *row = rows + (size_t)(rh - 1 - (uint32_t)y) * stride;
      for (x = x_start; x < x_end; x++)
      {
         const uint8_t *p = row + (size_t)x * 4;
         uint16_t c = (uint16_t)(((p[0] >> 3) << 11) | ((p[1] >> 3) << 6) | ((p[2] >> 3) << 1) | 1);
         if (read_alpha && c == 1)
            c = 0;
         fb_store(dst, size16, width, (uint32_t)x, (uint32_t)y, c);
      }
   }
}

#endif
