#include <assert.h>
#include "Combiner.h"
#include "FrameBuffer.h"
#include "DepthBuffer.h"
#include "SoftwareRender.h"
#include "VI.h"
#include "Config.h"
#include "gli_textures.h"
#include <Graphics/Context.h>
#include <Graphics/Parameters.h>
#include "DisplayWindow.h"
#include "gli_framebuffer.h"

using namespace graphics;

DepthBuffer::DepthBuffer()
{
	m_copyFBO = gfxContext.createFramebuffer();
}

DepthBuffer::~DepthBuffer()
{
	gfxContext.deleteFramebuffer(m_depthRenderbuffer);
	gfxContext.deleteFramebuffer(m_copyFBO);
	gfxContext.deleteFramebuffer(m_ZTextureClearFBO);
	gfxContext.deleteFramebuffer(m_DeltaZTextureClearFBO);

	textureCache().removeFrameBufferTexture(m_pDepthImageZTexture);
	textureCache().removeFrameBufferTexture(m_pDepthImageDeltaZTexture);
	textureCache().removeFrameBufferTexture(m_pDepthBufferTexture);
	textureCache().removeFrameBufferTexture(m_pResolveDepthBufferTexture);
	textureCache().removeFrameBufferTexture(m_pDepthBufferCopyTexture);
}

void DepthBuffer::_initDepthImageTexture(FrameBuffer * _pBuffer, CachedTexture& _cachedTexture, graphics::ObjectHandle & _clearFBO)
{
	const FramebufferTextureFormats & fbTexFormat = gfxContext.getFramebufferTextureFormats();

	_cachedTexture.width = (u32)(_pBuffer->m_pTexture->width);
	_cachedTexture.height = (u32)(_pBuffer->m_pTexture->height);
	_cachedTexture.format = 0;
	_cachedTexture.size = 2;
	_cachedTexture.clampS = 1;
	_cachedTexture.clampT = 1;
	_cachedTexture.address = _pBuffer->m_startAddress;
	_cachedTexture.clampWidth = _pBuffer->m_width;
	_cachedTexture.clampHeight = _pBuffer->m_height;
	_cachedTexture.frameBufferTexture = CachedTexture::fbOneSample;
	_cachedTexture.maskS = 0;
	_cachedTexture.maskT = 0;
	_cachedTexture.mirrorS = 0;
	_cachedTexture.mirrorT = 0;
	_cachedTexture.textureBytes = _cachedTexture.width * _cachedTexture.height * fbTexFormat.depthImageFormatBytes;

	{
		Context::InitTextureParams params;
		params.handle = _cachedTexture.name;
		params.width = _cachedTexture.width;
		params.height = _cachedTexture.height;
		params.internalFormat = fbTexFormat.depthImageInternalFormat;
		params.format = fbTexFormat.depthImageFormat;
		params.dataType = fbTexFormat.depthImageType;
		gfxContext.init2DTexture(params);
	}
	{
		Context::TexParameters params;
		params.handle = _cachedTexture.name;
		params.target = textureTarget::TEXTURE_2D;
		params.textureUnitIndex = textureIndices::Tex[0];
		params.minFilter = textureParameters::FILTER_NEAREST;
		params.magFilter = textureParameters::FILTER_NEAREST;
		gfxContext.setTextureParameters(params);
	}
	{
		Context::FrameBufferRenderTarget targetParams;
		targetParams.bufferHandle = _clearFBO;
		targetParams.bufferTarget = bufferTarget::DRAW_FRAMEBUFFER;
		targetParams.attachment = bufferAttachment::COLOR_ATTACHMENT0;
		targetParams.textureHandle = _cachedTexture.name;
		targetParams.textureTarget = textureTarget::TEXTURE_2D;
		gfxContext.addFrameBufferRenderTarget(targetParams);
	}
}

void DepthBuffer::initDepthImageTexture(FrameBuffer * _pBuffer)
{
	if (config.frameBufferEmulation.N64DepthCompare == 0 || m_pDepthImageZTexture != nullptr)
		return;

	m_pDepthImageZTexture = textureCache().addFrameBufferTexture(textureTarget::TEXTURE_2D);
	m_ZTextureClearFBO = gfxContext.createFramebuffer();
	m_pDepthImageDeltaZTexture = textureCache().addFrameBufferTexture(textureTarget::TEXTURE_2D);
	m_DeltaZTextureClearFBO = gfxContext.createFramebuffer();

	_initDepthImageTexture(_pBuffer, *m_pDepthImageZTexture, m_ZTextureClearFBO);
	_initDepthImageTexture(_pBuffer, *m_pDepthImageDeltaZTexture, m_DeltaZTextureClearFBO);

	gli_depth_clear();
}

