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
extern unsigned long glitch_fullscreen;

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

// unused by glide64

FX_ENTRY FxI32 FX_CALL
grQueryResolutions( const GrResolution *resTemplate, GrResolution *output )
{
  int res_inf = 0;
  int res_sup = 0xf;
  int i;
  int n=0;
  LOG("grQueryResolutions\r\n");
  display_warning("grQueryResolutions");
  if ((unsigned int)resTemplate->resolution != GR_QUERY_ANY)
  {
    res_inf = res_sup = resTemplate->resolution;
  }
  if ((unsigned int)resTemplate->refresh == GR_QUERY_ANY) display_warning("querying any refresh rate");
  if ((unsigned int)resTemplate->numAuxBuffers == GR_QUERY_ANY) display_warning("querying any numAuxBuffers");
  if ((unsigned int)resTemplate->numColorBuffers == GR_QUERY_ANY) display_warning("querying any numColorBuffers");

  if (output == NULL) return res_sup - res_inf + 1;
  for (i=res_inf; i<=res_sup; i++)
  {
    output[n].resolution = i;
    output[n].refresh = resTemplate->refresh;
    output[n].numAuxBuffers = resTemplate->numAuxBuffers;
    output[n].numColorBuffers = resTemplate->numColorBuffers;
    n++;
  }
  return res_sup - res_inf + 1;
}

FX_ENTRY FxBool FX_CALL
grReset( FxU32 what )
{
  display_warning("grReset");
  return 1;
}

FX_ENTRY void FX_CALL
grEnable( GrEnableMode_t mode )
{
  LOG("grEnable(%d)\r\n", mode);
  if (mode == GR_TEXTURE_UMA_EXT)
    UMAmode = 1;
}

FX_ENTRY void FX_CALL
grDisable( GrEnableMode_t mode )
{
  LOG("grDisable(%d)\r\n", mode);
  if (mode == GR_TEXTURE_UMA_EXT)
    UMAmode = 0;
}

FX_ENTRY void FX_CALL
grDisableAllEffects( void )
{
  display_warning("grDisableAllEffects");
}

FX_ENTRY void FX_CALL
grErrorSetCallback( GrErrorCallbackFnc_t fnc )
{
  display_warning("grErrorSetCallback");
}

FX_ENTRY void FX_CALL
grFinish(void)
{
  display_warning("grFinish");
}

FX_ENTRY void FX_CALL
grFlush(void)
{
  display_warning("grFlush");
}

FX_ENTRY void FX_CALL
grTexMultibase( GrChipID_t tmu,
               FxBool     enable )
{
  display_warning("grTexMultibase");
}

FX_ENTRY void FX_CALL
grTexMipMapMode( GrChipID_t     tmu,
                GrMipMapMode_t mode,
                FxBool         lodBlend )
{
  display_warning("grTexMipMapMode");
}

FX_ENTRY void FX_CALL
grTexDownloadTablePartial( GrTexTable_t type,
                          void         *data,
                          int          start,
                          int          end )
{
  display_warning("grTexDownloadTablePartial");
}

FX_ENTRY void FX_CALL
grTexDownloadTable( GrTexTable_t type,
                   void         *data )
{
  display_warning("grTexDownloadTable");
}

FX_ENTRY FxBool FX_CALL
grTexDownloadMipMapLevelPartial( GrChipID_t        tmu,
                                FxU32             startAddress,
                                GrLOD_t           thisLod,
                                GrLOD_t           largeLod,
                                GrAspectRatio_t   aspectRatio,
                                GrTextureFormat_t format,
                                FxU32             evenOdd,
                                void              *data,
                                int               start,
                                int               end )
{
  display_warning("grTexDownloadMipMapLevelPartial");
  return 1;
}

FX_ENTRY void FX_CALL
grTexDownloadMipMapLevel( GrChipID_t        tmu,
                         FxU32             startAddress,
                         GrLOD_t           thisLod,
                         GrLOD_t           largeLod,
                         GrAspectRatio_t   aspectRatio,
                         GrTextureFormat_t format,
                         FxU32             evenOdd,
                         void              *data )
{
  display_warning("grTexDownloadMipMapLevel");
}

FX_ENTRY void FX_CALL
grTexNCCTable( GrNCCTable_t table )
{
  display_warning("grTexNCCTable");
}

FX_ENTRY void FX_CALL
grViewport( FxI32 x, FxI32 y, FxI32 width, FxI32 height )
{
  display_warning("grViewport");
}

FX_ENTRY void FX_CALL
grDepthRange( FxFloat n, FxFloat f )
{
  display_warning("grDepthRange");
}

FX_ENTRY void FX_CALL
grSplash(float x, float y, float width, float height, FxU32 frame)
{
  display_warning("grSplash");
}

FX_ENTRY FxBool FX_CALL
grSelectContext( GrContext_t context )
{
  display_warning("grSelectContext");
  return 1;
}

FX_ENTRY void FX_CALL
grAADrawTriangle(
                 const void *a, const void *b, const void *c,
                 FxBool ab_antialias, FxBool bc_antialias, FxBool ca_antialias
                 )
{
  display_warning("grAADrawTriangle");
}

FX_ENTRY void FX_CALL
grAlphaControlsITRGBLighting( FxBool enable )
{
  display_warning("grAlphaControlsITRGBLighting");
}

FX_ENTRY void FX_CALL
grGlideSetVertexLayout( const void *layout )
{
  display_warning("grGlideSetVertexLayout");
}

FX_ENTRY void FX_CALL
grGlideGetVertexLayout( void *layout )
{
  display_warning("grGlideGetVertexLayout");
}

