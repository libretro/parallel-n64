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

/* Glide64's combiner: the N64's own, built into the fragment shader
 * (glitch64_n64_combiner.h), instead of a mapping of each mode onto a
 * Voodoo's. */

#include "Gfx_1.3.h"
#include "Util.h"
#include "Combine.h"

#include "../../../Graphics/GBI.h"
#include "../../../Graphics/RDP/gDP_state.h"

COMBINE cmb;

#define A_BLEND(f1,f2) cmb.abf1=f1,cmb.abf2=f2

/* GLITCH64_N64_FOG_*: the blender modes that read the fog colour */
static unsigned n64_blender_flags;

/* the tiles a cycle's inputs read, bit 0 tile 0, bit 1 tile 1: the
 * second cycle of two swaps the texels */
static unsigned n64_tiles(const glitch64_n64_combiner *c, unsigned i, int second)
{
   unsigned t = 0, k;
   for (k = 0; k < 4; k++)
   {
      unsigned v = c->rgb[i][k];
      if (k == 2 && (v == 8 || v == 9))
         v -= 7;
      if (v == 1 || v == 2)
         t |= 1u << ((v - 1) ^ (unsigned)second);
      v = c->alpha[i][k];
      if (v == 1 || v == 2)
         t |= 1u << ((v - 1) ^ (unsigned)second);
   }
   /* LOD_FRACTION steps tile 0's coordinates */
   if (c->rgb[i][2] == 13 || c->alpha[i][2] == 0)
      t |= 1;
   return t;
}

void Combine(void)
{
   const gdp_combine_modes *m = &g_gdp.combine;
   glitch64_n64_combiner c;
   float prim[4], env[4], consts[4], level[4], center[3], scale[3];
   const int two = gDP.otherMode.cycleType == G_CYC_2CYCLE;
   unsigned i;

   c.rgb[0][0] = (uint8_t)m->sub_a_rgb0; c.rgb[0][1] = (uint8_t)m->sub_b_rgb0;
   c.rgb[0][2] = (uint8_t)m->mul_rgb0;   c.rgb[0][3] = (uint8_t)m->add_rgb0;
   c.rgb[1][0] = (uint8_t)m->sub_a_rgb1; c.rgb[1][1] = (uint8_t)m->sub_b_rgb1;
   c.rgb[1][2] = (uint8_t)m->mul_rgb1;   c.rgb[1][3] = (uint8_t)m->add_rgb1;
   c.alpha[0][0] = (uint8_t)m->sub_a_a0; c.alpha[0][1] = (uint8_t)m->sub_b_a0;
   c.alpha[0][2] = (uint8_t)m->mul_a0;   c.alpha[0][3] = (uint8_t)m->add_a0;
   c.alpha[1][0] = (uint8_t)m->sub_a_a1; c.alpha[1][1] = (uint8_t)m->sub_b_a1;
   c.alpha[1][2] = (uint8_t)m->mul_a1;   c.alpha[1][3] = (uint8_t)m->add_a1;
   c.two_cycle = (uint8_t)two;
   if (gDP.otherMode.cycleType >= G_CYC_COPY)
   {
      /* copy (and fill) mode passes the texel through: the combiner is
       * not run. As the second cycle's (0 - 0) * 0 + TEXEL0 */
      c.rgb[1][0] = 15; c.rgb[1][1] = 15; c.rgb[1][2] = 31; c.rgb[1][3] = 1;
      c.alpha[1][0] = 7; c.alpha[1][1] = 7; c.alpha[1][2] = 7; c.alpha[1][3] = 1;
   }
   c.flags = gDP.otherMode.textureLOD ? GLITCH64_N64_LOD : 0;

   cmb.tex = 0;
   for (i = two ? 0 : 1; i < 2; i++)
      cmb.tex |= n64_tiles(&c, i, two && i == 1);


   cmb.abf1 = GR_BLEND_SRC_ALPHA;
   cmb.abf2 = GR_BLEND_ONE_MINUS_SRC_ALPHA;
   n64_blender_flags = 0;
   CombineBlender();
   c.flags |= (uint8_t)n64_blender_flags;

   rdp.tex = cmb.tex;
   grN64Combine(&c);

   prim[0] = g_gdp.prim_color.r / 255.0f; prim[1] = g_gdp.prim_color.g / 255.0f;
   prim[2] = g_gdp.prim_color.b / 255.0f; prim[3] = g_gdp.prim_color.a / 255.0f;
   env[0] = g_gdp.env_color.r / 255.0f;   env[1] = g_gdp.env_color.g / 255.0f;
   env[2] = g_gdp.env_color.b / 255.0f;   env[3] = g_gdp.env_color.a / 255.0f;
   consts[0] = g_gdp.k4 / 255.0f;
   consts[1] = g_gdp.k5 / 255.0f;
   consts[2] = g_gdp.primitive_lod_frac / 255.0f;
   /* LOD_FRACTION where the GPU cannot step it per pixel: the primitive's */
   consts[3] = consts[2];
   level[0] = g_gdp.primitive_lod_min / 32.0f;
   /* Glide64 keeps the primitive's level count in textureDetail */
   level[1] = (float)gDP.otherMode.textureDetail;
   level[2] = (g_gdp.other_modes.detail_tex_en || g_gdp.other_modes.sharpen_tex_en) ? 1.0f : 0.0f;
   level[3] = (float)(frame_count & 1023);
   center[0] = g_gdp.key_center.r / 255.0f; center[1] = g_gdp.key_center.g / 255.0f;
   center[2] = g_gdp.key_center.b / 255.0f;
   scale[0] = g_gdp.key_scale.r / 255.0f;   scale[1] = g_gdp.key_scale.g / 255.0f;
   scale[2] = g_gdp.key_scale.b / 255.0f;
   grN64Constants(prim, env, consts, level, center, scale);

}

