#include <assert.h>
#include <Graphics/Context.h>
#include <Graphics/Parameters.h>
#include <Graphics/ShaderProgram.h>
#include "DisplayWindow.h"
#include "GraphicsDrawer.h"
#include "Textures.h"
#include "FrameBuffer.h"
#include "Combiner.h"
#include "TexrectDrawer.h"

using namespace graphics;

static ObjectHandle l_FBO;
static CachedTexture * l_pTexture;
static TexrectDrawerShaderProgram * l_programTex;
static ShaderProgram * l_programClear;

/* The drawer's private state, for the texrect drawer alone */
struct TexrectGpu
{
	static RectVertex * rect() { return dwnd().getDrawer().m_rect; }
	static void restart() { dwnd().getDrawer()._updateStates(DrawingState::TexRect); gfxContext.enable(enable::CULL_FACE, false); }
	static void setBlend() { dwnd().getDrawer()._setBlendMode(); }
	static void depthCompare() { dwnd().getDrawer()._updateDepthCompare(); }
	static void screenViewport(const FrameBuffer * _p) { dwnd().getDrawer()._updateScreenCoordsViewport(_p); }
	static void updateScissor(FrameBuffer * _p) { dwnd().getDrawer().updateScissor(_p); }
};

void gli_texrect_gpu_init(void)
{
	const FramebufferTextureFormats & fbTexFormats = gfxContext.getFramebufferTextureFormats();

	l_FBO = gfxContext.createFramebuffer();

	l_pTexture = textureCache().addFrameBufferTexture(textureTarget::TEXTURE_2D);
	l_pTexture->format = G_IM_FMT_RGBA;
	l_pTexture->clampS = 1;
	l_pTexture->clampT = 1;
	l_pTexture->frameBufferTexture = CachedTexture::fbOneSample;
	l_pTexture->maskS = 0;
	l_pTexture->maskT = 0;
	l_pTexture->mirrorS = 0;
	l_pTexture->mirrorT = 0;
	l_pTexture->realWidth = 640;
	l_pTexture->realHeight = 580;
	l_pTexture->textureBytes = l_pTexture->realWidth * l_pTexture->realHeight * fbTexFormats.colorFormatBytes;

	Context::InitTextureParams initParams;
	initParams.handle = l_pTexture->name;
	initParams.textureUnitIndex = textureIndices::Tex[0];
	initParams.width = l_pTexture->realWidth;
	initParams.height = l_pTexture->realHeight;
	initParams.internalFormat = fbTexFormats.colorInternalFormat;
	initParams.format = fbTexFormats.colorFormat;
	initParams.dataType = fbTexFormats.colorType;
	gfxContext.init2DTexture(initParams);

	Context::TexParameters texParams;
	texParams.handle = l_pTexture->name;
	texParams.target = textureTarget::TEXTURE_2D;
	texParams.textureUnitIndex = textureIndices::Tex[0];
	texParams.minFilter = textureParameters::FILTER_LINEAR;
	texParams.magFilter = textureParameters::FILTER_LINEAR;
	gfxContext.setTextureParameters(texParams);

	Context::FrameBufferRenderTarget bufTarget;
	bufTarget.bufferHandle = l_FBO;
	bufTarget.bufferTarget = bufferTarget::DRAW_FRAMEBUFFER;
	bufTarget.attachment = bufferAttachment::COLOR_ATTACHMENT0;
	bufTarget.textureTarget = textureTarget::TEXTURE_2D;
	bufTarget.textureHandle = l_pTexture->name;
	gfxContext.addFrameBufferRenderTarget(bufTarget);

	/* check if everything is OK */
	assert(!gfxContext.isFramebufferError());
	gfxContext.bindFramebuffer(bufferTarget::DRAW_FRAMEBUFFER, ObjectHandle::defaultFramebuffer);

	l_programTex = gfxContext.createTexrectDrawerDrawShader();
	l_programClear = gfxContext.createTexrectDrawerClearShader();
	l_programTex->setTextureSize(l_pTexture->realWidth, l_pTexture->realHeight);
}

