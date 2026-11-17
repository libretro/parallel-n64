#ifndef RDP_COVERAGE_H
#define RDP_COVERAGE_H

/* The RDP's coverage, for the renderers that keep none (GLideN64, Glide64).
 * C89.
 *
 * A game can read the coverage back: Ocarina of Time and Majora's Mask, as
 * they pause, draw a frame buffer's coverage into it with the blender
 * (G_RM_VISCVG), copy that into an 8-bit image with a texture rectangle,
 * and filter on the CPU every pixel that image calls partly covered. A
 * renderer that writes neither leaves stale bytes there, and the CPU
 * filters the whole frame, which took about 280 frames before the pause
 * menu showed. These write what the RDP writes for those two draws, in
 * RDRAM, as if every pixel were fully covered, which is all a renderer
 * without coverage can say. */

#include <stdint.h>

/* For a renderer's hooks: out of the draw functions' way, which call them
 * only for an 8-bit color image or a coverage mode */
#if defined(__GNUC__)
#define RDPCVG_COLD __attribute__((noinline, cold))
#elif defined(_MSC_VER)
#define RDPCVG_COLD __declspec(noinline)
#else
#define RDPCVG_COLD
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Whether set_other_modes' low word @_l has the blender draw memory
 * coverage: forced blending of the blend color by memory alpha, read from
 * memory, the pixel's own color by nothing */
int rdpcvg_draws_coverage(uint32_t _l);

/* What a draw function tests first, inline: forced blending reading memory,
 * which rdpcvg_draws_coverage needs, and which few modes set */
#define RDPCVG_MAY_DRAW_COVERAGE(_l) (((_l) & 0x4040u) == 0x4040u)

/* A rectangle drawn so into the color image at @_address, @_width pixels a
 * row, of @_size (1 8-bit, 2 16-bit, 3 32-bit), from (@_ulx, @_uly) up to
 * but not through (@_lrx, @_lry): each pixel gets the blend color @_rgb
 * (0xRRGGBB), which full coverage gives, and full coverage. */
void rdpcvg_fill(uint8_t *_rdram, uint32_t _rdramSize, uint32_t _address, uint32_t _width,
		uint32_t _size, int32_t _ulx, int32_t _uly, int32_t _lrx, int32_t _lry, uint32_t _rgb);

/* A 16-bit texel of a tile from its renderer's TMEM: @_s and @_t from the
 * tile's corner */
typedef uint16_t (*rdpcvg_texel_fn)(const void *_ctx, uint32_t _s, uint32_t _t);

/* A texture rectangle, one texel a pixel, of a 16-bit tile whose texels
 * @_fetch gives (IA when @_ia, else RGBA), into an 8-bit color image at
 * @_address, @_width pixels a row: from (@_ulx, @_uly) up to but not through
 * (@_lrx, @_lry), starting at the tile's texel (@_s, @_t), clamped to its
 * @_tileWidth x @_tileHeight. The pixel is the texel's color, as the
 * combiner's TEXEL0 gives it unblended: red at an even address, green at an
 * odd one. */
void rdpcvg_texrect_8(uint8_t *_rdram, uint32_t _rdramSize, uint32_t _address, uint32_t _width,
		int32_t _ulx, int32_t _uly, int32_t _lrx, int32_t _lry, int32_t _s, int32_t _t,
		uint32_t _tileWidth, uint32_t _tileHeight, int _ia, rdpcvg_texel_fn _fetch, const void *_ctx);

/* Whether a texture rectangle is the copy rdpcvg_texrect_8 makes: in one
 * cycle, unblended (set_other_modes low word @_l), combining TEXEL0 alone
 * (the second cycle's RGB multiplier @_mulRgb1 and addend @_addRgb1, as
 * set_combine codes them), into an 8-bit color image (@_ciSize) from a
 * 16-bit tile (@_tileSize) of format @_tileFormat (RGBA 0 or IA 3), one
 * texel a pixel (@_dsdx, @_dtdy in texels a pixel, not flipped) */
int rdpcvg_is_texel_copy(uint32_t _cycleType, uint32_t _l, uint32_t _mulRgb1, uint32_t _addRgb1, uint32_t _ciSize,
		uint32_t _tileSize, uint32_t _tileFormat, double _dsdx, double _dtdy, int _flip);

#ifdef __cplusplus
}
#endif

#endif
