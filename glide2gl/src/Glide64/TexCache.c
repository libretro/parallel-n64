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
#include "TexCache.h"
#include "TexLoad.h"
#include "Combine.h"
#include "Util.h"
#include "libretro.h"
#include "GlideExtensions.h"
#include "MiClWr.h"

#include <clamping.h>
#include <encodings/crc32.h>

#include "../../../Graphics/GBI.h"
#include "../../../Graphics/RDP/gDP_state.h"
#include "../../../Graphics/image_convert.h"

int GetTexAddrUMA(int tmu, int texsize);
static void LoadTex (int id, int tmu);

uint32_t tex1[2048*2048];		// temporary texture
uint8_t *texture;
uint8_t *texture_buffer = (uint8_t *)tex1;

typedef struct TEXINFO_t
{
   int real_image_width, real_image_height;	// FOR ALIGNMENT PURPOSES ONLY!!!
   int tile_width, tile_height;
   int mask_width, mask_height;
   int width, height;
   int wid_64, line;
   uint32_t crc;
   uint32_t flags;
   int splitheight;
} TEXINFO;

TEXINFO texinfo[2];
int tex_found[2][MAX_TMU];

//****************************************************************
// List functions

typedef struct NODE_t
{
   uint32_t	crc;
   uintptr_t	data;
   int		tmu;
   int		number;
   struct NODE_t	*pNext;
} NODE;

NODE *cachelut[65536];

static void AddToList (NODE **list, uint32_t crc, uintptr_t data, int tmu, int number)
{
   NODE *node = (NODE*)malloc(sizeof(NODE));
   node->crc = crc;
   node->data = data;
   node->tmu = tmu;
   node->number = number;
   node->pNext = *list;
   *list = node;
   rdp.n_cached[tmu] ++;
   rdp.n_cached[tmu^1] = rdp.n_cached[tmu];
}

static void DeleteList (NODE **list)
{
   while (*list)
   {
      NODE *next = (*list)->pNext;
      free(*list);
      *list = next;
   }
}

void TexCacheInit(void)
{
   int i;
   for (i = 0; i < 65536; i++)
      cachelut[i] = NULL;
}

// Clear the texture cache for both TMUs
// TMU : Texture Memory Unit (3Dfx Voodoo term)
void ClearCache(void)
{
   int i;
   voodoo.tmem_ptr[0] = offset_textures;
   rdp.n_cached[0] = 0;
   voodoo.tmem_ptr[1] = offset_textures;
   rdp.n_cached[1] = 0;

   for (i = 0; i < 65536; i++)
      DeleteList(&cachelut[i]);
}

static uint32_t textureCRC(uint32_t crc, uint8_t *addr, int width, int height, int line)
{
   const size_t len = sizeof(uint32_t) * 2 * width;

   while (height--)
   {
      crc = encoding_crc32(crc, addr, len);
      addr += len + line;
   }

   return crc;
}

