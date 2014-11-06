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

#ifndef __LIBRETRO__
#define SAVE_CBUFFER
#endif

#ifdef _WIN32
#include <windows.h>
#include <commctrl.h>

#ifndef __LIBRETRO__
#define HAVE_WGL
#endif

#else
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <SDL.h>
#endif // _WIN32
#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <fstream>
#include <math.h>
#include "glide.h"
#include "g3ext.h"
#include "main.h"
#include "m64p.h"
#include "glitchlog.h"
#ifdef VPDEBUG
#include "glitchdebug.h"
#endif

#ifdef HAVE_WGL
#include "glitchmain_WGL.h"
#endif

#include <SDL_opengles.h>
//#include <GL/glext.h>

#define OPENGL_CHECK_ERRORS { const GLenum errcode = glGetError(); if (errcode != GL_NO_ERROR) LOG("OpenGL Error code %i in '%s' line %i\n", errcode, __FILE__, __LINE__-1); }


extern void (*renderCallback)(int);

wrapper_config config = {0, 0, 0, 0};
int screen_width, screen_height;

/*
static inline void opt_glCopyTexImage2D( GLenum target,
                                        GLint level,
                                        GLenum internalFormat,
                                        GLint x,
                                        GLint y,
                                        GLsizei width,
                                        GLsizei height,
                                        GLint border )

{
  int w, h, fmt;
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &fmt);
  //printf("copyteximage %dx%d fmt %x oldfmt %x\n", width, height, internalFormat, fmt);
  if (w == (int) width && h == (int) height && fmt == (int) internalFormat) {
    if (x+width >= screen_width) {
      width = screen_width - x;
      //printf("resizing w --> %d\n", width);
    }
    if (y+height >= screen_height+viewport_offset) {
      height = screen_height+viewport_offset - y;
      //printf("resizing h --> %d\n", height);
    }
    glCopyTexSubImage2D(target, level, 0, 0, x, y, width, height);
  } else {
    //printf("copyteximage %dx%d fmt %x old %dx%d oldfmt %x\n", width, height, internalFormat, w, h, fmt);
    //       glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, internalFormat, GL_UNSIGNED_BYTE, 0);
    //       glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &fmt);
    //       printf("--> %dx%d newfmt %x\n", width, height, fmt);
    glCopyTexImage2D(target, level, internalFormat, x, y, width, height, border);
  }
}
#define glCopyTexImage2D opt_glCopyTexImage2D
*/

typedef struct
{
  unsigned int address;
  int width;
  int height;
  unsigned int fbid;
  unsigned int zbid;
  unsigned int texid;
  int buff_clear;
} fb;

int nbTextureUnits;
int nbAuxBuffers, current_buffer;
int width, widtho, heighto, height;
int saved_width, saved_height;
int blend_func_separate_support;
int npot_support;
int fog_coord_support;
int render_to_texture = 0;
int texture_unit;
int use_fbo = 1;
int buffer_cleared;
// ZIGGY
// to allocate a new static texture name, take the value (free_texture++)
int free_texture;
int default_texture; // the infamous "32*1024*1024" is now configurable
int current_texture;
int depth_texture, color_texture;
int glsl_support = 1;
int viewport_width, viewport_height, viewport_offset = 0, nvidia_viewport_hack = 0;
int save_w, save_h;
int lfb_color_fmt;
float invtex[2];
//Gonetz
int UMAmode = 0; //support for VSA-100 UMA mode;

unsigned long glitch_fullscreen;

static int savedWidtho, savedHeighto;
static int savedWidth, savedHeight;
unsigned int pBufferAddress;
static int pBufferFmt;
static int pBufferWidth, pBufferHeight;
static fb fbs[100];
static int nb_fb = 0;
static unsigned int curBufferAddr = 0;

struct TMU_USAGE { int min, max; } tmu_usage[2] = { {0xfffffff, 0}, {0xfffffff, 0} };

struct texbuf_t {
  FxU32 start, end;
  int fmt;
};
#define NB_TEXBUFS 128 // MUST be a power of two
static texbuf_t texbufs[NB_TEXBUFS];
static int texbuf_i;

unsigned short frameBuffer[2048*2048];
unsigned short depthBuffer[2048*2048];

//#define VOODOO1

