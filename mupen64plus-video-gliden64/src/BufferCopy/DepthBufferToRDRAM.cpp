#include <assert.h>
#include <math.h>
#include <algorithm>
#include <cstring>

#include "DepthBufferToRDRAM.h"
#include "../gli_rdram_write.h"

#include <FrameBuffer.h>
#include <DepthBuffer.h>
#include <Textures.h>
#include <Config.h>
#include <N64.h>
#include <VI.h>

#include <Graphics/Context.h>
#include <Graphics/Parameters.h>
#include <Graphics/PixelBuffer.h>
#include <DisplayWindow.h>

using namespace graphics;

#define DEPTH_TEX_WIDTH 640
#define DEPTH_TEX_HEIGHT 580

DepthBufferToRDRAM::DepthBufferToRDRAM()
	: m_frameCount(-1)
	, m_pColorTexture(nullptr)
	, m_pDepthTexture(nullptr)
	, m_pCurFrameBuffer(nullptr)
{
}

DepthBufferToRDRAM::~DepthBufferToRDRAM()
{
}

DepthBufferToRDRAM & DepthBufferToRDRAM::get()
{
	static DepthBufferToRDRAM dbCopy;
	return dbCopy;
}

void DepthBufferToRDRAM::init()
{
	// Generate and initialize Pixel Buffer Objects
	m_pbuf.reset(gfxContext.createPixelReadBuffer(DEPTH_TEX_WIDTH * DEPTH_TEX_HEIGHT * sizeof(float)));
	if (!m_pbuf)
		return;

	m_pColorTexture = textureCache().addFrameBufferTexture(textureTarget::TEXTURE_2D);
	m_pColorTexture->format = G_IM_FMT_I;
	m_pColorTexture->size = 2;
	m_pColorTexture->clampS = 1;
	m_pColorTexture->clampT = 1;
	m_pColorTexture->frameBufferTexture = CachedTexture::fbOneSample;
	m_pColorTexture->maskS = 0;
	m_pColorTexture->maskT = 0;
	m_pColorTexture->mirrorS = 0;
	m_pColorTexture->mirrorT = 0;
	m_pColorTexture->width = DEPTH_TEX_WIDTH;
	m_pColorTexture->height = DEPTH_TEX_HEIGHT;
	m_pColorTexture->textureBytes = m_pColorTexture->width * m_pColorTexture->height;

	m_pDepthTexture = textureCache().addFrameBufferTexture(textureTarget::TEXTURE_2D);
	m_pDepthTexture->format = G_IM_FMT_I;
	m_pColorTexture->size = 2;
	m_pDepthTexture->clampS = 1;
	m_pDepthTexture->clampT = 1;
	m_pDepthTexture->frameBufferTexture = CachedTexture::fbOneSample;
	m_pDepthTexture->maskS = 0;
	m_pDepthTexture->maskT = 0;
	m_pDepthTexture->mirrorS = 0;
	m_pDepthTexture->mirrorT = 0;
	m_pDepthTexture->width = DEPTH_TEX_WIDTH;
	m_pDepthTexture->height = DEPTH_TEX_HEIGHT;
	m_pDepthTexture->textureBytes = m_pDepthTexture->width * m_pDepthTexture->height * sizeof(float);

	const FramebufferTextureFormats & fbTexFormats = gfxContext.getFramebufferTextureFormats();
	Context::InitTextureParams initParams;
	initParams.handle = m_pColorTexture->name;
	initParams.textureUnitIndex = textureIndices::Tex[0];
	initParams.width = m_pColorTexture->width;
	initParams.height = m_pColorTexture->height;
	initParams.internalFormat = fbTexFormats.monochromeInternalFormat;
	initParams.format = fbTexFormats.monochromeFormat;
	initParams.dataType = fbTexFormats.monochromeType;
	gfxContext.init2DTexture(initParams);

	Context::TexParameters setParams;
	setParams.handle = m_pColorTexture->name;
	setParams.target = textureTarget::TEXTURE_2D;
	setParams.textureUnitIndex = textureIndices::Tex[0];
	setParams.minFilter = textureParameters::FILTER_NEAREST;
	setParams.magFilter = textureParameters::FILTER_NEAREST;
	gfxContext.setTextureParameters(setParams);

	initParams.handle = m_pDepthTexture->name;
	initParams.width = m_pDepthTexture->width;
	initParams.height = m_pDepthTexture->height;
	initParams.internalFormat = fbTexFormats.depthInternalFormat;
	initParams.format = fbTexFormats.depthFormat;
	initParams.dataType = fbTexFormats.depthType;
	gfxContext.init2DTexture(initParams);

	setParams.handle = m_pDepthTexture->name;
	gfxContext.setTextureParameters(setParams);

	m_FBO = gfxContext.createFramebuffer();
	Context::FrameBufferRenderTarget bufTarget;
	bufTarget.bufferHandle = m_FBO;
	bufTarget.bufferTarget = bufferTarget::DRAW_FRAMEBUFFER;
	bufTarget.attachment = bufferAttachment::COLOR_ATTACHMENT0;
	bufTarget.textureTarget = textureTarget::TEXTURE_2D;
	bufTarget.textureHandle = m_pColorTexture->name;
	gfxContext.addFrameBufferRenderTarget(bufTarget);

	bufTarget.attachment = bufferAttachment::DEPTH_ATTACHMENT;
	bufTarget.textureHandle = m_pDepthTexture->name;
	gfxContext.addFrameBufferRenderTarget(bufTarget);

	// check if everything is OK
	assert(!gfxContext.isFramebufferError());
	assert(!gfxContext.isError());
	gfxContext.bindFramebuffer(bufferTarget::DRAW_FRAMEBUFFER, ObjectHandle::defaultFramebuffer);
}

