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

int cur_mtx = 0;
int billboarding = 0;
int vtx_last = 0;
uint32_t dma_offset_mtx = 0;
uint32_t dma_offset_vtx = 0;

static void uc5_dma_offsets(uint32_t w0, uint32_t w1)
{
  dma_offset_mtx = w0 & 0x00FFFFFF;
  dma_offset_vtx = w1 & 0x00FFFFFF;
  vtx_last = 0;
  FRDP("uc5:dma_offsets - mtx: %08lx, vtx: %08lx\n", dma_offset_mtx, dma_offset_vtx);
}

static void uc5_matrix(uint32_t w0, uint32_t w1)
{
   // Use segment offset to get the address
   uint32_t addr = dma_offset_mtx + (segoffset(w1) & BMASK);

   uint8_t n = (uint8_t)((w0 >> 16) & 0xF);
   uint8_t multiply;

   if (n == 0) //DKR
   {
      n = (uint8_t)((w0 >> 22) & 0x3);
      multiply = 0;
   }
   else //JF
      multiply = (uint8_t)((w0 >> 23) & 0x1);

   cur_mtx = n;

   FRDP("uc5:matrix - #%d, addr: %08lx\n", n, addr);

   if (multiply)
   {
      DECLAREALIGN16VAR(m[4][4]);
      load_matrix(m, addr);
      DECLAREALIGN16VAR(m_src[4][4]);
      memcpy (m_src, rdp.dkrproj[0], 64);
      MulMatrices(m, m_src, rdp.dkrproj[n]);
   }
   else
      load_matrix(rdp.dkrproj[n], addr);

   rdp.update |= UPDATE_MULT_MAT;

#ifdef EXTREME_LOGGING
   int i;
   FRDP ("{%f,%f,%f,%f}\n", rdp.dkrproj[n][0][0], rdp.dkrproj[n][0][1], rdp.dkrproj[n][0][2], rdp.dkrproj[n][0][3]);
   FRDP ("{%f,%f,%f,%f}\n", rdp.dkrproj[n][1][0], rdp.dkrproj[n][1][1], rdp.dkrproj[n][1][2], rdp.dkrproj[n][1][3]);
   FRDP ("{%f,%f,%f,%f}\n", rdp.dkrproj[n][2][0], rdp.dkrproj[n][2][1], rdp.dkrproj[n][2][2], rdp.dkrproj[n][2][3]);
   FRDP ("{%f,%f,%f,%f}\n", rdp.dkrproj[n][3][0], rdp.dkrproj[n][3][1], rdp.dkrproj[n][3][2], rdp.dkrproj[n][3][3]);

   for ( i = 0; i < 3; i++)
   {
      FRDP ("proj %d\n", i);
      FRDP ("{%f,%f,%f,%f}\n", rdp.dkrproj[i][0][0], rdp.dkrproj[i][0][1], rdp.dkrproj[i][0][2], rdp.dkrproj[i][0][3]);
      FRDP ("{%f,%f,%f,%f}\n", rdp.dkrproj[i][1][0], rdp.dkrproj[i][1][1], rdp.dkrproj[i][1][2], rdp.dkrproj[i][1][3]);
      FRDP ("{%f,%f,%f,%f}\n", rdp.dkrproj[i][2][0], rdp.dkrproj[i][2][1], rdp.dkrproj[i][2][2], rdp.dkrproj[i][2][3]);
      FRDP ("{%f,%f,%f,%f}\n", rdp.dkrproj[i][3][0], rdp.dkrproj[i][3][1], rdp.dkrproj[i][3][2], rdp.dkrproj[i][3][3]);
   }
#endif
}

