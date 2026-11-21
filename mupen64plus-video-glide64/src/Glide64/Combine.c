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

static void n64_blender(glitch64_n64_combiner *c);
static void n64_blender_hacks(glitch64_n64_combiner *c);

void Combine(void)
{
   const gdp_combine_modes *m = &g_gdp.combine;
   glitch64_n64_combiner c;
   float prim[4], env[4], consts[4], level[4], center[3], scale[3], convert[4];
   const int two = gDP.otherMode.cycleType == G_CYC_2CYCLE;
   /* the texture unit's second cycle converting (TEXTCONV's CONVERT_ONE):
    * its texel is the first's through K0-K3, or with BI_LERP1 too, the
    * first's blue. Only two-cycle mode runs a second cycle. */
   const int convert_one = two && (gDP.otherMode.h & (1u << 9));
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
   if (convert_one)
      c.flags |= (gDP.otherMode.h & (1u << 10)) ? GLITCH64_N64_CONVERT1_Y : GLITCH64_N64_CONVERT1;

   cmb.tex = 0;
   for (i = two ? 0 : 1; i < 2; i++)
      cmb.tex |= n64_tiles(&c, i, two && i == 1);
   /* the second texel read from the first's tile: tile 1 is not loaded */
   if (convert_one && (cmb.tex & 2))
      cmb.tex = 1;


   n64_blender(&c);
   n64_blender_hacks(&c);

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
   /* as the RDP takes them: 2k + 1, over the 256 of its 8-bit steps */
   convert[0] = (2 * g_gdp.k0 + 1) / 256.0f; convert[1] = (2 * g_gdp.k1 + 1) / 256.0f;
   convert[2] = (2 * g_gdp.k2 + 1) / 256.0f; convert[3] = (2 * g_gdp.k3 + 1) / 256.0f;
   grN64Constants(prim, env, consts, level, center, scale, convert);

}

/* the blender's cycle @i as the other modes hold it (GLITCH64_BL) */
static unsigned n64_blender_cycle(int i)
{
   if (i == 0)
      return GLITCH64_BL(gDP.otherMode.c1_m1a, gDP.otherMode.c1_m1b, gDP.otherMode.c1_m2a, gDP.otherMode.c1_m2b);
   return GLITCH64_BL(gDP.otherMode.c2_m1a, gDP.otherMode.c2_m1b, gDP.otherMode.c2_m2a, gDP.otherMode.c2_m2b);
}

/* The blender as the RDP has it: P*A + M*B, its inputs as the other modes
 * select them. A cycle reading no memory colour runs in the shader; one
 * that does, the shader gives the colour and the alpha the GPU blends
 * with the framebuffer. Coverage is not kept: a pixel blends where
 * FORCE_BL says (the RDP blends edges too), else it is P. */
static void n64_blender(glitch64_n64_combiner *c)
{
   const int two = gDP.otherMode.cycleType == G_CYC_2CYCLE;
   unsigned last  = n64_blender_cycle(two ? 1 : 0);
   unsigned first = n64_blender_cycle(0);
   int force      = gDP.otherMode.forceBlender;
   unsigned p, a, mm, b;

   c->blend[0] = c->blend[1] = 0;
   if (gDP.otherMode.cycleType >= G_CYC_COPY)
   {
      A_BLEND (GR_BLEND_ONE, GR_BLEND_ZERO);
      return;
   }
   if (gDP.otherMode.alphaCvgSel)
      c->flags |= GLITCH64_N64_CVG_ALPHA;

   if (two)
   {
      /* the first cycle always blends, into the second's pixel */
      if (GLITCH64_BL_P(first) != GLITCH64_BL_MEM && GLITCH64_BL_M(first) != GLITCH64_BL_MEM)
      {
         c->flags |= GLITCH64_N64_BLEND0;
         c->blend[0] = (uint8_t)first;
      }
      else
      {
         /* it reads memory: the GPU blends it, the second cycle left out */
         last  = first;
         force = 1;
      }
   }

   p  = GLITCH64_BL_P(last);
   a  = GLITCH64_BL_A(last);
   mm = GLITCH64_BL_M(last);
   b  = GLITCH64_BL_B(last);

   if (!force)
   {
      /* not blended: P */
      if (p == GLITCH64_BL_MEM)
         A_BLEND (GR_BLEND_ZERO, GR_BLEND_ONE);
      else
      {
         c->blend[1] = GLITCH64_BL(p, GLITCH64_BL_A_IN, 0, 0);
         A_BLEND (GR_BLEND_ONE, GR_BLEND_ZERO);
      }
      return;
   }

   if (p != GLITCH64_BL_MEM && mm != GLITCH64_BL_MEM)
   {
      c->flags |= GLITCH64_N64_BLEND1;
      c->blend[1] = (uint8_t)last;
      A_BLEND (GR_BLEND_ONE, GR_BLEND_ZERO);
   }
   else if (p != GLITCH64_BL_MEM)
   {
      /* P*A + memory*B */
      if (b == GLITCH64_BL_B_ZERO)
      {
         c->flags |= GLITCH64_N64_BLEND1;
         c->blend[1] = GLITCH64_BL(p, a, 0, GLITCH64_BL_B_ZERO);
         A_BLEND (GR_BLEND_ONE, GR_BLEND_ZERO);
      }
      else
      {
         c->blend[1] = GLITCH64_BL(p, a, 0, 0);
         A_BLEND (GR_BLEND_SRC_ALPHA, b == GLITCH64_BL_B_1MA ? GR_BLEND_ONE_MINUS_SRC_ALPHA : GR_BLEND_ONE);
      }
   }
   else if (mm != GLITCH64_BL_MEM)
   {
      /* memory*A + M*B */
      c->blend[1] = GLITCH64_BL(mm, a, 0, 0);
      A_BLEND (b == GLITCH64_BL_B_1MA ? GR_BLEND_ONE_MINUS_SRC_ALPHA :
            (b == GLITCH64_BL_B_ZERO ? GR_BLEND_ZERO : GR_BLEND_ONE), GR_BLEND_SRC_ALPHA);
   }
   else
   {
      /* memory*(A + B) */
      c->blend[1] = GLITCH64_BL(0, a, 0, 0);
      A_BLEND (GR_BLEND_ZERO, b == GLITCH64_BL_B_ZERO ? GR_BLEND_SRC_ALPHA : GR_BLEND_ONE);
   }
}

