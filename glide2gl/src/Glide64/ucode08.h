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
   if (rdp.update & UPDATE_MULT_MAT)
      gSPCombineMatrices();

   uint32_t l;
   uint32_t addr = segoffset(w1);
   int v0, i, n;
   float x, y, z;

   rdp.vn = n = (w0 >> 12) & 0xFF;
   rdp.v0 = v0 = ((w0 >> 1) & 0x7F) - n;

   FRDP ("uc8:vertex n: %d, v0: %d, from: %08lx\n", n, v0, addr);

   if (v0 < 0)
   {
      RDP_E ("** ERROR: uc2:vertex v0 < 0\n");
      LRDP("** ERROR: uc2:vertex v0 < 0\n");
      return;
   }
   //*
   // This is special, not handled in update()
   if (rdp.update & UPDATE_LIGHTS)
   {
      rdp.update ^= UPDATE_LIGHTS;

      // Calculate light vectors
      for (l = 0; l < rdp.num_lights; l++)
      {
         InverseTransformVector(&rdp.light[l].dir_x, rdp.light_vector[l], rdp.model);
         NormalizeVector (rdp.light_vector[l]);
#ifdef EXTREME_LOGGING
         FRDP("light_vector[%d] x: %f, y: %f, z: %f\n", l, rdp.light_vector[l][0], rdp.light_vector[l][1], rdp.light_vector[l][2]);
#endif
      }
   }
   //*/
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

#ifdef EXTREME_LOGGING
      FRDP ("before v%d - x: %f, y: %f, z: %f\n", i>>4, x, y, z);
#endif
      v->x = x*rdp.combined[0][0] + y*rdp.combined[1][0] + z*rdp.combined[2][0] + rdp.combined[3][0];
      v->y = x*rdp.combined[0][1] + y*rdp.combined[1][1] + z*rdp.combined[2][1] + rdp.combined[3][1];
      v->z = x*rdp.combined[0][2] + y*rdp.combined[1][2] + z*rdp.combined[2][2] + rdp.combined[3][2];
      v->w = x*rdp.combined[0][3] + y*rdp.combined[1][3] + z*rdp.combined[2][3] + rdp.combined[3][3];

#ifdef EXTREME_LOGGING
      FRDP ("v%d - x: %f, y: %f, z: %f, w: %f, u: %f, v: %f, flags: %d\n", i>>4, v->x, v->y, v->z, v->w, v->ou, v->ov, v->flags);
#endif

      if (fabs(v->w) < 0.001) v->w = 0.001f;
      v->oow = 1.0f / v->w;
      v->x_w = v->x * v->oow;
      v->y_w = v->y * v->oow;
      v->z_w = v->z * v->oow;

      v->uv_calculated = 0xFFFFFFFF;
      v->screen_translated = 0;
      v->shade_mod = 0;

      v->scr_off = 0;
      if (v->x < -v->w) v->scr_off |= 1;
      if (v->x > v->w) v->scr_off |= 2;
      if (v->y < -v->w) v->scr_off |= 4;
      if (v->y > v->w) v->scr_off |= 8;
      if (v->w < 0.1f) v->scr_off |= 16;
      ///*
      v->r = ((uint8_t*)gfx.RDRAM)[(addr+i + 12)^3];
      v->g = ((uint8_t*)gfx.RDRAM)[(addr+i + 13)^3];
      v->b = ((uint8_t*)gfx.RDRAM)[(addr+i + 14)^3];
#ifdef EXTREME_LOGGING
      FRDP ("r: %02lx, g: %02lx, b: %02lx, a: %02lx\n", v->r, v->g, v->b, v->a);
