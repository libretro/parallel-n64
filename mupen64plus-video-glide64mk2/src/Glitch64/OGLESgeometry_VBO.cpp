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

#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif // _WIN32
#include "glide.h"
#include "main.h"
#include "../Glide64/winlnxdefs.h"
#include "../Glide64/rdp.h"

#define Z_MAX (65536.0f)
#define VERTEX_SIZE sizeof(VERTEX) //Size of vertex struct

int xy_off;
int xy_en;
int z_en;
int z_off;
int q_off;
int q_en;
int pargb_off;
int pargb_en;
int st0_off;
int st0_en;
int st1_off;
int st1_en;
int fog_ext_off;
int fog_ext_en;

int w_buffer_mode;
int inverted_culling;
int culling_mode;

#define VERTEX_BUFFER_SIZE 1500 //Max amount of vertices to buffer, this seems large enough.
static VERTEX vertex_buffer[VERTEX_BUFFER_SIZE];
static int vertex_buffer_count = 0;
static GLenum vertex_draw_mode;
static bool vertex_buffer_enabled = false;

void vbo_init()
{
  
}

void vbo_free()
{
}

void vbo_bind()
{
}

void vbo_unbind()
{
}

void vbo_buffer_data(void *data, size_t data_sizeof)
{
}

void vbo_draw()
{
  if(vertex_buffer_count)
  {
    glDrawArrays(vertex_draw_mode,0,vertex_buffer_count);
    vertex_buffer_count = 0;
  }
}

//Buffer vertices instead of glDrawArrays(...)
static void vbo_buffer(GLenum mode,GLint first,GLsizei count,void* pointers)
{
  if((count != 3 && mode != GL_TRIANGLES) || vertex_buffer_count + count > VERTEX_BUFFER_SIZE)
  {
    vbo_draw();
  }

  memcpy(&vertex_buffer[vertex_buffer_count],pointers,count * VERTEX_SIZE);
  vertex_buffer_count += count;

  if(count == 3 || mode == GL_TRIANGLES)
  {
    vertex_draw_mode = GL_TRIANGLES;
  }
  else
  {
    vertex_draw_mode = mode;
    vbo_draw(); //Triangle fans and strips can't be joined as easily, just draw them straight away.
  }


}

void vbo_enable()
{
  if(vertex_buffer_enabled)
    return;

  vertex_buffer_enabled = true;
  glEnableVertexAttribArray(POSITION_ATTR);
  glVertexAttribPointer(POSITION_ATTR, 4, GL_FLOAT, false, VERTEX_SIZE, &vertex_buffer[0].x); //Position

  glEnableVertexAttribArray(COLOUR_ATTR);
  glVertexAttribPointer(COLOUR_ATTR, 4, GL_UNSIGNED_BYTE, true, VERTEX_SIZE, &vertex_buffer[0].b); //Colour

  glEnableVertexAttribArray(TEXCOORD_0_ATTR);
  glVertexAttribPointer(TEXCOORD_0_ATTR, 2, GL_FLOAT, false, VERTEX_SIZE, &vertex_buffer[0].coord[2]); //Tex0

  glEnableVertexAttribArray(TEXCOORD_1_ATTR);
  glVertexAttribPointer(TEXCOORD_1_ATTR, 2, GL_FLOAT, false, VERTEX_SIZE, &vertex_buffer[0].coord[0]); //Tex1

  glEnableVertexAttribArray(FOG_ATTR);
  glVertexAttribPointer(FOG_ATTR, 1, GL_FLOAT, false, VERTEX_SIZE, &vertex_buffer[0].f); //Fog
}

void vbo_disable()
{
  vbo_draw();
  vertex_buffer_enabled = false;
}


void init_geometry()
{
  xy_en = q_en = pargb_en = st0_en = st1_en = z_en = 0;
  w_buffer_mode = 0;
  inverted_culling = 0;

  glDisable(GL_CULL_FACE);
  glDisable(GL_DEPTH_TEST);

  vbo_init();
}

float biasFactor = 0;

// draw

FX_ENTRY void FX_CALL
grDrawTriangle( const void *a, const void *b, const void *c )
{
  LOG("grDrawTriangle()\r\n\t");

  if(nvidia_viewport_hack && !render_to_texture)
  {
    glViewport(0, viewport_offset, viewport_width, viewport_height);
    nvidia_viewport_hack = 0;
  }

  reloadTexture();

  if(need_to_compile) compile_shader();

  if(vertex_buffer_count + 3 > VERTEX_BUFFER_SIZE)
  {
    vbo_draw();
  }
  vertex_draw_mode = GL_TRIANGLES;
  memcpy(&vertex_buffer[vertex_buffer_count],a,VERTEX_SIZE);
  memcpy(&vertex_buffer[vertex_buffer_count+1],b,VERTEX_SIZE);
  memcpy(&vertex_buffer[vertex_buffer_count+2],c,VERTEX_SIZE);
  vertex_buffer_count += 3;
}

FX_ENTRY void FX_CALL
grDrawPoint( const void *pt )
{
   /* TODO? */
}

FX_ENTRY void FX_CALL
grDrawLine( const void *a, const void *b )
{
   /* TODO? */
}

FX_ENTRY void FX_CALL
grDrawVertexArray(FxU32 mode, FxU32 Count, void *pointers2)
{
  void **pointers = (void**)pointers2;
  LOG("grDrawVertexArray(%d,%d)\r\n", mode, Count);

  if(nvidia_viewport_hack && !render_to_texture)
  {
    glViewport(0, viewport_offset, viewport_width, viewport_height);
    nvidia_viewport_hack = 0;
  }

  reloadTexture();

  if(need_to_compile) compile_shader();

  if(mode != GR_TRIANGLE_FAN)
  {
    display_warning("grDrawVertexArray : unknown mode : %x", mode);
  }

  vbo_enable();
  vbo_buffer(GL_TRIANGLE_FAN,0,Count,pointers[0]);
}

FX_ENTRY void FX_CALL
grDrawVertexArrayContiguous(FxU32 mode, FxU32 Count, void *pointers, FxU32 stride)
{
  LOG("grDrawVertexArrayContiguous(%d,%d,%d)\r\n", mode, Count, stride);

  if(nvidia_viewport_hack && !render_to_texture)
  {
    glViewport(0, viewport_offset, viewport_width, viewport_height);
    nvidia_viewport_hack = 0;
  }

  if(stride != 156)
  {
	  LOGINFO("Incompatible stride\n");
  }

  reloadTexture();

  if(need_to_compile) compile_shader();

  vbo_enable();

  switch(mode)
  {
  case GR_TRIANGLE_STRIP:
    vbo_buffer(GL_TRIANGLE_STRIP,0,Count,pointers);
    break;
  case GR_TRIANGLE_FAN:
    vbo_buffer(GL_TRIANGLE_FAN,0,Count,pointers);
    break;
  default:
    display_warning("grDrawVertexArrayContiguous : unknown mode : %x", mode);
  }
}
