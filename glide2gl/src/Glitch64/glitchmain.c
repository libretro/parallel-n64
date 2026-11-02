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
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "glide.h"
#include "glitchmain.h"
#include "../Glide64/rdp.h"
#include "../../libretro/libretro_private.h"

#include <gfx/gl_capabilities.h>

extern retro_environment_t environ_cb;

int width, height;
int bgra8888_support;
static int npot_support;
// ZIGGY
static GLuint default_texture;
int glsl_support = 1;
//Gonetz

extern uint16_t *glide64_frameBuffer;
static uint8_t  *buf;

static int isExtensionSupported(const char *extension)
{
   const char *str = (const char*)glGetString(GL_EXTENSIONS);
   if (str && strstr(str, extension))
      return 1;
   return 0;
}

uint32_t grSstWinOpen(void)
{
   bool ret;
   struct retro_variable var = { "parallel-n64-screensize", 0 };

   if (glide64_frameBuffer)
      grSstWinClose(0);

   ret = environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);

   if (ret && var.value)
   {
      if (sscanf(var.value ? var.value : "640x480", "%dx%d", &width, &height) != 2)
      {
         width = 640;
         height = 480;
      }
   }
   else
   {
      width = 640;
      height =480;
   }

   // ZIGGY
   // allocate static texture names
   // the initial value should be big enough to support the maximal resolution
   glGenTextures(1, &default_texture);
   glide64_frameBuffer = (uint16_t*)malloc(width * height * sizeof(uint16_t));
   buf = (uint8_t*)malloc(width * height * 4 * sizeof(uint8_t));
   glViewport(0, 0, width, height);

   packed_pixels_support = 0;
   npot_support          = 0;
   bgra8888_support      = 0;

   // we can assume that non-GLES has GL_EXT_packed_pixels
   // support -it's included since OpenGL 1.2
   if (isExtensionSupported("GL_EXT_packed_pixels") != 0)
      packed_pixels_support = 1;

   if (gl_check_capability(GL_CAPS_FULL_NPOT_SUPPORT))
   {
      printf("GL_ARB_texture_non_power_of_two supported.\n");
      npot_support = 1;
   }

   if (gl_check_capability(GL_CAPS_BGRA8888))
   {
      printf("GL_EXT_texture_format_BGRA8888 supported.\n");
      bgra8888_support = 1;
   }

   init_geometry();
   init_combiner();
   init_textures();

   return 1;
}

int32_t grSstWinClose(uint32_t context)
{
   if (glide64_frameBuffer)
      free(glide64_frameBuffer);

   if (buf)
      free(buf);

   glDeleteTextures(1, &default_texture);

   glide64_frameBuffer = NULL;
   buf         = NULL;

   free_geometry();
   free_combiners();
   free_textures();

   return FXTRUE;
}

// frame buffer

int32_t grLfbLock( int32_t type, int32_t buffer, int32_t writeMode,
          int32_t origin, int32_t pixelPipeline,
          GrLfbInfo_t *info )
{
   info->origin        = origin;
   info->strideInBytes = width * ((writeMode == GR_LFBWRITEMODE_888) ? 4 : 2);
   info->lfbPtr        = glide64_frameBuffer;
   info->writeMode     = writeMode;

   if (writeMode == GR_LFBWRITEMODE_565)
   {
      signed i, j;

      glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, buf);
      for (j=0; j < height; j++)
      {
         for (i=0; i < width; i++)
         {
            glide64_frameBuffer[(height-j-1)*width+i] =
               ((buf[j*width*4+i*4+0] >> 3) << 11) |
               ((buf[j*width*4+i*4+1] >> 2) <<  5) |
               (buf[j*width*4+i*4+2] >> 3);
         }
      }
   }

   return FXTRUE;
}

const uint8_t *grLfbReadRect(int32_t *x, int32_t *y, int32_t *w, int32_t *h, uint32_t *stride)
{
   int32_t x0 = *x < 0 ? 0 : *x;
   int32_t y0 = *y < 0 ? 0 : *y;
   int32_t x1 = *x + *w > width  ? width  : *x + *w;
   int32_t y1 = *y + *h > height ? height : *y + *h;

   if (x1 <= x0 || y1 <= y0)
      return NULL;
   *x = x0;
   *y = y0;
   *w = x1 - x0;
   *h = y1 - y0;
   *stride = (uint32_t)*w * 4;
   glReadPixels(x0, height - y1, *w, *h, GL_RGBA, GL_UNSIGNED_BYTE, buf);
   return buf;
}

