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
#include "GBI.h"

#define ucode_Fast3D 0
#define ucode_F3DEX 1
#define ucode_F3DEX2 2
#define ucode_WaveRace 3
#define ucode_StarWars 4
#define ucode_DiddyKong 5
#define ucode_S2DEX 6
#define ucode_PerfectDark 7
#define ucode_CBFD 8
#define ucode_zSort 9
#define ucode_Turbo3d 21

static void rsp_vertex(int v0, int n)
{
   uint32_t addr = RSP_SegmentToPhysical(rdp.cmd1);
   int i;
   float x, y, z;

   rdp.v0 = v0; // Current vertex
   rdp.vn = n;  // Number to copy

   // This is special, not handled in update(), but here
   // * Matrix Pre-multiplication idea by Gonetz (Gonetz@ngs.ru)
   if (rdp.update & UPDATE_MULT_MAT)
   {
      rdp.update ^= UPDATE_MULT_MAT;
      MulMatrices(rdp.model, rdp.proj, rdp.combined);
   }
   // *

   // This is special, not handled in update()
   if (rdp.update & UPDATE_LIGHTS)
   {
      uint32_t l;
      rdp.update ^= UPDATE_LIGHTS;

      // Calculate light vectors
      for (l = 0; l < rdp.num_lights; l++)
      {
         InverseTransformVector(&rdp.light[l].dir_x, rdp.light_vector[l], rdp.model);
         NormalizeVector (rdp.light_vector[l]);
      }
   }

   //FRDP ("rsp:vertex v0:%d, n:%d, from: %08lx\n", v0, n, addr);

   for (i=0; i < (n<<4); i+=16)
   {
      VERTEX *v = (VERTEX*)&rdp.vtx[v0 + (i>>4)];
      int16_t *rdram = (int16_t*)gfx.RDRAM;
      x   = (float)rdram[(((addr+i) >> 1) + 0)^1];
      y   = (float)rdram[(((addr+i) >> 1) + 1)^1];
      z   = (float)rdram[(((addr+i) >> 1) + 2)^1];
      v->flags  = ((uint16_t*)gfx.RDRAM)[(((addr+i) >> 1) + 3)^1];
      v->ou = (float)rdram[(((addr+i) >> 1) + 4)^1];
      v->ov = (float)rdram[(((addr+i) >> 1) + 5)^1];
      v->uv_scaled = 0;
      v->a    = ((uint8_t*)gfx.RDRAM)[(addr+i + 15)^3];

      v->x = x*rdp.combined[0][0] + y*rdp.combined[1][0] + z*rdp.combined[2][0] + rdp.combined[3][0];
      v->y = x*rdp.combined[0][1] + y*rdp.combined[1][1] + z*rdp.combined[2][1] + rdp.combined[3][1];
      v->z = x*rdp.combined[0][2] + y*rdp.combined[1][2] + z*rdp.combined[2][2] + rdp.combined[3][2];
      v->w = x*rdp.combined[0][3] + y*rdp.combined[1][3] + z*rdp.combined[2][3] + rdp.combined[3][3];


      if (fabs(v->w) < 0.001) v->w = 0.001f;
      v->oow = 1.0f / v->w;
      v->x_w = v->x * v->oow;
      v->y_w = v->y * v->oow;
      v->z_w = v->z * v->oow;
      CalculateFog (v);

      v->uv_calculated = 0xFFFFFFFF;
      v->screen_translated = 0;
      v->shade_mod = 0;

      v->scr_off = 0;
      if (v->x < -v->w) v->scr_off |= 1;
      if (v->x > v->w) v->scr_off |= 2;
      if (v->y < -v->w) v->scr_off |= 4;
      if (v->y > v->w) v->scr_off |= 8;
      if (v->w < 0.1f) v->scr_off |= 16;
      //    if (v->z_w > 1.0f) v->scr_off |= 32;

      if (rdp.geom_mode & G_LIGHTING)
      {
         int8_t *rdram = (int8_t*)gfx.RDRAM;
         v->vec[0] = rdram[(addr+i + 12)^3];
         v->vec[1] = rdram[(addr+i + 13)^3];
         v->vec[2] = rdram[(addr+i + 14)^3];
         if (rdp.geom_mode & G_TEXTURE_GEN)
         {
            if (rdp.geom_mode & G_TEXTURE_GEN_LINEAR)
               calc_linear (v);
            else
               calc_sphere (v);
         }
         NormalizeVector (v->vec);

         calc_light (v);
      }
      else
      {
         uint8_t *rdram = (uint8_t*)gfx.RDRAM;
         v->r = rdram[(addr+i + 12)^3];
         v->g = rdram[(addr+i + 13)^3];
         v->b = rdram[(addr+i + 14)^3];
      }
      //FRDP ("v%d - x: %f, y: %f, z: %f, w: %f, u: %f, v: %f, f: %f, z_w: %f, r=%d, g=%d, b=%d, a=%d\n", i>>4, v->x, v->y, v->z, v->w, v->ou*rdp.tiles[rdp.cur_tile].s_scale, v->ov*rdp.tiles[rdp.cur_tile].t_scale, v->f, v->z_w, v->r, v->g, v->b, v->a);
   }
}

