#ifndef GLITCH64_N64_COMBINER_H
#define GLITCH64_N64_COMBINER_H

/* The N64's colour combiner as the RDP has it, written out as GLSL for
 * Glitch64's fragment shader. C89.
 *
 * Each cycle is (a - b) * c + d for the colour and for the alpha, with the
 * RDP's inputs. In two-cycle mode the second cycle takes the first's result
 * as COMBINED, and the texels swap: its TEXEL0 is the second tile, as on
 * the RDP. In one-cycle mode the RDP runs the second cycle's settings. */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* the blender's fog colour, or its alpha, stands in for the combiner's
 * (what Glide64 did for a few blend modes that read the fog colour) */
#define GLITCH64_N64_FOG_RGB    1u
#define GLITCH64_N64_FOG_ALPHA  2u
/* LOD_FRACTION worked out per pixel from the texture coordinates'
 * derivatives; without it, the n64Const.w the CPU gives */
#define GLITCH64_N64_LOD        4u

typedef struct glitch64_n64_combiner
{
   /* sub a, sub b, mul, add of each cycle, as SetCombine gives them */
   uint8_t rgb[2][4];
   uint8_t alpha[2][4];
   uint8_t two_cycle;
   uint8_t flags;
} glitch64_n64_combiner;

/* The combiner in two words, for a shader's key: equal words, equal GLSL. */
void glitch64_n64_key(const glitch64_n64_combiner *c, uint32_t key[2]);
void glitch64_n64_from_key(const uint32_t key[2], glitch64_n64_combiner *c);

/* GLSL statements for main(): from the two tiles' texels (tile 0 in
 * readtex1, tile 1 in readtex0, Glitch64's units), vFrontColor and the
 * uniforms the shader header declares (n64Prim, n64Env, n64Const = K4, K5,
 * PRIM_LOD_FRAC and the CPU's LOD_FRACTION, n64Level = the minimum
 * level, the maximum level, detail or sharpen on, the noise seed;
 * n64KeyCenter, n64KeyScale; fogColor, fogAlpha), sets
 * gl_FragColor. Appended to @out, which holds @cap bytes with its
 * terminator; 0 if it does not fit, else the length written. */
size_t glitch64_n64_fragment(const glitch64_n64_combiner *c, char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif
