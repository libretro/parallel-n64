
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

#include <math.h>
#include "Gfx_1.3.h"
#include "Util.h"
#include "Combine.h"
#include "3dmath.h"
#include "TexCache.h"
#include "DepthBufferRender.h"
#include "GBI.h"

extern int dzdx;
extern int deltaZ;
extern VERTEX **org_vtx;

typedef struct
{
   float d;
   float x;
   float y;
} LineEquationType;

typedef struct
{
   unsigned int	c2_m2b:2;
   unsigned int	c1_m2b:2;
   unsigned int	c2_m2a:2;
   unsigned int	c1_m2a:2;
   unsigned int	c2_m1b:2;
   unsigned int	c1_m1b:2;
   unsigned int	c2_m1a:2;
   unsigned int	c1_m1a:2;
} rdp_blender_setting;

#define interp2p(a, b, r)  (a + (b - a) * r)
#define interp3p(a, b, c, r1, r2) ((a)+(((b)+((c)-(b))*(r2))-(a))*(r1))
#define EvaLine(li, x, y) ((li->x) * (x) + (li->y) * (y) + (li->d))

static INLINE void InterpolateColors(VERTEX *dest, float percent, VERTEX *first, VERTEX *second)
{
   dest->r = (uint8_t)(first->r + percent*(second->r - first->r));
   dest->g = (uint8_t)(first->g + percent*(second->g - first->g));
   dest->b = (uint8_t)(first->b + percent*(second->b - first->b));
   dest->a = (uint8_t)(first->a + percent*(second->a - first->a));
   dest->f = ( float )(first->f + percent*(second->f - first->f));
}

void apply_shade_mods (VERTEX *v)
{
   if (rdp.cmb_flags)
   {
      if (v->shade_mod == 0)
         v->color_backup = *(uint32_t*)(&(v->b));
      else
         *(uint32_t*)(&(v->b)) = v->color_backup;

      if (rdp.cmb_flags & CMB_SET)
      {
         v->r = (uint8_t)(255.0f * get_float_color_clamped(rdp.col[0]));
         v->g = (uint8_t)(255.0f * get_float_color_clamped(rdp.col[1]));
         v->b = (uint8_t)(255.0f * get_float_color_clamped(rdp.col[2]));
      }

      if (rdp.cmb_flags & CMB_A_SET)
         v->a = (uint8_t)(255.0f * get_float_color_clamped(rdp.col[3]));

      if (rdp.cmb_flags & CMB_SETSHADE_SHADEALPHA)
         v->r = v->g = v->b = v->a;

      if (rdp.cmb_flags & CMB_MULT_OWN_ALPHA)
      {
         float percent = v->a / 255.0f;
         v->r = (uint8_t)(v->r * percent);
         v->g = (uint8_t)(v->g * percent);
         v->b = (uint8_t)(v->b * percent);
      }

      if (rdp.cmb_flags & CMB_MULT)
      {
         v->r = (uint8_t)(v->r * get_float_color_clamped(rdp.col[0]));
         v->g = (uint8_t)(v->g * get_float_color_clamped(rdp.col[1]));
         v->b = (uint8_t)(v->b * get_float_color_clamped(rdp.col[2]));
      }

      if (rdp.cmb_flags & CMB_A_MULT)
         v->a = (uint8_t)(v->a * get_float_color_clamped(rdp.col[3]));
      if (rdp.cmb_flags & CMB_SUB)
      {
         int r = v->r - (int)(255.0f * rdp.coladd[0]);
         int g = v->g - (int)(255.0f * rdp.coladd[1]);
         int b = v->b - (int)(255.0f * rdp.coladd[2]);
         if (r < 0) r = 0;
         if (g < 0) g = 0;
         if (b < 0) b = 0;
         v->r = (uint8_t)r;
         v->g = (uint8_t)g;
         v->b = (uint8_t)b;
      }
      if (rdp.cmb_flags & CMB_A_SUB)
      {
         int a = v->a - (int)(255.0f * rdp.coladd[3]);
         if (a < 0) a = 0;
         v->a = (uint8_t)a;
      }
      if (rdp.cmb_flags & CMB_ADD)
      {
         int r = v->r + (int)(255.0f * rdp.coladd[0]);
         int g = v->g + (int)(255.0f * rdp.coladd[1]);
         int b = v->b + (int)(255.0f * rdp.coladd[2]);
         if (r > 255) r = 255;
         if (g > 255) g = 255;
         if (b > 255) b = 255;
         v->r = (uint8_t)r;
         v->g = (uint8_t)g;
         v->b = (uint8_t)b;
      }
      if (rdp.cmb_flags & CMB_A_ADD)
      {
         int a = v->a + (int)(255.0f * rdp.coladd[3]);
         if (a > 255) a = 255;
         v->a = (uint8_t)a;
      }
      if (rdp.cmb_flags & CMB_COL_SUB_OWN)
      {
         int r = (uint8_t)(255.0f * rdp.coladd[0]) - v->r;
         int g = (uint8_t)(255.0f * rdp.coladd[1]) - v->g;
         int b = (uint8_t)(255.0f * rdp.coladd[2]) - v->b;
         if (r < 0) r = 0;
         if (g < 0) g = 0;
         if (b < 0) b = 0;
         v->r = (uint8_t)r;
         v->g = (uint8_t)g;
         v->b = (uint8_t)b;
      }
      v->shade_mod = cmb.shade_mod_hash;
   }

   if (rdp.cmb_flags_2 & CMB_INTER)
   {
      v->r = (uint8_t)(rdp.col_2[0] * rdp.shade_factor * 255.0f + v->r * (1.0f - rdp.shade_factor));
      v->g = (uint8_t)(rdp.col_2[1] * rdp.shade_factor * 255.0f + v->g * (1.0f - rdp.shade_factor));
      v->b = (uint8_t)(rdp.col_2[2] * rdp.shade_factor * 255.0f + v->b * (1.0f - rdp.shade_factor));
      v->shade_mod = cmb.shade_mod_hash;
   }
}


static void Create1LineEquation(LineEquationType *l, VERTEX *v1, VERTEX *v2, VERTEX *v3)
{
   float x = v3->sx;
   float y = v3->sy;

   // Line between (x1,y1) to (x2,y2)
   l->x = v2->sy-v1->sy;
   l->y = v1->sx-v2->sx;
   l->d = -(l->x * v2->sx+ (l->y) * v2->sy);

   if (EvaLine(l,x,y) * v3->oow < 0)
   {
      l->x = -l->x;
      l->y = -l->y;
      l->d = -l->d;
   }
}

