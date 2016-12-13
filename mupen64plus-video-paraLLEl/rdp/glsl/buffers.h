#ifndef BUFFERS_H_
#define BUFFERS_H_

#define MAX_COMBINERS 64
#define MAX_PRIMITIVES 1024u
#define MAX_PRIMITIVES_LOG2 12u

// A large 2D array texture holding all arrays.
layout(set = 0, binding = 0) uniform mediump usampler2DArray uDitherLUT;
layout(set = 0, binding = 1) uniform mediump usampler2D uCentroidLUT;
layout(set = 0, binding = 2, std140) uniform ZLUT
{
   ivec2 encode[128];
   ivec2 decode[8];
} depth_lut;

layout(std430, set = 1, binding = 0) readonly buffer PrimitiveData
{
   RDPPrimitive data[];
} primitive_data;

layout(std430, set = 1, binding = 1) readonly buffer TileListsHeader
{
   uint heads[];
} tile_list_header;

layout(std430, set = 1, binding = 2) readonly buffer TileList
{
   uvec2 nodes[];
} tile_list;

layout(std430, set = 1, binding = 3) buffer Framebuffer
{
   uint data[];
} framebuffer;

layout(std430, set = 1, binding = 4) buffer FramebufferDepth
{
   uint data[];
} framebuffer_depth;

layout(std140, set = 1, binding = 5) uniform CombinerData
{
   Combiner combiners[MAX_COMBINERS];
} combiner_data;

layout(std430, set = 1, binding = 6) readonly buffer TileDescriptors
{
   TileDescriptor descriptors[];
} tile_descriptors;

layout(set = 1, binding = 7) uniform mediump usampler2DArray uTilemap;

layout(std430, set = 1, binding = 8) readonly buffer WorkList
{
   WorkDescriptor desc[];
} work;

layout(std430, set = 1, binding = 9) buffer TileMemory
{
   Tile tiles[];
} tile_buffer;

layout(push_constant, std430) uniform PushConstant
{
   uvec2 FramebufferSize;
   vec2 InvSizeTilemap;
   uint TilesX;
   int seed;
} constants;

#endif
