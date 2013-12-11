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

//****************************************************************
// 16-bit Horizontal Mirror
#include <stdint.h>
#include <string.h>

void Mirror16bS (unsigned char * tex, uint32_t mask, uint32_t max_width, uint32_t real_width, uint32_t height)
{
   if (mask == 0)
      return;

   uint32_t mask_width = (1 << mask);
   uint32_t mask_mask = (mask_width-1) << 1;
   if (mask_width >= max_width)
      return;
   int count = max_width - mask_width;
   if (count <= 0)
      return;
   int line_full = real_width << 1;
   int line = line_full - (count << 1);
   if (line < 0)
      return;
   uint16_t *v8 = (uint16_t *)(unsigned char*)(tex + (mask_width << 1));
   do
   {
      int v10 = 0;
      do
      {
         if ( mask_width & (v10 + mask_width) )
            *v8++ = *(uint16_t *)(&tex[mask_mask] - (mask_mask & 2 * v10));
         else
            *v8++ = *(uint16_t *)&tex[mask_mask & 2 * v10];
      }while ( ++v10 != count );
      v8 = (uint16_t *)((char *)v8 + line);
      tex += line_full;
   }while (--height);
}

//****************************************************************
// 16-bit Horizontal Wrap (like mirror)

void Wrap16bS (unsigned char * tex, uint32_t mask, uint32_t max_width, uint32_t real_width, uint32_t height)
{
   if (mask == 0)
      return;

   uint32_t mask_width = (1 << mask);
   uint32_t mask_mask = (mask_width-1) >> 1;
   if (mask_width >= max_width)
      return;
   int count = (max_width - mask_width) >> 1;
   if (count <= 0)
      return;
   int line_full = real_width << 1;
   int line = line_full - (count << 2);
   if (line < 0)
      return;
   unsigned char * start = tex + (mask_width << 1);
   uint32_t *v7 = (uint32_t *)start;

   do
   {
      int v9 = 0;
      do
      {
         *v7++ = *(uint32_t *)&tex[4 * (mask_mask & v9)];
      }while ( ++v9 != count );
      v7 = (uint32_t *)((char *)v7 + line);
      tex += line_full;
   }while(--height);
}

//****************************************************************
// 16-bit Horizontal Clamp

void Clamp16bS (unsigned char * tex, uint32_t width, uint32_t clamp_to, uint32_t real_width, uint32_t real_height)
{
   if (real_width <= width)
      return;

   unsigned char * dest = tex + (width << 1);
   unsigned char * constant = dest-2;
   int count = clamp_to - width;

   int line_full = real_width << 1;
   int line = width << 1;

   uint16_t *v6 = (uint16_t *)constant;
   uint16_t *v7 = (uint16_t *)dest;
   do
   {
      int v10 = count;
      do
      {
         *v7++ = *v6;
      }while (--v10 );
      v6 = (uint16_t *)((char *)v6 + line_full);
      v7 = (uint16_t *)((char *)v7 + line);
   }while(--real_height);
}

//****************************************************************
// 16-bit Vertical Mirror

void Mirror16bT (unsigned char * tex, uint32_t mask, uint32_t max_height, uint32_t real_width)
{
   uint32_t y;
   if (mask == 0)
      return;

   uint32_t mask_height = (1 << mask);
   uint32_t mask_mask = mask_height-1;
   if (max_height <= mask_height) return;
   int line_full = real_width << 1;

   unsigned char * dst = tex + mask_height * line_full;

   for (y = mask_height; y<max_height; y++)
   {
      if (y & mask_height)   // mirrored
         memcpy ((void*)dst, (void*)(tex + (mask_mask - (y & mask_mask)) * line_full), line_full);
      else                   // not mirrored
         memcpy ((void*)dst, (void*)(tex + (y & mask_mask) * line_full), line_full);

      dst += line_full;
   }
}

//****************************************************************
// 16-bit Vertical Wrap

void Wrap16bT (unsigned char * tex, uint32_t mask, uint32_t max_height, uint32_t real_width)
{
   if (mask == 0)
      return;

   uint32_t mask_height = (1 << mask);
   uint32_t mask_mask = mask_height-1;
   if (max_height <= mask_height)
      return;
   int line_full = real_width << 1;

   unsigned char * dst = tex + mask_height * line_full;
   uint32_t y;

   for (y = mask_height; y < max_height; y++)
   {
      // not mirrored
      memcpy ((void*)dst, (void*)(tex + (y & mask_mask) * line_full), line_full);

      dst += line_full;
   }
}

//****************************************************************
// 16-bit Vertical Clamp

void Clamp16bT (unsigned char * tex, uint32_t height, uint32_t real_width, uint32_t clamp_to)
{
   uint32_t y;
   int line_full = real_width << 1;
   unsigned char * dst = tex + height * line_full;
   unsigned char * const_line = dst - line_full;

   for (y = height; y < clamp_to; y++)
   {
      memcpy ((void*)dst, (void*)const_line, line_full);
      dst += line_full;
   }
}
