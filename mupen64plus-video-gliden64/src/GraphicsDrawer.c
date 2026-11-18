/* The drawer: triangles, lines, rects and textured rects drawn through the
 * graphics context, the states they need set first, and copies of
 * textures between frame buffers (GraphicsDrawer.h, gli_drawer.h). C89. */
#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "GraphicsDrawer.h"
#include "gli_drawer.h"
#include "gli_framebuffer.h"
#include "gli_fb_rdram.h"
#include "gli_blend.h"
#include "gli_combiner.h"
#include "gli_textures.h"
#include "gli_tex.h"
#include "gli_tri_clip.h"
#include "DisplayWindow.h"
#include "SoftwareRender.h"
#include "TextureFilterHandler.h"
#include "PostProcessor.h"
#include "NoiseTexture.h"
#include "ZlutTexture.h"
#include "PaletteTexture.h"
#include "DepthBuffer.h"
#include "FrameBufferInfo.h"
#include "TexrectDrawer.h"
#include "Config.h"
#include "RSP.h"
#include "RDP.h"
#include "VI.h"
#include "gDP.h"
#include "gSP.h"
#include "Graphics/gli_context.h"

/* what the drawer last drew */
enum
{
	DRAWING_NONE,
	DRAWING_LINE,
	DRAWING_TRIANGLE,
	DRAWING_RECT,
	DRAWING_TEXRECT
};

/* the triangles the microcodes add (TriangleList.c) */
gli_triangles gli_tris;

/* the most DMA vertices a microcode asks for at once is 765, Turbo3D's 255
 * triangles */
#define DMA_VERTICES 4096

typedef bool (*texrect_special)(const gli_texrect_params *_params);

static struct
{
	u32 drawingState;
	f32 texrectRect[4]; /* the last texrect, ulx, uly, lrx, lry */
	f32 maxLineWidth;
	bool flatColors;
	bool bgMode;
	u32 dmaVerticesNum;
	RectVertex rect[4];
	/* Special processing of a textured rect: whether it need not be drawn */
	texrect_special texrectSpecial;
	/* triangles clipped as the RSP clips them */
	SPVertex clipped[ELEMBUFF_SIZE * 3];
	/* the microcodes fill these directly */
	SPVertex dmaVertices[DMA_VERTICES];
} drw;

u32 rectDepthBufferCopyFrame = 0xFFFFFFFF;

static int can_draw(void)
{
	return config.frameBufferEmulation.enable == 0 || gli_fb_current() != NULL;
}

static const gli_cprog *current_combiner(void)
{
	return gli_cmb.current;
}

static void set_polygon_mode(u32 _drawingState)
{
	gli_combiner_set_rect_mode(_drawingState == DRAWING_RECT || _drawingState == DRAWING_TEXRECT);
}

static s32 roundup(f32 _v, f32 _scale)
{
	return (s32)floorf(_v * _scale + 0.5f);
}

static void update_cull_face(void)
{
	static const u32 modes[] = { GLI_CULLMODE_FRONT, GLI_CULLMODE_FRONT, GLI_CULLMODE_BACK, GLI_CULLMODE_FRONT_AND_BACK };
	const int mode = gli_drawer_cull_mode();
	gli_ctx.ops->enable(gli_ctx.impl, GLI_EN_CULL_FACE, mode != GLI_CULL_NONE);
	if (mode != GLI_CULL_NONE)
		gli_ctx.ops->cullFace(gli_ctx.impl, modes[mode]);
}

static void update_depth_update(void)
{
	gli_ctx.ops->enableDepthWrite(gli_ctx.impl, gDP.otherMode.depthUpdate != 0);
}

/* the context's clamp modes: clipping, no near plane clipping, none */
enum { CLAMP_CLIPPING, CLAMP_NO_NEAR_CLIPPING, CLAMP_NO_CLIPPING };

static void update_depth_compare(void)
{
	static const u32 compares[] = { GLI_COMPARE_LESS, GLI_COMPARE_LEQUAL, GLI_COMPARE_ALWAYS };
	gli_depth_state d;
	gli_drawer_depth_state(&d);
	if (d.set & GLI_DS_COMPARE) {
		gli_ctx.ops->enable(gli_ctx.impl, GLI_EN_POLYGON_OFFSET_FILL, d.offset != 0);
		gli_ctx.ops->setDepthCompare(gli_ctx.impl, compares[d.compare]);
	}
	if (d.set & GLI_DS_WRITE)
		gli_ctx.ops->enableDepthWrite(gli_ctx.impl, d.write != 0);
	if (d.set & GLI_DS_TEST)
		gli_ctx.ops->enable(gli_ctx.impl, GLI_EN_DEPTH_TEST, d.test != 0);
	if (d.set & GLI_DS_CLAMP)
		gli_ctx.ops->setClampMode(gli_ctx.impl, d.clipping ? CLAMP_CLIPPING : CLAMP_NO_CLIPPING);
}

static void update_scissor(struct FrameBuffer *_pBuffer)
{
	f32 scaleX, scaleY;
	f32 offsetX = 0.0f, offsetY = 0.0f;
	s32 rect[4];
	if (_pBuffer == NULL) {
		scaleX = g_window.scaleX;
		scaleY = g_window.scaleY;
	} else {
		const gli_fb *fb = gli_fb_of(_pBuffer);
		scaleX = fb->m_scale;
		scaleY = fb->m_scale;
		offsetX = (f32)fb->m_originX;
		offsetY = (f32)fb->m_originY;
	}

	gli_drawer_scissor_rect(scaleX, scaleY, offsetX, offsetY,
		gli_drawer_need_adjust(g_window.adjustScreen), g_window.adjustScale,
		gli_gSPImmersiveActive(), rect);
	gli_ctx.ops->setScissor(gli_ctx.impl, rect[0], rect[1], rect[2], rect[3]);

	gDP.changed &= ~CHANGED_SCISSOR;
}

