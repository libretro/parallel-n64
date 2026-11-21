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

/* LoadBlock and LoadTile: RDRAM words into texture memory. On their
 * own so that they can be built and tested without the plugin. */
#include <stdint.h>
#include <boolean.h>
#include <retro_inline.h>
#include <retro_endianness.h>

#include "TexLoadBlock.h"

static INLINE uint32_t rotl8(uint32_t value)
{
   return (value << 8) | (value >> 24);
}

static INLINE void load_block_line(uint32_t *src, uint32_t *dst,
      unsigned offset, unsigned width, bool is_load_block)
{
   unsigned length = width;
   uint32_t *src32 = (uint32_t*)((uint8_t*)src + (offset & 0xFFFFFFFC));
   uint32_t *dst32 = dst;

   if (!length)
      return;

   if (offset & 3)
   {
      uint32_t numrot = offset & 3;
      uint32_t nwords = 4 - numrot;
      uint32_t word   = *src32++;

      while (numrot--)
         word = rotl8(word);

      if (is_load_block)
      {
         while (nwords--)
         {
            word = rotl8(word);
            *dst32++ = word;
         }
      }
      else
      {
         while (nwords--)
         {
            word = rotl8(word);
            *(uint8_t*)dst32 = word;
            dst32 = (uint32_t*)((uint8_t*)dst32 + 1);
         }
      }

      *dst32++ = SWAP32(*src32++);

      --length;
   }

   while (length--)
   {
      *dst32++ = SWAP32(*src32++);
      *dst32++ = SWAP32(*src32++);
   }

   if (offset & 3)
   {
      uint32_t nwords = offset & 3;
      uint32_t word   = *(uint32_t*)((uint8_t*)src + ((8 * width + offset) & 0xFFFFFFFC));

      if (is_load_block)
      {
         while (nwords--)
         {
            word = rotl8(word);
            *dst32++ = word;
         }
      }
      else
      {
         while (nwords--)
         {
            word = rotl8(word);
            *(uint8_t*)dst32 = word;
            dst32 = (uint32_t*)((uint8_t*)dst32 + 1);
         }
      }
   }
}

static INLINE void dxt_swap(uint32_t *line, int width)
{
   while (width--)
   {
      line[0] ^= line[1];
      line[1] ^= line[0];
      line[0] ^= line[1];
      line += 2;
   }
}

void loadTile(uint32_t *src, uint32_t *dst,
      int width, int height, int line, int off, uint32_t *end)
{
   unsigned odd = 0;

   while (height-- && end >= dst)
   {
      load_block_line(src, dst, off, width, false);

      if (odd)
         dxt_swap(dst, width);

      dst += width * 2;
      off += line;
      odd ^= 1;
   }
}

void loadBlock(uint32_t *src, uint32_t *dst, uint32_t off, int dxt, int cnt)
{
   /* The texture coordinate step accumulates modulo 2^32, and a run of
    * swapped words lasts while its top bit is set: unsigned, so that
    * the wrap is defined. */
   uint32_t v16 = 0;
   int32_t length = cnt;

   load_block_line(src, dst, off, cnt, true);

   while (length-- > 0)
   {
      int32_t v18 = 0;

      dst += 2;
      v16 += (uint32_t)dxt;

      while ((v16 & 0x80000000u) && length--)
      {
         ++v18;
         v16 += (uint32_t)dxt;
      }

      dxt_swap(dst, v18);
      dst += v18 * 2;
   }
}
