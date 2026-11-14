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

#pragma once

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "rdp_common.hpp"
#include "primitive_decode.h"
#include "tmem_load.h"

namespace RDP
{
// A primitive's edges and coefficients, and the edges' flags (primitive_decode.h)
using TriangleSetupFlags = uint8_t;
using TriangleSetup = ::rdp_triangle_setup_t;
using AttributeSetup = ::rdp_attribute_setup_t;

using StaticRasterizationFlags = uint32_t;
using DepthBlendFlags = uint32_t;

using ConstantCombinerInputs = ::rdp_constant_combiner_inputs_t;
using DerivedSetup = ::rdp_derived_setup_t;

static_assert((sizeof(TriangleSetup) & 15) == 0, "TriangleSetup must be aligned to 16 bytes.");
static_assert((sizeof(AttributeSetup) & 15) == 0, "AttributeSetup must be aligned to 16 bytes.");
static_assert(sizeof(DerivedSetup) == 56, "DerivedSetup is not 56 bytes.");

using ScissorState = ::rdp_scissor_state_t;
using StaticRasterizationState = ::rdp_static_state_t;
static_assert(sizeof(StaticRasterizationState) == 32, "StaticRasterizationState must be 32 bytes.");
using DepthBlendState = ::rdp_depth_blend_state_t;
static_assert(sizeof(DepthBlendState) == 16, "DepthBlendState must be 16 bytes.");

using UploadInfo = ::rdp_upload_info_t;
static_assert(sizeof(UploadInfo) == 64, "UploadInfo must be 64 bytes.");

struct SpanSetup
{
	int32_t r, g, b, a;
	int32_t s, t, w, z;

	int16_t xlo[4];
	int16_t xhi[4];

	int32_t interpolation_base_x;
	int32_t start_x;
	int32_t end_x;
	int16_t lodlength;
	uint16_t valid_line;
};
static_assert((sizeof(SpanSetup) & 15) == 0, "SpanSetup is not aligned to 16 bytes.");

struct TileRasterWork
{
	uint32_t tile_x, tile_y;
	uint32_t tile_instance;
	uint32_t primitive;
};
static_assert((sizeof(TileRasterWork) == 16), "TileRasterWork is not 16 bytes.");

namespace Limits
{
constexpr unsigned MaxPrimitives = 256;
constexpr unsigned MaxStaticRasterizationStates = 64;
constexpr unsigned MaxDepthBlendStates = 64;
constexpr unsigned MaxTileInfoStates = 256;
constexpr unsigned NumSyncStates = 32;
constexpr unsigned MaxNumTiles = 8;
constexpr unsigned MaxTMEMInstances = 256;
constexpr unsigned MaxSpanSetups = 32 * 1024;
constexpr unsigned MaxWidth = 1024;
constexpr unsigned MaxHeight = 1024;
constexpr unsigned MaxTileInstances = 0x8000;
}

namespace ImplementationConstants
{
constexpr unsigned DefaultWorkgroupSize = 64;

constexpr unsigned TileWidth = 8;
constexpr unsigned TileHeight = 8;
constexpr unsigned MaxTilesX = Limits::MaxWidth / TileWidth;
constexpr unsigned MaxTilesY = Limits::MaxHeight / TileHeight;
constexpr unsigned MaxPendingRenderPassesBeforeFlush = 8;
constexpr unsigned MinimumPrimitivesForIdleFlush = 32;
constexpr unsigned MinimumRenderPassesForIdleFlush = 2;
}

// Whether an interpolated X span reaches past +-limit. Interpolation
// clamps to int32, and std::abs of INT_MIN is not representable, so the
// magnitudes are taken in 64 bits.
}