void CombineBlender(void)
{
   uint32_t blendmode = gDP.otherMode.l >> 16;
   // Check force-blending
   if (gDP.otherMode.forceBlender && (gDP.otherMode.cycleType < G_CYC_COPY))
   {
      switch (blendmode)
      {
         case 0x0382:            /* Mace objects */
         case 0x0091:            /* Mace special blend mode */
         case 0x0c08:            /* 1080 Snowboarding - sky */
         case 0x0f0a:            /* Mario Kart 64 - player select
                                  * In * 0 + In * 1
                                  * - or just clr_in, no matter what alpha
                                  */
         case 0x0302:            /* Donkey Kong 64 blue prints */
         case 0xA500:            /* Bomberman 2 special blend mode */
         case 0xcb02:            /* Sin and Punishment */
         case BLEND_FOG_ASHADE:  /* Battlezone
                                  * In * a + In * (1-A) */
         case 0x07C2:            /* Conker BFD */
         case 0x00c0:            /* Conker BFD */
         case 0xc302:            /* ISS64 */
         case 0xC702:            /* Donald Duck */
            A_BLEND (GR_BLEND_ONE, GR_BLEND_ZERO);
            break;

            //Space Invaders
         case 0x0448:
         case 0x055a:
            A_BLEND (GR_BLEND_ONE, GR_BLEND_ONE);
            break;


         case 0xc712:   /* Pokemon Stadium? */
         case 0xaf50:   /* LOT in Zelda: Majora's Mask */
         case 0x0FA5:   /* LOT in Zelda: Majora's Mask */
         case 0x0f5a:   /* Seems to be doing just blend color - maybe combiner can be used for this? */
         case 0x5055:   /* Used in Paper Mario intro, I'm not sure if this is right... */
            /* In * 0 + Mem * 1 */
            A_BLEND (GR_BLEND_ZERO, GR_BLEND_ONE);
            break;

         case 0x5f50: /* Mem * 0 + Mem * (1-A) */
            A_BLEND (GR_BLEND_ZERO, GR_BLEND_ONE_MINUS_SRC_ALPHA);
            break;
         case 0xf550: /* Fog * AFog + Mem * (1-A): the fog colour and alpha out of the combiner */
			case 0x0D18: /* In  * AFog + Mem * (1-A) */
            A_BLEND (GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA);
            n64_blender_flags |= GLITCH64_N64_FOG_RGB | GLITCH64_N64_FOG_ALPHA;
            break;

         case 0x0150: //spiderman
            A_BLEND (GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA);
            if (gDP.otherMode.cycleType == G_CYC_2CYCLE && rdp.cycle2 != 0x01ff1fff)
               n64_blender_flags |= GLITCH64_N64_FOG_ALPHA;
            break;

         case 0xc912: //40 winks, clr_in * a_fog + clr_mem * 1
            n64_blender_flags |= GLITCH64_N64_FOG_ALPHA;
            A_BLEND (GR_BLEND_SRC_ALPHA, GR_BLEND_ONE);
            break;

			case 0x0040: // Fzero
			case 0xC810: // Blends fog
			case 0xC811: // Blends fog
			case 0x0C18: // Standard interpolated blend
			case 0x0C19: // Used for antialiasing
			case 0x0050: // Standard interpolated blend
			case 0x0051: // Standard interpolated blend
			case 0x0055: // Used for antialiasing
            A_BLEND (GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA);
            break;

         case 0x0550: /* Bomberman 64: In * AFog + Mem * (1-AFog) */
            A_BLEND (GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA);
            n64_blender_flags |= GLITCH64_N64_FOG_ALPHA;
            break;

         case 0x5000: /* V8 explosions */
            A_BLEND (GR_BLEND_ONE_MINUS_SRC_ALPHA, GR_BLEND_SRC_ALPHA);
            break;

         case 0xFA00: /* Bomberman second attack */
            A_BLEND(GR_BLEND_ONE, GR_BLEND_ZERO);
            break;

         default:
            A_BLEND (GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA);
      }
   }
   else if (blendmode == BLEND_XLU) // Mia Soccer Lights
      A_BLEND (GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA);
   else if ((settings.hacks&hack_Pilotwings) && gDP.otherMode.clearOnCvg) //RDP_COLOR_ON_CVG without FORCE_BL
      A_BLEND (GR_BLEND_ZERO, GR_BLEND_ONE);
#if 0
   else if ((settings.hacks & hack_Blastcorps))
   {
      A_BLEND (GR_BLEND_ZERO, GR_BLEND_ONE);
   }
#endif
   else
      A_BLEND (GR_BLEND_ONE, GR_BLEND_ZERO);

   // RDP_ALPHA_CVG_SELECT means full alpha
   // The reason it wasn't working before was because I wasn't handling rdp:setothermode
   if (gDP.otherMode.alphaCvgSel && ((gDP.otherMode.l & 0x7000) != 0x7000))
   {
      switch (blendmode)
      {
         case 0x4055:      /* Mario Golf */
         case 0x5055:      /* Paper Mario intro
                              Mem * AIn + Mem * AMem */
            A_BLEND (GR_BLEND_ZERO, GR_BLEND_ONE);
            break;
         default:
            A_BLEND (GR_BLEND_ONE, GR_BLEND_ZERO);
            break;
      }
   }

   if (settings.hacks&hack_ISS64)
   {
      if (gDP.otherMode.l == 0xff5a6379)
      {
         A_BLEND (GR_BLEND_ZERO, GR_BLEND_SRC_ALPHA);
      }
      else if (gDP.otherMode.l == 0x00504dd9) //players shadows. CVG_DST_WRAP
      {
         A_BLEND (GR_BLEND_ZERO, GR_BLEND_ONE);
      }
   }
   else if (settings.hacks&hack_TGR)
   {
      if (gDP.otherMode.l == 0x0f0a0235)
      {
         A_BLEND (GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA);
      }
   }
   //*/
}

