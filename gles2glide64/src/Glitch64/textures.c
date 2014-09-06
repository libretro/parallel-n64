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

#ifdef _WIN32
#include <windows.h>
#else // _WIN32
#include <stdlib.h>
#endif // _WIN32
#include <stdint.h>
#include "glide.h"
#include "main.h"

/* Napalm extensions to GrTextureFormat_t */
#define GR_TEXFMT_ARGB_8888               0x12
#define GR_TEXFMT_YUYV_422                0x13
#define GR_TEXFMT_UYVY_422                0x14
#define GR_TEXFMT_AYUV_444                0x15
#define GR_TEXTFMT_RGB_888                0xFF

int TMU_SIZE = 8*2048*2048;
static uint8_t* texture = NULL;
int packed_pixels_support = -1;

#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif

int tex0_width, tex0_height, tex1_width, tex1_height;
float lambda;

static int min_filter0, mag_filter0, wrap_s0, wrap_t0;
static int min_filter1, mag_filter1, wrap_s1, wrap_t1;

unsigned char *filter(unsigned char *source, int width, int height, int *width2, int *height2);

typedef struct _texlist
{
   unsigned int id;
   struct _texlist *next;
} texlist;

static int nbTex = 0;
static texlist *list = NULL;

void remove_tex(unsigned int idmin, unsigned int idmax)
{
   unsigned int *t;
   int n = 0;
   texlist *aux = list;
   int sz = nbTex;
   if (aux == NULL)
      return;
   t = (unsigned int*)malloc(sz * sizeof(int));
   while (aux && aux->id >= idmin && aux->id < idmax)
   {
      if (n >= sz)
         t = (unsigned int *) realloc(t, ++sz*sizeof(int));
      t[n++] = aux->id;
      aux = aux->next;
      free(list);
      list = aux;
      nbTex--;
   }
   while (aux && aux->next)
   {
      if (aux->next->id >= idmin && aux->next->id < idmax)
      {
         texlist *aux2 = aux->next->next;
         if (n >= sz)
            t = (unsigned int *) realloc(t, ++sz*sizeof(int));
         t[n++] = aux->next->id;
         free(aux->next);
         aux->next = aux2;
         nbTex--;
      }
      aux = aux->next;
   }
   glDeleteTextures(n, t);
   free(t);
   TEXLOG("RMVTEX nbtex is now %d (%06x - %06x)\n", nbTex, idmin, idmax);
}


void add_tex(unsigned int id)
{
  texlist *aux = list;
  texlist *aux2;
  if (list == NULL || id < list->id)
  {
    nbTex++;
    list = (texlist*)malloc(sizeof(texlist));
    list->next = aux;
    list->id = id;
    goto addtex_log;
  }
  while (aux->next && aux->next->id < id) aux = aux->next;
  // ZIGGY added this test so that add_tex now accept re-adding an existing texture
  if (aux->next && aux->next->id == id)
     return;
  nbTex++;
  aux2 = aux->next;
  aux->next = (texlist*)malloc(sizeof(texlist));
  aux->next->id = id;
  aux->next->next = aux2;
addtex_log:
  TEXLOG("ADDTEX nbtex is now %d (%06x)\n", nbTex, id);
}

void init_textures(void)
{
  tex0_width = tex0_height = tex1_width = tex1_height = 2;

  list = NULL;
  nbTex = 0;

  if (!texture)
     texture = (uint8_t*)malloc(2048*2048*4);
}

void free_textures(void)
{
  remove_tex(0x00000000, 0xFFFFFFFF);
  if (texture)
    free(texture);
  texture = NULL;
}

FX_ENTRY FxU32 FX_CALL
grTexMaxAddress( GrChipID_t tmu )
{
  LOG("grTexMaxAddress(%d)\r\n", tmu);
  return TMU_SIZE*2 - 1;
}

