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

#include <stdint.h>

static INLINE void load4bCI(uint8_t *src, uint8_t *dst, int wid_64, int height, uint16_t line, int ext, uint16_t *pal)
{
  uint32_t *src32 = (uint32_t*)src;
  uint32_t *dst32 = (uint32_t*)dst;
  unsigned odd = 0;

  while (height--)
  {
    int width = wid_64;

    while (width--)
    {
      uint32_t v12 = m64p_swap32(src32[odd]);
      uint32_t v16 = m64p_swap32(src32[!odd]);
      uint32_t pix = width + 1;

      ALOWORD(pix) = ror16(*(uint16_t*)((uint8_t*)pal + ((v12 >> 23) & 0x1E)), 1);
      pix = pix << 16;
      ALOWORD(pix) = ror16(*(uint16_t*)((uint8_t*)pal + ((v12 >> 27) & 0x1E)), 1);
      *dst32++ = pix;

      ALOWORD(pix) = ror16(*(uint16_t*)((uint8_t*)pal + ((v12 >> 15) & 0x1E)), 1);
      pix <<= 16;
      ALOWORD(pix) = ror16(*(uint16_t*)((uint8_t*)pal + ((v12 >> 19) & 0x1E)), 1);
      *dst32++ = pix;

      ALOWORD(pix) = ror16(*(uint16_t*)((uint8_t*)pal + ((v12 >> 7) & 0x1E)), 1);
      pix <<= 16;
      ALOWORD(pix) = ror16(*(uint16_t*)((uint8_t*)pal + ((v12 >> 11) & 0x1E)), 1);
      *dst32++ = pix;

      ALOWORD(pix) = ror16(*(uint16_t*)((uint8_t*)pal + (2 * (uint8_t)v12 & 0x1E)), 1);
      pix <<= 16;
      ALOWORD(pix) = ror16(*(uint16_t*)((uint8_t*)pal + ((v12 >> 3) & 0x1E)), 1);
      *dst32++ = pix;

      ALOWORD(pix) = ror16(*(uint16_t*)((uint8_t*)pal + ((v16 >> 23) & 0x1E)), 1);
      pix <<= 16;
      ALOWORD(pix) = ror16(*(uint16_t*)((uint8_t*)pal + ((v16 >> 27) & 0x1E)), 1);
      *dst32++ = pix;

      ALOWORD(pix) = ror16(*(uint16_t*)((uint8_t*)pal + ((v16 >> 15) & 0x1E)), 1);
      pix <<= 16;
      ALOWORD(pix) = ror16(*(uint16_t*)((uint8_t*)pal + ((v16 >> 19) & 0x1E)), 1);
      *dst32++ = pix;

      ALOWORD(pix) = ror16(*(uint16_t*)((uint8_t*)pal + ((v16 >> 7) & 0x1E)), 1);
      pix <<= 16;
      ALOWORD(pix) = ror16(*(uint16_t*)((uint8_t*)pal + ((v16 >> 11) & 0x1E)), 1);
      *dst32++ = pix;

      ALOWORD(pix) = ror16(*(uint16_t*)((uint8_t*)pal + (2 * (uint8_t)v16 & 0x1E)), 1);
      pix <<= 16;
      ALOWORD(pix) = ror16(*(uint16_t*)((uint8_t*)pal + ((v16 >> 3) & 0x1E)), 1);
      *dst32++ = pix;

      src32 += 2;
    }

    src32 = (uint32_t*)&src[(line + (uintptr_t)src32 - (uintptr_t)src) & 0x7FF];
    dst32 = (uint32_t*)((uint8_t*)dst32 + ext);

    odd ^= 1;
  }
}

