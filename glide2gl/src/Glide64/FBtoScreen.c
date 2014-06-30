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
// Draw N64 frame buffer to screen.
// Created by Gonetz, 2007
//
//****************************************************************


#include "Gfx_1.3.h"
#include "FBtoScreen.h"
#include "TexCache.h"

static int SetupFBtoScreenCombiner(uint32_t texture_size, uint32_t opaque)
{
   int tmu, filter;

   if (voodoo.tmem_ptr[GR_TMU0]+texture_size < voodoo.tex_max_addr)
   {
      tmu = GR_TMU0;
      grTexCombine( GR_TMU1,
            GR_COMBINE_FUNCTION_NONE,
            GR_COMBINE_FACTOR_NONE,
            GR_COMBINE_FUNCTION_NONE,
            GR_COMBINE_FACTOR_NONE,
            FXFALSE,
            FXFALSE );
      grTexCombine( GR_TMU0,
            GR_COMBINE_FUNCTION_LOCAL,
            GR_COMBINE_FACTOR_NONE,
            GR_COMBINE_FUNCTION_LOCAL,
            GR_COMBINE_FACTOR_NONE,
            FXFALSE,
            FXFALSE );
   }
   else
   {
      if (voodoo.tmem_ptr[GR_TMU1]+texture_size >= voodoo.tex_max_addr)
         ClearCache ();
      tmu = GR_TMU1;
      grTexCombine( GR_TMU1,
            GR_COMBINE_FUNCTION_LOCAL,
            GR_COMBINE_FACTOR_NONE,
            GR_COMBINE_FUNCTION_LOCAL,
            GR_COMBINE_FACTOR_NONE,
            FXFALSE,
            FXFALSE );
      grTexCombine( GR_TMU0,
            GR_COMBINE_FUNCTION_SCALE_OTHER,
            GR_COMBINE_FACTOR_ONE,
            GR_COMBINE_FUNCTION_SCALE_OTHER,
            GR_COMBINE_FACTOR_ONE,
            FXFALSE,
            FXFALSE );
   }
   filter = (rdp.filter_mode!=2)?GR_TEXTUREFILTER_POINT_SAMPLED:GR_TEXTUREFILTER_3POINT_LINEAR;

   grTexFilterMode (tmu, filter, filter);
   grTexClampMode (tmu,
         GR_TEXTURECLAMP_CLAMP,
         GR_TEXTURECLAMP_CLAMP);
   grColorCombine (GR_COMBINE_FUNCTION_SCALE_OTHER,
         GR_COMBINE_FACTOR_ONE,
         GR_COMBINE_LOCAL_NONE,
         GR_COMBINE_OTHER_TEXTURE,
         //    GR_COMBINE_OTHER_CONSTANT,
         FXFALSE);
   grAlphaCombine (GR_COMBINE_FUNCTION_SCALE_OTHER,
         GR_COMBINE_FACTOR_ONE,
         GR_COMBINE_LOCAL_NONE,
         GR_COMBINE_OTHER_TEXTURE,
         FXFALSE);
   if (opaque)
   {
      grAlphaTestFunction (GR_CMP_ALWAYS);
      grAlphaBlendFunction( GR_BLEND_ONE,
            GR_BLEND_ZERO,
            GR_BLEND_ONE,
            GR_BLEND_ZERO);
   }
   else
   {
      grAlphaBlendFunction( GR_BLEND_SRC_ALPHA,
            GR_BLEND_ONE_MINUS_SRC_ALPHA,
            GR_BLEND_ONE,
            GR_BLEND_ZERO);
   }
   grDepthBufferFunction (GR_CMP_ALWAYS);
   grCullMode(GR_CULL_DISABLE);
   grDepthMask (FXFALSE);
   rdp.update |= UPDATE_COMBINE | UPDATE_ZBUF_ENABLED | UPDATE_CULL_MODE;
   return tmu;
}

