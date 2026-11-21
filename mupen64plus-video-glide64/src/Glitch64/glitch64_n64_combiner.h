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

/* blend[0], the blender's first cycle of two, runs in the shader: it
 * reads no memory colour */
#define GLITCH64_N64_BLEND0     1u
/* blend[1] runs whole in the shader (it reads no memory colour); without
 * it, the shader gives the colour its P selects and the alpha its A
 * selects, which the GPU blends with what the framebuffer holds */
#define GLITCH64_N64_BLEND1     2u
/* LOD_FRACTION worked out per pixel from the texture coordinates'
 * derivatives; without it, the n64Const.w the CPU gives */
#define GLITCH64_N64_LOD        4u
/* the pixel's alpha into the blender is its coverage (ALPHA_CVG_SEL):
 * full, so 1 */
#define GLITCH64_N64_CVG_ALPHA  8u
/* two-cycle mode with the texture unit's second cycle converting
 * (CONVERT_ONE): TEXEL1 is no tile's, it is TEXEL0 through the YUV
 * matrix, K0 to K3 (n64Convert: each as the RDP takes it, 2k + 1, over
 * 256), blue as the luma:
 *   r = b + K0 g, g = b + K1 r + K2 g, b = b + K3 r, a = b */
#define GLITCH64_N64_CONVERT1   16u
/* the same with the second cycle filtering too: TEXEL1 is TEXEL0's blue,
 * in all four */
#define GLITCH64_N64_CONVERT1_Y 32u

/* a blender cycle, as the RDP's other modes hold it: P, A, M, B */
#define GLITCH64_BL(p, a, m, b) ((uint8_t)(((p) << 6) | ((a) << 4) | ((m) << 2) | (b)))
#define GLITCH64_BL_P(x)        (((x) >> 6) & 3)
#define GLITCH64_BL_A(x)        (((x) >> 4) & 3)
#define GLITCH64_BL_M(x)        (((x) >> 2) & 3)
#define GLITCH64_BL_B(x)        ((x) & 3)
/* P and M: the pixel (the combiner's, or the first cycle's), the memory
 * colour, the blend colour, the fog colour */
enum { GLITCH64_BL_IN = 0, GLITCH64_BL_MEM, GLITCH64_BL_BLEND, GLITCH64_BL_FOG };
/* A: the pixel's alpha, the fog alpha, the shade alpha, 0 */
enum { GLITCH64_BL_A_IN = 0, GLITCH64_BL_A_FOG, GLITCH64_BL_A_SHADE, GLITCH64_BL_A_ZERO };
/* B: 1 - A, the memory alpha (full: 1), 1, 0 */
enum { GLITCH64_BL_B_1MA = 0, GLITCH64_BL_B_MEM, GLITCH64_BL_B_ONE, GLITCH64_BL_B_ZERO };

typedef struct glitch64_n64_combiner
{
   /* sub a, sub b, mul, add of each cycle, as SetCombine gives them */
   uint8_t rgb[2][4];
   uint8_t alpha[2][4];
   uint8_t two_cycle;
   uint8_t flags;
   /* the blender's cycles (GLITCH64_BL), as the flags say they run: P*A +
    * M*B. 0 gives the pixel as it is. */
   uint8_t blend[2];
} glitch64_n64_combiner;

/* The inputs a shader reading them by index (glitch64_n64_inputs) has:
 * the colour from .rgb, the alpha from .a */
enum
{
   GLITCH64_IN_COMBINED = 0, GLITCH64_IN_TEXEL0, GLITCH64_IN_TEXEL1, GLITCH64_IN_PRIM, GLITCH64_IN_SHADE,
   GLITCH64_IN_ENV, GLITCH64_IN_ONE, GLITCH64_IN_ZERO, GLITCH64_IN_NOISE, GLITCH64_IN_CENTER, GLITCH64_IN_SCALE,
   GLITCH64_IN_K4, GLITCH64_IN_K5, GLITCH64_IN_COMBINED_A, GLITCH64_IN_TEXEL0_A, GLITCH64_IN_TEXEL1_A,
   GLITCH64_IN_PRIM_A, GLITCH64_IN_SHADE_A, GLITCH64_IN_ENV_A, GLITCH64_IN_LOD, GLITCH64_IN_PRIM_LOD,
   GLITCH64_IN_COUNT
};

/* The combiner as the inputs each cycle reads, by those indices:
 * ops[8 * cycle + slot], slots 0-3 the colour's a, b, c, d, 4-7 the
 * alpha's; the second cycle's texels swapped in two-cycle mode, as on the
 * RDP. In one-cycle mode the second cycle's settings are the ones run. */
void glitch64_n64_inputs(const glitch64_n64_combiner *c, int ops[16]);

/* The combiner and blender in three words, for a shader's key: equal
 * words, equal GLSL. */
#define GLITCH64_N64_KEY_WORDS 3
void glitch64_n64_key(const glitch64_n64_combiner *c, uint32_t key[GLITCH64_N64_KEY_WORDS]);
void glitch64_n64_from_key(const uint32_t key[GLITCH64_N64_KEY_WORDS], glitch64_n64_combiner *c);

/* TEXEL1 from TEXEL0 through the YUV matrix, as GLSL: the RDP's 8-bit
 * steps, rounded as it rounds them, and not wrapped at 9 bits */
#define GLITCH64_N64_CONVERT_GLSL \
   "  { vec3 n64Y = floor(n64T0.rgb * 255.0 + 0.5); \n" \
   "    n64T1 = vec4(n64Y.b + floor(n64Convert.x * n64Y.g + 0.5), \n" \
   "        n64Y.b + floor(n64Convert.y * n64Y.r + n64Convert.z * n64Y.g + 0.5), \n" \
   "        n64Y.b + floor(n64Convert.w * n64Y.r + 0.5), n64Y.b) / 255.0; } \n"

/* GLSL statements for main(): from the two tiles' texels (tile 0 in
 * readtex1, tile 1 in readtex0, Glitch64's units), vFrontColor and the
 * uniforms the shader header declares (n64Prim, n64Env, n64Const = K4, K5,
 * PRIM_LOD_FRAC and the CPU's LOD_FRACTION, n64Level = the minimum
 * level, the maximum level, detail or sharpen on, the noise seed;
 * n64KeyCenter, n64KeyScale, n64Convert; fogColor, fogAlpha, n64Blend, alphaRef),
 * sets gl_FragColor: the alpha test on the combiner's alpha, then the
 * blender as the flags say. Appended to @out, which holds @cap bytes with
 * its terminator; 0 if it does not fit, else the length written. */
size_t glitch64_n64_fragment(const glitch64_n64_combiner *c, char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif
