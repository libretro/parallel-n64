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
#include "g3ext.h"
#include "main.h"
#include "../../libretro/SDL.h"

#define TEXTURE_UNITS 4

extern retro_environment_t environ_cb;

int width, height;
int blend_func_separate_support;
int bgra8888_support;
int npot_support;
int fog_coord_support;
int texture_unit;
int buffer_cleared;
// ZIGGY
// to allocate a new static texture name, take the value (free_texture++)
int free_texture;
int default_texture;
int current_texture;
int depth_texture, color_texture;
int glsl_support = 1;
int lfb_color_fmt;
float invtex[2];
//Gonetz

uint16_t *frameBuffer;
uint16_t *depthBuffer;
uint8_t  *buf;

FX_ENTRY void FX_CALL
grSstOrigin(GrOriginLocation_t  origin)
{
   LOG("grSstOrigin(%d)\r\n", origin);
   if (origin != GR_ORIGIN_UPPER_LEFT)
      DISPLAY_WARNING("grSstOrigin : %x", origin);
}

FX_ENTRY void FX_CALL
grClipWindow( FxU32 minx, FxU32 miny, FxU32 maxx, FxU32 maxy )
{
   LOG("grClipWindow(%d,%d,%d,%d)\r\n", minx, miny, maxx, maxy);
   GLint y = height - maxy;
   glScissor(minx, y, maxx - minx, maxy - miny);
   glEnable(GL_SCISSOR_TEST);
}

FX_ENTRY void FX_CALL
grGlideInit( void )
{
   LOG("grGlideInit()\r\n");
}

FX_ENTRY void FX_CALL
grSstSelect( int which_sst )
{
   LOG("grSstSelect(%d)\r\n", which_sst);
}

int isExtensionSupported(const char *extension)
{
   const char *str = (const char*)glGetString(GL_EXTENSIONS);
   if (str && strstr(str, extension))
      return 1;
   return 0;
}

#define GrPixelFormat_t int

FX_ENTRY GrContext_t FX_CALL
grSstWinOpenExt(
                GrScreenResolution_t screen_resolution,
                GrScreenRefresh_t    refresh_rate,
                GrColorFormat_t      color_format,
                GrOriginLocation_t   origin_location,
                GrPixelFormat_t      pixelformat,
                int                  nColBuffers,
                int                  nAuxBuffers)
{
   LOG("grSstWinOpenExt(%d, %d, %d, %d, %d %d)\r\n", screen_resolution, refresh_rate, color_format, origin_location, nColBuffers, nAuxBuffers);
   return grSstWinOpen(screen_resolution, refresh_rate, color_format, origin_location, nColBuffers, nAuxBuffers);
}

void FindBestDepthBias();

