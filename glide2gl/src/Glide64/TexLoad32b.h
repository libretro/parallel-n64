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
#include "Gfx_1.3.h"

//****************************************************************
// Size: 2, Format: 0
//
// Load 32bit RGBA texture
// Based on sources of angrylion's software plugin.
//
uint32_t Load32bRGBA (uintptr_t dst, uintptr_t src, int wid_64, int height, int line, int real_width, int tile)
{
   uint16_t *tmem16;
   int32_t ext, id;
   uint32_t tbase, width, s, t, *tex, mod;

   if (height < 1)
      height = 1;

   tmem16 = (uint16_t*)rdp.tmem;
   tbase = (src - (uintptr_t)rdp.tmem) >> 1;
   width = max(1, wid_64 << 1);
   ext = real_width - width;
   line = width + (line>>2);
   tex = (uint32_t*)dst;
   for (t = 0; t < (uint32_t)height; t++)
   {
      uint32_t tline, xorval;
      tline = tbase + line * t;
      xorval = (t & 1) ? 3 : 1;
      for (s = 0; s < width; s++)
      {
         uint32_t taddr = ((tline + s) ^ xorval) & 0x3ff;
         *tex++ = ((tmem16[taddr|0x400] & 0xFF)<<24) | (tmem16[taddr] << 8) | (tmem16[taddr|0x400] >> 8);
      }
      tex += ext;
   }

   id = tile - rdp.cur_tile;
   mod = (id == 0) ? cmb.mod_0 : cmb.mod_1;

   if (mod)
   {
      uint32_t tex_size, c;
      uint16_t *tex16;

      //convert to ARGB_4444
      tex_size = real_width * height;
      tex = (uint32_t *)dst;
      tex16 = (uint16_t*)dst;
      do
      {
         c = *tex++;
         *tex16++ = (((c >> 28) & 0xF) <<12) | (((c >> 20) & 0xF) << 8) | (((c >> 12) & 0xF) << 4) | ((c >> 4)  & 0xF);
      }while(--tex_size);
      return (1 << 16) | GR_TEXFMT_ARGB_4444;
   }
   return (2 << 16) | GR_TEXFMT_ARGB_8888;
}

//****************************************************************
// LoadTile for 32bit RGBA texture
// Based on sources of angrylion's software plugin.
//
void LoadTile32b (uint32_t tile, uint32_t ul_s, uint32_t ul_t, uint32_t width, uint32_t height)
{
   uint32_t j, i, line, tbase, addr, *src, c, ptr, tline, s, xorval;
   uint16_t *tmem16;

   line = rdp.tiles[tile].line << 2;
   tbase = rdp.tiles[tile].t_mem << 2;
   addr = rdp.timg.addr >> 2;
   src = (uint32_t*)gfx.RDRAM;
   tmem16 = (uint16_t*)rdp.tmem;

   for (j = 0; j < height; j++)
   {
      tline = tbase + line * j;
      s = ((j + ul_t) * rdp.timg.width) + ul_s;
      xorval = (j & 1) ? 3 : 1;				
      for (i = 0; i < width; i++)
      {
         c = src[addr + s + i];
         ptr = ((tline + i) ^ xorval) & 0x3ff;
         tmem16[ptr] = c >> 16;
         tmem16[ptr|0x400] = c & 0xffff;
      }
   }
}

//****************************************************************
// LoadBlock for 32bit RGBA texture
// Based on sources of angrylion's software plugin.
//
void LoadBlock32b(uint32_t tile, uint32_t ul_s, uint32_t ul_t, uint32_t lr_s, uint32_t dxt)
{
   uint32_t i, *src, tb, tiwindwords, slindwords, line, addr, width;
   uint16_t *tmem16;

   src = (uint32_t*)gfx.RDRAM;
   tb = rdp.tiles[tile].t_mem << 2;
   tiwindwords = rdp.timg.width;
   slindwords = ul_s;
   line = rdp.tiles[tile].line << 2;

   tmem16 = (uint16_t*)rdp.tmem;
   addr = rdp.timg.addr >> 2;
   width = (lr_s - ul_s + 1) << 2;

   if (width & 7)
      width = (width & (~7)) + 8;

   if (dxt != 0)
   {
      uint32_t j, t, oldt, ptr;
      j= 0;
      t = 0;
      oldt = 0;

      addr += (ul_t * tiwindwords) + slindwords;
      for (i = 0; i < width; i += 2)
      {
         uint32_t c;
         oldt = t;
         t = ((j >> 11) & 1) ? 3 : 1;
         if (t != oldt)
            i += line;
         ptr = ((tb + i) ^ t) & 0x3ff;
         c = src[addr + i];
         tmem16[ptr] = c >> 16;
         tmem16[ptr|0x400] = c & 0xffff;
         ptr = ((tb+ i + 1) ^ t) & 0x3ff;
         c = src[addr + i + 1];
         tmem16[ptr] = c >> 16;
         tmem16[ptr|0x400] = c & 0xffff;
         j += dxt;
      }
   }
   else
   {
      uint32_t ptr;
      addr += (ul_t * tiwindwords) + slindwords;
      for (i = 0; i < width; i ++)
      {
         uint32_t c;
         ptr = ((tb + i) ^ 1) & 0x3ff;
         c = src[addr + i];
         tmem16[ptr] = c >> 16;
         tmem16[ptr|0x400] = c & 0xffff;
      }
   }
}
