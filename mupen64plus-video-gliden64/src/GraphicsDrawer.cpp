#include <algorithm>
#include <string>
#include <assert.h>
#include <cmath>
#include "Platform.h"
#include "Graphics/Context.h"
#include "DisplayWindow.h"
#include "SoftwareRender.h"
#include "GraphicsDrawer.h"
#include "gli_drawer.h"
#include "gli_blend.h"
#include "TextureFilterHandler.h"
#include "PostProcessor.h"
#include "NoiseTexture.h"
#include "ZlutTexture.h"
#include "PaletteTexture.h"
#include "FrameBuffer.h"
#include "DepthBuffer.h"
#include "FrameBufferInfo.h"
#include "Config.h"
#include "RSP.h"
#include "RDP.h"
#include "VI.h"

using namespace graphics;

GraphicsDrawer::GraphicsDrawer()
: m_drawingState(DrawingState::Non)
, m_dmaVerticesNum(0)
, m_modifyVertices(0)
, m_maxLineWidth(1.0f)
, m_bFlatColors(false)
, m_bBGMode(false)
{
	memset(m_rect, 0, sizeof(m_rect));
	memset(m_texrectRect, 0, sizeof(m_texrectRect));
	gli_drawer_bind_triangles(triangles.vertices.data(), triangles.elements.data(), &triangles.num,
		&triangles.maxElement, &m_modifyVertices, &Context::ClipControl);
}

GraphicsDrawer::~GraphicsDrawer()
{
}

void GraphicsDrawer::_updateCullFace() const
{
	static const CullModeParam modes[] = { cullMode::FRONT, cullMode::FRONT, cullMode::BACK, cullMode::FRONT_AND_BACK };
	const int mode = gli_drawer_cull_mode();
	gfxContext.enable(enable::CULL_FACE, mode != GLI_CULL_NONE);
	if (mode != GLI_CULL_NONE)
		gfxContext.cullFace(modes[mode]);
}

void GraphicsDrawer::_updateDepthUpdate() const
{
	gfxContext.enableDepthWrite(gDP.otherMode.depthUpdate != 0);
}

void GraphicsDrawer::_updateDepthCompare() const
{
	static const CompareParam compares[] = { compare::LESS, compare::LEQUAL, compare::ALWAYS };
	gli_depth_state d;
	gli_drawer_depth_state(&d);
	if (d.set & GLI_DS_COMPARE) {
		gfxContext.enable(enable::POLYGON_OFFSET_FILL, d.offset != 0);
		gfxContext.setDepthCompare(compares[d.compare]);
	}
	if (d.set & GLI_DS_WRITE)
		gfxContext.enableDepthWrite(d.write != 0);
	if (d.set & GLI_DS_TEST)
		gfxContext.enable(enable::DEPTH_TEST, d.test != 0);
	if (d.set & GLI_DS_CLAMP)
		gfxContext.setClampMode(d.clipping ? graphics::ClampMode::ClippingEnabled : graphics::ClampMode::NoClipping);
}

SPVertex * gli_drawer_vertices(void)
{
	return dwnd().getDrawer().getVertexPtr(0);
}

SPVertex * gli_drawer_dma_vertices(u32 _atLeast)
{
	/* the largest ask is 765, Turbo3D's 255 triangles; 4096 are allocated at load */
	(void)_atLeast;
	return dwnd().getDrawer().getDMAVerticesData();
}

void gli_drawer_draw_triangles(void)
{
	dwnd().getDrawer().drawTriangles();
}

void gli_drawer_draw_dma_triangles(u32 _numVtx)
{
	dwnd().getDrawer().drawDMATriangles(_numVtx);
}

void gli_drawer_draw_screen_triangles(u32 _numVtx)
{
	dwnd().getDrawer().drawScreenSpaceTriangle(_numVtx, graphics::drawmode::TRIANGLES);
}

SPVertex * gli_drawer_take_dma_vertices(u32 _n)
{
	GraphicsDrawer & drawer = dwnd().getDrawer();
	SPVertex * first;
	u32 i;
	/* flush rather than grow: the vector must not move under a game */
	if (drawer.getDMAVerticesCount() + _n > drawer.getDMAVerticesCapacity())
		drawer.drawDMATriangles(drawer.getDMAVerticesCount());
	first = &drawer.getCurrentDMAVertex();
	for (i = 1; i < _n; ++i)
		drawer.getCurrentDMAVertex();
	return first;
}

void gli_drawer_draw_dma_pending(void)
{
	GraphicsDrawer & drawer = dwnd().getDrawer();
	drawer.drawDMATriangles(drawer.getDMAVerticesCount());
}

void gli_drawer_draw_screen_strip(u32 _numVtx)
{
	dwnd().getDrawer().drawScreenSpaceTriangle(_numVtx);
}

void gli_drawer_clear_depth(void)
{
	dwnd().getDrawer().clearDepthBuffer();
}

void gli_drawer_draw_rect(s32 _ulx, s32 _uly, s32 _lrx, s32 _lry)
{
	dwnd().getDrawer().drawRect(_ulx, _uly, _lrx, _lry);
}

void gli_drawer_textured_rect(f32 _ulx, f32 _uly, f32 _lrx, f32 _lry, f32 _dsdx, f32 _dtdy, s16 _s, s16 _t, bool _flip)
{
	GraphicsDrawer & drawer = dwnd().getDrawer();
	GraphicsDrawer::TexturedRectParams params(_ulx, _uly, _lrx, _lry, _dsdx, _dtdy, _s, _t,
		_flip, false, true, frameBufferList().getCurrent());
	if (config.graphics2D.enableNativeResTexrects == 0 && config.graphics2D.correctTexrectCoords != tcDisable)
		drawer.correctTexturedRectParams(params);
	drawer.drawTexturedRect(params);
}