FX_ENTRY GrContext_t FX_CALL
grSstWinOpen(
             GrScreenResolution_t screen_resolution,
             GrScreenRefresh_t    refresh_rate,
             GrColorFormat_t      color_format,
             GrOriginLocation_t   origin_location,
             int                  nColBuffers,
             int                  nAuxBuffers)
{
   bool ret;
   struct retro_variable var = { "mupen64-screensize", 0 };

   LOG("grSstWinOpen(%d, %d, %d, %d, %d %d)\r\n", screen_resolution&~0x80000000, refresh_rate, color_format, origin_location, nColBuffers, nAuxBuffers);

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
   free_texture = 32 * width * height;
   default_texture = free_texture++;
   color_texture = free_texture++;
   depth_texture = free_texture++;
   frameBuffer = (uint16_t*)malloc(width * height * sizeof(uint16_t));
   depthBuffer = (uint16_t*)malloc(width * height * sizeof(uint16_t));
   buf = (uint8_t*)malloc(width * height * 4 * sizeof(uint8_t));
   glViewport(0, 0, width, height);

   lfb_color_fmt = color_format;
   if (origin_location != GR_ORIGIN_UPPER_LEFT) DISPLAY_WARNING("origin must be in upper left corner");
   if (nColBuffers != 2) DISPLAY_WARNING("number of color buffer is not 2");
   if (nAuxBuffers != 1) DISPLAY_WARNING("number of auxiliary buffer is not 1");

   if (isExtensionSupported("GL_ARB_texture_env_combine") == 0 &&
         isExtensionSupported("GL_EXT_texture_env_combine") == 0)
      DISPLAY_WARNING("Your video card doesn't support GL_ARB_texture_env_combine extension");
   if (isExtensionSupported("GL_ARB_multitexture") == 0)
      DISPLAY_WARNING("Your video card doesn't support GL_ARB_multitexture extension");
   if (isExtensionSupported("GL_ARB_texture_mirrored_repeat") == 0)
      DISPLAY_WARNING("Your video card doesn't support GL_ARB_texture_mirrored_repeat extension");

   blend_func_separate_support = 1;
   packed_pixels_support = 0;
   
   if (isExtensionSupported("GL_EXT_blend_func_separate") == 0)
   {
      DISPLAY_WARNING("GL_EXT_blend_func_separate not supported.\n");
      blend_func_separate_support = 0;
   }
   else
   {
      printf("GL_EXT_blend_func_separate supported.\n");
      blend_func_separate_support = 1;
   }

   // we can assume that non-GLES has GL_EXT_packed_pixels
   // support -it's included since OpenGL 1.2
#ifdef HAVE_OPENGLES2
   if (isExtensionSupported("GL_EXT_packed_pixels") != 0)
#endif
      packed_pixels_support = 1;

   if (isExtensionSupported("GL_ARB_texture_non_power_of_two") == 0)
   {
      DISPLAY_WARNING("GL_ARB_texture_non_power_of_two supported.\n");
      npot_support = 0;
   }
   else
   {
      printf("GL_ARB_texture_non_power_of_two supported.\n");
      npot_support = 1;
   }

   if (isExtensionSupported("GL_EXT_fog_coord") == 0)
   {
      DISPLAY_WARNING("GL_EXT_fog_coord not supported.\n");
      fog_coord_support = 0;
   }
   else
   {
      printf("GL_EXT_fog_coord supported.\n");
      fog_coord_support = 1;
   }

   if (isExtensionSupported("GL_ARB_shading_language_100") &&
         isExtensionSupported("GL_ARB_shader_objects") &&
         isExtensionSupported("GL_ARB_fragment_shader") &&
         isExtensionSupported("GL_ARB_vertex_shader"))
   {}

#ifdef HAVE_OPENGLES2
   if (isExtensionSupported("GL_EXT_texture_format_BGRA8888"))
   {
      printf("GL_EXT_texture_format_BGRA8888 supported.\n");
      bgra8888_support = 1;
   }
   else
   {
      DISPLAY_WARNING("GL_EXT_texture_format_BGRA8888 not supported.\n");
      bgra8888_support = 0;
   }
#endif

   glViewport(0, 0, width, height);

   texture_unit = GL_TEXTURE0;

   FindBestDepthBias();

   init_geometry();
   init_textures();
   init_combiner();

   return 1;
}

FX_ENTRY void FX_CALL
grGlideShutdown( void )
{
   LOG("grGlideShutdown\r\n");
}

FX_ENTRY FxBool FX_CALL
grSstWinClose( GrContext_t context )
{
   int i;
   LOG("grSstWinClose(%d)\r\n", context);

   for (i=0; i<2; i++)
      invtex[i] = 0;

   if (frameBuffer)
      free(frameBuffer);
   if (depthBuffer)
      free(depthBuffer);
   if (buf)
      free(buf);
   frameBuffer = NULL;
   depthBuffer = NULL;
   buf = NULL;

   free_combiners();
   free_textures();

   return FXTRUE;
}

