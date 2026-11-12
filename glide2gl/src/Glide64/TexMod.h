#ifndef GLIDE64_TEXMOD_H
#define GLIDE64_TEXMOD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* texture MOD types */
#define TMOD_TEX_INTER_COLOR_USING_FACTOR                      1
#define TMOD_TEX_INTER_COL_USING_COL1                          2
#define TMOD_FULL_COLOR_SUB_TEX                                3
#define TMOD_COL_INTER_COL1_USING_TEX                          4
#define TMOD_COL_INTER_COL1_USING_TEXA                         5
#define TMOD_COL_INTER_COL1_USING_TEXA__MUL_TEX                6
#define TMOD_COL_INTER_TEX_USING_TEXA                          7
#define TMOD_COL2_INTER__COL_INTER_COL1_USING_TEX__USING_TEXA  8
#define TMOD_TEX_SCALE_FAC_ADD_FAC                             9
#define TMOD_TEX_SUB_COL_MUL_FAC_ADD_TEX                       10
#define TMOD_TEX_SCALE_COL_ADD_COL                             11
#define TMOD_TEX_ADD_COL                                       12
#define TMOD_TEX_SUB_COL                                       13
#define TMOD_TEX_SUB_COL_MUL_FAC                               14
#define TMOD_COL_INTER_TEX_USING_COL1                          15
#define TMOD_COL_MUL_TEXA_ADD_TEX                              16
#define TMOD_COL_INTER_TEX_USING_TEX                           17
#define TMOD_TEX_INTER_NOISE_USING_COL                         18
#define TMOD_TEX_INTER_COL_USING_TEXA                          19
#define TMOD_TEX_MUL_COL                                       20
#define TMOD_TEX_SCALE_FAC_ADD_COL                             21

/* The texture formats the loaders give (glide.h, GlideExtensions.h) */
#define GR_TEXFMT_ALPHA_8               0x2
#define GR_TEXFMT_ALPHA_INTENSITY_44    0x4
#define GR_TEXFMT_RGB_565               0xa
#define GR_TEXFMT_ARGB_1555             0xb
#define GR_TEXFMT_ARGB_4444             0xc
#define GR_TEXFMT_ALPHA_INTENSITY_88    0xd
#define GR_TEXFMT_ARGB_8888             0x12

/* @count texels of format @texfmt at @src as ARGB 8888 in @dst, every
 * channel widened by repeating its bits, none narrowed. @dst may be @src
 * only for GR_TEXFMT_ARGB_8888. Returns 0 for a format it does not know. */
int glide64_tex_to_8888(unsigned texfmt, const void *src, uint32_t *dst, int count);

/* Texture modification @mod on @count ARGB 8888 texels, eight bits a
 * channel throughout. The colours are RGBA 8888, the factor 0..255. */
void glide64_tex_mod_8888(uint32_t *tex, int count, unsigned mod,
      uint32_t modcolor, uint32_t modcolor1, uint32_t modcolor2, uint32_t modfactor);

#ifdef __cplusplus
}
#endif

#endif