void gli_drawer_draw_line(u32 _v0, u32 _v1, f32 _width, u32 _flag)
{
	dwnd().getDrawer().drawLine(_v0, _v1, _width, _flag);
}

SPVertex & GraphicsDrawer::getCurrentDMAVertex()
{
	return m_dmaVertices[m_dmaVerticesNum++];
}

inline
s32 roundup(f32 _v, f32 _scale)
{
	return static_cast<s32>(floorf(_v * _scale + 0.5f));
}

void GraphicsDrawer::updateScissor(FrameBuffer * _pBuffer) const
{
	DisplayWindow & wnd = DisplayWindow::get();
	f32 scaleX, scaleY;
	f32 offsetX = 0.0f, offsetY = 0.0f;
	if (_pBuffer == nullptr) {
		scaleX = wnd.getScaleX();
		scaleY = wnd.getScaleY();
	} else {
		scaleX = _pBuffer->m_scale;
		scaleY = _pBuffer->m_scale;
		offsetX = f32(_pBuffer->m_originX);
		offsetY = f32(_pBuffer->m_originY);
	}

	s32 rect[4];
	gli_drawer_scissor_rect(scaleX, scaleY, offsetX, offsetY,
		gli_drawer_need_adjust(wnd.isAdjustScreen()), wnd.getAdjustScale(),
		gli_gSPImmersiveActive(), rect);
	gfxContext.setScissor(rect[0], rect[1], rect[2], rect[3]);

	gDP.changed &= ~CHANGED_SCISSOR;
}

void GraphicsDrawer::_updateViewport() const
{
	DisplayWindow & wnd = DisplayWindow::get();
	FrameBuffer * pCurrentBuffer = frameBufferList().getCurrent();
	s32 rect[4];
	if (pCurrentBuffer == nullptr)
		gli_drawer_viewport_rect(0, wnd.getScaleX(), wnd.getScaleY(), 0, 0,
			gli_drawer_need_adjust(wnd.isAdjustScreen()), wnd.getAdjustScale(), rect);
	else
		gli_drawer_viewport_rect(1, pCurrentBuffer->m_scale, pCurrentBuffer->m_scale,
			pCurrentBuffer->m_originX, pCurrentBuffer->m_originY,
			gli_drawer_need_adjust(wnd.isAdjustScreen()), wnd.getAdjustScale(), rect);
	gfxContext.setViewport(rect[0], rect[1], rect[2], rect[3]);
	gSP.changed &= ~CHANGED_VIEWPORT;
}

void GraphicsDrawer::_updateScreenCoordsViewport(const FrameBuffer * _pBuffer) const
{
	DisplayWindow & wnd = DisplayWindow::get();
	const FrameBuffer * pCurrentBuffer = _pBuffer != nullptr ? _pBuffer : frameBufferList().getCurrent();

	u32 bufferWidth, bufferHeight;
	f32 viewportScaleX, viewportScaleY;
	s32 X = 0, Y = 0;
	if (pCurrentBuffer == nullptr) {
		bufferWidth = VI.width;
		bufferHeight = VI.height;
		viewportScaleX = wnd.getScaleX();
		viewportScaleY = wnd.getScaleY();
	} else {
		bufferWidth = pCurrentBuffer->m_width;
		bufferHeight = gli_VI_GetMaxBufferHeight(bufferWidth);
		viewportScaleX = viewportScaleY = pCurrentBuffer->m_scale;
		X = roundup(f32(pCurrentBuffer->m_originX), viewportScaleX);
		Y = roundup(f32(pCurrentBuffer->m_originY), viewportScaleY);
	}

	gfxContext.setViewport(X, Y, roundup(f32(bufferWidth), viewportScaleX), roundup(f32(bufferHeight), viewportScaleY));
	gSP.changed |= CHANGED_VIEWPORT;
}

static BlendParam _blendParam(u8 _f)
{
	switch (_f) {
	case GLI_BF_ONE: return blend::ONE;
	case GLI_BF_SRC_ALPHA: return blend::SRC_ALPHA;
	case GLI_BF_ONE_MINUS_SRC_ALPHA: return blend::ONE_MINUS_SRC_ALPHA;
	case GLI_BF_DST_ALPHA: return blend::DST_ALPHA;
	case GLI_BF_CONSTANT_ALPHA: return blend::CONSTANT_ALPHA;
	case GLI_BF_ONE_MINUS_CONSTANT_ALPHA: return blend::ONE_MINUS_CONSTANT_ALPHA;
	}
	return blend::ZERO;
}

static void _applyBlendMode(bool _legacy)
{
	/* Blast Corps: the combiner is asked under the same guard as before */
	const bool blastCorpsTexture = (config.generalEmulation.hacks & hack_blastCorps) != 0 &&
		gDP.otherMode.cycleType < G_CYC_COPY && gSP.texture.on == 0 && currentCombiner()->usesTexture();
	gli_blend_state b;
	gli_blend_mode(_legacy, blastCorpsTexture, &b);
	if (b.fogColor)
		gfxContext.setBlendColor(gDP.fogColor.r, gDP.fogColor.g, gDP.fogColor.b, gDP.fogColor.a);
	gfxContext.enable(enable::BLEND, b.enable != 0);
	if (b.enable)
		gfxContext.setBlending(_blendParam(b.src), _blendParam(b.dst));
}

void GraphicsDrawer::_setBlendMode() const
{
	_applyBlendMode(false);
}