FX_ENTRY void FX_CALL
grGlideSetState( const void *state )
{
  display_warning("grGlideSetState");
}

FX_ENTRY void FX_CALL
grGlideGetState( void *state )
{
  display_warning("grGlideGetState");
}

FX_ENTRY void FX_CALL
grLfbWriteColorFormat(GrColorFormat_t colorFormat)
{
  display_warning("grLfbWriteColorFormat");
}

FX_ENTRY void FX_CALL
grLfbWriteColorSwizzle(FxBool swizzleBytes, FxBool swapWords)
{
  display_warning("grLfbWriteColorSwizzle");
}

FX_ENTRY void FX_CALL
grLfbConstantDepth( FxU32 depth )
{
  display_warning("grLfbConstantDepth");
}

FX_ENTRY void FX_CALL
grLfbConstantAlpha( GrAlpha_t alpha )
{
  display_warning("grLfbConstantAlpha");
}

FX_ENTRY void FX_CALL
grTexMultibaseAddress( GrChipID_t       tmu,
                      GrTexBaseRange_t range,
                      FxU32            startAddress,
                      FxU32            evenOdd,
                      GrTexInfo        *info )
{
  display_warning("grTexMultibaseAddress");
}

/*
inline void MySleep(FxU32 ms)
{
#ifdef _WIN32
  Sleep(ms);
#else
  SDL_Delay(ms);
#endif
}
*/

#ifdef _WIN32
static void CorrectGamma(LPVOID apGammaRamp)
{
  HDC hdc = GetDC(NULL);
  if (hdc != NULL)
  {
    SetDeviceGammaRamp(hdc, apGammaRamp);
    ReleaseDC(NULL, hdc);
  }
}
#else
static void CorrectGamma(const FxU16 aGammaRamp[3][256])
{
  //TODO?
  //int res = SDL_SetGammaRamp(aGammaRamp[0], aGammaRamp[1], aGammaRamp[2]);
  //LOG("SDL_SetGammaRamp returned %d\r\n", res);
}
#endif

FX_ENTRY void FX_CALL
grLoadGammaTable( FxU32 nentries, FxU32 *red, FxU32 *green, FxU32 *blue)
{
  LOG("grLoadGammaTable\r\n");
  if (!glitch_fullscreen)
    return;
  FxU16 aGammaRamp[3][256];
  for (int i = 0; i < 256; i++)
  {
    aGammaRamp[0][i] = (FxU16)((red[i] << 8) & 0xFFFF);
    aGammaRamp[1][i] = (FxU16)((green[i] << 8) & 0xFFFF);
    aGammaRamp[2][i] = (FxU16)((blue[i] << 8) & 0xFFFF);
  }
  CorrectGamma(aGammaRamp);
  //MySleep(1000); //workaround for Mupen64
}

FX_ENTRY void FX_CALL
grGetGammaTableExt(FxU32 nentries, FxU32 *red, FxU32 *green, FxU32 *blue)
{
  return;
  //TODO?
  /*
  LOG("grGetGammaTableExt()\r\n");
  FxU16 aGammaRamp[3][256];
#ifdef _WIN32
  HDC hdc = GetDC(NULL);
  if (hdc == NULL)
    return;
  if (GetDeviceGammaRamp(hdc, aGammaRamp) == TRUE)
  {
    ReleaseDC(NULL, hdc);
#else
  if (SDL_GetGammaRamp(aGammaRamp[0], aGammaRamp[1], aGammaRamp[2]) != -1)
  {
#endif
    for (int i = 0; i < 256; i++)
    {
      red[i] = aGammaRamp[0][i] >> 8;
      green[i] = aGammaRamp[1][i] >> 8;
      blue[i] = aGammaRamp[2][i] >> 8;
    }
  }
  */
}

FX_ENTRY void FX_CALL
guGammaCorrectionRGB( FxFloat gammaR, FxFloat gammaG, FxFloat gammaB )
{
  LOG("guGammaCorrectionRGB()\r\n");
  if (!glitch_fullscreen)
    return;
  FxU16 aGammaRamp[3][256];
  for (int i = 0; i < 256; i++)
  {
    aGammaRamp[0][i] = (((FxU16)((pow(i/255.0F, 1.0F/gammaR)) * 255.0F + 0.5F)) << 8) & 0xFFFF;
    aGammaRamp[1][i] = (((FxU16)((pow(i/255.0F, 1.0F/gammaG)) * 255.0F + 0.5F)) << 8) & 0xFFFF;
    aGammaRamp[2][i] = (((FxU16)((pow(i/255.0F, 1.0F/gammaB)) * 255.0F + 0.5F)) << 8) & 0xFFFF;
  }
  CorrectGamma(aGammaRamp);
}

FX_ENTRY void FX_CALL
grDitherMode( GrDitherMode_t mode )
{
  display_warning("grDitherMode");
}

void grChromaRangeExt(GrColor_t color0, GrColor_t color1, FxU32 mode)
{
  display_warning("grChromaRangeExt");
}

void grChromaRangeModeExt(GrChromakeyMode_t mode)
{
  display_warning("grChromaRangeModeExt");
}

void grTexChromaRangeExt(GrChipID_t tmu, GrColor_t color0, GrColor_t color1, GrTexChromakeyMode_t mode)
{
  display_warning("grTexChromaRangeExt");
}

void grTexChromaModeExt(GrChipID_t tmu, GrChromakeyMode_t mode)
{
  display_warning("grTexChromaRangeModeExt");
}
