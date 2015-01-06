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
// January 2004 Created by Gonetz (Gonetz@ngs.ru)
//
//****************************************************************

uint32_t uc8_normale_addr = 0;
float uc8_coord_mod[16];

static void uc8_vertex(uint32_t w0, uint32_t w1)
{
   uint32_t l;
   int32_t i;
   float x, y, z;
#ifdef __ARM_NEON__
   float32x4_t comb0, comb1, comb2, comb3;
   float32x4_t v_xyzw;
#endif
   uint32_t addr = segoffset(w1);
   int32_t n = (w0 >> 12) & 0xFF;
   int32_t v0 = ((w0 >> 1) & 0x7F) - n;
   void   *membase_ptr  = (void*)(gfx_info.RDRAM + addr);
   uint32_t iter = 16;

   if (v0 < 0)
      return;

   pre_update();

#ifdef __ARM_NEON__
   comb0 = vld1q_f32(rdp.combined[0]);
   comb1 = vld1q_f32(rdp.combined[1]);
   comb2 = vld1q_f32(rdp.combined[2]);
   comb3 = vld1q_f32(rdp.combined[3]);
#endif

   for (i=0; i < (n * iter); i+= iter)
   {
      VERTEX *vert = (VERTEX*)&rdp.vtx[v0 + (i / iter)];
      int16_t *rdram    = (int16_t*)membase_ptr;
      int8_t  *rdram_s8 = (int8_t* )membase_ptr;
      uint8_t *rdram_u8 = (uint8_t*)membase_ptr;
      uint8_t *color = (uint8_t*)(rdram_u8 + 12);
      y                 = (float)rdram[0];
      x                 = (float)rdram[1];
      vert->flags       = (uint16_t)rdram[2];
      z                 = (float)rdram[3];
      vert->ov          = (float)rdram[4];
      vert->ou          = (float)rdram[5];
      vert->uv_scaled   = 0;
      vert->a           = color[0];

#ifdef __ARM_NEON__
      v_xyzw  = vmulq_n_f32(comb0,x)+vmulq_n_f32(comb1,y)+vmulq_n_f32(comb2,z)+comb3;
      vert->x = vgetq_lane_f32(v_xyzw,0);
      vert->y = vgetq_lane_f32(v_xyzw,1);
      vert->z = vgetq_lane_f32(v_xyzw,2);
      vert->w = vgetq_lane_f32(v_xyzw,3);
#else
      vert->x = x*rdp.combined[0][0] + y*rdp.combined[1][0] + z*rdp.combined[2][0] + rdp.combined[3][0];
      vert->y = x*rdp.combined[0][1] + y*rdp.combined[1][1] + z*rdp.combined[2][1] + rdp.combined[3][1];
      vert->z = x*rdp.combined[0][2] + y*rdp.combined[1][2] + z*rdp.combined[2][2] + rdp.combined[3][2];
      vert->w = x*rdp.combined[0][3] + y*rdp.combined[1][3] + z*rdp.combined[2][3] + rdp.combined[3][3];
#endif

      vert->uv_calculated = 0xFFFFFFFF;
      vert->screen_translated = 0;
      vert->shade_mod = 0;

      if (fabs(vert->w) < 0.001)
         vert->w = 0.001f;
      vert->oow = 1.0f / vert->w;
#ifdef __ARM_NEON__
      v_xyzw = vmulq_n_f32(v_xyzw,vert->oow);
      vert->x_w=vgetq_lane_f32(v_xyzw,0);
      vert->y_w=vgetq_lane_f32(v_xyzw,1);
      vert->z_w=vgetq_lane_f32(v_xyzw,2);
#else
      vert->x_w = vert->x * vert->oow;
      vert->y_w = vert->y * vert->oow;
      vert->z_w = vert->z * vert->oow;
#endif

      vert->scr_off = 0;
      if (vert->x < -vert->w)
         vert->scr_off |= 1;
      if (vert->x > vert->w)
         vert->scr_off |= 2;
      if (vert->y < -vert->w)
         vert->scr_off |= 4;
      if (vert->y > vert->w)
         vert->scr_off |= 8;
      if (vert->w < 0.1f)
         vert->scr_off |= 16;

      vert->r = color[3];
      vert->g = color[2];
      vert->b = color[1];

      if ((rdp.geom_mode & G_LIGHTING))
      {
         uint32_t shift, l;
         float light_intensity, color[3];

         shift = v0 << 1;
         vert->vec[0] = ((int8_t*)gfx_info.RDRAM)[(uc8_normale_addr + (i>>3) + shift + 0)^3];
         vert->vec[1] = ((int8_t*)gfx_info.RDRAM)[(uc8_normale_addr + (i>>3) + shift + 1)^3];
         vert->vec[2] = (int8_t)(vert->flags & 0xff);

         if (rdp.geom_mode & G_TEXTURE_GEN_LINEAR)
            calc_linear (vert);
         else if (rdp.geom_mode & G_TEXTURE_GEN)
            calc_sphere (vert);

         color[0] = rdp.light[rdp.num_lights].col[0];
         color[1] = rdp.light[rdp.num_lights].col[1];
         color[2] = rdp.light[rdp.num_lights].col[2];

         light_intensity = 0.0f;
         if (rdp.geom_mode & 0x00400000)
         {
            NormalizeVector (vert->vec);
            for (l = 0; l < rdp.num_lights-1; l++)
            {
               if (!rdp.light[l].nonblack)
                  continue;
               light_intensity = DotProduct (rdp.light_vector[l], vert->vec);
               FRDP("light %d, intensity : %f\n", l, light_intensity);
               if (light_intensity < 0.0f)
                  continue;
               //*
               if (rdp.light[l].ca > 0.0f)
               {
                  float vx = (vert->x + uc8_coord_mod[8])*uc8_coord_mod[12] - rdp.light[l].x;
                  float vy = (vert->y + uc8_coord_mod[9])*uc8_coord_mod[13] - rdp.light[l].y;
                  float vz = (vert->z + uc8_coord_mod[10])*uc8_coord_mod[14] - rdp.light[l].z;
                  float vw = (vert->w + uc8_coord_mod[11])*uc8_coord_mod[15] - rdp.light[l].w;
                  float len = (vx*vx+vy*vy+vz*vz+vw*vw)/65536.0f;
                  float p_i = rdp.light[l].ca / len;
                  if (p_i > 1.0f) p_i = 1.0f;
                  light_intensity *= p_i;
               }
               //*/
               color[0] += rdp.light[l].col[0] * light_intensity;
               color[1] += rdp.light[l].col[1] * light_intensity;
               color[2] += rdp.light[l].col[2] * light_intensity;
            }
            light_intensity = DotProduct (rdp.light_vector[l], vert->vec);
            if (light_intensity > 0.0f)
            {
               color[0] += rdp.light[l].col[0] * light_intensity;
               color[1] += rdp.light[l].col[1] * light_intensity;
               color[2] += rdp.light[l].col[2] * light_intensity;
            }
         }
         else
         {
            for (l = 0; l < rdp.num_lights; l++)
            {
               if (rdp.light[l].nonblack && rdp.light[l].nonzero)
               {
                  float vx = (vert->x + uc8_coord_mod[8])*uc8_coord_mod[12] - rdp.light[l].x;
                  float vy = (vert->y + uc8_coord_mod[9])*uc8_coord_mod[13] - rdp.light[l].y;
                  float vz = (vert->z + uc8_coord_mod[10])*uc8_coord_mod[14] - rdp.light[l].z;
                  float vw = (vert->w + uc8_coord_mod[11])*uc8_coord_mod[15] - rdp.light[l].w;
                  float len = (vx*vx+vy*vy+vz*vz+vw*vw)/65536.0f;
                  light_intensity = rdp.light[l].ca / len;
                  if (light_intensity > 1.0f) light_intensity = 1.0f;
                  color[0] += rdp.light[l].col[0] * light_intensity;
                  color[1] += rdp.light[l].col[1] * light_intensity;
                  color[2] += rdp.light[l].col[2] * light_intensity;
               }
            }
         }
         if (color[0] > 1.0f) color[0] = 1.0f;
         if (color[1] > 1.0f) color[1] = 1.0f;
         if (color[2] > 1.0f) color[2] = 1.0f;
         vert->r = (uint8_t)(((float)vert->r)*color[0]);
         vert->g = (uint8_t)(((float)vert->g)*color[1]);
         vert->b = (uint8_t)(((float)vert->b)*color[2]);
      }
      membase_ptr = (char*)membase_ptr + iter;
   }
}

