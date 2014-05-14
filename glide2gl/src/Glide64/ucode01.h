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
// vertex - loads vertices
//

static void uc1_vertex(uint32_t w0, uint32_t w1)
{
   int v0, n;
   v0 = (w0 >> 17) & 0x7F; // Current vertex
   n = (w0 >> 10) & 0x3F; // Number to copy
   rsp_vertex(v0, n);
}

//
// tri1 - renders a triangle
//

static void uc1_tri1(uint32_t w0, uint32_t w1)
{
   VERTEX *v[3];
   if (rdp.skip_drawing)
      return;

#if 0
   FRDP("uc1:tri1 #%d - %d, %d, %d - %08lx - %08lx\n", rdp.tri_n,
         ((w1 >> 17) & 0x7F),
         ((w1 >> 9) & 0x7F),
         ((w1 >> 1) & 0x7F), w0, w1);
#endif

   v[0] = &rdp.vtx[(w1 >> 17) & 0x7F];
   v[1] = &rdp.vtx[(w1 >> 9) & 0x7F];
   v[2] = &rdp.vtx[(w1 >> 1) & 0x7F];

   rsp_tri1(v, 0);
}

static void uc1_tri2(uint32_t w0, uint32_t w1)
{
   VERTEX *v[6];
   if (rdp.skip_drawing)
      return;
#if 0
   LRDP("uc1:tri2");

   FRDP(" #%d, #%d - %d, %d, %d - %d, %d, %d\n", rdp.tri_n, rdp.tri_n+1,
         ((w0 >> 17) & 0x7F),
         ((w0 >> 9) & 0x7F),
         ((w0 >> 1) & 0x7F),
         ((w1 >> 17) & 0x7F),
         ((w1 >> 9) & 0x7F),
         ((w1 >> 1) & 0x7F));
#endif

   v[0] = &rdp.vtx[(w0 >> 17) & 0x7F];
   v[1] = &rdp.vtx[(w0 >> 9) & 0x7F];
   v[2] = &rdp.vtx[(w0 >> 1) & 0x7F];
   v[3] = &rdp.vtx[(w1 >> 17) & 0x7F];
   v[4] = &rdp.vtx[(w1 >> 9) & 0x7F];
   v[5] = &rdp.vtx[(w1 >> 1) & 0x7F];

   rsp_tri2(v);
}

static void uc1_line3d(uint32_t w0, uint32_t w1)
{
   if (!settings.force_quad3d && ((w1 & 0xFF000000) == 0) && ((w0 & 0x00FFFFFF) == 0))
   {
      uint32_t cull_mode;
      VERTEX *v[3];
      uint16_t width = (uint16_t)(w1 & 0xFF) + 3;

#if 0
      FRDP("uc1:line3d width: %d #%d, #%d - %d, %d\n", width, rdp.tri_n, rdp.tri_n+1,
            (w1 >> 17) & 0x7F,
            (w1 >> 9) & 0x7F);
#endif

      v[0] = &rdp.vtx[(w1 >> 17) & 0x7F];
      v[1] = &rdp.vtx[(w1 >> 9) & 0x7F];
      v[2] = &rdp.vtx[(w1 >> 9) & 0x7F];
      cull_mode = (rdp.flags & CULLMASK) >> CULLSHIFT;
      rdp.flags |= CULLMASK;
      rdp.update |= UPDATE_CULL_MODE;
      rsp_tri1(v, width);
      rdp.flags ^= CULLMASK;
      rdp.flags |= cull_mode << CULLSHIFT;
      rdp.update |= UPDATE_CULL_MODE;
   }
   else
   {
      VERTEX *v[6];
      //FRDP("uc1:quad3d #%d, #%d\n", rdp.tri_n, rdp.tri_n+1);

      v[0] = &rdp.vtx[(w1 >> 25) & 0x7F];
      v[1] = &rdp.vtx[(w1 >> 17) & 0x7F];
      v[2] = &rdp.vtx[(w1 >> 9) & 0x7F];
      v[3] = &rdp.vtx[(w1 >> 1) & 0x7F];
      v[4] = &rdp.vtx[(w1 >> 25) & 0x7F];
      v[5] = &rdp.vtx[(w1 >> 9) & 0x7F];

      rsp_tri2(v);
   }
}

uint32_t branch_dl = 0;

static void uc1_rdphalf_1(uint32_t w0, uint32_t w1)
{
   LRDP("uc1:rdphalf_1\n");
   branch_dl = w1;
   rdphalf_1(w0, w1);
}

static void uc1_branch_z(uint32_t w0, uint32_t w1)
{
   gSPBranchLessZ( branch_dl, _SHIFTR( w0, 1, 11 ), (int32_t)w1 );
}
