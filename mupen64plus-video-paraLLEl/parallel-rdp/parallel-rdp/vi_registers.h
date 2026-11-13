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

#ifndef RDP_VI_REGISTERS_H
#define RDP_VI_REGISTERS_H

#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>
#include <retro_inline.h>

RETRO_BEGIN_DECLS

enum rdp_vi_register
{
	RDP_VI_REGISTER_CONTROL = 0,
	RDP_VI_REGISTER_ORIGIN,
	RDP_VI_REGISTER_WIDTH,
	RDP_VI_REGISTER_INTR,
	RDP_VI_REGISTER_V_CURRENT_LINE,
	RDP_VI_REGISTER_TIMING,
	RDP_VI_REGISTER_V_SYNC,
	RDP_VI_REGISTER_H_SYNC,
	RDP_VI_REGISTER_LEAP,
	RDP_VI_REGISTER_H_START,
	RDP_VI_REGISTER_V_START,
	RDP_VI_REGISTER_V_BURST,
	RDP_VI_REGISTER_X_SCALE,
	RDP_VI_REGISTER_Y_SCALE,
	RDP_VI_REGISTER_COUNT
};

enum VIControlFlagBits
{
	VI_CONTROL_TYPE_BLANK_BIT = 0 << 0,
	VI_CONTROL_TYPE_RESERVED_BIT = 1 << 0,
	VI_CONTROL_TYPE_RGBA5551_BIT = 2 << 0,
	VI_CONTROL_TYPE_RGBA8888_BIT = 3 << 0,
	VI_CONTROL_TYPE_MASK = 3 << 0,
	VI_CONTROL_GAMMA_DITHER_ENABLE_BIT = 1 << 2,
	VI_CONTROL_GAMMA_ENABLE_BIT = 1 << 3,
	VI_CONTROL_DIVOT_ENABLE_BIT = 1 << 4,
	VI_CONTROL_SERRATE_BIT = 1 << 6,
	VI_CONTROL_AA_MODE_RESAMP_EXTRA_ALWAYS_BIT = 0 << 8,
	VI_CONTROL_AA_MODE_RESAMP_EXTRA_BIT = 1 << 8,
	VI_CONTROL_AA_MODE_RESAMP_ONLY_BIT = 2 << 8,
	VI_CONTROL_AA_MODE_RESAMP_REPLICATE_BIT = 3 << 8,
	VI_CONTROL_AA_MODE_MASK = 3 << 8,
	VI_CONTROL_DITHER_FILTER_ENABLE_BIT = 1 << 16,
	VI_CONTROL_META_AA_BIT = 1 << 17,
	VI_CONTROL_META_SCALE_BIT = 1 << 18
};

enum
{
	VI_V_SYNC_NTSC = 525,
	VI_V_SYNC_PAL = 625,
	VI_H_OFFSET_NTSC = 108,
	VI_H_OFFSET_PAL = 128,
	VI_V_OFFSET_NTSC = 34,
	VI_V_OFFSET_PAL = 44,
	VI_V_RES_NTSC = 480,
	VI_V_RES_PAL = 576,
	VI_SCANOUT_WIDTH = 640,
	VI_V_RES_MAX = VI_V_RES_PAL,
	/* Handle odd v_start as well. Needed for rounding to work in our favor. */
	VI_V_END_PAL = (VI_V_OFFSET_PAL + VI_V_RES_PAL) | 1,
	VI_V_END_NTSC = (VI_V_OFFSET_NTSC + VI_V_RES_NTSC) | 1,
	VI_V_END_MAX = VI_V_END_PAL,
	VI_MAX_OUTPUT_SCANLINES = VI_V_RES_MAX >> 1
};

/* What the scale pass reads for one output line (the shader's
 * HorizontalInfo) */
typedef struct rdp_vi_horizontal_info
{
	int32_t h_start;
	int32_t h_start_clamp;
	int32_t h_end_clamp;
	int32_t x_start;
	int32_t x_add;
	int32_t y_start;
	int32_t y_add;
	int32_t y_base;
} rdp_vi_horizontal_info_t;

/* The VI registers, as a scanout uses them */
typedef struct rdp_vi_decoded
{
	int vi_width;
	int vi_offset;
	int v_current_line;
	bool is_pal;
	uint32_t status;

	int init_y_add;

	/* Global scale pass scissor box. */
	int h_start_clamp, h_res_clamp;
	int h_start, h_res;
	int v_start, v_res;

	/* For AA stages. */
	int max_x, max_y;
	/* The framebuffer pixels a line shows: from min_x, right of the
	 * guard band the scanout blanks, to the one at the right. */
	int min_x, max_x_clamp;
} rdp_vi_decoded_t;

/* @registers: RDP_VI_REGISTER_COUNT of them. @lines: NULL, or
 * VI_MAX_OUTPUT_SCANLINES entries, which get each output line's state
 * (all zero for the lines nothing is shown on).
 *
 * The basic formula is that a frame is counted with an active
 * horizontal range of X(range) = [H_OFFSET, H_OFFSET + H_RES], giving
 * 640 output pixels per line. Similarly, vertical scanout has an active
 * range of Y(range) = [V_OFFSET, V_OFFSET + V_RES]. Y is counted in
 * terms of interlaced lines (i.e. 480 and 576). We will scan out half
 * of these per field (e.g. 240p or 480i for NTSC). The HStart and
 * VStart registers are used to signal where on screen we render. HStart
 * and VStart registers might carve out a portion of the screen, or use
 * a larger one, the active area on screen is an intersection of the VI
 * register state and the X(range)/Y(range). When the X counter hits
 * HStart, we begin computing the X coordinate we want to sample based
 * on X(sample) = XStart + XAdd * (X - HStart). Similarly, Y(sample) =
 * YStart + YAdd * (Y - (VStart >> 1)), YAdd increments once per
 * scanline. We always normalize the interpolations to be progressive.
 * Interlacing just shifts positions on screen after the fact.
 *
 * VRAM(X, Y) is fetched with any post-processing required, looking at
 * neighboring VRAM pixels. For this reason, we compute the maximum X
 * and Y we might access, and build an X x Y image which is already
 * preprocessed with AA, Divot filters, etc. The final scaling pass
 * interpolates that result. The mental model here is that the VI could
 * have a line buffer to keep some scanlines in cache to support the
 * processing. XStart/YStart registers just control how fast we iterate
 * through these lines, which implements scaling effects.
 *
 * As another weird quirk, it seems like we need to account for a 8
 * pixel guard band horizontally (h_start_clamp / h_res_clamp) if we
 * begin scanout inside the active region for whatever reason. This is
 * to match reference. */
void rdp_vi_decode_registers(const uint32_t *registers, rdp_vi_decoded_t *decoded, rdp_vi_horizontal_info_t *lines);

/* The bytes of RDRAM a scanout with these registers reads: @offset and
 * @length, both 0 when it shows nothing */
void rdp_vi_scanout_memory_range(const uint32_t *registers, unsigned *offset, unsigned *length);

/* If we risk sampling same Y coordinate for two scanlines we can
 * trigger the fetch bug, so there are workaround paths for it. */
static INLINE bool rdp_vi_need_fetch_bug_emulation(const rdp_vi_decoded_t *decoded, unsigned scaling_factor)
{
	return decoded->init_y_add < 1024 && scaling_factor == 1;
}

RETRO_END_DECLS

#endif
