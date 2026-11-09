#include <assert.h>
#include <retro_inline.h>
#include "DepthBufferRender/ClipPolygon.h"
#include "DepthBufferRender/DepthBufferRender.h"
#include "gSP.h"
#include "SoftwareRender.h"

static INLINE
void clipTest(vertexclip * _vtx)
{
	_vtx->visible = 0;
	if (_vtx->x > gSP.viewport.width)
		_vtx->visible |= RIGHT;
	if (_vtx->x < 0)
		_vtx->visible |= LEFT;
	if (_vtx->y > gSP.viewport.height)
		_vtx->visible |= TOP;
	if (_vtx->y < 0)
		_vtx->visible |= BOT;
}

static
bool calcScreenCoordinates(const SPVertex * const * _vsrc, vertexclip * _vclip, u32 _numVertex, bool * _clockwise)
{
	u32 i;
	float x1;
	float y1;
	float x2;
	float y2;
	u32 cullMode;
	for (i = 0; i < _numVertex; ++i) {
		const SPVertex * v = _vsrc[i];

		if ((v->modify & MODIFY_XY) == 0) {
			_vclip[i].x = gSP.viewport.vtrans[0] + (v->x / v->w) * gSP.viewport.vscale[0];
			_vclip[i].y = gSP.viewport.vtrans[1] + (v->y / v->w) * -gSP.viewport.vscale[1];
		} else {
			_vclip[i].x = v->x;
			_vclip[i].y = v->y;
		}

		if ((v->modify & MODIFY_Z) == 0) {
			_vclip[i].z = (gSP.viewport.vtrans[2] + (v->z / v->w) * gSP.viewport.vscale[2]) * 32767.0f;
		} else {
			_vclip[i].z = v->z * 32767.0f;
		}

		clipTest(&_vclip[i]);
	}

	if (_numVertex > 3) /* Don't cull w-clipped vertices */
		return true;

	/* Check culling */
	x1 = _vclip[0].x - _vclip[1].x;
	y1 = _vclip[0].y - _vclip[1].y;
	x2 = _vclip[2].x - _vclip[1].x;
	y2 = _vclip[2].y - _vclip[1].y;

	*_clockwise = (x1*y2 - y1*x2) >= 0.0f;

	cullMode = (gSP.geometryMode & G_CULL_BOTH);

	if (cullMode == G_CULL_FRONT) {
		if (*_clockwise) /*clockwise, negative */
			return false;
	} else if (cullMode == G_CULL_BACK) {
		if (!*_clockwise) /*counter-clockwise, positive */
			return false;
	}

	return true;
}

static INLINE
int floatToFixed16(double _v)
{
	return (int)(_v * 65536.0);
}

static
int calcDzDx(vertexclip * _v)
{
	double X0 = _v[0].x;
	double Y0 = _v[0].y;
	double X1 = _v[1].x;
	double Y1 = _v[1].y;
	double X2 = _v[2].x;
	double Y2 = _v[2].y;
	double diffy_02 = Y0 - Y2;
	double diffy_12 = Y1 - Y2;
	double diffx_02 = X0 - X2;
	double diffx_12 = X1 - X2;

	double denom = (diffx_02 * diffy_12 - diffx_12 * diffy_02);
	if(denom*denom > 0.0) {
		double diffz_02 = _v[0].z - _v[2].z;
		double diffz_12 = _v[1].z - _v[2].z;
		double fdzdx = (diffz_02 * diffy_12 - diffz_12 * diffy_02) / denom;
		return floatToFixed16(fdzdx);
	}
	return 0;
}

static
int calcDzDx2(const SPVertex ** _vsrc)
{
	double X1;
	double Y1;
	double Z1;
	double X2;
	double Y2;
	double Z2;
	double diffy_02;
	double diffy_12;
	double diffx_02;
	double diffx_12;
	double denom;
	const SPVertex * v = _vsrc[0];
	double X0 = gSP.viewport.vtrans[0] + (v->x / v->w) * gSP.viewport.vscale[0];
	double Y0 = gSP.viewport.vtrans[1] + (v->y / v->w) * -gSP.viewport.vscale[1];
	double Z0 = (gSP.viewport.vtrans[2] + (v->z / v->w) * gSP.viewport.vscale[2]) * 32767.0f;
	v = _vsrc[1];
	X1 = gSP.viewport.vtrans[0] + (v->x / v->w) * gSP.viewport.vscale[0];
	Y1 = gSP.viewport.vtrans[1] + (v->y / v->w) * -gSP.viewport.vscale[1];
	Z1 = (gSP.viewport.vtrans[2] + (v->z / v->w) * gSP.viewport.vscale[2]) * 32767.0f;
	v = _vsrc[2];
	X2 = gSP.viewport.vtrans[0] + (v->x / v->w) * gSP.viewport.vscale[0];
	Y2 = gSP.viewport.vtrans[1] + (v->y / v->w) * -gSP.viewport.vscale[1];
	Z2 = (gSP.viewport.vtrans[2] + (v->z / v->w) * gSP.viewport.vscale[2]) * 32767.0f;
	diffy_02 = Y0 - Y2;
	diffy_12 = Y1 - Y2;
	diffx_02 = X0 - X2;
	diffx_12 = X1 - X2;

	denom = (diffx_02 * diffy_12 - diffx_12 * diffy_02);
	if (denom*denom > 0.0) {
		double diffz_02 = Z0 - Z2;
		double diffz_12 = Z1 - Z2;
		double fdzdx = (diffz_02 * diffy_12 - diffz_12 * diffy_02) / denom;
		return floatToFixed16(fdzdx);
	}
	return 0;
}