static void InterpolateColors3(VERTEX *v1, VERTEX *v2, VERTEX *v3, VERTEX *out)
{
   LineEquationType line;
   float aDot, bDot, scale1, tx, ty, s1, s2, den, w;
   Create1LineEquation(&line, v2, v3, v1);

   aDot = (out->x * line.x + out->y * line.y);
   bDot = (v1->sx * line.x + v1->sy * line.y);
   scale1 = ( -line.d - aDot) / ( bDot - aDot );
   tx = out->x + scale1 * (v1->sx - out->x);
   ty = out->y + scale1 * (v1->sy - out->y);
   s1 = 101.0;
   s2 = 101.0;
   den = tx - v1->sx;

   if (fabs(den) > 1.0)
      s1 = (out->x-v1->sx)/den;
   if (s1 > 100.0f)
      s1 = (out->y-v1->sy)/(ty-v1->sy);

   den = v3->sx - v2->sx;
   if (fabs(den) > 1.0)
      s2 = (tx-v2->sx)/den;
   if (s2 > 100.0f)
      s2 =(ty-v2->sy)/(v3->sy-v2->sy);

   w = 1.0f / interp3p(v1->oow,v2->oow,v3->oow,s1,s2);

   out->r = (uint8_t)
      (w * interp3p(v1->r*v1->oow,v2->r*v2->oow,v3->r*v3->oow,s1,s2));
   out->g = (uint8_t)
      (w * interp3p(v1->g*v1->oow,v2->g*v2->oow,v3->g*v3->oow,s1,s2));
   out->b = (uint8_t)
      (w * interp3p(v1->b*v1->oow,v2->b*v2->oow,v3->b*v3->oow,s1,s2));
   out->a = (uint8_t)
      (w * interp3p(v1->a*v1->oow,v2->a*v2->oow,v3->a*v3->oow,s1,s2));
   out->f = interp3p(v1->f*v1->oow,v2->f*v2->oow,v3->f*v3->oow,s1,s2) * w;
}

static void InterpolateColors2(VERTEX *va, VERTEX *vb, VERTEX *res, float percent)
{
   float w, ba, bb, ga, gb, ra, rb, aa, ab, fa, fb;
   w = 1.0f/(va->oow + (vb->oow-va->oow) * percent);
   //   res->oow = va->oow + (vb->oow-va->oow) * percent;
   //   res->q = res->oow;
   ba = va->b * va->oow;
   bb = vb->b * vb->oow;
   res->b = (uint8_t)(interp2p(ba, bb, percent) * w);
   ga = va->g * va->oow;
   gb = vb->g * vb->oow;
   res->g = (uint8_t)(interp2p(ga, gb, percent) * w);
   ra = va->r * va->oow;
   rb = vb->r * vb->oow;
   res->r = (uint8_t)(interp2p(ra, rb, percent) * w);
   aa = va->a * va->oow;
   ab = vb->a * vb->oow;
   res->a = (uint8_t)(interp2p(aa, ab, percent) * w);
   fa = va->f * va->oow;
   fb = vb->f * vb->oow;
   res->f = interp2p(fa, fb, percent) * w;
}

static INLINE void CalculateLODValues(VERTEX *v, int32_t i, int32_t j, float *lodFactor, float s_scale, float t_scale)
{
   float deltaS, deltaT, deltaTexels, deltaPixels, deltaX, deltaY;
   deltaS = (v[j].u0/v[j].q - v[i].u0/v[i].q) * s_scale;
   deltaT = (v[j].v0/v[j].q - v[i].v0/v[i].q) * t_scale;
   deltaTexels = sqrtf( deltaS * deltaS + deltaT * deltaT );

   deltaX = (v[j].x - v[i].x)/rdp.scale_x;
   deltaY = (v[j].y - v[i].y)/rdp.scale_y;
   deltaPixels = sqrtf( deltaX * deltaX + deltaY * deltaY );

   *lodFactor += deltaTexels / deltaPixels;
}

static void CalculateLOD(VERTEX *v, int n, uint32_t lodmode)
{
   float lodFactor, intptr, s_scale, t_scale, lod_fraction, detailmax;
   int i, j, ilod, lod_tile;
   s_scale = rdp.tiles[rdp.cur_tile].width / 255.0f;
   t_scale = rdp.tiles[rdp.cur_tile].height / 255.0f;
   lodFactor = 0;

   if (lodmode == G_TL_LOD)
   {
      n = 1;
      j = 1;
   }

   for (i = 0; i < n; i++)
   {
      if (lodmode == G_TL_TILE)
         j = (i < n-1) ? (i + 1) : 0;
      CalculateLODValues(v, i, j, &lodFactor, s_scale, t_scale);
   }

   if (lodmode == G_TL_TILE)
      lodFactor = lodFactor / n; // Divide by n (n edges) to find average

   ilod = (int)lodFactor;
   lod_tile = min((int)(log10f((float)ilod)/log10f(2.0f)), rdp.cur_tile + rdp.mipmap_level);
   lod_fraction = 1.0f;
   detailmax = 1.0f - lod_fraction;

   if (lod_tile < rdp.cur_tile + rdp.mipmap_level)
      lod_fraction = max((float)modff(lodFactor / pow(2.,lod_tile),&intptr), rdp.prim_lodmin / 255.0f);

   if (cmb.dc0_detailmax < 0.5f)
      detailmax = lod_fraction;

   grTexDetailControl (GR_TMU0, cmb.dc0_lodbias, cmb.dc0_detailscale, detailmax);
   grTexDetailControl (GR_TMU1, cmb.dc1_lodbias, cmb.dc1_detailscale, detailmax);
}

float ScaleZ(float z)
{
   if (settings.n64_z_scale)
   {
      int iz = (int)(z*8.0f+0.5f);
      if (iz < 0)
         iz = 0;
      else if (iz >= ZLUT_SIZE)
         iz = ZLUT_SIZE - 1;
      return (float)zLUT[iz];
   }
   if (z  < 0.0f)
      return 0.0f;
   z *= 1.9f;
   if (z > 65535.0f)
      return 65535.0f;
   return z;
}

static void DepthBuffer(VERTEX * vtx, int n)
{
   int i;
   struct vertexi v[12];

   if ( gfx_plugin_accuracy < 3)
       return;

   if (fb_depth_render_enabled && dzdx && (rdp.flags & ZBUF_UPDATE))
   {
       if (rdp.u_cull_mode == 1) //cull front
       {
          for (i = 0; i < n; i++)
          {
             v[i].x = (int)((vtx[n-i-1].x-rdp.offset_x) / rdp.scale_x * 65536.0);
             v[i].y = (int)((vtx[n-i-1].y-rdp.offset_y) / rdp.scale_y * 65536.0);
             v[i].z = (int)(vtx[n-i-1].z * 65536.0);
          }
       }
       else
       {
          for (i = 0; i < n; i++)
          {
             v[i].x = (int)((vtx[i].x-rdp.offset_x) / rdp.scale_x * 65536.0);
             v[i].y = (int)((vtx[i].y-rdp.offset_y) / rdp.scale_y * 65536.0);
             v[i].z = (int)(vtx[i].z * 65536.0);
          }
       }
       Rasterize(v, n, dzdx);
   }

   for (i = 0; i < n; i++)
      vtx[i].z = ScaleZ(vtx[i].z);
}