/* Gets information for either t0 or t1, checks if in cache & fills tex_found */
static void GetTexInfo (int id, int tile)
{
   int t, tile_width, tile_height, mask_width, mask_height, width, height, wid_64, line;
   int real_image_width, real_image_height, crc_height;
   uint32_t crc, flags;
   NODE *node;
   CACHE_LUT *cache;
   TEXINFO *info;

   // this is the NEW cache searching, searches only textures with similar crc's
   for (t = 0; t < MAX_TMU; t++)
      tex_found[id][t] = -1;

   info = (TEXINFO*)&texinfo[id];

   // Get width and height
   tile_width  = g_gdp.tile[tile].sl - g_gdp.tile[tile].sh + 1;
   tile_height = g_gdp.tile[tile].tl - g_gdp.tile[tile].th + 1;

   mask_width = (g_gdp.tile[tile].mask_s==0)?(tile_width):(1 << g_gdp.tile[tile].mask_s);
   mask_height = (g_gdp.tile[tile].mask_t==0)?(tile_height):(1 << g_gdp.tile[tile].mask_t);

   if (settings.alt_tex_size)
   {
      // ** ALTERNATE TEXTURE SIZE METHOD **
      // Helps speed in some games that loaded weird-sized textures, but could break other
      //  textures.

      // wrap all the way
      width = MIN(mask_width, tile_width);	// changed from mask_width only
      gDP.tiles[tile].width = width;

      // Get the width/height to load
      if ((g_gdp.tile[tile].cs && tile_width <= 256) || (mask_width > 256))   // actual width
         gDP.tiles[tile].width = tile_width;

      height = MIN(mask_height, tile_height);
      gDP.tiles[tile].height = height;

      // actual height
      if ((g_gdp.tile[tile].ct && tile_height <= 256) || (mask_height > 256))
         gDP.tiles[tile].height = tile_height;
   }
   else
   {
      // ** NORMAL TEXTURE SIZE METHOD **
      // This is the 'correct' method for determining texture size, but may cause certain
      //  textures to load too large & make the whole game go slow.

      if (mask_width > 256 && mask_height > 256)
      {
         mask_width = tile_width;
         mask_height = tile_height;
      }

      width = mask_width;
      gDP.tiles[tile].width = mask_width;
      // Get the width/height to load
      if ((g_gdp.tile[tile].cs && tile_width <= 256) )//|| (mask_width > 256))
      {
         // loading width
         width = MIN(mask_width, tile_width);
         // actual width
         gDP.tiles[tile].width = tile_width;
      }

      height = mask_height;
      gDP.tiles[tile].height = mask_height;

      if ((g_gdp.tile[tile].ct && tile_height <= 256) || (mask_height > 256))
      {
         // loading height
         height = MIN(mask_height, tile_height);
         // actual height
         gDP.tiles[tile].height = tile_height;
      }
   }

   // without any large texture fixing-up; for alignment
   real_image_width  = gDP.tiles[tile].width;
   real_image_height = gDP.tiles[tile].height;
   crc_height = height;
   if (rdp.timg.set_by == 1)
      crc_height = tile_height;

#ifndef NDEBUG
   LRDP(" | | |-+ Texture approved:\n");
   FRDP (" | | | |- tmem: %08lx\n", g_gdp.tile[tile].tmem);
   FRDP (" | | | |- load width: %d\n", width);
   FRDP (" | | | |- load height: %d\n", height);
   FRDP (" | | | |- actual width: %d\n", gDP.tiles[tile].width);
   FRDP (" | | | |- actual height: %d\n", gDP.tiles[tile].height);
   FRDP (" | | | |- size: %d\n", g_gdp.tile[tile].size);
   FRDP (" | | | +- format: %d\n", g_gdp.tile[tile].format);
   LRDP(" | | |- Calculating CRC... ");
#endif

   // ** CRC CHECK

   wid_64 = width << (g_gdp.tile[tile].size) >> 1;
   if (g_gdp.tile[tile].size == G_IM_SIZ_32b)
   {
      if (wid_64 & 15) wid_64 += 16;
      wid_64 &= 0xFFFFFFF0;
   }
   else
   {
      if (wid_64 & 7) wid_64 += 8;	// round up
   }
   wid_64 = wid_64>>3;

   // Texture too big for tmem & needs to wrap? (trees in mm)
   if (g_gdp.tile[tile].tmem + MIN(height, tile_height) * (g_gdp.tile[tile].line << 3) > 4096)
   {
      int y, shift;
      LRDP("TEXTURE WRAPS TMEM!!! ");

      // calculate the y value that intersects at 4096 bytes
      y = (4096 - g_gdp.tile[tile].tmem) / (g_gdp.tile[tile].line<<3);

      g_gdp.tile[tile].ct = 0;
      g_gdp.tile[tile].tl = g_gdp.tile[tile].th + y - 1;

      // calc mask
      for (shift=0; (1<<shift)<y; shift++);
      g_gdp.tile[tile].mask_t = shift;

      // restart the function
      LRDP("restarting...\n");
      GetTexInfo (id, tile);
      return;
   }

   line = g_gdp.tile[tile].line;
   if (g_gdp.tile[tile].size == G_IM_SIZ_32b)
      line <<= 1;

   crc = 0;

   if ((g_gdp.tile[tile].size < 2) && (rdp.tlut_mode || g_gdp.tile[tile].format == G_IM_FMT_CI))
   {
      if (g_gdp.tile[tile].size == G_IM_SIZ_4b)
         crc = rdp.pal_8_crc[g_gdp.tile[tile].palette];
      else
         crc = gDP.paletteCRC256;
   }

   {
      uint8_t *addr;
      line = (line - wid_64) << 3;
      if (wid_64 < 1)
         wid_64 = 1;
      addr = (uint8_t*)((((uint8_t*)g_gdp.tmem) + (g_gdp.tile[tile].tmem << 3)));
      if (crc_height > 0) // Check the CRC
      {
         if (g_gdp.tile[tile].size < 3)
            crc = textureCRC(crc, addr, wid_64, crc_height, line);
         else //32b texture
         {
            int line_2, wid_64_2;
            line_2 = line >> 1;
            wid_64_2 = MAX(1, wid_64 >> 1);
            crc = textureCRC(crc, addr, wid_64_2, crc_height, line_2);
            crc = textureCRC(crc, addr+0x800, wid_64_2, crc_height, line_2);
         }
      }
   }


   FRDP ("Done.  CRC is: %08lx.\n", crc);

   flags = (g_gdp.tile[tile].cs << 23) | (g_gdp.tile[tile].ms << 22) |
      (g_gdp.tile[tile].mask_s << 18) | (g_gdp.tile[tile].ct << 17) |
      (g_gdp.tile[tile].mt << 16) | (g_gdp.tile[tile].mask_t << 12);

   info->real_image_width = real_image_width;
   info->real_image_height = real_image_height;
   info->tile_width = tile_width;
   info->tile_height = tile_height;
   info->mask_width = mask_width;
   info->mask_height = mask_height;
   info->width = width;
   info->height = height;
   info->wid_64 = wid_64;
   info->line = line;
   info->crc = crc;
   info->flags = flags;

   // Search the texture cache for this texture
   LRDP(" | | |-+ Checking cache...\n");

   node = (NODE*)cachelut[crc>>16];
   while (node)
   {
      if (node->crc == crc)
      {
         cache = (CACHE_LUT*)node->data;
         if (/*tex_found[id][node->tmu] == -1 &&
               g_gdp.tile[tile].palette == cache->palette &&
               g_gdp.tile[tile].format == cache->format &&
               g_gdp.tile[tile].size == cache->size &&*/
               gDP.tiles[tile].width == cache->width &&
               gDP.tiles[tile].height == cache->height &&
               flags == cache->flags)
         {
            FRDP (" | | | |- Texture found in cache (tmu=%d).\n", node->tmu);
            tex_found[id][node->tmu] = node->number;
            tex_found[id][node->tmu^1] = node->number;
            return;
         }
      }
      node = node->pNext;
   }
}