void gli_texrect_gpu_destroy(void)
{
	gfxContext.deleteFramebuffer(l_FBO);
	if (l_pTexture != nullptr) {
		textureCache().removeFrameBufferTexture(l_pTexture);
		l_pTexture = nullptr;
	}
	delete l_programTex;
	l_programTex = nullptr;
	delete l_programClear;
	l_programClear = nullptr;
}

RectVertex * gli_texrect_gpu_rect(void)
{
	return TexrectGpu::rect();
}

void gli_texrect_gpu_begin(u32 _width, u32 _height, s32 _sx, s32 _sy, s32 _sw, s32 _sh)
{
	CombinerInfo & cmbInfo = CombinerInfo::get();
	cmbInfo.update();
	cmbInfo.updateParameters();
	gfxContext.enableDepthWrite(false);
	gfxContext.enable(enable::DEPTH_TEST, false);
	gfxContext.enable(enable::BLEND, false);

	gfxContext.setViewport(0, 0, _width, _height);

	gfxContext.setScissor(_sx, _sy, _sw, _sh);

	gfxContext.bindFramebuffer(bufferTarget::DRAW_FRAMEBUFFER, l_FBO);
}

void gli_texrect_gpu_restart(void)
{
	TexrectGpu::restart();
}

void gli_texrect_gpu_draw_rect(RectVertex * _pRect)
{
	Context::DrawRectParameters rectParams;
	rectParams.mode = drawmode::TRIANGLE_STRIP;
	rectParams.verticesCount = 4;
	rectParams.vertices = _pRect;
	rectParams.combiner = currentCombiner();
	gfxContext.drawRects(rectParams);
}

void gli_texrect_gpu_set_blend(void)
{
	TexrectGpu::setBlend();
}

void gli_texrect_gpu_update_depth_compare(void)
{
	TexrectGpu::depthCompare();
}

void gli_texrect_gpu_scales(const FrameBuffer * _pBuffer, f32 * _x, f32 * _y)
{
	calcCoordsScales(_pBuffer, *_x, *_y);
}

void gli_texrect_gpu_begin_composite(const FrameBuffer * _pBuffer, int _alphaTest)
{
	TexrectGpu::screenViewport(_pBuffer);

	textureCache().activateTexture(0, l_pTexture);
	/* Disable filtering to avoid black outlines */
	Context::TexParameters texParams;
	texParams.handle = l_pTexture->name;
	texParams.target = textureTarget::TEXTURE_2D;
	texParams.textureUnitIndex = textureIndices::Tex[0];
	texParams.minFilter = textureParameters::FILTER_NEAREST;
	texParams.magFilter = textureParameters::FILTER_NEAREST;
	gfxContext.setTextureParameters(texParams);

	l_programTex->activate();
	l_programTex->setEnableAlphaTest(_alphaTest);
}

void gli_texrect_gpu_update_scissor(FrameBuffer * _pBuffer)
{
	TexrectGpu::updateScissor(_pBuffer);
}

void gli_texrect_gpu_composite(RectVertex * _rect)
{
	Context::DrawRectParameters rectParams;
	rectParams.mode = drawmode::TRIANGLE_STRIP;
	rectParams.verticesCount = 4;
	rectParams.vertices = _rect;
	rectParams.combiner = l_programTex;
	gfxContext.drawRects(rectParams);

	gfxContext.bindFramebuffer(bufferTarget::DRAW_FRAMEBUFFER, l_FBO);
	l_programClear->activate();
}

void gli_texrect_gpu_clear(RectVertex * _rect, u32 _width, u32 _height)
{
	Context::DrawRectParameters rectParams;

	gfxContext.setViewport(0, 0, _width, _height);

	gfxContext.enable(enable::BLEND, false);
	gfxContext.enable(enable::SCISSOR_TEST, false);
	rectParams.mode = drawmode::TRIANGLE_STRIP;
	rectParams.verticesCount = 4;
	rectParams.vertices = _rect;
	rectParams.combiner = l_programClear;
	gfxContext.drawRects(rectParams);
	gfxContext.enable(enable::SCISSOR_TEST, true);
}
