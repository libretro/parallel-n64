#include <stdlib.h>
#include "GBI.h"
#include "N64.h"
#include "RDP.h"
#include "RSP.h"
#include "VI.h"
#include "gDP.h"
#include "gSP.h"
#include "gli_framebuffer.h"
#include "TexrectDrawer.h"

#define TEXRECT_WIDTH 640.0f
#define TEXRECT_HEIGHT 580.0f
#define MAX_RECT_COORDS 8192

typedef struct RectCoords { s32 x, y; } RectCoords;
typedef struct iRect { s32 ulx, uly, lrx, lry; } iRect;

static u32 l_numRects;
static u64 l_otherMode;
static u64 l_mux;
static f32 l_ulx, l_lrx, l_uly, l_lry, l_Z;
static s32 l_ulx_i, l_uly_i, l_lry_i;
static f32 l_max_lrx, l_max_lry;
static const f32 l_stepX = 2.0f / TEXRECT_WIDTH;
static const f32 l_stepY = 2.0f / TEXRECT_HEIGHT;
static gDPScissor l_scissor;
static struct FrameBuffer * l_pBuffer;
static RectCoords l_rectCoords[MAX_RECT_COORDS];
static u32 l_numRectCoords;
static iRect l_curRect;

static f32 trd_maxf(f32 _a, f32 _b)
{
	return _a < _b ? _b : _a;
}

static f32 trd_minf(f32 _a, f32 _b)
{
	return _b < _a ? _b : _a;
}

void gli_texrect_init(void)
{
	gli_texrect_gpu_init();
}

void gli_texrect_destroy(void)
{
	gli_texrect_gpu_destroy();
}

static void _setDrawBuffer(void)
{
	if (l_pBuffer != NULL)
		gli_fb_bind_draw(l_pBuffer);
	else
		gli_fb_set_current_draw_buffer();
}

static iRect _getiRect(u32 w0, u32 w1)
{
	iRect rect;
	rect.ulx = _SHIFTR(w1, 12, 12);
	rect.uly = _SHIFTR(w1, 0, 12);
	rect.lrx = _SHIFTR(w0, 12, 12);
	rect.lry = _SHIFTR(w0, 0, 12);
	return rect;
}

#define COMPARE_COORDS(a, b) (abs((a) - (b)) <= 4)

static bool _sideBySide(u32 pc)
{
	const u32 w0 = *(u32*)&RDRAM[pc];
	const u32 w1 = *(u32*)&RDRAM[pc + 4];
	const iRect nextRect = _getiRect(w0, w1);

	if (COMPARE_COORDS(l_curRect.ulx, nextRect.ulx)) {
		bool sbs = COMPARE_COORDS(l_curRect.lry, nextRect.uly);
		sbs |= COMPARE_COORDS(l_curRect.uly, nextRect.lry);
		return sbs;
	}
	if (COMPARE_COORDS(l_curRect.uly, nextRect.uly)) {
		bool sbs = COMPARE_COORDS(l_curRect.lrx, nextRect.ulx);
		sbs |= COMPARE_COORDS(l_curRect.ulx, nextRect.lrx);
		return sbs;
	}
	return false;
}

static bool _lookAhead(bool _checkCoordinates)
{
	u32 pc;

	if (RSP.LLE)
		return true;
	switch (gli_GBI->type) {
	case Turbo3D:
	case T3DUX:
	case F5Rogue:
	case F5Indi_Naboo:
		return true;
	}

	pc = RSP.PC[RSP.PCi];
	for (;;) {
		if (pc > RDRAMSize - 8)
			return false;
		switch (_SHIFTR(*(u32*)&RDRAM[pc], 24, 8)) {
		case G_RDPLOADSYNC:
		case G_RDPPIPESYNC:
		case G_RDPTILESYNC:
		case G_LOADTLUT:
		case G_SETTILESIZE:
		case G_LOADBLOCK:
		case G_LOADTILE:
		case G_SETTILE:
		case G_SETTIMG:
			break;
		case G_TEXRECT:
		case G_TEXRECTFLIP:
			if (_checkCoordinates)
				return _sideBySide(pc);
			return true;
		default:
			return false;
		}
		pc += 8;
	}
}

static void _startBatch(const RectVertex * _pRect, f32 _z)
{
	l_numRects = 1;
	l_pBuffer = gli_fb_current();
	l_otherMode = gDP.otherMode._u64;
	l_mux = gDP.combine.mux;
	l_Z = _z;
	l_scissor = gDP.scissor;

	l_ulx = _pRect[0].x;
	l_uly = _pRect[0].y;
	l_lrx = l_max_lrx = _pRect[3].x;
	l_lry = l_max_lry = _pRect[3].y;
}

