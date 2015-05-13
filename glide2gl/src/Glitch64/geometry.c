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
#include <stddef.h>
#include "glide.h"
#include "glitchmain.h"
#include "../Glide64/rdp.h"

/* TODO: get rid of glitch_vbo */
/* TODO: try glDrawElements */
/* TODO: #ifdefs for EMSCRIPTEN (ToadKing?) */
/* TODO: check which GR_TRIANGLE_STRIP calls can be turned into GR_TRIANGLE */
#define VERTEX_OFF(x) offsetof(VERTEX, x)
#define VERTEX_SIZE sizeof(VERTEX)
#define VERTEX_BUFFER_SIZE (1500)
static VERTEX   vbuf_data[VERTEX_BUFFER_SIZE];
static GLenum   vbuf_primitive = GL_TRIANGLES;
static unsigned vbuf_length    = 0;
static unsigned vbuf_lastlen   = 0;
static bool     vbuf_use_vbo   = false;
static bool     vbuf_enabled   = false;
static GLuint   vbuf_vbo       = 0;

#ifdef EMSCRIPTEN
struct draw_buffer {
  float x, y, z, q;

  uint8_t  b;  // These values are arranged like this so that *(uint32_t*)(VERTEX+?) is
  uint8_t  g;  // ARGB format that glide can use.
  uint8_t  r;
  uint8_t  a;

  float coord[4];

  float f; //fog
};

static struct draw_buffer *gli_vbo;
static unsigned gli_vbo_size;
#endif

void vbo_init()
{
   /* TODO: query frontend for use_vbo */
   vbuf_length = 0;
}

void vbo_free()
{
   if (vbuf_vbo)
      glDeleteBuffers(1, &vbuf_vbo);

   vbuf_vbo    = 0;
   vbuf_length = 0;
}

void vbo_draw()
{
   if (!vbuf_length)
      return;

   if (vbuf_vbo)
   {
      static bool drawing = false;

      /* avoid infinite loop in sgl*BindBuffer */
      if (drawing)
         return;

      drawing = true;

      glBindBuffer(GL_ARRAY_BUFFER, vbuf_vbo);

      if (vbuf_length >= vbuf_lastlen)
         glBufferData(GL_ARRAY_BUFFER, VERTEX_SIZE * vbuf_length, vbuf_data, GL_DYNAMIC_DRAW);
      else
         glBufferSubData(GL_ARRAY_BUFFER, 0, VERTEX_SIZE * vbuf_length, vbuf_data);

      vbuf_lastlen = vbuf_length;

      glDrawArrays(vbuf_primitive, 0, vbuf_length);
      glBindBuffer(GL_ARRAY_BUFFER, 0);

      drawing = false;
   }
   else
      glDrawArrays(vbuf_primitive, 0, vbuf_length);

   vbuf_length = 0;
}

static void vbo_append(GLenum mode, GLsizei count, void *pointers)
{
   if ((count != 3 && mode != GL_TRIANGLES) || vbuf_length + count > VERTEX_BUFFER_SIZE)
     vbo_draw();

   memcpy(&vbuf_data[vbuf_length], pointers, count * VERTEX_SIZE);
   vbuf_length += count;

   if (count == 3 || mode == GL_TRIANGLES)
     vbuf_primitive = GL_TRIANGLES;
   else
   {
     vbuf_primitive = mode;
     vbo_draw();
   }
}

void vbo_enable()
{
   if (vbuf_enabled)
      return;

   if (vbuf_use_vbo && !vbuf_vbo)
   {
      if (!vbuf_vbo)
         glGenBuffers(1, &vbuf_vbo);

      if (!vbuf_vbo)
      {
         log_cb(RETRO_LOG_ERROR, "Failed to create the VBO.");
         vbuf_use_vbo = false;
      }
   }

   if (vbuf_vbo)
   {
      glBindBuffer(GL_ARRAY_BUFFER, vbuf_vbo);

      glEnableVertexAttribArray(POSITION_ATTR);
      glEnableVertexAttribArray(COLOUR_ATTR);
      glEnableVertexAttribArray(TEXCOORD_0_ATTR);
      glEnableVertexAttribArray(TEXCOORD_1_ATTR);
      glEnableVertexAttribArray(FOG_ATTR);

      glVertexAttribPointer(POSITION_ATTR, 4, GL_FLOAT, false, VERTEX_SIZE, (void*)VERTEX_OFF(x));
      glVertexAttribPointer(COLOUR_ATTR, 4, GL_UNSIGNED_BYTE, true, VERTEX_SIZE, (void*)VERTEX_OFF(b));
      glVertexAttribPointer(TEXCOORD_0_ATTR, 2, GL_FLOAT, false, VERTEX_SIZE, (void*)VERTEX_OFF(coord[2]));
      glVertexAttribPointer(TEXCOORD_1_ATTR, 2, GL_FLOAT, false, VERTEX_SIZE, (void*)VERTEX_OFF(coord[0]));
      glVertexAttribPointer(FOG_ATTR, 1, GL_FLOAT, false, VERTEX_SIZE, (void*)VERTEX_OFF(f));

      glBindBuffer(GL_ARRAY_BUFFER, 0);
   }
   else
   {
      glEnableVertexAttribArray(POSITION_ATTR);
      glEnableVertexAttribArray(COLOUR_ATTR);
      glEnableVertexAttribArray(TEXCOORD_0_ATTR);
      glEnableVertexAttribArray(TEXCOORD_1_ATTR);
      glEnableVertexAttribArray(FOG_ATTR);

      glVertexAttribPointer(POSITION_ATTR, 4, GL_FLOAT, false, VERTEX_SIZE, &vbuf_data->x);
      glVertexAttribPointer(COLOUR_ATTR, 4, GL_UNSIGNED_BYTE, true, VERTEX_SIZE, &vbuf_data->b);
      glVertexAttribPointer(TEXCOORD_0_ATTR, 2, GL_FLOAT, false, VERTEX_SIZE, &vbuf_data->coord[2]);
      glVertexAttribPointer(TEXCOORD_1_ATTR, 2, GL_FLOAT, false, VERTEX_SIZE, &vbuf_data->coord[0]);
      glVertexAttribPointer(FOG_ATTR, 1, GL_FLOAT, false, VERTEX_SIZE, &vbuf_data->f);
   }

   vbuf_enabled = true;
}

