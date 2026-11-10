#include <math.h>
#include <boolean.h>

#include "Types.h"
#include "GBI.h"
#include "RSP.h"
#include "gDP.h"
#include "gSP.h"
#include "gli_drawer.h"

/* The drawer's triangle list, bound once at its construction */
static struct
{
	SPVertex *vertices;
	u16 *elements;
	u32 *num;
	s32 *maxElement;
	u32 *modify;
	const bool *clipControl;
} g_tris;

void gli_drawer_bind_triangles(SPVertex *_vertices, u16 *_elements, u32 *_num,
		s32 *_maxElement, u32 *_modify, const bool *_clipControl)
{
	g_tris.vertices = _vertices;
	g_tris.elements = _elements;
	g_tris.num = _num;
	g_tris.maxElement = _maxElement;
	g_tris.modify = _modify;
	g_tris.clipControl = _clipControl;
}

void gli_drawer_add_triangle(s32 _v0, s32 _v1, s32 _v2)
{
	SPVertex *vertices = g_tris.vertices;
	u16 *elements = g_tris.elements;
	const u32 firstIndex = *g_tris.num;
	u32 num = firstIndex;
	u32 i;

	elements[num++] = (u16)_v0;
	elements[num++] = (u16)_v1;
	elements[num++] = (u16)_v2;
	*g_tris.num = num;
	if (*g_tris.maxElement < _v0)
		*g_tris.maxElement = _v0;
	if (*g_tris.maxElement < _v1)
		*g_tris.maxElement = _v1;
	if (*g_tris.maxElement < _v2)
		*g_tris.maxElement = _v2;

	*g_tris.modify |= vertices[_v0].modify | vertices[_v1].modify | vertices[_v2].modify;

	if ((gSP.geometryMode & G_LIGHTING) == 0) {
		if ((gSP.geometryMode & G_SHADE) == 0) {
			/* Prim shading */
			for (i = firstIndex; i < num; ++i) {
				SPVertex *vtx = &vertices[elements[i]];
				vtx->flat_r = gDP.primColor.r;
				vtx->flat_g = gDP.primColor.g;
				vtx->flat_b = gDP.primColor.b;
				vtx->flat_a = gDP.primColor.a;
			}
		} else if ((gSP.geometryMode & G_SHADING_SMOOTH) == 0) {
			/* Flat shading */
			const SPVertex *vtx0 = &vertices[elements[firstIndex + (((RSP.w1 >> 24) & 3) % 3)]];
			const f32 r = vtx0->r, g = vtx0->g, b = vtx0->b, a = vtx0->a;
			for (i = firstIndex; i < num; ++i) {
				SPVertex *vtx = &vertices[elements[i]];
				vtx->r = vtx->flat_r = r;
				vtx->g = vtx->flat_g = g;
				vtx->b = vtx->flat_b = b;
				vtx->a = vtx->flat_a = a;
			}
		}
	}

	if (gDP.otherMode.depthSource == G_ZS_PRIM) {
		for (i = firstIndex; i < num; ++i) {
			SPVertex *vtx = &vertices[elements[i]];
			vtx->z = gDP.primDepth.z * vtx->w;
		}
	}

	if (!*g_tris.clipControl) {
		if (gli_GBI->NoN && gDP.otherMode.depthCompare == 0 && gDP.otherMode.depthUpdate == 0) {
			for (i = firstIndex; i < num; ++i)
				vertices[elements[i]].z = 0.0f;
		}
	}
}

bool gli_drawer_is_rejected(s32 _v0, s32 _v1, s32 _v2)
{
	static gDPScissor rejectBox;
	s32 verts[3];
	f32 ySign;
	u32 i;

	if (!gli_GBI->Rej || gSP.clipRatio < 2)
		return false;

	if ((gDP.changed & CHANGED_REJECT_BOX) != 0) {
		const f32 scissorWidth2 = (gDP.scissor.lrx - gDP.scissor.ulx) * (gSP.clipRatio - 1) * 0.5f;
		const f32 scissorHeight2 = (gDP.scissor.lry - gDP.scissor.uly) * (gSP.clipRatio - 1) * 0.5f;
		rejectBox.ulx = gDP.scissor.ulx - scissorWidth2;
		rejectBox.lrx = gDP.scissor.lrx + scissorWidth2;
		rejectBox.uly = gDP.scissor.uly - scissorHeight2;
		rejectBox.lry = gDP.scissor.lry + scissorHeight2;
		gDP.changed ^= CHANGED_REJECT_BOX;
	}
	verts[0] = _v0;
	verts[1] = _v1;
	verts[2] = _v2;
	ySign = gli_GBI->negativeY ? -1.0f : 1.0f;
	for (i = 0; i < 3; ++i) {
		const SPVertex *v = &g_tris.vertices[verts[i]];
		const f32 sx = gSP.viewport.vtrans[0] + (v->x / v->w) * gSP.viewport.vscale[0];
		f32 sy;
		if (sx < rejectBox.ulx)
			return true;
		if (sx > rejectBox.lrx)
			return true;
		sy = gSP.viewport.vtrans[1] + (v->y / v->w) * gSP.viewport.vscale[1] * ySign;
		if (sy < rejectBox.uly)
			return true;
		if (sy > rejectBox.lry)
			return true;
	}
	return false;
}

