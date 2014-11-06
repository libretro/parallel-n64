#ifdef _WIN32
#include <windows.h>
#else // _WIN32
#include <stdlib.h>
#endif // _WIN32
#include "glide.h"
#include "main.h"
#include <stdio.h>
#include "glitchtextureman.h"

/* Napalm extensions to GrTextureFormat_t */
#define GR_TEXFMT_ARGB_CMP_FXT1           0x11
#define GR_TEXFMT_ARGB_8888               0x12
#define GR_TEXFMT_YUYV_422                0x13
#define GR_TEXFMT_UYVY_422                0x14
#define GR_TEXFMT_AYUV_444                0x15
#define GR_TEXFMT_ARGB_CMP_DXT1           0x16
#define GR_TEXFMT_ARGB_CMP_DXT2           0x17
#define GR_TEXFMT_ARGB_CMP_DXT3           0x18
#define GR_TEXFMT_ARGB_CMP_DXT4           0x19
#define GR_TEXFMT_ARGB_CMP_DXT5           0x1A
#define GR_TEXTFMT_RGB_888                0xFF

FX_ENTRY FxU32 FX_CALL
grTexMinAddress( GrChipID_t tmu )
{
  LOG("grTexMinAddress(%d)\r\n", tmu);
  if (UMAmode)
    return 0;
  else
    return tmu*TMU_SIZE;
}

FX_ENTRY FxU32 FX_CALL
grTexMaxAddress( GrChipID_t tmu )
{
  LOG("grTexMaxAddress(%d)\r\n", tmu);
  if (UMAmode)
    return TMU_SIZE*2 - 1;
  else
    return tmu*TMU_SIZE + TMU_SIZE - 1;
}

FX_ENTRY FxU32 FX_CALL
grTexTextureMemRequired( FxU32     evenOdd,
                        GrTexInfo *info   )
{
  int width, height;
  LOG("grTextureMemRequired(%d)\r\n", evenOdd);
  if (info->largeLodLog2 != info->smallLodLog2) display_warning("grTexTextureMemRequired : loading more than one LOD");

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
  case GR_TEXFMT_ARGB_CMP_DXT1:  // FXT1,DXT1,5 support - H.Morii
    return ((((width+0x3)&~0x3)*((height+0x3)&~0x3))>>1);
  case GR_TEXFMT_ARGB_CMP_DXT3:
    return ((width+0x3)&~0x3)*((height+0x3)&~0x3);
  case GR_TEXFMT_ARGB_CMP_DXT5:
    return ((width+0x3)&~0x3)*((height+0x3)&~0x3);
  case GR_TEXFMT_ARGB_CMP_FXT1:
    return ((((width+0x7)&~0x7)*((height+0x3)&~0x3))>>1);
  default:
    display_warning("grTexTextureMemRequired : unknown texture format: %x", info->format);
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
  if (lodmax != lodmin) display_warning("grTexCalcMemRequired : loading more than one LOD");

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
  case GR_TEXFMT_ARGB_1555:
  case GR_TEXFMT_ARGB_4444:
  case GR_TEXFMT_ALPHA_INTENSITY_88:
  case GR_TEXFMT_RGB_565:
    return (width*height)<<1;
  case GR_TEXFMT_ARGB_8888:
    return (width*height)<<2;
  case GR_TEXFMT_ARGB_CMP_DXT1:  // FXT1,DXT1,5 support - H.Morii
    return ((((width+0x3)&~0x3)*((height+0x3)&~0x3))>>1);
  case GR_TEXFMT_ARGB_CMP_DXT3:
    return ((width+0x3)&~0x3)*((height+0x3)&~0x3);
  case GR_TEXFMT_ARGB_CMP_DXT5:
    return ((width+0x3)&~0x3)*((height+0x3)&~0x3);
  case GR_TEXFMT_ARGB_CMP_FXT1:
    return ((((width+0x7)&~0x7)*((height+0x3)&~0x3))>>1);
  default:
    display_warning("grTexTextureMemRequired : unknown texture format: %x", fmt);
  }
  return 0;
}

int grTexFormatSize(int fmt)
{
  switch(fmt) {
  case GR_TEXFMT_ALPHA_8:
  case GR_TEXFMT_INTENSITY_8: // I8 support - H.Morii
  case GR_TEXFMT_ALPHA_INTENSITY_44:
    return 1;
  case GR_TEXFMT_RGB_565:
  case GR_TEXFMT_ARGB_1555:
  case GR_TEXFMT_ALPHA_INTENSITY_88:
  case GR_TEXFMT_ARGB_4444:
    return 2;
  case GR_TEXFMT_ARGB_8888:
    return 4;
  case GR_TEXFMT_ARGB_CMP_DXT1:  // FXT1,DXT1,5 support - H.Morii
  case GR_TEXFMT_ARGB_CMP_FXT1:
    return 8;                  // HACKALERT for DXT1: factor holds block bytes
  case GR_TEXFMT_ARGB_CMP_DXT3:  // FXT1,DXT1,5 support - H.Morii
  case GR_TEXFMT_ARGB_CMP_DXT5:
    return 16;                  // HACKALERT for DXT3: factor holds block bytes
  default:
    display_warning("grTexFormatSize : unknown texture format: %x", fmt);
  }
  return -1;
}

FX_ENTRY void FX_CALL
grTexLodBiasValue(GrChipID_t tmu, float bias )
{
  LOG("grTexLodBiasValue(%d,%f)\r\n", tmu, bias);
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
      display_warning("grTexDetailControl : %d, %d, %f", lod_bias, detail_scale, detail_max);
  }
  lambda = detail_max;
  if(lambda > 1.0f)
  {
    lambda = 1.0f - (255.0f - lambda);
  }
  if(lambda > 1.0f) display_warning("lambda:%f", lambda);

  set_lambda();
}
