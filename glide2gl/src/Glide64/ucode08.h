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
   uint32_t l, addr;
   int32_t v0, i, n;
   float x, y, z;

   addr = segoffset(w1);

   n = (w0 >> 12) & 0xFF;
   v0 = ((w0 >> 1) & 0x7F) - n;

   FRDP ("uc8:vertex n: %d, v0: %d, from: %08lx\n", n, v0, addr);

   if (v0 < 0)
      return;

   pre_update();

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
         float color[3] = {rdp.light[rdp.num_lights].col[0], rdp.light[rdp.num_lights].col[1], rdp.light[rdp.num_lights].col[2]};
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
               color[0] += rdp.light[l].col[0] * light_intensity;
               color[1] += rdp.light[l].col[1] * light_intensity;
               color[2] += rdp.light[l].col[2] * light_intensity;
               FRDP("light %d r: %f, g: %f, b: %f\n", l, color[0], color[1], color[2]);
            }
            light_intensity = DotProduct (rdp.light_vector[l], v->vec);
            FRDP("light %d, intensity : %f\n", l, light_intensity);
            if (light_intensity > 0.0f)
            {
               color[0] += rdp.light[l].col[0] * light_intensity;
               color[1] += rdp.light[l].col[1] * light_intensity;
               color[2] += rdp.light[l].col[2] * light_intensity;
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
                  color[0] += rdp.light[l].col[0] * light_intensity;
                  color[1] += rdp.light[l].col[1] * light_intensity;
                  color[2] += rdp.light[l].col[2] * light_intensity;
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
            rdp.light[n].nonblack = gfx.RDRAM[(addr+0)^3];
            rdp.light[n].nonblack += gfx.RDRAM[(addr+1)^3];
            rdp.light[n].nonblack += gfx.RDRAM[(addr+2)^3];

            gSPLight(gfx.RDRAM, w1, n);
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
   gsSP4Triangles(
         (w0 >> 23) & 0x1F,     /* v00 */
         (w0 >> 18) & 0x1F,     /* v01 */
         ((((w0 >> 15) & 0x7) << 2) | ((w1 >> 30) &0x3)),   /* v02 */
         0,                     /* flag0 */
         (w0 >> 10) & 0x1F,     /* v10 */
         (w0 >> 5) & 0x1F,      /* v11 */
         (w0 >> 0) & 0x1F,      /* v12 */
         0,                     /* flag1 */
         (w1 >> 25) & 0x1F,     /* v20 */
         (w1 >> 20) & 0x1F,     /* v21 */
         (w1 >> 15) & 0x1F,     /* v22 */
         0,                     /* flag2 */
         (w1 >> 10) & 0x1F,     /* v30 */
         (w1 >> 5) & 0x1F,      /* v31 */
         (w1 >> 0) & 0x1F,      /* v32 */
         0                      /* flag3 */
         );
}

static void uc8_setothermode_l(uint32_t w0, uint32_t w1)
{
   int32_t sft, len;
   len = (w0 & 0xFF) + 1;
   sft = 32 - ((w0 >> 8) & 0xFF) - len;
   if (sft < 0)
      sft = 0;

   gSPSetOtherMode(
         G_SETOTHERMODE_L, /* cmd */
         sft,              /* sft */
         len,              /* len */
         0                 /* data - stub */
         );
}

static void uc8_setothermode_h(uint32_t w0, uint32_t w1)
{
   int32_t len = (w0 & 0xFF) + 1;
   gSPSetOtherMode(
         G_SETOTHERMODE_H,              /* cmd */
         32 - ((w0 >> 8) & 0xFF) - len, /* sft */
         len,                           /* len */
         0                              /* data - stub */
         );
}