FX_ENTRY GrContext_t FX_CALL
grSstWinOpen(
             HWND                 hWnd,
             GrScreenResolution_t screen_resolution,
             GrScreenRefresh_t    refresh_rate,
             GrColorFormat_t      color_format,
             GrOriginLocation_t   origin_location,
             int                  nColBuffers,
             int                  nAuxBuffers)
{
  static int show_warning = 1;

  // ZIGGY
  // allocate static texture names
  // the initial value should be big enough to support the maximal resolution
  free_texture = 32*2048*2048;
  default_texture = free_texture++;
  color_texture = free_texture++;
  depth_texture = free_texture++;

  LOG("grSstWinOpen(%08lx, %d, %d, %d, %d, %d %d)\r\n", hWnd, screen_resolution&~0x80000000, refresh_rate, color_format, origin_location, nColBuffers, nAuxBuffers);

#ifdef HAVE_WGL
  wgl_init(hWnd);
#endif
  width = height = 0;

  m64p_handle video_general_section;
  printf("&ConfigOpenSection is %p\n", &ConfigOpenSection);
  if (ConfigOpenSection("Video-General", &video_general_section) != M64ERR_SUCCESS)
  {
    printf("Could not open video settings");
    return false;
  }
  width = ConfigGetParamInt(video_general_section, "ScreenWidth");
  height = ConfigGetParamInt(video_general_section, "ScreenHeight");
  glitch_fullscreen = ConfigGetParamBool(video_general_section, "Fullscreen");
  int vsync = ConfigGetParamBool(video_general_section, "VerticalSync");
  //viewport_offset = ((screen_resolution>>2) > 20) ? screen_resolution >> 2 : 20;
  // ZIGGY viewport_offset is WIN32 specific, with SDL just set it to zero
  viewport_offset = 0; //-10 //-20;

  CoreVideo_Init();
  CoreVideo_GL_SetAttribute(M64P_GL_DOUBLEBUFFER, 1);
  CoreVideo_GL_SetAttribute(M64P_GL_SWAP_CONTROL, vsync);
  CoreVideo_GL_SetAttribute(M64P_GL_BUFFER_SIZE, 16);
  //   SDL_GL_SetAttribute(SDL_GL_BUFFER_SIZE, 32);
  //   SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
  //   SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
  //   SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
  //   SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
  CoreVideo_GL_SetAttribute(M64P_GL_DEPTH_SIZE, 16);

  printf("(II) Setting video mode %dx%d...\n", width, height);
  if(CoreVideo_SetVideoMode(width, height, 0, glitch_fullscreen ? M64VIDEO_FULLSCREEN : M64VIDEO_WINDOWED, (m64p_video_flags) 0) != M64ERR_SUCCESS)
  {
    printf("(EE) Error setting videomode %dx%d\n", width, height);
    return false;
  }

  char caption[500];
# ifdef _DEBUG
  sprintf(caption, "Glide64mk2 debug");
# else // _DEBUG
  sprintf(caption, "Glide64mk2");
# endif // _DEBUG
  CoreVideo_SetCaption(caption);

  glViewport(0, viewport_offset, width, height);
  lfb_color_fmt = color_format;
  if (origin_location != GR_ORIGIN_UPPER_LEFT) display_warning("origin must be in upper left corner");
  if (nColBuffers != 2) display_warning("number of color buffer is not 2");
  if (nAuxBuffers != 1) display_warning("number of auxiliary buffer is not 1");

  if (isExtensionSupported("GL_ARB_texture_env_combine") == 0 &&
    isExtensionSupported("GL_EXT_texture_env_combine") == 0 &&
    show_warning)
    display_warning("Your video card doesn't support GL_ARB_texture_env_combine extension");
  if (isExtensionSupported("GL_ARB_multitexture") == 0 && show_warning)
    display_warning("Your video card doesn't support GL_ARB_multitexture extension");
  if (isExtensionSupported("GL_ARB_texture_mirrored_repeat") == 0 && show_warning)
    display_warning("Your video card doesn't support GL_ARB_texture_mirrored_repeat extension");
  show_warning = 0;


  nbTextureUnits = 4;
  //glGetIntegerv(GL_MAX_TEXTURE_UNITS_ARB, &nbTextureUnits);
  if (nbTextureUnits == 1) display_warning("You need a video card that has at least 2 texture units");

  nbAuxBuffers = 4;
  //glGetIntegerv(GL_AUX_BUFFERS, &nbAuxBuffers);
  if (nbAuxBuffers > 0)
    printf("Congratulations, you have %d auxilliary buffers, we'll use them wisely !\n", nbAuxBuffers);
#ifdef VOODOO1
  nbTextureUnits = 2;
#endif

  blend_func_separate_support = 1;
  packed_pixels_support = 0;
/*
  if (isExtensionSupported("GL_EXT_blend_func_separate") == 0)
    blend_func_separate_support = 0;
  else
    blend_func_separate_support = 1;

  if (isExtensionSupported("GL_EXT_packed_pixels") == 0)
    packed_pixels_support = 0;
  else {
    printf("packed pixels extension used\n");
    packed_pixels_support = 1;
  }
*/

  if (isExtensionSupported("GL_ARB_texture_non_power_of_two") == 0)
    npot_support = 0;
  else {
    printf("NPOT extension used\n");
    npot_support = 1;
  }

  if (isExtensionSupported("GL_EXT_fog_coord") == 0)
    fog_coord_support = 0;
  else
    fog_coord_support = 1;

#ifdef HAVE_WGL
  use_fbo = WGL_LookupSymbols();
  if (use_fbo)
#endif
     use_fbo = config.fbo;

  LOGINFO("use_fbo %d\n", use_fbo);

  if (isExtensionSupported("GL_EXT_texture_compression_s3tc") == 0  && show_warning)
    display_warning("Your video card doesn't support GL_EXT_texture_compression_s3tc extension");
  if (isExtensionSupported("GL_3DFX_texture_compression_FXT1") == 0  && show_warning)
    display_warning("Your video card doesn't support GL_3DFX_texture_compression_FXT1 extension");

  glViewport(0, viewport_offset, width, height);
  viewport_width = width;
  viewport_height = height;
#ifdef _WIN32
  nvidia_viewport_hack = 1;
#endif // _WIN32

  //   void do_benchmarks();
  //   do_benchmarks();

  // VP try to resolve z precision issues
//  glMatrixMode(GL_MODELVIEW);
//  glLoadIdentity();
//  glTranslatef(0, 0, 1-zscale);
//  glScalef(1, 1, zscale);

  widtho = width/2;
  heighto = height/2;

  pBufferWidth = pBufferHeight = -1;

  current_buffer = GL_BACK;

  texture_unit = GL_TEXTURE0;

  {
    int i;
    for (i=0; i<NB_TEXBUFS; i++)
      texbufs[i].start = texbufs[i].end = 0xffffffff;
  }

  if (!use_fbo && nbAuxBuffers == 0) {
    // create the framebuffer saving texture
    int w = width, h = height;
    glBindTexture(GL_TEXTURE_2D, color_texture);
    if (!npot_support) {
      w = h = 1;
      while (w<width) w*=2;
      while (h<height) h*=2;
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    save_w = save_h = 0;
  }

  //void FindBestDepthBias();
  //FindBestDepthBias();

  init_geometry();
  init_textures();
  init_combiner();

  config.anisofilter = ConfigGetParamInt(video_general_section, "AnisoFilter");
#ifndef GLES
  // Aniso filter check
  if (config.anisofilter > 0 )
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &largest_supported_anisotropy);
#endif
/*

  // ATI hack - certain texture formats are slow on ATI?
  // Hmm, perhaps the internal format need to be specified explicitly...
  {
    GLint ifmt;
    glTexImage2D(GL_PROXY_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV, NULL);
    glGetTexLevelParameteriv(GL_PROXY_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &ifmt);
    if (ifmt != GL_RGB5_A1) {
      display_warning("ATI SUCKS %x\n", ifmt);
      ati_sucks = 1;
    } else
      ati_sucks = 0;
  }
*/

  return 1;
}

