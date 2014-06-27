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

#define Z_MAX (65536.0f)
#define VERTEX_SIZE sizeof(VERTEX) //Size of vertex struct

int inverted_culling;
int culling_mode;


static void vbo_draw(GLenum mode,GLint first,GLsizei count, VERTEX *v)
{
   glEnableVertexAttribArray(POSITION_ATTR);
   glVertexAttribPointer(POSITION_ATTR, 4, GL_FLOAT, false, VERTEX_SIZE, &v->x); //Position

   glEnableVertexAttribArray(COLOUR_ATTR);
   glVertexAttribPointer(COLOUR_ATTR, 4, GL_UNSIGNED_BYTE, true, VERTEX_SIZE, &v->b); //Colour

   glEnableVertexAttribArray(TEXCOORD_0_ATTR);
   glVertexAttribPointer(TEXCOORD_0_ATTR, 2, GL_FLOAT, false, VERTEX_SIZE, &v->coord[2]); //Tex0

   glEnableVertexAttribArray(TEXCOORD_1_ATTR);
   glVertexAttribPointer(TEXCOORD_1_ATTR, 2, GL_FLOAT, false, VERTEX_SIZE, &v->coord[0]); //Tex1

   glEnableVertexAttribArray(FOG_ATTR);
   glVertexAttribPointer(FOG_ATTR, 1, GL_FLOAT, false, VERTEX_SIZE, &v->f); //Fog

   glDrawArrays(mode,first,count);
}

#define ZCALC(z, q) ((z_en) ? ((z) / Z_MAX) / (q) : 1.0f)

static INLINE float ytex(int tmu, float y)
{
   if (invtex[tmu])
      return invtex[tmu] - y;
   else
      return y;
}

void init_geometry(void)
{
   inverted_culling = 0;

   glDisable(GL_CULL_FACE);
   glDisable(GL_DEPTH_TEST);
}

FX_ENTRY void FX_CALL
grCullMode( GrCullMode_t mode )
{
	static int oldmode = -1, oldinv = -1;
	culling_mode = mode;
   LOG("grCullMode(%d)\r\n", mode);
   
   if (inverted_culling == oldinv && oldmode == mode)
      return;
   oldmode = mode;
   oldinv = inverted_culling;
   switch(mode)
   {
      case GR_CULL_DISABLE:
         glDisable(GL_CULL_FACE);
         break;
      case GR_CULL_NEGATIVE:
         if (!inverted_culling)
            glCullFace(GL_FRONT);
         else
            glCullFace(GL_BACK);
         glEnable(GL_CULL_FACE);
         break;
      case GR_CULL_POSITIVE:
         if (!inverted_culling)
            glCullFace(GL_BACK);
         else
            glCullFace(GL_FRONT);
         glEnable(GL_CULL_FACE);
         break;
      default:
         DISPLAY_WARNING("unknown cull mode : %x", mode);
   }
}

// Depth buffer

FX_ENTRY void FX_CALL
grDepthBufferMode( GrDepthBufferMode_t mode )
{
   LOG("grDepthBufferMode(%d)\r\n", mode);
   switch(mode)
   {
      case GR_DEPTHBUFFER_DISABLE:
         glDisable(GL_DEPTH_TEST);
         return;
      case GR_DEPTHBUFFER_ZBUFFER:
      case GR_DEPTHBUFFER_ZBUFFER_COMPARE_TO_BIAS:
         glEnable(GL_DEPTH_TEST);
         break;
      default:
         DISPLAY_WARNING("unknown depth buffer mode : %x", mode);
   }
}

FX_ENTRY void FX_CALL
grDepthBufferFunction( GrCmpFnc_t function )
{
   LOG("grDepthBufferFunction(%d)\r\n", function);
   switch(function)
   {
      case GR_CMP_GEQUAL:
         glDepthFunc(GL_GEQUAL);
         break;
      case GR_CMP_LEQUAL:
         glDepthFunc(GL_LEQUAL);
         break;
      case GR_CMP_LESS:
         glDepthFunc(GL_LESS);
         break;
      case GR_CMP_ALWAYS:
         glDepthFunc(GL_ALWAYS);
         break;
      case GR_CMP_EQUAL:
         glDepthFunc(GL_EQUAL);
         break;
      case GR_CMP_GREATER:
         glDepthFunc(GL_GREATER);
         break;
      case GR_CMP_NEVER:
         glDepthFunc(GL_NEVER);
         break;
      case GR_CMP_NOTEQUAL:
         glDepthFunc(GL_NOTEQUAL);
         break;

      default:
         DISPLAY_WARNING("unknown depth buffer function : %x", function);
   }
}

FX_ENTRY void FX_CALL
grDepthMask( FxBool mask )
{
   LOG("grDepthMask(%d)\r\n", mask);
   glDepthMask(mask);
}

extern retro_log_printf_t log_cb;
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

// draw

FX_ENTRY void FX_CALL
grDrawTriangle( const void *a, const void *b, const void *c )
{
   VERTEX vertex_buffer[4]
   LOG("grDrawTriangle()\r\n\t");

   if(need_to_compile)
      compile_shader();

   memcpy(&vertex_buffer[0],a,VERTEX_SIZE);
   memcpy(&vertex_buffer[1],b,VERTEX_SIZE);
   memcpy(&vertex_buffer[2],c,VERTEX_SIZE);

   vbo_draw(GL_TRIANGLES,0,3,&vertex_buffer[0]);
}

FX_ENTRY void FX_CALL
grDrawVertexArray(FxU32 mode, FxU32 Count, void *pointers2)
{
   void **pointers = (void**)pointers2;
   LOG("grDrawVertexArray(%d,%d)\r\n", mode, Count);

   if(need_to_compile)
      compile_shader();

   vbo_draw(GL_TRIANGLE_FAN,0,Count,pointers[0]);
}

FX_ENTRY void FX_CALL
grDrawVertexArrayContiguous(FxU32 mode, FxU32 Count, void *pointers, FxU32 stride)
{
   LOG("grDrawVertexArrayContiguous(%d,%d,%d)\r\n", mode, Count, stride);

   if(stride != 156)
      LOGINFO("Incompatible stride\n");

   if(need_to_compile)
      compile_shader();

   vbo_draw(GL_TRIANGLE_STRIP,0,Count,pointers);
}