static INLINE void load4bIAPal(uint8_t *src, uint8_t *dst, int wid_64, int height, int line, int ext, uint16_t *pal)
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
         uint32_t pix = width + 1;

         ALOWORD(pix) = ror16(*(uint16_t *)((char *)pal + ((ab >> 23) & 0x1E)), 8);
         pix = pix << 16;
         ALOWORD(pix) = ror16(*(uint16_t *)((char *)pal + ((ab >> 27) & 0x1E)), 8);
         *dst32++ = pix;

         ALOWORD(pix) = ror16(*(uint16_t *)((char *)pal + ((ab >> 15) & 0x1E)), 8);
         pix <<= 16;
         ALOWORD(pix) = ror16(*(uint16_t *)((char *)pal + ((ab >> 19) & 0x1E)), 8);
         *dst32++ = pix;

         ALOWORD(pix) = ror16(*(uint16_t *)((char *)pal + ((ab >> 7) & 0x1E)), 8);
         pix <<= 16;
         ALOWORD(pix) = ror16(*(uint16_t *)((char *)pal + ((ab >> 11) & 0x1E)), 8);
         *dst32++ = pix;

         ALOWORD(pix) = ror16(*(uint16_t *)((char *)pal + (2 * (uint8_t)ab & 0x1E)), 8);
         pix <<= 16;
         ALOWORD(pix) = ror16(*(uint16_t *)((char *)pal + ((ab >> 3) & 0x1E)), 8);
         *dst32++ = pix;

         ALOWORD(pix) = ror16(*(uint16_t *)((char *)pal + ((cd >> 23) & 0x1E)), 8);
         pix <<= 16;
         ALOWORD(pix) = ror16(*(uint16_t *)((char *)pal + ((cd >> 27) & 0x1E)), 8);
         *dst32++ = pix;

         ALOWORD(pix) = ror16(*(uint16_t *)((char *)pal + ((cd >> 15) & 0x1E)), 8);
         pix <<= 16;
         ALOWORD(pix) = ror16(*(uint16_t *)((char *)pal + ((cd >> 19) & 0x1E)), 8);
         *dst32++ = pix;

         ALOWORD(pix) = ror16(*(uint16_t *)((char *)pal + ((cd >> 7) & 0x1E)), 8);
         pix <<= 16;
         ALOWORD(pix) = ror16(*(uint16_t *)((char *)pal + ((cd >> 11) & 0x1E)), 8);
         *dst32++ = pix;

         ALOWORD(pix) = ror16(*(uint16_t *)((char *)pal + (2 * (uint8_t)cd & 0x1E)), 8);
         pix <<= 16;
         ALOWORD(pix) = ror16(*(uint16_t *)((char *)pal + ((cd >> 3) & 0x1E)), 8);
         *dst32++ = pix;

         src32 += 2;
      }

      src32 = (uint32_t*)&src[(line + (uintptr_t)src32 - (uintptr_t)src) & 0x7FF];
      dst32 = (uint32_t*)((uint8_t*)dst32 + ext);

      odd ^= 1;
   }
}

