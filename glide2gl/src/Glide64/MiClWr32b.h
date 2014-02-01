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
//
// Created by Gonetz, 2007
//
//****************************************************************

static INLINE void mirror32bS(uint8_t *tex, uint8_t *start, int width, int height, int mask, int line, int full, int count)
{
   uint32_t *v8 = (uint32_t *)start;

   do
   {
      int v10 = 0;
      do
      {
         if ( width & (v10 + width) )
            *v8++ = *(uint32_t *)(&tex[mask] - (mask & 4 * v10));
         else
            *v8++ = *(uint32_t *)&tex[mask & 4 * v10];
      }while (++v10 != count );
      v8 = (uint32_t *)((char *)v8 + line);
      tex += full;
   }while (--height);
}

static INLINE void wrap32bS(uint8_t *tex, uint8_t *start, int height, int mask, int line, int full, int count)
{
   uint32_t *v7 = (uint32_t *)start;

   do
   {
      int v9 = 0;
      do
      {
         *v7++ = *(uint32_t *)&tex[4 * (mask & v9)];
      }while (--v9 != count );
      v7 = (uint32_t *)((char *)v7 + line);
      tex += full;
   }while (--height);
}

static INLINE void clamp32bS(uint8_t *tex, uint8_t *constant, int height, int line, int full, int count)
{
   uint32_t *v6, *v7;
   v6 = (uint32_t *)constant;
   v7 = (uint32_t *)tex;

   do
   {
      int v10 = count;
      do
      {
         *v7++ = *v6;
      }while (--v10);
      v6 = (uint32_t *)((char *)v6 + full);
      v7 = (uint32_t *)((char *)v7 + line);
   }while (--height);
}

//****************************************************************
// 32-bit Horizontal Mirror

void Mirror32bS (uint8_t *tex, uint32_t mask, uint32_t max_width, uint32_t real_width, uint32_t height)
{
   int32_t count, line_full, line;
   uint8_t *start;
   uint32_t mask_width, mask_mask;
   if (mask == 0)
      return;

   mask_width = (1 << mask);
   mask_mask = (mask_width-1) << 2;
   if (mask_width >= max_width)
      return;
   count = max_width - mask_width;
   if (count <= 0)
      return;
   line_full = real_width << 2;
   line = line_full - (count << 2);
   if (line < 0)
      return;
   start = (uint8_t*)(tex + (mask_width << 2));
   mirror32bS (tex, start, mask_width, height, mask_mask, line, line_full, count);
}

//****************************************************************
// 32-bit Horizontal Wrap 

void Wrap32bS (uint8_t *tex, uint32_t mask, uint32_t max_width, uint32_t real_width, uint32_t height)
{
   int32_t count, line_full, line;
   uint8_t *start;
   uint32_t mask_width, mask_mask;
   if (mask == 0)
      return;

   mask_width = (1 << mask);
   mask_mask = (mask_width-1);

   if (mask_width >= max_width)
      return;
   count = (max_width - mask_width);
   if (count <= 0)
      return;
   line_full = real_width << 2;
   line = line_full - (count << 2);
   if (line < 0)
      return;
   start = (uint8_t*)(tex + (mask_width << 2));
   wrap32bS (tex, start, height, mask_mask, line, line_full, count);
}

//****************************************************************
// 32-bit Horizontal Clamp

void Clamp32bS (uint8_t *tex, uint32_t width, uint32_t clamp_to, uint32_t real_width, uint32_t real_height)
{
   uint8_t *dest, *constant;
   int32_t count, line_full, line;
	if (real_width <= width)
      return;

	dest = (uint8_t*)(tex + (width << 2));
	constant = (uint8_t*)(dest-4);
	count = clamp_to - width;
	line_full = real_width << 2;
	line = width << 2;
	clamp32bS (dest, constant, real_height, line, line_full, count);
}

//****************************************************************
// 32-bit Vertical Mirror

void Mirror32bT (uint8_t *tex, uint32_t mask, uint32_t max_height, uint32_t real_width)
{
   int32_t line_full;
   uint8_t *dst;
   uint32_t mask_height, mask_mask, y;
   if (mask == 0)
      return;

   mask_height = (1 << mask);
   mask_mask = mask_height-1;
   if (max_height <= mask_height) return;
   line_full = real_width << 2;

   dst = (uint8_t*)(tex + mask_height * line_full);

   for (y = mask_height; y < max_height; y++)
   {
      if (y & mask_height) // mirrored
         memcpy ((void*)dst, (void*)(tex + (mask_mask - (y & mask_mask)) * line_full), line_full);
      else                 // not mirrored
         memcpy ((void*)dst, (void*)(tex + (y & mask_mask) * line_full), line_full);

      dst += line_full;
   }
}

//****************************************************************
// 32-bit Vertical Wrap

void Wrap32bT (uint8_t *tex, uint32_t mask, uint32_t max_height, uint32_t real_width)
{
   uint32_t y, mask_height, mask_mask;
   uint8_t *dst;
   int line_full;

   if (mask == 0)
      return;

   mask_height = (1 << mask);
   mask_mask = mask_height-1;

   if (max_height <= mask_height)
      return;

   line_full = real_width << 2;
   dst = (uint8_t*)(tex + mask_height * line_full);

   for (y = mask_height; y < max_height; y++)
   {
      // not mirrored
      memcpy ((void*)dst, (void*)(tex + (y & mask_mask) * (line_full>>2)), (line_full>>2));
      dst += line_full;
   }
}

//****************************************************************
// 32-bit Vertical Clamp

void Clamp32bT (uint8_t *tex, uint32_t height, uint32_t real_width, uint32_t clamp_to)
{
   int32_t line_full;
   uint32_t y;
   uint8_t *dst, *const_line;

	line_full = real_width << 2;
	dst = (uint8_t*)(tex + height * line_full);
	const_line = (uint8_t*)(dst - line_full);

	for (y = height; y < clamp_to; y++)
	{
		memcpy ((void*)dst, (void*)const_line, line_full);
		dst += line_full;
	}
}