FX_ENTRY FxU32 FX_CALL
grTexTextureMemRequired( FxU32     evenOdd,
                        GrTexInfo *info   )
{
   int width, height;
   LOG("grTextureMemRequired(%d)\r\n", evenOdd);
#ifndef NDEBUG
   if (info->largeLodLog2 != info->smallLodLog2) DISPLAY_WARNING("grTexTextureMemRequired : loading more than one LOD");
#endif

   if (info->aspectRatioLog2 < 0)
   {
      height = 1 << info->largeLodLog2;
      width = height >> -info->aspectRatioLog2;
   }
   else
   {
      width = 1 << info->largeLodLog2;
      height = width >> info->aspectRatioLog2;
   }

   switch(info->format)
   {
      case GR_TEXFMT_ALPHA_8:
      case GR_TEXFMT_INTENSITY_8: // I8 support - H.Morii
      case GR_TEXFMT_ALPHA_INTENSITY_44:
         return width*height;
         break;
      case GR_TEXFMT_ARGB_1555:
      case GR_TEXFMT_ARGB_4444:
      case GR_TEXFMT_ALPHA_INTENSITY_88:
      case GR_TEXFMT_RGB_565:
         return (width*height)<<1;
         break;
      case GR_TEXFMT_ARGB_8888:
         return (width*height)<<2;
         break;
      default:
         DISPLAY_WARNING("grTexTextureMemRequired : unknown texture format: %x", info->format);
   }
   return 0;
}

FX_ENTRY FxU32 FX_CALL
grTexCalcMemRequired(
                     GrLOD_t lodmin, GrLOD_t lodmax,
                     GrAspectRatio_t aspect, GrTextureFormat_t fmt)
{
   int width, height;
   LOG("grTexCalcMemRequired(%d, %d, %d, %d)\r\n", lodmin, lodmax, aspect, fmt);
   if (lodmax != lodmin) DISPLAY_WARNING("grTexCalcMemRequired : loading more than one LOD");

   if (aspect < 0)
   {
      height = 1 << lodmax;
      width = height >> -aspect;
   }
   else
   {
      width = 1 << lodmax;
      height = width >> aspect;
   }

   switch(fmt)
   {
      case GR_TEXFMT_ALPHA_8:
      case GR_TEXFMT_INTENSITY_8: // I8 support - H.Morii
      case GR_TEXFMT_ALPHA_INTENSITY_44:
         return width*height;
         break;
      case GR_TEXFMT_ARGB_1555:
      case GR_TEXFMT_ARGB_4444:
      case GR_TEXFMT_ALPHA_INTENSITY_88:
      case GR_TEXFMT_RGB_565:
         return (width*height)<<1;
         break;
      case GR_TEXFMT_ARGB_8888:
         return (width*height)<<2;
         break;
      default:
         DISPLAY_WARNING("grTexTextureMemRequired : unknown texture format: %x", fmt);
   }
   return 0;
}

int grTexFormatSize(int fmt)
{
   int factor = -1;
   switch(fmt)
   {
      case GR_TEXFMT_ALPHA_8:
      case GR_TEXFMT_INTENSITY_8: // I8 support - H.Morii
         factor = 1;
         break;
      case GR_TEXFMT_ALPHA_INTENSITY_44:
         factor = 1;
         break;
      case GR_TEXFMT_RGB_565:
         factor = 2;
         break;
      case GR_TEXFMT_ARGB_1555:
         factor = 2;
         break;
      case GR_TEXFMT_ALPHA_INTENSITY_88:
         factor = 2;
         break;
      case GR_TEXFMT_ARGB_4444:
         factor = 2;
         break;
      case GR_TEXFMT_ARGB_8888:
         factor = 4;
         break;
      default:
         DISPLAY_WARNING("grTexFormatSize : unknown texture format: %x", fmt);
   }
   return factor;
}