#define clip_tri_uv(first, second, index, percent) \
   rdp.vtxbuf[index].u0 = first->u0 + (second->u0 - first->u0) * percent; \
   rdp.vtxbuf[index].v0 = first->v0 + (second->v0 - first->v0) * percent; \
   rdp.vtxbuf[index].u1 = first->u1 + (second->u1 - first->u1) * percent; \
   rdp.vtxbuf[index].v1 = first->v1 + (second->v1 - first->v1) * percent

#define clip_tri_interp_colors(first, second, index, percent, val, interpolate_colors) \
   if (interpolate_colors) \
      InterpolateColors(&rdp.vtxbuf[index++], percent, first, second); \
   else \
      rdp.vtxbuf[index++].number = first->number | second->number | val

static void clip_tri(int interpolate_colors)
{
   int i,j,index,n=rdp.n_global;
   float percent;

   // Check which clipping is needed
   if (rdp.clip & CLIP_XMAX) // right of the screen
   {
      // Swap vertex buffers
      VERTEX *tmp = rdp.vtxbuf2;
      rdp.vtxbuf2 = rdp.vtxbuf;
      rdp.vtxbuf = tmp;
      rdp.vtx_buffer ^= 1;
      index = 0;

      // Check the vertices for clipping
      for (i=0; i<n; i++)
      {
         VERTEX *first, *second;
         j = i+1;
         if (j == n)
            j = 0;
         first = (VERTEX*)&rdp.vtxbuf2[i];
         second = (VERTEX*)&rdp.vtxbuf2[j];

         if (first->x <= rdp.clip_max_x)
         {
            if (second->x <= rdp.clip_max_x)   // Both are in, save the last one
            {
               rdp.vtxbuf[index++] = rdp.vtxbuf2[j];
            }
            else      // First is in, second is out, save intersection
            {
               percent = (rdp.clip_max_x - first->x) / (second->x - first->x);
               rdp.vtxbuf[index].x = rdp.clip_max_x;
               rdp.vtxbuf[index].y = first->y + (second->y - first->y) * percent;
               rdp.vtxbuf[index].z = first->z + (second->z - first->z) * percent;
               rdp.vtxbuf[index].q = first->q + (second->q - first->q) * percent;
               clip_tri_uv(first, second, index, percent);
               clip_tri_interp_colors(first, second, index, percent, 8, interpolate_colors);
            }
         }
         else
         {
            if (second->x <= rdp.clip_max_x) // First is out, second is in, save intersection & in point
            {
               percent = (rdp.clip_max_x - second->x) / (first->x - second->x);
               rdp.vtxbuf[index].x = rdp.clip_max_x;
               rdp.vtxbuf[index].y = second->y + (first->y - second->y) * percent;
               rdp.vtxbuf[index].z = second->z + (first->z - second->z) * percent;
               rdp.vtxbuf[index].q = second->q + (first->q - second->q) * percent;
               clip_tri_uv(second, first, index, percent);
               clip_tri_interp_colors(second, first, index, percent, 8, interpolate_colors);

               // Save the in point
               rdp.vtxbuf[index++] = rdp.vtxbuf2[j];
            }
         }
      }
      n = index;
   }

   if (rdp.clip & CLIP_XMIN) // left of the screen
   {
      // Swap vertex buffers
      VERTEX *tmp = rdp.vtxbuf2;
      rdp.vtxbuf2 = rdp.vtxbuf;
      rdp.vtxbuf = tmp;
      rdp.vtx_buffer ^= 1;
      index = 0;

      // Check the vertices for clipping
      for (i=0; i<n; i++)
      {
         VERTEX *first, *second;
         j = i+1;
         if (j == n)
            j = 0;
         first = (VERTEX*)&rdp.vtxbuf2[i];
         second = (VERTEX*)&rdp.vtxbuf2[j];

         if (first->x >= rdp.clip_min_x)
         {
            if (second->x >= rdp.clip_min_x)   // Both are in, save the last one
            {
               rdp.vtxbuf[index++] = rdp.vtxbuf2[j];
            }
            else      // First is in, second is out, save intersection
            {
               percent = (rdp.clip_min_x - first->x) / (second->x - first->x);
               rdp.vtxbuf[index].x = rdp.clip_min_x;
               rdp.vtxbuf[index].y = first->y + (second->y - first->y) * percent;
               rdp.vtxbuf[index].z = first->z + (second->z - first->z) * percent;
               rdp.vtxbuf[index].q = first->q + (second->q - first->q) * percent;
               clip_tri_uv(first, second, index, percent);
               clip_tri_interp_colors(first, second, index, percent, 8, interpolate_colors);
            }
         }
         else
         {
            if (rdp.vtxbuf2[j].x >= rdp.clip_min_x) // First is out, second is in, save intersection & in point
            {
               percent = (rdp.clip_min_x - second->x) / (first->x - second->x);
               rdp.vtxbuf[index].x = rdp.clip_min_x;
               rdp.vtxbuf[index].y = second->y + (first->y - second->y) * percent;
               rdp.vtxbuf[index].z = second->z + (first->z - second->z) * percent;
               rdp.vtxbuf[index].q = second->q + (first->q - second->q) * percent;
               clip_tri_uv(second, first, index, percent);
               clip_tri_interp_colors(second, first, index, percent, 8, interpolate_colors);

               // Save the in point
               rdp.vtxbuf[index++] = rdp.vtxbuf2[j];
            }
         }
      }
      n = index;
   }

   if (rdp.clip & CLIP_YMAX) // top of the screen
   {
      // Swap vertex buffers
      VERTEX *tmp = rdp.vtxbuf2;
      rdp.vtxbuf2 = rdp.vtxbuf;
      rdp.vtxbuf = tmp;
      rdp.vtx_buffer ^= 1;
      index = 0;

      // Check the vertices for clipping
      for (i=0; i<n; i++)
      {
         VERTEX *first, *second;
         j = i+1;
         if (j == n)
            j = 0;
         first = (VERTEX*)&rdp.vtxbuf2[i];
         second = (VERTEX*)&rdp.vtxbuf2[j];

         if (first->y <= rdp.clip_max_y)
         {
            if (second->y <= rdp.clip_max_y)   // Both are in, save the last one
            {
               rdp.vtxbuf[index++] = rdp.vtxbuf2[j];
            }
            else      // First is in, second is out, save intersection
            {
               percent = (rdp.clip_max_y - first->y) / (second->y - first->y);
               rdp.vtxbuf[index].x = first->x + (second->x - first->x) * percent;
               rdp.vtxbuf[index].y = rdp.clip_max_y;
               rdp.vtxbuf[index].z = first->z + (second->z - first->z) * percent;
               rdp.vtxbuf[index].q = first->q + (second->q - first->q) * percent;
               clip_tri_uv(first, second, index, percent);
               clip_tri_interp_colors(first, second, index, percent, 16, interpolate_colors);
            }
         }
         else
         {
            if (second->y <= rdp.clip_max_y) // First is out, second is in, save intersection & in point
            {
               percent = (rdp.clip_max_y - second->y) / (first->y - second->y);
               rdp.vtxbuf[index].x = rdp.vtxbuf2[j].x + (rdp.vtxbuf2[i].x - second->x) * percent;
               rdp.vtxbuf[index].y = rdp.clip_max_y;
               rdp.vtxbuf[index].z = second->z + (first->z - second->z) * percent;
               rdp.vtxbuf[index].q = second->q + (first->q - second->q) * percent;
               clip_tri_uv(second, first, index, percent);
               clip_tri_interp_colors(second, first, index, percent, 16, interpolate_colors);

               // Save the in point
               rdp.vtxbuf[index++] = rdp.vtxbuf2[j];
            }
         }
      }
      n = index;
   }

   if (rdp.clip & CLIP_YMIN) // bottom of the screen
   {
      // Swap vertex buffers
      VERTEX *tmp = rdp.vtxbuf2;
      rdp.vtxbuf2 = rdp.vtxbuf;
      rdp.vtxbuf = tmp;
      rdp.vtx_buffer ^= 1;
      index = 0;

      // Check the vertices for clipping
      for (i=0; i<n; i++)
      {
         VERTEX *first, *second;
         j = i+1;
         if (j == n)
            j = 0;
         first = (VERTEX*)&rdp.vtxbuf2[i];
         second = (VERTEX*)&rdp.vtxbuf2[j];

         if (first->y >= rdp.clip_min_y)
         {
            if (second->y >= rdp.clip_min_y)   // Both are in, save the last one
            {
               rdp.vtxbuf[index++] = rdp.vtxbuf2[j];
            }
            else      // First is in, second is out, save intersection
            {
               percent = (rdp.clip_min_y - first->y) / (second->y - first->y);
               rdp.vtxbuf[index].x = first->x + (second->x - first->x) * percent;
               rdp.vtxbuf[index].y = rdp.clip_min_y;
               rdp.vtxbuf[index].z = first->z + (second->z - first->z) * percent;
               rdp.vtxbuf[index].q = first->q + (second->q - first->q) * percent;
               clip_tri_uv(first, second, index, percent);
               clip_tri_interp_colors(first, second, index, percent, 16, interpolate_colors);
            }
         }
         else
         {
            if (second->y >= rdp.clip_min_y) // First is out, second is in, save intersection & in point
            {
               percent = (rdp.clip_min_y - second->y) / (first->y - second->y);
               rdp.vtxbuf[index].x = second->x + (first->x - second->x) * percent;
               rdp.vtxbuf[index].y = rdp.clip_min_y;
               rdp.vtxbuf[index].z = second->z + (first->z - second->z) * percent;
               rdp.vtxbuf[index].q = second->q + (first->q - second->q) * percent;
               clip_tri_uv(second, first, index, percent);
               clip_tri_interp_colors(second, first, index, percent, 16, interpolate_colors);

               // Save the in point
               rdp.vtxbuf[index++] = rdp.vtxbuf2[j];
            }
         }
      }
      n = index;
   }

   if (rdp.clip & CLIP_ZMAX) // far plane
   {
      // Swap vertex buffers
      VERTEX *tmp;
	  float maxZ;

      tmp = (VERTEX*)rdp.vtxbuf2;
      rdp.vtxbuf2 = rdp.vtxbuf;
      rdp.vtxbuf = tmp;
      rdp.vtx_buffer ^= 1;
      index = 0;
      maxZ = rdp.view_trans[2] + rdp.view_scale[2];

      // Check the vertices for clipping
      for (i=0; i<n; i++)
      {
         VERTEX *first, *second;
         j = i+1;
         if (j == n)
            j = 0;
         first = (VERTEX*)&rdp.vtxbuf2[i];
         second = (VERTEX*)&rdp.vtxbuf2[j];

         if (first->z < maxZ)
         {
            if (second->z < maxZ)   // Both are in, save the last one
            {
               rdp.vtxbuf[index++] = rdp.vtxbuf2[j];
            }
            else      // First is in, second is out, save intersection
            {
               percent = (maxZ - first->z) / (second->z - first->z);
               rdp.vtxbuf[index].x = first->x + (second->x - first->x) * percent;
               rdp.vtxbuf[index].y = first->y + (second->y - first->y) * percent;
               rdp.vtxbuf[index].z = maxZ - 0.001f;
               rdp.vtxbuf[index].q = first->q + (second->q - first->q) * percent;
               clip_tri_uv(first, second, index, percent);
               clip_tri_interp_colors(first, second, index, percent, 0, interpolate_colors);
            }
         }
         else
         {
            if (second->z < maxZ) // First is out, second is in, save intersection & in point
            {
               percent = (maxZ - second->z) / (first->z - second->z);
               rdp.vtxbuf[index].x = second->x + (first->x - second->x) * percent;
               rdp.vtxbuf[index].y = second->y + (first->y - second->y) * percent;
               rdp.vtxbuf[index].z = maxZ - 0.001f;;
               rdp.vtxbuf[index].q = second->q + (first->q - second->q) * percent;
               clip_tri_uv(second, first, index, percent);
               clip_tri_interp_colors(second, first, index, percent, 0, interpolate_colors);

               // Save the in point
               rdp.vtxbuf[index++] = rdp.vtxbuf2[j];
            }
         }
      }
      n = index;
   }

#if 0
   if (rdp.clip & CLIP_ZMIN) // near Z
   {
      // Swap vertex buffers
      VERTEX *tmp = rdp.vtxbuf2;
      rdp.vtxbuf2 = rdp.vtxbuf;
      rdp.vtxbuf = tmp;
      rdp.vtx_buffer ^= 1;
      index = 0;

      // Check the vertices for clipping
      for (i=0; i<n; i++)
      {
         j = i+1;
         if (j == n) j = 0;

         if (rdp.vtxbuf2[i].z >= 0.0f)
         {
            if (rdp.vtxbuf2[j].z >= 0.0f)   // Both are in, save the last one
            {
               rdp.vtxbuf[index++] = rdp.vtxbuf2[j];
            }
            else      // First is in, second is out, save intersection
            {
               percent = (-rdp.vtxbuf2[i].z) / (rdp.vtxbuf2[j].z - rdp.vtxbuf2[i].z);
               rdp.vtxbuf[index].x = rdp.vtxbuf2[i].x + (rdp.vtxbuf2[j].x - rdp.vtxbuf2[i].x) * percent;
               rdp.vtxbuf[index].y = rdp.vtxbuf2[i].y + (rdp.vtxbuf2[j].y - rdp.vtxbuf2[i].y) * percent;
               rdp.vtxbuf[index].z = 0.0f;
               rdp.vtxbuf[index].q = rdp.vtxbuf2[i].q + (rdp.vtxbuf2[j].q - rdp.vtxbuf2[i].q) * percent;
               rdp.vtxbuf[index].u0 = rdp.vtxbuf2[i].u0 + (rdp.vtxbuf2[j].u0 - rdp.vtxbuf2[i].u0) * percent;
               rdp.vtxbuf[index].v0 = rdp.vtxbuf2[i].v0 + (rdp.vtxbuf2[j].v0 - rdp.vtxbuf2[i].v0) * percent;
               rdp.vtxbuf[index].u1 = rdp.vtxbuf2[i].u1 + (rdp.vtxbuf2[j].u1 - rdp.vtxbuf2[i].u1) * percent;
               rdp.vtxbuf[index].v1 = rdp.vtxbuf2[i].v1 + (rdp.vtxbuf2[j].v1 - rdp.vtxbuf2[i].v1) * percent;
               if (interpolate_colors)
                  InterpolateColors(&rdp.vtxbuf[index++], percent, &rdp.vtxbuf2[i], &rdp.vtxbuf2[j]);
               else
                  rdp.vtxbuf[index++].number = rdp.vtxbuf2[i].number | rdp.vtxbuf2[j].number;
            }
         }
         else
         {
            //if (rdp.vtxbuf2[j].z < 0.0f)  // Both are out, save nothing
            if (rdp.vtxbuf2[j].z >= 0.0f) // First is out, second is in, save intersection & in point
            {
               percent = (-rdp.vtxbuf2[j].z) / (rdp.vtxbuf2[i].z - rdp.vtxbuf2[j].z);
               rdp.vtxbuf[index].x = rdp.vtxbuf2[j].x + (rdp.vtxbuf2[i].x - rdp.vtxbuf2[j].x) * percent;
               rdp.vtxbuf[index].y = rdp.vtxbuf2[j].y + (rdp.vtxbuf2[i].y - rdp.vtxbuf2[j].y) * percent;
               rdp.vtxbuf[index].z = 0.0f;;
               rdp.vtxbuf[index].q = rdp.vtxbuf2[j].q + (rdp.vtxbuf2[i].q - rdp.vtxbuf2[j].q) * percent;
               rdp.vtxbuf[index].u0 = rdp.vtxbuf2[j].u0 + (rdp.vtxbuf2[i].u0 - rdp.vtxbuf2[j].u0) * percent;
               rdp.vtxbuf[index].v0 = rdp.vtxbuf2[j].v0 + (rdp.vtxbuf2[i].v0 - rdp.vtxbuf2[j].v0) * percent;
               rdp.vtxbuf[index].u1 = rdp.vtxbuf2[j].u1 + (rdp.vtxbuf2[i].u1 - rdp.vtxbuf2[j].u1) * percent;
               rdp.vtxbuf[index].v1 = rdp.vtxbuf2[j].v1 + (rdp.vtxbuf2[i].v1 - rdp.vtxbuf2[j].v1) * percent;
               if (interpolate_colors)
                  InterpolateColors(&rdp.vtxbuf[index++], percent, &rdp.vtxbuf2[j], &rdp.vtxbuf2[i]);
               else
                  rdp.vtxbuf[index++].number = rdp.vtxbuf2[i].number | rdp.vtxbuf2[j].number;

               // Save the in point
               rdp.vtxbuf[index++] = rdp.vtxbuf2[j];
            }
         }
      }
      n = index;
   }
#endif

   rdp.n_global = n;
}