static void DrawRE2Video(FB_TO_SCREEN_INFO *fb_info, float scale)
{
   int i;
   float scale_y, height, ul_x, ul_y, lr_x, lr_y, lr_u, lr_v;

   scale_y = (float)fb_info->width / rdp.vi_height;
   height = settings.scr_res_x / scale_y;
   ul_x = 0.5f;
   ul_y = (settings.scr_res_y - height) / 2.0f;
   lr_y = settings.scr_res_y - ul_y - 1.0f;
   lr_x = settings.scr_res_x - 1.0f;
   lr_u = (fb_info->width - 1) * scale;
   lr_v = (fb_info->height - 1) * scale;

   VERTEX v[4] = {
      { ul_x, ul_y, 1, 1, 0.5f, 0.5f, 0.5f, 0.5f, {0.5f, 0.5f, 0.5f, 0.5f} },
      { lr_x, ul_y, 1, 1, lr_u, 0.5f, lr_u, 0.5f, {lr_u, 0.5f, lr_u, 0.5f} },
      { ul_x, lr_y, 1, 1, 0.5f, lr_v, 0.5f, lr_v, {0.5f, lr_v, 0.5f, lr_v} },
      { lr_x, lr_y, 1, 1, lr_u, lr_v, lr_u, lr_v, {lr_u, lr_v, lr_u, lr_v} }
   };
   
   grDrawTriangle(&v[0], &v[2], &v[1]);
   grDrawTriangle(&v[2], &v[3], &v[1]);
}

static void DrawRE2Video256(FB_TO_SCREEN_INFO *fb_info)
{
   uint32_t h, w, *src, col;
   uint16_t *tex, *dst;
   uint8_t r, g, b;
   int tmu;
   GrTexInfo t_info;

   FRDP("DrawRE2Video256. ul_x=%d, ul_y=%d, lr_x=%d, lr_y=%d, size=%d, addr=%08lx\n", fb_info->ul_x, fb_info->ul_y, fb_info->lr_x, fb_info->lr_y, fb_info->size, fb_info->addr);
   src = (uint32_t*)(gfx_info.RDRAM + fb_info->addr);
   t_info.smallLodLog2 = GR_LOD_LOG2_256;
   t_info.largeLodLog2 = GR_LOD_LOG2_256;
   t_info.aspectRatioLog2 = GR_ASPECT_LOG2_1x1;

   tex = (uint16_t*)texture_buffer;
   dst = (uint16_t*)tex;

   fb_info->height = min(256, fb_info->height);
   for (h = 0; h < fb_info->height; h++)
   {
      for (w = 0; w < 256; w++)
      {
         col = *(src++);
         r = (uint8_t)((col >> 24)&0xFF);
         r = (uint8_t)((float)r / 255.0f * 31.0f);
         g = (uint8_t)((col >> 16)&0xFF);
         g = (uint8_t)((float)g / 255.0f * 63.0f);
         b = (uint8_t)((col >> 8)&0xFF);
         b = (uint8_t)((float)b / 255.0f * 31.0f);
         *(dst++) = (r << 11) | (g << 5) | b;
      }
      src += (fb_info->width - 256);
   }
   t_info.format = GR_TEXFMT_RGB_565;
   t_info.data = tex;
   tmu = SetupFBtoScreenCombiner(grTexCalcMemRequired(t_info.largeLodLog2, t_info.aspectRatioLog2, t_info.format), fb_info->opaque);
   grTexDownloadMipMap (tmu,
         voodoo.tmem_ptr[tmu],
         GR_MIPMAPLEVELMASK_BOTH,
         &t_info);
   grTexSource (tmu,
         voodoo.tmem_ptr[tmu],
         GR_MIPMAPLEVELMASK_BOTH,
         &t_info);
   DrawRE2Video(fb_info, 1.0f);
}