void GraphicsDrawer::_updateTextures() const
{
	//For some reason updating the texture cache on the first frame of LOZ:OOT causes a nullptr Pointer exception...
	CombinerInfo & cmbInfo = CombinerInfo::get();
	CombinerProgram * pCurrentCombiner = cmbInfo.getCurrent();
	if (pCurrentCombiner != nullptr) {
		for (u32 t = 0; t < 2; ++t) {
			if (pCurrentCombiner->usesTile(t))
				textureCache().update(t);
			else
				textureCache().activateDummy(t);
		}
	}
	gDP.changed &= ~(CHANGED_TILE | CHANGED_TMEM);
	gSP.changed &= ~(CHANGED_TEXTURE);
}

void GraphicsDrawer::_updateStates(DrawingState _drawingState) const
{
	CombinerInfo & cmbInfo = CombinerInfo::get();
	cmbInfo.setPolygonMode(_drawingState);
	cmbInfo.update();

	if (gSP.changed & CHANGED_GEOMETRYMODE) {
		_updateCullFace();
		gSP.changed &= ~CHANGED_GEOMETRYMODE;
	}

	_updateDepthCompare();

	if (gDP.changed & CHANGED_SCISSOR)
		updateScissor(frameBufferList().getCurrent());

	if (gSP.changed & CHANGED_VIEWPORT)
		_updateViewport();

	if ((gSP.changed & CHANGED_TEXTURE) ||
		(gDP.changed & (CHANGED_TILE | CHANGED_TMEM)) ||
		cmbInfo.isChanged() ||
		_drawingState == DrawingState::TexRect) {
		_updateTextures();
	}

	if ((gDP.changed & (CHANGED_RENDERMODE | CHANGED_CYCLETYPE))) {
		_setBlendMode();
		gDP.changed &= ~(CHANGED_RENDERMODE | CHANGED_CYCLETYPE);
	}

	cmbInfo.updateParameters();

	if (!config.generalEmulation.enableFragmentDepthWrite)
		return;

	if (gli_isCurrentColorImageDepthImage() &&
		config.generalEmulation.enableFragmentDepthWrite != 0 &&
		config.frameBufferEmulation.N64DepthCompare == 0) {
		// Current render target is depth buffer.
		// Shader will set gl_FragDepth to shader color, see ShaderCombiner ctor
		// Here we enable depth buffer write.
		if (gDP.otherMode.cycleType <= G_CYC_2CYCLE && gDP.otherMode.depthCompare != 0) {
			// Render to depth buffer with depth compare. Need to get copy of current depth buffer.
			FrameBuffer * pCurBuf = frameBufferList().getCurrent();
			if (pCurBuf != nullptr && pCurBuf->m_pDepthBuffer != nullptr) {
				CachedTexture * pDepthTexture = pCurBuf->m_pDepthBuffer->copyDepthBufferTexture(pCurBuf);
				if (pDepthTexture == nullptr)
					return;
				Context::TexParameters params;
				params.handle = pDepthTexture->name;
				params.target = textureTarget::TEXTURE_2D;
				params.textureUnitIndex = textureIndices::DepthTex;
				params.maxMipmapLevel = 0;
				params.minFilter = textureParameters::FILTER_NEAREST;
				params.magFilter = textureParameters::FILTER_NEAREST;
				gfxContext.setTextureParameters(params);
			}
		} else if (frameBufferList().getCurrent() == nullptr) {
			gfxContext.enable(enable::BLEND, true);
			gfxContext.setBlending(blend::ZERO, blend::ONE);
		}
		gfxContext.enable(enable::DEPTH_TEST, true);
		gfxContext.setDepthCompare(compare::ALWAYS);
		gfxContext.enableDepthWrite(true);
		gDP.changed |= CHANGED_RENDERMODE;
	}
}

void GraphicsDrawer::_prepareDrawTriangle()
{
	gli_texrect_draw();

	if ((m_modifyVertices & MODIFY_XY) != 0)
		gSP.changed &= ~CHANGED_VIEWPORT;

	if (gSP.changed || gDP.changed)
		_updateStates(DrawingState::Triangle);

	m_drawingState = DrawingState::Triangle;

	bool bFlatColors = false;
	if (!RSP.LLE && (gSP.geometryMode & G_LIGHTING) == 0) {
		bFlatColors = (gSP.geometryMode & G_SHADE) == 0;
		bFlatColors |= (gSP.geometryMode & G_SHADING_SMOOTH) == 0;
	}
	m_bFlatColors = bFlatColors;

	if ((m_modifyVertices & MODIFY_XY) != 0)
		_updateScreenCoordsViewport();
	m_modifyVertices = 0;
}

bool GraphicsDrawer::_canDraw() const
{
	return config.frameBufferEmulation.enable == 0 || frameBufferList().getCurrent() != nullptr;
}

void GraphicsDrawer::drawTriangles()
{
	if (triangles.num == 0 || !_canDraw()) {
		triangles.num = 0;
		triangles.maxElement = 0;
		return;
	}

	_prepareDrawTriangle();

	Context::DrawTriangleParameters triParams;
	triParams.mode = drawmode::TRIANGLES;
	triParams.flatColors = m_bFlatColors;
	triParams.elementsType = datatype::UNSIGNED_BYTE;
	triParams.verticesCount = static_cast<u32>(triangles.maxElement) + 1;
	triParams.elementsCount = triangles.num;
	triParams.vertices = triangles.vertices.data();
	triParams.elements = triangles.elements.data();
	triParams.combiner = currentCombiner();
	gfxContext.drawTriangles(triParams);

	if (config.frameBufferEmulation.enable != 0) {
		const f32 maxY = gli_renderTriangles(triangles.vertices.data(), triangles.elements.data(), triangles.num);
		frameBufferList().setBufferChanged(maxY);
		if (config.frameBufferEmulation.copyDepthToRDRAM == cdSoftwareRender &&
			gDP.otherMode.depthUpdate != 0) {
			FrameBuffer * pCurrentDepthBuffer = frameBufferList().findBuffer(gDP.depthImageAddress);
			if (pCurrentDepthBuffer != nullptr)
				pCurrentDepthBuffer->setDirty();
		}
	}

	triangles.num = 0;
	triangles.maxElement = 0;
}