static void _startDrawing(void)
{
	const u32 bufferWidth = l_pBuffer == NULL ? VI.width : gli_fb_width(l_pBuffer);
	gli_texrect_gpu_begin(bufferWidth, gli_VI_GetMaxBufferHeight((u16)bufferWidth),
		(s32)gDP.scissor.ulx, (s32)gDP.scissor.uly,
		(s32)(gDP.scissor.lrx - gDP.scissor.ulx), (s32)(gDP.scissor.lry - gDP.scissor.uly));
}

bool gli_texrect_add_rect(void)
{
	RectVertex * pRect = gli_texrect_gpu_rect();
	bool bDownUp = false;

	/* flush rather than grow */
	if (l_numRectCoords + 2 > MAX_RECT_COORDS)
		gli_texrect_draw();

	l_curRect = _getiRect(RDP.w0, RDP.w1);

	if (l_numRects != 0) {
		bool bContinue = false;
		if (l_otherMode == gDP.otherMode._u64 && l_mux == gDP.combine.mux) {
			if (COMPARE_COORDS(l_ulx_i, l_curRect.ulx)) {
				bContinue = COMPARE_COORDS(l_lry_i, l_curRect.uly);
				bDownUp = COMPARE_COORDS(l_uly_i, l_curRect.lry);
				bContinue |= bDownUp;
			}
			else {
				u32 i = l_numRectCoords;
				while (i > 0) {
					const RectCoords * coords = &l_rectCoords[--i];
					if (COMPARE_COORDS(coords->x, l_curRect.ulx) && COMPARE_COORDS(coords->y, l_curRect.uly)) {
						bContinue = true;
						break;
					}
				}
			}
		}
		if (!bContinue) {
			gli_texrect_draw();
			gli_texrect_gpu_restart();
		}
	}

	if (l_numRects == 0) {
		if (!_lookAhead(true))
			return false;

		_startBatch(pRect, (gDP.otherMode.depthSource == G_ZS_PRIM) ? gDP.primDepth.z : 0.0f);
		l_ulx_i = l_curRect.ulx;
		l_uly_i = l_curRect.uly;
		l_lry_i = l_curRect.lry;
		_startDrawing();
	} else {
		++l_numRects;
	}

	if (bDownUp) {
		l_ulx = pRect[0].x;
		l_uly = pRect[0].y;
		l_ulx_i = l_curRect.ulx;
		l_uly_i = l_curRect.uly;
	} else {
		l_lrx = pRect[3].x;
		l_lry = pRect[3].y;
		l_max_lrx = trd_maxf(l_max_lrx, l_lrx);
		l_max_lry = trd_maxf(l_max_lry, l_lry);
		l_lry_i = l_curRect.lry;
	}

	l_rectCoords[l_numRectCoords].x = l_curRect.lrx;
	l_rectCoords[l_numRectCoords].y = l_curRect.uly;
	++l_numRectCoords;
	l_rectCoords[l_numRectCoords].x = l_curRect.lrx;
	l_rectCoords[l_numRectCoords].y = l_curRect.lry;
	++l_numRectCoords;

	gli_texrect_gpu_draw_rect(pRect);

	if (l_numRects > 1 && !_lookAhead(false))
		gli_texrect_draw();

	return true;
}

void gli_texrect_add_background_rect(void)
{
	RectVertex * pRect = gli_texrect_gpu_rect();

	if (l_numRects == 0) {
		_startBatch(pRect, (gDP.otherMode.depthSource == G_ZS_PRIM) ? gDP.primDepth.z : gSP.viewport.nearz);
		_startDrawing();
	} else {
		++l_numRects;
	}

	l_lrx = pRect[3].x;
	l_lry = pRect[3].y;
	l_max_lrx = trd_maxf(l_max_lrx, l_lrx);
	l_max_lry = trd_maxf(l_max_lry, l_lry);

	gli_texrect_gpu_draw_rect(pRect);
}