static void update_viewport(void)
{
	struct FrameBuffer *pCurrentBuffer = gli_fb_current();
	s32 rect[4];
	if (pCurrentBuffer == NULL) {
		gli_drawer_viewport_rect(0, g_window.scaleX, g_window.scaleY, 0, 0,
			gli_drawer_need_adjust(g_window.adjustScreen), g_window.adjustScale, rect);
	} else {
		const gli_fb *fb = gli_fb_of(pCurrentBuffer);
		gli_drawer_viewport_rect(1, fb->m_scale, fb->m_scale, fb->m_originX, fb->m_originY,
			gli_drawer_need_adjust(g_window.adjustScreen), g_window.adjustScale, rect);
	}
	gli_ctx.ops->setViewport(gli_ctx.impl, rect[0], rect[1], rect[2], rect[3]);
	gSP.changed &= ~CHANGED_VIEWPORT;
}

static void update_screen_coords_viewport(const struct FrameBuffer *_pBuffer)
{
	const struct FrameBuffer *pCurrentBuffer = _pBuffer != NULL ? _pBuffer : gli_fb_current();
	u32 bufferWidth, bufferHeight;
	f32 viewportScaleX, viewportScaleY;
	s32 X = 0, Y = 0;
	if (pCurrentBuffer == NULL) {
		bufferWidth = VI.width;
		bufferHeight = VI.height;
		viewportScaleX = g_window.scaleX;
		viewportScaleY = g_window.scaleY;
	} else {
		const gli_fb *fb = gli_fb_of((struct FrameBuffer *)pCurrentBuffer);
		bufferWidth = fb->m_width;
		bufferHeight = gli_VI_GetMaxBufferHeight((u16)bufferWidth);
		viewportScaleX = viewportScaleY = fb->m_scale;
		X = roundup((f32)fb->m_originX, viewportScaleX);
		Y = roundup((f32)fb->m_originY, viewportScaleY);
	}

	gli_ctx.ops->setViewport(gli_ctx.impl, X, Y, roundup((f32)bufferWidth, viewportScaleX),
		roundup((f32)bufferHeight, viewportScaleY));
	gSP.changed |= CHANGED_VIEWPORT;
}

static u32 blend_param(u8 _f)
{
	switch (_f) {
	case GLI_BF_ONE: return GLI_BLEND_ONE;
	case GLI_BF_SRC_ALPHA: return GLI_BLEND_SRC_ALPHA;
	case GLI_BF_ONE_MINUS_SRC_ALPHA: return GLI_BLEND_ONE_MINUS_SRC_ALPHA;
	case GLI_BF_DST_ALPHA: return GLI_BLEND_DST_ALPHA;
	case GLI_BF_CONSTANT_ALPHA: return GLI_BLEND_CONSTANT_ALPHA;
	case GLI_BF_ONE_MINUS_CONSTANT_ALPHA: return GLI_BLEND_ONE_MINUS_CONSTANT_ALPHA;
	}
	return GLI_BLEND_ZERO;
}

static void apply_blend_mode(bool _legacy)
{
	/* Blast Corps: the combiner is asked under the same guard as before */
	const bool blastCorpsTexture = (config.generalEmulation.hacks & hack_blastCorps) != 0 &&
		gDP.otherMode.cycleType < G_CYC_COPY && gSP.texture.on == 0 &&
		current_combiner()->ops->usesTexture(current_combiner());
	gli_blend_state b;
	gli_blend_mode(_legacy, blastCorpsTexture, &b);
	if (b.fogColor)
		gli_ctx.ops->setBlendColor(gli_ctx.impl, gDP.fogColor.r, gDP.fogColor.g, gDP.fogColor.b, gDP.fogColor.a);
	gli_ctx.ops->enable(gli_ctx.impl, GLI_EN_BLEND, b.enable != 0);
	if (b.enable)
		gli_ctx.ops->setBlending(gli_ctx.impl, blend_param(b.src), blend_param(b.dst));
}

static void update_textures(void)
{
	/* For some reason updating the texture cache on the first frame of
	 * LOZ:OOT causes a nullptr Pointer exception... */
	const gli_cprog *pCurrentCombiner = current_combiner();
	if (pCurrentCombiner != NULL) {
		u32 t;
		for (t = 0; t < 2; ++t) {
			if (pCurrentCombiner->ops->usesTile(pCurrentCombiner, t))
				gli_textures_update(t);
			else
				gli_textures_activate_dummy(t);
		}
	}
	gDP.changed &= ~(CHANGED_TILE | CHANGED_TMEM);
	gSP.changed &= ~(CHANGED_TEXTURE);
}

