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

static void calc_point_light (VERTEX *v, float * vpos)
{
   uint32_t l;
   float light_intensity = 0.0f;
   float color[3];

   color[0] = rdp.light[rdp.num_lights].r;
   color[1] = rdp.light[rdp.num_lights].g;
   color[2] = rdp.light[rdp.num_lights].b;

   for (l = 0; l < rdp.num_lights; l++)
   {
      if (rdp.light[l].nonblack)
      {
         float lvec[3];
         lvec[0] = rdp.light[l].x - vpos[0];
         lvec[1] = rdp.light[l].y - vpos[1];
         lvec[2] = rdp.light[l].z - vpos[2];

         float light_len2 = lvec[0]*lvec[0] + lvec[1]*lvec[1] + lvec[2]*lvec[2];
         float light_len = squareRoot(light_len2);
#ifdef EXTREME_LOGGING
         FRDP ("calc_point_light: len: %f, len2: %f\n", light_len, light_len2);
#endif
         float at = rdp.light[l].ca + light_len/65535.0f*rdp.light[l].la + light_len2/65535.0f*rdp.light[l].qa;

         if (at > 0.0f)
            light_intensity = 1/at;//DotProduct (lvec, nvec) / (light_len * normal_len * at);
         else
            light_intensity = 0.0f;
      }
      else
         light_intensity = 0.0f;

      if (light_intensity > 0.0f)
      {
         color[0] += rdp.light[l].r * light_intensity;
         color[1] += rdp.light[l].g * light_intensity;
         color[2] += rdp.light[l].b * light_intensity;
      }
   }

   if (color[0] > 1.0f)
      color[0] = 1.0f;
   if (color[1] > 1.0f)
      color[1] = 1.0f;
   if (color[2] > 1.0f)
      color[2] = 1.0f;

   v->r = (uint8_t)(color[0]*255.0f);
   v->g = (uint8_t)(color[1]*255.0f);
   v->b = (uint8_t)(color[2]*255.0f);
}

#ifdef HAVE_NEON
#include <arm_neon.h>