void GraphicsDrawer::drawScreenSpaceTriangle(u32 _numVtx, graphics::DrawModeParam _mode)
{
	if (_numVtx == 0 || !_canDraw())
		return;

	// Immersive mode
	gli_gSPImmersiveScreenTriangles(m_dmaVertices.data(), _numVtx);

	f32 maxY = 0;
	for (u32 i = 0; i < _numVtx; ++i) {
		SPVertex & vtx = m_dmaVertices[i];
		vtx.modify = MODIFY_ALL;
		maxY = std::max(maxY, vtx.y);
	}
	m_modifyVertices = MODIFY_ALL;

	gSP.changed &= ~CHANGED_GEOMETRYMODE; // Don't update cull mode
	_prepareDrawTriangle();
	gfxContext.enable(enable::CULL_FACE, false);

	Context::DrawTriangleParameters triParams;
	triParams.mode = _mode;
	triParams.flatColors = m_bFlatColors;
	triParams.verticesCount = _numVtx;
	triParams.vertices = m_dmaVertices.data();
	triParams.combiner = currentCombiner();
	gfxContext.drawTriangles(triParams);
	m_dmaVerticesNum = 0;

	frameBufferList().setBufferChanged(maxY);
	gSP.changed |= CHANGED_GEOMETRYMODE;
}

void GraphicsDrawer::drawDMATriangles(u32 _numVtx)
{
	if (_numVtx == 0 || !_canDraw())
		return;
	_prepareDrawTriangle();


	Context::DrawTriangleParameters triParams;
	triParams.mode = drawmode::TRIANGLES;
	triParams.flatColors = m_bFlatColors;
	triParams.verticesCount = _numVtx;
	triParams.vertices = m_dmaVertices.data();
	triParams.combiner = currentCombiner();
	gfxContext.drawTriangles(triParams);
	m_dmaVerticesNum = 0;

	if (config.frameBufferEmulation.enable != 0) {
		const f32 maxY = gli_renderTriangles(m_dmaVertices.data(), nullptr, _numVtx);
		frameBufferList().setBufferChanged(maxY);
		if (config.frameBufferEmulation.copyDepthToRDRAM == cdSoftwareRender &&
			gDP.otherMode.depthUpdate != 0) {
			FrameBuffer * pCurrentDepthBuffer = frameBufferList().findBuffer(gDP.depthImageAddress);
			if (pCurrentDepthBuffer != nullptr)
				pCurrentDepthBuffer->setDirty();
		}
	}
}

void GraphicsDrawer::drawLine(u32 _v0, u32 _v1, float _width, u32 _flag)
{
	gli_texrect_draw();

	if (!_canDraw())
		return;

	f32 lineWidth = _width;
	if (config.frameBufferEmulation.nativeResFactor == 0)
		lineWidth *= dwnd().getScaleX();
	else
		lineWidth *= config.frameBufferEmulation.nativeResFactor;
	if (lineWidth > m_maxLineWidth) {
		gli_drawer_thick_line(getDMAVerticesData(), _v0, _v1, _width * 0.5f, _flag);
		drawScreenSpaceTriangle(4);
		return;
	}

	if ((triangles.vertices[_v0].modify & MODIFY_XY) != 0)
		gSP.changed &= ~CHANGED_VIEWPORT;
	if (gSP.changed || gDP.changed)
		_updateStates(DrawingState::Line);

	m_drawingState = DrawingState::Line;

	if ((triangles.vertices[_v0].modify & MODIFY_XY) != 0)
		_updateScreenCoordsViewport();

	SPVertex vertexBuf[2] = { triangles.vertices[_v0], triangles.vertices[_v1] };
	gfxContext.drawLine(lineWidth, vertexBuf);
}

void GraphicsDrawer::drawRect(int _ulx, int _uly, int _lrx, int _lry)
{
	gli_texrect_draw();

	if (!_canDraw())
		return;

	gSP.changed &= ~CHANGED_GEOMETRYMODE; // Don't update cull mode
	gSP.changed &= ~CHANGED_VIEWPORT; // Don't update viewport
	if (gSP.changed || gDP.changed)
		_updateStates(DrawingState::Rect);

	m_drawingState = DrawingState::Rect;

	_updateScreenCoordsViewport();

	gfxContext.enable(enable::CULL_FACE, false);

	f32 scale[2];
	calcCoordsScales(frameBufferList().getCurrent(), scale[0], scale[1]);
	const s32 rect[4] = { _ulx, _uly, _lrx, _lry };
	DisplayWindow & wnd = dwnd();
	if (!gli_drawer_fill_rect(m_rect, rect, scale, wnd.isAdjustScreen(), wnd.getAdjustScale())) {
		gSP.changed |= CHANGED_GEOMETRYMODE | CHANGED_VIEWPORT;
		dropRenderState();
		return;
	}

	Context::DrawRectParameters rectParams;
	rectParams.mode = drawmode::TRIANGLE_STRIP;
	rectParams.texrect = false;
	rectParams.verticesCount = 4;
	rectParams.vertices = m_rect;
	rectParams.combiner = currentCombiner();
	gfxContext.drawRects(rectParams);
	gSP.changed |= CHANGED_GEOMETRYMODE | CHANGED_VIEWPORT;
}