static void update_states(u32 _drawingState)
{
	set_polygon_mode(_drawingState);
	gli_combiner_update();

	if (gSP.changed & CHANGED_GEOMETRYMODE) {
		update_cull_face();
		gSP.changed &= ~CHANGED_GEOMETRYMODE;
	}

	update_depth_compare();

	if (gDP.changed & CHANGED_SCISSOR)
		update_scissor(gli_fb_current());

	if (gSP.changed & CHANGED_VIEWPORT)
		update_viewport();

	if ((gSP.changed & CHANGED_TEXTURE) ||
		(gDP.changed & (CHANGED_TILE | CHANGED_TMEM)) ||
		gli_cmb.changed ||
		_drawingState == DRAWING_TEXRECT) {
		update_textures();
	}

	if ((gDP.changed & (CHANGED_RENDERMODE | CHANGED_CYCLETYPE))) {
		apply_blend_mode(0);
		gDP.changed &= ~(CHANGED_RENDERMODE | CHANGED_CYCLETYPE);
	}

	gli_combiner_update_parameters();

	if (!config.generalEmulation.enableFragmentDepthWrite)
		return;

	if (gli_isCurrentColorImageDepthImage() &&
		config.generalEmulation.enableFragmentDepthWrite != 0 &&
		config.frameBufferEmulation.N64DepthCompare == 0) {
		/* Current render target is depth buffer. Shader will set
		 * gl_FragDepth to shader color, see ShaderCombiner ctor. Here we
		 * enable depth buffer write. */
		if (gDP.otherMode.cycleType <= G_CYC_2CYCLE && gDP.otherMode.depthCompare != 0) {
			/* Render to depth buffer with depth compare. Need to get copy
			 * of current depth buffer. */
			struct FrameBuffer *pCurBuf = gli_fb_current();
			if (pCurBuf != NULL && gli_fb_of(pCurBuf)->m_pDepthBuffer != NULL) {
				gli_fb *fb = gli_fb_of(pCurBuf);
				gli_tex *pDepthTexture = gli_depth_copy_texture(fb->m_pDepthBuffer, fb);
				gli_tex_parameters params = GLI_TEX_PARAMETERS_DEFAULT;
				if (pDepthTexture == NULL)
					return;
				params.handle = pDepthTexture->name;
				params.target = GLI_TT_TEXTURE_2D;
				params.textureUnitIndex = GLI_TU_DepthTex;
				params.maxMipmapLevel = 0;
				params.minFilter = GLI_TP_FILTER_NEAREST;
				params.magFilter = GLI_TP_FILTER_NEAREST;
				gli_ctx.ops->setTextureParameters(gli_ctx.impl, &params);
			}
		} else if (gli_fb_current() == NULL) {
			gli_ctx.ops->enable(gli_ctx.impl, GLI_EN_BLEND, 1);
			gli_ctx.ops->setBlending(gli_ctx.impl, GLI_BLEND_ZERO, GLI_BLEND_ONE);
		}
		gli_ctx.ops->enable(gli_ctx.impl, GLI_EN_DEPTH_TEST, 1);
		gli_ctx.ops->setDepthCompare(gli_ctx.impl, GLI_COMPARE_ALWAYS);
		gli_ctx.ops->enableDepthWrite(gli_ctx.impl, 1);
		gDP.changed |= CHANGED_RENDERMODE;
	}
}

static void prepare_draw_triangle(void)
{
	bool bFlatColors = 0;

	gli_texrect_draw();

	if ((gli_tris.modify & MODIFY_XY) != 0)
		gSP.changed &= ~CHANGED_VIEWPORT;

	if (gSP.changed || gDP.changed)
		update_states(DRAWING_TRIANGLE);

	drw.drawingState = DRAWING_TRIANGLE;

	if (!RSP.LLE && (gSP.geometryMode & G_LIGHTING) == 0) {
		bFlatColors = (gSP.geometryMode & G_SHADE) == 0;
		bFlatColors |= (gSP.geometryMode & G_SHADING_SMOOTH) == 0;
	}
	drw.flatColors = bFlatColors;

	if ((gli_tris.modify & MODIFY_XY) != 0)
		update_screen_coords_viewport(NULL);
	gli_tris.modify = 0;
}

/* Triangles of which a vertex is outside what the RSP clips to, clipped as
 * the RSP clips them, in order, a run at a time */
static void draw_clipped(const SPVertex *_vertices, const u16 *_elements, u32 _count)
{
	const u32 run = (u32)(sizeof(drw.clipped) / sizeof(drw.clipped[0])) / GLI_TRI_CLIP_MAX / 3 * 3;
	u32 first;
	for (first = 0; first < _count; first += run) {
		const u32 count = run < _count - first ? run : _count - first;
		gli_draw_triangles_params triParams = GLI_DRAW_TRIANGLES_PARAMS_DEFAULT;
		triParams.mode = GLI_DM_TRIANGLES;
		triParams.flatColors = drw.flatColors;
		if (_elements != NULL)
			triParams.verticesCount = gli_tri_clip(_vertices, _elements + first, count, drw.clipped);
		else
			triParams.verticesCount = gli_tri_clip(_vertices + first, NULL, count, drw.clipped);
		if (triParams.verticesCount == 0)
			continue;
		triParams.vertices = drw.clipped;
		triParams.combiner = current_combiner();
		gli_ctx.ops->drawTriangles(gli_ctx.impl, &triParams);
	}
}

/* what the triangles drew, for the frame buffer and software depth */
static void rendered(const SPVertex *_vertices, const u16 *_elements, u32 _count)
{
	const f32 maxY = gli_renderTriangles(_vertices, _elements, _count);
	gli_fb_set_changed(maxY);
	if (config.frameBufferEmulation.copyDepthToRDRAM == cdSoftwareRender &&
		gDP.otherMode.depthUpdate != 0) {
		struct FrameBuffer *pCurrentDepthBuffer = gli_fb_find(gDP.depthImageAddress);
		if (pCurrentDepthBuffer != NULL)
			gli_fb_set_dirty(gli_fb_of(pCurrentDepthBuffer));
	}
}