static void uc5_vertex(uint32_t w0, uint32_t w1)
{
   int i;
   uint32_t addr = dma_offset_vtx + (segoffset(w1) & BMASK);

   // | cccc cccc 1111 1??? 0000 0002 2222 2222 | cmd1 = address |
   // c = vtx command
   // 1 = method #1 of getting count
   // 2 = method #2 of getting count
   // ? = unknown, but used
   // 0 = unused

   int n = ((w0 >> 19) & 0x1F);// + 1;
   if (settings.hacks&hack_Diddy)
      n++;

   if (w0 & G_FOG)
   {
      if (billboarding)
         vtx_last = 1;
   }
   else
      vtx_last = 0;

   int first = ((w0 >> 9) & 0x1F) + vtx_last;
   FRDP ("uc5:vertex - addr: %08lx, first: %d, count: %d, matrix: %08lx\n", addr, first, n, cur_mtx);

   int prj = cur_mtx;

   int start = 0;
   float x, y, z;
   for (i = first; i < first + n; i++)
   {
      start = (i-first) * 10;
      VERTEX *v = &rdp.vtx[i];
      x   = (float)((int16_t*)gfx.RDRAM)[(((addr+start) >> 1) + 0)^1];
      y   = (float)((int16_t*)gfx.RDRAM)[(((addr+start) >> 1) + 1)^1];
      z   = (float)((int16_t*)gfx.RDRAM)[(((addr+start) >> 1) + 2)^1];

      v->x = x*rdp.dkrproj[prj][0][0] + y*rdp.dkrproj[prj][1][0] + z*rdp.dkrproj[prj][2][0] + rdp.dkrproj[prj][3][0];
      v->y = x*rdp.dkrproj[prj][0][1] + y*rdp.dkrproj[prj][1][1] + z*rdp.dkrproj[prj][2][1] + rdp.dkrproj[prj][3][1];
      v->z = x*rdp.dkrproj[prj][0][2] + y*rdp.dkrproj[prj][1][2] + z*rdp.dkrproj[prj][2][2] + rdp.dkrproj[prj][3][2];
      v->w = x*rdp.dkrproj[prj][0][3] + y*rdp.dkrproj[prj][1][3] + z*rdp.dkrproj[prj][2][3] + rdp.dkrproj[prj][3][3];

      if (billboarding)
      {
         v->x += rdp.vtx[0].x;
         v->y += rdp.vtx[0].y;
         v->z += rdp.vtx[0].z;
         v->w += rdp.vtx[0].w;
      }

      if (fabs(v->w) < 0.001)
         v->w = 0.001f;

      v->oow = 1.0f / v->w;
      v->x_w = v->x * v->oow;
      v->y_w = v->y * v->oow;
      v->z_w = v->z * v->oow;

      v->uv_calculated = 0xFFFFFFFF;
      v->screen_translated = 0;
      v->shade_mod = 0;

      v->scr_off = 0;
      if (v->x < -v->w)
         v->scr_off |= 1;
      if (v->x > v->w)
         v->scr_off |= 2;
      if (v->y < -v->w)
         v->scr_off |= 4;
      if (v->y > v->w)
         v->scr_off |= 8;
      if (v->w < 0.1f)
         v->scr_off |= 16;
      if (fabs(v->z_w) > 1.0)
         v->scr_off |= 32;

      v->r = ((uint8_t*)gfx.RDRAM)[(addr+start + 6)^3];
      v->g = ((uint8_t*)gfx.RDRAM)[(addr+start + 7)^3];
      v->b = ((uint8_t*)gfx.RDRAM)[(addr+start + 8)^3];
      v->a = ((uint8_t*)gfx.RDRAM)[(addr+start + 9)^3];
      CalculateFog (v);

#ifdef EXTREME_LOGGING
      FRDP ("v%d - x: %f, y: %f, z: %f, w: %f, z_w: %f, r=%d, g=%d, b=%d, a=%d\n", i, v->x, v->y, v->z, v->w, v->z_w, v->r, v->g, v->b, v->a);
#endif
   }

   vtx_last += n;
}

static void uc5_tridma(uint32_t w0, uint32_t w1)
{
   vtx_last = 0;    // we've drawn something, so the vertex index needs resetting

   // | cccc cccc 2222 0000 1111 1111 1111 0000 | cmd1 = address |
   // c = tridma command
   // 1 = method #1 of getting count
   // 2 = method #2 of getting count
   // 0 = unused

   uint32_t addr = segoffset(w1) & BMASK;
   int num = (w0 & 0xFFF0) >> 4;
   //int num = ((w0 & 0x00F00000) >> 20) + 1;  // same thing!
   FRDP("uc5:tridma #%d - addr: %08lx, count: %d\n", rdp.tri_n, addr, num);

   int i, start, v0, v1, v2, flags;

   for (i = 0; i < num; i++)
   {
      start = i << 4;
      v0 = gfx.RDRAM[addr+start];
      v1 = gfx.RDRAM[addr+start+1];
      v2 = gfx.RDRAM[addr+start+2];

      FRDP("tri #%d - %d, %d, %d\n", rdp.tri_n, v0, v1, v2);

      VERTEX *v[3];
      v[0] = &rdp.vtx[v0];
      v[1] = &rdp.vtx[v1];
      v[2] = &rdp.vtx[v2];

      flags = gfx.RDRAM[addr+start+3];

      if (flags & 0x40)
      { // no cull
         rdp.flags &= ~CULLMASK;
         grCullMode (GR_CULL_DISABLE);
      }
      else
      {        // front cull
         rdp.flags &= ~CULLMASK;
         if (rdp.view_scale[0] < 0)
         {
            rdp.flags |= CULL_BACK;   // agh, backwards culling
            grCullMode (GR_CULL_POSITIVE);
         }
         else
         {
            rdp.flags |= CULL_FRONT;
            grCullMode (GR_CULL_NEGATIVE);
         }
      }
      start += 4;

      v[0]->ou = (float)((int16_t*)gfx.RDRAM)[((addr+start) >> 1) + 5] / 32.0f;
      v[0]->ov = (float)((int16_t*)gfx.RDRAM)[((addr+start) >> 1) + 4] / 32.0f;
      v[1]->ou = (float)((int16_t*)gfx.RDRAM)[((addr+start) >> 1) + 3] / 32.0f;
      v[1]->ov = (float)((int16_t*)gfx.RDRAM)[((addr+start) >> 1) + 2] / 32.0f;
      v[2]->ou = (float)((int16_t*)gfx.RDRAM)[((addr+start) >> 1) + 1] / 32.0f;
      v[2]->ov = (float)((int16_t*)gfx.RDRAM)[((addr+start) >> 1) + 0] / 32.0f;

      v[0]->uv_calculated = 0xFFFFFFFF;
      v[1]->uv_calculated = 0xFFFFFFFF;
      v[2]->uv_calculated = 0xFFFFFFFF;

      gsSP1Triangle(v0, v1, v2, 0, true);
   }
}