void DepthBuffer::_initDepthBufferTexture(FrameBuffer * _pBuffer, CachedTexture * _pTexture, bool _multisample)
{
	const FramebufferTextureFormats & fbTexFormat = gfxContext.getFramebufferTextureFormats();

	if (_pBuffer != nullptr) {
		_pTexture->width = (u32)(_pBuffer->m_pTexture->width);
		_pTexture->height = (u32)(_pBuffer->m_pTexture->height);
		_pTexture->address = _pBuffer->m_startAddress;
		_pTexture->clampWidth = _pBuffer->m_width;
		_pTexture->clampHeight = gli_VI_GetMaxBufferHeight(_pBuffer->m_width);
	} else {
		const u16 maxHeight = gli_VI_GetMaxBufferHeight(VI.width);
		if (config.frameBufferEmulation.nativeResFactor == 0) {
			_pTexture->width = dwnd().getWidth();
			_pTexture->height = (u16)(u32)(maxHeight * dwnd().getScaleX());
		} else {
			_pTexture->width = VI.width * config.frameBufferEmulation.nativeResFactor;
			_pTexture->height = maxHeight * config.frameBufferEmulation.nativeResFactor;
		}
		_pTexture->address = gDP.depthImageAddress;
		_pTexture->clampWidth = VI.width;
		_pTexture->clampHeight = maxHeight;
	}
	_pTexture->format = 0;
	_pTexture->size = 2;
	_pTexture->clampS = 1;
	_pTexture->clampT = 1;
	_pTexture->frameBufferTexture = CachedTexture::fbOneSample;
	_pTexture->maskS = 0;
	_pTexture->maskT = 0;
	_pTexture->mirrorS = 0;
	_pTexture->mirrorT = 0;
	_pTexture->textureBytes = _pTexture->width * _pTexture->height * fbTexFormat.depthFormatBytes;

	Context::InitTextureParams initParams;
	initParams.handle = _pTexture->name;
	initParams.msaaLevel = _multisample ? config.video.multisampling : 0U;
	initParams.width = _pTexture->width;
	initParams.height = _pTexture->height;
	initParams.internalFormat = fbTexFormat.depthInternalFormat;
	initParams.format = fbTexFormat.depthFormat;
	initParams.dataType = fbTexFormat.depthType;
	gfxContext.init2DTexture(initParams);

	if (!_multisample) {
		_pTexture->frameBufferTexture = CachedTexture::fbOneSample;
		Context::TexParameters texParams;
		texParams.handle = _pTexture->name;
		texParams.target = textureTarget::TEXTURE_2D;
		texParams.textureUnitIndex = textureIndices::Tex[0];
		texParams.minFilter = textureParameters::FILTER_NEAREST;
		texParams.magFilter = textureParameters::FILTER_NEAREST;
		gfxContext.setTextureParameters(texParams);
	} else {
		_pTexture->frameBufferTexture = CachedTexture::fbMultiSample;
	}
}

void DepthBuffer::_initDepthBufferRenderbuffer(FrameBuffer * _pBuffer)
{
	if (m_depthRenderbuffer.isNotNull())
		return;
	u32 height;
	if (_pBuffer != NULL) {
		m_depthRenderbufferWidth = (u32)(_pBuffer->m_pTexture->width);
		height = (u32)(_pBuffer->m_pTexture->height);
	} else {
		if (config.frameBufferEmulation.nativeResFactor == 0) {
			m_depthRenderbufferWidth = dwnd().getWidth();
			height = (u32)(gli_VI_GetMaxBufferHeight(VI.width) * dwnd().getScaleX());
		} else {
			m_depthRenderbufferWidth = VI.width * config.frameBufferEmulation.nativeResFactor;
			height = gli_VI_GetMaxBufferHeight(VI.width) * config.frameBufferEmulation.nativeResFactor;
		}
	}

	m_depthRenderbuffer = gfxContext.createRenderbuffer();
	Context::InitRenderbufferParams params;
	params.handle = m_depthRenderbuffer;
	params.target = textureTarget::RENDERBUFFER;
	params.format = gfxContext.getFramebufferTextureFormats().depthInternalFormat;
	params.width = m_depthRenderbufferWidth;
	params.height = height;
	gfxContext.initRenderbuffer(params);
}

void DepthBuffer::setDepthAttachment(ObjectHandle _fbo, BufferTargetParam _target)
{
	Context::FrameBufferRenderTarget params;
	params.attachment = bufferAttachment::DEPTH_ATTACHMENT;
	params.bufferHandle = _fbo;
	params.bufferTarget = _target;
	if (Context::DepthFramebufferTextures) {
		params.textureHandle = m_pDepthBufferTexture->name;
		params.textureTarget = config.video.multisampling != 0 ? textureTarget::TEXTURE_2D_MULTISAMPLE : textureTarget::TEXTURE_2D;
	} else {
		params.textureHandle = m_depthRenderbuffer;
		params.textureTarget = textureTarget::RENDERBUFFER;
	}
	gfxContext.addFrameBufferRenderTarget(params);

	m_copied = false;
	m_resolved = false;
}

