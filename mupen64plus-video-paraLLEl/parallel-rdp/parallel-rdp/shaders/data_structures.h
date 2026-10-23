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

#ifndef DATA_STRUCTURES_H_
#define DATA_STRUCTURES_H_

// Data structures which are supposed to match up with rdp_data_structures.hpp.
// A little dirty to duplicate like this, but it's non-trivial to share headers with C++,
// especially when we need to deal with small integer types.

const int TRIANGLE_SETUP_FLIP_BIT = 1 << 0;
const int TRIANGLE_SETUP_DO_OFFSET_BIT = 1 << 1;
const int TRIANGLE_SETUP_SKIP_XFRAC_BIT = 1 << 2;
const int TRIANGLE_SETUP_INTERLACE_FIELD_BIT = 1 << 3;
const int TRIANGLE_SETUP_INTERLACE_KEEP_ODD_BIT = 1 << 4;
const int TRIANGLE_SETUP_DISABLE_UPSCALING_BIT = 1 << 5;
const int TRIANGLE_SETUP_NATIVE_LOD_BIT = 1 << 6;
const int TRIANGLE_SETUP_FILL_COPY_RASTER_BIT = 1 << 7;

const int RASTERIZATION_INTERLACE_FIELD_BIT = 1 << 0;
const int RASTERIZATION_INTERLACE_KEEP_ODD_BIT = 1 << 1;
const int RASTERIZATION_AA_BIT = 1 << 2;
const int RASTERIZATION_PERSPECTIVE_CORRECT_BIT = 1 << 3;
const int RASTERIZATION_TLUT_BIT = 1 << 4;
const int RASTERIZATION_TLUT_TYPE_BIT = 1 << 5;
const int RASTERIZATION_CVG_TIMES_ALPHA_BIT = 1 << 6;
const int RASTERIZATION_ALPHA_CVG_SELECT_BIT = 1 << 7;
const int RASTERIZATION_MULTI_CYCLE_BIT = 1 << 8;
const int RASTERIZATION_TEX_LOD_ENABLE_BIT = 1 << 9;
const int RASTERIZATION_SHARPEN_LOD_ENABLE_BIT = 1 << 10;
const int RASTERIZATION_DETAIL_LOD_ENABLE_BIT = 1 << 11;
const int RASTERIZATION_FILL_BIT = 1 << 12;
const int RASTERIZATION_COPY_BIT = 1 << 13;
const int RASTERIZATION_SAMPLE_MODE_BIT = 1 << 14;
const int RASTERIZATION_ALPHA_TEST_BIT = 1 << 15;
const int RASTERIZATION_ALPHA_TEST_DITHER_BIT = 1 << 16;
const int RASTERIZATION_SAMPLE_MID_TEXEL_BIT = 1 << 17;
const int RASTERIZATION_USES_TEXEL0_BIT = 1 << 18;
const int RASTERIZATION_USES_TEXEL1_BIT = 1 << 19;
const int RASTERIZATION_USES_LOD_BIT = 1 << 20;
const int RASTERIZATION_USES_PIPELINED_TEXEL1_BIT = 1 << 21;
const int RASTERIZATION_CONVERT_ONE_BIT = 1 << 22;
const int RASTERIZATION_BILERP_0_BIT = 1 << 23;
const int RASTERIZATION_BILERP_1_BIT = 1 << 24;
const int RASTERIZATION_NEED_NOISE_DUAL_BIT = 1 << 25;
const int RASTERIZATION_UPSCALING_LOG2_BIT_OFFSET = 26;
const int RASTERIZATION_NEED_NOISE_BIT = 1 << 28;
const int RASTERIZATION_USE_STATIC_TEXTURE_SIZE_FORMAT_BIT = 1 << 29;
const int RASTERIZATION_USE_SPECIALIZATION_CONSTANT_BIT = 1 << 30;

const int DEPTH_BLEND_DEPTH_TEST_BIT = 1 << 0;
const int DEPTH_BLEND_DEPTH_UPDATE_BIT = 1 << 1;
const int DEPTH_BLEND_FORCE_BLEND_BIT = 1 << 3;
const int DEPTH_BLEND_IMAGE_READ_ENABLE_BIT = 1 << 4;
const int DEPTH_BLEND_COLOR_ON_COVERAGE_BIT = 1 << 5;
const int DEPTH_BLEND_MULTI_CYCLE_BIT = 1 << 6;
const int DEPTH_BLEND_AA_BIT = 1 << 7;
const int DEPTH_BLEND_DITHER_ENABLE_BIT = 1 << 8;