static void uc2_vertex_neon(uint32_t w0, uint32_t w1)
{
   uint32_t i, l;
   
   if (!(w0 & 0x00FFFFFF))
   {
      uc6_obj_rectangle(w0, w1);
      return;
   }

   // This is special, not handled in update(), but here
   // * Matrix Pre-multiplication idea by Gonetz (Gonetz@ngs.ru)
   if (rdp.update & UPDATE_MULT_MAT)
      gSPCombineMatrices();

   if (rdp.update & UPDATE_LIGHTS)
   {
      rdp.update ^= UPDATE_LIGHTS;

      // Calculate light vectors
      for (l = 0; l < rdp.num_lights; l++)
      {
         InverseTransformVector(&rdp.light[l].dir_x, rdp.light_vector[l], rdp.model);
         NormalizeVector (rdp.light_vector[l]);
      }
   }

   uint32_t addr = segoffset(w1);
   int v0, n;
   float x, y, z;

   rdp.vn = n = (w0 >> 12) & 0xFF;
   rdp.v0 = v0 = ((w0 >> 1) & 0x7F) - n;

   FRDP ("uc2:vertex n: %d, v0: %d, from: %08lx\n", n, v0, addr);

   if (v0 < 0)
   {
      RDP_E ("** ERROR: uc2:vertex v0 < 0\n");
      LRDP("** ERROR: uc2:vertex v0 < 0\n");
      return;
   }

   uint32_t geom_mode = rdp.geom_mode;
   if ((settings.hacks&hack_Fzero) && (rdp.geom_mode & G_TEXTURE_GEN))
   {
      if (((int16_t*)gfx.RDRAM)[(((addr) >> 1) + 4)^1] || ((int16_t*)gfx.RDRAM)[(((addr) >> 1) + 5)^1])
         rdp.geom_mode ^= G_TEXTURE_GEN;
   }

   float32x4_t comb0, comb1, comb2, comb3;
   float32x4_t v_xyzw;
   comb0 = vld1q_f32(rdp.combined[0]);
   comb1 = vld1q_f32(rdp.combined[1]);
   comb2 = vld1q_f32(rdp.combined[2]);
   comb3 = vld1q_f32(rdp.combined[3]);

   for (i=0; i < (n<<4); i+=16)
   {
      int16_t *rdram = (int16_t*)gfx.RDRAM;
      VERTEX *v = &rdp.vtx[v0 + (i>>4)];
      x   = (float)rdram[(((addr+i) >> 1) + 0)^1];
      y   = (float)rdram[(((addr+i) >> 1) + 1)^1];
      z   = (float)rdram[(((addr+i) >> 1) + 2)^1];
      v->flags  = ((uint16_t*)gfx.RDRAM)[(((addr+i) >> 1) + 3)^1];
      v->ou   = (float)rdram[(((addr+i) >> 1) + 4)^1];
      v->ov   = (float)rdram[(((addr+i) >> 1) + 5)^1];
      v->uv_scaled = 0;
      v->a    = ((uint8_t*)gfx.RDRAM)[(addr+i + 15)^3];

      v_xyzw = vmulq_n_f32(comb0,x)+vmulq_n_f32(comb1,y)+vmulq_n_f32(comb2,z)+comb3;
      v->x=vgetq_lane_f32(v_xyzw,0);
      v->y=vgetq_lane_f32(v_xyzw,1);
      v->z=vgetq_lane_f32(v_xyzw,2);
      v->w=vgetq_lane_f32(v_xyzw,3);

      v->uv_calculated = 0xFFFFFFFF;
      v->screen_translated = 0;
      v->shade_mod = 0;

      if (fabs(v->w) < 0.001) v->w = 0.001f;
      v->oow = 1.0f / v->w;
      v_xyzw = vmulq_n_f32(v_xyzw,v->oow);
      v->x_w=vgetq_lane_f32(v_xyzw,0);
      v->y_w=vgetq_lane_f32(v_xyzw,1);
      v->z_w=vgetq_lane_f32(v_xyzw,2);
      CalculateFog (v);


      v->scr_off = 0;
      if (v->x < -v->w) v->scr_off |= 1;
      if (v->x > v->w) v->scr_off |= 2;
      if (v->y < -v->w) v->scr_off |= 4;
      if (v->y > v->w) v->scr_off |= 8;
      if (v->w < 0.1f) v->scr_off |= 16;
      //    if (v->z_w > 1.0f) v->scr_off |= 32;

      if (rdp.geom_mode & G_LIGHTING)
      {
         int8_t *rdram_s8 = (int8_t*)gfx.RDRAM;
         v->vec[0] = rdram_s8[(addr+i + 12)^3];
         v->vec[1] = rdram_s8[(addr+i + 13)^3];
         v->vec[2] = rdram_s8[(addr+i + 14)^3];
         //	  FRDP("Calc light. x: %f, y: %f z: %f\n", v->vec[0], v->vec[1], v->vec[2]);
         //      if (!(rdp.geom_mode & G_CLIPPING))
         {
            if (rdp.geom_mode & G_TEXTURE_GEN)
            {
               if (rdp.geom_mode & G_TEXTURE_GEN_LINEAR)
                  calc_linear (v);
               else
                  calc_sphere (v);
            }
         }
         if (rdp.geom_mode & 0x00400000)
         {
            float tmpvec[3] = {x, y, z};
            calc_point_light (v, tmpvec);
         }
         else
         {
            NormalizeVector (v->vec);
            calc_light (v);
         }
      }
      else
      {
         uint8_t *rdram_u8 = (uint8_t*)gfx.RDRAM;
         v->r = rdram_u8[(addr+i + 12)^3];
         v->g = rdram_u8[(addr+i + 13)^3];
         v->b = rdram_u8[(addr+i + 14)^3];
      }
#ifdef EXTREME_LOGGING
      FRDP ("v%d - x: %f, y: %f, z: %f, w: %f, u: %f, v: %f, f: %f, z_w: %f, r=%d, g=%d, b=%d, a=%d\n", i>>4, v->x, v->y, v->z, v->w, v->ou*rdp.tiles[rdp.cur_tile].s_scale, v->ov*rdp.tiles[rdp.cur_tile].t_scale, v->f, v->z_w, v->r, v->g, v->b, v->a);
#endif
   }

   rdp.geom_mode = geom_mode;
}
#endif