FX_ENTRY FxU32 FX_CALL
grGet( FxU32 pname, FxU32 plength, FxI32 *params )
{
   LOG("grGet(%d,%d)\r\n", pname, plength);
   switch(pname)
   {
      case GR_MAX_TEXTURE_SIZE:
         if (plength < 4 || params == NULL) return 0;
         params[0] = 2048;
         return 4;
         break;
      case GR_NUM_TMU:
         if (plength < 4 || params == NULL) return 0;
         params[0] = 2;
         return 4;
         break;
      case GR_NUM_BOARDS:
      case GR_NUM_FB:
      case GR_REVISION_FB:
      case GR_REVISION_TMU:
         if (plength < 4 || params == NULL) return 0;
         params[0] = 1;
         return 4;
         break;
      case GR_MEMORY_FB:
         if (plength < 4 || params == NULL) return 0;
         params[0] = 16*1024*1024;
         return 4;
         break;
      case GR_MEMORY_TMU:
         if (plength < 4 || params == NULL) return 0;
         params[0] = 16*1024*1024;
         return 4;
         break;
      case GR_MEMORY_UMA:
         if (plength < 4 || params == NULL) return 0;
         params[0] = 16*1024*1024 * TEXTURE_UNITS;
         return 4;
         break;
      case GR_BITS_RGBA:
         if (plength < 16 || params == NULL) return 0;
         params[0] = 8;
         params[1] = 8;
         params[2] = 8;
         params[3] = 8;
         return 16;
         break;
      case GR_BITS_DEPTH:
         if (plength < 4 || params == NULL) return 0;
         params[0] = 16;
         return 4;
         break;
      case GR_BITS_GAMMA:
         if (plength < 4 || params == NULL) return 0;
         params[0] = 8;
         return 4;
         break;
      case GR_GAMMA_TABLE_ENTRIES:
         if (plength < 4 || params == NULL) return 0;
         params[0] = 256;
         return 4;
         break;
      case GR_FOG_TABLE_ENTRIES:
         if (plength < 4 || params == NULL) return 0;
         params[0] = 64;
         return 4;
         break;
      case GR_WDEPTH_MIN_MAX:
         if (plength < 8 || params == NULL) return 0;
         params[0] = 0;
         params[1] = 65528;
         return 8;
         break;
      case GR_ZDEPTH_MIN_MAX:
         if (plength < 8 || params == NULL) return 0;
         params[0] = 0;
         params[1] = 65535;
         return 8;
         break;
      case GR_LFB_PIXEL_PIPE:
         if (plength < 4 || params == NULL) return 0;
         params[0] = FXFALSE;
         return 4;
         break;
      case GR_MAX_TEXTURE_ASPECT_RATIO:
         if (plength < 4 || params == NULL) return 0;
         params[0] = 3;
         return 4;
         break;
      case GR_NON_POWER_OF_TWO_TEXTURES:
         if (plength < 4 || params == NULL) return 0;
         params[0] = FXFALSE;
         return 4;
         break;
      case GR_TEXTURE_ALIGN:
         if (plength < 4 || params == NULL) return 0;
         params[0] = 0;
         return 4;
         break;
      default:
         DISPLAY_WARNING("unknown pname in grGet : %x", pname);
   }
   return 0;
}

FX_ENTRY const char * FX_CALL
grGetString( FxU32 pname )
{
   LOG("grGetString(%d)\r\n", pname);
   switch(pname)
   {
      case GR_EXTENSION:
         {
            static char extension[] = "CHROMARANGE TEXCHROMA TEXMIRROR PALETTE6666 FOGCOORD EVOODOO TEXTUREBUFFER TEXUMA TEXFMT COMBINE GETGAMMA";
            return extension;
         }
         break;
      case GR_HARDWARE:
         {
            static char hardware[] = "Voodoo5 (tm)";
            return hardware;
         }
         break;
      case GR_VENDOR:
         {
            static char vendor[] = "3Dfx Interactive";
            return vendor;
         }
         break;
      case GR_RENDERER:
         {
            static char renderer[] = "Glide";
            return renderer;
         }
         break;
      case GR_VERSION:
         {
            static char version[] = "3.0";
            return version;
         }
         break;
      default:
         DISPLAY_WARNING("unknown grGetString selector : %x", pname);
   }
   return NULL;
}