static
bool texturedRectShadowMap(const GraphicsDrawer::TexturedRectParams &)
{
	FrameBuffer * pCurrentBuffer = frameBufferList().getCurrent();
	if (pCurrentBuffer != nullptr) {
		if (gDP.textureImage.size == 2 && gDP.textureImage.address >= gDP.depthImageAddress &&
			gDP.textureImage.address < (gDP.depthImageAddress + gDP.colorImage.width*gDP.colorImage.width * 6 / 4)) {

			if (!Context::IntegerTextures)
				return true;

			pCurrentBuffer->m_pDepthBuffer->activateDepthBufferTexture(pCurrentBuffer);
			CombinerInfo::get().setDepthFogCombiner();
			// DepthFogCombiner does not support shader blending.
			_applyBlendMode(true);
			return false;
		}
	}
	return false;
}

u32 rectDepthBufferCopyFrame = 0xFFFFFFFF;
static
bool texturedRectDepthBufferCopy(const GraphicsDrawer::TexturedRectParams & _params)
{
	// Copy one line from depth buffer into auxiliary color buffer with height = 1.
	// Data from depth buffer loaded into TMEM and then rendered to RDRAM by texrect.
	// Works only with depth buffer emulation enabled.
	// Load of arbitrary data to that area causes weird camera rotation in CBFD.
	const gDPTile * pTile = gSP.textureTile[0];
	if (pTile->loadType == LOADTYPE_BLOCK && gDP.textureImage.size == 2 &&
		gDP.textureImage.address >= gDP.depthImageAddress &&
		gDP.textureImage.address < (gDP.depthImageAddress + gDP.colorImage.width*gDP.scissor.lry*2)) {
		if (config.frameBufferEmulation.copyDepthToRDRAM == cdDisable)
			return true;
		FrameBuffer * pBuffer = frameBufferList().getCurrent();
		if (pBuffer == nullptr)
			return true;
		pBuffer->m_cleared = true;
		if (config.frameBufferEmulation.copyDepthToRDRAM == cdCopyFromVRam) {
			if (rectDepthBufferCopyFrame != dwnd().getBuffersSwapCount()) {
				rectDepthBufferCopyFrame = dwnd().getBuffersSwapCount();
				if (!FrameBuffer_CopyDepthBuffer(gDP.depthImageAddress))
					return true;
			}
			gli_RDP_RepeatLastLoadBlock();
		}

		const u32 width = (u32)(_params.lrx - _params.ulx);
		const u32 ulx = (u32)_params.ulx;
		u16 * pSrc = ((u16*)TMEM) + _params.s/32;
		u16 *pDst = (u16*)(RDRAM + gDP.colorImage.address);
		for (u32 x = 0; x < width; ++x)
			pDst[(ulx + x) ^ 1] = swapword(pSrc[x]);

		return true;
	}
	return false;
}

static
bool texturedRectCopyToItself(const GraphicsDrawer::TexturedRectParams & _params)
{
	FrameBuffer * pCurrent = frameBufferList().getCurrent();
	if (pCurrent != nullptr && pCurrent->m_size == G_IM_SIZ_8b && gSP.textureTile[0]->frameBufferAddress == pCurrent->m_startAddress)
		return true;
	return texturedRectDepthBufferCopy(_params);
}

static
bool texturedRectBGCopy(const GraphicsDrawer::TexturedRectParams & _params)
{
	if (gDP.colorImage.size > G_IM_SIZ_8b)
		return false;

	float flry = _params.lry;
	if (flry > gDP.scissor.lry)
		flry = gDP.scissor.lry;

	const u32 width = (u32)(_params.lrx - _params.ulx);
	const u32 tex_width = gSP.textureTile[0]->line << 3;
	const u32 uly = (u32)_params.uly;
	const u32 lry = (u32)flry;

	u8 * texaddr = RDRAM + gDP.loadInfo[gSP.textureTile[0]->tmem].texAddress + tex_width*_params.t/32 + _params.s/32;
	u8 * fbaddr = RDRAM + gDP.colorImage.address + (u32)_params.ulx;

	for (u32 y = uly; y < lry; ++y) {
		u8 *src = texaddr + (y - uly) * tex_width;
		u8 *dst = fbaddr + y * gDP.colorImage.width;
		memcpy(dst, src, width);
	}
	frameBufferList().removeBuffer(gDP.colorImage.address);
	return true;
}

