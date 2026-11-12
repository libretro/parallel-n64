/* Glide64's texture modifications: the parts of an N64 colour combine a
 * Glide combiner cannot do, worked into the texture itself.
 *
 * They ran on ARGB 4444: every texture a combine touched this way was cut
 * to four bits a channel first, a 32-bit texture included, and came out
 * banded. They run on ARGB 8888 now, and the texture is uploaded as that.
 */
#include <stdlib.h>

#include "TexMod.h"

#define TEX_A(c) ((c) >> 24)
#define TEX_R(c) (((c) >> 16) & 0xFF)
#define TEX_G(c) (((c) >> 8) & 0xFF)
#define TEX_B(c) ((c) & 0xFF)

static uint32_t argb(uint32_t a, uint32_t r, uint32_t g, uint32_t b)
{
   return (a << 24) | (r << 16) | (g << 8) | b;
}

/* to the nearest of 0..255 */
static uint32_t to8(float v)
{
   if (v <= 0.0f)
      return 0;
   if (v >= 255.0f)
      return 255;
   return (uint32_t)(v + 0.5f);
}

int glide64_tex_to_8888(unsigned texfmt, const void *src, uint32_t *dst, int count)
{
   const uint8_t  *s8  = (const uint8_t*)src;
   const uint16_t *s16 = (const uint16_t*)src;
   int i;

   switch (texfmt)
   {
      case GR_TEXFMT_ARGB_8888:
         if (dst != (const uint32_t*)src)
            for (i = 0; i < count; i++)
               dst[i] = ((const uint32_t*)src)[i];
         return 1;
      case GR_TEXFMT_ARGB_1555:
         for (i = 0; i < count; i++)
         {
            const uint32_t c = s16[i];
            const uint32_t r = (c >> 10) & 0x1F, g = (c >> 5) & 0x1F, b = c & 0x1F;
            dst[i] = argb((c & 0x8000) ? 255u : 0u, (r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2));
         }
         return 1;
      case GR_TEXFMT_RGB_565:
         for (i = 0; i < count; i++)
         {
            const uint32_t c = s16[i];
            const uint32_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
            dst[i] = argb(255u, (r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2));
         }
         return 1;
      case GR_TEXFMT_ARGB_4444:
         for (i = 0; i < count; i++)
         {
            const uint32_t c = s16[i];
            dst[i] = argb((c >> 12) * 17u, ((c >> 8) & 0xF) * 17u, ((c >> 4) & 0xF) * 17u, (c & 0xF) * 17u);
         }
         return 1;
      case GR_TEXFMT_ALPHA_INTENSITY_88:
         for (i = 0; i < count; i++)
         {
            const uint32_t c = s16[i];
            const uint32_t in = c & 0xFF;
            dst[i] = argb(c >> 8, in, in, in);
         }
         return 1;
      case GR_TEXFMT_ALPHA_INTENSITY_44:
         for (i = 0; i < count; i++)
         {
            const uint32_t c = s8[i];
            const uint32_t in = (c & 0xF) * 17u;
            dst[i] = argb((c >> 4) * 17u, in, in, in);
         }
         return 1;
      case GR_TEXFMT_ALPHA_8:
         for (i = 0; i < count; i++)
         {
            const uint32_t c = s8[i];
            dst[i] = argb(c, c, c, c);
         }
         return 1;
   }
   return 0;
}

