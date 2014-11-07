#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif // _WIN32
#include "glide.h"
#include "main.h"
#include "../Glide64/winlnxdefs.h"
#include "../Glide64/rdp.h"

#ifdef ANDROID_EDITION
#include "ae_imports.h"
static float polygonOffsetFactor;
static float polygonOffsetUnits;
#endif

extern int xy_off;
extern int xy_en;
extern int z_en;
extern int z_off;
extern int q_off;
extern int q_en;
extern int pargb_off;
extern int pargb_en;
extern int st0_off;
extern int st0_en;
extern int st1_off;
extern int st1_en;
extern int fog_ext_off;
extern int fog_ext_en;

FX_ENTRY void FX_CALL
grCoordinateSpace( GrCoordinateSpaceMode_t mode )
{
  LOG("grCoordinateSpace(%d)\r\n", mode);
  switch(mode)
  {
  case GR_WINDOW_COORDS:
    break;
  default:
    display_warning("unknwown coordinate space : %x", mode);
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
    display_warning("unknown grVertexLayout parameter : %x", param);
  }
}

FX_ENTRY void FX_CALL
grCullMode( GrCullMode_t mode )
{
  LOG("grCullMode(%d)\r\n", mode);
  static int oldmode = -1, oldinv = -1;
  int enable_culling = 0;
  GLenum culling_mode = GL_BACK;
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
       culling_mode = GL_FRONT;
    enable_culling = 1;
    break;
  case GR_CULL_POSITIVE:
    if (inverted_culling)
       culling_mode = GL_FRONT;
    enable_culling = 1;
    break;
  default:
    display_warning("unknown cull mode : %x", mode);
  }

  if (enable_culling)
  {
     glCullFace(culling_mode);
     glEnable(GL_CULL_FACE);
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
    w_buffer_mode = 0;
    return;
  case GR_DEPTHBUFFER_WBUFFER:
  case GR_DEPTHBUFFER_WBUFFER_COMPARE_TO_BIAS:
    glEnable(GL_DEPTH_TEST);
    w_buffer_mode = 1;
    break;
  case GR_DEPTHBUFFER_ZBUFFER:
  case GR_DEPTHBUFFER_ZBUFFER_COMPARE_TO_BIAS:
    glEnable(GL_DEPTH_TEST);
    w_buffer_mode = 0;
    break;
  default:
    display_warning("unknown depth buffer mode : %x", mode);
  }
}

FX_ENTRY void FX_CALL
grDepthBufferFunction( GrCmpFnc_t function )
{
  LOG("grDepthBufferFunction(%d)\r\n", function);
  switch(function)
  {
  case GR_CMP_GEQUAL:
    if (w_buffer_mode)
      glDepthFunc(GL_LEQUAL);
    else
      glDepthFunc(GL_GEQUAL);
    break;
  case GR_CMP_LEQUAL:
    if (w_buffer_mode)
      glDepthFunc(GL_GEQUAL);
    else
      glDepthFunc(GL_LEQUAL);
    break;
  case GR_CMP_LESS:
    if (w_buffer_mode)
      glDepthFunc(GL_GREATER);
    else
      glDepthFunc(GL_LESS);
    break;
  case GR_CMP_ALWAYS:
    glDepthFunc(GL_ALWAYS);
    break;
  case GR_CMP_EQUAL:
    glDepthFunc(GL_EQUAL);
    break;
  case GR_CMP_GREATER:
    if (w_buffer_mode)
      glDepthFunc(GL_LESS);
    else
      glDepthFunc(GL_GREATER);
    break;
  case GR_CMP_NEVER:
    glDepthFunc(GL_NEVER);
    break;
  case GR_CMP_NOTEQUAL:
    glDepthFunc(GL_NOTEQUAL);
    break;

  default:
    display_warning("unknown depth buffer function : %x", function);
  }
}

FX_ENTRY void FX_CALL
grDepthMask( FxBool mask )
{
  LOG("grDepthMask(%d)\r\n", mask);
  glDepthMask(mask);
}

FX_ENTRY void FX_CALL
grDepthBiasLevel( FxI32 level )
{
  LOG("grDepthBiasLevel(%d)\r\n", level);
  if (level)
  {
    #ifdef ANDROID_EDITION
    glPolygonOffset(polygonOffsetFactor, polygonOffsetUnits);
    #elif defined(__LIBRETRO__)
    extern float polygonOffsetFactor;
    extern float polygonOffsetUnits;
    glPolygonOffset(polygonOffsetFactor, (float)level * settings.depth_bias * 0.01 );
    #else
    if(w_buffer_mode)
      glPolygonOffset(1.0f, -(float)level*zscale/255.0f);
    else
      glPolygonOffset(0, (float)level*biasFactor);
    #endif
    glEnable(GL_POLYGON_OFFSET_FILL);
  }
  else
  {
    glPolygonOffset(0,0);
    glDisable(GL_POLYGON_OFFSET_FILL);
  }
}