static void DrawFrameBufferToScreen256(FB_TO_SCREEN_INFO *fb_info)
{
  uint32_t w, h, x, y, width, height, width256, height256, tex_size, *src32;
  uint32_t cur_width, cur_height, cur_tail, tex_adr, w_tail, h_tail, bound, c32, idx;
  uint8_t r, g, b, a, *image;
  uint16_t *tex, *src, c;
  float ul_x, ul_y, lr_x, lr_y, lr_u, lr_v;
  int tmu;
  GrTexInfo t_info;

  FRDP("DrawFrameBufferToScreen256. ul_x=%d, ul_y=%d, lr_x=%d, lr_y=%d, size=%d, addr=%08lx\n", fb_info->ul_x, fb_info->ul_y, fb_info->lr_x, fb_info->lr_y, fb_info->size, fb_info->addr);
  width = fb_info->lr_x - fb_info->ul_x + 1;
  height = fb_info->lr_y - fb_info->ul_y + 1;
  image = (uint8_t*)(gfx_info.RDRAM + fb_info->addr);
  width256 = ((width - 1) >> 8) + 1;
  height256 = ((height - 1) >> 8) + 1;
  t_info.smallLodLog2 = t_info.largeLodLog2 = GR_LOD_LOG2_256;
  t_info.aspectRatioLog2 = GR_ASPECT_LOG2_1x1;
  t_info.format = GR_TEXFMT_ARGB_1555;

  tex = (uint16_t*)texture_buffer;
  t_info.data = tex;
  tex_size = grTexCalcMemRequired(t_info.largeLodLog2, t_info.aspectRatioLog2, t_info.format);
  tmu = SetupFBtoScreenCombiner(tex_size * width256 * height256, fb_info->opaque);
  src =   (uint16_t*)(image + fb_info->ul_x + fb_info->ul_y * fb_info->width);
  src32 = (uint32_t*)(image + fb_info->ul_x + fb_info->ul_y * fb_info->width);
  w_tail = width % 256;
  h_tail = height % 256;
  bound = (BMASK + 1) - fb_info->addr;
  bound = fb_info->size == 2 ? (bound >> 1) : (bound >> 2);
  tex_adr = voodoo.tmem_ptr[tmu];

  for (h = 0; h < height256; h++)
  {
    for (w = 0; w < width256; w++)
    {
      uint16_t *dst;

      cur_width = (256 *  (w + 1) < width) ? 256 : w_tail;
      cur_height = (256 * (h + 1) < height) ? 256 : h_tail;
      cur_tail = 256 - cur_width;
      dst = (uint16_t*)tex;

      if (fb_info->size == 2)
      {
        for (y=0; y < cur_height; y++)
        {
          for (x=0; x < cur_width; x++)
          {
            idx = (x + 256 * w + (y + 256 * h) * fb_info->width) ^ 1;
            if (idx >= bound)
              break;
            c = src[idx];
            *(dst++) = (c >> 1) | ((c&1)<<15);
          }
          dst += cur_tail;
        }
      }
      else
      {
        for (y=0; y < cur_height; y++)
        {
          for (x=0; x < cur_width; x++)
          {
            idx = (x+256*w+(y+256*h)*fb_info->width);
            if (idx >= bound)
              break;
            c32 = src32[idx];
            r = (uint8_t)((c32 >> 24) & 0xFF);
            r = (uint8_t)((float)r / 255.0f * 31.0f);
            g = (uint8_t)((c32 >> 16) & 0xFF);
            g = (uint8_t)((float)g / 255.0f * 63.0f);
            b = (uint8_t)((c32 >> 8) & 0xFF);
            b = (uint8_t)((float)b / 255.0f * 31.0f);
            a = (c32 & 0xFF) ? 1 : 0;
            *(dst++) = (a<<15) | (r << 10) | (g << 5) | b;
          }
          dst += cur_tail;
        }
      }
      grTexDownloadMipMap (tmu, tex_adr, GR_MIPMAPLEVELMASK_BOTH, &t_info);
      grTexSource (tmu, tex_adr, GR_MIPMAPLEVELMASK_BOTH, &t_info);
      tex_adr += tex_size;

      ul_x = (fb_info->ul_x + 256 * w)    * rdp.scale_x + rdp.offset_x;
      ul_y = (fb_info->ul_y + 256 * h)    * rdp.scale_y + rdp.offset_y;
      lr_x = (ul_x + (float)(cur_width))  * rdp.scale_x + rdp.offset_x;
      lr_y = (ul_y + (float)(cur_height)) * rdp.scale_y + rdp.offset_y;

      lr_u = (float)(cur_width - 1);
      lr_v = (float)(cur_height - 1);
      // Make the vertices
      VERTEX v[4] = {
        { ul_x, ul_y, 1, 1, 0.5f, 0.5f, 0.5f, 0.5f, {0.5f, 0.5f, 0.5f, 0.5f} },
        { lr_x, ul_y, 1, 1, lr_u, 0.5f, lr_u, 0.5f, {lr_u, 0.5f, lr_u, 0.5f} },
        { ul_x, lr_y, 1, 1, 0.5f, lr_v, 0.5f, lr_v, {0.5f, lr_v, 0.5f, lr_v} },
        { lr_x, lr_y, 1, 1, lr_u, lr_v, lr_u, lr_v, {lr_u, lr_v, lr_u, lr_v} }
      };
      grDrawTriangle(&v[0], &v[2], &v[1]);
      grDrawTriangle(&v[2], &v[3], &v[1]);
    }
  }
}