void DepthBuffer::initDepthBufferTexture(FrameBuffer * _pBuffer)
{
	if (Context::DepthFramebufferTextures) {
		if (m_pDepthBufferTexture == nullptr) {
			m_pDepthBufferTexture = textureCache().addFrameBufferTexture(config.video.multisampling != 0 ?
				textureTarget::TEXTURE_2D_MULTISAMPLE : textureTarget::TEXTURE_2D);
			_initDepthBufferTexture(_pBuffer, m_pDepthBufferTexture, config.video.multisampling != 0);
		}
	} else {
		_initDepthBufferRenderbuffer(_pBuffer);
	}

	if (config.video.multisampling != 0 && m_pResolveDepthBufferTexture == nullptr) {
		m_pResolveDepthBufferTexture = textureCache().addFrameBufferTexture(textureTarget::TEXTURE_2D);
		_initDepthBufferTexture(_pBuffer, m_pResolveDepthBufferTexture, false);
	}
}

CachedTexture * DepthBuffer::resolveDepthBufferTexture(FrameBuffer * _pBuffer)
{
	if (config.video.multisampling == 0)
		return m_pDepthBufferTexture;

	if (m_resolved)
		return m_pResolveDepthBufferTexture;

	Context::FrameBufferRenderTarget targetParams;
	targetParams.attachment = bufferAttachment::DEPTH_ATTACHMENT;
	targetParams.bufferHandle = _pBuffer->m_resolveFBO;
	targetParams.bufferTarget = bufferTarget::DRAW_FRAMEBUFFER;
	targetParams.textureHandle = m_pResolveDepthBufferTexture->name;
	targetParams.textureTarget = textureTarget::TEXTURE_2D;
	gfxContext.addFrameBufferRenderTarget(targetParams);

	Context::BlitFramebuffersParams blitParams;
	blitParams.readBuffer = _pBuffer->m_FBO;
	blitParams.drawBuffer = _pBuffer->m_resolveFBO;
	blitParams.srcX0 = 0;
	blitParams.srcY0 = 0;
	blitParams.srcX1 = m_pDepthBufferTexture->width;
	blitParams.srcY1 = m_pDepthBufferTexture->height;
	blitParams.dstX0 = 0;
	blitParams.dstY0 = 0;
	blitParams.dstX1 = m_pResolveDepthBufferTexture->width;
	blitParams.dstY1 = m_pResolveDepthBufferTexture->height;
	blitParams.mask = blitMask::DEPTH_BUFFER;
	blitParams.filter = textureParameters::FILTER_NEAREST;

	gfxContext.blitFramebuffers(blitParams);

	gfxContext.bindFramebuffer(bufferTarget::READ_FRAMEBUFFER, ObjectHandle::defaultFramebuffer);
	gfxContext.bindFramebuffer(bufferTarget::DRAW_FRAMEBUFFER, _pBuffer->m_FBO);

	m_resolved = true;
	return m_pResolveDepthBufferTexture;
}

CachedTexture * DepthBuffer::copyDepthBufferTexture(FrameBuffer * _pBuffer)
{
	if (m_copied)
		return m_pDepthBufferCopyTexture;

	if (m_pDepthBufferCopyTexture == nullptr) {
		m_pDepthBufferCopyTexture = textureCache().addFrameBufferTexture(textureTarget::TEXTURE_2D);
		_initDepthBufferTexture(_pBuffer, m_pDepthBufferCopyTexture, false);
	}


	Context::FrameBufferRenderTarget targetParams;
	targetParams.bufferHandle = m_copyFBO;
	targetParams.bufferTarget = bufferTarget::DRAW_FRAMEBUFFER;
	targetParams.attachment = bufferAttachment::COLOR_ATTACHMENT0;
	targetParams.textureHandle = _pBuffer->m_pTexture->frameBufferTexture == CachedTexture::fbMultiSample ?
		_pBuffer->m_pResolveTexture->name :
		_pBuffer->m_pTexture->name;
	targetParams.textureTarget = textureTarget::TEXTURE_2D;

	gfxContext.addFrameBufferRenderTarget(targetParams);

	targetParams.attachment = bufferAttachment::DEPTH_ATTACHMENT;
	targetParams.textureHandle = m_pDepthBufferCopyTexture->name;

	gfxContext.addFrameBufferRenderTarget(targetParams);


	Context::BlitFramebuffersParams blitParams;
	blitParams.readBuffer = _pBuffer->m_FBO;
	blitParams.drawBuffer = m_copyFBO;
	blitParams.srcX0 = 0;
	blitParams.srcY0 = 0;
	blitParams.srcX1 = m_pDepthBufferTexture->width;
	blitParams.srcY1 = m_pDepthBufferTexture->height;
	blitParams.dstX0 = 0;
	blitParams.dstY0 = 0;
	blitParams.dstX1 = m_pDepthBufferTexture->width;
	blitParams.dstY1 = m_pDepthBufferTexture->height;
	blitParams.mask = blitMask::DEPTH_BUFFER;
	blitParams.filter = textureParameters::FILTER_NEAREST;

	gfxContext.blitFramebuffers(blitParams);

	gfxContext.bindFramebuffer(bufferTarget::READ_FRAMEBUFFER, ObjectHandle::defaultFramebuffer);
	gfxContext.bindFramebuffer(bufferTarget::DRAW_FRAMEBUFFER, _pBuffer->m_FBO);

	m_copied = true;
	return m_pDepthBufferCopyTexture;
}