FX_ENTRY FxBool FX_CALL
grSstWinClose( GrContext_t context )
{
  int i, clear_texbuff = use_fbo;
  LOG("grSstWinClose(%d)\r\n", context);

  for (i=0; i<2; i++) {
    tmu_usage[i].min = 0xfffffff;
    tmu_usage[i].max = 0;
    invtex[i] = 0;
  }

  free_combiners();
#ifndef WIN32
  try // I don't know why, but opengl can be killed before this function call when emulator is closed (Gonetz).
    // ZIGGY : I found the problem : it is a function pointer, when the extension isn't supported , it is then zero, so just need to check the pointer prior to do the call.
  {
    if (use_fbo)
      glBindFramebuffer( GL_FRAMEBUFFER, 0 );
  }
  catch (...)
  {
    clear_texbuff = 0;
  }

  if (clear_texbuff)
  {
    for (i=0; i<nb_fb; i++)
    {
      glDeleteTextures( 1, &(fbs[i].texid) );
      glDeleteFramebuffers( 1, &(fbs[i].fbid) );
      glDeleteRenderbuffers( 1, &(fbs[i].zbid) );
    }
  }
#endif
  nb_fb = 0;

  free_textures();
#ifndef WIN32
  // ZIGGY for some reasons, Pj64 doesn't like remove_tex on exit
  remove_tex(0, 0xfffffff);
#endif

#ifdef HAVE_WGL
  wgl_deinit(glitch_fullscreen);
  glitch_fullscreen = 0;
#endif

  CoreVideo_Quit();

  return FXTRUE;
}

FX_ENTRY void FX_CALL grTextureBufferExt( GrChipID_t  		tmu,
                                         FxU32 				startAddress,
                                         GrLOD_t 			lodmin,
                                         GrLOD_t 			lodmax,
                                         GrAspectRatio_t 	aspect,
                                         GrTextureFormat_t 	fmt,
                                         FxU32 				evenOdd)
{
  int i;
  static int fbs_init = 0;

  //printf("grTextureBufferExt(%d, %d, %d, %d, %d, %d, %d)\r\n", tmu, startAddress, lodmin, lodmax, aspect, fmt, evenOdd);
  LOG("grTextureBufferExt(%d, %d, %d, %d %d, %d, %d)\r\n", tmu, startAddress, lodmin, lodmax, aspect, fmt, evenOdd);
  if (lodmin != lodmax) display_warning("grTextureBufferExt : loading more than one LOD");
  if (!use_fbo) {

    if (!render_to_texture) { //initialization
      return;
    }

    render_to_texture = 2;

    if (aspect < 0)
    {
      pBufferHeight = 1 << lodmin;
      pBufferWidth = pBufferHeight >> -aspect;
    }
    else
    {
      pBufferWidth = 1 << lodmin;
      pBufferHeight = pBufferWidth >> aspect;
    }

    if (curBufferAddr && startAddress+1 != curBufferAddr)
      updateTexture();
#ifdef SAVE_CBUFFER
    //printf("saving %dx%d\n", pBufferWidth, pBufferHeight);
    // save color buffer
    if (nbAuxBuffers > 0) {
      //glDrawBuffer(GL_AUX0);
      //current_buffer = GL_AUX0;
    } else {
      int tw, th;
      if (pBufferWidth < screen_width)
        tw = pBufferWidth;
      else
        tw = screen_width;
      if (pBufferHeight < screen_height)
        th = pBufferHeight;
      else
        th = screen_height;
      //glReadBuffer(GL_BACK);
      glActiveTexture(texture_unit);
      glBindTexture(GL_TEXTURE_2D, color_texture);
      // save incrementally the framebuffer
      if (save_w) {
        if (tw > save_w && th > save_h) {
          glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, save_h,
            0, viewport_offset+save_h, tw, th-save_h);
          glCopyTexSubImage2D(GL_TEXTURE_2D, 0, save_w, 0,
            save_w, viewport_offset, tw-save_w, save_h);
          save_w = tw;
          save_h = th;
        } else if (tw > save_w) {
          glCopyTexSubImage2D(GL_TEXTURE_2D, 0, save_w, 0,
            save_w, viewport_offset, tw-save_w, save_h);
          save_w = tw;
        } else if (th > save_h) {
          glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, save_h,
            0, viewport_offset+save_h, save_w, th-save_h);
          save_h = th;
        }
      } else {
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
          0, viewport_offset, tw, th);
        save_w = tw;
        save_h = th;
      }
      glBindTexture(GL_TEXTURE_2D, default_texture);
    }