int32_t
grLfbWriteRegion( int32_t dst_buffer,
      uint32_t dst_x, uint32_t dst_y,
      uint32_t src_format,
      uint32_t src_width, uint32_t src_height,
      int32_t pixelPipeline,
      int32_t src_stride, void *src_data )
{
   unsigned int i,j;
   uint16_t *frameBuffer = (uint16_t*)src_data;

   if(dst_buffer == GR_BUFFER_AUXBUFFER)
   {
      /* depth images are not drawn: the depth buffer is cleared */
      glEnable(GL_DEPTH_TEST);
      glDepthFunc(GL_ALWAYS);

      //glDrawBuffer(GL_BACK);
      glClear( GL_DEPTH_BUFFER_BIT );
      glDepthMask(1);
      //glDrawPixels(src_width, src_height, GL_DEPTH_COMPONENT, GL_FLOAT, buf);
   }
   else
   {
      /* An LFB write bypasses the pixel pipeline: the frame's clip window,
       * cull mode, depth test and blending do not apply to it. */
      const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
      const GLboolean cull    = glIsEnabled(GL_CULL_FACE);
      const GLboolean depth   = glIsEnabled(GL_DEPTH_TEST);
      const GLboolean blend   = glIsEnabled(GL_BLEND);
      int location;
      float data[16];
      const unsigned int half_stride = src_stride / 2;
      const float x0 = (float)dst_x, x1 = (float)(dst_x + src_width);
      const float y0 = (float)dst_y, y1 = (float)(dst_y + src_height);
      const float u1 = (float)src_width, v1 = (float)src_height;

      glActiveTexture(GL_TEXTURE0);

      /* src_format is GR_LFBWRITEMODE_555 */
      for (j=0; j<src_height; j++)
      {
         for (i=0; i<src_width; i++)
         {
            const unsigned int col = frameBuffer[j*half_stride+i];
            buf[j*src_width*4+i*4+0]=((col>>10)&0x1F)<<3;
            buf[j*src_width*4+i*4+1]=((col>>5)&0x1F)<<3;
            buf[j*src_width*4+i*4+2]=((col>>0)&0x1F)<<3;
            buf[j*src_width*4+i*4+3]=0xFF;
         }
      }

      /* default_texture is only a name until here: give it the image */
      glBindTexture(GL_TEXTURE_2D, default_texture);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, src_width, src_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

      set_copy_shader();
      /* the default program's other uniforms are never set: the shader
       * maps window pixels through vertexOffset, and texels through
       * textureSizes */
      location = glGetUniformLocation(program_object_default, "vertexOffset");
      glUniform3f(location, (float)width / 2.0f, (float)height / 2.0f, 1.0f);
      location = glGetUniformLocation(program_object_default, "textureSizes");
      glUniform4f(location, u1, v1, 1.0f, 1.0f);

      glDisable(GL_SCISSOR_TEST);
      glDisable(GL_CULL_FACE);
      glDisable(GL_DEPTH_TEST);
      glDisable(GL_BLEND);

      /* a strip: top left, bottom left, top right, bottom right */
      data[ 0] = x0; data[ 1] = y0; data[ 2] = 0.0f; data[ 3] = 0.0f;
      data[ 4] = x0; data[ 5] = y1; data[ 6] = 0.0f; data[ 7] = v1;
      data[ 8] = x1; data[ 9] = y0; data[10] = u1;   data[11] = 0.0f;
      data[12] = x1; data[13] = y1; data[14] = u1;   data[15] = v1;

      glDisableVertexAttribArray(COLOUR_ATTR);
      glDisableVertexAttribArray(TEXCOORD_1_ATTR);
      glDisableVertexAttribArray(FOG_ATTR);

      glVertexAttribPointer(POSITION_ATTR,2,GL_FLOAT,false,4 * sizeof(float), &data[0]); //Position
      glVertexAttribPointer(TEXCOORD_0_ATTR,2,GL_FLOAT,false,4 * sizeof(float), &data[2]); //Tex
      glEnableVertexAttribArray(POSITION_ATTR);
      glEnableVertexAttribArray(TEXCOORD_0_ATTR);

      glDrawArrays(GL_TRIANGLE_STRIP,0,4);

      glEnableVertexAttribArray(COLOUR_ATTR);
      glEnableVertexAttribArray(TEXCOORD_1_ATTR);
      glEnableVertexAttribArray(FOG_ATTR);

      compile_shader();

      if (scissor)
         glEnable(GL_SCISSOR_TEST);
      if (cull)
         glEnable(GL_CULL_FACE);
      if (depth)
         glEnable(GL_DEPTH_TEST);
      if (blend)
         glEnable(GL_BLEND);
   }

   return FXTRUE;
}

void grBufferSwap(uint32_t swap_interval)
{
   retro_return(true);
}

void grClipWindow(uint32_t minx, uint32_t miny, uint32_t maxx, uint32_t maxy)
{
   glScissor(minx, height - maxy, maxx - minx, maxy - miny);
   glEnable(GL_SCISSOR_TEST);
}

void grBufferClear(uint32_t color, uint32_t alpha, uint32_t depth)
{
   glClearColor(((color >> 24) & 0xFF) / 255.0f,
         ((color >> 16) & 0xFF) / 255.0f,
         (color         & 0xFF) / 255.0f,
         alpha / 255.0f);
   glClearDepth(depth / 65535.0f);
   glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void grColorMask(bool rgb, bool a)
{
   glColorMask(rgb, rgb, rgb, a);
}
