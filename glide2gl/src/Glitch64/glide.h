#ifndef __GLIDE_H__
#define __GLIDE_H__

#define GL_GLEXT_PROTOTYPES
#include "glitch64_n64_combiner.h"
#include <boolean.h>
#include <glsm/glsmsym.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
** -----------------------------------------------------------------------
** TYPE DEFINITIONS
** -----------------------------------------------------------------------
*/

/*
** -----------------------------------------------------------------------
** CONSTANTS AND TYPES
** -----------------------------------------------------------------------
*/
#define GR_TMU0         0x0
#define GR_TMU1         0x1

#define GR_BLEND_ZERO GL_ZERO
#define GR_BLEND_SRC_ALPHA GL_SRC_ALPHA
#define GR_BLEND_ONE GL_ONE
#define GR_BLEND_ONE_MINUS_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA


#define GR_CMP_LESS     GL_LESS
#define GR_CMP_LEQUAL   GL_LEQUAL
#define GR_CMP_GREATER  GL_GREATER
#define GR_CMP_GEQUAL   GL_EQUAL
#define GR_CMP_ALWAYS   GL_ALWAYS

#define GR_CULL_DISABLE         0x0
#define GR_CULL_NEGATIVE        0x1
#define GR_CULL_POSITIVE        0x2

#define GR_DEPTHBUFFER_DISABLE                  0x0
#define GR_DEPTHBUFFER_ZBUFFER                  0x1

#define GR_STIPPLE_DISABLE	0x0

#define GR_FOG_DISABLE                     0x0
#define GR_FOG_WITH_TABLE_ON_FOGCOORD_EXT  0x2


#define GR_TEXTURECLAMP_WRAP        GL_REPEAT
#define GR_TEXTURECLAMP_CLAMP       GL_CLAMP_TO_EDGE
#define GR_TEXTURECLAMP_MIRROR_EXT  GL_MIRRORED_REPEAT

#define GR_TEXTUREFILTER_POINT_SAMPLED  0x0
#define GR_TEXTUREFILTER_3POINT_LINEAR  0x1
#define GR_TEXTUREFILTER_BILINEAR       0x2

#define GR_TEXFMT_ALPHA_8               0x2 /* (0..0xFF) alpha     */
#define GR_TEXFMT_INTENSITY_8           0x3 /* (0..0xFF) intensity */
#define GR_TEXFMT_ALPHA_INTENSITY_44    0x4
#define GR_TEXFMT_RGB_565               0xa
#define GR_TEXFMT_ARGB_1555             0xb
#define GR_TEXFMT_ARGB_4444             0xc
#define GR_TEXFMT_ALPHA_INTENSITY_88    0xd

/*
** grDrawVertexArray/grDrawVertexArrayContiguous primitive type
*/
#define GR_TRIANGLE_STRIP                GL_TRIANGLE_STRIP
#define GR_TRIANGLE_FAN                  GL_TRIANGLE_FAN
#define GR_TRIANGLES                     GL_TRIANGLES

/*
** -----------------------------------------------------------------------
** STRUCTURES
** -----------------------------------------------------------------------
*/

/* a texture: its texels, width x height of them, both powers of two */
typedef struct {
    int32_t format;
    int               width;
    int               height;
    void              *data;
} GrTexInfo;


/*
** -----------------------------------------------------------------------
** FUNCTION PROTOTYPES
** -----------------------------------------------------------------------
*/

/*
** rendering functions
*/

void grDrawVertexArrayContiguous(uint32_t mode, uint32_t Count, void *pointers);

void grBufferClear(uint32_t color, uint32_t alpha, uint32_t depth);

void grBufferSwap(void);

/*
** SST routines
*/
uint32_t grSstWinOpen(void);

void grSstWinClose(void);

/*
** Glide configuration and special effect maintenance functions
*/

void grAlphaBlendFunction(uint32_t rgb_sf, uint32_t rgb_df, uint32_t alpha_sf, uint32_t alpha_df);

void  grAlphaTestFunction( int32_t function, uint8_t value, int set_alpha_ref);

void  grAlphaTestReferenceValue( uint8_t value );

void grClipWindow(uint32_t minx, uint32_t miny, uint32_t maxx, uint32_t maxy);

void grColorMask(bool rgb, bool a);

void grCullMode( int32_t mode );

/* The N64's combiner for the draws that follow (glitch64_n64_combiner.h);
 * and its inputs, each 0 to 1:
 * prim and env colours, K4 K5 PRIM_LOD_FRAC and the CPU's LOD_FRACTION,
 * the minimum and maximum levels with detail or sharpen and the noise
 * seed, the key's centre and scale. */