static void uc2_vertex(uint32_t w0, uint32_t w1)
{
   uint32_t i, l;
   
   if (!(w0 & 0x00FFFFFF))
   {
      uc6_obj_rectangle(w0, w1);
      return;
   }

   // This is special, not handled in update(), but here
   // * Matrix Pre-multiplication idea by Gonetz (Gonetz@ngs.ru)
   if (rdp.update & UPDATE_MULT_MAT)
      gSPCombineMatrices();

   if (rdp.update & UPDATE_LIGHTS)
   {
      rdp.update ^= UPDATE_LIGHTS;

      // Calculate light vectors
      for (l = 0; l < rdp.num_lights; l++)
      {
         InverseTransformVector(&rdp.light[l].dir_x, rdp.light_vector[l], rdp.model);
         NormalizeVector (rdp.light_vector[l]);
      }
   }

   uint32_t addr = segoffset(w1);
   int v0, n;
   float x, y, z;

   rdp.vn = n = (w0 >> 12) & 0xFF;
   rdp.v0 = v0 = ((w0 >> 1) & 0x7F) - n;

   FRDP ("uc2:vertex n: %d, v0: %d, from: %08lx\n", n, v0, addr);

   if (v0 < 0)
   {
      RDP_E ("** ERROR: uc2:vertex v0 < 0\n");
      LRDP("** ERROR: uc2:vertex v0 < 0\n");
      return;
   }

   uint32_t geom_mode = rdp.geom_mode;
   if ((settings.hacks&hack_Fzero) && (rdp.geom_mode & G_TEXTURE_GEN))
   {
      if (((int16_t*)gfx.RDRAM)[(((addr) >> 1) + 4)^1] || ((int16_t*)gfx.RDRAM)[(((addr) >> 1) + 5)^1])
         rdp.geom_mode ^= G_TEXTURE_GEN;
   }

   for (i=0; i < (n<<4); i+=16)
   {
      VERTEX *v = &rdp.vtx[v0 + (i>>4)];
      x   = (float)((int16_t*)gfx.RDRAM)[(((addr+i) >> 1) + 0)^1];
      y   = (float)((int16_t*)gfx.RDRAM)[(((addr+i) >> 1) + 1)^1];
      z   = (float)((int16_t*)gfx.RDRAM)[(((addr+i) >> 1) + 2)^1];
      v->flags  = ((uint16_t*)gfx.RDRAM)[(((addr+i) >> 1) + 3)^1];
      v->ou   = (float)((int16_t*)gfx.RDRAM)[(((addr+i) >> 1) + 4)^1];
      v->ov   = (float)((int16_t*)gfx.RDRAM)[(((addr+i) >> 1) + 5)^1];
      v->uv_scaled = 0;
      v->a    = ((uint8_t*)gfx.RDRAM)[(addr+i + 15)^3];

      v->x = x*rdp.combined[0][0] + y*rdp.combined[1][0] + z*rdp.combined[2][0] + rdp.combined[3][0];
      v->y = x*rdp.combined[0][1] + y*rdp.combined[1][1] + z*rdp.combined[2][1] + rdp.combined[3][1];
      v->z = x*rdp.combined[0][2] + y*rdp.combined[1][2] + z*rdp.combined[2][2] + rdp.combined[3][2];
      v->w = x*rdp.combined[0][3] + y*rdp.combined[1][3] + z*rdp.combined[2][3] + rdp.combined[3][3];

      v->uv_calculated = 0xFFFFFFFF;
      v->screen_translated = 0;
      v->shade_mod = 0;

      if (fabs(v->w) < 0.001) v->w = 0.001f;
      v->oow = 1.0f / v->w;
      v->x_w = v->x * v->oow;
      v->y_w = v->y * v->oow;
      v->z_w = v->z * v->oow;
      CalculateFog (v);


      v->scr_off = 0;
      if (v->x < -v->w) v->scr_off |= 1;
      if (v->x > v->w) v->scr_off |= 2;
      if (v->y < -v->w) v->scr_off |= 4;
      if (v->y > v->w) v->scr_off |= 8;
      if (v->w < 0.1f) v->scr_off |= 16;
      //    if (v->z_w > 1.0f) v->scr_off |= 32;

      if (rdp.geom_mode & G_LIGHTING)
      {
         int8_t *rdram_s8 = (int8_t*)gfx.RDRAM;
         v->vec[0] = rdram_s8[(addr+i + 12)^3];
         v->vec[1] = rdram_s8[(addr+i + 13)^3];
         v->vec[2] = rdram_s8[(addr+i + 14)^3];
         //	  FRDP("Calc light. x: %f, y: %f z: %f\n", v->vec[0], v->vec[1], v->vec[2]);
         //      if (!(rdp.geom_mode & G_CLIPPING))
         {
            if (rdp.geom_mode & G_TEXTURE_GEN)
            {
               if (rdp.geom_mode & G_TEXTURE_GEN_LINEAR)
                  calc_linear (v);
               else
                  calc_sphere (v);
            }
         }
         if (rdp.geom_mode & 0x00400000)
         {
            float tmpvec[3] = {x, y, z};
            calc_point_light (v, tmpvec);
         }
         else
         {
            NormalizeVector (v->vec);
            calc_light (v);
         }
      }
      else
      {
         uint8_t *rdram_u8 = (uint8_t*)gfx.RDRAM;
         v->r = rdram_u8[(addr+i + 12)^3];
         v->g = rdram_u8[(addr+i + 13)^3];
         v->b = rdram_u8[(addr+i + 14)^3];
      }
#ifdef EXTREME_LOGGING
      FRDP ("v%d - x: %f, y: %f, z: %f, w: %f, u: %f, v: %f, f: %f, z_w: %f, r=%d, g=%d, b=%d, a=%d\n", i>>4, v->x, v->y, v->z, v->w, v->ou*rdp.tiles[rdp.cur_tile].s_scale, v->ov*rdp.tiles[rdp.cur_tile].t_scale, v->f, v->z_w, v->r, v->g, v->b, v->a);
#endif
   }

   rdp.geom_mode = geom_mode;
}