#if NO_STORAGE_8BIT
// The NO_STORAGE_8BIT twins below keep each Mem struct's layout; byte
// groups become 16/32-bit words (lowest address in the low bits).
struct TriangleSetupMem
{
	int xh, xm, xl;
	mem_i16 yh, ym;
	int dxhdy, dxmdy, dxldy;
	mem_i16 yl; mem_u16 flags_tile;
};
#define triangle_setup_flags(index) unpack_mem_u8(triangle_setup.elems[index].flags_tile, 0)
#define triangle_setup_tile(index) unpack_mem_u8(triangle_setup.elems[index].flags_tile, 1)
#else
struct TriangleSetupMem
{
	int xh, xm, xl;
	mem_i16 yh, ym;
	int dxhdy, dxmdy, dxldy;
	mem_i16 yl; mem_u8 flags; mem_u8 tile;
};
#define triangle_setup_flags(index) triangle_setup.elems[index].flags
#define triangle_setup_tile(index) triangle_setup.elems[index].tile
#endif

#if SMALL_TYPES
#define TriangleSetup TriangleSetupMem
#else
struct TriangleSetup
{
	int xh, xm, xl;
	i16 yh, ym;
	int dxhdy, dxmdy, dxldy;
	i16 yl; u8 flags; u8 tile;
};
#endif

struct AttributeSetupMem
{
	ivec4 rgba;
	ivec4 drgba_dx;
	ivec4 drgba_de;
	ivec4 drgba_dy;

	ivec4 stzw;
	ivec4 dstzw_dx;
	ivec4 dstzw_de;
	ivec4 dstzw_dy;
};
#define AttributeSetup AttributeSetupMem

struct SpanSetupMem
{
	ivec4 rgba;
	ivec4 stzw;

	mem_u16x4 xleft;
	mem_u16x4 xright;

	int interpolation_base_x;
	int start_x;
	int end_x;
	mem_i16 lodlength;
	mem_u16 valid_line;
};
#if SMALL_TYPES
#define SpanSetup SpanSetupMem
#else
struct SpanSetup
{
	ivec4 rgba;
	ivec4 stzw;

	u16x4 xleft;
	u16x4 xright;

	int interpolation_base_x;
	int start_x;
	int end_x;
	i16 lodlength;
	u16 valid_line;
};
#endif

struct SpanInfoOffsetsMem
{
	int offset;
	int ylo;
	int yhi;
	int padding;
};
#define SpanInfoOffsets SpanInfoOffsetsMem

#if NO_STORAGE_8BIT
struct DerivedSetupMem
{
	uint constant_muladd0_packed;
	uint constant_mulsub0_packed;
	uint constant_mul0_packed;
	uint constant_add0_packed;

	uint constant_muladd1_packed;
	uint constant_mulsub1_packed;
	uint constant_mul1_packed;
	uint constant_add1_packed;

	uint fog_color_packed;
	uint blend_color_packed;
	uint fill_color;

	mem_u16 dz;
	mem_u16 dz_compressed_min_lod;

	mem_i16x4 factors;
};
#else
struct DerivedSetupMem
{
	mem_u8x4 constant_muladd0;
	mem_u8x4 constant_mulsub0;
	mem_u8x4 constant_mul0;
	mem_u8x4 constant_add0;

	mem_u8x4 constant_muladd1;
	mem_u8x4 constant_mulsub1;
	mem_u8x4 constant_mul1;
	mem_u8x4 constant_add1;

	mem_u8x4 fog_color;
	mem_u8x4 blend_color;
	uint fill_color;

	mem_u16 dz;
	mem_u8 dz_compressed;
	mem_u8 min_lod;

	mem_i16x4 factors;
};
#endif

#if SMALL_TYPES
#define DerivedSetup DerivedSetupMem
#else
struct DerivedSetup
{
	u8x4 constant_muladd0;
	u8x4 constant_mulsub0;
	u8x4 constant_mul0;
	u8x4 constant_add0;

	u8x4 constant_muladd1;
	u8x4 constant_mulsub1;
	u8x4 constant_mul1;
	u8x4 constant_add1;

	u8x4 fog_color;
	u8x4 blend_color;
	uint fill_color;

	u16 dz;
	u8 dz_compressed;
	u8 min_lod;

	i16x4 factors;
};
#endif

#define ScissorStateMem ivec4

struct ScissorState
{
	int xlo, ylo, xhi, yhi;
};

const int TILE_INFO_CLAMP_S_BIT = 1 << 0;
const int TILE_INFO_MIRROR_S_BIT = 1 << 1;
const int TILE_INFO_CLAMP_T_BIT = 1 << 2;
const int TILE_INFO_MIRROR_T_BIT = 1 << 3;