bool DrawFrameBufferToScreen(FB_TO_SCREEN_INFO *fb_info)
{
   uint32_t x, y, width, height, texwidth;
   uint8_t *image;
   int tmu;
   float scale;
   GrTexInfo t_info;

   if (fb_info->width < 200 || fb_info->size < 2)
      return false;

   width  = fb_info->lr_x - fb_info->ul_x + 1;
   height = fb_info->lr_y - fb_info->ul_y + 1;

   if (width > 512 || height > 512)
   {
      DrawFrameBufferToScreen256(fb_info);
      return true;
   }
   FRDP("DrawFrameBufferToScreen. ul_x=%d, ul_y=%d, lr_x=%d, lr_y=%d, size=%d, addr=%08lx\n", fb_info->ul_x, fb_info->ul_y, fb_info->lr_x, fb_info->lr_y, fb_info->size, fb_info->addr);

   image = (uint8_t*)(gfx_info.RDRAM + fb_info->addr);

   texwidth = 512;
   scale = 0.5f;
   t_info.smallLodLog2 = t_info.largeLodLog2 = GR_LOD_LOG2_512;
   t_info.aspectRatioLog2 = GR_ASPECT_LOG2_1x1;

   if (width <= 256)
   {
      texwidth = 256;
      scale = 1.0f;
      t_info.smallLodLog2 = t_info.largeLodLog2 = GR_LOD_LOG2_256;
   }

   if (height <= (texwidth>>1))
      t_info.aspectRatioLog2 = GR_ASPECT_LOG2_2x1;

   if (fb_info->size == 2)
   {
      uint16_t *tex, *dst, *src, c;
      uint32_t idx, bound;
      bool empty;

      tex = (uint16_t*)texture_buffer;
      dst = (uint16_t*)tex;
      src = (uint16_t*)(image + fb_info->ul_x + fb_info->ul_y * fb_info->width);

      bound = (BMASK+1 - fb_info->addr) >> 1;
      empty = true;
      for (y = 0; y < height; y++)
      {
         for (x = 0; x < width; x++)
         {
            idx = (x + y * fb_info->width) ^ 1;
            if (idx >= bound)
               break;
            c = src[idx];
            if (c) empty = false;
            *(dst++) = (c >> 1) | ((c & 1) << 15);
         }
         dst += texwidth-width;
      }
      if (empty)
         return false;
      t_info.format = GR_TEXFMT_ARGB_1555;
      t_info.data = tex;
   }
   else
   {
      uint32_t *tex, *dst, *src, col, idx, bound;

      tex = (uint32_t*)texture_buffer;
      dst = (uint32_t*)tex;
      src = (uint32_t*)(image + fb_info->ul_x + fb_info->ul_y * fb_info->width);
      bound = (BMASK + 1 - fb_info->addr) >> 2;

      for (y = 0; y < height; y++)
      {
         for (x = 0; x < width; x++)
         {
            idx = x + y * fb_info->width;
            if (idx >= bound)
               break;
            col = src[idx];
            *(dst++) = (col >> 8) | 0xFF000000;
         }
         dst += texwidth-width;
      }
      t_info.format = GR_TEXFMT_ARGB_8888;
      t_info.data = tex;
   }

   tmu = SetupFBtoScreenCombiner(grTexCalcMemRequired(t_info.largeLodLog2, t_info.aspectRatioLog2, t_info.format), fb_info->opaque);
   grTexDownloadMipMap (tmu,
         voodoo.tmem_ptr[tmu],
         GR_MIPMAPLEVELMASK_BOTH,
         &t_info);
   grTexSource (tmu,
         voodoo.tmem_ptr[tmu],
         GR_MIPMAPLEVELMASK_BOTH,
         &t_info);

   if (settings.hacks&hack_RE2)
      DrawRE2Video(fb_info, scale);
   else
   {
      float ul_x, ul_y, lr_x, lr_y, lr_u, lr_v;

      ul_x = fb_info->ul_x * rdp.scale_x + rdp.offset_x;
      ul_y = fb_info->ul_y * rdp.scale_y + rdp.offset_y;
      lr_x = fb_info->lr_x * rdp.scale_x + rdp.offset_x;
      lr_y = fb_info->lr_y * rdp.scale_y + rdp.offset_y;
      lr_u = (width  - 1) * scale;
      lr_v = (height - 1) * scale;

      // Make the vertices
      VERTEX v[4] = {
         { ul_x, ul_y, 1, 1, 0.5f, 0.5f, 0.5f, 0.5f, {0.5f, 0.5f, 0.5f, 0.5f} },
         { lr_x, ul_y, 1, 1, lr_u, 0.5f, lr_u, 0.5f, {lr_u, 0.5f, lr_u, 0.5f} },
         { ul_x, lr_y, 1, 1, 0.5f, lr_v, 0.5f, lr_v, {0.5f, lr_v, 0.5f, lr_v} },
         { lr_x, lr_y, 1, 1, lr_u, lr_v, lr_u, lr_v, {lr_u, lr_v, lr_u, lr_v} }
      };

      grDrawTriangle(&v[0], &v[2], &v[1]);
      grDrawTriangle(&v[2], &v[3], &v[1]);
   }
   return true;
}

