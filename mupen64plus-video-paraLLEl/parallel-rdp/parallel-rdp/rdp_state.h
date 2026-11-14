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

#ifndef RDP_STATE_H
#define RDP_STATE_H

#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>
#include "primitive_decode.h"

RETRO_BEGIN_DECLS

/* ---- the combiner's inputs, as the set combine command has them ---- */
enum rdp_rgb_mul
{
	RDP_RGB_MUL_COMBINED = 0,
	RDP_RGB_MUL_TEXEL0 = 1,
	RDP_RGB_MUL_TEXEL1 = 2,
	RDP_RGB_MUL_PRIMITIVE = 3,
	RDP_RGB_MUL_SHADE = 4,
	RDP_RGB_MUL_ENV = 5,
	RDP_RGB_MUL_KEY_SCALE = 6,
	RDP_RGB_MUL_COMBINED_ALPHA = 7,
	RDP_RGB_MUL_TEXEL0_ALPHA = 8,
	RDP_RGB_MUL_TEXEL1_ALPHA = 9,
	RDP_RGB_MUL_PRIMITIVE_ALPHA = 10,
	RDP_RGB_MUL_SHADE_ALPHA = 11,
	RDP_RGB_MUL_ENV_ALPHA = 12,
	RDP_RGB_MUL_LOD_FRAC = 13,
	RDP_RGB_MUL_PRIM_LOD_FRAC = 14,
	RDP_RGB_MUL_CONVERT_K5 = 15,
	RDP_RGB_MUL_ZERO = 16
};

enum rdp_rgb_muladd
{
	RDP_RGB_MULADD_COMBINED = 0,
	RDP_RGB_MULADD_TEXEL0 = 1,
	RDP_RGB_MULADD_TEXEL1 = 2,
	RDP_RGB_MULADD_PRIMITIVE = 3,
	RDP_RGB_MULADD_SHADE = 4,
	RDP_RGB_MULADD_ENV = 5,
	RDP_RGB_MULADD_ONE = 6,
	RDP_RGB_MULADD_NOISE = 7,
	RDP_RGB_MULADD_ZERO = 8
};

enum rdp_rgb_mulsub
{
	RDP_RGB_MULSUB_COMBINED = 0,
	RDP_RGB_MULSUB_TEXEL0 = 1,
	RDP_RGB_MULSUB_TEXEL1 = 2,
	RDP_RGB_MULSUB_PRIMITIVE = 3,
	RDP_RGB_MULSUB_SHADE = 4,
	RDP_RGB_MULSUB_ENV = 5,
	RDP_RGB_MULSUB_KEY_CENTER = 6,
	RDP_RGB_MULSUB_CONVERT_K4 = 7,
	RDP_RGB_MULSUB_ZERO = 8
};

enum rdp_rgb_add
{
	RDP_RGB_ADD_COMBINED = 0,
	RDP_RGB_ADD_TEXEL0 = 1,
	RDP_RGB_ADD_TEXEL1 = 2,
	RDP_RGB_ADD_PRIMITIVE = 3,
	RDP_RGB_ADD_SHADE = 4,
	RDP_RGB_ADD_ENV = 5,
	RDP_RGB_ADD_ONE = 6,
	RDP_RGB_ADD_ZERO = 7
};

/* alpha's add, subtract and the added one */
enum rdp_alpha_addsub
{
	RDP_ALPHA_ADDSUB_COMBINED_ALPHA = 0,
	RDP_ALPHA_ADDSUB_TEXEL0_ALPHA = 1,
	RDP_ALPHA_ADDSUB_TEXEL1_ALPHA = 2,
	RDP_ALPHA_ADDSUB_PRIMITIVE_ALPHA = 3,
	RDP_ALPHA_ADDSUB_SHADE_ALPHA = 4,
	RDP_ALPHA_ADDSUB_ENV_ALPHA = 5,
	RDP_ALPHA_ADDSUB_ONE = 6,
	RDP_ALPHA_ADDSUB_ZERO = 7
};