#ifndef HAVE_OPENGLES2
static int grTexFormat2GLPackedFmt(GrTexInfo *info, int fmt, int * gltexfmt, int * glpixfmt, int * glpackfmt)
{
   int i, j, factor;
   factor = -1;
   unsigned size_tex = width * height;
   switch(fmt)
   {
      case GR_TEXFMT_ALPHA_8:
         factor = 1;
         *gltexfmt = GL_INTENSITY8;
         *glpixfmt = GL_LUMINANCE;
         *glpackfmt = GL_UNSIGNED_BYTE;
         break;
      case GR_TEXFMT_INTENSITY_8: // I8 support - H.Morii
         factor = 1;
         *gltexfmt = GL_LUMINANCE8;
         *glpixfmt = GL_LUMINANCE;
         *glpackfmt = GL_UNSIGNED_BYTE;
         break;
      case GR_TEXFMT_ALPHA_INTENSITY_44:
         {
            uint16_t *texture_ptr = &((uint16_t*)texture)[size_tex];
            //FIXME - still CPU software color conversion
            do{
               uint16_t texel = (uint16_t)((uint8_t*)info->data)[size_tex];
               // Replicate glide's ALPHA_INTENSITY_44 to match gl's LUMINANCE_ALPHA
               texel = (texel & 0x00F0) << 4 | (texel & 0x000F);
               *texture_ptr-- = (texel << 4) | texel;
            }while(size_tex--);
            factor = 1;
            *gltexfmt = GL_LUMINANCE_ALPHA;
            *glpixfmt = GL_LUMINANCE_ALPHA;
            *glpackfmt = GL_UNSIGNED_BYTE;
            info->data = texture;
         }
         break;
      case GR_TEXFMT_RGB_565:
         factor = 2;
         *gltexfmt = GL_RGB;
         *glpixfmt = GL_RGB;
         *glpackfmt = GL_UNSIGNED_SHORT_5_6_5;
         break;
      case GR_TEXFMT_ARGB_1555:
         factor = 2;
         *gltexfmt = GL_RGB5_A1;
         *glpixfmt = GL_BGRA;
         *glpackfmt = GL_UNSIGNED_SHORT_1_5_5_5_REV;
         break;
      case GR_TEXFMT_ALPHA_INTENSITY_88:
         factor = 2;
         *gltexfmt = GL_LUMINANCE8_ALPHA8;
         *glpixfmt = GL_LUMINANCE_ALPHA;
         *glpackfmt = GL_UNSIGNED_BYTE;
         break;
      case GR_TEXFMT_ARGB_4444:
         factor = 2;
         *gltexfmt = GL_RGBA4;
         *glpixfmt = GL_BGRA;
         *glpackfmt = GL_UNSIGNED_SHORT_4_4_4_4_REV;
         break;
      case GR_TEXFMT_ARGB_8888:
         factor = 4;
         *gltexfmt = GL_RGBA8;
         *glpixfmt = GL_BGRA;
         *glpackfmt = GL_UNSIGNED_INT_8_8_8_8_REV;
         break;
      default:
         DISPLAY_WARNING("grTexFormat2GLPackedFmt : unknown texture format: %x", fmt);
   }
   return factor;
}
#endif