/* Screen space position of vertex @_v */
static void line_end(SPVertex *_out, const SPVertex *_v, f32 _ySign)
{
	*_out = *_v;
	_out->x = _out->x / _out->w * gSP.viewport.vscale[0] + gSP.viewport.vtrans[0];
	_out->y = _ySign * _out->y / _out->w * gSP.viewport.vscale[1] + gSP.viewport.vtrans[1];
	_out->z = _out->z / _out->w * gSP.viewport.vscale[2] + gSP.viewport.vtrans[2];
}

void gli_drawer_thick_line(SPVertex *_out, u32 _v0, u32 _v1, f32 _halfWidth, u32 _flag)
{
	SPVertex *vertices = g_tris.vertices;
	const f32 ySign = gli_GBI->negativeY ? -1.0f : 1.0f;

	if ((gSP.geometryMode & G_LIGHTING) == 0) {
		SPVertex *vtx1 = &vertices[_v0];
		SPVertex *vtx2 = &vertices[_v1];
		if ((gSP.geometryMode & G_SHADE) == 0) {
			vtx1->flat_r = vtx2->flat_r = gDP.primColor.r;
			vtx1->flat_g = vtx2->flat_g = gDP.primColor.g;
			vtx1->flat_b = vtx2->flat_b = gDP.primColor.b;
			vtx1->flat_a = vtx2->flat_a = gDP.primColor.a;
		} else if ((gSP.geometryMode & G_SHADING_SMOOTH) == 0) {
			/* flat shading takes the color of vertex @_flag */
			const SPVertex *vtx0 = &vertices[_flag];
			const f32 r = vtx0->r, g = vtx0->g, b = vtx0->b, a = vtx0->a;
			vtx1->r = vtx1->flat_r = r;
			vtx1->g = vtx1->flat_g = g;
			vtx1->b = vtx1->flat_b = b;
			vtx1->a = vtx1->flat_a = a;
			vtx2->r = vtx2->flat_r = r;
			vtx2->g = vtx2->flat_g = g;
			vtx2->b = vtx2->flat_b = b;
			vtx2->a = vtx2->flat_a = a;
		}
	}

	line_end(&_out[0], &vertices[_v0], ySign);
	_out[1] = _out[0];
	line_end(&_out[2], &vertices[_v1], ySign);
	_out[3] = _out[2];

	if (fabs(_out[0].y - _out[2].y) < 0.0001) {
		const f32 y = _out[0].y;
		_out[0].y = _out[2].y = y - _halfWidth;
		_out[1].y = _out[3].y = y + _halfWidth;
	} else if (fabs(_out[0].x - _out[2].x) < 0.0001) {
		const f32 x = _out[0].x;
		_out[0].x = _out[2].x = x - _halfWidth;
		_out[1].x = _out[3].x = x + _halfWidth;
	} else {
		const f32 x0 = _out[0].x, y0 = _out[0].y;
		const f32 x1 = _out[2].x, y1 = _out[2].y;
		const f32 dx = x1 - x0;
		const f32 dy = y1 - y0;
		const f32 len = sqrtf(dx * dx + dy * dy);
		const f32 wx = dy * _halfWidth / len;
		const f32 wy = dx * _halfWidth / len;
		_out[0].x = x0 + wx; _out[0].y = y0 - wy;
		_out[1].x = x0 - wx; _out[1].y = y0 + wy;
		_out[2].x = x1 + wx; _out[2].y = y1 - wy;
		_out[3].x = x1 - wx; _out[3].y = y1 + wy;
	}
}