void gli_drawer_draw_triangles(void)
{
	if (gli_tris.num == 0 || !can_draw()) {
		gli_tris.num = 0;
		gli_tris.maxElement = 0;
		gli_tris.clip = 0;
		return;
	}

	prepare_draw_triangle();

	if (gli_tris.clip != 0) {
		draw_clipped(gli_tris.vertices, gli_tris.elements, gli_tris.num);
	} else {
		gli_draw_triangles_params triParams = GLI_DRAW_TRIANGLES_PARAMS_DEFAULT;
		triParams.mode = GLI_DM_TRIANGLES;
		triParams.flatColors = drw.flatColors;
		triParams.elementsType = GLI_DT_UNSIGNED_BYTE;
		triParams.verticesCount = (u32)gli_tris.maxElement + 1;
		triParams.elementsCount = gli_tris.num;
		triParams.vertices = gli_tris.vertices;
		triParams.elements = gli_tris.elements;
		triParams.combiner = current_combiner();
		gli_ctx.ops->drawTriangles(gli_ctx.impl, &triParams);
	}

	if (config.frameBufferEmulation.enable != 0)
		rendered(gli_tris.vertices, gli_tris.elements, gli_tris.num);

	gli_tris.num = 0;
	gli_tris.maxElement = 0;
	gli_tris.clip = 0;
}

static void draw_screen_space_triangle(u32 _numVtx, u32 _mode)
{
	f32 maxY = 0;
	u32 i;
	gli_draw_triangles_params triParams = GLI_DRAW_TRIANGLES_PARAMS_DEFAULT;

	if (_numVtx == 0 || !can_draw())
		return;

	/* Immersive mode */
	gli_gSPImmersiveScreenTriangles(drw.dmaVertices, _numVtx);

	for (i = 0; i < _numVtx; ++i) {
		SPVertex *vtx = &drw.dmaVertices[i];
		vtx->modify = MODIFY_ALL;
		if (vtx->y > maxY)
			maxY = vtx->y;
	}
	gli_tris.modify = MODIFY_ALL;

	gSP.changed &= ~CHANGED_GEOMETRYMODE; /* Don't update cull mode */
	prepare_draw_triangle();
	gli_ctx.ops->enable(gli_ctx.impl, GLI_EN_CULL_FACE, 0);

	triParams.mode = _mode;
	triParams.flatColors = drw.flatColors;
	triParams.verticesCount = _numVtx;
	triParams.vertices = drw.dmaVertices;
	triParams.combiner = current_combiner();
	gli_ctx.ops->drawTriangles(gli_ctx.impl, &triParams);
	drw.dmaVerticesNum = 0;

	gli_fb_set_changed(maxY);
	gSP.changed |= CHANGED_GEOMETRYMODE;
}

void gli_drawer_draw_screen_triangles(u32 _numVtx)
{
	draw_screen_space_triangle(_numVtx, GLI_DM_TRIANGLES);
}

void gli_drawer_draw_screen_strip(u32 _numVtx)
{
	draw_screen_space_triangle(_numVtx, GLI_DM_TRIANGLE_STRIP);
}

void gli_drawer_draw_dma_triangles(u32 _numVtx)
{
	bool clip = 0;
	u32 i;

	if (_numVtx == 0 || !can_draw())
		return;
	prepare_draw_triangle();

	for (i = 0; i < _numVtx && !clip; ++i)
		clip = gli_tri_clip_vertex_outside(&drw.dmaVertices[i]) != 0;
	if (clip) {
		draw_clipped(drw.dmaVertices, NULL, _numVtx);
	} else {
		gli_draw_triangles_params triParams = GLI_DRAW_TRIANGLES_PARAMS_DEFAULT;
		triParams.mode = GLI_DM_TRIANGLES;
		triParams.flatColors = drw.flatColors;
		triParams.verticesCount = _numVtx;
		triParams.vertices = drw.dmaVertices;
		triParams.combiner = current_combiner();
		gli_ctx.ops->drawTriangles(gli_ctx.impl, &triParams);
	}
	drw.dmaVerticesNum = 0;

	if (config.frameBufferEmulation.enable != 0)
		rendered(drw.dmaVertices, NULL, _numVtx);
}

SPVertex *gli_drawer_vertices(void)
{
	return gli_tris.vertices;
}

SPVertex *gli_drawer_dma_vertices(u32 _atLeast)
{
	/* the largest ask is 765, Turbo3D's 255 triangles */
	(void)_atLeast;
	return drw.dmaVertices;
}

SPVertex *gli_drawer_take_dma_vertices(u32 _n)
{
	SPVertex *first;
	/* drawn rather than grown: the vertices must not move under a game */
	if (drw.dmaVerticesNum + _n > DMA_VERTICES)
		gli_drawer_draw_dma_triangles(drw.dmaVerticesNum);
	first = &drw.dmaVertices[drw.dmaVerticesNum];
	drw.dmaVerticesNum += _n;
	return first;
}

void gli_drawer_draw_dma_pending(void)
{
	gli_drawer_draw_dma_triangles(drw.dmaVerticesNum);
}

void gli_drawer_draw_line(u32 _v0, u32 _v1, f32 _width, u32 _flag)
{
	f32 lineWidth = _width;
	SPVertex vertexBuf[2];

	gli_texrect_draw();

	if (!can_draw())
		return;

	if (config.frameBufferEmulation.nativeResFactor == 0)
		lineWidth *= g_window.scaleX;
	else
		lineWidth *= config.frameBufferEmulation.nativeResFactor;
	if (lineWidth > drw.maxLineWidth) {
		gli_drawer_thick_line(drw.dmaVertices, _v0, _v1, _width * 0.5f, _flag);
		draw_screen_space_triangle(4, GLI_DM_TRIANGLE_STRIP);
		return;
	}

	if ((gli_tris.vertices[_v0].modify & MODIFY_XY) != 0)
		gSP.changed &= ~CHANGED_VIEWPORT;
	if (gSP.changed || gDP.changed)
		update_states(DRAWING_LINE);

	drw.drawingState = DRAWING_LINE;

	if ((gli_tris.vertices[_v0].modify & MODIFY_XY) != 0)
		update_screen_coords_viewport(NULL);

	vertexBuf[0] = gli_tris.vertices[_v0];
	vertexBuf[1] = gli_tris.vertices[_v1];
	gli_ctx.ops->drawLine(gli_ctx.impl, lineWidth, vertexBuf);
}

