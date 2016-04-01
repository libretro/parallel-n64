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
#include "../../../Graphics/GBI.h"
#include "../../../Graphics/HLE/Microcode/Fast3D.h"

static void rsp_vertex(int v0, int n)
{
   uint32_t addr = RSP_SegmentToPhysical(__RSP.w1);

   pre_update();
   glide64gSPVertex(addr, n, v0);
}

/* uc0:vertex - loads vertices */
static void uc0_vertex(uint32_t w0, uint32_t w1)
{
   int v0 = _SHIFTR(w0, 16, 4);     /* Current vertex */
   int n  = _SHIFTR(w0, 20, 4) + 1; /* Number of vertices to copy */
   rsp_vertex(v0, n);
}

// ** Definitions **

static void modelview_load (float m[4][4])
{
   CopyMatrix(rdp.model, m);
   g_gdp.flags |= UPDATE_MULT_MAT | UPDATE_LIGHTS;
}

static void modelview_mul (float m[4][4])
{
   MulMatrices(m, rdp.model, rdp.model);
   g_gdp.flags |= UPDATE_MULT_MAT | UPDATE_LIGHTS;
}

static void modelview_push(void)
{
   if (rdp.model_i == rdp.model_stack_size)
      return;

   CopyMatrix(rdp.model_stack[rdp.model_i++], rdp.model);
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
   CopyMatrix(rdp.proj, m);
   g_gdp.flags |= UPDATE_MULT_MAT;
}

static void projection_mul (float m[4][4])
{
   MulMatrices(m, rdp.proj, rdp.proj);
   g_gdp.flags |= UPDATE_MULT_MAT;
}

/* uc0:matrix - performs matrix operations */
static void uc0_matrix(uint32_t w0, uint32_t w1)
{
   DECLAREALIGN16VAR(m[4][4]);
   // Use segment offset to get the address
   uint32_t addr   = RSP_SegmentToPhysical(w1);
   uint8_t command = (uint8_t)((w0 >> 16) & 0xFF);

   load_matrix(m, addr);

   switch (command)
   {
      case G_MTX_NOPUSH: // modelview mul nopush
         modelview_mul (m);
         break;

      case 1: // projection mul nopush
      case 5: // projection mul push, can't push projection
         projection_mul(m);
         break;

      case G_MTX_LOAD: // modelview load nopush
         modelview_load(m);
         break;

      case 3: // projection load nopush
      case 7: // projection load push, can't push projection
         projection_load(m);
         break;

      case 4: // modelview mul push
         modelview_mul_push(m);
         break;

      case 6: // modelview load push
         modelview_load_push(m);
         break;
#if 0
      default:
         FRDP_E ("Unknown matrix command, %02lx", command);
         FRDP ("Unknown matrix command, %02lx", command);
#endif
   }
}

/* uc0:displaylist - makes a call to another section of code */

static void uc0_displaylist(uint32_t w0, uint32_t w1)
{
   uint32_t addr = RSP_SegmentToPhysical(w1);

   /* This fixes partially Gauntlet: Legends */
   if (addr == __RSP.PC[__RSP.PCi] - 8)
      return;

   switch (_SHIFTR(w0, 16, 8))
   {
      case G_DL_PUSH:
         glide64gSPDisplayList(w1);
         break;
      case G_DL_NOPUSH:
         glide64gSPBranchList(w1);
         break;
   }
}

/* tri1 - renders a triangle */
static void uc0_tri1(uint32_t w0, uint32_t w1)
{
	glide64gSP1Triangle( _SHIFTR( w1, 16, 8 ) / 10,
				  _SHIFTR( w1, 8, 8 ) / 10,
				  _SHIFTR( w1, 0, 8 ) / 10, 0);
}

static void uc0_tri1_mischief(uint32_t w0, uint32_t w1)
{
   int i;
   VERTEX *v[3];

   v[0]           = &rdp.vtx[_SHIFTR(w1, 16, 8) / 10];
   v[1]           = &rdp.vtx[_SHIFTR(w1,  8, 8) / 10];
   v[2]           = &rdp.vtx[_SHIFTR(w1,  0, 8) / 10];

   rdp.force_wrap = false;

   for (i = 0; i < 3; i++)
   {
      if (v[i]->ou < 0.0f || v[i]->ov < 0.0f)
      {
         rdp.force_wrap = true;
         break;
      }
   }

   cull_trianglefaces(v, 1, true, true, 0);
}

static void uc0_culldl(uint32_t w0, uint32_t w1)
{
	glide64gSPCullDisplayList( _SHIFTR( w0, 0, 24 ) / 40, (w1 / 40) - 1 );
}