static void rsp_tri1(VERTEX **v, uint16_t linew)
{
  if (cull_tri(v))
    rdp.tri_n ++;
  else
  {
    update();
    draw_tri (v, linew);
    rdp.tri_n ++;
  }
}

static void rsp_tri2 (VERTEX **v)
{
  int updated = 0;

  if (cull_tri(v))
    rdp.tri_n ++;
  else
  {
    updated = 1;
    update();

    draw_tri (v, 0);
    rdp.tri_n ++;
  }

  if (cull_tri(v+3))
    rdp.tri_n ++;
  else
  {
    if (!updated)
      update();

    draw_tri (v+3, 0);
    rdp.tri_n ++;
  }
}

//
// uc0:vertex - loads vertices
//
static void uc0_vertex(uint32_t w0, uint32_t w1)
{
  int v0 = (w0 >> 16) & 0xF;      // Current vertex
  int n = ((w0 >> 20) & 0xF) + 1; // Number of vertices to copy
  rsp_vertex(v0, n);
}

// ** Definitions **

void modelview_load (float m[4][4])
{
   CopyMatrix(rdp.model, m, 64); // 4*4*4 (float)
   rdp.update |= UPDATE_MULT_MAT | UPDATE_LIGHTS;
}

void modelview_mul (float m[4][4])
{
   MulMatrices(m, rdp.model, rdp.model);
   rdp.update |= UPDATE_MULT_MAT | UPDATE_LIGHTS;
}

void modelview_push(void)
{
   if (rdp.model_i == rdp.model_stack_size)
      return;

   CopyMatrix(rdp.model_stack[rdp.model_i++], rdp.model, 64);
}

void modelview_load_push (float m[4][4])
{
   modelview_push();
   modelview_load(m);
}

void modelview_mul_push (float m[4][4])
{
   modelview_push();
   modelview_mul (m);
}

void projection_load (float m[4][4])
{
   CopyMatrix(rdp.proj, m, 64); // 4*4*4 (float)
   rdp.update |= UPDATE_MULT_MAT;
}

void projection_mul (float m[4][4])
{
   MulMatrices(m, rdp.proj, rdp.proj);
   rdp.update |= UPDATE_MULT_MAT;
}

void load_matrix (float m[4][4], uint32_t addr)
{
   //FRDP ("matrix - addr: %08lx\n", addr);
   int x,y;  // matrix index
   addr >>= 1;
   uint16_t * src = (uint16_t*)gfx.RDRAM;

   // Adding 4 instead of one, just to remove mult. later
   for (x = 0; x < 16; x += 4)
   {
      for (y=0; y<4; y++)
         m[x>>2][y] = (float)((((int32_t)src[(addr+x+y)^1]) << 16) | src[(addr+x+y+16)^1]) / 65536.0f;
   }
}

