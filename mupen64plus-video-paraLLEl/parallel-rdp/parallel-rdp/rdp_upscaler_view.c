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
#include "rdp_upscaler_view.h"

unsigned rdp_upscaler_view_flags(unsigned op, const rdp_static_state_t *static_state,
		const rdp_depth_blend_state_t *depth_blend, bool prim_depth)
{
	unsigned flags = 0;

	/* Fill and copy cycles leave the depth buffer alone */
	if ((static_state->flags & (RASTERIZATION_FILL_BIT | RASTERIZATION_COPY_BIT)) == 0)
	{
		if (depth_blend->flags & DEPTH_BLEND_DEPTH_TEST_BIT)
			flags |= RDP_UPSCALER_DEPTH_TEST;
		if (depth_blend->flags & DEPTH_BLEND_DEPTH_UPDATE_BIT)
			flags |= RDP_UPSCALER_DEPTH_WRITE;
		/* (a decal moves with the surface it lies on) */
		else if ((depth_blend->flags & DEPTH_BLEND_FORCE_BLEND_BIT) && depth_blend->z_mode != RDP_Z_MODE_DECAL)
			flags |= RDP_UPSCALER_TRANSLUCENT;
	}
	if (prim_depth)
		flags |= RDP_UPSCALER_DEPTH_CONSTANT;
	if (static_state->flags & (RASTERIZATION_AA_BIT | RASTERIZATION_FILL_BIT | RASTERIZATION_COPY_BIT))
		flags |= RDP_UPSCALER_ANY_COVERAGE;
	if (op & 2)
		flags |= RDP_UPSCALER_TEXTURED;
	if (op & 4)
		flags |= RDP_UPSCALER_SHADED;
	if (static_state->flags & RASTERIZATION_PERSPECTIVE_CORRECT_BIT)
		flags |= RDP_UPSCALER_PERSPECTIVE;
	return flags;
}

uint64_t rdp_upscaler_view_signature(const rdp_upscaler_view_t *view, unsigned op, unsigned tile)
{
	uint64_t h = rdp_upscaler_hash_init();
	tile &= 7;
	h = rdp_upscaler_hash(h, op);
	h = rdp_upscaler_hash(h, view->other_modes[0]);
	h = rdp_upscaler_hash(h, view->other_modes[1]);
	h = rdp_upscaler_hash(h, view->combine[0]);
	h = rdp_upscaler_hash(h, view->combine[1]);
	h = rdp_upscaler_hash(h, view->tile[tile][0]);
	h = rdp_upscaler_hash(h, view->tile[tile][1]);
	return rdp_upscaler_hash(h, view->load_addr);
}

void rdp_upscaler_view_triangle(rdp_upscaler_t *u, const rdp_upscaler_view_t *view, unsigned op,
		const rdp_static_state_t *static_state, const rdp_depth_blend_state_t *depth_blend, unsigned scale,
		rdp_triangle_setup_t *setup, rdp_attribute_setup_t *attr)
{
	rdp_upscaler_triangle_t tri;

	if (!u || !rdp_upscaler_is_active(u))
		return;

	tri.xh = setup->xh;
	tri.xm = setup->xm;
	tri.xl = setup->xl;
	tri.dxhdy = setup->dxhdy;
	tri.dxmdy = setup->dxmdy;
	tri.dxldy = setup->dxldy;
	tri.yh = setup->yh;
	tri.ym = setup->ym;
	tri.yl = setup->yl;
	tri.attr[0] = attr->r;
	tri.attr[1] = attr->g;
	tri.attr[2] = attr->b;
	tri.attr[3] = attr->a;
	tri.attr[4] = attr->s;
	tri.attr[5] = attr->t;
	tri.attr[6] = attr->w;
	tri.attr[7] = attr->z;
	tri.attr_de[0] = attr->drde;
	tri.attr_de[1] = attr->dgde;
	tri.attr_de[2] = attr->dbde;
	tri.attr_de[3] = attr->dade;
	tri.attr_de[4] = attr->dsde;
	tri.attr_de[5] = attr->dtde;
	tri.attr_de[6] = attr->dwde;
	tri.attr_de[7] = attr->dzde;
	tri.attr_dx[0] = attr->drdx;
	tri.attr_dx[1] = attr->dgdx;
	tri.attr_dx[2] = attr->dbdx;
	tri.attr_dx[3] = attr->dadx;
	tri.attr_dx[4] = attr->dsdx;
	tri.attr_dx[5] = attr->dtdx;
	tri.attr_dx[6] = attr->dwdx;
	tri.attr_dx[7] = attr->dzdx;

	rdp_upscaler_triangle(u, &tri, rdp_upscaler_view_signature(view, op, setup->tile),
			rdp_upscaler_view_flags(op, static_state, depth_blend, view->prim_depth), view->prim_z, scale);

	/* (the jitter moves the edges and what is at them) */
	setup->xh = tri.xh;
	setup->xm = tri.xm;
	setup->xl = tri.xl;
	attr->r = tri.attr[0];
	attr->g = tri.attr[1];
	attr->b = tri.attr[2];
	attr->a = tri.attr[3];
	attr->s = tri.attr[4];
	attr->t = tri.attr[5];
	attr->w = tri.attr[6];
	attr->z = tri.attr[7];
}

granite_image_t *rdp_upscaler_view_scanout(rdp_upscaler_t *u, rdp_vi_t *vi, granite_device_t *device,
		unsigned scale, unsigned jitter_phase, bool reset, rdp_upscaler_targets_t *targets,
		enum rdp_upscaler_frame *frame)
{
	rdp_upscaler_framebuffer_t fb;
	rdp_upscaler_geometry_t geometry;
	granite_command_buffer_t *cb;
	granite_image_t *image;
	bool repeat = false;

	rdp_upscaler_configure(u, true, jitter_phase, reset);

	image = rdp_vi_upscaler_scanout(vi, scale, &fb, &repeat);
	if (!image)
	{
		*frame = RDP_UPSCALER_FRAME_NONE;
		return NULL;
	}
	if (repeat)
	{
		*frame = RDP_UPSCALER_FRAME_REPEAT;
		return image;
	}

	*frame = RDP_UPSCALER_FRAME_NEW;
	rdp_upscaler_resolve(u, &fb, scale, &geometry);
	cb = granite_device_request_command_buffer(device, GRANITE_QUEUE_GRAPHICS);
	/* (out of memory: a frame without targets) */
	if (cb)
	{
		rdp_upscaler_motion_pass(device, cb, &geometry, fb.width * scale, fb.height * scale, targets);
		granite_device_submit(device, cb, NULL, 0, NULL);
	}
	return image;
}