static
bool texturedRectPaletteMod(const GraphicsDrawer::TexturedRectParams & _params)
{
	if (gDP.textureImage.address == 0x400) {
		// Paper Mario uses complex set of actions to prepare darkness texture.
		// It includes manipulations with texture formats and drawing buffer into itsels.
		// All that stuff is hardly possible to reproduce with GL, so I just use dirty hacks to emualte it.

		if (gDP.colorImage.address == 0x400 && gDP.colorImage.width == 64) {
			memcpy(RDRAM + 0x400, RDRAM + 0x14d500, 4096);
			return true;
		}

		if (gDP.textureImage.width == 64) {
			gDPTile & curTile = gDP.tiles[0];
			curTile.frameBufferAddress = 0;
			curTile.textureMode = TEXTUREMODE_NORMAL;
			textureCache().update(0);
			currentCombiner()->update(false);
		}
		return false;
	}

	// Modify palette for Paper Mario "2D lighting" effect
	if (gDP.scissor.lrx != 16 || gDP.scissor.lry != 1 || _params.lrx != 16 || _params.lry != 1)
		return false;
	u8 envr = (u8)(gDP.envColor.r * 31.0f);
	u8 envg = (u8)(gDP.envColor.g * 31.0f);
	u8 envb = (u8)(gDP.envColor.b * 31.0f);
	u16 env16 = (u16)((envr << 11) | (envg << 6) | (envb << 1) | 1);
	u8 prmr = (u8)(gDP.primColor.r * 31.0f);
	u8 prmg = (u8)(gDP.primColor.g * 31.0f);
	u8 prmb = (u8)(gDP.primColor.b * 31.0f);
	u16 prim16 = (u16)((prmr << 11) | (prmg << 6) | (prmb << 1) | 1);
	u16 * src = (u16*)&TMEM[256];
	u16 * dst = (u16*)(RDRAM + gDP.colorImage.address);
	for (u32 i = 0; i < 16; ++i)
		dst[i ^ 1] = (src[i << 2] & 0x100) ? prim16 : env16;
	return true;
}

// Special processing of textured rect.
// Return true if actuial rendering is not necessary
static bool(*texturedRectSpecial)(const GraphicsDrawer::TexturedRectParams & _params) = nullptr;

void GraphicsDrawer::drawTexturedRect(const TexturedRectParams & _params)
{
	gSP.changed &= ~CHANGED_GEOMETRYMODE; // Don't update cull mode
	m_drawingState = DrawingState::TexRect;

	if (gli_texrect_can_continue()) {
		CombinerInfo & cmbInfo = CombinerInfo::get();
		cmbInfo.setPolygonMode(DrawingState::TexRect);
		cmbInfo.update();
		_updateTextures();
		cmbInfo.updateParameters();
	} else {
		if (!gli_texrect_is_empty())
			gli_texrect_draw();
		gSP.changed &= ~CHANGED_GEOMETRYMODE; // Don't update cull mode
		gSP.changed &= ~CHANGED_VIEWPORT; // Don't update viewport
		if (_params.texrectCmd && (gSP.changed | gDP.changed) != 0)
			_updateStates(DrawingState::TexRect);
		gfxContext.enable(enable::CULL_FACE, false);

		if (_params.texrectCmd && texturedRectSpecial != nullptr && texturedRectSpecial(_params)) {
			gSP.changed |= CHANGED_GEOMETRYMODE | CHANGED_VIEWPORT;
			return;
		}

		if (!_canDraw())
			return;
	}

	CombinerProgram * pCurrentCombiner = currentCombiner();
	const FrameBuffer * pCurrentBuffer = _params.pBuffer;
	DisplayWindow & wnd = dwnd();
	TextureCache & cache = textureCache();
	const bool bUseBilinear = gDP.otherMode.textureFilter != 0;
	int immersiveFullScreen = 0;
	const bool immersiveRect = _params.texrectCmd &&
		gli_drawer_immersive_rect(&_params.ulx, &immersiveFullScreen);
	const bool bUseTexrectDrawer = !immersiveRect && (m_bBGMode || ((config.graphics2D.enableNativeResTexrects != 0)
		&& bUseBilinear
		&& pCurrentCombiner->usesTexture()
		&& (pCurrentBuffer == nullptr || !pCurrentBuffer->m_cfb)
		&& (cache.current[0] != nullptr)
		//		&& (cache.current[0] == nullptr || cache.current[0]->format == G_IM_FMT_RGBA || cache.current[0]->format == G_IM_FMT_CI)
		&& ((cache.current[0]->frameBufferTexture == CachedTexture::fbNone && !cache.current[0]->bHDTexture))
		&& (cache.current[1] == nullptr || (cache.current[1]->frameBufferTexture == CachedTexture::fbNone && !cache.current[1]->bHDTexture))));

	f32 scale[2];
	calcCoordsScales(pCurrentBuffer, scale[0], scale[1]);

	/* the cached textures the combiner samples, as the C side reads them */
	gli_texrect_tex texs[2];
	const gli_texrect_tex * tex[2] = { nullptr, nullptr };
	for (u32 t = 0; t < 2; ++t) {
		const CachedTexture * pTex = cache.current[t];
		if (!pCurrentCombiner->usesTile(t) || pTex == nullptr || gSP.textureTile[t] == nullptr)
			continue;
		texs[t].offset[0] = pTex->offsetS;
		texs[t].offset[1] = pTex->offsetT;
		texs[t].scale[0] = pTex->scaleS;
		texs[t].scale[1] = pTex->scaleT;
		texs[t].size[0] = pTex->width;
		texs[t].size[1] = pTex->height;
		texs[t].fb = (u8)pTex->frameBufferTexture;
		texs[t].maskS = pTex->maskS;
		texs[t].mirrorS = pTex->mirrorS;
		texs[t].mirrorT = pTex->mirrorT;
		tex[t] = &texs[t];
	}

	const u32 clamp = gli_drawer_texrect_vertices(m_rect, &_params, tex, scale,
		wnd.isAdjustScreen(), wnd.getAdjustScale());
	for (u32 t = 0; t < 2; ++t) {
		const u32 c = (clamp >> (2 * t)) & 3;
		if (c == 0)
			continue;
		Context::TexParameters texParams;
		if (c & 1)
			texParams.wrapS = textureParameters::WRAP_CLAMP_TO_EDGE;
		if (c & 2)
			texParams.wrapT = textureParameters::WRAP_CLAMP_TO_EDGE;
		texParams.handle = cache.current[t]->name;
		texParams.target = textureTarget::TEXTURE_2D;
		texParams.textureUnitIndex = textureIndices::Tex[t];
		gfxContext.setTextureParameters(texParams);
	}

	if (gDP.otherMode.cycleType == G_CYC_COPY && cache.current[0]->frameBufferTexture != CachedTexture::fbMultiSample) {
		Context::TexParameters texParams;
		texParams.handle = cache.current[0]->name;
		texParams.target = textureTarget::TEXTURE_2D;
		texParams.textureUnitIndex = textureIndices::Tex[0];
		texParams.minFilter = textureParameters::FILTER_NEAREST;
		texParams.magFilter = textureParameters::FILTER_NEAREST;
		gfxContext.setTextureParameters(texParams);
	}

	if (bUseTexrectDrawer) {
		if (m_bBGMode) {
			gli_texrect_add_background_rect();
			return;
		}
		if (gli_texrect_add_rect())
			return;
	}

	if (immersiveRect)
		gli_drawer_immersive_clip(m_rect, immersiveFullScreen, scale);

	_updateScreenCoordsViewport(_params.pBuffer);
	Context::DrawRectParameters rectParams;
	rectParams.mode = drawmode::TRIANGLE_STRIP;
	rectParams.verticesCount = 4;
	rectParams.vertices = m_rect;
	rectParams.combiner = currentCombiner();
	gfxContext.drawRects(rectParams);

	gSP.changed |= CHANGED_GEOMETRYMODE | CHANGED_VIEWPORT;
}