static void render_tri (uint16_t linew, int old_interpolate)
{
   int i, n;
   float fog;

   if (rdp.clip)
      clip_tri(old_interpolate);

   n = rdp.n_global;

   if ((rdp.clip & CLIP_ZMIN) && (rdp.othermode_l & G_OBJLT_TLUT))
   {
      int to_render = false;

      for (i = 0; i < n; i++)
      {
         if (rdp.vtxbuf[i].z >= 0.0f)
         {
            to_render = true;
            break;
         }
      }

      if (!to_render) //all z < 0
      {
         FRDP (" * render_tri: all z < 0\n");
         return;
      }
   }

   if (rdp.clip && !old_interpolate)
   {
      for (i = 0; i < n; i++)
      {
         float percent = 101.0f;
         VERTEX * v1 = 0,  * v2 = 0;

         switch (rdp.vtxbuf[i].number&7)
         {
            case 1:
            case 2:
            case 4:
               continue;
               break;
            case 3:
               v1 = org_vtx[0];
               v2 = org_vtx[1];
               break;
            case 5:
               v1 = org_vtx[0];
               v2 = org_vtx[2];
               break;
            case 6:
               v1 = org_vtx[1];
               v2 = org_vtx[2];
               break;
            case 7:
               InterpolateColors3(org_vtx[0], org_vtx[1], org_vtx[2], &rdp.vtxbuf[i]);
               continue;
               break;
         }

         switch (rdp.vtxbuf[i].number&24)
         {
            case 8:
               percent = (rdp.vtxbuf[i].x-v1->sx)/(v2->sx-v1->sx);
               break;
            case 16:
               percent = (rdp.vtxbuf[i].y-v1->sy)/(v2->sy-v1->sy);
               break;
            default:
               {
                  float d = (v2->sx-v1->sx);
                  if (fabs(d) > 1.0)
                     percent = (rdp.vtxbuf[i].x-v1->sx)/d;
                  if (percent > 100.0f)
                     percent = (rdp.vtxbuf[i].y-v1->sy)/(v2->sy-v1->sy);
               }
         }

         InterpolateColors2(v1, v2, &rdp.vtxbuf[i], percent);
      }
   }

   ConvertCoordsConvert (rdp.vtxbuf, n);

   switch (rdp.fog_mode)
   {
      case FOG_MODE_ENABLED:
         for (i = 0; i < n; i++)
            rdp.vtxbuf[i].f = 1.0f/max(4.0f, rdp.vtxbuf[i].f);
         break;
      case FOG_MODE_BLEND:
         fog = 1.0f/max(1, g_gdp.fog_color.a);
         for (i = 0; i < n; i++)
            rdp.vtxbuf[i].f = fog;
         break;
      case FOG_MODE_BLEND_INVERSE:
         fog = 1.0f/max(1, (~g_gdp.fog_color.total) & 0xFF);
         for (i = 0; i < n; i++)
            rdp.vtxbuf[i].f = fog;
         break;
   }

   if (settings.lodmode && rdp.cur_tile < rdp.mipmap_level)
      CalculateLOD(rdp.vtxbuf, n, settings.lodmode);

   cmb.cmb_ext_use = cmb.tex_cmb_ext_use = 0;

   if (linew > 0)
   {
      VERTEX *V0, *V1, v[4];
      float width;

      V0 = &rdp.vtxbuf[0];
      V1 = &rdp.vtxbuf[1];

      if (fabs(V0->x - V1->x) < 0.01 && fabs(V0->y - V1->y) < 0.01)
         V1 = &rdp.vtxbuf[2];

      V0->z = ScaleZ(V0->z);
      V1->z = ScaleZ(V1->z);

      v[0] = *V0;
      v[1] = *V0;
      v[2] = *V1;
      v[3] = *V1;

      width = linew * 0.25f;

      if (fabs(V0->y - V1->y) < 0.0001)
      {
         v[0].x = v[1].x = V0->x;
         v[2].x = v[3].x = V1->x;

         width *= rdp.scale_y;
         v[0].y = v[2].y = V0->y - width;
         v[1].y = v[3].y = V0->y + width;
      }
      else if (fabs(V0->x - V1->x) < 0.0001)
      {
         v[0].y = v[1].y = V0->y;
         v[2].y = v[3].y = V1->y;

         width *= rdp.scale_x;
         v[0].x = v[2].x = V0->x - width;
         v[1].x = v[3].x = V0->x + width;
      }
      else
      {
         float dx = V1->x - V0->x;
         float dy = V1->y - V0->y;
         float len = sqrtf(dx*dx + dy*dy);
         float wx = dy * width * rdp.scale_x / len;
         float wy = dx * width * rdp.scale_y / len;
         v[0].x = V0->x + wx;
         v[0].y = V0->y - wy;
         v[1].x = V0->x - wx;
         v[1].y = V0->y + wy;
         v[2].x = V1->x + wx;
         v[2].y = V1->y - wy;
         v[3].x = V1->x - wx;
         v[3].y = V1->y + wy;
      }

      grDrawVertexArrayContiguous(GR_TRIANGLE_STRIP, 4, &v[0]);
   }
   else
   {
      DepthBuffer(rdp.vtxbuf, n);

      if ((rdp.rm & 0xC10) == 0xC10)
         grDepthBiasLevel(-deltaZ);

      grDrawVertexArrayContiguous(GR_TRIANGLE_FAN, n,
            rdp.vtx_buffer ? rdp.vtx2 : rdp.vtx1);
   }
}