//
// uc0:matrix - performs matrix operations
//
static void uc0_matrix(uint32_t w0, uint32_t w1)
{
   //LRDP("uc0:matrix ");

   // Use segment offset to get the address
   uint32_t addr = RSP_SegmentToPhysical(w1);
   uint8_t command = (uint8_t)((w0 >> 16) & 0xFF);

   DECLAREALIGN16VAR(m[4][4]);
   load_matrix(m, addr);

   switch (command)
   {
      case 0: // modelview mul nopush
         //LRDP("modelview mul\n");
         modelview_mul (m);
         break;

      case 1: // projection mul nopush
      case 5: // projection mul push, can't push projection
         //LRDP("projection mul\n");
         projection_mul(m);
         break;

      case 2: // modelview load nopush
         //LRDP("modelview load\n");
         modelview_load(m);
         break;

      case 3: // projection load nopush
      case 7: // projection load push, can't push projection
         //LRDP("projection load\n");
         projection_load(m);
         break;

      case 4: // modelview mul push
         //LRDP("modelview mul push\n");
         modelview_mul_push(m);
         break;

      case 6: // modelview load push
         //LRDP("modelview load push\n");
         modelview_load_push(m);
         break;
#if 0
      default:
         FRDP_E ("Unknown matrix command, %02lx", command);
         FRDP ("Unknown matrix command, %02lx", command);
#endif
   }

   //FRDP ("{%f,%f,%f,%f}\n", m[0][0], m[0][1], m[0][2], m[0][3]);
   //FRDP ("{%f,%f,%f,%f}\n", m[1][0], m[1][1], m[1][2], m[1][3]);
   //FRDP ("{%f,%f,%f,%f}\n", m[2][0], m[2][1], m[2][2], m[2][3]);
   //FRDP ("{%f,%f,%f,%f}\n", m[3][0], m[3][1], m[3][2], m[3][3]);
   //FRDP ("\nmodel\n{%f,%f,%f,%f}\n", rdp.model[0][0], rdp.model[0][1], rdp.model[0][2], rdp.model[0][3]);
   //FRDP ("{%f,%f,%f,%f}\n", rdp.model[1][0], rdp.model[1][1], rdp.model[1][2], rdp.model[1][3]);
   //FRDP ("{%f,%f,%f,%f}\n", rdp.model[2][0], rdp.model[2][1], rdp.model[2][2], rdp.model[2][3]);
   //FRDP ("{%f,%f,%f,%f}\n", rdp.model[3][0], rdp.model[3][1], rdp.model[3][2], rdp.model[3][3]);
   //FRDP ("\nproj\n{%f,%f,%f,%f}\n", rdp.proj[0][0], rdp.proj[0][1], rdp.proj[0][2], rdp.proj[0][3]);
   //FRDP ("{%f,%f,%f,%f}\n", rdp.proj[1][0], rdp.proj[1][1], rdp.proj[1][2], rdp.proj[1][3]);
   //FRDP ("{%f,%f,%f,%f}\n", rdp.proj[2][0], rdp.proj[2][1], rdp.proj[2][2], rdp.proj[2][3]);
   //FRDP ("{%f,%f,%f,%f}\n", rdp.proj[3][0], rdp.proj[3][1], rdp.proj[3][2], rdp.proj[3][3]);
}


