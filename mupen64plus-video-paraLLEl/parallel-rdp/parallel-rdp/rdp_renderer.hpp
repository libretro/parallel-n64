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

/* The renderer (rdp_renderer.c), for the command processor */

#include "rdp_data_structures.hpp"
#include "device.hpp"
#include "rdp_common.hpp"
#include "rdp_renderer.h"

namespace RDP
{
using CoherencyOperation = ::rdp_timeline_op_t;
using CoherencyCopy = ::rdp_coherency_copy_t;
// A load command (tmem_load.h)
using LoadTileInfo = ::rdp_load_tile_info_t;

static_assert(RDP_QUEUE_MAX_PRIMITIVES == Limits::MaxPrimitives &&
              RDP_QUEUE_MAX_STATIC_STATES == Limits::MaxStaticRasterizationStates &&
              RDP_QUEUE_MAX_DEPTH_BLEND_STATES == Limits::MaxDepthBlendStates &&
              RDP_QUEUE_MAX_TILE_INFOS == Limits::MaxTileInfoStates &&
              RDP_QUEUE_MAX_UPLOADS == Limits::MaxTMEMInstances &&
              RDP_QUEUE_MAX_SPAN_SETUPS == Limits::MaxSpanSetups &&
              RDP_QUEUE_SPAN_JOB_LINES == ImplementationConstants::DefaultWorkgroupSize,
              "rdp_queue.h queues for other limits.");
static_assert((sizeof(rdp_instance_indices_t) & 15) == 0 && sizeof(rdp_span_info_offsets_t) == 16 &&
              sizeof(rdp_span_job_t) == 8, "The shaders read other layouts.");
static_assert(RDP_SYNC_RING_SIZE == Limits::NumSyncStates, "rdp_sync_ring.h rotates another number of slots.");
static_assert(RDP_RENDERER_SPAN_SETUP_SIZE == sizeof(SpanSetup), "rdp_renderer.c sizes span setups otherwise.");
static_assert((Limits::MaxStaticRasterizationStates % ImplementationConstants::DefaultWorkgroupSize) == 0,
              "MaxStaticRasterizationStates does not align.");
static_assert((Limits::MaxPrimitives % 32) == 0 && Limits::MaxPrimitives <= 32 * 32,
              "MaxPrimitives must be a multiple of 32, at most 1024.");
static_assert(RDP_CAPS_MAX_WIDTH == Limits::MaxWidth && RDP_CAPS_MAX_HEIGHT == Limits::MaxHeight &&
              RDP_CAPS_TILE_WIDTH == ImplementationConstants::TileWidth &&
              RDP_CAPS_TILE_HEIGHT == ImplementationConstants::TileHeight &&
              RDP_CAPS_MAX_PRIMITIVES == Limits::MaxPrimitives &&
              RDP_CAPS_MAX_STATIC_RASTER_STATES == Limits::MaxStaticRasterizationStates &&
              RDP_CAPS_MAX_TILE_INSTANCES == Limits::MaxTileInstances &&
              RDP_CAPS_TILE_RASTER_WORK_SIZE == sizeof(TileRasterWork),
              "renderer_caps.h sizes buffers for other limits.");

enum class FBFormat : uint32_t
{
	I4 = RDP_FB_I4,
	I8 = RDP_FB_I8,
	RGBA5551 = RDP_FB_RGBA5551,
	IA88 = RDP_FB_IA88,
	RGBA8888 = RDP_FB_RGBA8888
};

struct RendererOptions
{
	unsigned upscaling_factor = 1;
	bool super_sampled_readback = false;
	bool super_sampled_readback_dither = false;
	// The device has no storageBuffer8BitAccess: use the NO_STORAGE_8BIT
	// shader variants, whose hidden RDRAM is 16 bits per entry.
	bool no_storage_8bit = false;
};

enum class ValidationError
{
	Fill4bpp = RDP_VALIDATION_FILL_4BPP,
	LoadTile4bpp = RDP_VALIDATION_LOAD_TILE_4BPP,
	InvalidMultilineLoadTlut = RDP_VALIDATION_INVALID_MULTILINE_LOAD_TLUT,
	FillDepthTest = RDP_VALIDATION_FILL_DEPTH_TEST,
	FillDepthWrite = RDP_VALIDATION_FILL_DEPTH_WRITE,
	FillImageReadEnable = RDP_VALIDATION_FILL_IMAGE_READ_ENABLE,
	Copy32bpp = RDP_VALIDATION_COPY_32BPP
};

struct ValidationInterface
{
	// Reports, on the thread that processes commands, what would crash a
	// real RDP; it is drawn as well as it can be. Only of use while
	// developing N64 homebrew.
	void (*report_rdp_crash)(void *user, ValidationError err, const char *msg);
	void *user;
};

}
