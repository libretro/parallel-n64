#ifndef _IMAGE_CONVERT_H
#define _IMAGE_CONVERT_H

#include <stdint.h>
#include <retro_inline.h>

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif

#endif