enum rdp_alpha_mul
{
	RDP_ALPHA_MUL_LOD_FRAC = 0,
	RDP_ALPHA_MUL_TEXEL0_ALPHA = 1,
	RDP_ALPHA_MUL_TEXEL1_ALPHA = 2,
	RDP_ALPHA_MUL_PRIMITIVE_ALPHA = 3,
	RDP_ALPHA_MUL_SHADE_ALPHA = 4,
	RDP_ALPHA_MUL_ENV_ALPHA = 5,
	RDP_ALPHA_MUL_PRIM_LOD_FRAC = 6,
	RDP_ALPHA_MUL_ZERO = 7
};

/* ---- textures ---- */
enum rdp_texture_size
{
	RDP_TEXTURE_SIZE_4 = 0,
	RDP_TEXTURE_SIZE_8 = 1,
	RDP_TEXTURE_SIZE_16 = 2,
	RDP_TEXTURE_SIZE_32 = 3
};

enum rdp_texture_format
{
	RDP_TEXTURE_FORMAT_RGBA = 0,
	RDP_TEXTURE_FORMAT_YUV = 1,
	RDP_TEXTURE_FORMAT_CI = 2,
	RDP_TEXTURE_FORMAT_IA = 3,
	RDP_TEXTURE_FORMAT_I = 4
};

/* ---- other modes ---- */
enum rdp_rgb_dither
{
	RDP_RGB_DITHER_MAGIC = 0,
	RDP_RGB_DITHER_BAYER = 1,
	RDP_RGB_DITHER_NOISE = 2,
	RDP_RGB_DITHER_OFF = 3
};

enum rdp_cycle_type
{
	RDP_CYCLE_1 = 0,
	RDP_CYCLE_2 = 1,
	RDP_CYCLE_COPY = 2,
	RDP_CYCLE_FILL = 3
};

/* the blender's first and second colour inputs */
enum rdp_blend_color
{
	RDP_BLEND_PIXEL_COLOR = 0,
	RDP_BLEND_MEMORY_COLOR = 1,
	RDP_BLEND_BLEND_COLOR = 2,
	RDP_BLEND_FOG_COLOR = 3
};

enum rdp_blend_1b
{
	RDP_BLEND_1B_PIXEL_ALPHA = 0,
	RDP_BLEND_1B_FOG_ALPHA = 1,
	RDP_BLEND_1B_SHADE_ALPHA = 2,
	RDP_BLEND_1B_ZERO = 3
};

enum rdp_blend_2b
{
	RDP_BLEND_2B_INV_PIXEL_ALPHA = 0,
	RDP_BLEND_2B_MEMORY_ALPHA = 1,
	RDP_BLEND_2B_ONE = 2,
	RDP_BLEND_2B_ZERO = 3
};

enum rdp_coverage_mode
{
	RDP_COVERAGE_CLAMP = 0,
	RDP_COVERAGE_WRAP = 1,
	RDP_COVERAGE_ZAP = 2,
	RDP_COVERAGE_SAVE = 3
};

enum rdp_z_mode
{
	RDP_Z_MODE_OPAQUE = 0,
	RDP_Z_MODE_INTERPENETRATING = 1,
	RDP_Z_MODE_TRANSPARENT = 2,
	RDP_Z_MODE_DECAL = 3
};

/* ---- tiles: 32 bytes, as the shaders read them ---- */
enum TileInfoFlagBits
{
	TILE_INFO_CLAMP_S_BIT = 1 << 0,
	TILE_INFO_MIRROR_S_BIT = 1 << 1,
	TILE_INFO_CLAMP_T_BIT = 1 << 2,
	TILE_INFO_MIRROR_T_BIT = 1 << 3
};

typedef struct rdp_tile_size
{
	uint32_t slo;
	uint32_t shi;
	uint32_t tlo;
	uint32_t thi;
} rdp_tile_size_t;

typedef struct rdp_tile_meta
{
	uint32_t offset;
	uint32_t stride;
	/* enum rdp_texture_format */
	uint8_t fmt;
	/* enum rdp_texture_size */
	uint8_t size;
	uint8_t palette;
	uint8_t mask_s;
	uint8_t shift_s;
	uint8_t mask_t;
	uint8_t shift_t;
	/* TILE_INFO_* */
	uint8_t flags;
} rdp_tile_meta_t;