static void DrawDepthBufferToScreen256(FB_TO_SCREEN_INFO *fb_info)
{
   uint32_t h, w, x, y, width, height, width256, height256, tex_size;
   uint32_t cur_width, cur_height, cur_tail, w_tail, h_tail, tex_adr;
   uint16_t *tex, *src;
   uint8_t *image;
   int tmu;
   GrTexInfo t_info;

   FRDP("DrawDepthBufferToScreen256. ul_x=%d, ul_y=%d, lr_x=%d, lr_y=%d, size=%d, addr=%08lx\n", fb_info->ul_x, fb_info->ul_y, fb_info->lr_x, fb_info->lr_y, fb_info->size, fb_info->addr);
   width = fb_info->lr_x - fb_info->ul_x + 1;
   height = fb_info->lr_y - fb_info->ul_y + 1;
   image = (uint8_t*)(gfx_info.RDRAM + fb_info->addr);
   width256 = ((width-1) >> 8) + 1;
   height256 = ((height-1) >> 8) + 1;
   t_info.smallLodLog2 = t_info.largeLodLog2 = GR_LOD_LOG2_256;
   t_info.aspectRatioLog2 = GR_ASPECT_LOG2_1x1;
   t_info.format = GR_TEXFMT_ALPHA_INTENSITY_88;
   tex = (uint16_t*)texture_buffer;
   t_info.data = tex;
   tex_size = grTexCalcMemRequired(t_info.largeLodLog2, t_info.aspectRatioLog2, t_info.format);
   tmu = SetupFBtoScreenCombiner(tex_size*width256*height256, fb_info->opaque);
   grConstantColorValue (rdp.fog_color);
   grColorCombine (GR_COMBINE_FUNCTION_SCALE_OTHER,
         GR_COMBINE_FACTOR_ONE,
         GR_COMBINE_LOCAL_NONE,
         GR_COMBINE_OTHER_CONSTANT,
         FXFALSE);
   src = (uint16_t*)(image + fb_info->ul_x + fb_info->ul_y * fb_info->width);
   w_tail = width % 256;
   h_tail = height % 256;
   tex_adr = voodoo.tmem_ptr[tmu];

   for (h = 0; h < height256; h++)
   {
      for (w = 0; w < width256; w++)
      {
         uint16_t *dst;
         float ul_x, ul_y, lr_x, lr_y, lr_u, lr_v;

         cur_width = (256 * (w + 1) < width) ? 256 : w_tail;
         cur_height = (256 * (h + 1) < height) ? 256 : h_tail;
         cur_tail = 256 - cur_width;
         dst = (uint16_t*)tex;

         for (y=0; y < cur_height; y++)
         {
            for (x=0; x < cur_width; x++)
               *(dst++) = rdp.pal_8[src[(x + 256 * w + (y + 256 * h) * fb_info->width) ^ 1]>>8];
            dst += cur_tail;
         }
         grTexDownloadMipMap (tmu, tex_adr, GR_MIPMAPLEVELMASK_BOTH, &t_info);
         grTexSource (tmu, tex_adr, GR_MIPMAPLEVELMASK_BOTH, &t_info);
         tex_adr += tex_size;
         ul_x = (float)(fb_info->ul_x + 256 * w);
         ul_y = (float)(fb_info->ul_y + 256 * h);
         lr_x = (ul_x + (float)(cur_width)) * rdp.scale_x + rdp.offset_x;
         lr_y = (ul_y + (float)(cur_height)) * rdp.scale_y + rdp.offset_y;
         ul_x = ul_x * rdp.scale_x + rdp.offset_x;
         ul_y = ul_y * rdp.scale_y + rdp.offset_y;
         lr_u = (float)(cur_width-1);
         lr_v = (float)(cur_height-1);
         // Make the vertices
         VERTEX v[4] = {
            { ul_x, ul_y, 1, 1, 0.5f, 0.5f, 0.5f, 0.5f, {0.5f, 0.5f, 0.5f, 0.5f} },
            { lr_x, ul_y, 1, 1, lr_u, 0.5f, lr_u, 0.5f, {lr_u, 0.5f, lr_u, 0.5f} },
            { ul_x, lr_y, 1, 1, 0.5f, lr_v, 0.5f, lr_v, {0.5f, lr_v, 0.5f, lr_v} },
            { lr_x, lr_y, 1, 1, lr_u, lr_v, lr_u, lr_v, {lr_u, lr_v, lr_u, lr_v} }
         };
         grDrawTriangle(&v[0], &v[2], &v[1]);
         grDrawTriangle(&v[2], &v[3], &v[1]);
      }
   }
}