void do_triangle_stuff_2 (uint16_t linew, uint8_t no_clip, int old_interpolate)
{
   int i;
   if (no_clip)
      rdp.clip = 0;

   for (i = 0; i < rdp.n_global; i++)
   {
      if (rdp.vtxbuf[i].x > rdp.clip_max_x)
         rdp.clip |= CLIP_XMAX;
      if (rdp.vtxbuf[i].x < rdp.clip_min_x)
         rdp.clip |= CLIP_XMIN;
      if (rdp.vtxbuf[i].y > rdp.clip_max_y)
         rdp.clip |= CLIP_YMAX;
      if (rdp.vtxbuf[i].y < rdp.clip_min_y)
         rdp.clip |= CLIP_YMIN;
   }

   render_tri (linew, old_interpolate);
}

void do_triangle_stuff (uint16_t linew, int old_interpolate) // what else?? do the triangle stuff :P (to keep from writing code twice)
{
   int i;
   float maxZ = (g_gdp.other_modes.z_source_sel != 1) ? rdp.view_trans[2] + rdp.view_scale[2] : g_gdp.prim_color.z;
   uint8_t no_clip = 2;

   for (i=0; i<rdp.n_global; i++)
   {
      if (rdp.vtxbuf[i].not_zclipped)
      {
         //FRDP (" * NOT ZCLIPPPED: %d\n", rdp.vtxbuf[i].number);
         rdp.vtxbuf[i].x = rdp.vtxbuf[i].sx;
         rdp.vtxbuf[i].y = rdp.vtxbuf[i].sy;
         rdp.vtxbuf[i].z = rdp.vtxbuf[i].sz;
         rdp.vtxbuf[i].q = rdp.vtxbuf[i].oow;
         rdp.vtxbuf[i].u0 = rdp.vtxbuf[i].u0_w;
         rdp.vtxbuf[i].v0 = rdp.vtxbuf[i].v0_w;
         rdp.vtxbuf[i].u1 = rdp.vtxbuf[i].u1_w;
         rdp.vtxbuf[i].v1 = rdp.vtxbuf[i].v1_w;
      }
      else
      {
         //FRDP (" * ZCLIPPED: %d\n", rdp.vtxbuf[i].number);
         rdp.vtxbuf[i].q = 1.0f / rdp.vtxbuf[i].w;
         rdp.vtxbuf[i].x = rdp.view_trans[0] + rdp.vtxbuf[i].x * rdp.vtxbuf[i].q * rdp.view_scale[0] + rdp.offset_x;
         rdp.vtxbuf[i].y = rdp.view_trans[1] + rdp.vtxbuf[i].y * rdp.vtxbuf[i].q * rdp.view_scale[1] + rdp.offset_y;
         rdp.vtxbuf[i].z = rdp.view_trans[2] + rdp.vtxbuf[i].z * rdp.vtxbuf[i].q * rdp.view_scale[2];
         if (rdp.tex >= 1)
         {
            rdp.vtxbuf[i].u0 *= rdp.vtxbuf[i].q;
            rdp.vtxbuf[i].v0 *= rdp.vtxbuf[i].q;
         }
         if (rdp.tex >= 2)
         {
            rdp.vtxbuf[i].u1 *= rdp.vtxbuf[i].q;
            rdp.vtxbuf[i].v1 *= rdp.vtxbuf[i].q;
         }
      }

      if (g_gdp.other_modes.z_source_sel == 1)
         rdp.vtxbuf[i].z = g_gdp.prim_color.z;

      // Don't remove clipping, or it will freeze
      if (rdp.vtxbuf[i].z > maxZ)           rdp.clip |= CLIP_ZMAX;
      if (rdp.vtxbuf[i].z < 0.0f)           rdp.clip |= CLIP_ZMIN;
      no_clip &= rdp.vtxbuf[i].screen_translated;
   }
   if (!no_clip)
   {
      if (!settings.clip_zmin)
         rdp.clip &= ~CLIP_ZMIN;
   }

   do_triangle_stuff_2(linew, no_clip, old_interpolate);
}