typedef struct rdp_tile_info
{
	rdp_tile_size_t size;
	rdp_tile_meta_t meta;
} rdp_tile_info_t;

/* ---- the state a primitive is drawn with ---- */
typedef struct rdp_combiner_inputs
{
	struct
	{
		/* enum rdp_rgb_muladd, rdp_rgb_mulsub, rdp_rgb_mul, rdp_rgb_add */
		uint8_t muladd;
		uint8_t mulsub;
		uint8_t mul;
		uint8_t add;
	} rgb;
	struct
	{
		/* enum rdp_alpha_addsub, rdp_alpha_mul */
		uint8_t muladd;
		uint8_t mulsub;
		uint8_t mul;
		uint8_t add;
	} alpha;
} rdp_combiner_inputs_t;

typedef struct rdp_blend_modes
{
	/* enum rdp_blend_color, rdp_blend_1b, rdp_blend_color, rdp_blend_2b */
	uint8_t blend_1a;
	uint8_t blend_1b;
	uint8_t blend_2a;
	uint8_t blend_2b;
} rdp_blend_modes_t;

enum StaticRasterizationFlagBits
{
	RASTERIZATION_INTERLACE_FIELD_BIT = 1 << 0,
	RASTERIZATION_INTERLACE_KEEP_ODD_BIT = 1 << 1,
	RASTERIZATION_AA_BIT = 1 << 2,
	RASTERIZATION_PERSPECTIVE_CORRECT_BIT = 1 << 3,
	RASTERIZATION_TLUT_BIT = 1 << 4,
	RASTERIZATION_TLUT_TYPE_BIT = 1 << 5,
	RASTERIZATION_CVG_TIMES_ALPHA_BIT = 1 << 6,
	RASTERIZATION_ALPHA_CVG_SELECT_BIT = 1 << 7,
	RASTERIZATION_MULTI_CYCLE_BIT = 1 << 8,
	RASTERIZATION_TEX_LOD_ENABLE_BIT = 1 << 9,
	RASTERIZATION_SHARPEN_LOD_ENABLE_BIT = 1 << 10,
	RASTERIZATION_DETAIL_LOD_ENABLE_BIT = 1 << 11,
	RASTERIZATION_FILL_BIT = 1 << 12,
	RASTERIZATION_COPY_BIT = 1 << 13,
	RASTERIZATION_SAMPLE_MODE_BIT = 1 << 14,
	RASTERIZATION_ALPHA_TEST_BIT = 1 << 15,
	RASTERIZATION_ALPHA_TEST_DITHER_BIT = 1 << 16,
	RASTERIZATION_SAMPLE_MID_TEXEL_BIT = 1 << 17,
	RASTERIZATION_USES_TEXEL0_BIT = 1 << 18,
	RASTERIZATION_USES_TEXEL1_BIT = 1 << 19,
	RASTERIZATION_USES_LOD_BIT = 1 << 20,
	RASTERIZATION_USES_PIPELINED_TEXEL1_BIT = 1 << 21,
	RASTERIZATION_CONVERT_ONE_BIT = 1 << 22,
	RASTERIZATION_BILERP_0_BIT = 1 << 23,
	RASTERIZATION_BILERP_1_BIT = 1 << 24,
	RASTERIZATION_NEED_NOISE_DUAL_BIT = 1 << 25,
	RASTERIZATION_UPSCALING_LOG2_BIT_OFFSET = 26,
	RASTERIZATION_NEED_NOISE_BIT = 1 << 28,
	RASTERIZATION_USE_STATIC_TEXTURE_SIZE_FORMAT_BIT = 1 << 29,
	RASTERIZATION_USE_SPECIALIZATION_CONSTANT_BIT = 1 << 30
};

/* 32 bytes, as the shaders read it */
typedef struct rdp_static_state
{
	rdp_combiner_inputs_t combiner[2];
	/* RASTERIZATION_* */
	uint32_t flags;
	uint32_t dither;
	uint32_t texture_size;
	uint32_t texture_fmt;
} rdp_static_state_t;

