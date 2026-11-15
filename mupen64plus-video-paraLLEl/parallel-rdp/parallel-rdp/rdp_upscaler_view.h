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


#ifndef RDP_UPSCALER_VIEW_H
#define RDP_UPSCALER_VIEW_H

/* The temporal upscaler's view of the RDP: what of the state triangles
 * are drawn under tells them apart from frame to frame, and the
 * upscaler's part of each scanout. C89. */

#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>
#include "device.h"
#include "primitive_decode.h"
#include "rdp_state.h"
#include "temporal_upscaler.h"
#include "upscaler_pass.h"
#include "video_interface.h"

RETRO_BEGIN_DECLS

/* The commands' words the upscaler keeps, as they were last set */
typedef struct rdp_upscaler_view
{
	uint32_t other_modes[2];
	uint32_t combine[2];
	uint32_t tile[8][2];
	/* RDRAM address of the last load (tile, block or TLUT) */
	uint32_t load_addr;
	/* the primitive depth, in the attributes' z units */
	int32_t prim_z;
	/* set other modes picks the primitive depth over the triangles' */
	bool prim_depth;
} rdp_upscaler_view_t;

/* A triangle's RDP_UPSCALER_* flags: how it meets depth, its coverage,
 * and whether it is textured, shaded and in perspective. @op is the
 * command, 0x08 to 0x0f. */
unsigned rdp_upscaler_view_flags(unsigned op, const rdp_static_state_t *static_state,
		const rdp_depth_blend_state_t *depth_blend, bool prim_depth);

/* What tells triangles drawn alike from others: @op, the other modes,
 * the combiner, the words of @tile and the last load's address */
uint64_t rdp_upscaler_view_signature(const rdp_upscaler_view_t *view, unsigned op, unsigned tile);

/* Before a triangle is drawn: hands it to @u, while @u is active, and
 * takes its jittered edges and attributes back into @setup and @attr */
void rdp_upscaler_view_triangle(rdp_upscaler_t *u, const rdp_upscaler_view_t *view, unsigned op,
		const rdp_static_state_t *static_state, const rdp_depth_blend_state_t *depth_blend, unsigned scale,
		rdp_triangle_setup_t *setup, rdp_attribute_setup_t *attr);

enum rdp_upscaler_frame
{
	/* nothing to show: the targets go */
	RDP_UPSCALER_FRAME_NONE = 0,
	/* the frame before, shown again: its targets stay, with no reset */
	RDP_UPSCALER_FRAME_REPEAT,
	/* a new frame: @targets holds its own */
	RDP_UPSCALER_FRAME_NEW
};

/* A scanout for the upscaler, at @scale: the VI's image (a reference,
 * or NULL) and, in @frame, what became of the targets. For a new frame
 * its triangles are resolved and its motion vectors and depth drawn, in
 * one submission, into @targets, which holds nothing before; a repeated
 * frame or none submits nothing. */
granite_image_t *rdp_upscaler_view_scanout(rdp_upscaler_t *u, rdp_vi_t *vi, granite_device_t *device,
		unsigned scale, unsigned jitter_phase, bool reset, rdp_upscaler_targets_t *targets,
		enum rdp_upscaler_frame *frame);

RETRO_END_DECLS

#endif
