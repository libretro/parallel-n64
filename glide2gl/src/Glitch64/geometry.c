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

static int xy_off;
static int xy_en;
static int z_en;
static int z_off;
static int q_off;
static int q_en;
static int pargb_off;
static int pargb_en;
static int st0_off;
static int st0_en;
static int st1_off;
static int st1_en;
static int fog_ext_off;
static int fog_ext_en;

int inverted_culling;
int culling_mode;

void *previous_pointers;

#define VERTEX_BUFFER_SIZE 1500 //Max amount of vertices to buffer, this seems large enough.
static VERTEX vertex_buffer[VERTEX_BUFFER_SIZE];
static int vertex_buffer_count = 0;
static GLenum vertex_draw_mode;
static bool vertex_buffer_enabled = false;

void vbo_init(void) { }

void vbo_draw(void)
{
   if(!vertex_buffer_count)
      return;

   glDrawArrays(vertex_draw_mode,0,vertex_buffer_count);
   vertex_buffer_count = 0;
}

//Buffer vertices instead of glDrawArrays(...)
static void vbo_buffer(GLenum mode,GLint first,GLsizei count,void* pointers)
{
   if(!vertex_buffer_enabled && previous_pointers != pointers)
   {
      VERTEX *v = (VERTEX*)&vertex_buffer[0];
      // enable vertex buffer if not already enabled

      vertex_buffer_enabled = true;
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
      
      previous_pointers = pointers;
   }

   if((count != 3 && mode != GL_TRIANGLES) || vertex_buffer_count + count > VERTEX_BUFFER_SIZE)
      vbo_draw();

   memcpy(&vertex_buffer[vertex_buffer_count],pointers,count * VERTEX_SIZE);
   vertex_buffer_count += count;

   if(count == 3 || mode == GL_TRIANGLES)
      vertex_draw_mode = GL_TRIANGLES;
   else
   {
      vertex_draw_mode = mode;
      vbo_draw(); //Triangle fans and strips can't be joined as easily, just draw them straight away.
   }
}

#define ZCALC(z, q) ((z_en) ? ((z) / Z_MAX) / (q) : 1.0f)

void vbo_disable(void)
{
   vbo_draw();
   vertex_buffer_enabled = false;
}

static INLINE float ytex(int tmu, float y)
{
   if (invtex[tmu])
      return invtex[tmu] - y;
   else
      return y;
}

void init_geometry(void)
{
   xy_en = q_en = pargb_en = st0_en = st1_en = z_en = 0;
   inverted_culling = 0;

   glDisable(GL_CULL_FACE);
   glDisable(GL_DEPTH_TEST);

   vbo_init();
}

FX_ENTRY void FX_CALL
grCoordinateSpace( GrCoordinateSpaceMode_t mode )
{
   LOG("grCoordinateSpace(%d)\r\n", mode);
   switch(mode)
   {
      case GR_WINDOW_COORDS:
         break;
      default:
         DISPLAY_WARNING("unknwown coordinate space : %x", mode);
   }
}

FX_ENTRY void FX_CALL
grVertexLayout(FxU32 param, FxI32 offset, FxU32 mode)
{
   LOG("grVertexLayout(%d,%d,%d)\r\n", param, offset, mode);
   switch(param)
   {
      case GR_PARAM_XY:
         xy_en = mode;
         xy_off = offset;
         break;
      case GR_PARAM_Z:
         z_en = mode;
         z_off = offset;
         break;
      case GR_PARAM_Q:
         q_en = mode;
         q_off = offset;
         break;
      case GR_PARAM_FOG_EXT:
         fog_ext_en = mode;
         fog_ext_off = offset;
         break;
      case GR_PARAM_PARGB:
         pargb_en = mode;
         pargb_off = offset;
         break;
      case GR_PARAM_ST0:
         st0_en = mode;
         st0_off = offset;
         break;
      case GR_PARAM_ST1:
         st1_en = mode;
         st1_off = offset;
         break;
      default:
         DISPLAY_WARNING("unknown grVertexLayout parameter : %x", param);
   }
}

FX_ENTRY void FX_CALL
grCullMode( GrCullMode_t mode )
{
   LOG("grCullMode(%d)\r\n", mode);
   static int oldmode = -1, oldinv = -1;
   culling_mode = mode;
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
#if defined(__LIBRETRO__) // TODO: How to calculate this?
   if (biasFound)
      return;

   const char *renderer = (const char*)glGetString(GL_RENDERER);

   if (log_cb)
      log_cb(RETRO_LOG_INFO, "GL_RENDERER: %s\n", renderer);

   polygonOffsetFactor = 0;
   polygonOffsetUnits  = -3.0f;

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
      glPolygonOffset(0, (float)level * settings.depth_bias * 0.01 );
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
   LOG("grDrawTriangle()\r\n\t");

   if(need_to_compile)
      compile_shader();

   if(vertex_buffer_count + 3 > VERTEX_BUFFER_SIZE)
      vbo_draw();

   vertex_draw_mode = GL_TRIANGLES;
   memcpy(&vertex_buffer[vertex_buffer_count],a,VERTEX_SIZE);
   memcpy(&vertex_buffer[vertex_buffer_count+1],b,VERTEX_SIZE);
   memcpy(&vertex_buffer[vertex_buffer_count+2],c,VERTEX_SIZE);
   vertex_buffer_count += 3;
}

FX_ENTRY void FX_CALL
grDrawVertexArray(FxU32 mode, FxU32 Count, void *pointers2)
{
   void **pointers = (void**)pointers2;
   LOG("grDrawVertexArray(%d,%d)\r\n", mode, Count);

   if(need_to_compile)
      compile_shader();

   vbo_buffer(GL_TRIANGLE_FAN,0,Count,pointers[0]);
}

FX_ENTRY void FX_CALL
grDrawVertexArrayContiguous(FxU32 mode, FxU32 Count, void *pointers, FxU32 stride)
{
   LOG("grDrawVertexArrayContiguous(%d,%d,%d)\r\n", mode, Count, stride);

   if(stride != 156)
      LOGINFO("Incompatible stride\n");

   if(need_to_compile)
      compile_shader();

   //only calls ever made are for GR_TRIANGLE_STRIP from Glide64 - so optimize for that
   vbo_buffer(GL_TRIANGLE_STRIP,0,Count,pointers);
}