#endif

    if (startAddress+1 != curBufferAddr ||
      (curBufferAddr == 0L && nbAuxBuffers == 0))
      buffer_cleared = 0;

    curBufferAddr = pBufferAddress = startAddress+1;
    pBufferFmt = fmt;

    int rtmu = startAddress < grTexMinAddress(GR_TMU1)? 0 : 1;
    int size = pBufferWidth*pBufferHeight*2; //grTexFormatSize(fmt);
    if ((unsigned int) tmu_usage[rtmu].min > pBufferAddress)
      tmu_usage[rtmu].min = pBufferAddress;
    if ((unsigned int) tmu_usage[rtmu].max < pBufferAddress+size)
      tmu_usage[rtmu].max = pBufferAddress+size;
    //   printf("tmu %d usage now %gMb - %gMb\n",
    //          rtmu, tmu_usage[rtmu].min/1024.0f, tmu_usage[rtmu].max/1024.0f);


    width = pBufferWidth;
    height = pBufferHeight;

    widtho = width/2;
    heighto = height/2;

    // this could be improved, but might be enough as long as the set of
    // texture buffer addresses stay small
    for (i=(texbuf_i-1)&(NB_TEXBUFS-1) ; i!=texbuf_i; i=(i-1)&(NB_TEXBUFS-1))
      if (texbufs[i].start == pBufferAddress)
        break;
    texbufs[i].start = pBufferAddress;
    texbufs[i].end = pBufferAddress + size;
    texbufs[i].fmt = fmt;
    if (i == texbuf_i)
      texbuf_i = (texbuf_i+1)&(NB_TEXBUFS-1);
    //printf("texbuf %x fmt %x\n", pBufferAddress, fmt);

    // ZIGGY it speeds things up to not delete the buffers
    // a better thing would be to delete them *sometimes*
    //   remove_tex(pBufferAddress+1, pBufferAddress + size);
    add_tex(pBufferAddress);

    //printf("viewport %dx%d\n", width, height);
    if (height > screen_height) {
      glViewport( 0, viewport_offset + screen_height - height, width, height);
    } else
      glViewport( 0, viewport_offset, width, height);

    glScissor(0, viewport_offset, width, height);


  } else {
    if (!render_to_texture) //initialization
    {
      if(!fbs_init)
      {
        for(i=0; i<100; i++) fbs[i].address = 0;
        fbs_init = 1;
        nb_fb = 0;
      }
      return; //no need to allocate FBO if render buffer is not texture buffer
    }

    render_to_texture = 2;

    if (aspect < 0)
    {
      pBufferHeight = 1 << lodmin;
      pBufferWidth = pBufferHeight >> -aspect;
    }
    else
    {
      pBufferWidth = 1 << lodmin;
      pBufferHeight = pBufferWidth >> aspect;
    }
    pBufferAddress = startAddress+1;

    width = pBufferWidth;
    height = pBufferHeight;

    widtho = width/2;
    heighto = height/2;

    for (i=0; i<nb_fb; i++)
    {
      if (fbs[i].address == pBufferAddress)
      {
        if (fbs[i].width == width && fbs[i].height == height) //select already allocated FBO
        {
          glBindFramebuffer( GL_FRAMEBUFFER, 0 );
          glBindFramebuffer( GL_FRAMEBUFFER, fbs[i].fbid );
          glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbs[i].texid, 0 );
          glBindRenderbuffer( GL_RENDERBUFFER, fbs[i].zbid );
          glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fbs[i].zbid );
          glViewport( 0, 0, width, height);
          glScissor( 0, 0, width, height);
          if (fbs[i].buff_clear)
          {
            glDepthMask(1);
            glClear( GL_DEPTH_BUFFER_BIT ); //clear z-buffer only. we may need content, stored in the frame buffer
            fbs[i].buff_clear = 0;
          }
          CHECK_FRAMEBUFFER_STATUS();
          curBufferAddr = pBufferAddress;
          return;
        }
        else //create new FBO at the same address, delete old one
        {
          glDeleteFramebuffers( 1, &(fbs[i].fbid) );
          glDeleteRenderbuffers( 1, &(fbs[i].zbid) );
          if (nb_fb > 1)
            memmove(&(fbs[i]), &(fbs[i+1]), sizeof(fb)*(nb_fb-i));
          nb_fb--;
          break;
        }
      }
    }

    remove_tex(pBufferAddress, pBufferAddress + width*height*2/*grTexFormatSize(fmt)*/);
    //create new FBO
    glGenFramebuffers( 1, &(fbs[nb_fb].fbid) );
    glGenRenderbuffers( 1, &(fbs[nb_fb].zbid) );
    glBindRenderbuffer( GL_RENDERBUFFER, fbs[nb_fb].zbid );
    glRenderbufferStorage( GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, width, height);
    fbs[nb_fb].address = pBufferAddress;
    fbs[nb_fb].width = width;
    fbs[nb_fb].height = height;
    fbs[nb_fb].texid = pBufferAddress;
    fbs[nb_fb].buff_clear = 0;
    add_tex(fbs[nb_fb].texid);
    glBindTexture(GL_TEXTURE_2D, fbs[nb_fb].texid);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0,
      GL_RGB, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );

    glBindFramebuffer( GL_FRAMEBUFFER, fbs[nb_fb].fbid);
    glFramebufferTexture2D(GL_FRAMEBUFFER,
      GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbs[nb_fb].texid, 0);
    glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fbs[nb_fb].zbid );
    glViewport(0,0,width,height);
    glScissor(0,0,width,height);
    glClearColor( 0.0f, 0.0f, 0.0f, 1.0f );
    glDepthMask(1);
    glClear( GL_DEPTH_BUFFER_BIT );
    CHECK_FRAMEBUFFER_STATUS();
    curBufferAddr = pBufferAddress;
    nb_fb++;
  }
}

int CheckTextureBufferFormat(GrChipID_t tmu, FxU32 startAddress, GrTexInfo *info )
{
  int found, i;
  if (!use_fbo) {
    for (found=i=0; i<2; i++)
      if ((FxU32) tmu_usage[i].min <= startAddress && (FxU32) tmu_usage[i].max > startAddress) {
        //printf("tmu %d == framebuffer %x\n", tmu, startAddress);
        found = 1;
        break;
      }
  } else {
    found = i = 0;
    while (i < nb_fb)
    {
      unsigned int end = fbs[i].address + fbs[i].width*fbs[i].height*2;
      if (startAddress >= fbs[i].address &&  startAddress < end)
      {
        found = 1;
        break;
      }
      i++;
    }
  }

  if (!use_fbo && found) {
    int tw, th, rh, cw, ch;
    if (info->aspectRatioLog2 < 0)
    {
      th = 1 << info->largeLodLog2;
      tw = th >> -info->aspectRatioLog2;
    }
    else
    {
      tw = 1 << info->largeLodLog2;
      th = tw >> info->aspectRatioLog2;
    }

    if (info->aspectRatioLog2 < 0)
    {
      ch = 256;
      cw = ch >> -info->aspectRatioLog2;
    }
    else
    {
      cw = 256;
      ch = cw >> info->aspectRatioLog2;
    }

    if (use_fbo || th < screen_height)
      rh = th;
    else
      rh = screen_height;

    //printf("th %d rh %d ch %d\n", th, rh, ch);

    invtex[tmu] = 1.0f - (th - rh) / (float)th;
  } else
    invtex[tmu] = 0;

  if (info->format == GR_TEXFMT_ALPHA_INTENSITY_88 ) {
    if (!found) {
      return 0;
    }
    if(tmu == 0)
    {
      if(blackandwhite1 != found)
      {
        blackandwhite1 = found;
        need_to_compile = 1;
      }
    }
    else
    {
      if(blackandwhite0 != found)
      {
        blackandwhite0 = found;
        need_to_compile = 1;
      }
    }
    return 1;
  }
  return 0;

}


FX_ENTRY void FX_CALL
grTextureAuxBufferExt( GrChipID_t tmu,
                      FxU32      startAddress,
                      GrLOD_t    thisLOD,
                      GrLOD_t    largeLOD,
                      GrAspectRatio_t aspectRatio,
                      GrTextureFormat_t format,
                      FxU32      odd_even_mask )
{
  LOG("grTextureAuxBufferExt(%d, %d, %d, %d %d, %d, %d)\r\n", tmu, startAddress, thisLOD, largeLOD, aspectRatio, format, odd_even_mask);
  //display_warning("grTextureAuxBufferExt");
}

FX_ENTRY void FX_CALL grAuxBufferExt( GrBuffer_t buffer );