enum DepthBlendFlagBits
{
	DEPTH_BLEND_DEPTH_TEST_BIT = 1 << 0,
	DEPTH_BLEND_DEPTH_UPDATE_BIT = 1 << 1,
	DEPTH_BLEND_FORCE_BLEND_BIT = 1 << 3,
	DEPTH_BLEND_IMAGE_READ_ENABLE_BIT = 1 << 4,
	DEPTH_BLEND_COLOR_ON_COVERAGE_BIT = 1 << 5,
	DEPTH_BLEND_MULTI_CYCLE_BIT = 1 << 6,
	DEPTH_BLEND_AA_BIT = 1 << 7,
	DEPTH_BLEND_DITHER_ENABLE_BIT = 1 << 8
};

/* 16 bytes, as the shaders read it */
typedef struct rdp_depth_blend_state
{
	rdp_blend_modes_t blend_cycles[2];
	/* DEPTH_BLEND_* */
	uint32_t flags;
	/* enum rdp_coverage_mode, rdp_z_mode */
	uint8_t coverage_mode;
	uint8_t z_mode;
	uint8_t padding[2];
} rdp_depth_blend_state_t;

typedef struct rdp_scissor_state
{
	uint32_t xlo;
	uint32_t ylo;
	uint32_t xhi;
	uint32_t yhi;
} rdp_scissor_state_t;

/* ---- what a primitive gets from the constant registers: 56 bytes, as
 * the shaders read it ---- */
typedef struct rdp_constant_combiner_inputs
{
	uint8_t muladd[4];
	uint8_t mulsub[4];
	uint8_t mul[4];
	uint8_t add[4];
} rdp_constant_combiner_inputs_t;

typedef struct rdp_derived_setup
{
	rdp_constant_combiner_inputs_t constants[2];
	uint8_t fog_color[4];
	uint8_t blend_color[4];
	uint32_t fill_color;
	uint16_t dz;
	uint8_t dz_compressed;
	uint8_t min_lod;
	int16_t convert_factors[4];
} rdp_derived_setup_t;

/* The constant registers. All zero is how they start. */
typedef struct rdp_constants
{
	uint32_t blend_color;
	uint32_t fog_color;
	uint32_t env_color;
	uint32_t primitive_color;
	uint32_t fill_color;
	uint8_t min_level;
	uint8_t prim_lod_frac;
	int32_t prim_depth;
	uint16_t prim_dz;
	/* 9 bits each, signed */
	uint16_t convert[6];

	uint16_t key_width[3];
	uint8_t key_center[3];
	uint8_t key_scale[3];

	bool use_prim_depth;
} rdp_constants_t;

/* ---- what is worked out from the state for a primitive ---- */
/* Its constants, for @state's combiner, and its depth slope */
void rdp_derived_setup(const rdp_constants_t *constants, const rdp_static_state_t *state,
		const rdp_attribute_setup_t *attr, rdp_derived_setup_t *setup);

/* At most how many 8x8 tiles of the framebuffer, at @scaling, the
 * primitive covers inside @scissor */
unsigned rdp_conservative_tile_count(const rdp_triangle_setup_t *setup, const rdp_scissor_state_t *scissor,
		unsigned scaling);

/* X from @start_x to @end_x goes past what the rasterizer holds,
 * @limit either way */
bool rdp_x_range_exceeds(int32_t start_x, int32_t end_x, int32_t limit);

/* The setup as the rasterizer wants it under @static_flags */
void rdp_fixup_triangle_setup(rdp_triangle_setup_t *setup, uint32_t static_flags);

/* Whether the primitive needs noise, and two of it */
void rdp_deduce_noise_state(rdp_static_state_t *state);

/* Which texels the combiner uses, and the texture format and size when
 * every tile it samples from @tile on (up to @max_lod_level levels) has
 * the same: then a variant for them can be used */
void rdp_deduce_static_texture_state(rdp_static_state_t *state, const rdp_tile_info_t *tiles, unsigned tile,
		unsigned max_lod_level);

/* @state with what the shaders do not read under it cleared, so that
 * states that draw the same compare equal */
void rdp_normalize_static_state(rdp_static_state_t *state);

RETRO_END_DECLS

#endif