void glide64_tex_mod_8888(uint32_t *tex, int count, unsigned mod,
      uint32_t modcolor, uint32_t modcolor1, uint32_t modcolor2, uint32_t modfactor)
{
   const float c0[3] = { (float)((modcolor >> 24) & 0xFF), (float)((modcolor >> 16) & 0xFF), (float)((modcolor >> 8) & 0xFF) };
   const float c1[3] = { (float)((modcolor1 >> 24) & 0xFF), (float)((modcolor1 >> 16) & 0xFF), (float)((modcolor1 >> 8) & 0xFF) };
   const float c2[3] = { (float)((modcolor2 >> 24) & 0xFF), (float)((modcolor2 >> 16) & 0xFF), (float)((modcolor2 >> 8) & 0xFF) };
   const float ca0 = (float)(modcolor & 0xFF);
   const float fac = (float)(modfactor & 0xFF) / 255.0f;
   int i, k;

   for (i = 0; i < count; i++)
   {
      const uint32_t c = tex[i];
      float t[3];
      float out[3];
      float a  = (float)TEX_A(c);
      float ta = a / 255.0f;

      t[0] = (float)TEX_R(c);
      t[1] = (float)TEX_G(c);
      t[2] = (float)TEX_B(c);

      for (k = 0; k < 3; k++)
      {
         const float tk = t[k] / 255.0f; /* the texel as a factor */
         switch (mod)
         {
            case TMOD_TEX_INTER_COLOR_USING_FACTOR:
               out[k] = (1.0f - fac) * t[k] + fac * c0[k];
               break;
            case TMOD_TEX_INTER_COL_USING_COL1:
               out[k] = (1.0f - c1[k] / 255.0f) * t[k] + c1[k] / 255.0f * c0[k];
               break;
            case TMOD_FULL_COLOR_SUB_TEX:
               out[k] = c0[k] - t[k];
               break;
            case TMOD_COL_INTER_COL1_USING_TEX:
               out[k] = (1.0f - tk) * c0[k] + tk * c1[k];
               break;
            case TMOD_COL_INTER_COL1_USING_TEXA:
               out[k] = (1.0f - ta) * c0[k] + ta * c1[k];
               break;
            case TMOD_COL_INTER_COL1_USING_TEXA__MUL_TEX:
               out[k] = ((1.0f - ta) * c0[k] + ta * c1[k]) * tk;
               break;
            case TMOD_COL_INTER_TEX_USING_TEXA:
               out[k] = (1.0f - ta) * c0[k] + ta * t[k];
               break;
            case TMOD_COL_INTER_TEX_USING_TEX:
               out[k] = (1.0f - tk) * c0[k] + tk * t[k];
               break;
            case TMOD_COL2_INTER__COL_INTER_COL1_USING_TEX__USING_TEXA:
               out[k] = ((1.0f - tk) * c0[k] + tk * c1[k]) * ta + c2[k] * (1.0f - ta);
               break;
            case TMOD_TEX_SUB_COL_MUL_FAC_ADD_TEX:
               out[k] = (t[k] - c0[k]) * fac + t[k];
               break;
            case TMOD_TEX_SCALE_COL_ADD_COL:
               out[k] = tk * c0[k] + c1[k];
               break;
            case TMOD_TEX_ADD_COL:
               out[k] = t[k] + c0[k];
               break;
            case TMOD_TEX_SUB_COL:
               out[k] = t[k] - c0[k];
               break;
            case TMOD_TEX_SUB_COL_MUL_FAC:
               out[k] = (t[k] - c0[k]) * fac;
               break;
            case TMOD_COL_INTER_TEX_USING_COL1:
               out[k] = c1[k] / 255.0f * t[k] + (1.0f - c1[k] / 255.0f) * c0[k];
               break;
            case TMOD_COL_MUL_TEXA_ADD_TEX:
               out[k] = c0[k] * ta + t[k];
               break;
            case TMOD_TEX_INTER_NOISE_USING_COL:
               /* (one noise value a texel, drawn below) */
               out[k] = t[k];
               break;
            case TMOD_TEX_INTER_COL_USING_TEXA:
               out[k] = ta * c0[k] + (1.0f - ta) * t[k];
               break;
            case TMOD_TEX_MUL_COL:
               out[k] = c0[k] * tk;
               break;
            case TMOD_TEX_SCALE_FAC_ADD_COL:
               out[k] = c0[k] + fac * t[k];
               break;
            default: /* alpha only, or no modification */
               out[k] = t[k];
               break;
         }
      }

      switch (mod)
      {
         case TMOD_FULL_COLOR_SUB_TEX:
            a = ca0 - a;
            break;
         case TMOD_TEX_SCALE_FAC_ADD_FAC:
            a = (1.0f - fac) * 255.0f + fac * a;
            break;
         case TMOD_TEX_INTER_NOISE_USING_COL:
            {
               const float noise = (float)(rand() & 0xFF);
               for (k = 0; k < 3; k++)
                  out[k] = (1.0f - c0[k] / 255.0f) * t[k] + c0[k] / 255.0f * noise;
            }
            break;
      }

      tex[i] = argb(to8(a), to8(out[0]), to8(out[1]), to8(out[2]));
   }
}