static void uc5_dl_in_mem(uint32_t w0, uint32_t w1)
{
   uint32_t addr = segoffset(w1) & BMASK;
   int count = (w0 & 0x00FF0000) >> 16;
   FRDP ("uc5:dl_in_mem - addr: %08lx, count: %d\n", addr, count);

   if (rdp.pc_i >= 9)
      return;

   rdp.pc_i ++;  // go to the next PC in the stack
   rdp.pc[rdp.pc_i] = addr;  // jump to the address
   rdp.dl_count = count + 1;
}

static void uc5_moveword(uint32_t w0, uint32_t w1)
{
   LRDP("uc5:moveword ");

   // Find which command this is (lowest byte of cmd0)
   switch (w0 & 0xFF)
   {
      case 0x02:  // moveword matrix 2 billboard
         billboarding = (w1 & 1);
         FRDP ("matrix billboard - %s\n", str_offon[billboarding]);
         break;

      case G_MW_CLIP:
         gSPClipRatio(w0, w1);
         break;

      case G_MW_SEGMENT:
         gSPSegment((w0 >> 10) & 0x0F, w1);
         break;
      case G_MW_FOG:
         gSPFogFactor((int16_t)_SHIFTR( w1, 16, 16 ), (int16_t)_SHIFTR( w1, 0, 16 ));
         break;

      case 0x0a:  // moveword matrix select
         cur_mtx = (w1 >> 6) & 3;
         FRDP ("matrix select - mtx: %d\n", cur_mtx);
         break;

      default:
         FRDP ("(unknown) %02lx - IGNORED\n", w0 & 0xFF);
   }
}

static void uc5_setgeometrymode(uint32_t w0, uint32_t w1)
{
   rdp.geom_mode |= w1;

   if (w1 & G_ZBUFFER)  // Z-Buffer enable
   {
      if (!(rdp.flags & ZBUF_ENABLED))
      {
         rdp.flags |= ZBUF_ENABLED;
         rdp.update |= UPDATE_ZBUF_ENABLED;
      }
   }

   //Added by Gonetz
   if (w1 & G_FOG)      // Fog enable
   {
      if (!(rdp.flags & FOG_ENABLED))
      {
         rdp.flags |= FOG_ENABLED;
         rdp.update |= UPDATE_FOG_ENABLED;
      }
   }

   //FRDP("uc0:setgeometrymode %08lx\n", w1);
}

static void uc5_cleargeometrymode(uint32_t w0, uint32_t w1)
{
   rdp.geom_mode &= (~w1);

   if (w1 & G_ZBUFFER)  // Z-Buffer enable
   {
      if (rdp.flags & ZBUF_ENABLED)
      {
         rdp.flags ^= ZBUF_ENABLED;
         rdp.update |= UPDATE_ZBUF_ENABLED;
      }
   }
   //Added by Gonetz
   if (w1 & G_FOG)      // Fog enable
   {
      if (rdp.flags & FOG_ENABLED)
      {
         rdp.flags ^= FOG_ENABLED;
         rdp.update |= UPDATE_FOG_ENABLED;
      }
   }
   //FRDP("uc0:cleargeometrymode %08lx\n", w1);
}
