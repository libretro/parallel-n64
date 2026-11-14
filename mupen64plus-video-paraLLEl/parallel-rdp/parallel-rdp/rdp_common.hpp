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

// The registers of the VI, its control bits and timing constants (C).
#include "vi_registers.h"
// The RDP's state, as primitives are drawn with it (C).
#include "rdp_state.h"

namespace Vulkan
{
class Program;
class Shader;
}

// In place of <utility> and <algorithm>: the same code, without the
// standard library headers.
template <typename T>
static inline T &&rdp_move(T &v)
{
	return static_cast<T &&>(v);
}

template <typename T>
static inline const T &rdp_min(const T &a, const T &b)
{
	return b < a ? b : a;
}

template <typename T>
static inline const T &rdp_max(const T &a, const T &b)
{
	return a < b ? b : a;
}

// The shaders (shader_bank.h).
struct rdp_shader_bank;

namespace RDP
{
using ShaderBank = ::rdp_shader_bank;

// list of command IDs
enum class Op
{
	Nop = 0,

	MetaSignalTimeline = 1,
	MetaFlush = 2,
	MetaIdle = 3,
	MetaSetQuirks = 4,
	MetaScanoutPrepare = 5,
	MetaNextFrameContext = 6,
	MetaCall = 7,

	FillTriangle = 0x08,
	FillZBufferTriangle = 0x09,
	TextureTriangle = 0x0a,
	TextureZBufferTriangle = 0x0b,
	ShadeTriangle = 0x0c,
	ShadeZBufferTriangle = 0x0d,
	ShadeTextureTriangle = 0x0e,
	ShadeTextureZBufferTriangle = 0x0f,
	TextureRectangle = 0x24,
	TextureRectangleFlip = 0x25,
	SyncLoad = 0x26,
	SyncPipe = 0x27,
	SyncTile = 0x28,
	SyncFull = 0x29,
	SetKeyGB = 0x2a,
	SetKeyR = 0x2b,
	SetConvert = 0x2c,
	SetScissor = 0x2d,
	SetPrimDepth = 0x2e,
	SetOtherModes = 0x2f,
	LoadTLut = 0x30,
	SetTileSize = 0x32,
	LoadBlock = 0x33,
	LoadTile = 0x34,
	SetTile = 0x35,
	FillRectangle = 0x36,
	SetFillColor = 0x37,
	SetFogColor = 0x38,
	SetBlendColor = 0x39,
	SetPrimColor = 0x3a,
	SetEnvColor = 0x3b,
	SetCombine = 0x3c,
	SetTextureImage = 0x3d,
	SetMaskImage = 0x3e,
	SetColorImage = 0x3f
};

// The combiner's, the blender's and the tiles' state (rdp_state.h)
using TileInfoFlags = uint8_t;
using TileSize = ::rdp_tile_size_t;
using TileMeta = ::rdp_tile_meta_t;
using TileInfo = ::rdp_tile_info_t;
using CombinerInputs = ::rdp_combiner_inputs_t;
using BlendModes = ::rdp_blend_modes_t;

static_assert(sizeof(TileInfo) == 32, "TileInfo must be 32 bytes.");

enum class VIRegister
{
	Control = RDP_VI_REGISTER_CONTROL,
	Origin = RDP_VI_REGISTER_ORIGIN,
	Width = RDP_VI_REGISTER_WIDTH,
	Intr = RDP_VI_REGISTER_INTR,
	VCurrentLine = RDP_VI_REGISTER_V_CURRENT_LINE,
	Timing = RDP_VI_REGISTER_TIMING,
	VSync = RDP_VI_REGISTER_V_SYNC,
	HSync = RDP_VI_REGISTER_H_SYNC,
	Leap = RDP_VI_REGISTER_LEAP,
	HStart = RDP_VI_REGISTER_H_START,
	VStart = RDP_VI_REGISTER_V_START,
	VBurst = RDP_VI_REGISTER_V_BURST,
	XScale = RDP_VI_REGISTER_X_SCALE,
	YScale = RDP_VI_REGISTER_Y_SCALE,
	Count = RDP_VI_REGISTER_COUNT
};

template <int bits>
static int32_t sext(int32_t v)
{
	struct { int32_t dummy : bits; } d;
	d.dummy = v;
	return d.dummy;
}
}