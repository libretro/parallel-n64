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
// 8-bit Horizontal Mirror

void Mirror8bS (uint8_t *tex, uint32_t mask, uint32_t max_width, uint32_t real_width, uint32_t height)
{
   uint32_t mask_width = (1 << mask);
   uint32_t mask_mask = (mask_width-1);
   int32_t count = max_width - mask_width;
   int32_t line_full = real_width;
   int32_t line = line_full - (count);
   uint8_t *start = (uint8_t*)(tex + (mask_width));

   do
   {
      int v10 = 0;
      do
      {
         if ( mask_width & (v10 + mask_width) )
            *start++ = *(&tex[mask_mask] - (mask_mask & v10));
         else
            *start++ = tex[mask_mask & v10];
      }while ( ++v10 != count );
      start += line;
      tex += line_full;
   }while ( --height);
}

//****************************************************************
// 8-bit Horizontal Clamp


void Clamp8bS (uint8_t *tex, uint32_t width, uint32_t clamp_to, uint32_t real_width, uint32_t real_height)
{
   uint8_t *dest = tex + (width);
   uint8_t *constant = dest-1;
   int32_t count = clamp_to - width;
   int32_t line_full = real_width;
   int32_t line = width;

   do
   {
      unsigned i = count;
      do
      {
         *dest++ = *constant;
      }while(--i);
      constant += line_full;
      dest += line;
   }while (--real_height);
}