//
// uc0:movemem - loads a structure with data
//
static void uc0_movemem(uint32_t w0, uint32_t w1)
{
   //LRDP("uc0:movemem ");

   uint32_t i,a;

   // Check the command
   switch ((w0 >> 16) & 0xFF)
   {
      case F3D_MV_VIEWPORT:
         gSPViewport(w1, settings.correct_viewport);
         break;
      case G_MV_LOOKATY:
         {
            int8_t dir_x, dir_y, dir_z, *rdram;
            a = RSP_SegmentToPhysical(w1);
            rdram = (int8_t*)gfx.RDRAM;
            dir_x = rdram[(a+8)^3];
            dir_y = rdram[(a+9)^3];
            dir_z = rdram[(a+10)^3];
            rdp.lookat[1][0] = (float)(dir_x) / 127.0f;
            rdp.lookat[1][1] = (float)(dir_y) / 127.0f;
            rdp.lookat[1][2] = (float)(dir_z) / 127.0f;
            if (!dir_x && !dir_y)
               rdp.use_lookat = false;
            else
               rdp.use_lookat = true;
            //FRDP("lookat_y (%f, %f, %f)\n", rdp.lookat[1][0], rdp.lookat[1][1], rdp.lookat[1][2]);
         }
         break;

      case G_MV_LOOKATX:
         {
            int8_t *rdram = (int8_t*)gfx.RDRAM;
            a = RSP_SegmentToPhysical(w1);
            rdp.lookat[0][0] = (float)(rdram[(a+8)^3]) / 127.0f;
            rdp.lookat[0][1] = (float)(rdram[(a+9)^3]) / 127.0f;
            rdp.lookat[0][2] = (float)(rdram[(a+10)^3]) / 127.0f;
            rdp.use_lookat = true;
            //FRDP("lookat_x (%f, %f, %f)\n", rdp.lookat[1][0], rdp.lookat[1][1], rdp.lookat[1][2]);
         }
         break;

      case G_MV_L0:
      case G_MV_L1:
      case G_MV_L2:
      case G_MV_L3:
      case G_MV_L4:
      case G_MV_L5:
      case G_MV_L6:
      case G_MV_L7:
         // Get the light #
         i = (((w0 >> 16) & 0xff) - G_MV_L0) >> 1;
         gSPLight(w1, i);
         break;


      case G_MV_MATRIX_1:
         gSPForceMatrix(w1);
         break;
         //next 3 command should never appear since they will be skipped in previous command
      case G_MV_MATRIX_2:
         //RDP_E ("uc0:movemem matrix 0 - ERROR!\n");
         //LRDP("matrix 0 - IGNORED\n");
         break;

      case G_MV_MATRIX_3:
         //RDP_E ("uc0:movemem matrix 1 - ERROR!\n");
         //LRDP("matrix 1 - IGNORED\n");
         break;

      case G_MV_MATRIX_4:
         //RDP_E ("uc0:movemem matrix 2 - ERROR!\n");
         //LRDP("matrix 2 - IGNORED\n");
         break;
#if 0
      default:
         FRDP_E ("uc0:movemem unknown (index: 0x%08lx)\n", (w0 >> 16) & 0xFF);
         FRDP ("unknown (index: 0x%08lx)\n", (w0 >> 16) & 0xFF);
#endif
   }
}

//
// uc0:displaylist - makes a call to another section of code
//
static void uc0_displaylist(uint32_t w0, uint32_t w1)
{
   switch (_SHIFTR( w0, 16, 8 ))
   {
      case G_DL_PUSH: // push
         gSPDisplayList(w1);
         break;
      case G_DL_NOPUSH: // no push
         gSPBranchList(w1);
         break;
   }
}

//
// tri1 - renders a triangle
//
static void uc0_tri1(uint32_t w0, uint32_t w1)
{
   VERTEX *v[3];
   //FRDP("uc0:tri1 #%d - %d, %d, %d\n", rdp.tri_n, ((w1 >> 16) & 0xFF) / 10, ((w1 >> 8) & 0xFF) / 10, (w1 & 0xFF) / 10);

   v[0] = &rdp.vtx[((w1 >> 16) & 0xFF) / 10];
   v[1] = &rdp.vtx[((w1 >> 8) & 0xFF) / 10];
   v[2] = &rdp.vtx[(w1 & 0xFF) / 10];

   if (settings.hacks & hack_Makers)
   {
      int i;
      rdp.force_wrap = false;
      for (i = 0; i < 3; i++)
      {
         if (v[i]->ou < 0.0f || v[i]->ov < 0.0f)
         {
            rdp.force_wrap = true;
            break;
         }
      }
   }
   rsp_tri1(v, 0);
}

