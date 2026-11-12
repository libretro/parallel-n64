#ifndef VK_SHADERS_H
#define VK_SHADERS_H

/* GLideN64's shaders, written directly as SPIR-V. C89. */

#include <stdint.h>
#include "vk_spirv.h"

#ifdef __cplusplus
extern "C" {
#endif

/* set 0 */
enum
{
   VKR_BIND_VALUES = 0,
   VKR_BIND_TEX0,
   VKR_BIND_TEX1,
   VKR_BIND_NOISE,
   VKR_BIND_DEPTH,
   VKR_BIND_MS0,
   VKR_BIND_MS1,
   VKR_BIND_ZLUT,
   VKR_BIND_TLUT,
   VKR_BIND_DEPTH_Z,
   VKR_BIND_DEPTH_DZ,
   VKR_BIND_COUNT
};

enum
{
   VKR_CF_ALPHA_C    = 1 << 0,
   VKR_CF_ALPHA_ABD  = 1 << 1,
   VKR_CF_COLOR_C    = 1 << 2,
   VKR_CF_COLOR_ABD  = 1 << 3,
   VKR_CF_TWO_STAGES = 1 << 4,
   VKR_CF_TEXTURED   = 1 << 5,
   VKR_CF_TILE0      = 1 << 6,
   VKR_CF_TILE1      = 1 << 7,
   VKR_CF_LOD        = 1 << 8,
   VKR_CF_HWLIGHT    = 1 << 9,
   VKR_CF_BILERP0    = 1 << 10,
   VKR_CF_BILERP1    = 1 << 11,
   VKR_CF_CYCLE_SHIFT = 12
};

/* the whole push block; the generic shader and the special ones read it */
typedef struct vkr_push
{
   int   ops[16];
   int   flags;
   int   clip_enable;
   /* triangles: moved by this much of the viewport, in clip space */
   float jitter[2];
   float special[4];
} vkr_push;

/* What the emulator's settings fix for as long as the shaders live. */
typedef struct vkr_shader_config
{
   int legacy_blending;
   int noise;
   int fragment_depth_write;
   int n64_depth_compare;
   int enable_lod;
   int filter_mode;     /* bilinear mode + halos removal * 2 */
   int three_point;     /* the bilinear mode alone is 3-point */
   int msaa;
   int interlock;
   int noise_width;
   int noise_height;
} vkr_shader_config;

/* A combiner: its stages as op * 256 + input, four words a stage, stage 0
 * and 1 are the first cycle's alpha and colour; INTER keeps its other two
 * inputs in the next two words; -1 ends a stage. */
typedef struct vkr_combiner_desc
{
   int ops[16];
   int flags;   /* VKR_CF_* and the cycle type */
} vkr_combiner_desc;

enum
{
   VKR_VS_TRIANGLE = 0,
   VKR_VS_RECT,
   VKR_VS_SPECIAL,   /* a rect with vTexCoord0 */
   VKR_VS_FXAA,
   VKR_VS_COUNT
};

enum
{
   VKR_FS_SHADOW_MAP = 0,
   VKR_FS_TEXRECT_DRAW,
   VKR_FS_TEXRECT_CLEAR,
   VKR_FS_TEXRECT_COPY,
   VKR_FS_GAMMA,
   VKR_FS_FXAA,
   VKR_FS_COUNT
};

/* NULL only if the module would not fit. Valid until the next call on @m. */
const uint32_t *vkr_write_combiner(spv_module *m, const vkr_shader_config *cfg,
      const vkr_combiner_desc *desc, int generic, unsigned *count);
const uint32_t *vkr_write_vertex(spv_module *m, const vkr_shader_config *cfg, int kind, unsigned *count);
const uint32_t *vkr_write_special(spv_module *m, const vkr_shader_config *cfg, int kind, unsigned *count);

#ifdef __cplusplus
}
#endif

#endif