void vbo_disable()
{
   vbo_draw();
   vbuf_enabled = false;
}

void grCullMode( int32_t mode )
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

void grDepthBufferFunction(GLenum func)
{
   glDepthFunc(func);
}

void grDepthMask(bool mask)
{
   glDepthMask(mask);
}

bool biasFound = false;
extern float polygonOffsetFactor;
extern float polygonOffsetUnits;

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

void grDepthBiasLevel( int32_t level )
{
   if (level)
   {
      glPolygonOffset(polygonOffsetFactor, (float)level * settings.depth_bias * 0.01f);
      glEnable(GL_POLYGON_OFFSET_FILL);
   }
   else
   {
      glPolygonOffset(0,0);
      glDisable(GL_POLYGON_OFFSET_FILL);
   }
}

#ifdef EMSCRIPTEN
#define buffer_struct struct draw_buffer
#define gl_offset(x) offsetof(buffer_struct, x)
#else
#define buffer_struct VERTEX
#define gl_offset(x) &v->x
#endif

void grDrawVertexArrayContiguous(uint32_t mode, uint32_t count, void *pointers)
{
   if(need_to_compile)
      compile_shader();

   vbo_enable();
   vbo_append(mode, count, pointers);

   /*
#ifdef EMSCRIPTEN
   unsigned i;
#endif
   VERTEX *v = (VERTEX*)pointers;

   if(need_to_compile)
      compile_shader();

#ifdef EMSCRIPTEN
   if (count > gli_vbo_size)
   {
      gli_vbo_size = count;
      gli_vbo = realloc(gli_vbo, sizeof(struct draw_buffer) * gli_vbo_size);
   }

   for (i = 0; i < count; i++)
   {
      memcpy(&gli_vbo[i], &v[i], sizeof(struct draw_buffer));
   }

   glBindBuffer(GL_ARRAY_BUFFER, glitch_vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(buffer_struct) * count, gli_vbo, GL_DYNAMIC_DRAW);
#endif

   glEnableVertexAttribArray(POSITION_ATTR);
   glVertexAttribPointer(POSITION_ATTR, 4, GL_FLOAT, false, sizeof(buffer_struct), gl_offset(x)); //Position

   glEnableVertexAttribArray(COLOUR_ATTR);
   glVertexAttribPointer(COLOUR_ATTR, 4, GL_UNSIGNED_BYTE, true, sizeof(buffer_struct), gl_offset(b)); //Colour

   glEnableVertexAttribArray(TEXCOORD_0_ATTR);
   glVertexAttribPointer(TEXCOORD_0_ATTR, 2, GL_FLOAT, false, sizeof(buffer_struct), gl_offset(coord[2])); //Tex0

   glEnableVertexAttribArray(TEXCOORD_1_ATTR);
   glVertexAttribPointer(TEXCOORD_1_ATTR, 2, GL_FLOAT, false, sizeof(buffer_struct), gl_offset(coord[0])); //Tex1

   glEnableVertexAttribArray(FOG_ATTR);
   glVertexAttribPointer(FOG_ATTR, 1, GL_FLOAT, false, sizeof(buffer_struct), gl_offset(f)); //Fog

   glDrawArrays(mode, 0, count);
#ifdef EMSCRIPTEN
   glBindBuffer(GL_ARRAY_BUFFER, 0);
#endif
*/
}

void init_geometry()
{
   vbo_init();
}

void free_geometry()
{
   vbo_free();
}