int SwapTextureBuffer(void); //forward decl

// Does texture loading after combiner is set
void TexCache(void)
{
   int i, tmu_0, tmu_1;

   if (rdp.tex & 1)
      GetTexInfo (0, rdp.cur_tile);
   if (rdp.tex & 2)
      GetTexInfo (1, rdp.cur_tile+1);

   /* tile 0 in TMU 0, tile 1 in TMU 1: the shader runs the N64's
    * combiner itself (Combine.c), and reads both */
   tmu_0 = 0;
   tmu_1 = 1;
   rdp.t0 = tmu_0;
   rdp.t1 = tmu_1;

   if (rdp.allow_combine)
   {
      grAlphaBlendFunction (cmb.abf1, cmb.abf2, GR_BLEND_ZERO, GR_BLEND_ZERO);
      if (!rdp.tex) //nothing more to do
         return;
   }

   if ((rdp.tex & 1) && tmu_0 < NUM_TMU)
   {
      if (tex_found[0][tmu_0] != -1)
      {
         CACHE_LUT *cache;
         LRDP(" | |- T0 found in cache.\n");
         cache = (CACHE_LUT*)&rdp.cache[0][tex_found[0][0]];
         rdp.cur_cache[0] = cache;
         rdp.cur_cache[0]->last_used = frame_count;
         rdp.cur_cache[0]->uses = 0;
         grTexSource (tmu_0,
               cache->tmem_addr,
               GR_MIPMAPLEVELMASK_BOTH,
               &cache->t_info,
               false);
      }
      else
         LoadTex (0, tmu_0);
   }
   if ((rdp.tex & 2) && tmu_1 < NUM_TMU)
   {
      if (tex_found[1][tmu_1] != -1)
      {
         CACHE_LUT *cache;
         LRDP(" | |- T1 found in cache.\n");
         cache = (CACHE_LUT*)&rdp.cache[0][tex_found[1][0]];
         rdp.cur_cache[1] = cache;
         rdp.cur_cache[1]->last_used = frame_count;
         rdp.cur_cache[1]->uses = 0;
         grTexSource (tmu_1,
               cache->tmem_addr,
               GR_MIPMAPLEVELMASK_BOTH,
               &cache->t_info,
               false);
      }
      else
         LoadTex (1, tmu_1);
   }

   {
      int tmu_v[2];

      tmu_v[0] = tmu_0;
      tmu_v[1] = tmu_1;
      for (i = 0; i < NUM_TMU; i++)
      {
         int tile;
         const int tmu = tmu_v[i];

         if (tmu >= NUM_TMU)
            continue;

         tile = rdp.cur_tile + i;


         if (rdp.cur_cache[i])
         {
            uint32_t mode_s, mode_t;
            int cs, ct, filter;
            if (rdp.force_wrap && !rdp.texrecting)
            {
               cs = g_gdp.tile[tile].cs && g_gdp.tile[tile].sl - g_gdp.tile[tile].sh < 256;
               ct = g_gdp.tile[tile].ct && g_gdp.tile[tile].tl - g_gdp.tile[tile].th < 256;
            }
            else
            {
               cs = (g_gdp.tile[tile].cs || g_gdp.tile[tile].mask_s == 0) &&
                  g_gdp.tile[tile].sl - g_gdp.tile[tile].sh < 256;
               ct = (g_gdp.tile[tile].ct || g_gdp.tile[tile].mask_t == 0) &&
                  g_gdp.tile[tile].tl - g_gdp.tile[tile].th < 256;
            }

            mode_s = GR_TEXTURECLAMP_WRAP;
            mode_t = GR_TEXTURECLAMP_WRAP;

            if (cs)
               mode_s = GR_TEXTURECLAMP_CLAMP;
            else if (g_gdp.tile[tile].ms)
               mode_s = GR_TEXTURECLAMP_MIRROR_EXT;

            if (ct)
               mode_t = GR_TEXTURECLAMP_CLAMP;
            else if (g_gdp.tile[tile].mt)
               mode_t = GR_TEXTURECLAMP_MIRROR_EXT;

            if (settings.filtering == 0)
               filter = (gDP.otherMode.textureFilter == 2)? GR_TEXTUREFILTER_3POINT_LINEAR : GR_TEXTUREFILTER_POINT_SAMPLED;
            else
               filter = (settings.filtering==1)? GR_TEXTUREFILTER_3POINT_LINEAR : (settings.filtering==2)?GR_TEXTUREFILTER_POINT_SAMPLED:GR_TEXTUREFILTER_BILINEAR;
            grTexFilterClampMode (tmu, mode_s, mode_t, filter, filter);
         }
      }
   }
}