#endif

      if ((rdp.geom_mode & G_LIGHTING))
      {
         uint32_t shift = v0 << 1;
         v->vec[0] = ((int8_t*)gfx.RDRAM)[(uc8_normale_addr + (i>>3) + shift + 0)^3];
         v->vec[1] = ((int8_t*)gfx.RDRAM)[(uc8_normale_addr + (i>>3) + shift + 1)^3];
         v->vec[2] = (int8_t)(v->flags&0xff);

         if (rdp.geom_mode & G_TEXTURE_GEN_LINEAR)
            calc_linear (v);
         else if (rdp.geom_mode & G_TEXTURE_GEN)
            calc_sphere (v);
         //     FRDP("calc light. r: 0x%02lx, g: 0x%02lx, b: 0x%02lx, nx: %.3f, ny: %.3f, nz: %.3f\n", v->r, v->g, v->b, v->vec[0], v->vec[1], v->vec[2]);
         FRDP("v[%d] calc light. r: 0x%02lx, g: 0x%02lx, b: 0x%02lx\n", i>>4, v->r, v->g, v->b);
         float color[3] = {rdp.light[rdp.num_lights].r, rdp.light[rdp.num_lights].g, rdp.light[rdp.num_lights].b};
         FRDP("ambient light. r: %f, g: %f, b: %f\n", color[0], color[1], color[2]);
         float light_intensity = 0.0f;
         uint32_t l;
         if (rdp.geom_mode & 0x00400000)
         {
            NormalizeVector (v->vec);
            for (l = 0; l < rdp.num_lights-1; l++)
            {
               if (!rdp.light[l].nonblack)
                  continue;
               light_intensity = DotProduct (rdp.light_vector[l], v->vec);
               FRDP("light %d, intensity : %f\n", l, light_intensity);
               if (light_intensity < 0.0f)
                  continue;
               //*
               if (rdp.light[l].ca > 0.0f)
               {
                  float vx = (v->x + uc8_coord_mod[8])*uc8_coord_mod[12] - rdp.light[l].x;
                  float vy = (v->y + uc8_coord_mod[9])*uc8_coord_mod[13] - rdp.light[l].y;
                  float vz = (v->z + uc8_coord_mod[10])*uc8_coord_mod[14] - rdp.light[l].z;
                  float vw = (v->w + uc8_coord_mod[11])*uc8_coord_mod[15] - rdp.light[l].w;
                  float len = (vx*vx+vy*vy+vz*vz+vw*vw)/65536.0f;
                  float p_i = rdp.light[l].ca / len;
                  if (p_i > 1.0f) p_i = 1.0f;
                  light_intensity *= p_i;
                  FRDP("light %d, len: %f, p_intensity : %f\n", l, len, p_i);
               }
               //*/
               color[0] += rdp.light[l].r * light_intensity;
               color[1] += rdp.light[l].g * light_intensity;
               color[2] += rdp.light[l].b * light_intensity;
               FRDP("light %d r: %f, g: %f, b: %f\n", l, color[0], color[1], color[2]);
            }
            light_intensity = DotProduct (rdp.light_vector[l], v->vec);
            FRDP("light %d, intensity : %f\n", l, light_intensity);
            if (light_intensity > 0.0f)
            {
               color[0] += rdp.light[l].r * light_intensity;
               color[1] += rdp.light[l].g * light_intensity;
               color[2] += rdp.light[l].b * light_intensity;
            }
            FRDP("light %d r: %f, g: %f, b: %f\n", l, color[0], color[1], color[2]);
         }
         else
         {
            for (l = 0; l < rdp.num_lights; l++)
            {
               if (rdp.light[l].nonblack && rdp.light[l].nonzero)
               {
                  float vx = (v->x + uc8_coord_mod[8])*uc8_coord_mod[12] - rdp.light[l].x;
                  float vy = (v->y + uc8_coord_mod[9])*uc8_coord_mod[13] - rdp.light[l].y;
                  float vz = (v->z + uc8_coord_mod[10])*uc8_coord_mod[14] - rdp.light[l].z;
                  float vw = (v->w + uc8_coord_mod[11])*uc8_coord_mod[15] - rdp.light[l].w;
                  float len = (vx*vx+vy*vy+vz*vz+vw*vw)/65536.0f;
                  light_intensity = rdp.light[l].ca / len;
                  if (light_intensity > 1.0f) light_intensity = 1.0f;
                  FRDP("light %d, p_intensity : %f\n", l, light_intensity);
                  color[0] += rdp.light[l].r * light_intensity;
                  color[1] += rdp.light[l].g * light_intensity;
                  color[2] += rdp.light[l].b * light_intensity;
                  //FRDP("light %d r: %f, g: %f, b: %f\n", l, color[0], color[1], color[2]);
               }
            }
         }
         if (color[0] > 1.0f) color[0] = 1.0f;
         if (color[1] > 1.0f) color[1] = 1.0f;
         if (color[2] > 1.0f) color[2] = 1.0f;
         v->r = (uint8_t)(((float)v->r)*color[0]);
         v->g = (uint8_t)(((float)v->g)*color[1]);
         v->b = (uint8_t)(((float)v->b)*color[2]);
#ifdef EXTREME_LOGGING
         FRDP("color after light: r: 0x%02lx, g: 0x%02lx, b: 0x%02lx\n", v->r, v->g, v->b);
#endif
      }
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
         gSPClipRatio(w0, w1);
         break;

      case G_MW_SEGMENT:
         gSPSegment((offset >> 2) & 0xF, w1);
         break;

      case G_MW_FOG:
         gSPFogFactor((int16_t)_SHIFTR( w1, 16, 16 ), (int16_t)_SHIFTR( w1, 0, 16 ));
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
            uint8_t n = offset >> 2;

            FRDP ("coord mod:%d, %08lx\n", n, w1);
            if (w0 & 8)
               return;
            uint32_t idx = (w0 >> 1)&3;
            uint32_t pos = w0 & 0x30;
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
            int16_t scale_x = ((int16_t*)gfx.RDRAM)[(a+0)^1] >> 2;
            int16_t scale_y = ((int16_t*)gfx.RDRAM)[(a+1)^1] >> 2;
            int16_t scale_z = ((int16_t*)gfx.RDRAM)[(a+2)^1];
            int16_t trans_x = ((int16_t*)gfx.RDRAM)[(a+4)^1] >> 2;
            int16_t trans_y = ((int16_t*)gfx.RDRAM)[(a+5)^1] >> 2;
            int16_t trans_z = ((int16_t*)gfx.RDRAM)[(a+6)^1];
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
            int n = (ofs / 48);
            if (n < 2)
            {
               int8_t dir_x = ((int8_t*)gfx.RDRAM)[(addr+8)^3];
               rdp.lookat[n][0] = (float)(dir_x) / 127.0f;
               int8_t dir_y = ((int8_t*)gfx.RDRAM)[(addr+9)^3];
               rdp.lookat[n][1] = (float)(dir_y) / 127.0f;
               int8_t dir_z = ((int8_t*)gfx.RDRAM)[(addr+10)^3];
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
            uint8_t col = gfx.RDRAM[(addr+0)^3];
            rdp.light[n].r = (float)col / 255.0f;
            rdp.light[n].nonblack = col;
            col = gfx.RDRAM[(addr+1)^3];
            rdp.light[n].g = (float)col / 255.0f;
            rdp.light[n].nonblack += col;
            col = gfx.RDRAM[(addr+2)^3];
            rdp.light[n].b = (float)col / 255.0f;
            rdp.light[n].nonblack += col;
            rdp.light[n].a = 1.0f;
            rdp.light[n].dir_x = (float)(((int8_t*)gfx.RDRAM)[(addr+8)^3]) / 127.0f;
            rdp.light[n].dir_y = (float)(((int8_t*)gfx.RDRAM)[(addr+9)^3]) / 127.0f;
            rdp.light[n].dir_z = (float)(((int8_t*)gfx.RDRAM)[(addr+10)^3]) / 127.0f;
            // **
            uint32_t a = addr >> 1;
            rdp.light[n].x = (float)(((int16_t*)gfx.RDRAM)[(a+16)^1]);
            rdp.light[n].y = (float)(((int16_t*)gfx.RDRAM)[(a+17)^1]);
            rdp.light[n].z = (float)(((int16_t*)gfx.RDRAM)[(a+18)^1]);
            rdp.light[n].w = (float)(((int16_t*)gfx.RDRAM)[(a+19)^1]);
            rdp.light[n].nonzero = gfx.RDRAM[(addr+12)^3];
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
               FRDP ("light[%d] = 0x%04lx \n", t, ((uint16_t*)gfx.RDRAM)[(a+t)^1]);
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
               int8_t x = ((int8_t*)gfx.RDRAM)[uc8_normale_addr + ((i<<1) + 0)^3];
               int8_t y = ((int8_t*)gfx.RDRAM)[uc8_normale_addr + ((i<<1) + 1)^3];
               FRDP("#%d x = %d, y = %d\n", i, x, y);
            }
            uint32_t a = uc8_normale_addr >> 1;
            for (i = 0; i < 32; i++)
            {
               FRDP ("n[%d] = 0x%04lx \n", i, ((uint16_t*)gfx.RDRAM)[(a+i)^1]);
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

   v[0] = &rdp.vtx[(w0 >> 23) & 0x1F];
   v[1] = &rdp.vtx[(w0 >> 18) & 0x1F];
   v[2] = &rdp.vtx[((((w0 >> 15) & 0x7) << 2) | ((w1 >> 30) &0x3))];
   v[3] = &rdp.vtx[(w0 >> 10) & 0x1F];
   v[4] = &rdp.vtx[(w0 >> 5) & 0x1F];
   v[5] = &rdp.vtx[(w0 >> 0) & 0x1F];
   v[6] = &rdp.vtx[(w1 >> 25) & 0x1F];
   v[7] = &rdp.vtx[(w1 >> 20) & 0x1F];
   v[8] = &rdp.vtx[(w1 >> 15) & 0x1F];
   v[9] = &rdp.vtx[(w1 >> 10) & 0x1F];
   v[10] = &rdp.vtx[(w1 >> 5) & 0x1F];
   v[11] = &rdp.vtx[(w1 >> 0) & 0x1F];

   int updated = 0;

   if (!cull_tri(v))
      updated |= (1 << 0);
   rdp.tri_n ++;

   if (!cull_tri(v+3))
      updated |= (1 << 1);
   rdp.tri_n ++;

   if (!cull_tri(v+6))
      updated |= (1 << 2);
   rdp.tri_n ++;

   if (!cull_tri(v+9))
      updated |= (1 << 3);
   rdp.tri_n ++;

   if (!updated)
      return;

   update();

   if (updated & (1 << 0))
      draw_tri (v, 0);
   if (updated & (1 << 1))
      draw_tri (v+3, 0);
   if (updated & (1 << 2))
      draw_tri (v+6, 0);
   if (updated & (1 << 3))
      draw_tri (v+9, 0);

   //FRDP("uc8:tri4 (#%d - #%d), %d-%d-%d, %d-%d-%d, %d-%d-%d, %d-%d-%d\n", rdp.tri_n, rdp.tri_n+3, ((w0 >> 23) & 0x1F), ((w0 >> 18) & 0x1F), ((((w0 >> 15) & 0x7) << 2) | ((w1 >> 30) &0x3)), ((w0 >> 10) & 0x1F), ((w0 >> 5) & 0x1F), ((w0 >> 0) & 0x1F), ((w1 >> 25) & 0x1F), ((w1 >> 20) & 0x1F), ((w1 >> 15) & 0x1F), ((w1 >> 10) & 0x1F), ((w1 >> 5) & 0x1F), ((w1 >> 0) & 0x1F));
}

static void uc8_setothermode_l(uint32_t w0, uint32_t w1)
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

   //LRDP("uc0:setothermode_l ");
}

static void uc8_setothermode_h(uint32_t w0, uint32_t w1)
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
   //LRDP("uc8:setothermode_h: ");
}