void gli_drawer_draw_rect(s32 _ulx, s32 _uly, s32 _lrx, s32 _lry)
{
	f32 scale[2];
	s32 rect[4];
	gli_draw_rects_params rectParams = GLI_DRAW_RECTS_PARAMS_DEFAULT;

	gli_texrect_draw();

	if (!can_draw())
		return;

	gSP.changed &= ~CHANGED_GEOMETRYMODE; /* Don't update cull mode */
	gSP.changed &= ~CHANGED_VIEWPORT; /* Don't update viewport */
	if (gSP.changed || gDP.changed)
		update_states(DRAWING_RECT);

	drw.drawingState = DRAWING_RECT;

	update_screen_coords_viewport(NULL);

	gli_ctx.ops->enable(gli_ctx.impl, GLI_EN_CULL_FACE, 0);

	gli_fb_coords_scales(gli_fb_current(), &scale[0], &scale[1]);
	rect[0] = _ulx;
	rect[1] = _uly;
	rect[2] = _lrx;
	rect[3] = _lry;
	if (!gli_drawer_fill_rect(drw.rect, rect, scale, g_window.adjustScreen, g_window.adjustScale)) {
		gSP.changed |= CHANGED_GEOMETRYMODE | CHANGED_VIEWPORT;
		drw.drawingState = DRAWING_NONE;
		return;
	}

	rectParams.mode = GLI_DM_TRIANGLE_STRIP;
	rectParams.texrect = 0;
	rectParams.verticesCount = 4;
	rectParams.vertices = drw.rect;
	rectParams.combiner = current_combiner();
	gli_ctx.ops->drawRects(gli_ctx.impl, &rectParams);
	gSP.changed |= CHANGED_GEOMETRYMODE | CHANGED_VIEWPORT;
}

static bool texrect_shadow_map(const gli_texrect_params *_params)
{
	struct FrameBuffer *pCurrentBuffer = gli_fb_current();
	(void)_params;
	if (pCurrentBuffer != NULL) {
		if (gDP.textureImage.size == 2 && gDP.textureImage.address >= gDP.depthImageAddress &&
			gDP.textureImage.address < (gDP.depthImageAddress + gDP.colorImage.width * gDP.colorImage.width * 6 / 4)) {
			gli_fb *fb;

			if (!gli_ctx.integerTextures)
				return 1;

			fb = gli_fb_of(pCurrentBuffer);
			gli_depth_activate_texture(fb->m_pDepthBuffer, fb);
			gli_combiner_set_depth_fog();
			/* DepthFogCombiner does not support shader blending. */
			apply_blend_mode(1);
			return 0;
		}
	}
	return 0;
}

static bool texrect_depth_buffer_copy(const gli_texrect_params *_params)
{
	/* Copy one line from depth buffer into auxiliary color buffer with
	 * height = 1. Data from depth buffer loaded into TMEM and then rendered
	 * to RDRAM by texrect. Works only with depth buffer emulation enabled.
	 * Load of arbitrary data to that area causes weird camera rotation in
	 * CBFD. */
	if (gli_texrect_is_depth_copy()) {
		struct FrameBuffer *pBuffer;
		if (config.frameBufferEmulation.copyDepthToRDRAM == cdDisable)
			return 1;
		pBuffer = gli_fb_current();
		if (pBuffer == NULL)
			return 1;
		gli_fb_of(pBuffer)->m_cleared = 1;
		if (config.frameBufferEmulation.copyDepthToRDRAM == cdCopyFromVRam) {
			if (rectDepthBufferCopyFrame != g_window.buffersSwapCount) {
				rectDepthBufferCopyFrame = g_window.buffersSwapCount;
				if (!gli_fb_copy_depth(gDP.depthImageAddress))
					return 1;
			}
			gli_RDP_RepeatLastLoadBlock();
		}

		gli_texrect_depth_row(_params);

		return 1;
	}
	return 0;
}

static bool texrect_copy_to_itself(const gli_texrect_params *_params)
{
	struct FrameBuffer *pCurrent = gli_fb_current();
	if (pCurrent != NULL) {
		const gli_fb *fb = gli_fb_of(pCurrent);
		if (fb->m_size == G_IM_SIZ_8b && gSP.textureTile[0]->frameBufferAddress == fb->m_startAddress)
			return 1;
	}
	return texrect_depth_buffer_copy(_params);
}

static bool texrect_bg_copy(const gli_texrect_params *_params)
{
	if (!gli_texrect_bg_copy(_params))
		return 0;
	gli_fb_remove(gDP.colorImage.address);
	return 1;
}

static bool texrect_palette_mod(const gli_texrect_params *_params)
{
	if (gDP.textureImage.address == 0x400) {
		/* Paper Mario uses complex set of actions to prepare darkness
		 * texture. It includes manipulations with texture formats and
		 * drawing buffer into itsels. All that stuff is hardly possible to
		 * reproduce with GL, so I just use dirty hacks to emualte it. */

		if (gDP.colorImage.address == 0x400 && gDP.colorImage.width == 64) {
			memcpy(RDRAM + 0x400, RDRAM + 0x14d500, 4096);
			return 1;
		}

		if (gDP.textureImage.width == 64) {
			gDPTile *curTile = &gDP.tiles[0];
			curTile->frameBufferAddress = 0;
			curTile->textureMode = TEXTUREMODE_NORMAL;
			gli_textures_update(0);
			gli_cmb.current->ops->update(gli_cmb.current, 0);
		}
		return 0;
	}

	/* Modify palette for Paper Mario "2D lighting" effect */
	return gli_texrect_palette_mod(_params) != 0;
}

