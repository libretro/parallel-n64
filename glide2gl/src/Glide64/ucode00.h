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

//
// uc0:vertex - loads vertices
//
static void uc0_vertex(uint32_t w0, uint32_t w1)
{
   pre_update();
   gSPVertex(
         RSP_SegmentToPhysical(w1),       /* v - Current vertex */
         ((w0 >> 20) & 0xF) + 1,          /* n - Number of vertices to copy */
         (w0 >> 16) & 0xF                 /* v0 */
         );
}

// ** Definitions **

static void modelview_load (float m[4][4])
{
   CopyMatrix(rdp.model, m, 64); // 4*4*4 (float)
   rdp.update |= UPDATE_MULT_MAT | UPDATE_LIGHTS;
}

static void modelview_mul (float m[4][4])
{
   MulMatrices(m, rdp.model, rdp.model);
   rdp.update |= UPDATE_MULT_MAT | UPDATE_LIGHTS;
}

static void modelview_push(void)
{
   if (rdp.model_i == rdp.model_stack_size)
      return;

   CopyMatrix(rdp.model_stack[rdp.model_i++], rdp.model, 64);
}

static void modelview_load_push (float m[4][4])
{
   modelview_push();
   modelview_load(m);
}

static void modelview_mul_push (float m[4][4])
{
   modelview_push();
   modelview_mul (m);
}

static void projection_load (float m[4][4])
{
   CopyMatrix(rdp.proj, m, 64); // 4*4*4 (float)
   rdp.update |= UPDATE_MULT_MAT;
}

static void projection_mul (float m[4][4])
{
   MulMatrices(m, rdp.proj, rdp.proj);
   rdp.update |= UPDATE_MULT_MAT;
}

static void load_matrix (float m[4][4], uint32_t addr)
{
   //FRDP ("matrix - addr: %08lx\n", addr);
   int x,y;  // matrix index
   uint16_t *src;
   addr >>= 1;
   src = (uint16_t*)gfx.RDRAM;

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
   DECLAREALIGN16VAR(m[4][4]);
   // Use segment offset to get the address
   uint32_t addr = RSP_SegmentToPhysical(w1);
   uint8_t command = (uint8_t)((w0 >> 16) & 0xFF);

   load_matrix(m, addr);

   switch (command)
   {
      case G_MTX_NOPUSH: // modelview mul nopush
         //LRDP("modelview mul\n");
         modelview_mul (m);
         break;

      case 1: // projection mul nopush
      case 5: // projection mul push, can't push projection
         //LRDP("projection mul\n");
         projection_mul(m);
         break;

      case G_MTX_LOAD: // modelview load nopush
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
         gSPLight(gfx.RDRAM, w1, i);
         break;


      case G_MV_MATRIX_1:
         gSPForceMatrix(w1);
         rdp.pc[rdp.pc_i] = ((rdp.pc[rdp.pc_i] & BMASK) + 24) & BMASK; //skip next 3 command, b/c they all are part of gSPForceMatrix
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
   gsSP1Triangle(
         ((w1 >> 16) & 0xFF) / 10,     /* v0 */
         ((w1 >> 8) & 0xFF) / 10,      /* v1 */
         (w1 & 0xFF) / 10,             /* v2 */
         0,
         true);
}

static void uc0_tri1_mischief(uint32_t w0, uint32_t w1)
{
   VERTEX *v[3];

   v[0] = &rdp.vtx[((w1 >> 16) & 0xFF) / 10];
   v[1] = &rdp.vtx[((w1 >> 8) & 0xFF) / 10];
   v[2] = &rdp.vtx[(w1 & 0xFF) / 10];

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

   gsSP1Triangle(
         ((w1 >> 16) & 0xFF) / 10,  /* v0 */
         ((w1 >> 8) & 0xFF) / 10,   /* v1 */
         (w1 & 0xFF) / 10,          /* v2 */
         0,
         true
         );
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
   gSPCullDisplayList(
         ((w0 & 0x00FFFFFF) / 40) & 0xF,     /* v0 */
         (w1 / 40) & 0x0F                    /* vn */
         );

   //FRDP("uc0:culldl start: %d, end: %d\n", vStart, vEnd);
}

static void uc0_popmatrix(uint32_t w0, uint32_t w1)
{
   gSPPopMatrix(w1);
}

//
// uc0:moveword - moves a word to someplace, like the segment pointers
//
static void uc0_moveword(uint32_t w0, uint32_t w1)
{
   //LRDP("uc0:moveword ");

   // Find which command this is (lowest byte of w0)
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
         {
            uint32_t where = ((w0 >> 8) & 0xFFFF) % 40;
            if (where == 0)
               uc6_obj_sprite(w0, w1);
            else
               gSPModifyVertex((((w0 >> 8) & 0xFFFF) / 40), where, w1);
         }
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
   gSPTexture(
         (w1 >> 16) & 0xFFFF,          /* sc */
         (w1 & 0xFFFF),                /* tc */
         (w0 >> 11) & 0x07,            /* level */
         (w0 >> 8) & 0x07,             /* tile */
         (w0 & 0xFF)                   /* on */
         );
}

static void uc0_setothermode_h(uint32_t w0, uint32_t w1)
{
   gSPSetOtherMode(
         G_SETOTHERMODE_H, /* cmd */
         (w0 >> 8) & 0xFF, /* sft */
         (w0 & 0xFF),      /* len */
         0                 /* data - stub */
         );
}

static void uc0_setothermode_l(uint32_t w0, uint32_t w1)
{
   gSPSetOtherMode(
         G_SETOTHERMODE_L, /* cmd */
         (w0 >> 8) & 0xFF, /* sft */
         (w0 & 0xFF),      /* len */
         0                 /* data - stub */
         );
}

static void uc0_setgeometrymode(uint32_t w0, uint32_t w1)
{
   gSPSetGeometryMode(w1);
}


static void uc0_cleargeometrymode(uint32_t w0, uint32_t w1)
{
   gSPClearGeometryMode(w1);
}

static void uc0_line3d(uint32_t w0, uint32_t w1)
{
   gSPLineW3D(
         ((w1 >>  8) & 0xff) / 10,     /* v0 */
         ((w1 >> 16) & 0xff) / 10,     /* v1 */
         (w1 & 0xFF) + 3,              /* wd */
         0                             /* flag (stub) */
         );
}

static void uc0_tri4(uint32_t w0, uint32_t w1)
{
   // c0: 0000 0123, c1: 456789ab
   // becomes: 405 617 829 a3b

   gsSP4Triangles(
         (w1 >> 28) & 0xF,    /* v00 */
         (w0 >> 12) & 0xF,    /* v01 */
         (w1 >> 24) & 0xF,    /* v02 */
         0,                   /* flag0 */
         (w1 >> 20) & 0xF,    /* v10 */
         (w0 >> 8)  & 0xF,    /* v11 */
         (w1 >> 16) & 0xF,    /* v12 */
         0,                   /* flag1 */
         (w1 >> 12) & 0xF,    /* v20 */
         (w0 >> 4) & 0xF,     /* v21 */
         (w1 >> 8) & 0xF,     /* v22 */
         0,                   /* flag2 */
         (w1 >> 4) & 0xF,     /* v30 */
         (w0 >> 0) & 0xF,     /* v31 */
         (w1 >> 0) & 0xF,     /* v32 */
         0                    /* flag3 */
         );
}
