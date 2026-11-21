#ifndef GLITCH64_VK_SHADERS_H
#define GLITCH64_VK_SHADERS_H

/* Glitch64's shaders on Vulkan, written as SPIR-V (vk_spirv.h). C89.
 *
 * What the GL shaders of glitch64_combiner.c do, in one program: the N64
 * combiner reads its inputs by the indices pushed with each draw (ops),
 * the rest is switched by the pushed flags. */

#include <stdint.h>
#include "vk_spirv.h"
#include "glitch64_n64_combiner.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The uniform block, as it lies in memory (std140: vec4s only). */
typedef struct glitch64_vk_values
{
   float prim[4];
   float env[4];
   float consts[4];      /* K4, K5, PRIM_LOD_FRAC, the CPU's LOD_FRACTION */
   float level[4];       /* min level, max level, detail or sharpen, noise seed */
   float key_center[4];
   float key_scale[4];
   float fog_color[4];   /* rgb, and the fog alpha */
   float tex_sizes[4];   /* what the texture coordinates are divided by: tile 0, tile 1 */
   float exact_sizes[4]; /* the textures' sizes in texels: tile 0, tile 1 */
   float window[4];      /* half the window's width and height, y's sign */
   float test[4];        /* w: the alpha test's reference, -1 none */
   float blend_color[4]; /* the blender's blend colour */
   float convert[4];     /* K0 to K3, 2k + 1 over 256 (GLITCH64_N64_CONVERT1) */
} glitch64_vk_values;


/* ops[8 * cycle + slot]: slots 0-3 the colour's a, b, c, d, 4-7 the alpha's */
enum
{
   G64VK_TWO_CYCLE = 1 << 0,
   G64VK_BLEND0    = 1 << 1,  /* the blender's first cycle in the shader */
   G64VK_BLEND1    = 1 << 2,  /* its last cycle whole in the shader */
   G64VK_LOD_PIXEL = 1 << 3,  /* LOD_FRACTION from the derivatives */
   G64VK_CONVERT1  = 1 << 4,  /* tile 1's texel is tile 0's through K0-K3 */
   G64VK_DITHER    = 1 << 5,
   G64VK_3POINT0   = 1 << 6,
   G64VK_3POINT1   = 1 << 7,
   G64VK_COPY      = 1 << 8,  /* tile 0's texel as it is: an LFB write */
   G64VK_NOISE     = 1 << 9,
   G64VK_CVG_ALPHA = 1 << 10, /* the pixel's alpha into the blender is 1 */
   G64VK_CONVERT1_Y = 1 << 11 /* tile 1's texel is tile 0's blue */
};
/* the blender's cycles (GLITCH64_BL) in the flags' top half */
#define G64VK_BLEND0_SHIFT 16
#define G64VK_BLEND1_SHIFT 24
#define G64VK_BLEND_BYTES  0xffff0000u

/* the vertex: Glitch64's VBufVertex */
#define G64VK_VERTEX_STRIDE 36
#define G64VK_OFF_POS    0
#define G64VK_OFF_COLOR  16  /* b, g, r, a bytes */
#define G64VK_OFF_TC0    20  /* tile 0's coordinates */
#define G64VK_OFF_TC1    28  /* tile 1's */

/* NULL if it did not fit. Valid until the next call on @m. */
const uint32_t *glitch64_vk_vertex(spv_module *m, unsigned *count);
/* @depth: the depth image's shader, writing the depth tile 0 holds at
 * each texel, else the combiner's: with @key (ops[16], then the flags)
 * that one combiner's own, its choices made while writing; NULL reads
 * them as pushed. */
#define G64VK_KEY_WORDS 17
const uint32_t *glitch64_vk_fragment(spv_module *m, int depth, const int *key, unsigned *count);
/* the combiner's own shader, as the renderer's worker writes it */
const uint32_t *glitch64_vk_write_own(spv_module *m, const int *key, unsigned *count);

#ifdef __cplusplus
}
#endif

#endif