void gli_drawer_draw_textured_rect(const gli_texrect_params *_params, const struct FrameBuffer *_pBuffer)
{
	const gli_cprog *pCurrentCombiner;
	gli_tex *const *const current = gli_texture_current;
	const bool bUseBilinear = gDP.otherMode.textureFilter != 0;
	int immersiveFullScreen = 0;
	bool immersiveRect, bUseTexrectDrawer;
	const gli_fb *buf = _pBuffer != NULL ? gli_fb_of((struct FrameBuffer *)_pBuffer) : NULL;
	f32 scale[2];
	const gli_tex *tex[2];
	u32 clamp, t;
	gli_draw_rects_params rectParams = GLI_DRAW_RECTS_PARAMS_DEFAULT;

	gSP.changed &= ~CHANGED_GEOMETRYMODE; /* Don't update cull mode */
	drw.drawingState = DRAWING_TEXRECT;

	if (gli_texrect_can_continue()) {
		set_polygon_mode(DRAWING_TEXRECT);
		gli_combiner_update();
		update_textures();
		gli_combiner_update_parameters();
	} else {
		if (!gli_texrect_is_empty())
			gli_texrect_draw();
		gSP.changed &= ~CHANGED_GEOMETRYMODE; /* Don't update cull mode */
		gSP.changed &= ~CHANGED_VIEWPORT; /* Don't update viewport */
		if (_params->texrectCmd && (gSP.changed | gDP.changed) != 0)
			update_states(DRAWING_TEXRECT);
		gli_ctx.ops->enable(gli_ctx.impl, GLI_EN_CULL_FACE, 0);

		if (_params->texrectCmd && drw.texrectSpecial != NULL && drw.texrectSpecial(_params)) {
			gSP.changed |= CHANGED_GEOMETRYMODE | CHANGED_VIEWPORT;
			return;
		}

		if (!can_draw())
			return;
	}

	pCurrentCombiner = current_combiner();
	immersiveRect = _params->texrectCmd && gli_drawer_immersive_rect(&_params->ulx, &immersiveFullScreen);
	bUseTexrectDrawer = !immersiveRect && (drw.bgMode || ((config.graphics2D.enableNativeResTexrects != 0)
		&& bUseBilinear
		&& pCurrentCombiner->ops->usesTexture(pCurrentCombiner)
		&& (buf == NULL || !buf->m_cfb)
		&& (current[0] != NULL)
		&& ((current[0]->frameBufferTexture == GLI_TEX_FB_NONE && !current[0]->bHDTexture))
		&& (current[1] == NULL || (current[1]->frameBufferTexture == GLI_TEX_FB_NONE && !current[1]->bHDTexture))));

	gli_fb_coords_scales(_pBuffer, &scale[0], &scale[1]);

	/* the cached textures the combiner samples, read in place */
	for (t = 0; t < 2; ++t) {
		tex[t] = NULL;
		if (pCurrentCombiner->ops->usesTile(pCurrentCombiner, t) && current[t] != NULL && gSP.textureTile[t] != NULL)
			tex[t] = current[t];
	}

	clamp = gli_drawer_texrect_vertices(drw.rect, _params, tex, scale, g_window.adjustScreen, g_window.adjustScale);
	for (t = 0; t < 2; ++t) {
		const u32 c = (clamp >> (2 * t)) & 3;
		gli_tex_parameters texParams = GLI_TEX_PARAMETERS_DEFAULT;
		if (c == 0)
			continue;
		if (c & 1)
			texParams.wrapS = GLI_TP_WRAP_CLAMP_TO_EDGE;
		if (c & 2)
			texParams.wrapT = GLI_TP_WRAP_CLAMP_TO_EDGE;
		texParams.handle = current[t]->name;
		texParams.target = GLI_TT_TEXTURE_2D;
		texParams.textureUnitIndex = t == 0 ? GLI_TU_TEX0 : GLI_TU_TEX1;
		gli_ctx.ops->setTextureParameters(gli_ctx.impl, &texParams);
	}

	if (gDP.otherMode.cycleType == G_CYC_COPY && current[0]->frameBufferTexture != GLI_TEX_FB_MULTI_SAMPLE) {
		gli_tex_parameters texParams = GLI_TEX_PARAMETERS_DEFAULT;
		texParams.handle = current[0]->name;
		texParams.target = GLI_TT_TEXTURE_2D;
		texParams.textureUnitIndex = GLI_TU_TEX0;
		texParams.minFilter = GLI_TP_FILTER_NEAREST;
		texParams.magFilter = GLI_TP_FILTER_NEAREST;
		gli_ctx.ops->setTextureParameters(gli_ctx.impl, &texParams);
	}

	if (bUseTexrectDrawer) {
		if (drw.bgMode) {
			gli_texrect_add_background_rect();
			return;
		}
		if (gli_texrect_add_rect())
			return;
	}

	if (immersiveRect)
		gli_drawer_immersive_clip(drw.rect, immersiveFullScreen, scale);

	update_screen_coords_viewport(_pBuffer);
	rectParams.mode = GLI_DM_TRIANGLE_STRIP;
	rectParams.verticesCount = 4;
	rectParams.vertices = drw.rect;
	rectParams.combiner = current_combiner();
	gli_ctx.ops->drawRects(gli_ctx.impl, &rectParams);

	gSP.changed |= CHANGED_GEOMETRYMODE | CHANGED_VIEWPORT;
}

