/* al_select.h -- the key that identifies one per-pixel pipeline.
 *
 * Every field here is read by the per-pixel path and is fixed for a
 * whole primitive, so a span renderer generated for one key contains no
 * test of any of them. The set was taken from the mode fields the
 * per-pixel functions - the combiner, the texture pipeline, the depth
 * test, the blender, the dither - actually branch on, plus the
 * framebuffer format and size the write path selects on.
 *
 * Two keys that differ do not necessarily need different code; two keys
 * that are equal always run the same code. That is the only property
 * required of it.
 */

#ifndef AL_SELECT_H
#define AL_SELECT_H

#include <stdint.h>

struct al_selector
{
    uint64_t key;
};

/* bit positions, low word */
#define AL_SEL_CYCLE_TYPE      0   /* 2 */
#define AL_SEL_EN_TLUT         2
#define AL_SEL_TLUT_TYPE       3
#define AL_SEL_SAMPLE_TYPE     4
#define AL_SEL_MID_TEXEL       5
#define AL_SEL_BI_LERP0        6
#define AL_SEL_BI_LERP1        7
#define AL_SEL_CONVERT_ONE     8
#define AL_SEL_KEY_EN          9
#define AL_SEL_RGB_DITHER     10   /* 2 */
#define AL_SEL_ALPHA_DITHER   12   /* 2 */
#define AL_SEL_FORCE_BLEND    14
#define AL_SEL_ALPHA_CVG_SEL  15
#define AL_SEL_CVG_TIMES_A    16
#define AL_SEL_Z_MODE         17   /* 2 */
#define AL_SEL_CVG_DEST       19   /* 2 */
#define AL_SEL_COLOR_ON_CVG   21
#define AL_SEL_IMAGE_READ     22
#define AL_SEL_Z_UPDATE       23
#define AL_SEL_Z_COMPARE      24
#define AL_SEL_ANTIALIAS      25
#define AL_SEL_Z_SOURCE       26
#define AL_SEL_DITHER_ALPHA   27
#define AL_SEL_ALPHA_COMPARE  28
#define AL_SEL_FB_SIZE        29   /* 2 */
#define AL_SEL_FB_FORMAT      31   /* 3 */
/* the combiner's two command words decide the mux, and the blender's
 * source selection with them: both are folded in as a hash so a change
 * in either produces a different key */
#define AL_SEL_COMBINE_HASH   34   /* 30 */

#define AL_SEL_PUT(k, pos, val, bits) \
    ((k) |= ((uint64_t)((val) & ((1u << (bits)) - 1u)) << (pos)))

#define AL_SEL_GET(k, pos, bits) \
    ((uint32_t)(((k) >> (pos)) & ((1u << (bits)) - 1u)))

#endif /* AL_SELECT_H */