#ifdef HAVE_HWFBE
static void DrawHiresDepthBufferToScreen(FB_TO_SCREEN_INFO *fb_info)
{
   float scale, ul_x, ul_y, lr_x, lr_y, ul_u, ul_v, lr_u, lr_v;
   GrTexInfo t_info;
   GrLOD_t LOD;

   FRDP("DrawHiresDepthBufferToScreen. ul_x=%d, ul_y=%d, lr_x=%d, lr_y=%d, size=%d, addr=%08lx\n", fb_info->ul_x, fb_info->ul_y, fb_info->lr_x, fb_info->lr_y, fb_info->size, fb_info->addr);
   scale = 0.25f;
   LOD = GR_LOD_LOG2_1024;

   if (settings.scr_res_x > 1024)
   {
      scale = 0.125f;
      LOD = GR_LOD_LOG2_2048;
   }
   t_info.format = GR_TEXFMT_ALPHA_INTENSITY_88;
   t_info.smallLodLog2 = t_info.largeLodLog2 = LOD;
   t_info.aspectRatioLog2 = GR_ASPECT_LOG2_1x1;
   grConstantColorValue (rdp.fog_color);
   grColorCombine (GR_COMBINE_FUNCTION_LOCAL,
         GR_COMBINE_FACTOR_NONE,
         GR_COMBINE_LOCAL_CONSTANT,
         GR_COMBINE_OTHER_NONE,
         FXFALSE);
   grAlphaCombine (GR_COMBINE_FUNCTION_SCALE_OTHER,
         GR_COMBINE_FACTOR_ONE,
         GR_COMBINE_LOCAL_NONE,
         GR_COMBINE_OTHER_TEXTURE,
         FXFALSE);
   grAlphaBlendFunction( GR_BLEND_SRC_ALPHA,
         GR_BLEND_ONE_MINUS_SRC_ALPHA,
         GR_BLEND_ONE,
         GR_BLEND_ZERO);
   grDepthBufferFunction (GR_CMP_ALWAYS);
   grDepthMask (FXFALSE);
   grCullMode (GR_CULL_DISABLE);
   grTexCombine( GR_TMU1,
         GR_COMBINE_FUNCTION_NONE,
         GR_COMBINE_FACTOR_NONE,
         GR_COMBINE_FUNCTION_NONE,
         GR_COMBINE_FACTOR_NONE,
         FXFALSE,
         FXFALSE );
   grTexCombine( GR_TMU0,
         GR_COMBINE_FUNCTION_LOCAL,
         GR_COMBINE_FACTOR_NONE,
         GR_COMBINE_FUNCTION_LOCAL,
         GR_COMBINE_FACTOR_NONE,
         FXFALSE,
         FXFALSE);
   //  grAuxBufferExt( GR_BUFFER_AUXBUFFER );
   grTexSource( rdp.texbufs[0].tmu, rdp.texbufs[0].begin, GR_MIPMAPLEVELMASK_BOTH, &(t_info) );
   ul_x = (float)rdp.scissor.ul_x;
   ul_y = (float)rdp.scissor.ul_y;
   lr_x = (float)rdp.scissor.lr_x;
   lr_y = (float)rdp.scissor.lr_y;
   ul_u = (float)rdp.scissor.ul_x * scale;
   ul_v = (float)rdp.scissor.ul_y * scale;
   lr_u = (float)rdp.scissor.lr_x * scale;
   lr_v = (float)rdp.scissor.lr_y * scale;

   // Make the vertices
   VERTEX v[4] = {
      { ul_x, ul_y, 1, 1, ul_u, ul_v, ul_u, ul_v, {ul_u, ul_v, ul_u, ul_v} },
      { lr_x, ul_y, 1, 1, lr_u, ul_v, lr_u, ul_v, {lr_u, ul_v, lr_u, ul_v} },
      { ul_x, lr_y, 1, 1, ul_u, lr_v, ul_u, lr_v, {ul_u, lr_v, ul_u, lr_v} },
      { lr_x, lr_y, 1, 1, lr_u, lr_v, lr_u, lr_v, {lr_u, lr_v, lr_u, lr_v} }
   };
   
   grDrawTriangle(&v[0], &v[2], &v[1]);
   grDrawTriangle(&v[2], &v[3], &v[1]);
   //  grAuxBufferExt( GR_BUFFER_TEXTUREAUXBUFFER_EXT );
   rdp.update |= UPDATE_COMBINE | UPDATE_ZBUF_ENABLED | UPDATE_CULL_MODE;
}
#endif

