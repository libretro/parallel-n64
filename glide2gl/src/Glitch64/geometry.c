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

#include <stdint.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif // _WIN32
#include "glide.h"
#include "main.h"
#include "../Glide64/rdp.h"

FX_ENTRY void FX_CALL
grCullMode( GrCullMode_t mode )
{
   switch(mode)
   {
      case GR_CULL_DISABLE:
         glDisable(GL_CULL_FACE);
         break;
      case GR_CULL_NEGATIVE:
         glCullFace(GL_FRONT);
         glEnable(GL_CULL_FACE);
         break;
      case GR_CULL_POSITIVE:
         glCullFace(GL_BACK);
         glEnable(GL_CULL_FACE);
         break;
   }
}

// Depth buffer

bool biasFound = false;
float polygonOffsetFactor;
float polygonOffsetUnits;
void FindBestDepthBias(void)
{
	const char *renderer = NULL;
#if defined(__LIBRETRO__) // TODO: How to calculate this?
   if (biasFound)
      return;

   renderer = (const char*)glGetString(GL_RENDERER);

   if (log_cb)
      log_cb(RETRO_LOG_INFO, "GL_RENDERER: %s\n", renderer);

   biasFound = true;
#else
#if 0
   float f, bestz = 0.25f;
   int x;
   if (biasFactor) return;
   biasFactor = 64.0f; // default value
   glPushAttrib(GL_ALL_ATTRIB_BITS);
   glEnable(GL_DEPTH_TEST);
   glDepthFunc(GL_ALWAYS);
   glEnable(GL_POLYGON_OFFSET_FILL);
   glDrawBuffer(GL_BACK);
   glReadBuffer(GL_BACK);
   glDisable(GL_BLEND);
   glDisable(GL_ALPHA_TEST);
   glColor4ub(255,255,255,255);
   glDepthMask(GL_TRUE);
   for (x=0, f=1.0f; f<=65536.0f; x+=4, f*=2.0f) {
      float z;
      glPolygonOffset(0, f);
      glBegin(GL_TRIANGLE_STRIP);
      glVertex3f(float(x+4 - widtho)/(width/2), float(0 - heighto)/(height/2), 0.5);
      glVertex3f(float(x - widtho)/(width/2), float(0 - heighto)/(height/2), 0.5);
      glVertex3f(float(x+4 - widtho)/(width/2), float(4 - heighto)/(height/2), 0.5);
      glVertex3f(float(x - widtho)/(width/2), float(4 - heighto)/(height/2), 0.5);
      glEnd();
      glReadPixels(x+2, 2, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &z);
      z -= 0.75f + 8e-6f;
      if (z<0.0f) z = -z;
      if (z > 0.01f) continue;
      if (z < bestz) {
         bestz = z;
         biasFactor = f;
      }
      //printf("f %g z %g\n", f, z);
   }
   //printf(" --> bias factor %g\n", biasFactor);
   glPopAttrib();
#endif
#endif
}

FX_ENTRY void FX_CALL
grDepthBiasLevel( FxI32 level )
{
   LOG("grDepthBiasLevel(%d)\r\n", level);
   if (level)
   {
      glPolygonOffset(polygonOffsetFactor, (float)level * settings.depth_bias * 0.01 );
      glEnable(GL_POLYGON_OFFSET_FILL);
   }
   else
   {
      glPolygonOffset(0,0);
      glDisable(GL_POLYGON_OFFSET_FILL);
   }
}

FX_ENTRY void FX_CALL
grDrawVertexArrayContiguous(FxU32 mode, FxU32 count, void *pointers, int do_convert)
{
   int i = 0;
   VERTEX *v = (VERTEX*)pointers;

   if (do_convert)
   {
      for (i = 0; i < count; i++)
      {
         v[i].coord[0] = v[i].u0;
         v[i].coord[1] = v[i].v0;
         v[i].coord[2] = v[i].u1;
         v[i].coord[3] = v[i].v1;
      }
   }

   if(need_to_compile)
      compile_shader();

   glEnableVertexAttribArray(POSITION_ATTR);
   glVertexAttribPointer(POSITION_ATTR, 4, GL_FLOAT, false, sizeof(VERTEX), &v->x); //Position

   glEnableVertexAttribArray(COLOUR_ATTR);
   glVertexAttribPointer(COLOUR_ATTR, 4, GL_UNSIGNED_BYTE, true, sizeof(VERTEX), &v->b); //Colour

   glEnableVertexAttribArray(TEXCOORD_0_ATTR);
   glVertexAttribPointer(TEXCOORD_0_ATTR, 2, GL_FLOAT, false, sizeof(VERTEX), &v->coord[2]); //Tex0

   glEnableVertexAttribArray(TEXCOORD_1_ATTR);
   glVertexAttribPointer(TEXCOORD_1_ATTR, 2, GL_FLOAT, false, sizeof(VERTEX), &v->coord[0]); //Tex1

   glEnableVertexAttribArray(FOG_ATTR);
   glVertexAttribPointer(FOG_ATTR, 1, GL_FLOAT, false, sizeof(VERTEX), &v->f); //Fog

   glDrawArrays(mode, 0, count);
}
