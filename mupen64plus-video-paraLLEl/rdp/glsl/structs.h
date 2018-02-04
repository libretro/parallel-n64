#ifndef STRUCTS_H_
#define STRUCTS_H_

struct WorkDescriptor
{
   uvec2 base;
   uint lod_info_primitive;
   // This really shouldn't be here, but we had 4 bytes to spare and RDPPrimitive is full ...
   uint fog_color;
};

struct Tile
{
   uint uv[TILE_SIZE_Y][TILE_SIZE_X];
   uint uv_next[TILE_SIZE_Y][TILE_SIZE_X];
   uint uv_prev_y[TILE_SIZE_Y][TILE_SIZE_X];
   int z[TILE_SIZE_Y][TILE_SIZE_X];
   uint rgba[TILE_SIZE_Y][TILE_SIZE_X];
   uint coverage[TILE_SIZE_Y][TILE_SIZE_X];
};
#define COVERAGE(tile, local) (tile_buffer.tiles[tile].coverage[local.y][local.x])
#define RGBA(tile, local) (tile_buffer.tiles[tile].rgba[local.y][local.x])
#define UV(tile, local) (tile_buffer.tiles[tile].uv[local.y][local.x])
#define UV_NEXT(tile, local) (tile_buffer.tiles[tile].uv_next[local.y][local.x])
#define UV_PREV_Y(tile, local) (tile_buffer.tiles[tile].uv_prev_y[local.y][local.x])
#define DEPTH(tile, local) (tile_buffer.tiles[tile].z[local.y][local.x])

// Reuse memory
#define SAMPLED0(tile, local) UV(tile, local)
#define SAMPLED1(tile, local) UV_NEXT(tile, local)
#define SAMPLED2(tile, local) UV_PREV_Y(tile, local)
#define COMBINED(tile, local) UV(tile, local)

struct RDPAttribute
{
   // 1 cacheline
   ivec4 rgba;
   ivec4 d_rgba_dx;
   ivec4 d_rgba_de;
   ivec4 d_rgba_dy;

   // 1 cacheline
   ivec4 stwz;
   ivec4 d_stwz_dx;
   ivec4 d_stwz_de;
   ivec4 d_stwz_dy;

   ivec4 d_rgba_diff;
   ivec4 d_stwz_diff;

   // 32 bytes tile descriptor indices.
   uint tile_descriptor[8];
};

// 256 bytes per primitive.
struct RDPPrimitive
{
   // 64 bytes, should be aligned to cache line.
   int xl, xm, xh, primitive_z;
   int yl, ym, yh; uint span_stride_combiner;
   int DxLDy, DxMDy, DxHDy; uint blend_color;
   uint flags, scissor_x, scissor_y, fill_color_blend;

   // Only read if we need to.
   // 3x64 bytes.
   RDPAttribute attr;
};

struct TileDescriptor
{
   uvec4 payload0;
   uvec4 payload1;
   uvec4 payload2;
   uvec4 payload3;
};

#define TILE_MASK(desc) ivec2(desc.payload0.xy)
#define TILE_MIRROR(desc) ivec2(desc.payload0.zw)
#define TILE_CLAMP_MIN(desc) ivec2(desc.payload1.xy)
#define TILE_CLAMP_MAX(desc) ivec2(desc.payload1.zw)
#define TILE_OFFSET(desc) uintBitsToFloat(desc.payload2.xy)
#define TILE_BASE(desc) ivec2(desc.payload2.zw)
#define TILE_SHIFT(desc) ivec2(desc.payload3.xy)
#define TILE_LAYER(desc) uintBitsToFloat(desc.payload3.z)
#define TILE_MID_TEXEL(desc) int(desc.payload3.w)

struct CombinerBaked
{
   ivec4 sub_a, sub_b, mul, add;
};

struct Combiner
{
   CombinerBaked cycle[2];
   ivec4 color[2];
   ivec4 alpha[2];
   ivec4 padding[4];
};

#endif