static void uc2_modifyvtx(uint32_t w0, uint32_t w1)
{
   uint8_t where = (uint8_t)((w0 >> 16) & 0xFF);
   uint16_t vtx = (uint16_t)((w0 >> 1) & 0xFFFF);

   gSPModifyVertex(where, vtx, w1);
   FRDP ("uc2:modifyvtx: vtx: %d, where: 0x%02lx, val: %08lx - ", vtx, where, w1);
}

static void uc2_culldl(uint32_t w0, uint32_t w1)
{
   uint16_t i, vStart, vEnd, cond;

   vStart = (uint16_t)(w0 & 0xFFFF) >> 1;
   vEnd = (uint16_t)(w1 & 0xFFFF) >> 1;
   cond = 0;
   FRDP ("uc2:culldl start: %d, end: %d\n", vStart, vEnd);

   if (vEnd < vStart)
      return;

   for (i = vStart; i <= vEnd; i++)
   {
      /*
         VERTEX v = &rdp.vtx[i];
      // Check if completely off the screen (quick frustrum clipping for 90 FOV)
      if (v->x >= -v->w)
      cond |= 0x01;
      if (v->x <= v->w)
      cond |= 0x02;
      if (v->y >= -v->w)
      cond |= 0x04;
      if (v->y <= v->w)
      cond |= 0x08;
      if (v->w >= 0.1f)
      cond |= 0x10;

      if (cond == 0x1F)
      return;
      //*/

#ifdef EXTREME_LOGGING
      FRDP (" v[%d] = (%02f, %02f, %02f, 0x%02lx)\n", i, rdp.vtx[i].x, rdp.vtx[i].y, rdp.vtx[i].w, rdp.vtx[i].scr_off);
#endif

      cond |= (~rdp.vtx[i].scr_off) & 0x1F;
      if (cond == 0x1F)
         return;
   }

   LRDP(" - ");  // specify that the enddl is not a real command
   uc0_enddl(w0, w1);
}

static void uc2_tri1(uint32_t w0, uint32_t w1)
{
   if ((w0 & 0x00FFFFFF) == 0x17)
   {
      uc6_obj_loadtxtr(w0, w1);
      return;
   }

   if (rdp.skip_drawing)
      return;

   gsSP1Triangle(
         (w0 >> 17) & 0x7F,      /* v0 */
         (w0 >> 9)  & 0x7F,      /* v1 */
         (w0 >> 1)  & 0x7F,      /* v2 */
         0,
         true
         );
}

static void uc2_quad(uint32_t w0, uint32_t w1)
{
   if ((w0 & 0x00FFFFFF) == 0x2F)
   {
      uint32_t command = w0 >> 24;
      if (command == 0x6)
      {
         uc6_obj_ldtx_sprite(w0, w1);
         return;
      }
      if (command == 0x7)
      {
         uc6_obj_ldtx_rect(w0, w1);
         return;
      }
   }

   if (rdp.skip_drawing)
      return;

   gsSP2Triangles(
         (w0 >> 17) & 0x7F,      /* v00 */
         (w0 >> 9)  & 0x7F,      /* v01 */
         (w0 >> 1)  & 0x7F,      /* v02 */
         0,                      /* flag0 */
         (w1 >> 17) & 0x7F,      /* v10 */
         (w1 >> 9)  & 0x7F,      /* v11 */
         (w1 >> 1)  & 0x7F,      /* v12 */
         0                       /* flag1 */
         );
}

static void uc2_line3d(uint32_t w0, uint32_t w1)
{
   if ((w0 & 0xFF) == 0x2F)
      uc6_ldtx_rect_r(w0, w1);
   else
      gSPLineW3D(
            (w0 >> 9) & 0x7F,    /* v0 */
            (w0 >> 17) & 0x7F,   /* v1 */
            (w0 + 3) & 0xFF,     /* wd */
            0                    /* flags (stub) */
            );
}

static void uc2_special3(uint32_t w0, uint32_t w1)
{
   LRDP("uc2:special3\n");
}

static void uc2_special2(uint32_t w0, uint32_t w1)
{
   LRDP("uc2:special2\n");
}

