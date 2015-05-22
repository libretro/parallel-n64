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

#define NBITS_4B (sizeof(uint16_t) * 8)

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
  int v10;
  int v11;
  uint32_t v12;
  uint32_t *v13;
  uint32_t v14;
  uint32_t *v15;
  uint32_t v16;
  uint8_t *v17;
  uint32_t *v18;
  int v19;
  int v20;
  uint32_t v21;
  uint32_t v22;
  uint32_t *v23;
  uint32_t v24;
  int v25;
  int v26;
  uint8_t *v7 = src;
  uint32_t *v8 = (uint32_t *)dst;
  int v9 = height;

  do
  {
    v25 = v9;
    v10 = wid_64;
    do
    {
      v11 = v10;
      v12 = m64p_swap32(*(uint32_t *)v7);
      v13 = (uint32_t *)(v7 + 4);
      ALOWORD(v10) = __ROR16(*(uint16_t *)((char *)pal + ((v12 >> 23) & 0x1E)), 8, NBITS_4B);
      v14 = v10 << 16;
      ALOWORD(v14) = __ROR16(*(uint16_t *)((char *)pal + ((v12 >> 27) & 0x1E)), 8, NBITS_4B);
      *v8 = v14;
      v15 = v8 + 1;
      ALOWORD(v14) = __ROR16(*(uint16_t *)((char *)pal + ((v12 >> 15) & 0x1E)), 8, NBITS_4B);
      v14 <<= 16;
      ALOWORD(v14) = __ROR16(*(uint16_t *)((char *)pal + ((v12 >> 19) & 0x1E)), 8, NBITS_4B);
      *v15 = v14;
      ++v15;
      ALOWORD(v14) = __ROR16(*(uint16_t *)((char *)pal + ((v12 >> 7) & 0x1E)), 8, NBITS_4B);
      v14 <<= 16;
      ALOWORD(v14) = __ROR16(*(uint16_t *)((char *)pal + ((v12 >> 11) & 0x1E)), 8, NBITS_4B);
      *v15 = v14;
      ++v15;
      ALOWORD(v14) = __ROR16(*(uint16_t *)((char *)pal + (2 * (uint8_t)v12 & 0x1E)), 8, NBITS_4B);
      v14 <<= 16;
      ALOWORD(v14) = __ROR16(*(uint16_t *)((char *)pal + ((v12 >> 3) & 0x1E)), 8, NBITS_4B);
      *v15 = v14;
      ++v15;
      v16 = m64p_swap32(*v13);
      v7 = (uint8_t *)(v13 + 1);
      ALOWORD(v14) = __ROR16(*(uint16_t *)((char *)pal + ((v16 >> 23) & 0x1E)), 8, NBITS_4B);
      v14 <<= 16;
      ALOWORD(v14) = __ROR16(*(uint16_t *)((char *)pal + ((v16 >> 27) & 0x1E)), 8, NBITS_4B);
      *v15 = v14;
      ++v15;
      ALOWORD(v14) = __ROR16(*(uint16_t *)((char *)pal + ((v16 >> 15) & 0x1E)), 8, NBITS_4B);
      v14 <<= 16;
      ALOWORD(v14) = __ROR16(*(uint16_t *)((char *)pal + ((v16 >> 19) & 0x1E)), 8, NBITS_4B);
      *v15 = v14;
      ++v15;
      ALOWORD(v14) = __ROR16(*(uint16_t *)((char *)pal + ((v16 >> 7) & 0x1E)), 8, NBITS_4B);
      v14 <<= 16;
      ALOWORD(v14) = __ROR16(*(uint16_t *)((char *)pal + ((v16 >> 11) & 0x1E)), 8, NBITS_4B);
      *v15 = v14;
      ++v15;
      ALOWORD(v14) = __ROR16(*(uint16_t *)((char *)pal + (2 * (uint8_t)v16 & 0x1E)), 8, NBITS_4B);
      v14 <<= 16;
      ALOWORD(v14) = __ROR16(*(uint16_t *)((char *)pal + ((v16 >> 3) & 0x1E)), 8, NBITS_4B);
      *v15 = v14;
      v8 = v15 + 1;
      v10 = v11 - 1;
    }
    while ( v11 != 1 );
    if ( v25 == 1 )
      break;
    v26 = v25 - 1;
    v17 = &src[(line + (uintptr_t)v7 - (uintptr_t)src) & 0x7FF];
    v18 = (uint32_t *)((char *)v8 + ext);
    v19 = wid_64;
    do
    {
      v20 = v19;
      v21 = m64p_swap32(*((uint32_t *)v17 + 1));
      ALOWORD(v19) = __ROR16(*(uint16_t *)((char *)pal + ((v21 >> 23) & 0x1E)), 8, NBITS_4B);
      v22 = v19 << 16;
      ALOWORD(v22) = __ROR16(*(uint16_t *)((char *)pal + ((v21 >> 27) & 0x1E)), 8, NBITS_4B);
      *v18 = v22;
      v23 = v18 + 1;
      ALOWORD(v22) = __ROR16(*(uint16_t *)((char *)pal + ((v21 >> 15) & 0x1E)), 8, NBITS_4B);
      v22 <<= 16;
      ALOWORD(v22) = __ROR16(*(uint16_t *)((char *)pal + ((v21 >> 19) & 0x1E)), 8, NBITS_4B);
      *v23 = v22;
      ++v23;
      ALOWORD(v22) = __ROR16(*(uint16_t *)((char *)pal + ((v21 >> 7) & 0x1E)), 8, NBITS_4B);
      v22 <<= 16;
      ALOWORD(v22) = __ROR16(*(uint16_t *)((char *)pal + ((v21 >> 11) & 0x1E)), 8, NBITS_4B);
      *v23 = v22;
      ++v23;
      ALOWORD(v22) = __ROR16(*(uint16_t *)((char *)pal + (2 * (uint8_t)v21 & 0x1E)), 8, NBITS_4B);
      v22 <<= 16;
      ALOWORD(v22) = __ROR16(*(uint16_t *)((char *)pal + ((v21 >> 3) & 0x1E)), 8, NBITS_4B);
      *v23 = v22;
      ++v23;
      v24 = m64p_swap32(*(uint32_t *)v17);
      v17 = &src[((uintptr_t)v17 + 8 - (uintptr_t)src) & 0x7FF];
      ALOWORD(v22) = __ROR16(*(uint16_t *)((char *)pal + ((v24 >> 23) & 0x1E)), 8, NBITS_4B);
      v22 <<= 16;
      ALOWORD(v22) = __ROR16(*(uint16_t *)((char *)pal + ((v24 >> 27) & 0x1E)), 8, NBITS_4B);
      *v23 = v22;
      ++v23;
      ALOWORD(v22) = __ROR16(*(uint16_t *)((char *)pal + ((v24 >> 15) & 0x1E)), 8, NBITS_4B);
      v22 <<= 16;
      ALOWORD(v22) = __ROR16(*(uint16_t *)((char *)pal + ((v24 >> 19) & 0x1E)), 8, NBITS_4B);
      *v23 = v22;
      ++v23;
      ALOWORD(v22) = __ROR16(*(uint16_t *)((char *)pal + ((v24 >> 7) & 0x1E)), 8, NBITS_4B);
      v22 <<= 16;
      ALOWORD(v22) = __ROR16(*(uint16_t *)((char *)pal + ((v24 >> 11) & 0x1E)), 8, NBITS_4B);
      *v23 = v22;
      ++v23;
      ALOWORD(v22) = __ROR16(*(uint16_t *)((char *)pal + (2 * (uint8_t)v24 & 0x1E)), 8, NBITS_4B);
      v22 <<= 16;
      ALOWORD(v22) = __ROR16(*(uint16_t *)((char *)pal + ((v24 >> 3) & 0x1E)), 8, NBITS_4B);
      *v23 = v22;
      v18 = v23 + 1;
      v19 = v20 - 1;
    }
    while ( v20 != 1 );
    v7 = &src[(line + (uintptr_t)v17 - (uintptr_t)src) & 0x7FF];
    v8 = (uint32_t *)((char *)v18 + ext);
    v9 = v26 - 1;
  }
  while ( v26 != 1 );
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
  int v9;
  int v10;
  uint32_t v11;
  uint32_t *v12;
  uint32_t v13;
  uint32_t v14;
  uint32_t *v15;
  uint32_t v16;
  unsigned int v17;
  unsigned int v18;
  uint32_t v19;
  uint32_t v20;
  uint32_t *v21;
  uint32_t *v22;
  int v23;
  int v24;
  uint32_t v25;
  uint32_t v26;
  uint32_t *v27;
  uint32_t v28;
  uint32_t v29;
  uint32_t v30;
  uint32_t v31;
  uint32_t v32;
  int v33;
  int v34;
  uint32_t *v6 = (uint32_t *)src;
  uint32_t *v7 = (uint32_t *)dst;
  int v8 = height;

  do
  {
    v33 = v8;
    v9 = wid_64;
    do
    {
      v10 = v9;
      v11 = m64p_swap32(*v6);
      v12 = v6 + 1;
      v13 = v11;
      v14 = (16 * ((v11 >> 16) & 0xF00)) | ((v11 >> 16) & 0xF00) | (16 * (v11 >> 28)) | (v11 >> 28);
      v11 >>= 4;
      *v7 = (16 * ((v13 << 8) & 0xF000000)) | ((v13 << 8) & 0xF000000) | (16 * (v11 & 0xF0000)) | (v11 & 0xF0000) | v14;
      v15 = v7 + 1;
      v16 = v13 << 12;
      *v15 = (16 * ((v13 << 24) & 0xF000000)) | ((v13 << 24) & 0xF000000) | (16 * (v16 & 0xF0000)) | (v16 & 0xF0000) | (16 * (v13 & 0xF00)) | (v13 & 0xF00) | (16 * ((uint16_t)v13 >> 12)) | ((uint16_t)v13 >> 12);
      ++v15;
      v17 = m64p_swap32(*v12);
      v6 = v12 + 1;
      v18 = v17;
      v19 = (16 * ((v17 >> 16) & 0xF00)) | ((v17 >> 16) & 0xF00) | (16 * (v17 >> 28)) | (v17 >> 28);
      v17 >>= 4;
      *v15 = (16 * ((v18 << 8) & 0xF000000)) | ((v18 << 8) & 0xF000000) | (16 * (v17 & 0xF0000)) | (v17 & 0xF0000) | v19;
      ++v15;
      v20 = v18 << 12;
      *v15 = (16 * ((v18 << 24) & 0xF000000)) | ((v18 << 24) & 0xF000000) | (16 * (v20 & 0xF0000)) | (v20 & 0xF0000) | (16 * (v18 & 0xF00)) | (v18 & 0xF00) | (16 * ((uint16_t)v18 >> 12)) | ((uint16_t)v18 >> 12);
      v7 = v15 + 1;
      v9 = v10 - 1;
    }
    while ( v10 != 1 );
    if ( v33 == 1 )
      break;
    v34 = v33 - 1;
    v21 = (uint32_t *)((char *)v6 + line);
    v22 = (uint32_t *)((char *)v7 + ext);
    v23 = wid_64;
    do
    {
      v24 = v23;
      v25 = m64p_swap32(v21[1]);
      v26 = v25 >> 4;
      *v22 = (16 * ((v25 << 8) & 0xF000000)) | ((v25 << 8) & 0xF000000) | (16 * (v26 & 0xF0000)) | (v26 & 0xF0000) | (16 * ((v25 >> 16) & 0xF00)) | ((v25 >> 16) & 0xF00) | (16 * (v25 >> 28)) | (v25 >> 28);
      v27 = v22 + 1;
      v28 = v25 << 12;
      *v27 = (16 * ((v25 << 24) & 0xF000000)) | ((v25 << 24) & 0xF000000) | (16 * (v28 & 0xF0000)) | (v28 & 0xF0000) | (16 * (v25 & 0xF00)) | (v25 & 0xF00) | (16 * ((uint16_t)v25 >> 12)) | ((uint16_t)v25 >> 12);
      ++v27;
      v29 = m64p_swap32(*v21);
      v21 += 2;
      v30 = v29;
      v31 = (16 * ((v29 >> 16) & 0xF00)) | ((v29 >> 16) & 0xF00) | (16 * (v29 >> 28)) | (v29 >> 28);
      v29 >>= 4;
      *v27 = (16 * ((v30 << 8) & 0xF000000)) | ((v30 << 8) & 0xF000000) | (16 * (v29 & 0xF0000)) | (v29 & 0xF0000) | v31;
      ++v27;
      v32 = v30 << 12;
      *v27 = (16 * ((v30 << 24) & 0xF000000)) | ((v30 << 24) & 0xF000000) | (16 * (v32 & 0xF0000)) | (v32 & 0xF0000) | (16 * (v30 & 0xF00)) | (v30 & 0xF00) | (16 * ((uint16_t)v30 >> 12)) | ((uint16_t)v30 >> 12);
      v22 = v27 + 1;
      v23 = v24 - 1;
    }
    while ( v24 != 1 );
    v6 = (uint32_t *)((char *)v21 + line);
    v7 = (uint32_t *)((char *)v22 + ext);
    v8 = v34 - 1;
  }
  while ( v34 != 1 );
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