#if NO_STORAGE_8BIT
struct TileInfoMem
{
	uint slo;
	uint shi;
	uint tlo;
	uint thi;
	uint offset;
	uint stride;
	uint fmt_size_palette_mask_s;
	uint shift_s_mask_t_shift_t_flags;
};
#else
struct TileInfoMem
{
	uint slo;
	uint shi;
	uint tlo;
	uint thi;
	uint offset;
	uint stride;
	mem_u8 fmt;
	mem_u8 size;
	mem_u8 palette;
	mem_u8 mask_s;
	mem_u8 shift_s;
	mem_u8 mask_t;
	mem_u8 shift_t;
	mem_u8 flags;
};
#endif

#if SMALL_TYPES
#define TileInfo TileInfoMem
#else
struct TileInfo
{
	uint slo;
	uint shi;
	uint tlo;
	uint thi;
	uint offset;
	uint stride;
	u8 fmt;
	u8 size;
	u8 palette;
	u8 mask_s;
	u8 shift_s;
	u8 mask_t;
	u8 shift_t;
	u8 flags;
};
#endif

#if NO_STORAGE_8BIT
struct StaticRasterizationStateMem
{
	uint combiner_inputs_rgb0_packed;
	uint combiner_inputs_alpha0_packed;
	uint combiner_inputs_rgb1_packed;
	uint combiner_inputs_alpha1_packed;
	uint flags;
	int dither;
	int texture_size;
	int texture_fmt;
};
#else
struct StaticRasterizationStateMem
{
	mem_u8x4 combiner_inputs_rgb0;
	mem_u8x4 combiner_inputs_alpha0;
	mem_u8x4 combiner_inputs_rgb1;
	mem_u8x4 combiner_inputs_alpha1;
	uint flags;
	int dither;
	int texture_size;
	int texture_fmt;
};
#endif

#if SMALL_TYPES
#define StaticRasterizationState StaticRasterizationStateMem
#else
struct StaticRasterizationState
{
	u8x4 combiner_inputs_rgb0;
	u8x4 combiner_inputs_alpha0;
	u8x4 combiner_inputs_rgb1;
	u8x4 combiner_inputs_alpha1;
	uint flags;
	int dither;
	int texture_size;
	int texture_fmt;
};
#endif

#if NO_STORAGE_8BIT
struct DepthBlendStateMem
{
	uint blend_modes0_packed;
	uint blend_modes1_packed;
	uint flags;
	uint coverage_mode_z_mode;
};
#else
struct DepthBlendStateMem
{
	mem_u8x4 blend_modes0;
	mem_u8x4 blend_modes1;
	uint flags;
	mem_u8 coverage_mode;
	mem_u8 z_mode;
	mem_u8 padding0;
	mem_u8 padding1;
};
#endif

#if SMALL_TYPES
#define DepthBlendState DepthBlendStateMem
#else
struct DepthBlendState
{
	u8x4 blend_modes0;
	u8x4 blend_modes1;
	uint flags;
	u8 coverage_mode;
	u8 z_mode;
	u8 padding0;
	u8 padding1;
};
#endif

#if NO_STORAGE_8BIT
struct InstanceIndicesMem
{
	uint static_depth_tmem_packed;
	uint other_packed;
	uint tile_infos_packed[2];
};
#define instance_static_depth_tmem(prim) unpack_mem_u8x4(state_indices.elems[prim].static_depth_tmem_packed)
#define instance_tile_info(prim, tile) unpack_mem_u8(state_indices.elems[prim].tile_infos_packed[(tile) >> 2], (tile) & 3)
#else
struct InstanceIndicesMem
{
	mem_u8x4 static_depth_tmem;
	mem_u8x4 other;
	mem_u8 tile_infos[8];
};
#define instance_static_depth_tmem(prim) state_indices.elems[prim].static_depth_tmem
#define instance_tile_info(prim, tile) state_indices.elems[prim].tile_infos[tile]
#endif

struct TMEMInstance16Mem
{
	mem_u16 elems[2048];
};

#if !NO_STORAGE_8BIT
struct TMEMInstance8Mem
{
	mem_u8 elems[4096];
};
#endif

struct ShadedData
{
	u8x4 combined;
	int z_dith;
	u8 coverage_count;
	u8 shade_alpha;
};

const int COVERAGE_FILL_BIT = 0x40;
const int COVERAGE_COPY_BIT = 0x20;

struct GlobalFBInfo
{
	int dx_shift;
	int dx_mask;
	int fb_size;
	uint base_primitive_index;
};

#endif