static void uc8_moveword(uint32_t w0, uint32_t w1)
{
   int k;
   uint8_t index = (uint8_t)((w0 >> 16) & 0xFF);
   uint16_t offset = (uint16_t)(w0 & 0xFFFF);

   FRDP ("uc8:moveword ");

   switch (index)
   {
      // NOTE: right now it's assuming that it sets the integer part first.  This could
      //  be easily fixed, but only if i had something to test with.

      case G_MW_NUMLIGHT:
         /* inlined version of gSPNumLights here because the conditional in gSPNumLights would
          * cause a segfault here - so set rdp.num_lights directly */
         rdp.num_lights = (w1 / 48);
         rdp.update |= UPDATE_LIGHTS;
         //FRDP ("numlights: %d\n", rdp.num_lights);
         break;

      case G_MW_CLIP:
         if (offset == 0x04)
         {
            rdp.clip_ratio = (float)vi_integer_sqrt(w1);
            rdp.update |= UPDATE_VIEWPORT;
         }
         break;

      case G_MW_SEGMENT:
         //FRDP ("SEGMENT %08lx -> seg%d\n", w1, offset >> 2);
         rdp.segment[(offset >> 2) & 0xF] = w1;
         break;

      case G_MW_FOG:
         rdp.fog_multiplier = (short)(w1 >> 16);
         rdp.fog_offset = (short)(w1 & 0x0000FFFF);
         break;

      case 0x0c:
         RDP_E ("uc8:moveword forcemtx - IGNORED\n");
         LRDP("forcemtx - IGNORED\n");
         break;

      case 0x0e:
         LRDP("perspnorm - IGNORED\n");
         break;

      case G_MV_COORDMOD:  // moveword coord mod
         {
            uint32_t idx, pos;
            uint8_t n;
			n = offset >> 2;

            FRDP ("coord mod:%d, %08lx\n", n, w1);
            if (w0 & 8)
               return;
            idx = (w0 >> 1)&3;
            pos = w0 & 0x30;
            if (pos == 0)
            {
               uc8_coord_mod[0+idx] = (int16_t)(w1 >> 16);
               uc8_coord_mod[1+idx] = (int16_t)(w1 & 0xffff);
            }
            else if (pos == 0x10)
            {
               uc8_coord_mod[4+idx] = (w1 >> 16) / 65536.0f;
               uc8_coord_mod[5+idx] = (w1 & 0xffff) / 65536.0f;
               uc8_coord_mod[12+idx] = uc8_coord_mod[0+idx] + uc8_coord_mod[4+idx];
               uc8_coord_mod[13+idx] = uc8_coord_mod[1+idx] + uc8_coord_mod[5+idx];

            }
            else if (pos == 0x20)
            {
               uc8_coord_mod[8+idx] = (int16_t)(w1 >> 16);
               uc8_coord_mod[9+idx] = (int16_t)(w1 & 0xffff);
#ifdef EXTREME_LOGGING
               if (idx)
               {
                  for (k = 8; k < 16; k++)
                     FRDP("coord_mod[%d]=%f\n", k, uc8_coord_mod[k]);
               }
#endif
            }

         }
         break;

      default:
         FRDP_E("uc8:moveword unknown (index: 0x%08lx, offset 0x%08lx)\n", index, offset);
         FRDP ("unknown (index: 0x%08lx, offset 0x%08lx)\n", index, offset);
   }
}

