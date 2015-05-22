/*
* Glide64 - Glide video plugin for Nintendo 64 emulators.
* Copyright (c) 2002  Dave2001
* Copyright (c) 2003-2009  Sergey 'Gonetz' Lipski
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

//****************************************************************
//
// Glide64 - Glide Plugin for Nintendo 64 emulators
// Project started on December 29th, 2001
//
// Authors:
// Dave2001, original author, founded the project in 2001, left it in 2002
// Gugaman, joined the project in 2002, left it in 2002
// Sergey 'Gonetz' Lipski, joined the project in 2002, main author since fall of 2002
// Hiroshi 'KoolSmoky' Morii, joined the project in 2007
//
//****************************************************************
//
// To modify Glide64:
// * Write your name and (optional)email, commented by your work, so I know who did it, and so that you can find which parts you modified when it comes time to send it to me.
// * Do NOT send me the whole project or file that you modified.  Take out your modified code sections, and tell me where to put them.  If people sent the whole thing, I would have many different versions, but no idea how to combine them all.
//
//****************************************************************

static INLINE void load16bRGBA(uint8_t *src, uint8_t *dst, int wid_64, int height, int line, int ext)
{
  uint32_t *src32 = (uint32_t*)src;
  uint32_t *dst32 = (uint32_t*)dst;
  unsigned odd = 0;

  while (height--)
  {
     int width = wid_64;

     while (width--)
     {
        uint32_t ab = m64p_swap32(src32[odd]);
        uint32_t cd = m64p_swap32(src32[!odd]);

        ALOWORD(ab) = ror16((uint16_t)ab, 1);
        ALOWORD(cd) = ror16((uint16_t)cd, 1);
        ab = ror32(ab, 16);
        cd = ror32(cd, 16);
        ALOWORD(ab) = ror16((uint16_t)ab, 1);
        ALOWORD(cd) = ror16((uint16_t)cd, 1);

        *dst32++ = ab;
        *dst32++ = cd;

        src32 += 2;
     }

     src32 = (uint32_t*)&src[(line + (uintptr_t)src32 - (uintptr_t)src) & 0xFFF];
     dst32 = (uint32_t*)((uint8_t*)dst32 + ext);

     odd ^= 1;
  }
}

static INLINE void load16bIA(uint8_t *src, uint8_t *dst, int wid_64, int height, int line, int ext)
{
   uint32_t *src32 = (uint32_t *)src;
   uint32_t *dst32 = (uint32_t *)dst;
   unsigned odd = 0;

   while (height--)
   {
      int width = wid_64;

      while (width--)
      {
         *dst32++ = src32[odd];
         *dst32++ = src32[!odd];

         src32 += 2;
      }

      src32 = (uint32_t*)((uint8_t*)src32 + line);
      dst32 = (uint32_t*)((uint8_t*)dst32 + ext);

      odd ^= 1;
   }
}

//****************************************************************
// Size: 2, Format: 0
//

uint32_t Load16bRGBA (uintptr_t dst, uintptr_t src, int wid_64, int height, int line, int real_width, int tile)
{
   int ext;
  if (wid_64 < 1)
     wid_64 = 1;
  if (height < 1)
     height = 1;
  ext = (real_width - (wid_64 << 2)) << 1;

  load16bRGBA((uint8_t *)src, (uint8_t *)dst, wid_64, height, line, ext);

  return (1 << 16) | GR_TEXFMT_ARGB_1555;
}

//****************************************************************
// Size: 2, Format: 3
//
// ** by Gugaman/Dave2001 **

uint32_t Load16bIA (uintptr_t dst, uintptr_t src, int wid_64, int height, int line, int real_width, int tile)
{
   int ext;
   if (wid_64 < 1)
      wid_64 = 1;
   if (height < 1)
      height = 1;
   ext = (real_width - (wid_64 << 2)) << 1;

   load16bIA((uint8_t *)src, (uint8_t *)dst, wid_64, height, line, ext);

   return (1 << 16) | GR_TEXFMT_ALPHA_INTENSITY_88;
}

//****************************************************************
// Size: 2, Format: 1
//

uint16_t yuv_to_rgb565(uint8_t y, uint8_t u, uint8_t v)
{
   float r = y + (1.370705f * (v-128));
   float g = y - (0.698001f * (v-128)) - (0.337633f * (u-128));
   float b = y + (1.732446f * (u-128));

   r *= 0.125f;
   g *= 0.25f;
   b *= 0.125f;
   //clipping the result
   if (r > 31) r = 31;
   if (g > 63) g = 63;
   if (b > 31) b = 31;
   if (r < 0) r = 0;
   if (g < 0) g = 0;
   if (b < 0) b = 0;
   return (uint16_t)(((uint16_t)(r) << 11) |
         ((uint16_t)(g) << 5) |
         (uint16_t)(b) );
   //*/
   /*
      const uint32_t c = y - 16;
      const uint32_t d = u - 128;
      const uint32_t e = v - 128;

      uint32_t r =  (298 * c           + 409 * e + 128) & 0xf800;
      uint32_t g = ((298 * c - 100 * d - 208 * e + 128) >> 5) & 0x7e0;
      uint32_t b = ((298 * c + 516 * d           + 128) >> 11) & 0x1f;

      WORD texel = (WORD)(r | g | b);

      return texel;
      */
}

//****************************************************************
// Size: 2, Format: 1
//

uint32_t Load16bYUV (uintptr_t dst, uintptr_t src,
      int wid_64, int height, int line, int real_width, int tile)
{
   uint16_t i;
   uint32_t *mb = (uint32_t*)(gfx_info.RDRAM+rdp.addr[g_gdp.tile[tile].tmem]); //pointer to the macro block
   uint16_t *tex = (uint16_t*)dst;

   for (i = 0; i < 128; i++)
   {
      uint32_t  t = mb[i]; //each uint32_t contains 2 pixels
      uint8_t  y1 = (uint8_t)t&0xFF;
      uint8_t  v  = (uint8_t)(t>>8)&0xFF;
      uint8_t  y0 = (uint8_t)(t>>16)&0xFF;
      uint8_t  u  = (uint8_t)(t>>24)&0xFF;
      uint16_t c = yuv_to_rgb565(y0, u, v);

      *(tex++) = c;
      c = yuv_to_rgb565(y1, u, v);
      *(tex++) = c;
   }
   return (1 << 16) | GR_TEXFMT_RGB_565;
}