//
// uc0:enddl - ends a call made by uc0:displaylist
//
static void uc0_enddl(uint32_t w0, uint32_t w1)
{
   gSPEndDisplayList();
}

static void uc0_culldl(uint32_t w0, uint32_t w1)
{
   uint16_t i;
   uint8_t vStart = (uint8_t)((w0 & 0x00FFFFFF) / 40) & 0xF;
   uint8_t vEnd = (uint8_t)(w1 / 40) & 0x0F;
   uint32_t cond = 0;
   VERTEX *v;

   //FRDP("uc0:culldl start: %d, end: %d\n", vStart, vEnd);

   if (vEnd < vStart)
      return;

   for (i = vStart; i <= vEnd; i++)
   {
      v = &rdp.vtx[i];
      // Check if completely off the screen (quick frustrum clipping for 90 FOV)
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
   }

   LRDP(" - ");  // specify that the enddl is not a real command
   uc0_enddl(w0, w1);
}

static void uc0_popmatrix(uint32_t w0, uint32_t w1)
{
   gSPPopMatrix(w1);
}

static void uc6_obj_sprite(uint32_t w0, uint32_t w1);

// FIXME - call not consistent with glN64
static void gSPModifyVertex(uint8_t where, uint16_t vtx, uint32_t val)
{
   VERTEX *v = &rdp.vtx[vtx];
   uint32_t w0, w1;
   w0 = rdp.cmd0;
   w1 = rdp.cmd1;

   switch (where)
   {
      case 0:
         uc6_obj_sprite(w0, w1);
         break;

      case G_MWO_POINT_RGBA:    // RGBA
         v->r = (uint8_t)(val >> 24);
         v->g = (uint8_t)((val >> 16) & 0xFF);
         v->b = (uint8_t)((val >> 8) & 0xFF);
         v->a = (uint8_t)(val & 0xFF);
         v->shade_mod = 0;

         //FRDP ("RGBA: %d, %d, %d, %d\n", v->r, v->g, v->b, v->a);
         break;

      case G_MWO_POINT_ST:    // ST
         {
            float scale = rdp.Persp_en ? 0.03125f : 0.015625f;
            v->ou = (float)((int16_t)(val>>16)) * scale;
            v->ov = (float)((int16_t)(val&0xFFFF)) * scale;
            v->uv_calculated = 0xFFFFFFFF;
            v->uv_scaled = 1;
         }
         //FRDP ("u/v: (%04lx, %04lx), (%f, %f)\n", (int16_t)(val>>16), (int16_t)(val&0xFFFF), v->ou, v->ov);
         break;

      case G_MWO_POINT_XYSCREEN:    // XY screen
         {
            float scr_x = (float)((int16_t)(val>>16)) / 4.0f;
            float scr_y = (float)((int16_t)(val&0xFFFF)) / 4.0f;
            v->screen_translated = 2;
            v->sx = scr_x * rdp.scale_x + rdp.offset_x;
            v->sy = scr_y * rdp.scale_y + rdp.offset_y;
            if (v->w < 0.01f)
            {
               v->w = 1.0f;
               v->oow = 1.0f;
               v->z_w = 1.0f;
            }
            v->sz = rdp.view_trans[2] + v->z_w * rdp.view_scale[2];

            v->scr_off = 0;
            if (scr_x < 0) v->scr_off |= 1;
            if (scr_x > rdp.vi_width) v->scr_off |= 2;
            if (scr_y < 0) v->scr_off |= 4;
            if (scr_y > rdp.vi_height) v->scr_off |= 8;
            if (v->w < 0.1f) v->scr_off |= 16;

            //FRDP ("x/y: (%f, %f)\n", scr_x, scr_y);
         }
         break;

      case G_MWO_POINT_ZSCREEN:    // Z screen
         {
            float scr_z = (float)((int16_t)(val>>16));
            v->z_w = (scr_z - rdp.view_trans[2]) / rdp.view_scale[2];
            v->z = v->z_w * v->w;
            //FRDP ("z: %f\n", scr_z);
         }
         break;

      default:
         //LRDP("UNKNOWN\n");
         break;
   }
   //FRDP ("uc0:modifyvtx: vtx: %d, where: 0x%02lx, val: %08lx - ", vtx, where, w1);
}