static void render_rectangle(int texture_number,
                             int dst_x, int dst_y,
                             int src_width, int src_height,
                             int tex_width, int tex_height, int invert)
{
   int vertexOffset_location, textureSizes_location;
   static float data[16];

   LOGINFO("render_rectangle(%d,%d,%d,%d,%d,%d,%d,%d)",texture_number,dst_x,dst_y,src_width,src_height,tex_width,tex_height,invert);
   data[0]   =     ((int)dst_x);                             //X 0
   data[1]   =     invert*-((int)dst_y);                     //Y 0 
   data[2]   =     0.0f;                                     //U 0 
   data[3]   =     0.0f;                                     //V 0
   data[4]   =     ((int)dst_x);                             //X 1
   data[5]   =     invert*-((int)dst_y + (int)src_height);   //Y 1
   data[6]   =     0.0f;                                     //U 1
   data[7]   =     (float)src_height;                        //V 1
   data[8]   =     ((int)dst_x + (int)src_width);
   data[9]  =     invert*-((int)dst_y + (int)src_height);
   data[10]  =     (float)src_width;
   data[11]  =     (float)src_height;
   data[12]  =     ((int)dst_x);
   data[13]  =     invert*-((int)dst_y);
   data[14]  =     0.0f;
   data[15]  =     0.0f;

   glDisableVertexAttribArray(COLOUR_ATTR);
   glDisableVertexAttribArray(TEXCOORD_1_ATTR);
   glDisableVertexAttribArray(FOG_ATTR);

   glVertexAttribPointer(POSITION_ATTR,2,GL_FLOAT,false,4 * sizeof(float),data); //Position
   glVertexAttribPointer(TEXCOORD_0_ATTR,2,GL_FLOAT,false,4 * sizeof(float),&data[2]); //Tex

   glEnableVertexAttribArray(COLOUR_ATTR);
   glEnableVertexAttribArray(TEXCOORD_1_ATTR);
   glEnableVertexAttribArray(FOG_ATTR);


   disable_textureSizes();

   glDrawArrays(GL_TRIANGLE_STRIP,0,4);

   compile_shader();

   glEnable(GL_DEPTH_TEST);
   glEnable(GL_BLEND);
}

FX_ENTRY void FX_CALL
grBufferClear( GrColor_t color, GrAlpha_t alpha, FxU32 depth )
{
   LOG("grBufferClear(%d,%d,%d)\r\n", color, alpha, depth);
   glClearColor(0, 0, 0, 0);
   glClearDepth(depth / 65535.0f);
   glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

   // ZIGGY TODO check that color mask is on
   buffer_cleared = 1;
}

FX_ENTRY void FX_CALL
grBufferSwap( FxU32 swap_interval )
{
   retro_return(true);
}

// frame buffer

FX_ENTRY FxBool FX_CALL
grLfbLock( GrLock_t type, GrBuffer_t buffer, GrLfbWriteMode_t writeMode,
          GrOriginLocation_t origin, FxBool pixelPipeline,
          GrLfbInfo_t *info )
{
   LOG("grLfbLock(%d,%d,%d,%d,%d)\r\n", type, buffer, writeMode, origin, pixelPipeline);

   info->origin = origin;
   info->strideInBytes = width * ((writeMode == GR_LFBWRITEMODE_888) ? 4 : 2);
   info->lfbPtr = frameBuffer;
   info->writeMode = writeMode;

   if (writeMode == GR_LFBWRITEMODE_565)
   {
      unsigned i,j;
      glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, buf);

      for (j=0; j < height; j++)
      {
         for (i=0; i < width; i++)
         {
            frameBuffer[(height-j-1)*width+i] =
               ((buf[j*width*4+i*4+0] >> 3) << 11) |
               ((buf[j*width*4+i*4+1] >> 2) <<  5) |
               (buf[j*width*4+i*4+2] >> 3);
         }
      }
   }

   return FXTRUE;
}

FX_ENTRY FxBool FX_CALL
grLfbUnlock( GrLock_t type, GrBuffer_t buffer )
{
   return FXTRUE;
}