void GraphicsDrawer::correctTexturedRectParams(TexturedRectParams & _params)
{
	gli_drawer_texrect_correct(&_params.ulx, m_texrectRect);
}

void GraphicsDrawer::clearDepthBuffer()
{
	if (!_canDraw())
		return;

	gli_depth_clear();

	_updateDepthUpdate();
}

void GraphicsDrawer::clearColorBuffer(float *_pColor)
{
	if (_pColor != nullptr)
		gfxContext.clearColorBuffer(_pColor[0], _pColor[1], _pColor[2], _pColor[3]);
	else
		gfxContext.clearColorBuffer(0.0f, 0.0f, 0.0f, 0.0f);
}

void GraphicsDrawer::copyTexturedRect(const CopyRectParams & _params)
{
	m_drawingState = DrawingState::TexRect;

	const float scaleX = 1.0f / _params.dstWidth;
	const float scaleY = 1.0f / _params.dstHeight;
	const float Z = 0.0f;
	const float W = 1.0f;
	float X0 = _params.dstX0 * (2.0f * scaleX) - 1.0f;
	float Y0 = _params.dstY0 * (2.0f * scaleY) - 1.0f;
	float X1 = _params.dstX1 * (2.0f * scaleX) - 1.0f;
	float Y1 = _params.dstY1 * (2.0f * scaleY) - 1.0f;
	if (_params.invertX) {
		X0 = -X0;
		X1 = -X1;
	}
	if (_params.invertY) {
		Y0 = -Y0;
		Y1 = -Y1;
	}

	m_rect[0].x = X0;
	m_rect[0].y = Y0;
	m_rect[0].z = Z;
	m_rect[0].w = W;
	m_rect[1].x = X1;
	m_rect[1].y = Y0;
	m_rect[1].z = Z;
	m_rect[1].w = W;
	m_rect[2].x = X0;
	m_rect[2].y = Y1;
	m_rect[2].z = Z;
	m_rect[2].w = W;
	m_rect[3].x = X1;
	m_rect[3].y = Y1;
	m_rect[3].z = Z;
	m_rect[3].w = W;

	const float scaleS = 1.0f / _params.srcWidth;
	const float scaleT = 1.0f / _params.srcHeight;

	const float S0 = _params.srcX0 * scaleS;
	const float S1 = _params.srcX1 * scaleS;
	const float T0 = _params.srcY0 * scaleT;
	const float T1 = _params.srcY1 * scaleT;

	m_rect[0].s0 = S0;
	m_rect[0].t0 = T0;
	m_rect[1].s0 = S1;
	m_rect[1].t0 = T0;
	m_rect[2].s0 = S0;
	m_rect[2].t0 = T1;
	m_rect[3].s0 = S1;
	m_rect[3].t0 = T1;

	for (u32 i = 0; i < 2; ++i) {
		CachedTexture * tex = _params.tex[i];
		if (tex == nullptr)
			continue;

		Context::TexParameters texParams;
		texParams.handle = tex->name;
		texParams.textureUnitIndex = textureIndices::Tex[i];
		if (tex->frameBufferTexture == CachedTexture::fbMultiSample)
			texParams.target = textureTarget::TEXTURE_2D_MULTISAMPLE;
		else {
			texParams.target = textureTarget::TEXTURE_2D;
			texParams.minFilter = _params.filter;
			texParams.magFilter = _params.filter;
			texParams.wrapS = textureParameters::WRAP_CLAMP_TO_EDGE;
			texParams.wrapT = textureParameters::WRAP_CLAMP_TO_EDGE;
		}
		gfxContext.setTextureParameters(texParams);
	}

	gfxContext.setViewport(0, 0, _params.dstWidth, _params.dstHeight);
	gfxContext.enable(enable::CULL_FACE, false);
	gfxContext.enable(enable::BLEND, false);
	gfxContext.enable(enable::DEPTH_TEST, false);
	gfxContext.enableDepthWrite(false);

	Context::DrawRectParameters rectParams;
	rectParams.mode = drawmode::TRIANGLE_STRIP;
	rectParams.verticesCount = 4;
	rectParams.vertices = m_rect;
	rectParams.combiner = _params.combiner;
	_params.combiner->activate();
	gfxContext.enable(enable::SCISSOR_TEST, false);
	gfxContext.drawRects(rectParams);
	gfxContext.enable(enable::SCISSOR_TEST, true);

	gSP.changed |= CHANGED_GEOMETRYMODE | CHANGED_VIEWPORT;
	gDP.changed |= CHANGED_RENDERMODE | CHANGED_TILE | CHANGED_COMBINE;
}

