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

#include <string.h>
#include "vi_registers.h"

/* Horizontal bounding boxes gathered over the lines */
typedef struct horizontal_bounds
{
	int h_start_lo, h_end_hi, h_start_clamp_lo, h_end_clamp_hi;
} horizontal_bounds_t;

static void analyze_line(rdp_vi_decoded_t *reg, horizontal_bounds_t *b, int y_start, int y_add,
		int x_start, int x_add, int h_start, int h_end, rdp_vi_horizontal_info_t *line)
{
	/* Clamp horizontal region to [0, 640]. */
	bool left_clamp = false;
	bool right_clamp = false;
	int h_start_clamp, h_end_clamp, h_res, value;

	h_start -= reg->is_pal ? VI_H_OFFSET_PAL : VI_H_OFFSET_NTSC;
	h_end -= reg->is_pal ? VI_H_OFFSET_PAL : VI_H_OFFSET_NTSC;

	if (h_start < 0)
	{
		x_start -= x_add * h_start;
		h_start = 0;
		/* Reference weirdness that doesn't really make sense. */
		left_clamp = true;
	}

	if (h_end > VI_SCANOUT_WIDTH)
	{
		h_end = VI_SCANOUT_WIDTH;
		/* Reference weirdness that doesn't really make sense. */
		right_clamp = true;
	}

	h_start_clamp = h_start + (left_clamp ? 0 : 8);
	h_end_clamp = h_end - (right_clamp ? 0 : 7);

	/* Effectively, these are bounding boxes. */
	h_res = h_end - h_start;
	value = (x_start + h_res * x_add) >> 10;
	if (value > reg->max_x)
		reg->max_x = value;
	value = (x_start + (h_start_clamp - h_start) * x_add) >> 10;
	if (value < reg->min_x)
		reg->min_x = value;
	value = (x_start + (h_end_clamp - h_start) * x_add + 1023) >> 10;
	if (value > reg->max_x_clamp)
		reg->max_x_clamp = value;
	if (h_start < b->h_start_lo)
		b->h_start_lo = h_start;
	if (h_end > b->h_end_hi)
		b->h_end_hi = h_end;
	if (h_start_clamp < b->h_start_clamp_lo)
		b->h_start_clamp_lo = h_start_clamp;
	if (h_end_clamp > b->h_end_clamp_hi)
		b->h_end_clamp_hi = h_end_clamp;

	line->h_start = h_start;
	line->h_start_clamp = h_start_clamp;
	line->h_end_clamp = h_end_clamp;
	line->x_start = x_start;
	line->x_add = x_add;
	line->y_start = y_start;
	line->y_add = y_add;
	line->y_base = 0;
}

