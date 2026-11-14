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

struct InstanceIndices
{
	uint8_t static_index;
	uint8_t depth_blend_index;
	uint8_t tile_instance_index;
	uint8_t padding[5];
	uint8_t tile_indices[8];
};
static_assert((sizeof(InstanceIndices) & 15) == 0, "InstanceIndices must be aligned to 16 bytes.");

struct UploadInfo
{
	int32_t width, height;
	float min_t_mod, max_t_mod;

	int32_t vram_addr;
	int32_t vram_width;
	int32_t vram_size;
	int32_t vram_effective_width;

	int32_t tmem_offset;
	int32_t tmem_stride_words;
	int32_t tmem_size;
	int32_t tmem_fmt;

	int32_t mode;
	float inv_tmem_stride_words;
	int32_t dxt;
	int32_t padding;
};
static_assert((sizeof(UploadInfo) & 15) == 0, "UploadInfo must be aligned to 16 bytes.");

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

struct SpanInfoOffsets
{
	int32_t offset, ylo, yhi, padding;
};
static_assert((sizeof(SpanInfoOffsets) == 16), "SpanInfoOffsets is not 16 bytes.");

struct SpanInterpolationJob
{
	uint16_t primitive_index, base_y, max_y, padding;
};
static_assert((sizeof(SpanInterpolationJob) == 8), "SpanInterpolationJob is not 8 bytes.");

struct GlobalState
{
	uint32_t addr_index;
	uint32_t depth_addr_index;
	uint32_t fb_width, fb_height;
	uint32_t group_mask;
};

struct TileRasterWork
{
	uint32_t tile_x, tile_y;
	uint32_t tile_instance;
	uint32_t primitive;
};
static_assert((sizeof(TileRasterWork) == 16), "TileRasterWork is not 16 bytes.");

struct GlobalFBInfo
{
	uint32_t dx_shift;
	uint32_t dx_mask;
	uint32_t fb_size;
	uint32_t base_primitive_index;
};

template <typename T, unsigned N>
class StateCache
{
public:
	unsigned add(const T &t)
	{
		if (cached_index >= 0)
			if (memcmp(&elements[cached_index], &t, sizeof(T)) == 0)
				return unsigned(cached_index);

		for (int i = int(count) - 1; i >= 0; i--)
		{
			if (memcmp(&elements[i], &t, sizeof(T)) == 0)
			{
				cached_index = i;
				return unsigned(i);
			}
		}

		assert(count < N);
		memcpy(elements + count, &t, sizeof(T));
		unsigned ret = count++;
		cached_index = int(ret);
		return ret;
	}

	bool full() const
	{
		return count == N;
	}

	unsigned size() const
	{
		return count;
	}

	unsigned byte_size() const
	{
		return size() * sizeof(T);
	}

	const T *data() const
	{
		return elements;
	}

	void reset()
	{
		count = 0;
		cached_index = -1;
	}

	bool empty() const
	{
		return count == 0;
	}

private:
	unsigned count = 0;
	int cached_index = -1;
	T elements[N];
};

template <typename T, unsigned N>
class StreamCache
{
public:
	// Built where it is uploaded from: storage is the mapped buffer, N long.
	void bind(void *storage)
	{
		elements = static_cast<T *>(storage);
	}

	void add(const T &t)
	{
		assert(count < N);
		memcpy(&elements[count++], &t, sizeof(T));
	}

	bool full() const
	{
		return count == N;
	}

	unsigned size() const
	{
		return count;
	}

	unsigned byte_size() const
	{
		return size() * sizeof(T);
	}

	const T *data() const
	{
		return elements;
	}

	void reset()
	{
		count = 0;
	}

	bool empty() const
	{
		return count == 0;
	}

private:
	unsigned count = 0;
	T *elements = nullptr;
};

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
constexpr unsigned IncoherentPageSize = 1024;
constexpr unsigned MaxPendingRenderPassesBeforeFlush = 8;
constexpr unsigned MinimumPrimitivesForIdleFlush = 32;
constexpr unsigned MinimumRenderPassesForIdleFlush = 2;
}

// Whether an interpolated X span reaches past +-limit. Interpolation
// clamps to int32, and std::abs of INT_MIN is not representable, so the
// magnitudes are taken in 64 bits.
}