void update_scissor(bool set_scissor)
{
   if (!(g_gdp.flags & UPDATE_SCISSOR))
      return;

   if (set_scissor)
   {
      rdp.scissor.ul_x = (uint32_t)rdp.clip_min_x;
      rdp.scissor.lr_x = (uint32_t)rdp.clip_max_x;
      rdp.scissor.ul_y = (uint32_t)rdp.clip_min_y;
      rdp.scissor.lr_y = (uint32_t)rdp.clip_max_y;
   }
   else
   {
      rdp.scissor.ul_x = (uint32_t)(rdp.scissor_o.ul_x * rdp.scale_x + rdp.offset_x);
      rdp.scissor.lr_x = (uint32_t)(rdp.scissor_o.lr_x * rdp.scale_x + rdp.offset_x);
      rdp.scissor.ul_y = (uint32_t)(rdp.scissor_o.ul_y * rdp.scale_y + rdp.offset_y);
      rdp.scissor.lr_y = (uint32_t)(rdp.scissor_o.lr_y * rdp.scale_y + rdp.offset_y);
   }

   grClipWindow (rdp.scissor.ul_x, rdp.scissor.ul_y, rdp.scissor.lr_x, rdp.scissor.lr_y);

   g_gdp.flags ^= UPDATE_SCISSOR;
}