void rdp_vi_decode_registers(const uint32_t *registers, rdp_vi_decoded_t *reg, rdp_vi_horizontal_info_t *lines)
{
	/* It might be possible to change YStart/YAdd per scanline, but
	 * it's dubious ... */
	int y_start = (registers[RDP_VI_REGISTER_Y_SCALE] >> 16) & 0xfff;
	int y_add = registers[RDP_VI_REGISTER_Y_SCALE] & 0xfff;
	/* Must be constant for a frame, or things won't make any sense. */
	int v_end = registers[RDP_VI_REGISTER_V_START] & 0x3ff;
	int v_sync = registers[RDP_VI_REGISTER_V_SYNC] & 0x3ff;
	int x_start = (registers[RDP_VI_REGISTER_X_SCALE] >> 16) & 0xfff;
	int x_add = registers[RDP_VI_REGISTER_X_SCALE] & 0xfff;
	int h_start = (registers[RDP_VI_REGISTER_H_START] >> 16) & 0x3ff;
	int h_end = registers[RDP_VI_REGISTER_H_START] & 0x3ff;
	rdp_vi_horizontal_info_t line_info;
	horizontal_bounds_t bounds;
	int v_end_max, v_start_offset, line;

	memset(reg, 0, sizeof(*reg));

	reg->status = registers[RDP_VI_REGISTER_CONTROL];
	reg->vi_width = registers[RDP_VI_REGISTER_WIDTH] & 0xfff;
	reg->vi_offset = registers[RDP_VI_REGISTER_ORIGIN] & 0xffffff;
	reg->v_current_line = registers[RDP_VI_REGISTER_V_CURRENT_LINE] & 1;
	reg->v_start = (registers[RDP_VI_REGISTER_V_START] >> 16) & 0x3ff;
	reg->init_y_add = y_add;

	reg->is_pal = v_sync > VI_V_SYNC_NTSC + 25;

	/* Clamp vertical ranges. Restrict the area we might have to scan
	 * out to AA buffers as a performance optimization and safety since
	 * we iterate over v_start/v_end later. */
	v_end_max = reg->is_pal ? VI_V_END_PAL : VI_V_END_NTSC;
	if (v_end > v_end_max)
		v_end = v_end_max;
	if (reg->v_start > v_end_max)
		reg->v_start = v_end_max;

	v_start_offset = reg->is_pal ? VI_V_OFFSET_PAL : VI_V_OFFSET_NTSC;
	reg->v_res = (v_end - reg->v_start) >> 1;
	reg->v_start = (reg->v_start - v_start_offset) / 2;

	if (reg->v_start < 0)
	{
		y_start -= y_add * reg->v_start;
		/* v_res is not adjusted here for some reason, but h_res is? */
		reg->v_start = 0;
	}

	/* Clamp v_res to scanout range. */
	if (reg->v_res > VI_MAX_OUTPUT_SCANLINES - reg->v_start)
		reg->v_res = VI_MAX_OUTPUT_SCANLINES - reg->v_start;

	/* Horizontal shenanigans. */
	bounds.h_start_clamp_lo = INT32_MAX;
	bounds.h_end_clamp_hi = 0;
	bounds.h_start_lo = INT32_MAX;
	bounds.h_end_hi = 0;
	reg->max_x = 0;
	reg->min_x = INT32_MAX;
	reg->max_x_clamp = 0;

	/* Need to make sure we update bounding box state for X at least,
	 * even when no line is shown. This is treated as a degenerate
	 * frame, not necessarily invalid frame (null handle). This is to
	 * have same behavior as reference. */
	analyze_line(reg, &bounds, y_start, y_add, x_start, x_add, h_start, h_end, &line_info);

	if (lines)
	{
		/* (clear out the lines nothing is shown on) */
		memset(lines, 0, VI_MAX_OUTPUT_SCANLINES * sizeof(*lines));
		for (line = reg->v_start; line < reg->v_start + reg->v_res; line++)
			lines[line] = line_info;
	}

	/* Effectively, these are bounding boxes. */
	reg->max_y = (y_start + reg->v_res * y_add) >> 10;
	reg->h_start = bounds.h_start_lo;
	reg->h_res = bounds.h_end_hi - bounds.h_start_lo;
	reg->h_start_clamp = bounds.h_start_clamp_lo;
	reg->h_res_clamp = bounds.h_end_clamp_hi - bounds.h_start_clamp_lo;
}

void rdp_vi_scanout_memory_range(const uint32_t *registers, unsigned *offset, unsigned *length)
{
	rdp_vi_decoded_t reg;
	bool divot;
	int aa_width, aa_height, x_off, y_off, pixel_size;

	rdp_vi_decode_registers(registers, &reg, NULL);

	*offset = 0;
	*length = 0;
	if (reg.vi_offset == 0 || reg.h_res <= 0 || reg.h_start >= VI_SCANOUT_WIDTH)
		return;

	divot = (reg.status & VI_CONTROL_DIVOT_ENABLE_BIT) != 0;

	/* Need to sample a 2-pixel border to have room for AA filter and
	 * divot. */
	aa_width = reg.max_x + 2 + 4 + (divot ? 2 : 0);
	/* 1 pixel border on top and bottom. */
	aa_height = reg.max_y + 1 + 4;

	x_off = divot ? -3 : -2;
	y_off = -2;

	pixel_size = ((reg.status & VI_CONTROL_TYPE_MASK) | VI_CONTROL_TYPE_RGBA5551_BIT) == VI_CONTROL_TYPE_RGBA8888_BIT ?
		4 : 2;
	reg.vi_offset &= ~(pixel_size - 1);
	reg.vi_offset += (x_off + y_off * reg.vi_width) * pixel_size;

	*offset = (unsigned)reg.vi_offset;
	*length = (unsigned)((aa_height * reg.vi_width + aa_width) * pixel_size);
}