FX_ENTRY FxBool FX_CALL
grLfbReadRegion( GrBuffer_t src_buffer,
                FxU32 src_x, FxU32 src_y,
                FxU32 src_width, FxU32 src_height,
                FxU32 dst_stride, void *dst_data )
{
   unsigned int i,j;
   uint16_t *depthBuffer = (uint16_t*)dst_data;
   LOG("grLfbReadRegion(%d,%d,%d,%d,%d,%d)\r\n", src_buffer, src_x, src_y, src_width, src_height, dst_stride);

   if(src_buffer == GR_BUFFER_AUXBUFFER)
   {
      glReadPixels(src_x, height-src_y-src_height, src_width, src_height, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT, depthBuffer);

      for (j = 0;j < src_height; j++)
         for (i = 0; i < src_width; i++)
            depthBuffer[j*(dst_stride/2)+i] = ((uint16_t*)buf)[(src_height-j-1)*src_width*4+i*4];
   }
   else
   {
      glReadPixels(src_x, height-src_y-src_height, src_width, src_height, GL_RGBA, GL_UNSIGNED_BYTE, buf);

      for (j=0; j<src_height; j++)
      {
         for (i=0; i<src_width; i++)
         {
            frameBuffer[j*(dst_stride/2)+i] =
               ((buf[(src_height-j-1)*src_width*4+i*4+0] >> 3) << 11) |
               ((buf[(src_height-j-1)*src_width*4+i*4+1] >> 2) <<  5) |
               (buf[(src_height-j-1)*src_width*4+i*4+2] >> 3);
         }
      }
   }

   return FXTRUE;
}

FX_ENTRY FxBool FX_CALL
grLfbWriteRegion( GrBuffer_t dst_buffer,
                 FxU32 dst_x, FxU32 dst_y,
                 GrLfbSrcFmt_t src_format,
                 FxU32 src_width, FxU32 src_height,
                 FxBool pixelPipeline,
                 FxI32 src_stride, void *src_data )
{
   unsigned int i,j;
   uint16_t *frameBuffer = (uint16_t*)src_data;
   int texture_number;
   LOG("grLfbWriteRegion(%d,%d,%d,%d,%d,%d,%d,%d)\r\n",dst_buffer, dst_x, dst_y, src_format, src_width, src_height, pixelPipeline, src_stride);

   if(dst_buffer == GR_BUFFER_AUXBUFFER)
   {
      for (j=0; j<src_height; j++)
         for (i=0; i<src_width; i++)
            buf[j * src_width+i] = (frameBuffer[(src_height-j-1)*(src_stride/2)+i]/(65536.0f*(2.0f/zscale)))+1-zscale/2.0f;

      glEnable(GL_DEPTH_TEST);
      glDepthFunc(GL_ALWAYS);

      //glDrawBuffer(GL_BACK);
      glClear( GL_DEPTH_BUFFER_BIT );
      glDepthMask(1);
      //glDrawPixels(src_width, src_height, GL_DEPTH_COMPONENT, GL_FLOAT, buf);
   }
   else
   {
      const unsigned int half_stride = src_stride / 2;
      texture_number = GL_TEXTURE0;
      glActiveTexture(texture_number);

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

      glBindTexture(GL_TEXTURE_2D, default_texture);
      glTexSubImage2D(GL_TEXTURE_2D, 0, 4, src_width, src_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf);

      set_copy_shader();

      glDisable(GL_DEPTH_TEST);
      glDisable(GL_BLEND);
      render_rectangle(texture_number,
            dst_x, dst_y,
            src_width,  src_height,
            src_width,  src_height, +1);

   }
   return FXTRUE;
}

void check_gl_error(const char *stmt, const char *fname, int line)
{
   GLenum error = glGetError();
   if (error != GL_NO_ERROR && log_cb)
      log_cb(RETRO_LOG_INFO, "OpenGL error %08x, at %s:%i - for %s\n", error, fname, line, stmt);
}