static void uc2_dma_io(uint32_t w0, uint32_t w1)
{
   LRDP("uc2:dma_io\n");
}

static void uc2_pop_matrix(uint32_t w0, uint32_t w1)
{
   // Just pop the modelview matrix
   gSPPopMatrixN(w1 >> 6);
   //FRDP ("uc2:pop_matrix %08lx, %08lx\n", w0, w1);
}

static void uc2_geom_mode(uint32_t w0, uint32_t w1)
{
   // Switch around some things
   uint32_t clr_mode = (w0 & 0x00DFC9FF) |
      ((w0 & 0x00000600) << 3) |
      ((w0 & 0x00200000) >> 12) | 0xFF000000;
   uint32_t set_mode = (w1 & 0xFFDFC9FF) |
      ((w1 & 0x00000600) << 3) |
      ((w1 & 0x00200000) >> 12);

   FRDP("uc2:geom_mode c:%08lx, s:%08lx ", clr_mode, set_mode);

   rdp.geom_mode &= clr_mode;
   rdp.geom_mode |= set_mode;

   FRDP ("result:%08lx\n", rdp.geom_mode);

   if (rdp.geom_mode & G_ZBUFFER) // Z-Buffer enable
   {
      if (!(rdp.flags & ZBUF_ENABLED))
      {
         rdp.flags |= ZBUF_ENABLED;
         rdp.update |= UPDATE_ZBUF_ENABLED;
      }
   }
   else
   {
      if ((rdp.flags & ZBUF_ENABLED))
      {
         if (!settings.flame_corona || (rdp.rm != 0x00504341)) //hack for flame's corona
            rdp.flags ^= ZBUF_ENABLED;
         rdp.update |= UPDATE_ZBUF_ENABLED;
      }
   }
   if (rdp.geom_mode & CULL_FRONT) // Front culling
   {
      if (!(rdp.flags & CULL_FRONT))
      {
         rdp.flags |= CULL_FRONT;
         rdp.update |= UPDATE_CULL_MODE;
      }
   }
   else
   {
      if (rdp.flags & CULL_FRONT)
      {
         rdp.flags ^= CULL_FRONT;
         rdp.update |= UPDATE_CULL_MODE;
      }
   }
   if (rdp.geom_mode & CULL_BACK) // Back culling
   {
      if (!(rdp.flags & CULL_BACK))
      {
         rdp.flags |= CULL_BACK;
         rdp.update |= UPDATE_CULL_MODE;
      }
   }
   else
   {
      if (rdp.flags & CULL_BACK)
      {
         rdp.flags ^= CULL_BACK;
         rdp.update |= UPDATE_CULL_MODE;
      }
   }

   //Added by Gonetz
   if (rdp.geom_mode & FOG_ENABLED)      // Fog enable
   {
      if (!(rdp.flags & FOG_ENABLED))
      {
         rdp.flags |= FOG_ENABLED;
         rdp.update |= UPDATE_FOG_ENABLED;
      }
   }
   else
   {
      if (rdp.flags & FOG_ENABLED)
      {
         rdp.flags ^= FOG_ENABLED;
         rdp.update |= UPDATE_FOG_ENABLED;
      }
   }
}

