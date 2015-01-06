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
// Oct 2002 Created by Gonetz (Gonetz@ngs.ru)
// Info about this ucode is taken from TR64 OGL plugin. Thanks, Icepir8!
// Oct 2003 Modified by Gonetz (Gonetz@ngs.ru)
// Bugs fixed with help from glN64 sources. Thanks, Orkin!
//****************************************************************

uint32_t pd_col_addr = 0;

static void uc7_colorbase(uint32_t w0, uint32_t w1)
{
   LRDP("uc7_colorbase\n");
   pd_col_addr = segoffset(w1);
}

typedef struct 
{
   int16_t y;
   int16_t x;
   uint16_t idx;

   int16_t z;

   int16_t t;
   int16_t s;
} vtx_uc7;

static void uc7_vertex(uint32_t w0, uint32_t w1)
{
   unsigned int i;
   float x, y, z;
#ifdef __ARM_NEON__
   float32x4_t comb0, comb1, comb2, comb3;
   float32x4_t v_xyzw;
#endif
   uint32_t v0 = (w0 & 0x0F0000) >> 16;
   uint32_t n = ((w0 & 0xF00000) >> 20) + 1;
   uint32_t addr = segoffset(w1);
   vtx_uc7 *vertex = (vtx_uc7*)&gfx_info.RDRAM[addr];
   void   *membase_ptr  = (void*)(gfx_info.RDRAM + addr);
   uint32_t iter = 1;

#ifdef __ARM_NEON__
   comb0 = vld1q_f32(rdp.combined[0]);
   comb1 = vld1q_f32(rdp.combined[1]);
   comb2 = vld1q_f32(rdp.combined[2]);
   comb3 = vld1q_f32(rdp.combined[3]);
#endif

   pre_update();


   for (i = 0; i < (n * iter); i += iter)
   {
      VERTEX *vert = (VERTEX*)&rdp.vtx[v0 + (i / iter)];
      int16_t *rdram    = (int16_t*)membase_ptr;
      int8_t  *rdram_s8 = (int8_t* )membase_ptr;
      uint8_t *rdram_u8 = (uint8_t*)membase_ptr;
      uint8_t *color = (uint8_t*)&gfx_info.RDRAM[pd_col_addr + (vertex->idx & 0xff)];

      x   = (float)vertex->x;
      y   = (float)vertex->y;
      z   = (float)vertex->z;
      vert->flags  = 0;
      vert->ou   = (float)vertex->s;
      vert->ov   = (float)vertex->t;
      vert->uv_scaled = 0;
      vert->a = color[0];

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
      CalculateFog (vert);

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
#if 0
      if (vert->z_w > 1.0f)
         vert->scr_off |= 32;
#endif

      if (rdp.geom_mode & G_LIGHTING)
      {
         vert->vec[0] = (int8_t)color[3];
         vert->vec[1] = (int8_t)color[2];
         vert->vec[2] = (int8_t)color[1];

         if (rdp.geom_mode & G_TEXTURE_GEN_LINEAR) 
            calc_linear(vert);
         else if (rdp.geom_mode & G_TEXTURE_GEN) 
            calc_sphere(vert);

         NormalizeVector (vert->vec);

         calc_light (vert);
      }
      else
      {
         vert->r = color[3];
         vert->g = color[2];
         vert->b = color[1];
      }
      vertex++;
      membase_ptr = (char*)membase_ptr + iter;
   }
}