void GraphicsDrawer::blitOrCopyTexturedRect(const BlitOrCopyRectParams & _params)
{
	Context::BlitFramebuffersParams blitParams;
	blitParams.readBuffer = _params.readBuffer;
	blitParams.drawBuffer = _params.drawBuffer;
	blitParams.srcX0 = _params.srcX0;
	blitParams.srcX1 = _params.srcX1;
	blitParams.dstX0 = _params.dstX0;
	blitParams.dstX1 = _params.dstX1;
	blitParams.srcY0 = _params.srcY0;
	blitParams.srcY1 = _params.srcY1;
	blitParams.dstY0 = _params.dstY0;
	blitParams.dstY1 = _params.dstY1;
	blitParams.mask = _params.mask;
	blitParams.filter = _params.filter;
	if (_params.invertX) {
		std::swap(blitParams.srcX0, blitParams.srcX1);
	}
	if (_params.invertY) {
		std::swap(blitParams.srcY0, blitParams.srcY1);
	}

	if (gfxContext.blitFramebuffers(blitParams))
		return;

	gfxContext.bindFramebuffer(bufferTarget::READ_FRAMEBUFFER, _params.readBuffer);
	gfxContext.bindFramebuffer(bufferTarget::DRAW_FRAMEBUFFER, _params.drawBuffer);
	copyTexturedRect(_params);
}

void GraphicsDrawer::_initStates()
{
	gfxContext.enable(enable::CULL_FACE, false);
	gfxContext.enable(enable::SCISSOR_TEST, true);
	gfxContext.enableDepthWrite(false);
	gfxContext.setDepthCompare(compare::ALWAYS);

	if (config.frameBufferEmulation.N64DepthCompare != 0) {
		gfxContext.enable(enable::DEPTH_TEST, false);
		gfxContext.enable(enable::POLYGON_OFFSET_FILL, false);
	}
	else {
		gfxContext.enable(enable::DEPTH_TEST, true);
#if defined(OS_ANDROID) || defined(OS_IOS)
		if (config.generalEmulation.forcePolygonOffset != 0)
			gfxContext.setPolygonOffset(config.generalEmulation.polygonOffsetFactor, config.generalEmulation.polygonOffsetUnits);
		else
#endif
			gfxContext.setPolygonOffset(-3.0f, -3.0f);
	}

	DisplayWindow & wnd = DisplayWindow::get();
	gfxContext.setViewport(0, wnd.getHeightOffset(), wnd.getScreenWidth(), wnd.getScreenHeight());

	gfxContext.clearColorBuffer(0.0f, 0.0f, 0.0f, 0.0f);

	srand((unsigned int)time(nullptr));

	wnd.swapBuffers();
}

void GraphicsDrawer::_setSpecialTexrect() const
{
	std::string name(RSP.romname);
	std::transform(name.begin(), name.end(), name.begin(), ::toupper);
#define FOUND(romname) name.find(romname) != std::string::npos

	if (FOUND("BEETLE") || FOUND("HSV") || FOUND("DUCK DODGERS") || FOUND("DAFFY DUCK"))
		texturedRectSpecial = texturedRectShadowMap;
	else if (FOUND("PERFECT DARK") || FOUND("TUROK_DINOSAUR_HUNTE"))
		texturedRectSpecial = texturedRectDepthBufferCopy; // See comments to that function!
	else if (FOUND("CONKER BFD"))
		texturedRectSpecial = texturedRectCopyToItself;
	else if (FOUND("YOSHI STORY"))
		texturedRectSpecial = texturedRectBGCopy;
	else if (FOUND("PAPER MARIO") || FOUND("MARIO STORY"))
		texturedRectSpecial = texturedRectPaletteMod;
	else
		texturedRectSpecial = nullptr;
}

void GraphicsDrawer::_initData()
{
	/* the microcodes fill this directly: sized here, not mid-game */
	m_dmaVertices.resize(4096);

	_initStates();
	_setSpecialTexrect();

	textureCache().init();
	gli_depth_list_init();
	FrameBuffer_Init();
	Combiner_Init();
	gli_TFH_init();
	gli_pp_init();
	gli_zlut_init();
	gli_noise_init();
	gli_palette_init();
	gli_fbinfo_reset();
	gli_texrect_init();
	m_drawingState = DrawingState::Non;
	m_maxLineWidth = gfxContext.getMaxLineWidth();

	gSP.changed = gDP.changed = 0xFFFFFFFF;

	memset(triangles.vertices.data(), 0, triangles.vertices.size() * sizeof(SPVertex));
	triangles.elements.fill(0);
	for (auto vtx : triangles.vertices)
		vtx.w = 1.0f;
	triangles.num = 0;
	m_dmaVerticesNum = 0;
}

void GraphicsDrawer::_destroyData()
{
	m_drawingState = DrawingState::Non;
	gli_texrect_destroy();
	gli_palette_destroy();
	gli_zlut_destroy();
	gli_noise_destroy();
	gli_pp_destroy();
	if (gli_TFH_options_changed())
		gli_TFH_shutdown();
	Combiner_Destroy();
	FrameBuffer_Destroy();
	gli_depth_list_destroy();
	textureCache().destroy();
}