static void uc0_popmatrix(uint32_t w0, uint32_t w1)
{
   glide64gSPPopMatrix( w1 );
}

/* uc0:moveword - moves a word to someplace, like the segment pointers */
static void uc0_moveword(uint32_t w0, uint32_t w1)
{
   // Find which command this is (lowest byte of cmd0)
   switch (_SHIFTR( w0, 0, 8))
   {
      case G_MW_MATRIX:
         break;
      case G_MW_NUMLIGHT:
         glide64gSPNumLights( ((w1 - 0x80000000) >> 5) - 1 );
         break;
      case G_MW_CLIP:
         if (((w0 >> 8)&0xFFFF) == 0x04)
            glide64gSPClipRatio(w1);
         break;

      case G_MW_SEGMENT:
         if ((w1 & BMASK)<BMASK)
            glide64gSPSegment((w0 >> 10) & 0x0F, w1);
         break;
      case G_MW_FOG:
         glide64gSPFogFactor((int16_t)_SHIFTR(w1, 16, 16), (int16_t)_SHIFTR(w1, 0, 16));
         break;
      case G_MW_LIGHTCOL:
         switch (_SHIFTR( w0, 8, 16 ))
         {
            case F3D_MWO_aLIGHT_1:
               gSPLightColor( LIGHT_1, w1 );
               break;
            case F3D_MWO_aLIGHT_2:
               gSPLightColor( LIGHT_2, w1 );
               break;
            case F3D_MWO_aLIGHT_3:
               gSPLightColor( LIGHT_3, w1 );
               break;
            case F3D_MWO_aLIGHT_4:
               gSPLightColor( LIGHT_4, w1 );
               break;
            case F3D_MWO_aLIGHT_5:
               gSPLightColor( LIGHT_5, w1 );
               break;
            case F3D_MWO_aLIGHT_6:
               gSPLightColor( LIGHT_6, w1 );
               break;
            case F3D_MWO_aLIGHT_7:
               gSPLightColor( LIGHT_7, w1 );
               break;
            case F3D_MWO_aLIGHT_8:
               gSPLightColor( LIGHT_8, w1 );
               break;
         }
         break;

      case G_MW_POINTS:
         {
            const uint32_t val = _SHIFTR(w0, 8, 16);
            glide64gSPModifyVertex(val / 40, val % 40, w1);
         }
         break;
      case G_MW_PERSPNORM:
         break;
   }
}

static void uc0_texture(uint32_t w0, uint32_t w1)
{
   glide64gSPTexture(
         _SHIFTR(w1, 16, 16), /* sc */
         _SHIFTR(w1,  0, 16), /* tc */
         _SHIFTR(w0, 11, 3),  /* level */
         _SHIFTR(w0, 8, 3),   /* tile */
         _SHIFTR(w0, 0, 8)    /* on */
         );
}

static void uc0_setothermode_h(uint32_t w0, uint32_t w1)
{
   int i;
   int len       = _SHIFTR(w0, 0, 8);
   int shift     = _SHIFTR(w0, 8, 8);
   uint32_t mask = 0;

   if ((settings.ucode == ucode_F3DEX2) || (settings.ucode == ucode_CBFD))
      shift = 32 - shift - (++len);

   i = len;
   for (; i; i--)
      mask = (mask << 1) | 1;
   mask <<= shift;

   __RSP.w1        &= mask;
   rdp.othermode_h  = (rdp.othermode_h & ~mask) | __RSP.w1;

   if (mask & 0x00003000) // filter mode
   {
      rdp.filter_mode = (int)((rdp.othermode_h & 0x00003000) >> 12);
      g_gdp.flags |= UPDATE_TEXTURE;
      FRDP ("filter mode: %s\n", str_filter[rdp.filter_mode]);
   }

   if (mask & 0x0000C000) // tlut mode
   {
      rdp.tlut_mode = (uint8_t)((rdp.othermode_h & 0x0000C000) >> 14);
      FRDP ("tlut mode: %s\n", str_tlut[rdp.tlut_mode]);
   }

   if (mask & 0x00300000) // cycle type
      g_gdp.flags |= UPDATE_ZBUF_ENABLED;
}