FX_ENTRY void FX_CALL
grTexDownloadMipMap( GrChipID_t tmu,
                    FxU32      startAddress,
                    FxU32      evenOdd,
                    GrTexInfo  *info )
{
   int width, height;
   int factor;
   int gltexfmt, glpixfmt, glpackfmt;
   LOG("grTexDownloadMipMap(%d,%d,%d)\r\n", tmu, startAddress, evenOdd);
   if (info->largeLodLog2 != info->smallLodLog2) DISPLAY_WARNING("grTexDownloadMipMap : loading more than one LOD");

   if (info->aspectRatioLog2 < 0)
   {
      height = 1 << info->largeLodLog2;
      width = height >> -info->aspectRatioLog2;
   }
   else
   {
      width = 1 << info->largeLodLog2;
      height = width >> info->aspectRatioLog2;
   }


#ifndef HAVE_OPENGLES2
   if (packed_pixels_support)
      factor = grTexFormat2GLPackedFmt(info, info->format, &gltexfmt, &glpixfmt, &glpackfmt);
   else
#endif
   {
      // VP fixed the texture conversions to be more accurate, also swapped
      // the for i/j loops so that is is less likely to break the memory cache
      unsigned size_tex = width * height;
      switch(info->format)
      {
         case GR_TEXFMT_ALPHA_8:
            do
            {
               uint16_t texel = (uint16_t)((uint8_t*)info->data)[size_tex];

               // Replicate glide's ALPHA_8 to match gl's LUMINANCE_ALPHA
               // This is to make up for the lack of INTENSITY in gles
               ((uint16_t*)texture)[size_tex] = texel | (texel << 8);
            }while(size_tex--);
            factor = 1;
            glpixfmt = GL_LUMINANCE_ALPHA;
            gltexfmt = GL_LUMINANCE_ALPHA;
            glpackfmt = GL_UNSIGNED_BYTE;
            info->data = texture;
            break;
         case GR_TEXFMT_INTENSITY_8: // I8 support - H.Morii
            do
            {
               unsigned int texel = (unsigned int)((uint8_t*)info->data)[size_tex];
               texel |= (0xFF000000 | (texel << 16) | (texel << 8));
               ((unsigned int*)texture)[size_tex] = texel;
            }while(size_tex--);
            factor = 1;
            glpixfmt = GL_RGBA;
            gltexfmt = GL_RGBA;
            glpackfmt = GL_UNSIGNED_BYTE;
            info->data = texture;
            break;
         case GR_TEXFMT_ALPHA_INTENSITY_44:
            {
               uint16_t *texture_ptr = &((uint16_t*)texture)[size_tex];
               //FIXME - still CPU software color conversion
               do{
                  uint16_t texel = (uint16_t)((uint8_t*)info->data)[size_tex];
                  // Replicate glide's ALPHA_INTENSITY_44 to match gl's LUMINANCE_ALPHA
                  texel = (texel & 0x00F0) << 4 | (texel & 0x000F);
                  *texture_ptr-- = (texel << 4) | texel;
               }while(size_tex--);
               factor = 1;
               glpixfmt = GL_LUMINANCE_ALPHA;
               gltexfmt = GL_LUMINANCE_ALPHA;
               glpackfmt = GL_UNSIGNED_BYTE;
               info->data = texture;
            }
            break;
         case GR_TEXFMT_RGB_565:
            do
            {
               unsigned int texel = (unsigned int)((uint16_t*)info->data)[size_tex];
               unsigned int B = texel & 0x0000F800;
               unsigned int G = texel & 0x000007E0;
               unsigned int R = texel & 0x0000001F;
               ((unsigned int*)texture)[size_tex] = 0xFF000000 | (R << 19) | (G << 5) | (B >> 8);
            }while(size_tex--);
            factor = 2;
            glpixfmt = GL_RGBA;
            gltexfmt = GL_RGBA;
            glpackfmt = GL_UNSIGNED_BYTE;
            info->data = texture;
            break;
         case GR_TEXFMT_ARGB_1555:
            do
            {
               uint16_t texel = ((uint16_t*)info->data)[size_tex];

               // Shift-rotate glide's ARGB_1555 to match gl's RGB5_A1
               ((uint16_t*)texture)[size_tex] = (texel << 1) | (texel >> 15);
            }while(size_tex--);
            factor = 2;
            glpixfmt = GL_RGBA;
            gltexfmt = GL_RGBA;
            glpackfmt = GL_UNSIGNED_SHORT_5_5_5_1;
            info->data = texture;
            break;
         case GR_TEXFMT_ALPHA_INTENSITY_88:
            do
            {
               unsigned int AI = (unsigned int)((uint16_t*)info->data)[size_tex];
               unsigned int I = (unsigned int)(AI & 0x000000FF);
               ((unsigned int*)texture)[size_tex] = (AI << 16) | (I << 8) | I;
            }while(size_tex--);
            factor = 2;
            glpixfmt = GL_RGBA;
            gltexfmt = GL_RGBA;
            glpackfmt = GL_UNSIGNED_BYTE;
            info->data = texture;
            break;
         case GR_TEXFMT_ARGB_4444:
            do
            {
               unsigned int texel = (unsigned int)((uint16_t*)info->data)[size_tex];
               unsigned int A = texel & 0x0000F000;
               unsigned int B = texel & 0x00000F00;
               unsigned int G = texel & 0x000000F0;
               unsigned int R = texel & 0x0000000F;
               ((unsigned int*)texture)[size_tex] = (A << 16) | (R << 20) | (G << 8) | (B >> 4);
            }while(size_tex--);
            factor = 2;
            glpixfmt = GL_RGBA;
            gltexfmt = GL_RGBA;
            glpackfmt = GL_UNSIGNED_BYTE;
            info->data = texture;
            break;
         case GR_TEXFMT_ARGB_8888:
#ifdef HAVE_OPENGLES2
            if (bgra8888_support)
            {
               factor = 4;
               gltexfmt = GL_BGRA_EXT;
               glpixfmt = GL_BGRA_EXT;
               glpackfmt = GL_UNSIGNED_BYTE;
               break;
            }
#endif
            do
            {
               unsigned int texel = ((unsigned int*)info->data)[size_tex];
               unsigned int A = texel & 0xFF000000;
               unsigned int B = texel & 0x00FF0000;
               unsigned int G = texel & 0x0000FF00;
               unsigned int R = texel & 0x000000FF;
               ((unsigned int*)texture)[size_tex] = A | (R << 16) | G | (B >> 16);
            }while(size_tex--);
            factor = 4;
            glpixfmt = GL_RGBA;
            gltexfmt = GL_RGBA;
            glpackfmt = GL_UNSIGNED_BYTE;
            info->data = texture;
            break;
         default:
            DISPLAY_WARNING("grTexDownloadMipMap : unknown texture format: %x", info->format);
            factor = 0;
      }
   }

   glActiveTexture(GL_TEXTURE2);
   remove_tex(startAddress+1, startAddress+1+(width*height*factor));

   add_tex(startAddress+1);
   glBindTexture(GL_TEXTURE_2D, startAddress+1);

   glTexImage2D(GL_TEXTURE_2D, 0, gltexfmt, width, height, 0, glpixfmt, glpackfmt, info->data);
   glBindTexture(GL_TEXTURE_2D, default_texture);
}

