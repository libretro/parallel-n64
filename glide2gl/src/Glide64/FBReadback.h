#ifndef GLIDE64_FB_READBACK_H
#define GLIDE64_FB_READBACK_H

/* The colour image copied back to RDRAM from what the window shows: each
 * pixel read back is turned into RDRAM's format as it is read, with no
 * window-sized copy in between. The rows are RGBA8, stride bytes apart,
 * the bottom row first, as glReadPixels leaves them. */

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#if !defined(MSB_FIRST) && (defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2))
#define FB_READBACK_SSE2 1
#include <emmintrin.h>
#endif
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

/* An RGBA8 pixel as the colour image's 5551: read as one word, the
 * channels' top five bits moved into place with three shifts and masks */
static INLINE uint16_t fb_5551(const uint8_t *p)
{
   uint32_t v;
   memcpy(&v, p, 4);
#ifdef MSB_FIRST
   return (uint16_t)(((v >> 16) & 0xF800) | ((v >> 13) & 0x07C0) | ((v >> 10) & 0x003E) | 1);
#else
   return (uint16_t)(((v << 8) & 0xF800) | ((v >> 5) & 0x07C0) | ((v >> 18) & 0x003E) | 1);
#endif
}
#define FB_5551(p) fb_5551(p)

/* @n pixels from @p to the 16-bit colour image's halfwords from linear index
 * @i on (each at i ^ 1, as fb_store has them): two a 32-bit store where
 * they share a word, no test per pixel. With @read_alpha black (5551 1)
 * is kept transparent (0). */
static INLINE void fb_row16(const uint8_t *p, uint16_t *d, uint32_t i, int n, int read_alpha)
{
   /* the one value turned to 0: a 5551 colour is never 0 */
   const uint16_t keep = read_alpha ? 1 : 0;
   uint16_t c, c1;
   if (((uintptr_t)d & 3) != 0)
   {
      /* not on a word: one at a time */
      for (; n > 0; n--, i++, p += 4)
      {
         c = FB_5551(p);
         d[i ^ 1] = (uint16_t)(c == keep ? 0 : c);
      }
      return;
   }
   if ((i & 1) && n > 0)
   {
      c = FB_5551(p);
      d[i ^ 1] = (uint16_t)(c == keep ? 0 : c);
      i++; p += 4; n--;
   }
#ifdef FB_READBACK_SSE2
   {
      /* eight a time: the same shifts and masks on four words a register,
       * the two packed to halfwords (biased: the pack saturates signed),
       * the kept value zeroed, each pair's halfwords swapped */
      const __m128i r_mask = _mm_set1_epi32(0xF800), g_mask = _mm_set1_epi32(0x07C0);
      const __m128i b_mask = _mm_set1_epi32(0x003E), one = _mm_set1_epi32(1);
      const __m128i bias32 = _mm_set1_epi32(0x8000), bias16 = _mm_set1_epi16((short)0x8000);
      const __m128i kept = _mm_set1_epi16((short)keep);
      for (; n >= 8; n -= 8, i += 8, p += 32)
      {
         __m128i v0 = _mm_loadu_si128((const __m128i*)p);
         __m128i v1 = _mm_loadu_si128((const __m128i*)(p + 16));
         __m128i c0 = _mm_or_si128(_mm_or_si128(_mm_and_si128(_mm_slli_epi32(v0, 8), r_mask),
                  _mm_and_si128(_mm_srli_epi32(v0, 5), g_mask)),
               _mm_or_si128(_mm_and_si128(_mm_srli_epi32(v0, 18), b_mask), one));
         __m128i c1 = _mm_or_si128(_mm_or_si128(_mm_and_si128(_mm_slli_epi32(v1, 8), r_mask),
                  _mm_and_si128(_mm_srli_epi32(v1, 5), g_mask)),
               _mm_or_si128(_mm_and_si128(_mm_srli_epi32(v1, 18), b_mask), one));
         __m128i h = _mm_add_epi16(_mm_packs_epi32(_mm_sub_epi32(c0, bias32), _mm_sub_epi32(c1, bias32)), bias16);
         h = _mm_andnot_si128(_mm_cmpeq_epi16(h, kept), h);
         h = _mm_shufflelo_epi16(h, _MM_SHUFFLE(2, 3, 0, 1));
         h = _mm_shufflehi_epi16(h, _MM_SHUFFLE(2, 3, 0, 1));
         _mm_storeu_si128((__m128i*)(d + i), h);
      }
   }
#endif
   for (; n >= 2; n -= 2, i += 2, p += 8)
   {
      c = FB_5551(p);
      c1 = FB_5551(p + 4);
      c = (uint16_t)(c == keep ? 0 : c);
      c1 = (uint16_t)(c1 == keep ? 0 : c1);
      /* pixel i at halfword i + 1, pixel i + 1 at halfword i */
#ifdef MSB_FIRST
      *(uint32_t*)(d + i) = ((uint32_t)c1 << 16) | c;
#else
      *(uint32_t*)(d + i) = ((uint32_t)c << 16) | c1;
#endif
   }
   if (n > 0)
   {
      c = FB_5551(p);
      d[i ^ 1] = (uint16_t)(c == keep ? 0 : c);
   }
}

/* Scaled on the GPU first: N64 pixel (x, y) is pixel (x, y) of the rh
 * rows read back, the bottom row first. */
static INLINE void fb_readback_scaled(const uint8_t *rows, uint32_t stride, uint32_t rh,
      int x_start, int y_start, int x_end, int y_end,
      uint32_t width, int read_alpha, int size16, uint8_t *dst)
{
   int y, x;
   if (size16 && x_end > x_start)
   {
      for (y = y_start; y < y_end; y++)
         fb_row16(rows + (size_t)(rh - 1 - (uint32_t)y) * stride + (size_t)x_start * 4,
               (uint16_t*)dst, (uint32_t)x_start + (uint32_t)y * width, x_end - x_start, read_alpha);
      return;
   }
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