/* draws the triangles batched so far (Glitch64 does, at every change of
 * state and before the frame is read) */
void glitch64_flush(void);
void grN64Combine(const glitch64_n64_combiner *c);
void grN64Constants(const float prim[4], const float env[4], const float consts[4],
      const float level[4], const float key_center[3], const float key_scale[3]);

void grDepthBiasLevel( int32_t level );

void grDepthBufferFunction(uint32_t func);

void grDepthBufferMode(int32_t mode);

void grDepthMask(bool mask);

void grFogMode( int32_t mode );

void grStippleMode( int32_t mode );

/*
** texture mapping control functions
*/
/* the bytes a width x height texture of @fmt takes */
uint32_t grTexCalcMemRequired(int width, int height, int32_t fmt);

/* The textures Glitch64 holds, by slot: the cache's entries, then the
 * slots the framebuffer's images are drawn through */
#define GLITCH64_TEX_CACHE_SLOTS 4096
#define GLITCH64_TEX_FB_SLOTS    16
#define GLITCH64_TEX_SLOTS       (GLITCH64_TEX_CACHE_SLOTS + GLITCH64_TEX_FB_SLOTS)

/* binds texture @slot to @tmu; with @do_download, @info's texels are put
 * in it first, in place of what it held */
void grTexSource( int32_t tmu,
             uint32_t      slot,
             GrTexInfo  *info,
             int do_download);

void grTexFilterClampMode(
               int32_t tmu,
               int32_t s_clampmode,
               int32_t t_clampmode,
               int32_t minfilter_mode,
               int32_t magfilter_mode
               );

/*
** linear frame buffer functions
*/

/* a 16-bit colour image as it lies in RDRAM (5551, each word's two
 * halves swapped, as the host holds RDRAM), @src_stride bytes a row, put
 * on the frame at (dst_x, dst_y) as it is: past the pixel pipeline. Read
 * where it is: nothing is copied out first. */
void grLfbWriteRegion(uint32_t dst_x, uint32_t dst_y,
                  uint32_t src_width, uint32_t src_height,
                  int32_t src_stride, const void *src_data);

/* An N64 depth image into the depth buffer, colour untouched: @words its
 * w x h 16-bit depth words as they lie in RDRAM, drawn over the window's
 * [x0, x1) x [y0, y1), upper-left origin, at the depths the game's
 * vertices with those words as z would have. Without fragment depth
 * (GLSL ES 1.00 lacking GL_EXT_frag_depth) the depth buffer is cleared. */
void grDepthImageWrite(const void *words, uint32_t w, uint32_t h,
      float x0, float y0, float x1, float y1);

/* The window's pixels in [x, x + w) x [y, y + h), upper-left origin,
 * as RGBA8 rows *stride bytes apart, the bottom row first; the rect is
 * clipped to the window (*w, *h). Valid until the next read. */
const uint8_t *grLfbReadRect(int32_t *x, int32_t *y, int32_t *w, int32_t *h, uint32_t *stride);
/* GL with framebuffer blits: the scaled read below can be used; made at
 * load. */
int grLfbReadScaledInit(void);
/* The window's rect (x, y, w, h), top row first in window terms, scaled
 * on the GPU to dw x dh and read back as RGBA8 rows *stride bytes apart,
 * the bottom row first: dw x dh pixels read instead of the window's.
 * NULL where it cannot be done. Valid until the next read. */
const uint8_t *grLfbReadScaled(int32_t x, int32_t y, int32_t w, int32_t h,
      uint32_t dw, uint32_t dh, uint32_t *stride);

#define guFogGenerateLinear(nearZ, farZ) \
{ \
   fogStart = (nearZ) / 255.0f; \
   fogEnd = (farZ) / 255.0f; \
}

#define NUM_TMU 2

// Vertex structure
typedef struct
{
  float x, y, z, q;

  uint8_t  b;  // These values are arranged like this so that *(uint32_t*)(VERTEX+?) is
  uint8_t  g;  // ARGB format that glide can use.
  uint8_t  r;
  uint8_t  a;

  float coord[4];

  float f; //fog

  float u[2];
  float v[2];
  float w;
  uint16_t  flags;

  float vec[3]; // normal vector

  float sx, sy, sz;
  float x_w, y_w, z_w, oow;
  float u_w[2];
  float v_w[2];
  uint8_t  not_zclipped;
  uint8_t  screen_translated;
  uint8_t  uv_scaled;
  uint32_t uv_calculated;  // like crc

  float ou, ov;

  int   number;   // way to identify it
  int   scr_off, z_off; // off the screen?
} VERTEX;

extern int width, height;
extern float fogStart, fogEnd;

#ifdef __cplusplus
}
#endif

#endif /* __GLIDE_H__ */