bool gli_texrect_draw(void)
{
	RectVertex rect[4];
	u64 savedOtherMode;
	gDPScissor savedScissor;
	int enableAlphaTest = 0;
	f32 scaleX, scaleY;
	f32 s0, t0, s1, t1;
	f32 ulx, lrx, uly, lry;
	const f32 W = 1.0f;
	u32 bufferWidth;

	if (l_numRects == 0)
		return false;

	savedOtherMode = gDP.otherMode._u64;
	savedScissor = gDP.scissor;
	gDP.otherMode._u64 = l_otherMode;
	gDP.scissor = l_scissor;

	gli_texrect_gpu_set_blend();
	gDP.changed |= CHANGED_RENDERMODE;  /* Force update of depth compare parameters */
	gli_texrect_gpu_update_depth_compare();

	switch (gDP.otherMode.cycleType) {
	case G_CYC_COPY:
		if (gDP.otherMode.alphaCompare & G_AC_THRESHOLD)
			enableAlphaTest = 1;
		break;
	case G_CYC_1CYCLE:
	case G_CYC_2CYCLE:
		if (((gDP.otherMode.alphaCompare & G_AC_THRESHOLD) != 0) && (gDP.otherMode.alphaCvgSel == 0) && (gDP.otherMode.forceBlender == 0 || gDP.blendColor.a > 0))
			enableAlphaTest = 1;
		else if ((gDP.otherMode.alphaCompare == G_AC_DITHER) && (gDP.otherMode.alphaCvgSel == 0))
			enableAlphaTest = 1;
		else if (gDP.otherMode.cvgXAlpha != 0)
			enableAlphaTest = 1;
		break;
	}

	l_lrx = l_max_lrx;
	l_lry = l_max_lry;

	gli_texrect_gpu_scales(l_pBuffer, &scaleX, &scaleY);
	scaleX *= 2.0f;
	scaleY *= 2.0f;

	s0 = (l_ulx + 1.0f) / scaleX / TEXRECT_WIDTH + 0.5f / TEXRECT_WIDTH;
	t0 = (l_lry + 1.0f) / scaleY / TEXRECT_HEIGHT;
	s1 = (l_lrx + 1.0f) / scaleX / TEXRECT_WIDTH;
	t1 = (l_uly + 1.0f) / scaleY / TEXRECT_HEIGHT;

	gli_texrect_gpu_begin_composite(l_pBuffer, enableAlphaTest);

	rect[0].x = l_ulx;
	rect[0].y = l_lry;
	rect[0].z = l_Z;
	rect[0].w = W;
	rect[0].s0 = s0;
	rect[0].t0 = t0;
	rect[1].x = l_lrx;
	rect[1].y = l_lry;
	rect[1].z = l_Z;
	rect[1].w = W;
	rect[1].s0 = s1;
	rect[1].t0 = t0;
	rect[2].x = l_ulx;
	rect[2].y = l_uly;
	rect[2].z = l_Z;
	rect[2].w = W;
	rect[2].s0 = s0;
	rect[2].t0 = t1;
	rect[3].x = l_lrx;
	rect[3].y = l_uly;
	rect[3].z = l_Z;
	rect[3].w = W;
	rect[3].s0 = s1;
	rect[3].t0 = t1;

	gli_texrect_gpu_update_scissor(l_pBuffer);
	_setDrawBuffer();
	gli_texrect_gpu_composite(rect);

	ulx = trd_maxf(-1.0f, l_ulx - l_stepX);
	lrx = trd_minf( 1.0f, l_lrx + l_stepX);
	rect[0].x = ulx;
	rect[1].x = lrx;
	rect[2].x = ulx;
	rect[3].x = lrx;

	uly = trd_maxf(-1.0f, l_uly - l_stepY);
	lry = trd_minf( 1.0f, l_lry + l_stepY);
	rect[0].y = uly;
	rect[1].y = uly;
	rect[2].y = lry;
	rect[3].y = lry;

	bufferWidth = l_pBuffer == NULL ? VI.width : gli_fb_width(l_pBuffer);
	gli_texrect_gpu_clear(rect, bufferWidth, gli_VI_GetMaxBufferHeight((u16)bufferWidth));

	l_pBuffer = gli_fb_current();
	_setDrawBuffer();

	l_numRects = 0;
	l_numRectCoords = 0;
	gDP.changed |= CHANGED_COMBINE | CHANGED_SCISSOR | CHANGED_RENDERMODE;
	gSP.changed |= CHANGED_VIEWPORT | CHANGED_TEXTURE | CHANGED_GEOMETRYMODE;

	gDP.otherMode._u64 = savedOtherMode;
	gDP.scissor = savedScissor;
	return true;
}

bool gli_texrect_is_empty(void)
{
	return l_numRects == 0;
}

bool gli_texrect_can_continue(void)
{
	return (l_numRects != 0 &&
			l_otherMode == gDP.otherMode._u64 &&
			l_mux == gDP.combine.mux &&
			l_pBuffer == gli_fb_current());
}