void DrawDepthBufferToScreen(FB_TO_SCREEN_INFO *fb_info)
{
   uint32_t width, height, texwidth, x, y;
   uint8_t *image;
   int tmu;
   uint16_t *tex, *dst, *src;
   float scale, ul_x, ul_y, lr_x, lr_y, lr_u, lr_v, zero;
   GrTexInfo t_info;

   width  = fb_info->lr_x - fb_info->ul_x + 1;
   height = fb_info->lr_y - fb_info->ul_y + 1;
   if (width > 512)
   {
      DrawDepthBufferToScreen256(fb_info);
      return;
   }
#ifdef HAVE_HWFBE
   if (fb_hwfbe_enabled)
   {
      DrawHiresDepthBufferToScreen(fb_info);
      return;
   }
#endif
   FRDP("DrawDepthBufferToScreen. ul_x=%d, ul_y=%d, lr_x=%d, lr_y=%d, size=%d, addr=%08lx\n", fb_info->ul_x, fb_info->ul_y, fb_info->lr_x, fb_info->lr_y, fb_info->size, fb_info->addr);

   image = (uint8_t*)(gfx_info.RDRAM + fb_info->addr);
   texwidth = 512;
   scale = 0.5f;
   t_info.smallLodLog2 = t_info.largeLodLog2 = GR_LOD_LOG2_512;
   t_info.aspectRatioLog2 = GR_ASPECT_LOG2_1x1;

   if (width <= 256)
   {
      texwidth = 256;
      scale = 1.0f;
      t_info.smallLodLog2 = t_info.largeLodLog2 = GR_LOD_LOG2_256;
   }

   if (height <= (texwidth>>1))
      t_info.aspectRatioLog2 = GR_ASPECT_LOG2_2x1;

   tex = (uint16_t*)texture_buffer;
   dst = (uint16_t*)tex;
   src = (uint16_t*)(image + fb_info->ul_x + fb_info->ul_y * fb_info->width);

   for (y=0; y < height; y++)
   {
      for (x = 0; x < width; x++)
         *(dst++) = rdp.pal_8[src[(x+y*fb_info->width)^1]>>8];
      dst += texwidth-width;
   }
   t_info.format = GR_TEXFMT_ALPHA_INTENSITY_88;
   t_info.data = tex;

   tmu = SetupFBtoScreenCombiner(grTexCalcMemRequired(t_info.largeLodLog2, t_info.aspectRatioLog2, t_info.format), fb_info->opaque);
   grConstantColorValue (rdp.fog_color);
   grColorCombine (GR_COMBINE_FUNCTION_SCALE_OTHER,
         GR_COMBINE_FACTOR_ONE,
         GR_COMBINE_LOCAL_NONE,
         GR_COMBINE_OTHER_CONSTANT,
         FXFALSE);
   grTexDownloadMipMap (tmu,
         voodoo.tmem_ptr[tmu],
         GR_MIPMAPLEVELMASK_BOTH,
         &t_info);
   grTexSource (tmu,
         voodoo.tmem_ptr[tmu],
         GR_MIPMAPLEVELMASK_BOTH,
         &t_info);
   ul_x = fb_info->ul_x * rdp.scale_x + rdp.offset_x;
   ul_y = fb_info->ul_y * rdp.scale_y + rdp.offset_y;
   lr_x = fb_info->lr_x * rdp.scale_x + rdp.offset_x;
   lr_y = fb_info->lr_y * rdp.scale_y + rdp.offset_y;
   lr_u = (width - 1) * scale;
   lr_v = (height - 1) * scale;
   zero = scale * 0.5f;

   // Make the vertices
   VERTEX v[4] = {
      { ul_x, ul_y, 1, 1, zero, zero, zero, zero, {zero, zero, zero, zero} },
      { lr_x, ul_y, 1, 1, lr_u, zero, lr_u, zero, {lr_u, zero, lr_u, zero} },
      { ul_x, lr_y, 1, 1, zero, lr_v, zero, lr_v, {zero, lr_v, zero, lr_v} },
      { lr_x, lr_y, 1, 1, lr_u, lr_v, lr_u, lr_v, {lr_u, lr_v, lr_u, lr_v} }
   };

   grDrawTriangle(&v[0], &v[2], &v[1]);
   grDrawTriangle(&v[2], &v[3], &v[1]);
}