FX_ENTRY GrProc FX_CALL
grGetProcAddress( char *procName )
{
  LOG("grGetProcAddress(%s)\r\n", procName);
  if(!strcmp(procName, "grSstWinOpenExt"))
    return (GrProc)grSstWinOpenExt;
  if(!strcmp(procName, "grTextureBufferExt"))
    return (GrProc)grTextureBufferExt;
  if(!strcmp(procName, "grChromaRangeExt"))
    return (GrProc)grChromaRangeExt;
  if(!strcmp(procName, "grChromaRangeModeExt"))
    return (GrProc)grChromaRangeModeExt;
  if(!strcmp(procName, "grTexChromaRangeExt"))
    return (GrProc)grTexChromaRangeExt;
  if(!strcmp(procName, "grTexChromaModeExt"))
    return (GrProc)grTexChromaModeExt;
  // ZIGGY framebuffer copy extension
  if(!strcmp(procName, "grFramebufferCopyExt"))
    return (GrProc)grFramebufferCopyExt;
  if(!strcmp(procName, "grColorCombineExt"))
    return (GrProc)grColorCombineExt;
  if(!strcmp(procName, "grAlphaCombineExt"))
    return (GrProc)grAlphaCombineExt;
  if(!strcmp(procName, "grTexColorCombineExt"))
    return (GrProc)grTexColorCombineExt;
  if(!strcmp(procName, "grTexAlphaCombineExt"))
    return (GrProc)grTexAlphaCombineExt;
  if(!strcmp(procName, "grConstantColorValueExt"))
    return (GrProc)grConstantColorValueExt;
  if(!strcmp(procName, "grTextureAuxBufferExt"))
    return (GrProc)grTextureAuxBufferExt;
  if(!strcmp(procName, "grAuxBufferExt"))
    return (GrProc)grAuxBufferExt;
  if(!strcmp(procName, "grWrapperFullScreenResolutionExt"))
    return (GrProc)grWrapperFullScreenResolutionExt;
  if(!strcmp(procName, "grConfigWrapperExt"))
    return (GrProc)grConfigWrapperExt;
  if(!strcmp(procName, "grKeyPressedExt"))
    return (GrProc)grKeyPressedExt;
  if(!strcmp(procName, "grQueryResolutionsExt"))
    return (GrProc)grQueryResolutionsExt;
  if(!strcmp(procName, "grGetGammaTableExt"))
    return (GrProc)grGetGammaTableExt;
  display_warning("grGetProcAddress : %s", procName);
  return 0;
}

static void render_rectangle(int texture_number,
                             int dst_x, int dst_y,
                             int src_width, int src_height,
                             int tex_width, int tex_height, int invert)
{
  LOGINFO("render_rectangle(%d,%d,%d,%d,%d,%d,%d,%d)",texture_number,dst_x,dst_y,src_width,src_height,tex_width,tex_height,invert);
  int vertexOffset_location;
  int textureSizes_location;
  static float data[] = {
    (float)((int)dst_x),                      //X 0
    (float)(invert*-((int)dst_y)),            //Y 0 
    0.0f,                                     //U 0 
    0.0f,                                     //V 0

    (float)((int)dst_x),                      //X 1
    (float)(invert*-((int)dst_y + (int)src_height)),   //Y 1
    0.0f,                                     //U 1
    (float)src_height / (float)tex_height,    //V 1

    (float)((int)dst_x + (int)src_width), 
    (float)(invert*-((int)dst_y + (int)src_height)),
    (float)src_width / (float)tex_width,
    (float)src_height / (float)tex_height,

    (float)((int)dst_x),
    (float)(invert*-((int)dst_y)),
    0.0f,
    0.0f
  };

  vbo_disable();
  glDisableVertexAttribArray(COLOUR_ATTR);
  glDisableVertexAttribArray(TEXCOORD_1_ATTR);
  glDisableVertexAttribArray(FOG_ATTR);

  glVertexAttribPointer(POSITION_ATTR,2,GL_FLOAT,false,2,data); //Position
  glVertexAttribPointer(TEXCOORD_0_ATTR,2,GL_FLOAT,false,2,&data[2]); //Tex

  glEnableVertexAttribArray(COLOUR_ATTR);
  glEnableVertexAttribArray(TEXCOORD_1_ATTR);
  glEnableVertexAttribArray(FOG_ATTR);


  disable_textureSizes();

  glDrawArrays(GL_TRIANGLE_STRIP,0,4);
  vbo_enable();


/*
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBegin(GL_QUADS);
  glMultiTexCoord2fARB(texture_number, 0.0f, 0.0f);
  glVertex2f(((int)dst_x - widtho) / (float)(width/2),
    invert*-((int)dst_y - heighto) / (float)(height/2));
  glMultiTexCoord2fARB(texture_number, 0.0f, (float)src_height / (float)tex_height);
  glVertex2f(((int)dst_x - widtho) / (float)(width/2),
    invert*-((int)dst_y + (int)src_height - heighto) / (float)(height/2));
  glMultiTexCoord2fARB(texture_number, (float)src_width / (float)tex_width, (float)src_height / (float)tex_height);
  glVertex2f(((int)dst_x + (int)src_width - widtho) / (float)(width/2),
    invert*-((int)dst_y + (int)src_height - heighto) / (float)(height/2));
  glMultiTexCoord2fARB(texture_number, (float)src_width / (float)tex_width, 0.0f);
  glVertex2f(((int)dst_x + (int)src_width - widtho) / (float)(width/2),
    invert*-((int)dst_y - heighto) / (float)(height/2));
  glMultiTexCoord2fARB(texture_number, 0.0f, 0.0f);
  glVertex2f(((int)dst_x - widtho) / (float)(width/2),
    invert*-((int)dst_y - heighto) / (float)(height/2));
  glEnd();
*/

  compile_shader();

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
}

void reloadTexture()
{
  if (use_fbo || !render_to_texture || buffer_cleared)
    return;

  LOG("reload texture %dx%d\n", width, height);
  //printf("reload texture %dx%d\n", width, height);

  buffer_cleared = 1;

  //glPushAttrib(GL_ALL_ATTRIB_BITS);
  glActiveTexture(texture_unit);
  glBindTexture(GL_TEXTURE_2D, pBufferAddress);
  //glDisable(GL_ALPHA_TEST);
  //glDrawBuffer(current_buffer);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  set_copy_shader();
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  int w = 0, h = 0;
  if (height > screen_height) h = screen_height - height;
  render_rectangle(texture_unit,
    -w, -h,
    width,  height,
    width, height, -1);
  glBindTexture(GL_TEXTURE_2D, default_texture);
  //glPopAttrib();
}