FX_ENTRY void FX_CALL
grTexSource( GrChipID_t tmu,
            FxU32      startAddress,
            FxU32      evenOdd,
            GrTexInfo  *info )
{
   LOG("grTexSource(%d,%d,%d)\r\n", tmu, startAddress, evenOdd);

   if (tmu == GR_TMU1)
   {
      glActiveTexture(GL_TEXTURE0);

      if (info->aspectRatioLog2 < 0)
      {
         tex0_height = 256;
         tex0_width = tex0_height >> -info->aspectRatioLog2;
      }
      else
      {
         tex0_width = 256;
         tex0_height = tex0_width >> info->aspectRatioLog2;
      }

      glBindTexture(GL_TEXTURE_2D, startAddress+1);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_filter0);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag_filter0);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap_s0);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap_t0);
   }
   else
   {
      glActiveTexture(GL_TEXTURE1);

      if (info->aspectRatioLog2 < 0)
      {
         tex1_height = 256;
         tex1_width = tex1_height >> -info->aspectRatioLog2;
      }
      else
      {
         tex1_width = 256;
         tex1_height = tex1_width >> info->aspectRatioLog2;
      }

      glBindTexture(GL_TEXTURE_2D, startAddress+1);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_filter1);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag_filter1);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap_s1);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap_t1);
   }
   if(!CheckTextureBufferFormat(tmu, startAddress+1, info))
   {
      if(tmu == 0 && blackandwhite1 != 0)
      {
         blackandwhite1 = 0;
         need_to_compile = 1;
      }
      if(tmu == 1 && blackandwhite0 != 0)
      {
         blackandwhite0 = 0;
         need_to_compile = 1;
      }
   }

#if 0
   extern int auxbuffer;
   static int oldbuffer;
   FX_ENTRY void FX_CALL grAuxBufferExt( GrBuffer_t buffer );
   if (auxbuffer == GR_BUFFER_AUXBUFFER && auxbuffer != oldbuffer)
      grAuxBufferExt(auxbuffer);
   oldbuffer = auxbuffer;
#endif
}

FX_ENTRY void FX_CALL
grTexDetailControl(
                   GrChipID_t tmu,
                   int lod_bias,
                   FxU8 detail_scale,
                   float detail_max
                   )
{
   LOG("grTexDetailControl(%d,%d,%d,%d)\r\n", tmu, lod_bias, detail_scale, detail_max);
   if (lod_bias != 31 && detail_scale != 7)
   {
      if (!lod_bias && !detail_scale && !detail_max) return;
      else
         DISPLAY_WARNING("grTexDetailControl : %d, %d, %f", lod_bias, detail_scale, detail_max);
   }
   lambda = detail_max;
   if(lambda > 1.0f)
      lambda = 1.0f - (255.0f - lambda);
   if(lambda > 1.0f)
      DISPLAY_WARNING("lambda:%f", lambda);

   set_lambda();
}

