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

extern int use_fbo;
extern int screen_width, screen_height;

FX_ENTRY void FX_CALL
grSstOrigin(GrOriginLocation_t  origin)
{
  LOG("grSstOrigin(%d)\r\n", origin);
  if (origin != GR_ORIGIN_UPPER_LEFT)
    display_warning("grSstOrigin : %x", origin);
}

FX_ENTRY void FX_CALL
grClipWindow( FxU32 minx, FxU32 miny, FxU32 maxx, FxU32 maxy )
{
   GLint new_y = (viewport_offset)+height-maxy;

  if (use_fbo && render_to_texture) {
    if (int(minx) < 0) minx = 0;
    if (int(miny) < 0) miny = 0;
    if (maxx < minx) maxx = minx;
    if (maxy < miny) maxy = miny;
    new_y = miny;
    goto setscissor;
  }
  else if (!use_fbo) {
    int th = height;
    if (th > screen_height)
      th = screen_height;
    maxy = th - maxy;
    miny = th - miny;
    FxU32 tmp = maxy;
    maxy = miny;
    miny = tmp;
    if (maxx > (FxU32) width) maxx = width;
    if (maxy > (FxU32) height) maxy = height;
    if (int(minx) < 0) minx = 0;
    if (int(miny) < 0) miny = 0;
    if (maxx < minx) maxx = minx;
    if (maxy < miny) maxy = miny;
    new_y = miny+viewport_offset;
    //printf("gl scissor %d %d %d %d\n", minx, miny, maxx, maxy);
  }
  
setscissor:
  LOG("grClipWindow(%d,%d,%d,%d)\r\n", minx, miny, maxx, maxy);
  glScissor(minx, new_y, maxx - minx, maxy - miny);
  glEnable(GL_SCISSOR_TEST);
}

FX_ENTRY void FX_CALL
grColorMask( FxBool rgb, FxBool a )
{
  LOG("grColorMask(%d, %d)\r\n", rgb, a);
  glColorMask(rgb, rgb, rgb, a);
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
  return 0;
  const GLubyte *extensions = NULL;
  const GLubyte *start;
  GLubyte *where, *terminator;

  where = (GLubyte *)strchr(extension, ' ');
  if (where || *extension == '\0')
    return 0;

  extensions = glGetString(GL_EXTENSIONS);

  start = extensions;
  for (;;)
  {
    where = (GLubyte *) strstr((const char *) start, extension);
    if (!where)
      break;

    terminator = where + strlen(extension);
    if (where == start || *(where - 1) == ' ')
      if (*terminator == ' ' || *terminator == '\0')
        return 1;

    start = terminator;
  }

  return 0;
}

FX_ENTRY GrContext_t FX_CALL
grSstWinOpenExt(
                HWND                 hWnd,
                GrScreenResolution_t screen_resolution,
                GrScreenRefresh_t    refresh_rate,
                GrColorFormat_t      color_format,
                GrOriginLocation_t   origin_location,
                GrPixelFormat_t      pixelformat,
                int                  nColBuffers,
                int                  nAuxBuffers)
{
  LOG("grSstWinOpenExt(%d, %d, %d, %d, %d, %d %d)\r\n", hWnd, screen_resolution, refresh_rate, color_format, origin_location, nColBuffers, nAuxBuffers);
  return grSstWinOpen(hWnd, screen_resolution, refresh_rate, color_format,
    origin_location, nColBuffers, nAuxBuffers);
}

FX_ENTRY void FX_CALL
grGlideShutdown( void )
{
  LOG("grGlideShutdown\r\n");
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
    if (!nbTextureUnits)
    {
      grSstWinOpen((unsigned long)NULL, GR_RESOLUTION_640x480 | 0x80000000, 0, GR_COLORFORMAT_ARGB,
        GR_ORIGIN_UPPER_LEFT, 2, 1);
      grSstWinClose(0);
    }
#ifdef VOODOO1
    params[0] = 1;
#else
    if (nbTextureUnits > 2)
      params[0] = 2;
    else
      params[0] = 1;
#endif
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
    params[0] = 16*1024*1024*nbTextureUnits;
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
    display_warning("unknown pname in grGet : %x", pname);
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
    display_warning("unknown grGetString selector : %x", pname);
  }
  return NULL;
}

FX_ENTRY FxBool FX_CALL
grLfbUnlock( GrLock_t type, GrBuffer_t buffer )
{
  LOG("grLfbUnlock(%d,%d)\r\n", type, buffer);
  if (type == GR_LFB_WRITE_ONLY)
  {
    display_warning("grLfbUnlock : write only");
  }
  return FXTRUE;
}

/* wrapper-specific glide extensions */

FX_ENTRY char ** FX_CALL
grQueryResolutionsExt(FxI32 * Size)
{
  return 0;
/*
  LOG("grQueryResolutionsExt\r\n");
  return g_FullScreenResolutions.getResolutionsList(Size);
*/
}

FX_ENTRY GrScreenResolution_t FX_CALL grWrapperFullScreenResolutionExt(FxU32* width, FxU32* height)
{
  return 0;
/*
  LOG("grWrapperFullScreenResolutionExt\r\n");
  g_FullScreenResolutions.getResolution(config.res, width, height);
  return config.res;
*/
}

FX_ENTRY FxBool FX_CALL grKeyPressedExt(FxU32 key)
{
  return 0;
/*
#ifdef _WIN32
  return (GetAsyncKeyState(key) & 0x8000);
#else
  if (key == 1) //LBUTTON
  {
    Uint8 mstate = SDL_GetMouseState(NULL, NULL);
    return (mstate & SDL_BUTTON_LMASK);
  }
  else
  {
    Uint8 *keystates = SDL_GetKeyState( NULL );
    if( keystates[ key ] )
    {
      return 1;
    }
    else
    {
      return 0;
    }
  }
#endif
*/
}

FX_ENTRY void FX_CALL grConfigWrapperExt(FxI32 resolution, FxI32 vram, FxBool fbo, FxBool aniso)
{
  LOG("grConfigWrapperExt\r\n");
  config.res = resolution;
  config.vram_size = vram;
  config.fbo = fbo;
  config.anisofilter = aniso;
}
