#include <assert.h>

#include "N64.h"
#include "StereoFrames.h"
#include "FrameBuffer.h"
#include "Config.h"

#include <Graphics/Context.h>
#include <Graphics/Parameters.h>

using namespace graphics;

StereoFrames & StereoFrames::get()
{
	static StereoFrames frames;
	return frames;
}

std::unique_ptr<FrameBuffer> StereoFrames::_createBuffer(const FrameBuffer * _pMainBuffer)
{
	std::unique_ptr<FrameBuffer> leftEye(new FrameBuffer());
	leftEye->m_startAddress = _pMainBuffer->m_startAddress;
	leftEye->m_width = _pMainBuffer->m_width;
	leftEye->m_height = _pMainBuffer->m_height;
	leftEye->m_scale = _pMainBuffer->m_scale;

	const CachedTexture * pMainTexture = _pMainBuffer->m_pTexture;
	CachedTexture * pTexture = leftEye->m_pTexture;
	pTexture->format = G_IM_FMT_RGBA;
	pTexture->clampS = 1;
	pTexture->clampT = 1;
	pTexture->frameBufferTexture = CachedTexture::fbOneSample;
	pTexture->maskS = 0;
	pTexture->maskT = 0;
	pTexture->mirrorS = 0;
	pTexture->mirrorT = 0;
	pTexture->width = pMainTexture->width;
	pTexture->height = pMainTexture->height;
	pTexture->textureBytes = pTexture->width * pTexture->height * 4;

	Context::InitTextureParams initParams;
	initParams.handle = pTexture->name;
	initParams.width = pTexture->width;
	initParams.height = pTexture->height;
	initParams.internalFormat = gfxContext.convertInternalTextureFormat(u32(internalcolorFormat::RGBA8));
	initParams.format = colorFormat::RGBA;
	initParams.dataType = datatype::UNSIGNED_BYTE;
	gfxContext.init2DTexture(initParams);

	Context::TexParameters setParams;
	setParams.handle = pTexture->name;
	setParams.target = textureTarget::TEXTURE_2D;
	setParams.minFilter = textureParameters::FILTER_LINEAR;
	setParams.magFilter = textureParameters::FILTER_LINEAR;
	gfxContext.setTextureParameters(setParams);

	Context::FrameBufferRenderTarget bufTarget;
	bufTarget.bufferHandle = leftEye->m_FBO;
	bufTarget.bufferTarget = bufferTarget::DRAW_FRAMEBUFFER;
	bufTarget.attachment = bufferAttachment::COLOR_ATTACHMENT0;
	bufTarget.textureTarget = textureTarget::TEXTURE_2D;
	bufTarget.textureHandle = pTexture->name;
	gfxContext.addFrameBufferRenderTarget(bufTarget);
	assert(!gfxContext.isFramebufferError());
	return leftEye;
}

void StereoFrames::captureLeftEye(FrameBuffer * _pBuffer)
{
	if (_pBuffer == nullptr || _pBuffer->m_pTexture == nullptr) {
		return;
	}

	const CachedTexture * pSource = _pBuffer->m_pTexture;
	std::unique_ptr<FrameBuffer> & leftEye = m_leftEyes[_pBuffer->m_startAddress];
	if (leftEye == nullptr ||
		leftEye->m_pTexture->width != pSource->width ||
		leftEye->m_pTexture->height != pSource->height)
		leftEye = _createBuffer(_pBuffer);

	ObjectHandle readBuffer = _pBuffer->m_FBO;
	if (pSource->frameBufferTexture == CachedTexture::fbMultiSample) {
		_pBuffer->resolveMultisampledTexture(true);
		readBuffer = _pBuffer->m_resolveFBO;
	}

	Context::BlitFramebuffersParams params;
	params.readBuffer = readBuffer;
	params.drawBuffer = leftEye->m_FBO;
	params.srcX0 = 0;
	params.srcY0 = 0;
	params.srcX1 = pSource->width;
	params.srcY1 = pSource->height;
	params.dstX0 = 0;
	params.dstY0 = 0;
	params.dstX1 = leftEye->m_pTexture->width;
	params.dstY1 = leftEye->m_pTexture->height;
	params.mask = blitMask::COLOR_BUFFER;
	params.filter = textureParameters::FILTER_NEAREST;

	if (gfxContext.blitFramebuffers(params)) {
		m_captured.insert(_pBuffer->m_startAddress);
		m_capturedThisTask.insert(_pBuffer->m_startAddress);
	} else {
		m_captured.erase(_pBuffer->m_startAddress);
		m_capturedThisTask.erase(_pBuffer->m_startAddress);
	}

	gfxContext.bindFramebuffer(bufferTarget::DRAW_FRAMEBUFFER, ObjectHandle::defaultFramebuffer);
}

FrameBuffer * StereoFrames::leftEye(const FrameBuffer * _pBuffer) const
{
	if (_pBuffer == nullptr || m_captured.count(_pBuffer->m_startAddress) == 0)
		return nullptr;
	const auto iter = m_leftEyes.find(_pBuffer->m_startAddress);
	return iter != m_leftEyes.end() ? iter->second.get() : nullptr;
}

void StereoFrames::forget(const FrameBuffer * _pBuffer)
{
	if (_pBuffer == nullptr)
		return;
	m_captured.erase(_pBuffer->m_startAddress);
	m_capturedThisTask.erase(_pBuffer->m_startAddress);
}

void StereoFrames::destroy()
{
	m_captured.clear();
	m_capturedThisTask.clear();
	m_enteredThisPass.clear();
	m_leftEyes.clear();
	m_capturing = false;
}