static void uc2_matrix(uint32_t w0, uint32_t w1)
{
   if (!(w0 & 0x00FFFFFF))
   {
      uc6_obj_rectangle_r(w0, w1);
      return;
   }
   LRDP("uc2:matrix\n");

   DECLAREALIGN16VAR(m[4][4]);
   load_matrix(m, segoffset(w1));

   uint8_t command = (uint8_t)((w0 ^ 1) & 0xFF);
   switch (command)
   {
      case 0: // modelview mul nopush
         LRDP("modelview mul\n");
         modelview_mul (m);
         break;

      case 1: // modelview mul push
         LRDP("modelview mul push\n");
         modelview_mul_push (m);
         break;

      case 2: // modelview load nopush
         LRDP("modelview load\n");
         modelview_load (m);
         break;

      case 3: // modelview load push
         LRDP("modelview load push\n");
         modelview_load_push (m);
         break;

      case 4: // projection mul nopush
      case 5: // projection mul push, can't push projection
         LRDP("projection mul\n");
         projection_mul (m);
         break;

      case 6: // projection load nopush
      case 7: // projection load push, can't push projection
         LRDP("projection load\n");
         projection_load (m);
         break;

      default:
         FRDP_E ("Unknown matrix command, %02lx", command);
         FRDP ("Unknown matrix command, %02lx", command);
   }

#ifdef EXTREME_LOGGING
   FRDP ("{%f,%f,%f,%f}\n", m[0][0], m[0][1], m[0][2], m[0][3]);
   FRDP ("{%f,%f,%f,%f}\n", m[1][0], m[1][1], m[1][2], m[1][3]);
   FRDP ("{%f,%f,%f,%f}\n", m[2][0], m[2][1], m[2][2], m[2][3]);
   FRDP ("{%f,%f,%f,%f}\n", m[3][0], m[3][1], m[3][2], m[3][3]);
   FRDP ("\nmodel\n{%f,%f,%f,%f}\n", rdp.model[0][0], rdp.model[0][1], rdp.model[0][2], rdp.model[0][3]);
   FRDP ("{%f,%f,%f,%f}\n", rdp.model[1][0], rdp.model[1][1], rdp.model[1][2], rdp.model[1][3]);
   FRDP ("{%f,%f,%f,%f}\n", rdp.model[2][0], rdp.model[2][1], rdp.model[2][2], rdp.model[2][3]);
   FRDP ("{%f,%f,%f,%f}\n", rdp.model[3][0], rdp.model[3][1], rdp.model[3][2], rdp.model[3][3]);
   FRDP ("\nproj\n{%f,%f,%f,%f}\n", rdp.proj[0][0], rdp.proj[0][1], rdp.proj[0][2], rdp.proj[0][3]);
   FRDP ("{%f,%f,%f,%f}\n", rdp.proj[1][0], rdp.proj[1][1], rdp.proj[1][2], rdp.proj[1][3]);
   FRDP ("{%f,%f,%f,%f}\n", rdp.proj[2][0], rdp.proj[2][1], rdp.proj[2][2], rdp.proj[2][3]);
   FRDP ("{%f,%f,%f,%f}\n", rdp.proj[3][0], rdp.proj[3][1], rdp.proj[3][2], rdp.proj[3][3]);
#endif
}

static void uc2_moveword(uint32_t w0, uint32_t w1)
{
   uint8_t index = (uint8_t)((w0 >> 16) & 0xFF);
   uint16_t offset = (uint16_t)(w0 & 0xFFFF);

   FRDP ("uc2:moveword ");

   switch (index)
   {
      // NOTE: right now it's assuming that it sets the integer part first.  This could
      //  be easily fixed, but only if i had something to test with.

      case G_MW_MATRIX:  // moveword matrix
         {
            // do matrix pre-mult so it's re-updated next time
            if (rdp.update & UPDATE_MULT_MAT)
               gSPCombineMatrices();

            if (w0 & 0x20)  // fractional part
            {
               int index_x = (w0 & 0x1F) >> 1;
               int index_y = index_x >> 2;
               index_x &= 3;

               float fpart = (w1 >> 16) / 65536.0f;
               rdp.combined[index_y][index_x] = (float)(int)rdp.combined[index_y][index_x];
               rdp.combined[index_y][index_x] += fpart;

               fpart = (w1 & 0xFFFF) / 65536.0f;
               rdp.combined[index_y][index_x+1] = (float)(int)rdp.combined[index_y][index_x+1];
               rdp.combined[index_y][index_x+1] += fpart;
            }
            else
            {
               int index_x = (w0 & 0x1F) >> 1;
               int index_y = index_x >> 2;
               index_x &= 3;

               rdp.combined[index_y][index_x] = (int16_t)(w1 >> 16);
               rdp.combined[index_y][index_x+1] = (int16_t)(w1 & 0xFFFF);
            }

            LRDP("matrix\n");
         }
         break;

      case G_MW_NUMLIGHT:
         gSPNumLights( w1 / 24 );
         break;

      case G_MW_CLIP:
         gSPClipRatio(w0, w1);
         break;
      case G_MW_SEGMENT:
         if ((w1 & BMASK) < BMASK)
            gSPSegment((offset >> 2) & 0xF, w1);
         break;
      case G_MW_FOG:
         gSPFogFactor((int16_t)_SHIFTR( w1, 16, 16 ), (int16_t)_SHIFTR( w1, 0, 16 ));

         //offset must be 0 for move_fog, but it can be non zero in Nushi Zuri 64 - Shiokaze ni Notte
         //low-level display list has setothermode commands in this place, so this is obviously not move_fog.
         if (offset == 0x04)
            rdp.tlut_mode = (w1 == 0xffffffff) ? 0 : 2; 
         break;
      case G_MW_LIGHTCOL:
         gSPLightColor(offset / 24, w1);
         break;
      case G_MW_FORCEMTX:
         RDP_E ("uc2:moveword forcemtx - IGNORED\n");
         LRDP("forcemtx - IGNORED\n");
         break;

      case G_MW_PERSPNORM:
         LRDP("perspnorm - IGNORED\n");
         break;

      default:
         FRDP_E("uc2:moveword unknown (index: 0x%08lx, offset 0x%08lx)\n", index, offset);
         FRDP ("unknown (index: 0x%08lx, offset 0x%08lx)\n", index, offset);
   }
}

