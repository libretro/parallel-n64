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
   float light_intensity, color[3];

   light_intensity = 0.0f;
   color[0] = rdp.light[rdp.num_lights].col[0];
   color[1] = rdp.light[rdp.num_lights].col[1];
   color[2] = rdp.light[rdp.num_lights].col[2];

   for (l = 0; l < rdp.num_lights; l++)
   {
      light_intensity = 0.0f;

      if (rdp.light[l].nonblack)
      {
         float lvec[3], light_len2, light_len, at;
         lvec[0] = rdp.light[l].x - vpos[0];
         lvec[1] = rdp.light[l].y - vpos[1];
         lvec[2] = rdp.light[l].z - vpos[2];

         light_len2 = lvec[0] * lvec[0] + lvec[1] * lvec[1] + lvec[2] * lvec[2];
         light_len = squareRoot(light_len2);
         //FRDP ("calc_point_light: len: %f, len2: %f\n", light_len, light_len2);
         at = rdp.light[l].ca + light_len/65535.0f*rdp.light[l].la + light_len2/65535.0f*rdp.light[l].qa;

         if (at > 0.0f)
            light_intensity = 1/at;//DotProduct (lvec, nvec) / (light_len * normal_len * at);
      }

      if (light_intensity > 0.0f)
      {
         color[0] += rdp.light[l].col[0] * light_intensity;
         color[1] += rdp.light[l].col[1] * light_intensity;
         color[2] += rdp.light[l].col[2] * light_intensity;
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

static void uc2_vertex(uint32_t w0, uint32_t w1)
{
   uint32_t i, l, addr, geom_mode;
   int v0, n;
   float x, y, z;
   
   if (!(w0 & 0x00FFFFFF))
   {
      uc6_obj_rectangle(w0, w1);
      return;
   }

   pre_update();

   addr = segoffset(w1);

   n = (w0 >> 12) & 0xFF;
   v0 = ((w0 >> 1) & 0x7F) - n;

   FRDP ("uc2:vertex n: %d, v0: %d, from: %08lx\n", n, v0, addr);

   if (v0 < 0)
      return;

   geom_mode = rdp.geom_mode;
   if ((settings.hacks&hack_Fzero) && (rdp.geom_mode & G_TEXTURE_GEN))
   {
      if (((int16_t*)gfx.RDRAM)[(((addr) >> 1) + 4)^1] || ((int16_t*)gfx.RDRAM)[(((addr) >> 1) + 5)^1])
         rdp.geom_mode ^= G_TEXTURE_GEN;
   }

   _gSPVertex(
         addr,          /* v - Current vertex */
         n,             /* n */
         v0             /* v0 */
         );

   rdp.geom_mode = geom_mode;
}

static void uc2_modifyvtx(uint32_t w0, uint32_t w1)
{
   gSPModifyVertex(
         (w0 >> 1) & 0xFFFF,  /* vtx */
         (w0 >> 16) & 0xFF,   /* where */
         w1                   /* val */
         );
   //FRDP ("uc2:modifyvtx: vtx: %d, where: 0x%02lx, val: %08lx - ", vtx, where, w1);
}

static void uc2_culldl(uint32_t w0, uint32_t w1)
{
   uint16_t i, vStart, vEnd, cond;
   VERTEX *v;

   vStart = (uint16_t)(w0 & 0xFFFF) >> 1;
   vEnd = (uint16_t)(w1 & 0xFFFF) >> 1;
   cond = 0;
   FRDP ("uc2:culldl start: %d, end: %d\n", vStart, vEnd);

   if (vEnd < vStart)
      return;

   for (i = vStart; i <= vEnd; i++)
   {
#if 0
      v = (VERTEX*)&rdp.vtx[i];

      /*
       * Check if completely off the screen
       * (quick frustrum clipping for 90 FOV)
       */

      if (v->x >= -v->w)
         cond |= X_CLIP_MAX;
      if (v->x <= v->w)
         cond |= X_CLIP_MIN;
      if (v->y >= -v->w)
         cond |= Y_CLIP_MAX;
      if (v->y <= v->w)
         cond |= Y_CLIP_MIN;
      if (v->w >= 0.1f)
         cond |= Z_CLIP_MAX;

      if (cond == 0x1F)
         return;
#endif

      cond |= (~rdp.vtx[i].scr_off) & 0x1F;
      if (cond == 0x1F)
         return;
   }

   LRDP(" - ");  // specify that the enddl is not a real command
   gSPEndDisplayList();
}

static void uc2_tri1(uint32_t w0, uint32_t w1)
{
   if ((w0 & 0x00FFFFFF) == 0x17)
   {
      uc6_obj_loadtxtr(w0, w1);
      return;
   }

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

   if (rdp.geom_mode & 0x00000001) // Z-Buffer enable
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
   if (rdp.geom_mode & 0x00001000) // Front culling
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
   if (rdp.geom_mode & 0x00002000) // Back culling
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
   if (rdp.geom_mode & 0x00010000)      // Fog enable
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
   DECLAREALIGN16VAR(m[4][4]);
   uint8_t command;

   if (!(w0 & 0x00FFFFFF))
   {
      uc6_obj_rectangle_r(w0, w1);
      return;
   }
   LRDP("uc2:matrix\n");

   load_matrix(m, segoffset(w1));

   command = (uint8_t)((w0 ^ 1) & 0xFF);
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
         gSPInsertMatrix(w0, w1);
         break;

      case G_MW_NUMLIGHT:
         rdp.num_lights = w1 / 24;
         rdp.update |= UPDATE_LIGHTS;
         //FRDP ("numlights: %d\n", rdp.num_lights);
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
			uint32_t a;

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
            rdp.light[n].nonblack = rdram_u8[(addr+0)^3];
            rdp.light[n].nonblack += rdram_u8[(addr+1)^3];
            rdp.light[n].nonblack += rdram_u8[(addr+2)^3];
            gSPLight(gfx.RDRAM, w1, n);

            a = addr >> 1;
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

static void uc2_setothermode_h(uint32_t w0, uint32_t w1)
{
   int32_t len = (w0 & 0xFF) + 1;
   gSPSetOtherMode(
         G_SETOTHERMODE_H,              /* cmd */
         32 - ((w0 >> 8) & 0xFF) - len, /* sft */
         len,                           /* len */
         0                              /* data - stub */
         );
}