void glide64_z_compare(void)
{
   // Z buffer
   if (g_gdp.flags & UPDATE_ZBUF_ENABLED)
   {
      int depthbias_level = 0;
      int depthbuf_func = GR_CMP_ALWAYS;
      int depthmask_val = FXFALSE;
      g_gdp.flags ^= UPDATE_ZBUF_ENABLED;

      if (((rdp.flags & ZBUF_ENABLED) || ((g_gdp.other_modes.z_source_sel == G_ZS_PRIM) && (((rdp.othermode_h & RDP_CYCLE_TYPE) >> 20) < G_CYC_COPY))))
      {
         if (rdp.flags & ZBUF_COMPARE)
         {
            switch (g_gdp.other_modes.z_mode)
            {
               case ZMODE_OPA:
                  depthbuf_func = settings.zmode_compare_less ? GR_CMP_LESS : GR_CMP_LEQUAL;
                  break;
               case ZMODE_INTER:
                  depthbias_level = -4;
                  depthbuf_func = settings.zmode_compare_less ? GR_CMP_LESS : GR_CMP_LEQUAL;
                  break;
               case ZMODE_XLU:
                  if (settings.ucode == 7)
                     depthbias_level = -4;
                  depthbuf_func = GR_CMP_LESS;
                  break;
               case ZMODE_DEC:
                  // will be set dynamically per polygon
                  //grDepthBiasLevel(-deltaZ);
                  depthbuf_func = GR_CMP_LEQUAL;
                  break;
            }
         }

         if (rdp.flags & ZBUF_UPDATE)
            depthmask_val = FXTRUE;
      }

      grDepthBiasLevel(depthbias_level);
      grDepthBufferFunction (depthbuf_func);
      grDepthMask(depthmask_val);
   }
}