/* the games' own blends, kept: the pixel as the combiner gives it, and
 * the GPU blend these set */
static void n64_blender_hacks(glitch64_n64_combiner *c)
{
   int hit = 0;
   if ((settings.hacks&hack_Pilotwings) && gDP.otherMode.clearOnCvg && !gDP.otherMode.forceBlender) //RDP_COLOR_ON_CVG without FORCE_BL
   {
      A_BLEND (GR_BLEND_ZERO, GR_BLEND_ONE);
      hit = 1;
   }
   if (settings.hacks&hack_ISS64)
   {
      if (gDP.otherMode.l == 0xff5a6379)
      {
         A_BLEND (GR_BLEND_ZERO, GR_BLEND_SRC_ALPHA);
         hit = 1;
      }
      else if (gDP.otherMode.l == 0x00504dd9) //players shadows. CVG_DST_WRAP
      {
         A_BLEND (GR_BLEND_ZERO, GR_BLEND_ONE);
         hit = 1;
      }
   }
   else if ((settings.hacks&hack_TGR) && gDP.otherMode.l == 0x0f0a0235)
   {
      A_BLEND (GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA);
      hit = 1;
   }
   if (hit)
   {
      c->flags &= (uint8_t)~(GLITCH64_N64_BLEND1 | GLITCH64_N64_CVG_ALPHA);
      c->blend[1] = 0;
   }
}

/* Glide64's own draws, as N64 combiners: (0 - 0) * 0 + IN, the blender
 * giving the colour its P selects (@blend_p) and the pixel's alpha */
static void glide64_combine_pass(unsigned rgb_in, unsigned alpha_in, unsigned blend_p, const float prim[4])
{
   static const float zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
   glitch64_n64_combiner c;
   memset(&c, 0, sizeof(c));
   c.rgb[1][0] = 15; c.rgb[1][1] = 15; c.rgb[1][2] = 31; c.rgb[1][3] = (uint8_t)rgb_in;
   c.alpha[1][0] = 7; c.alpha[1][1] = 7; c.alpha[1][2] = 7; c.alpha[1][3] = (uint8_t)alpha_in;
   c.blend[1] = GLITCH64_BL(blend_p, GLITCH64_BL_A_IN, 0, 0);
   grN64Combine(&c);
   grN64Constants(prim ? prim : zero, zero, zero, zero, zero, zero, zero);
}

void glide64_combine_texel(int tmu, int fog_rgb)
{
   /* tile 0 sits in TMU 0, tile 1 in TMU 1 */
   const unsigned t = tmu == GR_TMU0 ? 1 : 2;
   glide64_combine_pass(t, t, fog_rgb ? GLITCH64_BL_FOG : GLITCH64_BL_IN, NULL);
   g_gdp.flags |= UPDATE_COMBINE;
}

void glide64_combine_constant(uint32_t rgba)
{
   float c[4];
   c[0] = ((rgba >> 24) & 0xFF) / 255.0f;
   c[1] = ((rgba >> 16) & 0xFF) / 255.0f;
   c[2] = ((rgba >> 8) & 0xFF) / 255.0f;
   c[3] = (rgba & 0xFF) / 255.0f;
   glide64_combine_pass(3, 3, GLITCH64_BL_IN, c);
   g_gdp.flags |= UPDATE_COMBINE;
}

void InitCombine(void)
{
   memset(&cmb, 0, sizeof(cmb));
}