/* Glide64's own draws, as N64 combiners: (0 - 0) * 0 + IN */
static void glide64_combine_pass(unsigned rgb_in, unsigned alpha_in, unsigned flags, const float prim[4])
{
   static const float zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
   glitch64_n64_combiner c;
   memset(&c, 0, sizeof(c));
   c.rgb[1][0] = 15; c.rgb[1][1] = 15; c.rgb[1][2] = 31; c.rgb[1][3] = (uint8_t)rgb_in;
   c.alpha[1][0] = 7; c.alpha[1][1] = 7; c.alpha[1][2] = 7; c.alpha[1][3] = (uint8_t)alpha_in;
   c.flags = (uint8_t)flags;
   grN64Combine(&c);
   grN64Constants(prim ? prim : zero, zero, zero, zero, zero, zero);
}

void glide64_combine_texel(int tmu, int fog_rgb)
{
   /* tile 0 sits in TMU 0, tile 1 in TMU 1 */
   const unsigned t = tmu == GR_TMU0 ? 1 : 2;
   glide64_combine_pass(t, t, fog_rgb ? GLITCH64_N64_FOG_RGB : 0, NULL);
   g_gdp.flags |= UPDATE_COMBINE;
}

void glide64_combine_constant(uint32_t rgba)
{
   float c[4];
   c[0] = ((rgba >> 24) & 0xFF) / 255.0f;
   c[1] = ((rgba >> 16) & 0xFF) / 255.0f;
   c[2] = ((rgba >> 8) & 0xFF) / 255.0f;
   c[3] = (rgba & 0xFF) / 255.0f;
   glide64_combine_pass(3, 3, 0, c);
   g_gdp.flags |= UPDATE_COMBINE;
}

void InitCombine(void)
{
   memset(&cmb, 0, sizeof(cmb));
}