void DepthBuffer::activateDepthBufferTexture(FrameBuffer * _pBuffer)
{
	textureCache().activateTexture(0, resolveDepthBufferTexture(_pBuffer));
	gfxContext.textureBarrier();
}

void DepthBuffer::bindDepthImageTexture(ObjectHandle _fbo)
{
	if (Context::ImageTextures) {
		Context::BindImageTextureParameters bindParams;
		bindParams.imageUnit = textureImageUnits::DepthZ;
		bindParams.texture = m_pDepthImageZTexture->name;
		bindParams.accessMode = textureImageAccessMode::READ_WRITE;
		bindParams.textureFormat = gfxContext.getFramebufferTextureFormats().depthImageInternalFormat;
		gfxContext.bindImageTexture(bindParams);

		bindParams.imageUnit = textureImageUnits::DepthDeltaZ;
		bindParams.texture = m_pDepthImageDeltaZTexture->name;
		gfxContext.bindImageTexture(bindParams);
	} else if (Context::FramebufferFetch) {
		Context::FrameBufferRenderTarget targetParams;
		targetParams.bufferHandle = _fbo;
		targetParams.bufferTarget = bufferTarget::DRAW_FRAMEBUFFER;
		targetParams.attachment = bufferAttachment::COLOR_ATTACHMENT1;
		targetParams.textureHandle = m_pDepthImageZTexture->name;
		targetParams.textureTarget = textureTarget::TEXTURE_2D;
		gfxContext.addFrameBufferRenderTarget(targetParams);

		targetParams.attachment = bufferAttachment::COLOR_ATTACHMENT2;
		targetParams.textureHandle = m_pDepthImageDeltaZTexture->name;
		gfxContext.addFrameBufferRenderTarget(targetParams);

		gfxContext.setDrawBuffers(3);
	}
}

void gli_depth_gpu_clear(void)
{
	if (config.frameBufferEmulation.enable == 0 || config.frameBufferEmulation.N64DepthCompare == 0) {
		gfxContext.clearDepthBuffer();
		return;
	}

	/* Clear depth image texture */
	FrameBuffer * pColorBuffer = frameBufferList().getCurrent();
	if (pColorBuffer == nullptr || pColorBuffer->m_pDepthBuffer == nullptr)
		return;
	DepthBuffer * pDepthBuffer = pColorBuffer->m_pDepthBuffer;

	gfxContext.bindFramebuffer(bufferTarget::DRAW_FRAMEBUFFER, pDepthBuffer->m_ZTextureClearFBO);
	gfxContext.clearColorBuffer(1.0f, 0.0f, 0.0f, 0.0f);

	gfxContext.bindFramebuffer(bufferTarget::DRAW_FRAMEBUFFER, pDepthBuffer->m_DeltaZTextureClearFBO);
	gfxContext.clearColorBuffer(1.0f, 0.0f, 0.0f, 0.0f);
	frameBufferList().setCurrentDrawBuffer();
}

struct DepthBuffer * gli_depth_new(u32 _address, FrameBuffer * _pBuffer)
{
	DepthBuffer * pBuffer = new DepthBuffer();
	pBuffer->m_address = _address;
	pBuffer->m_width = _pBuffer != nullptr ? _pBuffer->m_width : VI.width;
	pBuffer->initDepthBufferTexture(_pBuffer);
	return pBuffer;
}

void gli_depth_delete(DepthBuffer * _pBuffer)
{
	delete _pBuffer;
}

u32 gli_depth_address(const DepthBuffer * _pBuffer)
{
	return _pBuffer->m_address;
}

u32 gli_depth_width(const DepthBuffer * _pBuffer)
{
	return _pBuffer->m_width;
}

void gli_depth_mark_cleared(DepthBuffer * _pBuffer, bool _cleared)
{
	_pBuffer->m_cleared = _cleared;
}