void DepthBufferToRDRAM::destroy() {
	if (!m_pbuf)
		return;

	gfxContext.deleteFramebuffer(m_FBO);
	m_FBO.reset();
	if (m_pColorTexture != nullptr) {
		textureCache().removeFrameBufferTexture(m_pColorTexture);
		m_pColorTexture = nullptr;
	}
	if (m_pDepthTexture != nullptr) {
		textureCache().removeFrameBufferTexture(m_pDepthTexture);
		m_pDepthTexture = nullptr;
	}
	m_pbuf.reset();
}

bool DepthBufferToRDRAM::_prepareCopy(u32& _startAddress, bool _copyChunk)
{
	const u32 curFrame = dwnd().getBuffersSwapCount();
	if (_copyChunk && m_frameCount == curFrame)
		return true;

	if ((VI.width | VI.height) == 0) // Incorrect buffer size. Don't copy
		return false;

	FrameBuffer *pBuffer = frameBufferList().findBuffer(_startAddress);
	if (pBuffer == nullptr || pBuffer->isAuxiliary() || pBuffer->m_pDepthBuffer == nullptr || !pBuffer->m_pDepthBuffer->m_cleared)
		return false;

	FrameBuffer * pDepthFrameBuffer = frameBufferList().findBuffer(pBuffer->m_pDepthBuffer->m_address);
	if (pDepthFrameBuffer != nullptr)
		m_pCurFrameBuffer = pDepthFrameBuffer;
	else
		m_pCurFrameBuffer = pBuffer;

	if (m_pCurFrameBuffer->m_width != pBuffer->m_pDepthBuffer->m_width)
		return false;

	const u32 numPixels = m_pCurFrameBuffer->m_width * m_pCurFrameBuffer->m_height;
	const u32 bufferOrigin = m_pCurFrameBuffer->m_pDepthBuffer->m_address;
	if (bufferOrigin + numPixels * 2 > RDRAMSize + 1)
		return false;

	const u32 height = cutHeight(bufferOrigin, m_pCurFrameBuffer->m_height, m_pCurFrameBuffer->m_width * 2);
	if (height == 0)
		return false;

	_startAddress &= ~0xfff;
	if (_startAddress < bufferOrigin)
		_startAddress = bufferOrigin;

	ObjectHandle readBuffer = pBuffer->m_FBO;
	if (config.video.multisampling != 0) {
		m_pCurFrameBuffer->m_pDepthBuffer->resolveDepthBufferTexture(m_pCurFrameBuffer);
		readBuffer = m_pCurFrameBuffer->m_resolveFBO;
	}

	Context::BlitFramebuffersParams blitParams;
	blitParams.readBuffer = readBuffer;
	blitParams.drawBuffer = m_FBO;
	blitParams.srcX0 = 0;
	blitParams.srcY0 = 0;
	blitParams.srcX1 = m_pCurFrameBuffer->m_pTexture->realWidth;
	blitParams.srcY1 = s32(m_pCurFrameBuffer->m_height * m_pCurFrameBuffer->m_scale);
	blitParams.dstX0 = 0;
	blitParams.dstY0 = 0;
	blitParams.dstX1 = m_pCurFrameBuffer->m_width;
	blitParams.dstY1 = m_pCurFrameBuffer->m_height;
	blitParams.mask = blitMask::DEPTH_BUFFER;
	blitParams.filter = textureParameters::FILTER_NEAREST;

	gfxContext.blitFramebuffers(blitParams);

	frameBufferList().setCurrentDrawBuffer();
	m_frameCount = curFrame;
	return true;
}