static void uc2_movemem(uint32_t w0, uint32_t w1)
{
   int idx = w0 & 0xFF;
   uint32_t addr = segoffset(w1);
   int ofs = (w0 >> 5) & 0x7F8;

   FRDP ("uc2:movemem ofs:%d ", ofs);

   switch (idx)
   {
      case 0:
      case 2:
         uc6_obj_movemem(w0, w1);
         break;

      case F3DEX2_MV_VIEWPORT:   // VIEWPORT
         gSPViewport(w1, false);
         break;
      case G_MV_LIGHT:  // LIGHT
         {
            uint8_t *rdram_u8;
            int8_t *rdram_s8;
            int n;
            rdram_s8 = (int8_t*)gfx.RDRAM;
            rdram_u8 = (uint8_t*)gfx.RDRAM;
            n = ofs / 24;

            if (n < 2)
            {
               int8_t dir_x, dir_y;
               dir_x = rdram_s8[(addr+8)^3];
               dir_y = rdram_s8[(addr+9)^3];
               rdp.lookat[n][0] = (float)dir_x / 127.0f;
               rdp.lookat[n][1] = (float)dir_y / 127.0f;
               rdp.lookat[n][2] = (float)(rdram_s8[(addr+10)^3]) / 127.0f;
               rdp.use_lookat = true;
               if (n == 1)
               {
                  if (!dir_x && !dir_y)
                     rdp.use_lookat = false;
               }
               FRDP("lookat_%d (%f, %f, %f)\n", n, rdp.lookat[n][0], rdp.lookat[n][1], rdp.lookat[n][2]);
               return;
            }
            n -= 2;
            if (n > 7)
               return;

            // Get the data
            rdp.light[n].r = (float)rdram_u8[(addr+0)^3] / 255.0f;
            rdp.light[n].nonblack = rdram_u8[(addr+0)^3];
            rdp.light[n].g = (float)rdram_u8[(addr+1)^3] / 255.0f;
            rdp.light[n].nonblack += rdram_u8[(addr+1)^3];
            rdp.light[n].b = (float)rdram_u8[(addr+2)^3] / 255.0f;
            rdp.light[n].nonblack += rdram_u8[(addr+2)^3];
            rdp.light[n].a = 1.0f;
            // ** Thanks to Icepir8 for pointing this out **
            // Lighting must be signed byte instead of byte
            rdp.light[n].dir_x = (float)(rdram_s8[(addr+8)^3]) / 127.0f;
            rdp.light[n].dir_y = (float)(rdram_s8[(addr+9)^3]) / 127.0f;
            rdp.light[n].dir_z = (float)(rdram_s8[(addr+10)^3]) / 127.0f;
            uint32_t a = addr >> 1;
            rdp.light[n].x = (float)(((int16_t*)gfx.RDRAM)[(a+4)^1]);
            rdp.light[n].y = (float)(((int16_t*)gfx.RDRAM)[(a+5)^1]);
            rdp.light[n].z = (float)(((int16_t*)gfx.RDRAM)[(a+6)^1]);
            rdp.light[n].ca = (float)(gfx.RDRAM[(addr+3)^3]) / 16.0f;
            rdp.light[n].la = (float)(gfx.RDRAM[(addr+7)^3]);
            rdp.light[n].qa = (float)(gfx.RDRAM[(addr+14)^3]) / 8.0f;
#ifdef EXTREME_LOGGING
            FRDP ("light: n: %d, pos: x: %f, y: %f, z: %f, ca: %f, la:%f, qa: %f\n",
                  n, rdp.light[n].x, rdp.light[n].y, rdp.light[n].z, rdp.light[n].ca, rdp.light[n].la, rdp.light[n].qa);
#endif
            FRDP ("light: n: %d, r: %.3f, g: %.3f, b: %.3f. dir: x: %.3f, y: %.3f, z: %.3f\n",
                  n, rdp.light[n].r, rdp.light[n].g, rdp.light[n].b,
                  rdp.light[n].dir_x, rdp.light[n].dir_y, rdp.light[n].dir_z);
         }
         break;

      case G_MV_MATRIX:
         gSPForceMatrix(w1);
         break;
      default:
         FRDP ("uc2:matrix unknown (%d)\n", idx);
         FRDP ("** UNKNOWN %d\n", idx);
   }
}

static void uc2_load_ucode(uint32_t w0, uint32_t w1)
{
   LRDP("uc2:load_ucode\n");
}