static void uc8_movemem(uint32_t w0, uint32_t w1)
{
   int i, t;
   int idx = w0 & 0xFF;
   uint32_t addr = segoffset(w1);
   int ofs = (w0 >> 5) & 0x3FFF;

   FRDP ("uc8:movemem ofs:%d ", ofs);

   switch (idx)
   {
      case F3DCBFD_MV_VIEWPORT:   // VIEWPORT
         {
            uint32_t a = addr >> 1;
            int16_t scale_x = ((int16_t*)gfx_info.RDRAM)[(a+0)^1] >> 2;
            int16_t scale_y = ((int16_t*)gfx_info.RDRAM)[(a+1)^1] >> 2;
            int16_t scale_z = ((int16_t*)gfx_info.RDRAM)[(a+2)^1];
            int16_t trans_x = ((int16_t*)gfx_info.RDRAM)[(a+4)^1] >> 2;
            int16_t trans_y = ((int16_t*)gfx_info.RDRAM)[(a+5)^1] >> 2;
            int16_t trans_z = ((int16_t*)gfx_info.RDRAM)[(a+6)^1];
            rdp.view_scale[0] = scale_x * rdp.scale_x;
            rdp.view_scale[1] = -scale_y * rdp.scale_y;
            rdp.view_scale[2] = 32.0f * scale_z;
            rdp.view_trans[0] = trans_x * rdp.scale_x;
            rdp.view_trans[1] = trans_y * rdp.scale_y;
            rdp.view_trans[2] = 32.0f * trans_z;

            rdp.update |= UPDATE_VIEWPORT;

            FRDP ("viewport scale(%d, %d), trans(%d, %d), from:%08lx\n", scale_x, scale_y,
                  trans_x, trans_y, a);
         }
         break;

      case F3DCBFD_MV_LIGHT:  // LIGHT
         {
            int n;
			uint32_t a;

			n = (ofs / 48);
            if (n < 2)
            {
               int8_t dir_x, dir_y, dir_z;
               dir_x = ((int8_t*)gfx_info.RDRAM)[(addr+8)^3];
               rdp.lookat[n][0] = (float)(dir_x) / 127.0f;
               dir_y = ((int8_t*)gfx_info.RDRAM)[(addr+9)^3];
               rdp.lookat[n][1] = (float)(dir_y) / 127.0f;
               dir_z = ((int8_t*)gfx_info.RDRAM)[(addr+10)^3];
               rdp.lookat[n][2] = (float)(dir_z) / 127.0f;
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
            rdp.light[n].nonblack = gfx_info.RDRAM[(addr+0)^3];
            rdp.light[n].nonblack += gfx_info.RDRAM[(addr+1)^3];
            rdp.light[n].nonblack += gfx_info.RDRAM[(addr+2)^3];

            rdp.light[n].col[0] = (((uint8_t*)gfx_info.RDRAM)[(addr+0)^3]) / 255.0f;
            rdp.light[n].col[1] = (((uint8_t*)gfx_info.RDRAM)[(addr+1)^3]) / 255.0f;
            rdp.light[n].col[2] = (((uint8_t*)gfx_info.RDRAM)[(addr+2)^3]) / 255.0f;
            rdp.light[n].col[3] = 1.0f;

            // ** Thanks to Icepir8 for pointing this out **
            // Lighting must be signed byte instead of byte
            rdp.light[n].dir[0] = (float)(((int8_t*)gfx_info.RDRAM)[(addr+8)^3]) / 127.0f;
            rdp.light[n].dir[1] = (float)(((int8_t*)gfx_info.RDRAM)[(addr+9)^3]) / 127.0f;
            rdp.light[n].dir[2] = (float)(((int8_t*)gfx_info.RDRAM)[(addr+10)^3]) / 127.0f;
            // **
            a = addr >> 1;
            rdp.light[n].x = (float)(((int16_t*)gfx_info.RDRAM)[(a+16)^1]);
            rdp.light[n].y = (float)(((int16_t*)gfx_info.RDRAM)[(a+17)^1]);
            rdp.light[n].z = (float)(((int16_t*)gfx_info.RDRAM)[(a+18)^1]);
            rdp.light[n].w = (float)(((int16_t*)gfx_info.RDRAM)[(a+19)^1]);
            rdp.light[n].nonzero = gfx_info.RDRAM[(addr+12)^3];
            rdp.light[n].ca = (float)rdp.light[n].nonzero / 16.0f;
            //rdp.light[n].la = rdp.light[n].ca * 1.0f;
#ifdef EXTREME_LOGGING
            FRDP ("light: n: %d, pos: x: %f, y: %f, z: %f, w: %f, ca: %f\n",
                  n, rdp.light[n].x, rdp.light[n].y, rdp.light[n].z, rdp.light[n].w, rdp.light[n].ca);
#endif
            FRDP ("light: n: %d, r: %f, g: %f, b: %f. dir: x: %.3f, y: %.3f, z: %.3f\n",
                  n, rdp.light[n].r, rdp.light[n].g, rdp.light[n].b,
                  rdp.light[n].dir_x, rdp.light[n].dir_y, rdp.light[n].dir_z);
#ifdef EXTREME_LOGGING
            for (t = 0; t < 24; t++)
               FRDP ("light[%d] = 0x%04lx \n", t, ((uint16_t*)gfx_info.RDRAM)[(a+t)^1]);
#endif
         }
         break;

      case F3DCBFD_MV_NORMAL: //Normals
         {
            uc8_normale_addr = segoffset(w1);
            FRDP ("Normals - addr: %08lx\n", uc8_normale_addr);
#ifdef EXTREME_LOGGING
            for (i = 0; i < 32; i++)
            {
               int8_t x = ((int8_t*)gfx_info.RDRAM)[uc8_normale_addr + ((i<<1) + 0)^3];
               int8_t y = ((int8_t*)gfx_info.RDRAM)[uc8_normale_addr + ((i<<1) + 1)^3];
               FRDP("#%d x = %d, y = %d\n", i, x, y);
            }
            uint32_t a = uc8_normale_addr >> 1;
            for (i = 0; i < 32; i++)
            {
               FRDP ("n[%d] = 0x%04lx \n", i, ((uint16_t*)gfx_info.RDRAM)[(a+i)^1]);
            }
#endif
         }
         break;

      default:
         FRDP ("uc8:movemem unknown (%d)\n", idx);
   }
}

static void uc8_tri4(uint32_t w0, uint32_t w1) //by Gugaman Apr 19 2002
{
   VERTEX *v[12];

   if (rdp.skip_drawing)
      return;

   v[0]  = &rdp.vtx[(w0 >> 23) & 0x1F]; /* v00 */
   v[1]  = &rdp.vtx[(w0 >> 18) & 0x1F]; /* v01 */
   v[2]  = &rdp.vtx[((((w0 >> 15) & 0x7) << 2) | ((w1 >> 30) &0x3))]; /* v02 */
   v[3]  = &rdp.vtx[(w0 >> 10) & 0x1F]; /* v10 */
   v[4]  = &rdp.vtx[(w0 >> 5) & 0x1F];  /* v11 */
   v[5]  = &rdp.vtx[(w0 >> 0) & 0x1F];  /* v12 */
   v[6]  = &rdp.vtx[(w1 >> 25) & 0x1F]; /* v20 */
   v[7]  = &rdp.vtx[(w1 >> 20) & 0x1F]; /* v21 */
   v[8]  = &rdp.vtx[(w1 >> 15) & 0x1F]; /* v22 */
   v[9]  = &rdp.vtx[(w1 >> 10) & 0x1F]; /* v30 */
   v[10] = &rdp.vtx[(w1 >> 5) & 0x1F];  /* v31 */
   v[11] = &rdp.vtx[(w1 >> 0) & 0x1F];  /* v32 */

   cull_trianglefaces(v, 4, true, true, 0);
}