FX_ENTRY void FX_CALL
grTexLodBiasValue(GrChipID_t tmu, float bias )
{
   LOG("grTexLodBiasValue(%d,%f)\r\n", tmu, bias);
}

FX_ENTRY void FX_CALL
grTexFilterMode(
                GrChipID_t tmu,
                GrTextureFilterMode_t minfilter_mode,
                GrTextureFilterMode_t magfilter_mode
                )
{
   LOG("grTexFilterMode(%d,%d,%d)\r\n", tmu, minfilter_mode, magfilter_mode);
   if (tmu == GR_TMU1)
   {
      if (minfilter_mode == GR_TEXTUREFILTER_POINT_SAMPLED)
         min_filter0 = GL_NEAREST;
      else
         min_filter0 = GL_LINEAR;

      if (magfilter_mode == GR_TEXTUREFILTER_POINT_SAMPLED)
         mag_filter0 = GL_NEAREST;
      else
         mag_filter0 = GL_LINEAR;

      glActiveTexture(GL_TEXTURE0);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_filter0);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag_filter0);
   }
   else
   {
      if (minfilter_mode == GR_TEXTUREFILTER_POINT_SAMPLED) min_filter1 = GL_NEAREST;
      else min_filter1 = GL_LINEAR;

      if (magfilter_mode == GR_TEXTUREFILTER_POINT_SAMPLED) mag_filter1 = GL_NEAREST;
      else mag_filter1 = GL_LINEAR;

      glActiveTexture(GL_TEXTURE1);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_filter1);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag_filter1);
   }
}

FX_ENTRY void FX_CALL
grTexClampMode(
               GrChipID_t tmu,
               GrTextureClampMode_t s_clampmode,
               GrTextureClampMode_t t_clampmode
               )
{
   LOG("grTexClampMode(%d, %d, %d)\r\n", tmu, s_clampmode, t_clampmode);
   if (tmu == GR_TMU1)
   {
      switch(s_clampmode)
      {
         case GR_TEXTURECLAMP_WRAP:
            wrap_s0 = GL_REPEAT;
            break;
         case GR_TEXTURECLAMP_CLAMP:
            wrap_s0 = GL_CLAMP_TO_EDGE;
            break;
         case GR_TEXTURECLAMP_MIRROR_EXT:
            wrap_s0 = GL_MIRRORED_REPEAT;
            break;
         default:
            DISPLAY_WARNING("grTexClampMode : unknown s_clampmode : %x", s_clampmode);
      }
      switch(t_clampmode)
      {
         case GR_TEXTURECLAMP_WRAP:
            wrap_t0 = GL_REPEAT;
            break;
         case GR_TEXTURECLAMP_CLAMP:
            wrap_t0 = GL_CLAMP_TO_EDGE;
            break;
         case GR_TEXTURECLAMP_MIRROR_EXT:
            wrap_t0 = GL_MIRRORED_REPEAT;
            break;
         default:
            DISPLAY_WARNING("grTexClampMode : unknown t_clampmode : %x", t_clampmode);
      }
      glActiveTexture(GL_TEXTURE0);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap_s0);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap_t0);
   }
   else
   {
      switch(s_clampmode)
      {
         case GR_TEXTURECLAMP_WRAP:
            wrap_s1 = GL_REPEAT;
            break;
         case GR_TEXTURECLAMP_CLAMP:
            wrap_s1 = GL_CLAMP_TO_EDGE;
            break;
         case GR_TEXTURECLAMP_MIRROR_EXT:
            wrap_s1 = GL_MIRRORED_REPEAT;
            break;
         default:
            DISPLAY_WARNING("grTexClampMode : unknown s_clampmode : %x", s_clampmode);
      }
      switch(t_clampmode)
      {
         case GR_TEXTURECLAMP_WRAP:
            wrap_t1 = GL_REPEAT;
            break;
         case GR_TEXTURECLAMP_CLAMP:
            wrap_t1 = GL_CLAMP_TO_EDGE;
            break;
         case GR_TEXTURECLAMP_MIRROR_EXT:
            wrap_t1 = GL_MIRRORED_REPEAT;
            break;
         default:
            DISPLAY_WARNING("grTexClampMode : unknown t_clampmode : %x", t_clampmode);
      }
      glActiveTexture(GL_TEXTURE1);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap_s1);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap_t1);
   }
}