//
// uc0:moveword - moves a word to someplace, like the segment pointers
//
static void uc0_moveword(uint32_t w0, uint32_t w1)
{
   //LRDP("uc0:moveword ");

   // Find which command this is (lowest byte of cmd0)
   switch (w0 & 0xFF)
   {
      case G_MW_MATRIX:
         //RDP_E ("uc0:moveword matrix - IGNORED\n");
         //LRDP("matrix - IGNORED\n");
         break;

      case G_MW_NUMLIGHT:
         gSPNumLights( ((w1 - 0x80000000) >> 5) - 1 );
         break;
      case G_MW_CLIP:
         gSPClipRatio(w0, w1);
         break;

      case G_MW_SEGMENT:
         if ((w1 & BMASK) < BMASK)
            gSPSegment((w0 >> 10) & 0x0F, w1);
         break;

      case G_MW_FOG:
         gSPFogFactor((int16_t)_SHIFTR( w1, 16, 16 ), (int16_t)_SHIFTR( w1, 0, 16 ));
         break;

      case G_MW_LIGHTCOL:  // moveword LIGHTCOL
         gSPLightColor((w0 & 0xE000) >> 13, w1);
         break;

      case G_MW_POINTS:
         // FIXME - call not consistent with glN64
         gSPModifyVertex((((uint16_t)((w0 >> 8) & 0xFFFF)) % 40), (((uint16_t)((w0 >> 8) & 0xFFFF)) / 40), w1);
         break;

      case G_MW_PERSPNORM:
         //LRDP("perspnorm - IGNORED\n");
         break;
#if 0
      default:
         FRDP_E ("uc0:moveword unknown (index: 0x%08lx)\n", w0 & 0xFF);
         FRDP ("unknown (index: 0x%08lx)\n", w0 & 0xFF);
#endif
   }
}


static void uc0_texture(uint32_t w0, uint32_t w1)
{
   gSPTexture();
}

static void uc0_setothermode_h(uint32_t w0, uint32_t w1)
{
   //LRDP("uc0:setothermode_h: ");

   int shift, len;
   shift = (w0 >> 8) & 0xFF;
   len = w0 & 0xFF;

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
}

static void uc0_setothermode_l(uint32_t w0, uint32_t w1)
{
   //LRDP("uc0:setothermode_l ");

   int shift, len;
   len = w0 & 0xFF;
   shift = (w0 >> 8) & 0xFF;

   uint32_t mask = 0;
   int i = len;
   for (; i; i--)
      mask = (mask << 1) | 1;
   mask <<= shift;

   rdp.cmd1 &= mask;
   rdp.othermode_l &= ~mask;
   rdp.othermode_l |= rdp.cmd1;

   if (mask & 0x00000003)  // alpha compare
   {
      rdp.acmp = rdp.othermode_l & 0x00000003;
      rdp.update |= UPDATE_ALPHA_COMPARE;
      //FRDP ("alpha compare %s\n", ACmp[rdp.acmp]);
   }

   if (mask & ZBUF_COMPARE)  // z-src selection
   {
      rdp.zsrc = (rdp.othermode_l & G_SHADE) >> 2;
      rdp.update |= UPDATE_ZBUF_ENABLED;
      //FRDP ("z-src sel: %s\n", str_zs[rdp.zsrc]);
      //FRDP ("z-src sel: %08lx\n", rdp.zsrc);
   }

   if (mask & 0xFFFFFFF8)  // rendermode / blender bits
   {
      rdp.update |= UPDATE_FOG_ENABLED; //if blender has no fog bits, fog must be set off
      rdp.render_mode_changed |= rdp.rm ^ rdp.othermode_l;
      rdp.rm = rdp.othermode_l;
      if (settings.flame_corona && (rdp.rm == 0x00504341)) //hack for flame's corona
         rdp.othermode_l |= UPDATE_BIASLEVEL | UPDATE_LIGHTS;
      //FRDP ("rendermode: %08lx\n", rdp.othermode_l);  // just output whole othermode_l
   }

   // there is not one setothermode_l that's not handled :)
}