static INLINE void load4bIA(uint8_t *src, uint8_t *dst, int wid_64, int height, int line, int ext)
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
         uint32_t v1, v2, v3, v4, v5;

         v1 = (ab >> 4) & 0xE0000u;
         v2 = v1 | (8 * (ab & 0x100000)) | (4 * (ab & 0x100000)) | (2 * (ab & 0x100000)) | (ab & 0x100000) | ((((ab >> 16) & 0xE00) >> 3) & 0x100) | ((ab >> 16) & 0xE00) | (8 * ((ab >> 12) & 0x1000)) | (4 * ((ab >> 12) & 0x1000)) | (2 * ((ab >> 12) & 0x1000)) | ((ab >> 12) & 0x1000) | (((ab >> 28) & 0xE) >> 3) | ((ab >> 28) & 0xE) | (8 * ((ab >> 24) & 0x10)) | (4 * ((ab >> 24) & 0x10)) | (2 * ((ab >> 24) & 0x10)) | ((ab >> 24) & 0x10);
         v1 >>= 3;
         *dst32++ = ((((ab << 8) & 0xE000000) >> 3) & 0x1000000) | ((ab << 8) & 0xE000000) | (8 * ((ab << 12) & 0x10000000)) | (4 * ((ab << 12) & 0x10000000)) | (2 * ((ab << 12) & 0x10000000)) | ((ab << 12) & 0x10000000) | (v1 & 0x10000) | v2;

         v1 = (uint16_t)ab & 0x1000;
         v2 = (((ab & 0xE00) >> 3) & 0x100) | (ab & 0xE00) | (8 * v1) | (4 * v1) | (2 * v1) | v1 | (((ab >> 12) & 0xE) >> 3) | ((ab >> 12) & 0xE) | (8 * ((ab >> 8) & 0x10)) | (4 * ((ab >> 8) & 0x10)) | (2 * ((ab >> 8) & 0x10)) | ((ab >> 8) & 0x10);
         v3 = ab << 16;
         v4 = (ab << 12) & 0xE0000u;
         v5 = v4 | (8 * (v3 & 0x100000)) | (4 * (v3 & 0x100000)) | (2 * (v3 & 0x100000)) | (v3 & 0x100000) | v2;;
         v4 >>= 3;
         *dst32++ = ((((ab << 24) & 0xE000000) >> 3) & 0x1000000) | ((ab << 24) & 0xE000000) | (8 * ((ab << 28) & 0x10000000)) | (4 * ((ab << 28) & 0x10000000)) | (2 * ((ab << 28) & 0x10000000)) | ((ab << 28) & 0x10000000) | (v4 & 0x10000) | v5;

         v1 = (cd >> 4) & 0xE0000u;
         v2 = v1 | (8 * (cd & 0x100000)) | (4 * (cd & 0x100000)) | (2 * (cd & 0x100000)) | (cd & 0x100000) | ((((cd >> 16) & 0xE00) >> 3) & 0x100) | ((cd >> 16) & 0xE00) | (8 * ((cd >> 12) & 0x1000)) | (4 * ((cd >> 12) & 0x1000)) | (2 * ((cd >> 12) & 0x1000)) | ((cd >> 12) & 0x1000) | (((cd >> 28) & 0xE) >> 3) | ((cd >> 28) & 0xE) | (8 * ((cd >> 24) & 0x10)) | (4 * ((cd >> 24) & 0x10)) | (2 * ((cd >> 24) & 0x10)) | ((cd >> 24) & 0x10);
         v1 >>= 3;
         *dst32++ = ((((cd << 8) & 0xE000000) >> 3) & 0x1000000) | ((cd << 8) & 0xE000000) | (8 * ((cd << 12) & 0x10000000)) | (4 * ((cd << 12) & 0x10000000)) | (2 * ((cd << 12) & 0x10000000)) | ((cd << 12) & 0x10000000) | (v1 & 0x10000) | v2;

         v1 = (uint16_t)cd & 0x1000;
         v2 = (((cd & 0xE00) >> 3) & 0x100) | (cd & 0xE00) | (8 * v1) | (4 * v1) | (2 * v1) | v1 | (((cd >> 12) & 0xE) >> 3) | ((cd >> 12) & 0xE) | (8 * ((cd >> 8) & 0x10)) | (4 * ((cd >> 8) & 0x10)) | (2 * ((cd >> 8) & 0x10)) | ((cd >> 8) & 0x10);
         v3 = cd << 16;
         v4 = (cd << 12) & 0xE0000u;
         v5 = v4 | (8 * (v3 & 0x100000)) | (4 * (v3 & 0x100000)) | (2 * (v3 & 0x100000)) | (v3 & 0x100000) | v2;;
         v4 >>= 3;
         *dst32++ = ((((cd << 24) & 0xE000000) >> 3) & 0x1000000) | ((cd << 24) & 0xE000000) | (8 * ((cd << 28) & 0x10000000)) | (4 * ((cd << 28) & 0x10000000)) | (2 * ((cd << 28) & 0x10000000)) | ((cd << 28) & 0x10000000) | (v4 & 0x10000) | v5;

         src32 += 2;
      }

      src32 = (uint32_t*)((uint8_t*)src32 + line);
      dst32 = (uint32_t*)((uint8_t*)dst32 + ext);
      odd ^= 1;
   }
}