void updateTexture()
{
  if (!use_fbo && render_to_texture == 2) {
    LOG("update texture %x\n", pBufferAddress);
    //printf("update texture %x\n", pBufferAddress);

    // nothing changed, don't update the texture
    if (!buffer_cleared) {
      LOG("update cancelled\n", pBufferAddress);
      return;
    }

    //glPushAttrib(GL_ALL_ATTRIB_BITS);

    // save result of render to texture into actual texture
    //glReadBuffer(current_buffer);
    glActiveTexture(texture_unit);
    // ZIGGY
    // deleting the texture before resampling it increases speed on certain old
    // nvidia cards (geforce 2 for example), unfortunatly it slows down a lot
    // on newer cards.
    //glDeleteTextures( 1, &pBufferAddress );
    glBindTexture(GL_TEXTURE_2D, pBufferAddress);
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB,
      0, viewport_offset, width, height, 0);

    glBindTexture(GL_TEXTURE_2D, default_texture);
    //glPopAttrib();
  }
}

FX_ENTRY void FX_CALL grFramebufferCopyExt(int x, int y, int w, int h,
                                           int from, int to, int mode)
{
  if (mode == GR_FBCOPY_MODE_DEPTH) {

    int tw = 1, th = 1;
    if (npot_support) {
      tw = width; th = height;
    } else {
      while (tw < width) tw <<= 1;
      while (th < height) th <<= 1;
    }

    if (from == GR_FBCOPY_BUFFER_BACK && to == GR_FBCOPY_BUFFER_FRONT) {
      //printf("save depth buffer %d\n", render_to_texture);
      // save the depth image in a texture
      //glReadBuffer(current_buffer);
      glBindTexture(GL_TEXTURE_2D, depth_texture);
      glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT,
        0, viewport_offset, tw, th, 0);
      glBindTexture(GL_TEXTURE_2D, default_texture);
      return;
    }
    if (from == GR_FBCOPY_BUFFER_FRONT && to == GR_FBCOPY_BUFFER_BACK) {
      //printf("writing to depth buffer %d\n", render_to_texture);
      //glPushAttrib(GL_ALL_ATTRIB_BITS);
      //glDisable(GL_ALPHA_TEST);
      //glDrawBuffer(current_buffer);
      glActiveTexture(texture_unit);
      glBindTexture(GL_TEXTURE_2D, depth_texture);
      glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
      set_depth_shader();
      glEnable(GL_DEPTH_TEST);
      glDepthFunc(GL_ALWAYS);
      glDisable(GL_CULL_FACE);
      render_rectangle(texture_unit,
        0, 0,
        width,  height,
        tw, th, -1);
      glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
      glBindTexture(GL_TEXTURE_2D, default_texture);
      //glPopAttrib();
      return;
    }

  }
}

FX_ENTRY void FX_CALL
grRenderBuffer( GrBuffer_t buffer )
{
#ifdef _WIN32
  static HANDLE region = NULL;
  int realWidth = pBufferWidth, realHeight = pBufferHeight;
#endif // _WIN32
  LOG("grRenderBuffer(%d)\r\n", buffer);
  //printf("grRenderBuffer(%d)\n", buffer);

  switch(buffer)
  {
  case GR_BUFFER_BACKBUFFER:
    if(render_to_texture)
    {
      updateTexture();

      // VP z fix
      //glMatrixMode(GL_MODELVIEW);
      //glLoadIdentity();
      //glTranslatef(0, 0, 1-zscale);
      //glScalef(1, 1, zscale);
      inverted_culling = 0;
      grCullMode(culling_mode);

      width = savedWidth;
      height = savedHeight;
      widtho = savedWidtho;
      heighto = savedHeighto;
      if (use_fbo) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindRenderbuffer( GL_RENDERBUFFER, 0 );
      }
      curBufferAddr = 0;

      glViewport(0, viewport_offset, width, viewport_height);
      glScissor(0, viewport_offset, width, height);

#ifdef SAVE_CBUFFER
      if (!use_fbo && render_to_texture == 2) {
        // restore color buffer
        if (nbAuxBuffers > 0) {
          //glDrawBuffer(GL_BACK);
          current_buffer = GL_BACK;
        } else if (save_w) {
          int tw = 1, th = 1;
          //printf("restore %dx%d\n", save_w, save_h);
          if (npot_support) {
            tw = screen_width;
            th = screen_height;
          } else {
            while (tw < screen_width) tw <<= 1;
            while (th < screen_height) th <<= 1;
          }

          //glPushAttrib(GL_ALL_ATTRIB_BITS);
          //glDisable(GL_ALPHA_TEST);
          //glDrawBuffer(GL_BACK);
          glActiveTexture(texture_unit);
          glBindTexture(GL_TEXTURE_2D, color_texture);
          glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
          set_copy_shader();
          glDisable(GL_DEPTH_TEST);
          glDisable(GL_CULL_FACE);
          render_rectangle(texture_unit,
            0, 0,
            save_w,  save_h,
            tw, th, -1);
          glBindTexture(GL_TEXTURE_2D, default_texture);
          //glPopAttrib();

          save_w = save_h = 0;
        }
      }
#endif
      render_to_texture = 0;
    }
    //glDrawBuffer(GL_BACK);
    break;
  case 6: // RENDER TO TEXTURE
    if(!render_to_texture)
    {
      savedWidth = width;
      savedHeight = height;
      savedWidtho = widtho;
      savedHeighto = heighto;
    }

    {
      if (!use_fbo) {
        //glMatrixMode(GL_MODELVIEW);
        //glLoadIdentity();
        //glTranslatef(0, 0, 1-zscale);
        //glScalef(1, 1, zscale);
        inverted_culling = 0;
      } else {
/*
        float m[4*4] = {1.0f, 0.0f, 0.0f, 0.0f,
          0.0f,-1.0f, 0.0f, 0.0f,
          0.0f, 0.0f, 1.0f, 0.0f,
          0.0f, 0.0f, 0.0f, 1.0f};
        glMatrixMode(GL_MODELVIEW);
        glLoadMatrixf(m);
        // VP z fix
        glTranslatef(0, 0, 1-zscale);
        glScalef(1, 1*1, zscale);
*/
        inverted_culling = 1;
        grCullMode(culling_mode);
      }
    }
    render_to_texture = 1;
    break;
  default:
    display_warning("grRenderBuffer : unknown buffer : %x", buffer);
  }
}