void gli_drawer_textured_rect(f32 _ulx, f32 _uly, f32 _lrx, f32 _lry, f32 _dsdx, f32 _dtdy, s32 _s, s32 _t, int _flip)
{
	gli_texrect_params params;
	params.ulx = _ulx;
	params.uly = _uly;
	params.lrx = _lrx;
	params.lry = _lry;
	params.dsdx = _dsdx;
	params.dtdy = _dtdy;
	params.s = (s16)_s;
	params.t = (s16)_t;
	params.flip = (u8)_flip;
	params.forceAjustScale = 0;
	params.texrectCmd = 1;
	if (config.graphics2D.enableNativeResTexrects == 0 && config.graphics2D.correctTexrectCoords != tcDisable)
		gli_drawer_texrect_correct(&params.ulx, drw.texrectRect);
	gli_drawer_draw_textured_rect(&params, gli_fb_current());
}

void gli_drawer_clear_depth(void)
{
	if (!can_draw())
		return;

	gli_depth_clear();

	update_depth_update();
}

void gli_drawer_clear_color(const f32 *_pColor)
{
	if (_pColor != NULL)
		gli_ctx.ops->clearColorBuffer(gli_ctx.impl, _pColor[0], _pColor[1], _pColor[2], _pColor[3]);
	else
		gli_ctx.ops->clearColorBuffer(gli_ctx.impl, 0.0f, 0.0f, 0.0f, 0.0f);
}

void gli_drawer_copy(const gli_copy_params *_params)
{
	u32 i;
	gli_draw_rects_params rectParams = GLI_DRAW_RECTS_PARAMS_DEFAULT;

	drw.drawingState = DRAWING_TEXRECT;

	gli_drawer_copy_rect(drw.rect, &_params->r);

	for (i = 0; i < 2; ++i) {
		const gli_tex *tex = _params->tex[i];
		gli_tex_parameters texParams = GLI_TEX_PARAMETERS_DEFAULT;
		if (tex == NULL)
			continue;

		texParams.handle = tex->name;
		texParams.textureUnitIndex = i == 0 ? GLI_TU_TEX0 : GLI_TU_TEX1;
		if (tex->frameBufferTexture == GLI_TEX_FB_MULTI_SAMPLE) {
			texParams.target = GLI_TT_TEXTURE_2D_MULTISAMPLE;
		} else {
			texParams.target = GLI_TT_TEXTURE_2D;
			texParams.minFilter = _params->filter;
			texParams.magFilter = _params->filter;
			texParams.wrapS = GLI_TP_WRAP_CLAMP_TO_EDGE;
			texParams.wrapT = GLI_TP_WRAP_CLAMP_TO_EDGE;
		}
		gli_ctx.ops->setTextureParameters(gli_ctx.impl, &texParams);
	}

	gli_ctx.ops->setViewport(gli_ctx.impl, 0, 0, (s32)_params->r.dstWidth, (s32)_params->r.dstHeight);
	gli_ctx.ops->enable(gli_ctx.impl, GLI_EN_CULL_FACE, 0);
	gli_ctx.ops->enable(gli_ctx.impl, GLI_EN_BLEND, 0);
	gli_ctx.ops->enable(gli_ctx.impl, GLI_EN_DEPTH_TEST, 0);
	gli_ctx.ops->enableDepthWrite(gli_ctx.impl, 0);

	rectParams.mode = GLI_DM_TRIANGLE_STRIP;
	rectParams.verticesCount = 4;
	rectParams.vertices = drw.rect;
	rectParams.combiner = _params->combiner;
	_params->combiner->ops->activate((void *)_params->combiner);
	gli_ctx.ops->enable(gli_ctx.impl, GLI_EN_SCISSOR_TEST, 0);
	gli_ctx.ops->drawRects(gli_ctx.impl, &rectParams);
	gli_ctx.ops->enable(gli_ctx.impl, GLI_EN_SCISSOR_TEST, 1);

	gSP.changed |= CHANGED_GEOMETRYMODE | CHANGED_VIEWPORT;
	gDP.changed |= CHANGED_RENDERMODE | CHANGED_TILE | CHANGED_COMBINE;
}

void gli_drawer_blit_or_copy(const gli_blit_copy_params *_params)
{
	gli_blit_params blitParams = GLI_BLIT_PARAMS_DEFAULT;
	const gli_copy_rect *r = &_params->copy.r;
	blitParams.readBuffer = _params->readBuffer;
	blitParams.drawBuffer = _params->drawBuffer;
	blitParams.srcX0 = r->invertX ? r->srcX1 : r->srcX0;
	blitParams.srcX1 = r->invertX ? r->srcX0 : r->srcX1;
	blitParams.dstX0 = r->dstX0;
	blitParams.dstX1 = r->dstX1;
	blitParams.srcY0 = r->invertY ? r->srcY1 : r->srcY0;
	blitParams.srcY1 = r->invertY ? r->srcY0 : r->srcY1;
	blitParams.dstY0 = r->dstY0;
	blitParams.dstY1 = r->dstY1;
	blitParams.mask = _params->mask;
	blitParams.filter = _params->copy.filter;

	if (gli_ctx.ops->blitFramebuffers(gli_ctx.impl, &blitParams))
		return;

	gli_ctx.ops->bindFramebuffer(gli_ctx.impl, gli_ctx.readFramebuffer, _params->readBuffer);
	gli_ctx.ops->bindFramebuffer(gli_ctx.impl, gli_ctx.drawFramebuffer, _params->drawBuffer);
	gli_drawer_copy(&_params->copy);
}

void gli_drawer_copy_texrect(const gli_copy_rect *_p, struct gli_tex *_tex, u32 _filter)
{
	gli_copy_params params;
	params.r = *_p;
	params.tex[0] = _tex;
	params.tex[1] = NULL;
	params.combiner = gli_cmb.texrectCopy;
	params.filter = _filter;
	gli_drawer_copy(&params);
}