//
// update - update states if they need it
//
void update(void)
{
   bool set_scissor = false;

   // Check for rendermode changes
   // Z buffer
   if (rdp.render_mode_changed & 0x00000C30)
   {
      FRDP (" |- render_mode_changed zbuf - decal: %s, update: %s, compare: %s\n",
            str_yn[(rdp.othermode_l & G_CULL_BACK)?1:0],
            str_yn[(rdp.othermode_l & UPDATE_BIASLEVEL)?1:0],
            str_yn[(rdp.othermode_l & ALPHA_COMPARE)?1:0]);

      rdp.render_mode_changed &= ~0x00000C30;
      g_gdp.flags |= UPDATE_ZBUF_ENABLED;

      // Update?
      if ((rdp.othermode_l & RDP_Z_UPDATE_ENABLE))
         rdp.flags |= ZBUF_UPDATE;
      else
         rdp.flags &= ~ZBUF_UPDATE;

      // Compare?
      if (rdp.othermode_l & ALPHA_COMPARE)
         rdp.flags |= ZBUF_COMPARE;
      else
         rdp.flags &= ~ZBUF_COMPARE;
   }

   // Alpha compare
   if (rdp.render_mode_changed & CULL_FRONT)
   {
      FRDP (" |- render_mode_changed alpha compare - on: %s\n",
            str_yn[(rdp.othermode_l & CULL_FRONT)?1:0]);
      rdp.render_mode_changed &= ~CULL_FRONT;
      g_gdp.flags |= UPDATE_ALPHA_COMPARE;

      if (rdp.othermode_l & CULL_FRONT)
         rdp.flags |= ALPHA_COMPARE;
      else
         rdp.flags &= ~ALPHA_COMPARE;
   }

   if (rdp.render_mode_changed & CULL_BACK) // alpha cvg sel
   {
      FRDP (" |- render_mode_changed alpha cvg sel - on: %s\n",
            str_yn[(rdp.othermode_l & CULL_BACK)?1:0]);
      rdp.render_mode_changed &= ~CULL_BACK;
      g_gdp.flags |= UPDATE_COMBINE;
      g_gdp.flags |= UPDATE_ALPHA_COMPARE;
   }

   // Force blend
   if (rdp.render_mode_changed & 0xFFFF0000)
   {
      FRDP (" |- render_mode_changed force_blend - %08lx\n", rdp.othermode_l&0xFFFF0000);
      rdp.render_mode_changed &= 0x0000FFFF;

      rdp.fbl_a0 = (uint8_t)((rdp.othermode_l>>30)&0x3);
      rdp.fbl_b0 = (uint8_t)((rdp.othermode_l>>26)&0x3);
      rdp.fbl_c0 = (uint8_t)((rdp.othermode_l>>22)&0x3);
      rdp.fbl_d0 = (uint8_t)((rdp.othermode_l>>18)&0x3);
      rdp.fbl_a1 = (uint8_t)((rdp.othermode_l>>28)&0x3);
      rdp.fbl_b1 = (uint8_t)((rdp.othermode_l>>24)&0x3);
      rdp.fbl_c1 = (uint8_t)((rdp.othermode_l>>20)&0x3);
      rdp.fbl_d1 = (uint8_t)((rdp.othermode_l>>16)&0x3);

      g_gdp.flags |= UPDATE_COMBINE;
   }

   // Combine MUST go before texture
   if ((g_gdp.flags & UPDATE_COMBINE) && rdp.allow_combine)
   {

      LRDP (" |-+ update_combine\n");
      Combine ();
   }

   if (g_gdp.flags & UPDATE_TEXTURE)  // note: UPDATE_TEXTURE and UPDATE_COMBINE are the same
   {
      rdp.tex_ctr ++;
      if (rdp.tex_ctr == 0xFFFFFFFF)
         rdp.tex_ctr = 0;

      TexCache ();
      if (rdp.noise == NOISE_MODE_NONE)
         g_gdp.flags ^= UPDATE_TEXTURE;
   }

   glide64_z_compare();

   // Alpha compare
   if (g_gdp.flags & UPDATE_ALPHA_COMPARE)
   {
      g_gdp.flags ^= UPDATE_ALPHA_COMPARE;

      if ((rdp.othermode_l & RDP_ALPHA_COMPARE) == 1 && !(rdp.othermode_l & RDP_ALPHA_CVG_SELECT) && (!(rdp.othermode_l & RDP_FORCE_BLEND) || (g_gdp.blend_color.a)))
      {
         uint8_t reference = (uint8_t)g_gdp.blend_color.a;
         grAlphaTestFunction (reference ? GR_CMP_GEQUAL : GR_CMP_GREATER, reference, 1);
         FRDP (" |- alpha compare: blend: %02lx\n", reference);
      }
      else
      {
         if (rdp.flags & ALPHA_COMPARE)
         {
            bool cond_set = (rdp.othermode_l & 0x5000) == 0x5000;
            grAlphaTestFunction (!cond_set ? GR_CMP_GEQUAL : GR_CMP_GREATER, 0x20, !cond_set ? 1 : 0);
            if (cond_set)
               grAlphaTestReferenceValue (((rdp.othermode_l & RDP_ALPHA_COMPARE) == 3) ? (uint8_t)g_gdp.blend_color.a : 0x00);
         }
         else
         {
            grAlphaTestFunction (GR_CMP_ALWAYS, 0x00, 0);
            LRDP (" |- alpha compare: none\n");
         }
      }
      if ((rdp.othermode_l & RDP_ALPHA_COMPARE) == 3 && (((rdp.othermode_h & RDP_CYCLE_TYPE) >> 20) < G_CYC_COPY))
      {
         if (settings.old_style_adither || g_gdp.other_modes.alpha_dither_sel != 3)
         {
            LRDP (" |- alpha compare: dither\n");
            grStippleMode(settings.stipple_mode);
         }
         else
            grStippleMode(GR_STIPPLE_DISABLE);
      }
      else
      {
         //LRDP (" |- alpha compare: dither disabled\n");
         grStippleMode(GR_STIPPLE_DISABLE);
      }
   }

   // Cull mode (leave this in for z-clipped triangles)
   if (g_gdp.flags & UPDATE_CULL_MODE)
   {
      uint32_t mode;
      g_gdp.flags ^= UPDATE_CULL_MODE;
      mode = (rdp.flags & CULLMASK) >> CULLSHIFT;
      FRDP (" |- cull_mode - mode: %s\n", str_cull[mode]);
      switch (mode)
      {
         case 0: // cull none
         case 3: // cull both
            grCullMode(GR_CULL_DISABLE);
            break;
         case 1: // cull front
            grCullMode(GR_CULL_NEGATIVE);
            break;
         case 2: // cull back
            grCullMode (GR_CULL_POSITIVE);
            break;
      }
   }

   //Added by Gonetz.
   if (settings.fog && (g_gdp.flags & UPDATE_FOG_ENABLED))
   {
      uint16_t blender;
      g_gdp.flags ^= UPDATE_FOG_ENABLED;

      blender = (uint16_t)(rdp.othermode_l >> 16);
      if (rdp.flags & FOG_ENABLED)
      {
         rdp_blender_setting *bl = (rdp_blender_setting*)(&blender);
         if((rdp.fog_multiplier > 0) && (bl->c1_m1a==3 || bl->c1_m2a == 3 || bl->c2_m1a == 3 || bl->c2_m2a == 3))
         {
            grFogMode (GR_FOG_WITH_TABLE_ON_FOGCOORD_EXT, g_gdp.fog_color.total);
            rdp.fog_mode = FOG_MODE_ENABLED;
            LRDP("fog enabled \n");
         }
         else
         {
            LRDP("fog disabled in blender\n");
            rdp.fog_mode = FOG_MODE_DISABLED;
            grFogMode (GR_FOG_DISABLE, g_gdp.fog_color.total);
         }
      }
      else if (blender == 0xc410 || blender == 0xc411 || blender == 0xf500)
      {
         grFogMode (GR_FOG_WITH_TABLE_ON_FOGCOORD_EXT, g_gdp.fog_color.total);
         rdp.fog_mode = FOG_MODE_BLEND;
         LRDP("fog blend \n");
      }
      else if (blender == 0x04d1)
      {
         grFogMode (GR_FOG_WITH_TABLE_ON_FOGCOORD_EXT, g_gdp.fog_color.total);
         rdp.fog_mode = FOG_MODE_BLEND_INVERSE;
         LRDP("fog blend \n");
      }
      else
      {
         LRDP("fog disabled\n");
         rdp.fog_mode = FOG_MODE_DISABLED;
         grFogMode (GR_FOG_DISABLE, g_gdp.fog_color.total);
      }
   }

   if (g_gdp.flags & UPDATE_VIEWPORT)
   {
      g_gdp.flags ^= UPDATE_VIEWPORT;
      {
         float scale_x = (float)fabs(rdp.view_scale[0]);
         float scale_y = (float)fabs(rdp.view_scale[1]);

         rdp.clip_min_x = max((rdp.view_trans[0] - scale_x + rdp.offset_x) / rdp.clip_ratio, 0.0f);
         rdp.clip_min_y = max((rdp.view_trans[1] - scale_y + rdp.offset_y) / rdp.clip_ratio, 0.0f);
         rdp.clip_max_x = min((rdp.view_trans[0] + scale_x + rdp.offset_x) * rdp.clip_ratio, settings.res_x);
         rdp.clip_max_y = min((rdp.view_trans[1] + scale_y + rdp.offset_y) * rdp.clip_ratio, settings.res_y);

         FRDP (" |- viewport - (%d, %d, %d, %d)\n", (uint32_t)rdp.clip_min_x, (uint32_t)rdp.clip_min_y, (uint32_t)rdp.clip_max_x, (uint32_t)rdp.clip_max_y);
         if (!rdp.scissor_set)
         {
            g_gdp.flags |= UPDATE_SCISSOR;
            set_scissor = true;
         }
      }
   }

   update_scissor(set_scissor);
}