static INLINE void load4bI(uint8_t *src, uint8_t *dst, int wid_64, int height, int line, int ext)
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
         uint32_t pix;

         pix = ab >> 4;
         *dst32++ = (16 * ((ab << 8) & 0xF000000)) | ((ab << 8) & 0xF000000) | (16 * (pix & 0xF0000)) | (pix & 0xF0000) | (16 * ((ab >> 16) & 0xF00)) | ((ab >> 16) & 0xF00) | (16 * (ab >> 28)) | (ab >> 28);

         pix = ab << 12;
         *dst32++ = (16 * ((ab << 24) & 0xF000000)) | ((ab << 24) & 0xF000000) | (16 * (pix & 0xF0000)) | (pix & 0xF0000) | (16 * (ab & 0xF00)) | (ab & 0xF00) | (16 * ((uint16_t)ab >> 12)) | ((uint16_t)ab >> 12);

         pix = cd >> 4;
         *dst32++ = (16 * ((cd << 8) & 0xF000000)) | ((cd << 8) & 0xF000000) | (16 * (pix & 0xF0000)) | (pix & 0xF0000) | (16 * ((cd >> 16) & 0xF00)) | ((cd >> 16) & 0xF00) | (16 * (cd >> 28)) | (cd >> 28);

         pix = cd << 12;
         *dst32++ = (16 * ((cd << 24) & 0xF000000)) | ((cd << 24) & 0xF000000) | (16 * (pix & 0xF0000)) | (pix & 0xF0000) | (16 * (cd & 0xF00)) | (cd & 0xF00) | (16 * ((uint16_t)cd >> 12)) | ((uint16_t)cd >> 12);

         src32 += 2;
      }

      src32 = (uint32_t*)((uint8_t*)src32 + line);
      dst32 = (uint32_t*)((uint8_t*)dst32 + ext);
      odd ^= 1;
   }
}

//****************************************************************
// Size: 0, Format: 2

uint32_t Load4bCI (uintptr_t dst, uintptr_t src, int wid_64, int height, int line, int real_width, int tile)
{
   int ext;
   uintptr_t pal;

   if (wid_64 < 1)
      wid_64 = 1;
   if (height < 1)
      height = 1;
   ext = (real_width - (wid_64 << 4));

   if (rdp.tlut_mode == 0)
   {
      //in tlut DISABLE mode load CI texture as plain intensity texture instead of palette dereference.
      //Thanks to angrylion for the advice
      load4bI ((uint8_t *)src, (uint8_t *)dst, wid_64, height, line, ext);
      return /*(0 << 16) | */GR_TEXFMT_ALPHA_INTENSITY_44;
   }

   pal = (uintptr_t)(rdp.pal_8 + (g_gdp.tile[tile].palette << 4));
   if (rdp.tlut_mode == 2)
   {
      ext <<= 1;
      load4bCI ((uint8_t *)src, (uint8_t *)dst, wid_64, height, line, ext, (uint16_t *)pal);

      return (1 << 16) | GR_TEXFMT_ARGB_1555;
   }

   ext <<= 1;
   load4bIAPal ((uint8_t *)src, (uint8_t *)dst, wid_64, height, line, ext, (uint16_t *)pal);
   return (1 << 16) | GR_TEXFMT_ALPHA_INTENSITY_88;
}

//****************************************************************
// Size: 0, Format: 3
//
// ** BY GUGAMAN **

uint32_t Load4bIA (uintptr_t dst, uintptr_t src, int wid_64, int height, int line, int real_width, int tile)
{
  int ext;
  if (rdp.tlut_mode != 0)
    return Load4bCI (dst, src, wid_64, height, line, real_width, tile);

  if (wid_64 < 1) wid_64 = 1;
  if (height < 1) height = 1;
  ext = (real_width - (wid_64 << 4));
  load4bIA ((uint8_t *)src, (uint8_t *)dst, wid_64, height, line, ext);
  return /*(0 << 16) | */GR_TEXFMT_ALPHA_INTENSITY_44;
}

//****************************************************************
// Size: 0, Format: 4

uint32_t Load4bI (uintptr_t dst, uintptr_t src, int wid_64, int height, int line, int real_width, int tile)
{
  int ext;
  if (rdp.tlut_mode != 0)
    return Load4bCI (dst, src, wid_64, height, line, real_width, tile);

  if (wid_64 < 1)
     wid_64 = 1;
  if (height < 1)
     height = 1;
  ext = (real_width - (wid_64 << 4));
  load4bI ((uint8_t *)src, (uint8_t *)dst, wid_64, height, line, ext);
  
  return /*(0 << 16) | */GR_TEXFMT_ALPHA_INTENSITY_44;
}

//****************************************************************
// Size: 0, Format: 0

uint32_t Load4bSelect (uintptr_t dst, uintptr_t src, int wid_64, int height, int line, int real_width, int tile)
{
  if (rdp.tlut_mode == 0)
    return Load4bI (dst, src, wid_64, height, line, real_width, tile);

  return Load4bCI (dst, src, wid_64, height, line, real_width, tile);
}