static void uc0_setgeometrymode(uint32_t w0, uint32_t w1)
{
   gSPSetGeometryMode(w1);
}


static void uc0_cleargeometrymode(uint32_t w0, uint32_t w1)
{
   gSPClearGeometryMode(w1);
}

//gSPLine3D
static void uc0_line3d(uint32_t w0, uint32_t w1)
{
   uint32_t v0 = ((w1 >> 16) & 0xff) / 10;
   uint32_t v1 = ((w1 >>  8) & 0xff) / 10;
   uint16_t width = (uint16_t)(w1 & 0xFF) + 3;

   VERTEX *v[3];
   v[0] = &rdp.vtx[v1];
   v[1] = &rdp.vtx[v0];
   v[2] = &rdp.vtx[v0];

   uint32_t cull_mode = (rdp.flags & CULLMASK) >> CULLSHIFT;
   rdp.flags |= CULLMASK;
   rdp.update |= UPDATE_CULL_MODE;
   rsp_tri1(v, width);
   rdp.flags ^= CULLMASK;
   rdp.flags |= cull_mode << CULLSHIFT;
   rdp.update |= UPDATE_CULL_MODE;

   //FRDP("uc0:line3d v0:%d, v1:%d, width:%d\n", v0, v1, width);
}

static void uc0_tri4(uint32_t w0, uint32_t w1)
{
   // c0: 0000 0123, c1: 456789ab
   // becomes: 405 617 829 a3b
   VERTEX *v[12];

   //LRDP("uc0:tri4");

   v[0 ]  = &rdp.vtx[(w1 >> 28) & 0xF];
   v[1 ]  = &rdp.vtx[(w0 >> 12) & 0xF];
   v[2 ]  = &rdp.vtx[(w1 >> 24) & 0xF];
   v[3 ]  = &rdp.vtx[(w1 >> 20) & 0xF];
   v[4 ]  = &rdp.vtx[(w0 >> 8)  & 0xF];
   v[5 ]  = &rdp.vtx[(w1 >> 16) & 0xF];
   v[6 ]  = &rdp.vtx[(w1 >> 12) & 0xF],
   v[7 ]  = &rdp.vtx[(w0 >> 4) & 0xF];
   v[8 ]  = &rdp.vtx[(w1 >> 8) & 0xF];
   v[9 ]  = &rdp.vtx[(w1 >> 4) & 0xF];
   v[10]  = &rdp.vtx[(w0 >> 0) & 0xF];
   v[11]  = &rdp.vtx[(w1 >> 0) & 0xF];

   int updated = 0;

   if (cull_tri(v))
      rdp.tri_n ++;
   else
   {
      updated = 1;
      update();

      draw_tri (v, 0);
      rdp.tri_n ++;
   }

   if (cull_tri(v+3))
      rdp.tri_n ++;
   else
   {
      if (!updated)
      {
         updated = 1;
         update();
      }

      draw_tri (v+3, 0);
      rdp.tri_n ++;
   }

   if (cull_tri(v+6))
      rdp.tri_n ++;
   else
   {
      if (!updated)
      {
         updated = 1;
         update();
      }

      draw_tri (v+6, 0);
      rdp.tri_n ++;
   }

   if (cull_tri(v+9))
      rdp.tri_n ++;
   else
   {
      if (!updated)
      {
         updated = 1;
         update();
      }

      draw_tri (v+9, 0);
      rdp.tri_n ++;
   }
}