// Does the actual texture loading after everything is prepared
static void LoadTex(int id, int tmu)
{
   int lod, aspect, shift, size_max, wid, hei;
   uint32_t size_x, size_y, real_x, real_y, result;
   CACHE_LUT *cache;
   int td = rdp.cur_tile + id;

   if (texinfo[id].width < 0 || texinfo[id].height < 0)
      return;

   // Clear the cache if it's full
   if (rdp.n_cached[tmu] >= MAX_CACHE)
   {
      LRDP("Cache count reached, clearing...\n");
      ClearCache ();
      if (id == 1 && rdp.tex == 3)
         LoadTex (0, rdp.t0);
   }

   // Get this cache object
   cache = &rdp.cache[0][rdp.n_cached[0]];
   rdp.cur_cache[id] = cache;

   //!Hackalert
   //GoldenEye water texture. It has CI format in fact, but the game set it to RGBA
   if ((settings.hacks&hack_GoldenEye) && g_gdp.tile[td].format == G_IM_FMT_RGBA && rdp.tlut_mode == 2 && g_gdp.tile[td].size == G_IM_SIZ_16b)
   {
      g_gdp.tile[td].format = G_IM_FMT_CI;
      g_gdp.tile[td].size = G_IM_SIZ_8b;
   }

   // Set the data
   cache->line       = g_gdp.tile[td].line;
   cache->addr       = rdp.addr[g_gdp.tile[td].tmem];
   cache->crc        = texinfo[id].crc;
   cache->palette    = g_gdp.tile[td].palette;
   cache->width      = gDP.tiles[td].width;
   cache->height     = gDP.tiles[td].height;
   cache->format     = g_gdp.tile[td].format;
   cache->size       = g_gdp.tile[td].size;
   cache->tmem_addr  = voodoo.tmem_ptr[tmu];
   cache->set_by     = rdp.timg.set_by;
   cache->texrecting = rdp.texrecting;
   cache->last_used  = frame_count;
   cache->uses = 0;
   cache->flags = texinfo[id].flags;

   // Add this cache to the list
   AddToList (&cachelut[cache->crc>>16], cache->crc, (uintptr_t)(cache), tmu, rdp.n_cached[tmu]);

   // temporary
   cache->t_info.format = GR_TEXFMT_ARGB_1555;

   // Calculate lod and aspect
   size_x = gDP.tiles[td].width;
   size_y = gDP.tiles[td].height;

   for (shift=0; (1<<shift) < (int)size_x; shift++);
   size_x = 1 << shift;
   for (shift=0; (1<<shift) < (int)size_y; shift++);
   size_y = 1 << shift;

   // Calculate the maximum size
   size_max = MAX(size_x, size_y);
   real_x = size_max;
   real_y = size_max;
   switch (size_max)
   {
      case 1:
         lod = GR_LOD_LOG2_1;
         cache->scale = 256.0f;
         break;
      case 2:
         lod = GR_LOD_LOG2_2;
         cache->scale = 128.0f;
         break;
      case 4:
         lod = GR_LOD_LOG2_4;
         cache->scale = 64.0f;
         break;
      case 8:
         lod = GR_LOD_LOG2_8;
         cache->scale = 32.0f;
         break;
      case 16:
         lod = GR_LOD_LOG2_16;
         cache->scale = 16.0f;
         break;
      case 32:
         lod = GR_LOD_LOG2_32;
         cache->scale = 8.0f;
         break;
      case 64:
         lod = GR_LOD_LOG2_64;
         cache->scale = 4.0f;
         break;
      case 128:
         lod = GR_LOD_LOG2_128;
         cache->scale = 2.0f;
         break;
      case 256:
         lod = GR_LOD_LOG2_256;
         cache->scale = 1.0f;
         break;
      case 512:
         lod = GR_LOD_LOG2_512;
         cache->scale = 0.5f;
         break;
      default:
         lod = GR_LOD_LOG2_1024;
         cache->scale = 0.25f;
         break;
   }

   // Calculate the aspect ratio
   if (size_x >= size_y)
   {
      int ratio = size_x / size_y;
      switch (ratio)
      {
         case 1:
            aspect = GR_ASPECT_LOG2_1x1;
            cache->scale_x = 1.0f;
            cache->scale_y = 1.0f;
            break;
         case 2:
            aspect = GR_ASPECT_LOG2_2x1;
            cache->scale_x = 1.0f;
            cache->scale_y = 0.5f;
            real_y >>= 1;
            break;
         case 4:
            aspect = GR_ASPECT_LOG2_4x1;
            cache->scale_x = 1.0f;
            cache->scale_y = 0.25f;
            real_y >>= 2;
            break;
         default:
            aspect = GR_ASPECT_LOG2_8x1;
            cache->scale_x = 1.0f;
            cache->scale_y = 0.125f;
            real_y >>= 3;
            break;
      }
   }
   else
   {
      int ratio = size_y / size_x;
      switch (ratio)
      {
         case 2:
            aspect = GR_ASPECT_LOG2_1x2;
            cache->scale_x = 0.5f;
            cache->scale_y = 1.0f;
            real_x >>= 1;
            break;
         case 4:
            aspect = GR_ASPECT_LOG2_1x4;
            cache->scale_x = 0.25f;
            cache->scale_y = 1.0f;
            real_x >>= 2;
            break;
         default:
            aspect = GR_ASPECT_LOG2_1x8;
            cache->scale_x = 0.125f;
            cache->scale_y = 1.0f;
            real_x >>= 3;
            break;
      }
   }

   if (real_x != cache->width || real_y != cache->height)
   {
      cache->scale_x *= (float)cache->width / (float)real_x;
      cache->scale_y *= (float)cache->height / (float)real_y;
   }

   cache->splitheight = real_y;
   if (cache->splitheight < texinfo[id].splitheight)
      cache->splitheight = texinfo[id].splitheight;

   // ** Calculate alignment values
   wid = cache->width;
   hei = cache->height;

   cache->c_off = cache->scale * 0.5f;
   if (wid != 1) cache->c_scl_x = cache->scale;
   else cache->c_scl_x = 0.0f;
   if (hei != 1) cache->c_scl_y = cache->scale;
   else cache->c_scl_y = 0.0f;

   result = 0;	// keep =0 so it doesn't mess up on the first split

   texture = (uint8_t *)tex1;

   {
      uint32_t size;
      int min_x, min_y;

      result = load_table[g_gdp.tile[td].size][g_gdp.tile[td].format]
         ((uintptr_t)(texture), (uintptr_t)(g_gdp.tmem)+(g_gdp.tile[td].tmem<<3),
          texinfo[id].wid_64, texinfo[id].height, texinfo[id].line, real_x, td);

      size = HIWORD(result);

      if (g_gdp.tile[td].mask_s != 0)
         min_x = MIN((int)real_x, 1 << g_gdp.tile[td].mask_s);
      else
         min_x = real_x;
      if (g_gdp.tile[td].mask_t != 0)
         min_y  = MIN((int)real_y, 1 << g_gdp.tile[td].mask_t);
      else
         min_y = real_y;

      // Load using mirroring/clamping
      if (min_x > texinfo[id].width && (signed)real_x > texinfo[id].width) /* real_x unsigned just for right shift */
         ClampTex(texture, texinfo[id].width, min_x, real_x, texinfo[id].height, size);

      if (texinfo[id].width < (int)real_x)
      {
         bool cond_true = g_gdp.tile[td].mask_s != 0
            && (real_x > (1U << g_gdp.tile[td].mask_s));

         if (g_gdp.tile[td].ms && cond_true)
         {
            MirrorTex((texture), g_gdp.tile[td].mask_s,
                  real_x, real_x, texinfo[id].height, size);
         }
         else if (cond_true)
         {
            // Horizontal Wrap (like mirror) ** UNTESTED **
            uint8_t *tex        = (uint8_t*)texture;
            uint32_t max_height = texinfo[id].height;
            uint8_t shift_a     = (size == 0) ? 2 : (size == 1) ? 1 : 0;
            uint32_t mask_width = (1 << g_gdp.tile[td].mask_s);
            uint32_t mask_mask  = (mask_width-1) >> shift_a;
            int32_t count       = (real_x - mask_width) >> shift_a;
            int32_t line_full   = real_x << size;
            int32_t line        = line_full - (count << 2);
            uint8_t *start      = (uint8_t*)(tex + (mask_width << size));
            uint32_t *v7        = (uint32_t *)start;

            do
            {
               int v9 = 0;
               do
               {
                  *v7++ = *(uint32_t *)&tex[4 * (mask_mask & v9++)];
               }while ( v9 != count );
               v7 = (uint32_t *)((int8_t*)v7 + line);
               tex += line_full;
            }while (--max_height);
         }
      }

      if (min_y > texinfo[id].height)
      {
         // Vertical Clamp
         int32_t line_full   = real_x << size;
         uint8_t *dst        = (uint8_t*)(texture + texinfo[id].height * line_full);
         uint8_t *const_line = (uint8_t*)(dst - line_full);
         int y               = texinfo[id].height;

         for (; y < min_y; y++)
         {
            memcpy ((void*)dst, (void*)const_line, line_full);
            dst += line_full;
         }
      }

      if (texinfo[id].height < (int)real_y)
      {
         if (g_gdp.tile[td].mt)
         {
            // Vertical Mirror
            if (g_gdp.tile[td].mask_t != 0 && (real_y > (1U << g_gdp.tile[td].mask_t)))
            {
               uint32_t mask_height = (1 << g_gdp.tile[td].mask_t);
               uint32_t mask_mask   = mask_height-1;
               int32_t line_full    = real_x << size;
               uint8_t *dst         = (uint8_t*)(texture + mask_height * line_full);
               uint32_t y           = mask_height;

               for (; y < real_y; y++)
               {
                  if (y & mask_height)   // mirrored
                     memcpy ((void*)dst, (void*)(texture + (mask_mask - (y & mask_mask)) * line_full), line_full);
                  else                   // not mirrored
                     memcpy ((void*)dst, (void*)(texture + (y & mask_mask) * line_full), line_full);

                  dst += line_full;
               }
            }
         }
         else if (g_gdp.tile[td].mask_t != 0 && real_y > (1U << g_gdp.tile[td].mask_t))
         {
            // Vertical Wrap
            uint32_t wrap_size   = size;
            uint32_t mask_height = (1 << g_gdp.tile[td].mask_t);
            uint32_t y           = mask_height;
            uint32_t mask_mask   = mask_height-1;
            int32_t line_full    = real_x << wrap_size;
            uint8_t *dst         = (uint8_t*)(texture + mask_height * line_full);

            if (wrap_size == 1)
               wrap_size = 0;

            for ( ;y < real_y; y++)
            {
               // not mirrored
               memcpy ((void*)dst, (void*)(texture + (y & mask_mask) * (line_full >> wrap_size)), (line_full >> wrap_size));
               dst += line_full;
            }
         }
      }
   }


   cache->t_info.format = LOWORD(result);
   cache->realwidth     = real_x;
   cache->realheight    = real_y;
   cache->lod           = lod;
   cache->aspect        = aspect;

   {
	   uint32_t texture_size, tex_addr;
	   GrTexInfo *t_info;

      /* Load the texture into texture memory */
      t_info                  = (GrTexInfo*)&cache->t_info;
      t_info->data            = texture;
      t_info->smallLodLog2    = lod;
      t_info->largeLodLog2    = lod;
      t_info->aspectRatioLog2 = aspect;

      texture_size            = grTexCalcMemRequired (t_info->largeLodLog2, t_info->aspectRatioLog2, t_info->format);

      /* Check for end of memory (too many textures to fit, clear cache) */
      if (voodoo.tmem_ptr[tmu]+texture_size >= voodoo.tex_max_addr)
      {
         LRDP("Cache size reached, clearing...\n");
         ClearCache ();

         if (id == 1 && rdp.tex == 3)
            LoadTex (0, rdp.t0);

         LoadTex (id, tmu);
         /* Don't continue (already done) */
         return;

      }

      tex_addr = GetTexAddrUMA(tmu, texture_size);
      grTexSource (tmu,
            tex_addr,
            GR_MIPMAPLEVELMASK_BOTH,
            t_info,
            true);
   }
}
