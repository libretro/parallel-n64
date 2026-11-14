/* Copyright (c) 2020 Themaister
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */


#ifndef RDP_COMMAND_DECODE_H
#define RDP_COMMAND_DECODE_H

/* The RDP's state commands, decoded from their two words into the state
 * the renderer draws with. The primitives and loads are
 * primitive_decode.h's and tmem_load.h's. C89. */

#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>
#include "rdp_state.h"

RETRO_BEGIN_DECLS

/* Set Color Image (0x3f): the framebuffer's address, width, FBFormat
 * (I4 to RGBA8888) and pixel size (0 to 3: 4 to 32 bits) */
void rdp_decode_color_image(const uint32_t *words, uint32_t *addr, uint32_t *width, uint32_t *fb_fmt,
		unsigned *size);

/* Set Scissor (0x2d): the box, and the interlace bits of @static_flags */
void rdp_decode_scissor(const uint32_t *words, rdp_scissor_state_t *scissor, uint32_t *static_flags);

/* Set Other Modes (0x2f) into @state and @depth_blend: the cycle type,
 * texturing, dithering, coverage, depth and the blender's inputs.
 * Returns whether primitive depth is on. */
bool rdp_decode_other_modes(const uint32_t *words, rdp_static_state_t *state, rdp_depth_blend_state_t *depth_blend);

/* Set Texture Image (0x3d) */
typedef struct rdp_texture_image
{
	uint32_t addr;
	uint32_t width;
	uint8_t size;
	uint8_t fmt;
} rdp_texture_image_t;
void rdp_decode_texture_image(const uint32_t *words, rdp_texture_image_t *image);

/* Set Tile (0x35) into @meta, masks over 10 taken as 10 and a mask of 0
 * clamping. Returns the tile. */
unsigned rdp_decode_tile(const uint32_t *words, rdp_tile_meta_t *meta);

/* Set Tile Size (0x32): returns the tile */
unsigned rdp_decode_tile_size(const uint32_t *words, rdp_tile_size_t *size);

/* Set Combine Mode (0x3c) into @state's two cycles */
void rdp_decode_combine(const uint32_t *words, rdp_static_state_t *state);

/* Set Convert (0x2c): K0 to K5, 9 bits each */
void rdp_decode_convert(const uint32_t *words, uint16_t k[6]);

/* A chroma key component */
typedef struct rdp_color_key
{
	uint32_t width;
	uint32_t center;
	uint32_t scale;
} rdp_color_key_t;

/* Set Key GB (0x2a) and Set Key R (0x2b) */
void rdp_decode_key_gb(const uint32_t *words, rdp_color_key_t *g, rdp_color_key_t *b);
void rdp_decode_key_r(const uint32_t *words, rdp_color_key_t *r);

/* Set Primitive Color (0x3a) */
void rdp_decode_prim_color(const uint32_t *words, uint8_t *min_level, uint8_t *lod_frac, uint32_t *color);

RETRO_END_DECLS

#endif