static void uc0_setothermode_l(uint32_t w0, uint32_t w1)
{
   int i;
   int len       = _SHIFTR(w0, 0, 8);
   int shift     = _SHIFTR(w0, 8, 8);
   uint32_t mask = 0;

   if ((settings.ucode == ucode_F3DEX2) || (settings.ucode == ucode_CBFD))
   {
      shift = 32 - shift - (++len);
      if (shift < 0) shift = 0;
   }

   i = len;
   for (; i; i--)
      mask = (mask << 1) | 1;
   mask <<= shift;

   __RSP.w1 &= mask;
   rdp.othermode_l = (rdp.othermode_l & ~mask) | __RSP.w1;

   if (mask & RDP_ALPHA_COMPARE) // alpha compare
      g_gdp.flags |= UPDATE_ALPHA_COMPARE;

   if (mask & 0xFFFFFFF8) // rendermode / blender bits
   {
      g_gdp.flags |= UPDATE_FOG_ENABLED; //if blender has no fog bits, fog must be set off
      rdp.render_mode_changed |= rdp.rm ^ rdp.othermode_l;
      rdp.rm = rdp.othermode_l;
      if (settings.flame_corona && (rdp.rm == 0x00504341)) //hack for flame's corona
         rdp.othermode_l |= 0x00000010;
      FRDP ("rendermode: %08lx\n", rdp.othermode_l); // just output whole othermode_l
   }

   // there is not one setothermode_l that's not handled :)
}

static void uc0_setgeometrymode(uint32_t w0, uint32_t w1)
{
   glide64gSPSetGeometryMode(w1);

   if (w1 & G_ZBUFFER)
   {
      if (!(rdp.flags & ZBUF_ENABLED))
      {
         rdp.flags |= ZBUF_ENABLED;
         g_gdp.flags |= UPDATE_ZBUF_ENABLED;
      }
   }
   if (w1 & G_CULL_FRONT)
   {
      if (!(rdp.flags & CULL_FRONT))
      {
         rdp.flags |= CULL_FRONT;
         g_gdp.flags |= UPDATE_CULL_MODE;
      }
   }
   if (w1 & G_CULL_BACK)
   {
      if (!(rdp.flags & CULL_BACK))
      {
         rdp.flags |= CULL_BACK;
         g_gdp.flags |= UPDATE_CULL_MODE;
      }
   }

   if (w1 & G_FOG)
   {
      if (!(rdp.flags & FOG_ENABLED))
      {
         rdp.flags |= FOG_ENABLED;
         g_gdp.flags |= UPDATE_FOG_ENABLED;
      }
   }
}


static void uc0_cleargeometrymode(uint32_t w0, uint32_t w1)
{
   //FRDP("uc0:cleargeometrymode %08lx\n", w1);

   glide64gSPClearGeometryMode(w1);

   if (w1 & G_ZBUFFER)
   {
      if (rdp.flags & ZBUF_ENABLED)
      {
         rdp.flags ^= ZBUF_ENABLED;
         g_gdp.flags |= UPDATE_ZBUF_ENABLED;
      }
   }
   if (w1 & G_CULL_FRONT)
   {
      if (rdp.flags & CULL_FRONT)
      {
         rdp.flags ^= CULL_FRONT;
         g_gdp.flags |= UPDATE_CULL_MODE;
      }
   }
   if (w1 & G_CULL_BACK)
   {
      if (rdp.flags & CULL_BACK)
      {
         rdp.flags ^= CULL_BACK;
         g_gdp.flags |= UPDATE_CULL_MODE;
      }
   }

   if (w1 & G_FOG)
   {
      if (rdp.flags & FOG_ENABLED)
      {
         rdp.flags ^= FOG_ENABLED;
         g_gdp.flags |= UPDATE_FOG_ENABLED;
      }
   }
}

static void uc0_line3d(uint32_t w0, uint32_t w1)
{
   uint32_t mode      = (rdp.flags & CULLMASK) >> CULLSHIFT;

   rdp.flags         |= CULLMASK;
   g_gdp.flags       |= UPDATE_CULL_MODE;

   glide64gSP1Triangle(
         ((w1 >> 8) & 0xff) / 10,
         ((w1 >> 16) & 0xff) / 10,
         ((w1 >> 16) & 0xff) / 10,
         (uint16_t)(w1 & 0xFF) + 3
         );

   rdp.flags         ^= CULLMASK;
   rdp.flags         |= mode << CULLSHIFT;
   g_gdp.flags       |= UPDATE_CULL_MODE;
}

static void uc0_tri4(uint32_t w0, uint32_t w1)
{
	glide64gSP4Triangles( _SHIFTR( w1, 28, 4 ), _SHIFTR( w0, 12, 4 ), _SHIFTR( w1, 24, 4 ),
				   _SHIFTR( w1, 20, 4 ), _SHIFTR( w0,  8, 4 ), _SHIFTR( w1, 16, 4 ),
				   _SHIFTR( w1, 12, 4 ), _SHIFTR( w0,  4, 4 ), _SHIFTR( w1,  8, 4 ),
				   _SHIFTR( w1,  4, 4 ), _SHIFTR( w0,  0, 4 ), _SHIFTR( w1,  0, 4 ) );
}