static void uc2_rdphalf_2(uint32_t w0, uint32_t w1)
{
   LRDP("uc2:rdphalf_2\n");
}

static void uc2_dlist_cnt(uint32_t w0, uint32_t w1)
{
   uint32_t addr = segoffset(w1) & BMASK;
   int count = w0 & 0x000000FF;

   if (rdp.pc_i >= 9 || addr == 0)
      return;

   rdp.pc_i ++;  // go to the next PC in the stack
   rdp.pc[rdp.pc_i] = addr;  // jump to the address
   rdp.dl_count = count + 1;
   FRDP ("dl_count - addr: %08lx, count: %d\n", addr, count);
}

static void uc2_setothermode_l(uint32_t w0, uint32_t w1)
{

   int shift, len;
   len = (w0 & 0xFF) + 1;
   shift = 32 - ((w0 >> 8) & 0xFF) - len;
   if (shift < 0)
      shift = 0;

   uint32_t mask = 0;
   int i = len;
   for (; i; i--)
      mask = (mask << 1) | 1;
   mask <<= shift;

   rdp.cmd1 &= mask;
   rdp.othermode_l &= ~mask;
   rdp.othermode_l |= rdp.cmd1;

   if (mask & 0x00000003)  // alpha compare
      gDPSetAlphaCompare(w1 >> G_MDSFT_ALPHACOMPARE);

   if (mask & ZBUF_COMPARE)  // z-src selection
      gDPSetDepthSource(w1 >> G_MDSFT_ZSRCSEL);

   if (mask & 0xFFFFFFF8)  // rendermode / blender bits
      gDPSetRenderMode(w1 & 0xCCCCFFFF, w1 & 0x3333FFFF);

   // there is not one setothermode_l that's not handled :)
   //LRDP("uc2:setothermode_l ");
}

static void uc2_setothermode_h(uint32_t w0, uint32_t w1)
{
   int shift, len;
   len = (w0 & 0xFF) + 1;
   shift = 32 - ((w0 >> 8) & 0xFF) - len;

   uint32_t mask = 0;
   int i = len;
   for (; i; i--)
      mask = (mask << 1) | 1;
   mask <<= shift;

   rdp.cmd1 &= mask;
   rdp.othermode_h &= ~mask;
   rdp.othermode_h |= rdp.cmd1;

   if (mask & 0x00000030)  // alpha dither mode
   {
      rdp.alpha_dither_mode = (rdp.othermode_h >> G_MDSFT_ALPHADITHER) & 0x3;
      //FRDP ("alpha dither mode: %s\n", str_dither[rdp.alpha_dither_mode]);
   }

#ifndef NDEBUG
   if (mask & 0x000000C0)  // rgb dither mode
   {
      uint32_t dither_mode = (rdp.othermode_h >> G_MDSFT_RGBDITHER) & 0x3;
      //FRDP ("rgb dither mode: %s\n", str_dither[dither_mode]);
   }
#endif

   if (mask & 0x00003000)  // filter mode
   {
      rdp.filter_mode = (int)((rdp.othermode_h & 0x00003000) >> 12);
      rdp.update |= UPDATE_TEXTURE;
      //FRDP ("filter mode: %s\n", str_filter[rdp.filter_mode]);
   }

   if (mask & 0x0000C000)  // tlut mode
   {
      rdp.tlut_mode = (uint8_t)((rdp.othermode_h & 0x0000C000) >> 14);
      //FRDP ("tlut mode: %s\n", str_tlut[rdp.tlut_mode]);
   }

   if (mask & 0x00300000)  // cycle type
   {
      rdp.cycle_mode = (uint8_t)((rdp.othermode_h & 0x00300000) >> 20);
      rdp.update |= UPDATE_ZBUF_ENABLED;
      //FRDP ("cycletype: %d\n", rdp.cycle_mode);
   }

   if (mask & G_LOD)  // LOD enable
   {
      rdp.LOD_en = (rdp.othermode_h & G_LOD) ? true : false;
      //FRDP ("LOD_en: %d\n", rdp.LOD_en);
   }

   if (mask & G_TEXTURE_GEN_LINEAR)  // Persp enable
   {
      if (rdp.persp_supported)
         rdp.Persp_en = (rdp.othermode_h & G_TEXTURE_GEN_LINEAR) ? true : false;
      //FRDP ("Persp_en: %d\n", rdp.Persp_en);
   }

#ifndef NDEBUG
   uint32_t unk = mask & 0x0FFC60F0F;
   if (unk)  // unknown portions, LARGE
   {
      //FRDP ("UNKNOWN PORTIONS: shift: %d, len: %d, unknowns: %08lx\n", shift, len, unk);
   }
#endif
   //LRDP("uc2:setothermode_h: ");
}
