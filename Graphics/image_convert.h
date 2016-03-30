#ifndef _IMAGE_CONVERT_H
#define _IMAGE_CONVERT_H

#include <stdint.h>
#include <retro_inline.h>

#ifdef __cplusplus
extern "C" {
#endif

union RGBA {
   struct {
      uint8_t r, g, b, a;
   };
   uint32_t raw;
};

static INLINE uint16_t YUVtoRGBA16(uint8_t y, uint8_t u, uint8_t v)
{
   float r  = y + (1.370705f * (v-128));
   float g  = y - (0.698001f * (v-128)) - (0.337633f * (u-128));
   float b  = y + (1.732446f * (u-128));
   r       *= 0.125f;
   g       *= 0.125f;
   b       *= 0.125f;

   /* clipping the result */
   if (r > 32)
      r = 32;
   if (g > 32)
      g = 32;
   if (b > 32)
      b = 32;
   if (r < 0)
      r = 0;
   if (g < 0)
      g = 0;
   if (b < 0)
      b = 0;

   return (uint16_t)(((uint16_t)(r) << 11) | ((uint16_t)(g) << 6) | ((uint16_t)(b) << 1) | 1);
}

static uint16_t YUVtoRGB565(uint8_t y, uint8_t u, uint8_t v)
{
   float r = y + (1.370705f * (v-128));
   float g = y - (0.698001f * (v-128)) - (0.337633f * (u-128));
   float b = y + (1.732446f * (u-128));

   r *= 0.125f;
   g *= 0.25f;
   b *= 0.125f;

   /* clipping the result */
   if (r > 31) r = 31;
   if (g > 63) g = 63;
   if (b > 31) b = 31;
   if (r < 0) r = 0;
   if (g < 0) g = 0;
   if (b < 0) b = 0;
   return(uint16_t)(((uint16_t)(r) << 11) |
         ((uint16_t)(g) << 5) |
         (uint16_t)(b) );
}

static INLINE uint16_t RGBA32toRGBA16(uint32_t _c)
{
   union RGBA c;
   c.raw = _c;
   return ((c.r >> 3) << 11) | ((c.g >> 3) << 6) | ((c.b >> 3) << 1) | (c.a == 0 ? 0 : 1);
}

static INLINE uint8_t RGBA8toR8(uint8_t _c)
{
   return _c;
}

static INLINE uint32_t RGBA16toRGBA32(uint32_t _c)
{
   union RGBA c;
   c.raw = _c;
   return (c.r << 24) | (c.g << 16) | (c.b << 8) | c.a;
}

#ifdef __cplusplus
}
#endif

#endif