bool DepthBufferToRDRAM::_copy(u32 _startAddress, u32 _endAddress)
{
	DepthBuffer * pDepthBuffer = m_pCurFrameBuffer->m_pDepthBuffer;
	const u32 stride = m_pCurFrameBuffer->m_width << 1;
	const u32 max_height = cutHeight(_startAddress, m_pCurFrameBuffer->m_height, stride);

	u32 numPixels = (_endAddress - _startAddress) >> 1;
	if (numPixels / m_pCurFrameBuffer->m_width > max_height) {
		_endAddress = _startAddress + (max_height * stride);
		numPixels = (_endAddress - _startAddress) >> 1;
	}

	const u32 width = m_pCurFrameBuffer->m_width;
	const s32 x0 = 0;
	const s32 y0 = (_startAddress - pDepthBuffer->m_address) / stride;
	const u32 y1 = (_endAddress - pDepthBuffer->m_address) / stride;
	const u32 height = std::min(max_height, 1u + y1 - y0);

	gfxContext.bindFramebuffer(bufferTarget::READ_FRAMEBUFFER, m_FBO);

	PixelBufferBinder<PixelReadBuffer> binder(m_pbuf.get());
	const FramebufferTextureFormats & fbTexFormats = gfxContext.getFramebufferTextureFormats();
	m_pbuf->readPixels(x0, y0, width, height, fbTexFormats.depthFormat, fbTexFormats.depthType);
	u8 * pixelData = (u8*)m_pbuf->getDataRange(0, width * height * fbTexFormats.depthFormatBytes);
	if (pixelData == nullptr)
		return false;

	/* converted straight out of the mapped pack buffer */
	gli_rdram_write w;
	w.srcStride = width;
	w.width = width;
	w.height = height;
	w.numPixels = numPixels;
	w.startAddress = _startAddress;
	w.bufferAddress = pDepthBuffer->m_address;
	w.size = G_IM_SIZ_16b;
	gli_rdram_write_depth((const f32*)pixelData, &w);

	pDepthBuffer->m_cleared = false;
	FrameBuffer * pBuffer = frameBufferList().findBuffer(pDepthBuffer->m_address);
	if (pBuffer != nullptr)
		pBuffer->m_cleared = false;

	m_pbuf->closeReadBuffer();

	gDP.changed |= CHANGED_SCISSOR;
	return true;
}

bool DepthBufferToRDRAM::copyToRDRAM(u32 _address)
{
	if (config.frameBufferEmulation.copyDepthToRDRAM == cdSoftwareRender)
		return true;

	if (!m_pbuf)
		return false;

	if (!_prepareCopy(_address, false))
		return false;

	const u32 endAddress = m_pCurFrameBuffer->m_pDepthBuffer->m_address +
			m_pCurFrameBuffer->m_width * m_pCurFrameBuffer->m_height * 2;

	return _copy(m_pCurFrameBuffer->m_pDepthBuffer->m_address, endAddress);
}

bool DepthBufferToRDRAM::copyChunkToRDRAM(u32 _startAddress)
{
	if (config.frameBufferEmulation.copyDepthToRDRAM == cdSoftwareRender)
		return true;

	if (!m_pbuf)
		return false;

	const u32 endAddress = (_startAddress & ~0xfff) + 0x1000;

	if (!_prepareCopy(_startAddress, true))
		return false;

	return _copy(_startAddress, endAddress);
}