static INLINE
void copyVertex(SPVertex * _dst, const SPVertex * _src)
{
	_dst->x = _src->x;
	_dst->y = _src->y;
	_dst->z = _src->z;
	_dst->w = _src->w;
	_dst->modify = _src->modify;
}

/* A triangle wholly in front of the near plane is lent as it is; only one that
 * crosses it is copied, to make room for the new vertices. */
static
u32 clipW(const SPVertex ** _vsrc, SPVertex * _vdst, const SPVertex ** _vout)
{
	u32 dsti = 0;
	u32 i;
	int n;

	if (_vsrc[0]->w >= 0.01f && _vsrc[1]->w >= 0.01f && _vsrc[2]->w >= 0.01f) {
		_vout[0] = _vsrc[0];
		_vout[1] = _vsrc[1];
		_vout[2] = _vsrc[2];
		return 3;
	}

	for (n = 0; n < 3; ++n) {
		const SPVertex * src1 = _vsrc[n];               /* current vertex */
		const SPVertex * src2 = _vsrc[n + 1];           /* next vertex */
		f32 a;
		f32 ima;
		if (src1->w >= 0.01f) {
			copyVertex(&_vdst[dsti++], src1);          /* add visible vertex to list */
			if (src2->w >= 0.01f)
				continue;
		} else if (src2->w < 0.01f)
			continue;
		a = (-src1->w) / (src2->w - src1->w);
		ima = 1.0f - a;
		/* create new vertex */
		_vdst[dsti].x = src1->x*ima + src2->x*a;
		_vdst[dsti].y = src1->y*ima + src2->y*a;
		_vdst[dsti].z = src1->z*ima + src2->z*a;
		_vdst[dsti].w = 0.01f;
		_vdst[dsti].modify = 0;
		dsti++;
	}

	for (i = 0; i < dsti; ++i)
		_vout[i] = &_vdst[i];

	return dsti;
}

static INLINE
void setDrawVertex(vertexi * _dst, const vertexclip * _src)
{
	_dst->x = floatToFixed16(_src->x);
	_dst->y = floatToFixed16(_src->y);
	_dst->z = floatToFixed16(_src->z);
}

f32 gli_renderTriangles(const SPVertex * _pVertices, const u16 * _pElements, u32 _numElements)
{
	f32 maxY;
	u32 i;
	vertexclip vclip[16];
	vertexi vdraw[12];
	const SPVertex * vsrc[4];
	const SPVertex * vlent[6];
	SPVertex vdata[6];
	const u16 * zLUT = NULL;
	u32 depthWidth = 0;
	/* neither can change while this draw runs: ask once, not per triangle */
	const bool rasterize = gDP.otherMode.depthUpdate != 0 &&
		gli_depthbuffer_software_target(&zLUT, &depthWidth);
	maxY = 0.0f;

	for (i = 0; i < _numElements; i += 3) {
		u32 orbits = 0;
		u32 j;
		u32 numVertex;
		bool clockwise = true;
		int dzdx;

		if (_pElements != NULL) {
			for (j = 0; j < 3; ++j) {
				vsrc[j] = &_pVertices[_pElements[i + j]];
				orbits |= vsrc[j]->clip;
			}
		} else {
			for (j = 0; j < 3; ++j) {
				vsrc[j] = &_pVertices[i + j];
				orbits |= vsrc[j]->clip;
			}
		}
		vsrc[3] = vsrc[0];

		numVertex = clipW(vsrc, vdata, vlent);

		if (!calcScreenCoordinates(vlent, vclip, numVertex, &clockwise))
			continue;

		dzdx = ((orbits & CLIP_W) == 0) ? calcDzDx(vclip) : calcDzDx2(vsrc);

		/* A cleared clip flag can leave a near-plane split with no orbits. */
		if (orbits == 0 && numVertex == 3) {
			u32 k;
			for (k = 0; k < 3; ++k) {
				const u32 idx = clockwise ? k : 3 - k - 1;
				/* maxY first: a NaN must not replace it */
				if (maxY < vclip[idx].y)
					maxY = vclip[idx].y;
				if (rasterize)
					setDrawVertex(&vdraw[k], &vclip[idx]);
			}
		} else {
			vertexclip ** vtx;
			u32 k;
			numVertex = gli_ClipPolygon(&vtx, vclip, numVertex);
			if (numVertex < 3)
				continue;

			for (k = 0; k < numVertex; ++k) {
				const u32 idx = clockwise ? k : numVertex - k - 1;
				if (maxY < vtx[idx]->y)
					maxY = vtx[idx]->y;
				if (rasterize)
					setDrawVertex(&vdraw[k], vtx[idx]);
			}
		}

		if (rasterize)
			gli_Rasterize(vdraw, numVertex, dzdx, zLUT, depthWidth);
	}
	return maxY;
}