/* The drawer's private state, for the texrect drawer alone */

RectVertex *gli_drawer_rect(void)
{
	return drw.rect;
}

void gli_drawer_set_background_mode(bool _on)
{
	drw.bgMode = _on;
}

void gli_drawer_texrect_states(void)
{
	update_states(DRAWING_TEXRECT);
	gli_ctx.ops->enable(gli_ctx.impl, GLI_EN_CULL_FACE, 0);
}

void gli_drawer_set_blend_mode(void)
{
	apply_blend_mode(0);
}

void gli_drawer_update_depth_compare(void)
{
	update_depth_compare();
}

void gli_drawer_screen_viewport(const struct FrameBuffer *_pBuffer)
{
	update_screen_coords_viewport(_pBuffer);
}

void gli_drawer_update_scissor(struct FrameBuffer *_pBuffer)
{
	update_scissor(_pBuffer);
}

bool gli_drawer_is_drawing(void)
{
	return drw.drawingState != DRAWING_NONE;
}

bool gli_drawer_is_triangle(void)
{
	return drw.drawingState == DRAWING_TRIANGLE;
}

void gli_drawer_drop_render_state(void)
{
	drw.drawingState = DRAWING_NONE;
}

static void init_states(void)
{
	gli_ctx.ops->enable(gli_ctx.impl, GLI_EN_CULL_FACE, 0);
	gli_ctx.ops->enable(gli_ctx.impl, GLI_EN_SCISSOR_TEST, 1);
	gli_ctx.ops->enableDepthWrite(gli_ctx.impl, 0);
	gli_ctx.ops->setDepthCompare(gli_ctx.impl, GLI_COMPARE_ALWAYS);

	if (config.frameBufferEmulation.N64DepthCompare != 0) {
		gli_ctx.ops->enable(gli_ctx.impl, GLI_EN_DEPTH_TEST, 0);
		gli_ctx.ops->enable(gli_ctx.impl, GLI_EN_POLYGON_OFFSET_FILL, 0);
	} else {
		gli_ctx.ops->enable(gli_ctx.impl, GLI_EN_DEPTH_TEST, 1);
#if defined(OS_ANDROID) || defined(OS_IOS)
		if (config.generalEmulation.forcePolygonOffset != 0)
			gli_ctx.ops->setPolygonOffset(gli_ctx.impl, config.generalEmulation.polygonOffsetFactor,
				config.generalEmulation.polygonOffsetUnits);
		else
#endif
			gli_ctx.ops->setPolygonOffset(gli_ctx.impl, -3.0f, -3.0f);
	}

	gli_ctx.ops->setViewport(gli_ctx.impl, 0, (s32)g_window.heightOffset, (s32)g_window.screenWidth,
		(s32)g_window.screenHeight);

	gli_ctx.ops->clearColorBuffer(gli_ctx.impl, 0.0f, 0.0f, 0.0f, 0.0f);

	srand((unsigned int)time(NULL));

	gli_window_swap_buffers();
}

static void set_special_texrect(void)
{
	char name[sizeof(RSP.romname)];
	size_t i;
	for (i = 0; i + 1 < sizeof(name) && RSP.romname[i] != '\0'; ++i)
		name[i] = (char)toupper((unsigned char)RSP.romname[i]);
	name[i] = '\0';
#define FOUND(romname) (strstr(name, romname) != NULL)

	if (FOUND("BEETLE") || FOUND("HSV") || FOUND("DUCK DODGERS") || FOUND("DAFFY DUCK"))
		drw.texrectSpecial = texrect_shadow_map;
	else if (FOUND("PERFECT DARK") || FOUND("TUROK_DINOSAUR_HUNTE"))
		drw.texrectSpecial = texrect_depth_buffer_copy; /* See comments to that function! */
	else if (FOUND("CONKER BFD"))
		drw.texrectSpecial = texrect_copy_to_itself;
	else if (FOUND("YOSHI STORY"))
		drw.texrectSpecial = texrect_bg_copy;
	else if (FOUND("PAPER MARIO") || FOUND("MARIO STORY"))
		drw.texrectSpecial = texrect_palette_mod;
	else
		drw.texrectSpecial = NULL;
#undef FOUND
}

/* the drawer's data, made and dropped with the context */
void gli_drawer_init_data(void)
{
	init_states();
	set_special_texrect();

	gli_textures_init();
	gli_depth_list_init();
	gli_fb_emulation_init();
	gli_combiner_init();
	gli_TFH_init();
	gli_pp_init();
	gli_zlut_init();
	gli_noise_init();
	gli_palette_init();
	gli_fbinfo_reset();
	gli_texrect_init();
	drw.drawingState = DRAWING_NONE;
	drw.maxLineWidth = gli_ctx.ops->getMaxLineWidth(gli_ctx.impl);

	gSP.changed = gDP.changed = 0xFFFFFFFF;

	memset(gli_tris.vertices, 0, sizeof(gli_tris.vertices));
	memset(gli_tris.elements, 0, sizeof(gli_tris.elements));
	gli_tris.num = 0;
	gli_tris.clip = 0;
	drw.dmaVerticesNum = 0;
}

void gli_drawer_destroy_data(void)
{
	drw.drawingState = DRAWING_NONE;
	gli_texrect_destroy();
	gli_palette_destroy();
	gli_zlut_destroy();
	gli_noise_destroy();
	gli_pp_destroy();
	if (gli_TFH_options_changed())
		gli_TFH_shutdown();
	gli_combiner_destroy();
	gli_fb_emulation_destroy();
	gli_depth_list_destroy();
	gli_textures_destroy();
}