FX_ENTRY void FX_CALL
grAuxBufferExt( GrBuffer_t buffer )
{
  LOG("grAuxBufferExt(%d)\r\n", buffer);
  //display_warning("grAuxBufferExt");

  if (buffer == GR_BUFFER_AUXBUFFER) {
    invtex[0] = 0;
    invtex[1] = 0;
    need_to_compile = 0;
    set_depth_shader();
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_ALWAYS);
    glDisable(GL_CULL_FACE);
    //glDisable(GL_ALPHA_TEST);
    glDepthMask(GL_TRUE);
    grTexFilterMode(GR_TMU1, GR_TEXTUREFILTER_POINT_SAMPLED, GR_TEXTUREFILTER_POINT_SAMPLED);
  } else {
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    need_to_compile = 1;
  }
}
void vbo_draw();
FX_ENTRY void FX_CALL
grBufferClear( GrColor_t color, GrAlpha_t alpha, FxU32 depth )
{
  vbo_draw();
  LOG("grBufferClear(%d,%d,%d)\r\n", color, alpha, depth);
  switch(lfb_color_fmt)
  {
  case GR_COLORFORMAT_ARGB:
    glClearColor(((color >> 16) & 0xFF) / 255.0f,
      ((color >>  8) & 0xFF) / 255.0f,
      ( color        & 0xFF) / 255.0f,
      alpha / 255.0f);
    break;
  case GR_COLORFORMAT_RGBA:
    glClearColor(((color >> 24) & 0xFF) / 255.0f,
      ((color >> 16) & 0xFF) / 255.0f,
      (color         & 0xFF) / 255.0f,
      alpha / 255.0f);
    break;
  default:
    display_warning("grBufferClear: unknown color format : %x", lfb_color_fmt);
  }

  if (w_buffer_mode)
    glClearDepthf(1.0f - ((1.0f + (depth >> 4) / 4096.0f) * (1 << (depth & 0xF))) / 65528.0);
  else
    glClearDepthf(depth / 65535.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  // ZIGGY TODO check that color mask is on
  buffer_cleared = 1;

}

// #include <unistd.h>
FX_ENTRY void FX_CALL
grBufferSwap( FxU32 swap_interval )
{
//   GLuint program;

  vbo_draw();
//	glFinish();
//  printf("rendercallback is %p\n", renderCallback);
  if(renderCallback) {
//      glGetIntegerv(GL_CURRENT_PROGRAM, (GLint*) &program);
//      glUseProgramObjectARB(0);
      (*renderCallback)(1);
//      if (program)
//         glUseProgramObjectARB(program);
  }
  int i;
  LOG("grBufferSwap(%d)\r\n", swap_interval);
  //printf("swap\n");
  if (render_to_texture) {
    display_warning("swap while render_to_texture\n");
    return;
  }

  CoreVideo_GL_SwapBuffers();
  for (i = 0; i < nb_fb; i++)
    fbs[i].buff_clear = 1;

#ifdef VPDEBUG
  bufferswap_vpdebug();
#endif
}

// frame buffer

FX_ENTRY FxBool FX_CALL
grLfbLock( GrLock_t type, GrBuffer_t buffer, GrLfbWriteMode_t writeMode,
          GrOriginLocation_t origin, FxBool pixelPipeline,
          GrLfbInfo_t *info )
{
  LOG("grLfbLock(%d,%d,%d,%d,%d)\r\n", type, buffer, writeMode, origin, pixelPipeline);
  if (type == GR_LFB_WRITE_ONLY)
  {
    display_warning("grLfbLock : write only");
  }
  else
  {
    unsigned char *buf;
    int i,j;

    switch(buffer)
    {
    case GR_BUFFER_FRONTBUFFER:
      //glReadBuffer(GL_FRONT);
      break;
    case GR_BUFFER_BACKBUFFER:
      //glReadBuffer(GL_BACK);
      break;
    default:
      display_warning("grLfbLock : unknown buffer : %x", buffer);
    }

    if(buffer != GR_BUFFER_AUXBUFFER)
    {
      if (writeMode == GR_LFBWRITEMODE_888) {
        //printf("LfbLock GR_LFBWRITEMODE_888\n");
        info->lfbPtr = frameBuffer;
        info->strideInBytes = width*4;
        info->writeMode = GR_LFBWRITEMODE_888;
        info->origin = origin;
        //glReadPixels(0, viewport_offset, width, height, GL_BGRA, GL_UNSIGNED_BYTE, frameBuffer);
      } else {
        buf = (unsigned char*)malloc(width*height*4);

        info->lfbPtr = frameBuffer;
        info->strideInBytes = width*2;
        info->writeMode = GR_LFBWRITEMODE_565;
        info->origin = origin;
        glReadPixels(0, viewport_offset, width, height, GL_RGBA, GL_UNSIGNED_BYTE, buf);

        for (j=0; j<height; j++)
        {
          for (i=0; i<width; i++)
          {
            frameBuffer[(height-j-1)*width+i] =
              ((buf[j*width*4+i*4+0] >> 3) << 11) |
              ((buf[j*width*4+i*4+1] >> 2) <<  5) |
              (buf[j*width*4+i*4+2] >> 3);
          }
        }
        free(buf);
      }
    }
    else
    {
      info->lfbPtr = depthBuffer;
      info->strideInBytes = width*2;
      info->writeMode = GR_LFBWRITEMODE_ZA16;
      info->origin = origin;
      glReadPixels(0, viewport_offset, width, height, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT, depthBuffer);
    }
  }

  return FXTRUE;
}

FX_ENTRY FxBool FX_CALL
grLfbReadRegion( GrBuffer_t src_buffer,
                FxU32 src_x, FxU32 src_y,
                FxU32 src_width, FxU32 src_height,
                FxU32 dst_stride, void *dst_data )
{
  unsigned char *buf;
  unsigned int i,j;
  unsigned short *frameBuffer = (unsigned short*)dst_data;
  unsigned short *depthBuffer = (unsigned short*)dst_data;
  LOG("grLfbReadRegion(%d,%d,%d,%d,%d,%d)\r\n", src_buffer, src_x, src_y, src_width, src_height, dst_stride);

  switch(src_buffer)
  {
  case GR_BUFFER_FRONTBUFFER:
    //glReadBuffer(GL_FRONT);
    break;
  case GR_BUFFER_BACKBUFFER:
    //glReadBuffer(GL_BACK);
    break;
    /*case GR_BUFFER_AUXBUFFER:
    glReadBuffer(current_buffer);
    break;*/
  default:
    display_warning("grReadRegion : unknown buffer : %x", src_buffer);
  }

  if(src_buffer != GR_BUFFER_AUXBUFFER)
  {
    buf = (unsigned char*)malloc(src_width*src_height*4);

    glReadPixels(src_x, (viewport_offset)+height-src_y-src_height, src_width, src_height, GL_RGBA, GL_UNSIGNED_BYTE, buf);

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
    free(buf);
  }
  else
  {
    buf = (unsigned char*)malloc(src_width*src_height*2);

    glReadPixels(src_x, (viewport_offset)+height-src_y-src_height, src_width, src_height, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT, depthBuffer);

    for (j=0;j<src_height; j++)
    {
      for (i=0; i<src_width; i++)
      {
        depthBuffer[j*(dst_stride/2)+i] =
          ((unsigned short*)buf)[(src_height-j-1)*src_width*4+i*4];
      }
    }
    free(buf);
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
  unsigned char *buf;
  unsigned int i,j;
  unsigned short *frameBuffer = (unsigned short*)src_data;
  int texture_number;
  unsigned int tex_width = 1, tex_height = 1;
  LOG("grLfbWriteRegion(%d,%d,%d,%d,%d,%d,%d,%d)\r\n",dst_buffer, dst_x, dst_y, src_format, src_width, src_height, pixelPipeline, src_stride);

  //glPushAttrib(GL_ALL_ATTRIB_BITS);

  while (tex_width < src_width) tex_width <<= 1;
  while (tex_height < src_height) tex_height <<= 1;

  switch(dst_buffer)
  {
  case GR_BUFFER_BACKBUFFER:
    //glDrawBuffer(GL_BACK);
    break;
  case GR_BUFFER_AUXBUFFER:
    //glDrawBuffer(current_buffer);
    break;
  default:
    display_warning("grLfbWriteRegion : unknown buffer : %x", dst_buffer);
  }

  if(dst_buffer != GR_BUFFER_AUXBUFFER)
  {
    buf = (unsigned char*)malloc(tex_width*tex_height*4);

    texture_number = GL_TEXTURE0;
    glActiveTexture(texture_number);

    const unsigned int half_stride = src_stride / 2;
    switch(src_format)
    {
    case GR_LFB_SRC_FMT_1555:
      for (j=0; j<src_height; j++)
      {
        for (i=0; i<src_width; i++)
        {
          const unsigned int col = frameBuffer[j*half_stride+i];
          buf[j*tex_width*4+i*4+0]=((col>>10)&0x1F)<<3;
          buf[j*tex_width*4+i*4+1]=((col>>5)&0x1F)<<3;
          buf[j*tex_width*4+i*4+2]=((col>>0)&0x1F)<<3;
          buf[j*tex_width*4+i*4+3]= (col>>15) ? 0xFF : 0;
        }
      }
      break;
    case GR_LFBWRITEMODE_555:
      for (j=0; j<src_height; j++)
      {
        for (i=0; i<src_width; i++)
        {
          const unsigned int col = frameBuffer[j*half_stride+i];
          buf[j*tex_width*4+i*4+0]=((col>>10)&0x1F)<<3;
          buf[j*tex_width*4+i*4+1]=((col>>5)&0x1F)<<3;
          buf[j*tex_width*4+i*4+2]=((col>>0)&0x1F)<<3;
          buf[j*tex_width*4+i*4+3]=0xFF;
        }
      }
      break;
    case GR_LFBWRITEMODE_565:
      for (j=0; j<src_height; j++)
      {
        for (i=0; i<src_width; i++)
        {
          const unsigned int col = frameBuffer[j*half_stride+i];
          buf[j*tex_width*4+i*4+0]=((col>>11)&0x1F)<<3;
          buf[j*tex_width*4+i*4+1]=((col>>5)&0x3F)<<2;
          buf[j*tex_width*4+i*4+2]=((col>>0)&0x1F)<<3;
          buf[j*tex_width*4+i*4+3]=0xFF;
        }
      }
      break;
    default:
      display_warning("grLfbWriteRegion : unknown format : %d", src_format);
    }

#ifdef VPDEBUG
    if (dumping) {
      ilTexImage(tex_width, tex_height, 1, 4, IL_RGBA, IL_UNSIGNED_BYTE, buf);
      char name[128];
      static int id;
      sprintf(name, "dump/writecolor%d.png", id++);
      ilSaveImage(name);
      //printf("dumped gdLfbWriteRegion %s\n", name);
    }
#endif

    glBindTexture(GL_TEXTURE_2D, default_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, 4, tex_width, tex_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    free(buf);

    set_copy_shader();

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    render_rectangle(texture_number,
      dst_x, dst_y,
      src_width,  src_height,
      tex_width,  tex_height, +1);

  }
  else
  {
    float *buf = (float*)malloc(src_width*(src_height+(viewport_offset))*sizeof(float));

    if (src_format != GR_LFBWRITEMODE_ZA16)
      display_warning("unknown depth buffer write format:%x", src_format);

    if(dst_x || dst_y)
      display_warning("dst_x:%d, dst_y:%d\n",dst_x, dst_y);

    for (j=0; j<src_height; j++)
    {
      for (i=0; i<src_width; i++)
      {
        buf[(j+(viewport_offset))*src_width+i] =
          (frameBuffer[(src_height-j-1)*(src_stride/2)+i]/(65536.0f*(2.0f/zscale)))+1-zscale/2.0f;
      }
    }

#ifdef VPDEBUG
    if (dumping) {
      unsigned char * buf2 = (unsigned char *)malloc(src_width*(src_height+(viewport_offset)));
      for (i=0; i<src_width*src_height ; i++)
        buf2[i] = buf[i]*255.0f;
      ilTexImage(src_width, src_height, 1, 1, IL_LUMINANCE, IL_UNSIGNED_BYTE, buf2);
      char name[128];
      static int id;
      sprintf(name, "dump/writedepth%d.png", id++);
      ilSaveImage(name);
      //printf("dumped gdLfbWriteRegion %s\n", name);
      free(buf2);
    }
#endif

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_ALWAYS);

    //glDrawBuffer(GL_BACK);
    glClear( GL_DEPTH_BUFFER_BIT );
    glDepthMask(1);
    //glDrawPixels(src_width, src_height+(viewport_offset), GL_DEPTH_COMPONENT, GL_FLOAT, buf);

    free(buf);
  }
  //glDrawBuffer(current_buffer);
  //glPopAttrib();
  return FXTRUE;
}
