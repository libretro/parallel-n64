#include <assert.h>
#include <math.h>
#include <string.h>
#include <algorithm>
#include <vector>
#include "FrameBuffer.h"
#include "StereoFrames.h"
#include "gSP.h"
#include "DepthBuffer.h"
#include "N64.h"
#include "RSP.h"
#include "RDP.h"
#include "gDP.h"
#include "VI.h"
#include "Textures.h"
#include "Combiner.h"
#include "Types.h"
#include "Config.h"
#include "PostProcessor.h"
#include "FrameBufferInfo.h"

#include "BufferCopy/ColorBufferToRDRAM.h"
#include "BufferCopy/DepthBufferToRDRAM.h"
#include "BufferCopy/RDRAMtoColorBuffer.h"

#include <Graphics/Context.h>
#include <Graphics/Parameters.h>
#include "DisplayWindow.h"
#include "gli_framebuffer.h"
#include "gli_drawer.h"

using namespace std;
using namespace graphics;

FrameBuffer::FrameBuffer()
	: m_pDepthBuffer(nullptr)
	, m_pResolveTexture(nullptr)
	, m_pSubTexture(nullptr)
	, m_pFrameBufferCopyTexture(nullptr)
	, m_copyFBO(ObjectHandle::defaultFramebuffer)
{
	memset(static_cast<gli_fb *>(this), 0, sizeof(gli_fb));
	m_loadType = LOADTYPE_BLOCK;
	m_pTexture = textureCache().addFrameBufferTexture(config.video.multisampling != 0 ?
		textureTarget::TEXTURE_2D_MULTISAMPLE : textureTarget::TEXTURE_2D);
	m_FBO = gfxContext.createFramebuffer();
}

FrameBuffer::~FrameBuffer()
{
	gli_fb_rdram_copy_alloc(this, 0);
	gfxContext.deleteFramebuffer(m_FBO);
	gfxContext.deleteFramebuffer(m_resolveFBO);
	gfxContext.deleteFramebuffer(m_SubFBO);
	gfxContext.deleteFramebuffer(m_copyFBO);

	textureCache().removeFrameBufferTexture(m_pTexture);
	textureCache().removeFrameBufferTexture(m_pResolveTexture);
	textureCache().removeFrameBufferTexture(m_pSubTexture);
	textureCache().removeFrameBufferTexture(m_pFrameBufferCopyTexture);
}

static
void _initFrameBufferTexture(u32 _address, u16 _width, u16 _height, f32 _scale, u16 _format, u16 _size, CachedTexture *_pTexture)
{
	const FramebufferTextureFormats & fbTexFormats = gfxContext.getFramebufferTextureFormats();

	_pTexture->width = (u16)(u32)(_width * _scale);
	_pTexture->height = (u16)(u32)(_height * _scale);
	_pTexture->format = _format;
	_pTexture->size = _size;
	_pTexture->clampS = 1;
	_pTexture->clampT = 1;
	_pTexture->address = _address;
	_pTexture->clampWidth = _width;
	_pTexture->clampHeight = _height;
	_pTexture->frameBufferTexture = CachedTexture::fbOneSample;
	_pTexture->maskS = 0;
	_pTexture->maskT = 0;
	_pTexture->mirrorS = 0;
	_pTexture->mirrorT = 0;
	_pTexture->realWidth = _pTexture->width;
	_pTexture->realHeight = _pTexture->height;
	_pTexture->textureBytes = _pTexture->realWidth * _pTexture->realHeight;
	if (_size > G_IM_SIZ_8b)
		_pTexture->textureBytes *= fbTexFormats.colorFormatBytes;
	else
		_pTexture->textureBytes *= fbTexFormats.monochromeFormatBytes;
}

void FrameBuffer::_initTexture(u16 _width, u16 _height, u16 _format, u16 _size, CachedTexture *_pTexture)
{
	_initFrameBufferTexture(m_startAddress, _width, _height, m_scale, _format, _size, _pTexture);
}

static
void _setAndAttachBufferTexture(ObjectHandle _fbo, CachedTexture *_pTexture, u32 _t, bool _multisampling)
{
	const FramebufferTextureFormats & fbTexFormat = gfxContext.getFramebufferTextureFormats();
	Context::InitTextureParams initParams;
	initParams.handle = _pTexture->name;
	initParams.textureUnitIndex = textureIndices::Tex[_t];
	if (_multisampling)
		initParams.msaaLevel = config.video.multisampling;
	initParams.width = _pTexture->realWidth;
	initParams.height = _pTexture->realHeight;
	if (_pTexture->size > G_IM_SIZ_8b) {
		initParams.internalFormat = fbTexFormat.colorInternalFormat;
		initParams.format = fbTexFormat.colorFormat;
		initParams.dataType = fbTexFormat.colorType;
	} else {
		initParams.internalFormat = fbTexFormat.monochromeInternalFormat;
		initParams.format = fbTexFormat.monochromeFormat;
		initParams.dataType = fbTexFormat.monochromeType;
	}
	gfxContext.init2DTexture(initParams);

	if (!_multisampling) {
		Context::TexParameters texParams;
		texParams.handle = _pTexture->name;
		texParams.target = textureTarget::TEXTURE_2D;
		texParams.textureUnitIndex = textureIndices::Tex[_t];
		texParams.minFilter = textureParameters::FILTER_NEAREST;
		texParams.magFilter = textureParameters::FILTER_NEAREST;
		gfxContext.setTextureParameters(texParams);
	}

	Context::FrameBufferRenderTarget bufTarget;
	bufTarget.bufferHandle = _fbo;
	bufTarget.bufferTarget = bufferTarget::FRAMEBUFFER;
	bufTarget.attachment = bufferAttachment::COLOR_ATTACHMENT0;
	bufTarget.textureTarget = _multisampling ? textureTarget::TEXTURE_2D_MULTISAMPLE : textureTarget::TEXTURE_2D;
	bufTarget.textureHandle = _pTexture->name;
	gfxContext.addFrameBufferRenderTarget(bufTarget);
	assert(!gfxContext.isFramebufferError());
}

void FrameBuffer::_setAndAttachTexture(ObjectHandle _fbo, CachedTexture *_pTexture, u32 _t, bool _multisampling)
{
	_setAndAttachBufferTexture(_fbo, _pTexture, _t, _multisampling);
}

bool FrameBuffer::isAuxiliary() const
{
	return gli_fb_is_auxiliary(this) != 0;
}

void FrameBuffer::init(u32 _address, u16 _format, u16 _size, u16 _width, bool _cfb)
{
	m_startAddress = _address;
	m_width = _width;
	m_height = _cfb ? VI.height : 1;
//	m_height = VI.height;
	m_size = _size;
	updateEndAddress();
	if (isAuxiliary() && config.frameBufferEmulation.copyAuxToRDRAM != 0) {
		m_scale = 1.0f;
	} else if (config.frameBufferEmulation.nativeResFactor != 0 && config.frameBufferEmulation.enable != 0) {
		m_scale = static_cast<float>(config.frameBufferEmulation.nativeResFactor);
	} else {
		m_scale = dwnd().getScaleX();
	}
	m_cfb = _cfb;
	m_cleared = false;
	m_fingerprint = false;
	m_swapCount = dwnd().getBuffersSwapCount();

	const u16 maxHeight = gli_VI_GetMaxBufferHeight(_width);
	_initTexture(_width, maxHeight, _format, _size, m_pTexture);
	gli_fb_rdram_copy_alloc(this, (_width * maxHeight) << _size >> 1);

	if (config.video.multisampling != 0) {
		_setAndAttachTexture(m_FBO, m_pTexture, 0, true);
		m_pTexture->frameBufferTexture = CachedTexture::fbMultiSample;

		m_pResolveTexture = textureCache().addFrameBufferTexture(textureTarget::TEXTURE_2D);
		_initTexture(_width, maxHeight, _format, _size, m_pResolveTexture);
		m_resolveFBO = gfxContext.createFramebuffer();
		_setAndAttachTexture(m_resolveFBO, m_pResolveTexture, 0, false);
		assert(!gfxContext.isFramebufferError());

		gfxContext.bindFramebuffer(bufferTarget::FRAMEBUFFER, m_FBO);
	} else
		_setAndAttachTexture(m_FBO, m_pTexture, 0, false);

//	gfxContext.clearColorBuffer(0.0f, 0.0f, 0.0f, 0.0f);
}

void FrameBuffer::updateEndAddress()
{
	gli_fb_update_end_address(this);
}

void FrameBuffer::setBufferClearParams(u32 _fillcolor, s32 _ulx, s32 _uly, s32 _lrx, s32 _lry)
{
	gli_fb_set_clear_params(this, _fillcolor, _ulx, _uly, _lrx, _lry);
}

void FrameBuffer::copyRdram()
{
	gli_fb_save_rdram(this);
}

void FrameBuffer::setDirty()
{
	gli_fb_set_dirty(this);
}

bool FrameBuffer::isValid(bool _forceCheck) const
{
	/* the check stamps the swap it ran in */
	return gli_fb_is_valid(const_cast<FrameBuffer *>(this), _forceCheck, dwnd().getBuffersSwapCount()) != 0;
}

void FrameBuffer::resolveMultisampledTexture(bool _bForce)
{
	if (!Context::Multisampling)
		return;

	if (m_resolved && !_bForce)
		return;

	Context::BlitFramebuffersParams blitParams;
	blitParams.readBuffer = m_FBO;
	blitParams.drawBuffer = m_resolveFBO;
	blitParams.srcX0 = 0;
	blitParams.srcY0 = 0;
	blitParams.srcX1 = m_pTexture->realWidth;
	blitParams.srcY1 = m_pTexture->realHeight;
	blitParams.dstX0 = 0;
	blitParams.dstY0 = 0;
	blitParams.dstX1 = m_pResolveTexture->realWidth;
	blitParams.dstY1 = m_pResolveTexture->realHeight;
	blitParams.mask = blitMask::COLOR_BUFFER;
	blitParams.filter = textureParameters::FILTER_NEAREST;

	gfxContext.blitFramebuffers(blitParams);

	gfxContext.bindFramebuffer(bufferTarget::READ_FRAMEBUFFER, ObjectHandle::defaultFramebuffer);

	frameBufferList().setCurrentDrawBuffer();
	m_resolved = true;
}

bool FrameBuffer::_initSubTexture(u32 _t)
{
	if (!m_SubFBO.isNotNull())
		m_SubFBO = gfxContext.createFramebuffer();

	gDPTile * pTile = gSP.textureTile[_t];
	if (pTile->lrs < pTile->uls || pTile->lrt < pTile->ult)
		return false;
	const u32 width = pTile->lrs - pTile->uls + 1;
	const u32 height = pTile->lrt - pTile->ult + 1;

	if (m_pSubTexture != nullptr) {
		if (m_pSubTexture->size == m_pTexture->size &&
			m_pSubTexture->clampWidth == width &&
			m_pSubTexture->clampHeight == height)
			return true;
		textureCache().removeFrameBufferTexture(m_pSubTexture);
	}

	m_pSubTexture = textureCache().addFrameBufferTexture(textureTarget::TEXTURE_2D);
	_initTexture(width, height, m_pTexture->format, m_pTexture->size, m_pSubTexture);

	m_pSubTexture->clampS = pTile->clamps;
	m_pSubTexture->clampT = pTile->clampt;
	m_pSubTexture->offsetS = 0.0f;
	m_pSubTexture->offsetT = 0.0f;


	_setAndAttachTexture(m_SubFBO, m_pSubTexture, _t, false);

	return true;
}

CachedTexture * FrameBuffer::_getSubTexture(u32 _t)
{
	if (!Context::BlitFramebuffer)
		return m_pTexture;

	if (!_initSubTexture(_t))
		return m_pTexture;

	s32 x0 = (s32)(m_pTexture->offsetS * m_scale);
	s32 y0 = (s32)(m_pTexture->offsetT * m_scale);
	s32 copyWidth = m_pSubTexture->realWidth;
	if (x0 + copyWidth > m_pTexture->realWidth)
		copyWidth = m_pTexture->realWidth - x0;
	s32 copyHeight = m_pSubTexture->realHeight;
	if (y0 + copyHeight > m_pTexture->realHeight)
		copyHeight = m_pTexture->realHeight - y0;

	ObjectHandle readFBO = m_FBO;
	if (Context::WeakBlitFramebuffer &&
			m_pTexture->frameBufferTexture == CachedTexture::fbMultiSample) {
		resolveMultisampledTexture(true);
		readFBO = m_resolveFBO;
	}

	Context::BlitFramebuffersParams blitParams;
	blitParams.readBuffer = readFBO;
	blitParams.drawBuffer = m_SubFBO;
	blitParams.srcX0 = x0;
	blitParams.srcY0 = y0;
	blitParams.srcX1 = x0 + copyWidth;
	blitParams.srcY1 = y0 + copyHeight;
	blitParams.dstX0 = 0;
	blitParams.dstY0 = 0;
	blitParams.dstX1 = copyWidth;
	blitParams.dstY1 = copyHeight;
	blitParams.mask = blitMask::COLOR_BUFFER;
	blitParams.filter = textureParameters::FILTER_NEAREST;

	gfxContext.blitFramebuffers(blitParams);

	gfxContext.bindFramebuffer(bufferTarget::READ_FRAMEBUFFER, ObjectHandle::defaultFramebuffer);

	frameBufferList().setCurrentDrawBuffer();

	return m_pSubTexture;
}

void FrameBuffer::_initCopyTexture()
{
	m_copyFBO = gfxContext.createFramebuffer();
	m_pFrameBufferCopyTexture = textureCache().addFrameBufferTexture(config.video.multisampling != 0 ?
		textureTarget::TEXTURE_2D_MULTISAMPLE : textureTarget::TEXTURE_2D);
	_initTexture(m_width, gli_VI_GetMaxBufferHeight(m_width), m_pTexture->format, m_pTexture->size, m_pFrameBufferCopyTexture);
	_setAndAttachTexture(m_copyFBO, m_pFrameBufferCopyTexture, 0, config.video.multisampling != 0);
	if (config.video.multisampling != 0)
		m_pFrameBufferCopyTexture->frameBufferTexture = CachedTexture::fbMultiSample;
}

CachedTexture * FrameBuffer::_copyFrameBufferTexture()
{
	if (m_copied)
		return m_pFrameBufferCopyTexture;

	if (m_pFrameBufferCopyTexture == nullptr)
		_initCopyTexture();

	Context::BlitFramebuffersParams blitParams;
	blitParams.readBuffer = m_FBO;
	blitParams.drawBuffer = m_copyFBO;
	blitParams.srcX0 = 0;
	blitParams.srcY0 = 0;
	blitParams.srcX1 = m_pTexture->realWidth;
	blitParams.srcY1 = m_pTexture->realHeight;
	blitParams.dstX0 = 0;
	blitParams.dstY0 = 0;
	blitParams.dstX1 = m_pTexture->realWidth;
	blitParams.dstY1 = m_pTexture->realHeight;
	blitParams.mask = blitMask::COLOR_BUFFER;
	blitParams.filter = textureParameters::FILTER_NEAREST;

	gfxContext.blitFramebuffers(blitParams);

	gfxContext.bindFramebuffer(bufferTarget::READ_FRAMEBUFFER, ObjectHandle::defaultFramebuffer);
	frameBufferList().setCurrentDrawBuffer();

	m_copied = true;
	return m_pFrameBufferCopyTexture;
}

CachedTexture * FrameBuffer::getTexture(u32 _t)
{
	const bool getDepthTexture = m_isDepthBuffer &&
								 gDP.colorImage.address == gDP.depthImageAddress &&
								 m_pDepthBuffer != nullptr &&
								 (config.generalEmulation.hacks & hack_ZeldaMM) == 0;
	CachedTexture *pTexture = getDepthTexture ? m_pDepthBuffer->m_pDepthBufferTexture : m_pTexture;

	if (this == frameBufferList().getCurrent()) {
		if (Context::TextureBarrier)
			gfxContext.textureBarrier();
		else if (Context::BlitFramebuffer)
			pTexture = getDepthTexture ? m_pDepthBuffer->copyDepthBufferTexture(this) : _copyFrameBufferTexture();
	}

	const u32 shift = (gSP.textureTile[_t]->imageAddress - m_startAddress) >> (m_size - 1);
	const u32 factor = m_width;
	if (m_loadType == LOADTYPE_TILE) {
		pTexture->offsetS = (float)(m_loadTileOrigin.uls + (shift % factor));
		pTexture->offsetT = (float)(m_loadTileOrigin.ult + shift / factor);
	} else {
		pTexture->offsetS = (float)(shift % factor);
		pTexture->offsetT = (float)(shift / factor);
	}

	if (!getDepthTexture && (gSP.textureTile[_t]->clamps == 0 || gSP.textureTile[_t]->clampt == 0))
		pTexture = _getSubTexture(_t);

	pTexture->scaleS = m_scale / (float)pTexture->realWidth;
	pTexture->scaleT = m_scale / (float)pTexture->realHeight;

	if (gSP.textureTile[_t]->shifts > 10)
		pTexture->shiftScaleS = (float)(1 << (16 - gSP.textureTile[_t]->shifts));
	else if (gSP.textureTile[_t]->shifts > 0)
		pTexture->shiftScaleS = 1.0f / (float)(1 << gSP.textureTile[_t]->shifts);
	else
		pTexture->shiftScaleS = 1.0f;

	if (gSP.textureTile[_t]->shiftt > 10)
		pTexture->shiftScaleT = (float)(1 << (16 - gSP.textureTile[_t]->shiftt));
	else if (gSP.textureTile[_t]->shiftt > 0)
		pTexture->shiftScaleT = 1.0f / (float)(1 << gSP.textureTile[_t]->shiftt);
	else
		pTexture->shiftScaleT = 1.0f;

	return pTexture;
}

CachedTexture * FrameBuffer::getTextureBG(u32 _t)
{
	CachedTexture *pTexture = m_pTexture;

	if (this == frameBufferList().getCurrent()) {
		if (Context::TextureBarrier)
			gfxContext.textureBarrier();
		else if (Context::BlitFramebuffer)
			pTexture = _copyFrameBufferTexture();
	}

	pTexture->scaleS = m_scale / (float)pTexture->realWidth;
	pTexture->scaleT = m_scale / (float)pTexture->realHeight;

	pTexture->shiftScaleS = 1.0f;
	pTexture->shiftScaleT = 1.0f;

	pTexture->offsetS = gSP.bgImage.imageX;
	pTexture->offsetT = gSP.bgImage.imageY;
	return pTexture;
}

FrameBufferList & FrameBufferList::get()
{
	static FrameBufferList frameBufferList;
	return frameBufferList;
}

void FrameBufferList::init()
{
	 m_pCurrent = nullptr;
	 m_pCopy = nullptr;
	 gfxContext.bindFramebuffer(bufferTarget::DRAW_FRAMEBUFFER, ObjectHandle::defaultFramebuffer);
	 m_prevColorImageHeight = 0;
	 m_overscan.init();
	 gli_vi_state_init(&m_rdpUpdate);
}

void FrameBufferList::destroy() {
	gfxContext.bindFramebuffer(bufferTarget::FRAMEBUFFER, ObjectHandle::defaultFramebuffer);
	m_list.clear();
	gli_StereoFrames_destroy();
	m_pCurrent = nullptr;
	m_pCopy = nullptr;
	m_overscan.destroy();
}

void FrameBufferList::setBufferChanged(f32 _maxY)
{
	gDP.colorImage.changed = TRUE;
	gDP.colorImage.height = max(gDP.colorImage.height, (u32)_maxY);
	gDP.colorImage.height = min(gDP.colorImage.height, (u32)gDP.scissor.lry);
	if (m_pCurrent != nullptr) {
		m_pCurrent->m_height = max(m_pCurrent->m_height, gDP.colorImage.height);
		m_pCurrent->m_cfb = false;
		m_pCurrent->m_changed = true;
		m_pCurrent->m_copiedToRdram = false;
	}
}

void FrameBufferList::clearBuffersChanged()
{
	gDP.colorImage.changed = FALSE;
	FrameBuffer * pBuffer = frameBufferList().findBuffer(*REG.VI_ORIGIN);
	if (pBuffer != nullptr)
		pBuffer->m_changed = false;
}

void FrameBufferList::setCurrentDrawBuffer() const
{
	if (m_pCurrent != nullptr)
		gfxContext.bindFramebuffer(bufferTarget::DRAW_FRAMEBUFFER, m_pCurrent->m_FBO);
	else if (!m_list.empty())
		gfxContext.bindFramebuffer(bufferTarget::DRAW_FRAMEBUFFER, m_list.back().m_FBO);
}

FrameBuffer * FrameBufferList::findBuffer(u32 _startAddress)
{
	for (auto iter = m_list.begin(); iter != m_list.end(); ++iter) {
		if (iter->m_startAddress <= _startAddress && iter->m_endAddress >= _startAddress) // [  {  ]
			return &(*iter);
	}
	return nullptr;
}

FrameBuffer * FrameBufferList::getBuffer(u32 _startAddress)
{
	for (auto iter = m_list.begin(); iter != m_list.end(); ++iter) {
		if (iter->m_startAddress == _startAddress)
			return &(*iter);
	}
	return nullptr;
}

static bool isOverlapping(const FrameBuffer * _buf1, const FrameBuffer * _buf2)
{
	return gli_fb_overlapping(_buf1, _buf2) != 0;
}

void FrameBufferList::removeIntersections()
{
	assert(!m_list.empty());

	FrameBuffers::iterator iter = m_list.end();
	do {
		--iter;
		if (&(*iter) == m_pCurrent)
			continue;
		if (iter->m_startAddress <= m_pCurrent->m_startAddress && iter->m_endAddress >= m_pCurrent->m_startAddress) { // [  {  ]
			if (isOverlapping(&(*iter), m_pCurrent)) {
				iter->m_endAddress = m_pCurrent->m_startAddress - 1;
				continue;
			}
			iter = m_list.erase(iter);
		} else if (m_pCurrent->m_startAddress <= iter->m_startAddress && m_pCurrent->m_endAddress >= iter->m_startAddress) { // {  [  }
			if (isOverlapping(m_pCurrent, &(*iter))) {
				m_pCurrent->m_endAddress = iter->m_startAddress - 1;
				continue;
			}
			iter = m_list.erase(iter);
		}
	} while (iter != m_list.begin());
}

FrameBuffer * FrameBufferList::findTmpBuffer(u32 _address)
{
	for (auto iter = m_list.begin(); iter != m_list.end(); ++iter)
		if (iter->m_startAddress > _address || iter->m_endAddress < _address)
				return &(*iter);
	return nullptr;
}

void FrameBufferList::updateCurrentBufferEndAddress()
{
	if (m_pCurrent == nullptr)
		return;
	m_pCurrent->updateEndAddress();
	removeIntersections();
}

void FrameBufferList::_createScreenSizeBuffer()
{
	if (VI.height == 0)
		return;
	m_list.emplace_front();
	FrameBuffer & buffer = m_list.front();
	buffer.init(VI.width * 2, G_IM_FMT_RGBA, G_IM_SIZ_16b, VI.width, false);
}

void FrameBufferList::saveBuffer(u32 _address, u16 _format, u16 _size, u16 _width, bool _cfb)
{
	if (_width > 640)
		return;

	if (_width == 512 && (config.generalEmulation.hacks & hack_RE2) != 0)
		_width = *REG.VI_WIDTH;

	if (config.frameBufferEmulation.enable == 0) {
		if (m_list.empty())
			_createScreenSizeBuffer();
		return;
	}

	// stereoscopic 3D
	if (m_pCurrent != nullptr && !m_pCurrent->m_isDepthBuffer && g_stereoFramesCapturing &&
		gli_StereoFrames_wasEntered(m_pCurrent->m_startAddress)) {
		gli_StereoFrames_captureLeftEye(m_pCurrent);
		// The copy binds the screen; leave the state as the walk had it
		gfxContext.bindFramebuffer(bufferTarget::DRAW_FRAMEBUFFER, m_pCurrent->m_FBO);
	}

	if (m_pCurrent != nullptr &&
		config.frameBufferEmulation.copyAuxToRDRAM != 0 &&
		(config.generalEmulation.hacks & hack_Snap) == 0) {
		if (m_pCurrent->isAuxiliary()) {
			FrameBuffer_CopyToRDRAM(m_pCurrent->m_startAddress, true);
			removeBuffer(m_pCurrent->m_startAddress);
		}
	}

	DisplayWindow & wnd = dwnd();
	bool bPrevIsDepth = false;

	if (m_pCurrent != nullptr) {
		bPrevIsDepth = m_pCurrent->m_isDepthBuffer;
		m_pCurrent->m_readable = true;
		m_pCurrent->updateEndAddress();

		if (!m_pCurrent->m_isDepthBuffer &&
			!m_pCurrent->m_copiedToRdram &&
			!m_pCurrent->m_cfb &&
			!m_pCurrent->m_cleared &&
			m_pCurrent->m_RdramCopySize == 0 &&
			m_pCurrent->m_height > 1) {
			m_pCurrent->copyRdram();
		}

		removeIntersections();
	}

	const float scaleX = config.frameBufferEmulation.nativeResFactor == 0 ?
		wnd.getScaleX() :
		static_cast<float>(config.frameBufferEmulation.nativeResFactor);

	if (m_pCurrent == nullptr || m_pCurrent->m_startAddress != _address || m_pCurrent->m_width != _width)
		m_pCurrent = findBuffer(_address);

	if (m_pCurrent != nullptr) {
		m_pCurrent->m_originX = m_pCurrent->m_originY = 0;
		if ((m_pCurrent->m_startAddress != _address)) {
			if (gli_fb_is_sub_buffer(m_pCurrent, wnd.getBuffersSwapCount(), _address, _width, _size)) {
				u32 originX, originY;
				gli_fb_sub_origin(m_pCurrent->m_startAddress, _address, _width, _size, &originX, &originY);
				m_pCurrent->m_originX = originX;
				m_pCurrent->m_originY = originY;
				gSP.changed |= CHANGED_VIEWPORT;
				gDP.changed |= CHANGED_SCISSOR;
				return;
			} else if (gli_fb_runs_into(m_pCurrent, _address, _width, _size)) {
				m_pCurrent->m_endAddress = _address - 1;
				m_pCurrent = nullptr;
			} else {
				removeBuffer(m_pCurrent->m_startAddress);
				m_pCurrent = nullptr;
			}
		} else if ((m_pCurrent->m_width != _width) ||
					(m_pCurrent->m_size < _size) ||
					(m_pCurrent->m_scale != scaleX)) {
			removeBuffer(m_pCurrent->m_startAddress);
			m_pCurrent = nullptr;
		} else {
			m_pCurrent->m_resolved = false;
			gfxContext.bindFramebuffer(bufferTarget::FRAMEBUFFER, m_pCurrent->m_FBO);
			if (m_pCurrent->m_size != _size) {
				f32 fillColor[4];
				gli_gDPGetFillColor(fillColor);
				wnd.getDrawer().clearColorBuffer(fillColor);
				m_pCurrent->m_size = _size;
				m_pCurrent->m_pTexture->format = _format;
				m_pCurrent->m_pTexture->size = _size;
				if (m_pCurrent->m_pResolveTexture != nullptr) {
					m_pCurrent->m_pResolveTexture->format = _format;
					m_pCurrent->m_pResolveTexture->size = _size;
				}
				if (m_pCurrent->m_copiedToRdram)
					m_pCurrent->copyRdram();
			}
		}
	}
	const bool bNew = m_pCurrent == nullptr;
	if  (bNew) {
		// Wasn't found or removed, create a new one
		m_list.emplace_front();
		FrameBuffer & buffer = m_list.front();
		buffer.init(_address, _format, _size, _width, _cfb);
		m_pCurrent = &buffer;
		RDRAMtoColorBuffer::get().copyFromRDRAM(m_pCurrent);
		if (_cfb)
			m_pCurrent->copyRdram();
	}

	if (_address == gDP.depthImageAddress)
		gli_depth_save(_address);
	else
		attachDepthBuffer();


	if (m_pCurrent->isAuxiliary() &&
		m_pCurrent->m_pDepthBuffer != nullptr &&
		bPrevIsDepth &&
		(config.generalEmulation.hacks&hack_LoadDepthTextures) == 0) {
		// N64 games may use partial depth buffer clear for aux buffers
		// It will not work for GL, so we have to force clear depth buffer for aux buffer
		wnd.getDrawer().clearDepthBuffer();
	}

	m_pCurrent->m_isDepthBuffer = _address == gDP.depthImageAddress;
	// Immersive mode
	if (gli_gSPImmersiveClearsShownBuffers() && !m_pCurrent->m_isDepthBuffer && !m_pCurrent->isAuxiliary() &&
		m_pCurrent->m_swapCount != wnd.getBuffersSwapCount()) {
		gfxContext.bindFramebuffer(bufferTarget::FRAMEBUFFER, m_pCurrent->m_FBO);
		gfxContext.clearColorBuffer(0.0f, 0.0f, 0.0f, 0.0f);
	}
	if (config.stereo.mode == stereoBothEyes)
		gli_StereoFrames_noteEntered(m_pCurrent->m_startAddress);
	m_pCurrent->m_isPauseScreen = m_pCurrent->m_isOBScreen = false;
	m_pCurrent->m_copied = false;
	m_pCurrent->m_swapCount = wnd.getBuffersSwapCount();
}

void FrameBufferList::copyAux()
{
	for (auto iter = m_list.begin(); iter != m_list.end(); ++iter) {
		if (iter->isAuxiliary())
			FrameBuffer_CopyToRDRAM(iter->m_startAddress, true);
	}
}

void FrameBufferList::removeAux()
{
	for (auto iter = m_list.begin(); iter != m_list.end(); ++iter) {
		while (iter->isAuxiliary()) {
			if (&(*iter) == m_pCurrent) {
				m_pCurrent = nullptr;
				gfxContext.bindFramebuffer(bufferTarget::DRAW_FRAMEBUFFER, ObjectHandle::defaultFramebuffer);
			}
			iter = m_list.erase(iter);
			if (iter == m_list.end())
				return;
		}
	}
}

void FrameBufferList::removeBuffer(u32 _address )
{
	for (auto iter = m_list.begin(); iter != m_list.end(); ++iter)
		if (iter->m_startAddress == _address) {
			if (&(*iter) == m_pCurrent) {
				m_pCurrent = nullptr;
				gfxContext.bindFramebuffer(bufferTarget::DRAW_FRAMEBUFFER, ObjectHandle::defaultFramebuffer);
			}
			m_list.erase(iter);
			return;
		}
}

void FrameBufferList::removeBuffers(u32 _width)
{
	m_pCurrent = nullptr;
	for (auto iter = m_list.begin(); iter != m_list.end(); ++iter) {
		while (iter->m_width == _width) {
			if (&(*iter) == m_pCurrent) {
				m_pCurrent = nullptr;
				gfxContext.bindFramebuffer(bufferTarget::DRAW_FRAMEBUFFER, ObjectHandle::defaultFramebuffer);
			}
			iter = m_list.erase(iter);
			if (iter == m_list.end())
				return;
		}
	}
}

void FrameBufferList::depthBufferCopyRdram()
{
	FrameBuffer * pCurrentDepthBuffer = findBuffer(gDP.depthImageAddress);
	if (pCurrentDepthBuffer != nullptr)
		pCurrentDepthBuffer->copyRdram();
}

void FrameBufferList::fillBufferInfo(void * _pinfo, u32 _size)
{
	FBInfoEntry* pInfo = reinterpret_cast<FBInfoEntry*>(_pinfo);

	u32 idx = 0;
	for (auto iter = m_list.begin(); iter != m_list.end(); ++iter) {
		if (iter->m_width == VI.width && !iter->m_cfb && !iter->m_isDepthBuffer) {
			pInfo[idx].addr = iter->m_startAddress;
			pInfo[idx].width = iter->m_width;
			pInfo[idx].height = iter->m_height;
			pInfo[idx++].size = iter->m_size;
			if (idx >= _size)
				return;
		}
	}
}

void FrameBufferList::attachDepthBuffer()
{
	FrameBuffer * pCurrent = config.frameBufferEmulation.enable == 0 ? &m_list.back() : m_pCurrent;
	if (pCurrent == nullptr)
		return;

	DepthBuffer * pDepthBuffer = gli_depth_current_buffer();

	if (pCurrent->m_FBO.isNotNull() && pDepthBuffer != nullptr) {
		pDepthBuffer->initDepthImageTexture(pCurrent);
		pDepthBuffer->initDepthBufferTexture(pCurrent);

		bool goodDepthBufferTexture = false;
		if (Context::DepthFramebufferTextures) {
			if (Context::WeakBlitFramebuffer)
				goodDepthBufferTexture = pDepthBuffer->m_pDepthBufferTexture->width == pCurrent->m_pTexture->realWidth;
			else
				goodDepthBufferTexture = pDepthBuffer->m_pDepthBufferTexture->width >= pCurrent->m_pTexture->realWidth ||
											std::abs((s32)(pCurrent->m_width - pDepthBuffer->m_width)) < 2;
		} else {
			goodDepthBufferTexture = pDepthBuffer->m_depthRenderbufferWidth == pCurrent->m_pTexture->realWidth;
		}

		if (goodDepthBufferTexture) {
			pCurrent->m_pDepthBuffer = pDepthBuffer;
			pDepthBuffer->setDepthAttachment(pCurrent->m_FBO, bufferTarget::DRAW_FRAMEBUFFER);
			if (config.frameBufferEmulation.N64DepthCompare != 0)
				pDepthBuffer->bindDepthImageTexture(pCurrent->m_FBO);
		} else
			pCurrent->m_pDepthBuffer = nullptr;
	} else
		pCurrent->m_pDepthBuffer = nullptr;

	assert(!gfxContext.isFramebufferError());
}

void FrameBufferList::clearDepthBuffer(DepthBuffer * _pDepthBuffer)
{
	for (auto iter = m_list.begin(); iter != m_list.end(); ++iter) {
		if (iter->m_pDepthBuffer == _pDepthBuffer) {
			iter->m_pDepthBuffer = nullptr;
		}
	}
}

void FrameBuffer_Init()
{
	frameBufferList().init();
	if (config.frameBufferEmulation.enable != 0) {
	ColorBufferToRDRAM::get().init();
	DepthBufferToRDRAM::get().init();
	RDRAMtoColorBuffer::get().init();
	}
}

void FrameBuffer_Destroy()
{
	RDRAMtoColorBuffer::get().destroy();
	DepthBufferToRDRAM::get().destroy();
	ColorBufferToRDRAM::get().destroy();
	frameBufferList().destroy();
}

void FrameBufferList::_renderScreenSizeBuffer()
{
	if (m_list.empty())
		return;

	DisplayWindow & wnd = dwnd();
	GraphicsDrawer & drawer = wnd.getDrawer();
	FrameBuffer *pBuffer = &m_list.back();
	FrameBuffer * pFilteredBuffer = gli_pp_run(pBuffer);
	CachedTexture * pBufferTexture = pFilteredBuffer->m_pTexture;

	const u32 wndWidth = wnd.getWidth();
	const u32 wndHeight = wnd.getHeight();
	s32 srcCoord[4] = { 0, 0, static_cast<s32>(wndWidth), static_cast<s32>(wndHeight) };

	const u32 screenWidth = wnd.getScreenWidth();
	const u32 screenHeight = wnd.getScreenHeight();
	const u32 wndHeightOffset = wnd.getHeightOffset();
	const s32 hOffset = (screenWidth - wndWidth) / 2;
	const s32 vOffset = (screenHeight - wndHeight) / 2 + wndHeightOffset;
	s32 dstCoord[4] = { hOffset, vOffset, hOffset + static_cast<s32>(wndWidth), vOffset + static_cast<s32>(wndHeight) };

	gfxContext.bindFramebuffer(bufferTarget::DRAW_FRAMEBUFFER, ObjectHandle::defaultFramebuffer);

	gfxContext.clearColorBuffer(0.0f, 0.0f, 0.0f, 0.0f);

	TextureParam filter = textureParameters::FILTER_LINEAR;

	GraphicsDrawer::BlitOrCopyRectParams blitParams;
	blitParams.srcX0 = srcCoord[0];
	blitParams.srcY0 = srcCoord[3];
	blitParams.srcX1 = srcCoord[2];
	blitParams.srcY1 = srcCoord[1];
	blitParams.srcWidth = wndWidth;
	blitParams.srcHeight = wndHeight;
	blitParams.dstX0 = dstCoord[0];
	blitParams.dstY0 = dstCoord[1];
	blitParams.dstX1 = dstCoord[2];
	blitParams.dstY1 = dstCoord[3];
	blitParams.dstWidth = screenWidth;
	blitParams.dstHeight = screenHeight + wndHeightOffset;
	blitParams.filter = filter;
	blitParams.mask = blitMask::COLOR_BUFFER;
	blitParams.tex[0] = pBufferTexture;
	blitParams.combiner = CombinerInfo::get().getTexrectCopyProgram();
	blitParams.readBuffer = pFilteredBuffer->m_FBO;

	drawer.blitOrCopyTexturedRect(blitParams);

	gfxContext.bindFramebuffer(bufferTarget::READ_FRAMEBUFFER, ObjectHandle::defaultFramebuffer);

	if (config.stereo.mode != stereoOff)
		gli_gSPStereoEndFrame();
	wnd.swapBuffers();
	gfxContext.bindFramebuffer(bufferTarget::DRAW_FRAMEBUFFER, pBuffer->m_FBO);
	if (config.frameBufferEmulation.forceDepthBufferClear != 0) {
		drawer.clearDepthBuffer();
	}
	gDP.changed |= CHANGED_SCISSOR;
}

s32 FrameBufferList::OverscanBuffer::getHOffset() const
{
	if (m_enabled)
		return 0;

	return m_hOffset;
}

s32 FrameBufferList::OverscanBuffer::getVOffset() const
{
	if (m_enabled)
		return 0;

	return m_vOffset;
}

f32 FrameBufferList::OverscanBuffer::getScaleX() const
{
	if (m_enabled)
		return m_scale;
	return dwnd().getScaleX();
}

f32 FrameBufferList::OverscanBuffer::getScaleY(u32 _fullHeight) const
{
	if (m_enabled)
		return m_scale;

	return (float)dwnd().getHeight() / float(_fullHeight);
}

void FrameBufferList::OverscanBuffer::init()
{
	m_enabled = config.frameBufferEmulation.enableOverscan != 0;
	if (m_enabled)
		m_FBO = gfxContext.createFramebuffer();

	DisplayWindow & wnd = dwnd();
	m_hOffset = (wnd.getScreenWidth() - wnd.getWidth()) / 2;
	m_vOffset = (wnd.getScreenHeight() - wnd.getHeight()) / 2;
	m_scale = wnd.getScaleX();
	m_drawingWidth = wnd.getWidth();
	m_bufferWidth = wnd.getScreenWidth();
	m_bufferHeight = wnd.getScreenHeight() + wnd.getHeightOffset();
}

void FrameBufferList::OverscanBuffer::destroy()
{
	gfxContext.deleteFramebuffer(m_FBO);
	m_FBO = graphics::ObjectHandle::null;
	textureCache().removeFrameBufferTexture(m_pTexture);
	m_pTexture = nullptr;
}

void FrameBufferList::OverscanBuffer::setInputBuffer(const FrameBuffer *  _pBuffer)
{
	if (!m_enabled) {
		return;
	}

	if (m_pTexture != nullptr &&
		m_pTexture->width == _pBuffer->m_pTexture->width &&
		m_pTexture->height == _pBuffer->m_pTexture->height &&
		m_scale == _pBuffer->m_scale) {
		return;
	}

	textureCache().removeFrameBufferTexture(m_pTexture);
	m_pTexture = textureCache().addFrameBufferTexture(textureTarget::TEXTURE_2D);
	const CachedTexture * pSrcTexture = _pBuffer->m_pTexture;
	_initFrameBufferTexture(0,
		_pBuffer->m_width,
		gli_VI_GetMaxBufferHeight(_pBuffer->m_width),
		_pBuffer->m_scale,
		pSrcTexture->format,
		pSrcTexture->size,
		m_pTexture);
	_setAndAttachBufferTexture(m_FBO, m_pTexture, 0, false);
	m_scale = _pBuffer->m_scale;
	m_drawingWidth = m_bufferWidth = m_pTexture->width;
	m_bufferHeight = m_pTexture->height;
}

void FrameBufferList::OverscanBuffer::activate()
{
	if (!m_enabled) {
		gfxContext.bindFramebuffer(bufferTarget::DRAW_FRAMEBUFFER, ObjectHandle::defaultFramebuffer);
		return;
	}

	gfxContext.bindFramebuffer(bufferTarget::DRAW_FRAMEBUFFER, m_FBO);
}

void FrameBufferList::OverscanBuffer::draw(u32 _fullHeight, bool _PAL, s32 _x, u32 _windowWidth, bool _clear)
{
	if (!m_enabled)
		return;

	DisplayWindow & wnd = dwnd();
	GraphicsDrawer & drawer = wnd.getDrawer();

	gfxContext.bindFramebuffer(bufferTarget::DRAW_FRAMEBUFFER, ObjectHandle::defaultFramebuffer);
	GraphicsDrawer::BlitOrCopyRectParams blitParams;
	const auto & overscan = _PAL ? config.frameBufferEmulation.overscanPAL : config.frameBufferEmulation.overscanNTSC;
	const s32 left = static_cast<s32>(overscan.left * m_scale);
	const s32 right = static_cast<s32>(overscan.right * m_scale);
	const s32 top = static_cast<s32>(overscan.top * m_scale);
	const s32 bottom = static_cast<s32>(overscan.bottom * m_scale);
	blitParams.srcX0 = left;
	blitParams.srcY0 = static_cast<s32>(_fullHeight * m_scale) - bottom;
	blitParams.srcX1 = m_bufferWidth - right;
	blitParams.srcY1 = top;
	blitParams.srcWidth = m_pTexture->realWidth;
	blitParams.srcHeight = m_pTexture->realHeight;
	blitParams.dstX0 = _x + m_hOffset;
	blitParams.dstY0 = m_vOffset + wnd.getHeightOffset();
	blitParams.dstX1 = _x + m_hOffset + wnd.getWidth();
	blitParams.dstY1 = m_vOffset + wnd.getHeight() + wnd.getHeightOffset();
	blitParams.dstWidth = _windowWidth;
	blitParams.dstHeight = wnd.getScreenHeight() + wnd.getHeightOffset();
	blitParams.filter = textureParameters::FILTER_LINEAR;
	blitParams.mask = blitMask::COLOR_BUFFER;
	blitParams.tex[0] = m_pTexture;
	blitParams.combiner = CombinerInfo::get().getTexrectCopyProgram();
	blitParams.readBuffer = m_FBO;
	blitParams.invertY = false;

	if (_clear)
		gfxContext.clearColorBuffer(0.0f, 0.0f, 0.0f, 0.0f);
	drawer.copyTexturedRect(blitParams);
}

extern "C" uint32_t RemoveFBBlackBars;
void FrameBufferList::renderBuffer()
{
	if (config.frameBufferEmulation.enable == 0) {
		_renderScreenSizeBuffer();
		return;
	}

	RdpUpdateResult rdpRes;
	if (!gli_vi_update(&m_rdpUpdate, &rdpRes)) {
		gfxContext.bindFramebuffer(bufferTarget::DRAW_FRAMEBUFFER, ObjectHandle::defaultFramebuffer);
		gfxContext.clearColorBuffer(0.0f, 0.0f, 0.0f, 0.0f);
		dwnd().swapBuffers();
		if (m_pCurrent != nullptr)
			gfxContext.bindFramebuffer(bufferTarget::DRAW_FRAMEBUFFER, m_pCurrent->m_FBO);
		return;
	}

	if (RemoveFBBlackBars && !rdpRes.vi_ispal)
	{
		rdpRes.vi_vres = 240;
		rdpRes.vi_minhpass = 0;
		rdpRes.vi_maxhpass = 0;
		rdpRes.vi_v_start = 0;
	}

	FrameBuffer *pBuffer = findBuffer(rdpRes.vi_origin);
	if (pBuffer == nullptr)
		return;
	pBuffer->m_isMainBuffer = true;
	m_overscan.setInputBuffer(pBuffer);

	DisplayWindow & wnd = dwnd();
	GraphicsDrawer & drawer = wnd.getDrawer();
	const u32 vFullHeight = rdpRes.vi_ispal ? 288 : 240;

	gli_vi_source_rect src;
	gli_vi_source(&rdpRes, pBuffer->m_startAddress, pBuffer->m_width, pBuffer->m_height,
		pBuffer->m_size, pBuffer->m_scale, pBuffer->m_pTexture->realHeight, &src);

	const u32 stride = pBuffer->m_width << pBuffer->m_size >> 1;
	FrameBuffer *pNextBuffer = findBuffer(rdpRes.vi_origin + stride * min(u32(src.srcHeight) - 1, pBuffer->m_height - 1) - 1);
	if (pNextBuffer == pBuffer)
		pNextBuffer = nullptr;

	/* post-processing keeps the buffer's scale and size, so the presented
	 * area is measured on it */
	gli_vi_present_in in;
	in.bufScale = pBuffer->m_scale;
	in.texSize[0] = pBuffer->m_pTexture->realWidth;
	in.texSize[1] = pBuffer->m_pTexture->realHeight;
	in.windowWidth = wnd.getWidth();
	in.drawingWidth = m_overscan.getDrawingWidth();
	in.offset[0] = m_overscan.getHOffset();
	in.offset[1] = m_overscan.getVOffset();
	in.dstScaleY = m_overscan.getScaleY(vFullHeight);
	in.bufferSize[0] = m_overscan.getBufferWidth();
	in.bufferSize[1] = m_overscan.getBufferHeight();
	in.hasNext = pNextBuffer != nullptr;
	in.immersive = config.stereo.immersive != 0 && !gli_gSPImmersiveIsFlat();
	gli_vi_present_out rects;
	if (!gli_vi_present_rects(&rdpRes, &src, &in, &rects)) {
		removeBuffer(pBuffer->m_startAddress);
		return;
	}
	const s32 * srcCoord = rects.src;
	const s32 * dstCoord = rects.dst;
	const s32 hOffset = in.offset[0];

	// Stereoscopic 3D: GLideN64 draws for one eye's window; each eye is presented into its half
	const u32 eyes = config.stereo.mode == stereoBothEyes ? 2U : 1U;
	const s32 eyeWidth = static_cast<s32>(wnd.getScreenWidth());
	const bool overscan = config.frameBufferEmulation.enableOverscan != 0;
	for (u32 eye = 0; eye < eyes; ++eye) {
		const s32 eyeOffset = static_cast<s32>(eye) * eyeWidth;
		const s32 dstOffset = overscan ? 0 : eyeOffset;
		const u32 dstWidth = overscan ? m_overscan.getBufferWidth() : m_overscan.getBufferWidth() * eyes;
		FrameBuffer * pSource = pBuffer;
		FrameBuffer * pEyeBuffer;
		CachedTexture * pEyeTexture;
		ObjectHandle readBuffer;

		// The left eye from the copy its pass left, through the same post-processing
		if (eyes == 2 && eye == 0) {
			FrameBuffer * pLeftEye = gli_StereoFrames_leftEye(pBuffer);
			if (pLeftEye != nullptr)
				pSource = pLeftEye;
		}
		pEyeBuffer = gli_pp_run(pSource);
		if (pEyeBuffer->m_pTexture->frameBufferTexture == CachedTexture::fbMultiSample) {
			pEyeBuffer->resolveMultisampledTexture(true);
			readBuffer = pEyeBuffer->m_resolveFBO;
			pEyeTexture = pEyeBuffer->m_pResolveTexture;
		} else {
			readBuffer = pEyeBuffer->m_FBO;
			pEyeTexture = pEyeBuffer->m_pTexture;
		}

		m_overscan.activate();
		// The window holds both eyes: only the first clears it
		if (overscan || eye == 0)
			gfxContext.clearColorBuffer(0.0f, 0.0f, 0.0f, 0.0f);

		GraphicsDrawer::BlitOrCopyRectParams blitParams;
		blitParams.srcX0 = srcCoord[0];
		blitParams.srcY0 = srcCoord[1];
		blitParams.srcX1 = srcCoord[2];
		blitParams.srcY1 = srcCoord[3];
		blitParams.srcWidth = pEyeTexture->realWidth;
		blitParams.srcHeight = pEyeTexture->realHeight;
		blitParams.dstX0 = dstCoord[0] + dstOffset;
		blitParams.dstY0 = dstCoord[1];
		blitParams.dstX1 = dstCoord[2] + dstOffset;
		blitParams.dstY1 = dstCoord[3];
		blitParams.dstWidth = dstWidth;
		blitParams.dstHeight = m_overscan.getBufferHeight();
		blitParams.filter = textureParameters::FILTER_LINEAR;
		blitParams.mask = blitMask::COLOR_BUFFER;
		blitParams.tex[0] = pEyeTexture;
		blitParams.combiner = CombinerInfo::get().getTexrectCopyProgram();
		blitParams.readBuffer = readBuffer;
		blitParams.invertY = !overscan;

		drawer.copyTexturedRect(blitParams);

		if (pNextBuffer != nullptr) {
			pNextBuffer->m_isMainBuffer = true;
			pSource = pNextBuffer;
			if (eyes == 2 && eye == 0) {
				FrameBuffer * pLeftEye = gli_StereoFrames_leftEye(pNextBuffer);
				if (pLeftEye != nullptr)
					pSource = pLeftEye;
			}
			pEyeBuffer = gli_pp_run(pSource);
			if (pEyeBuffer->m_pTexture->frameBufferTexture == CachedTexture::fbMultiSample) {
				pEyeBuffer->resolveMultisampledTexture();
				readBuffer = pEyeBuffer->m_resolveFBO;
				pEyeTexture = pEyeBuffer->m_pResolveTexture;
			} else {
				readBuffer = pEyeBuffer->m_FBO;
				pEyeTexture = pEyeBuffer->m_pTexture;
			}
			// The post-processors draw into their own buffer
			m_overscan.activate();

			blitParams.srcY0 = 0;
			blitParams.srcY1 = min(rects.nextSrcY1, (s32)pEyeBuffer->m_pTexture->realHeight);
			blitParams.srcWidth = pEyeTexture->realWidth;
			blitParams.srcHeight = pEyeTexture->realHeight;
			blitParams.dstY0 = rects.nextDstY[0];
			blitParams.dstY1 = rects.nextDstY[1];
			blitParams.tex[0] = pEyeTexture;
			blitParams.readBuffer = readBuffer;

			drawer.copyTexturedRect(blitParams);
		}

		gfxContext.bindFramebuffer(bufferTarget::READ_FRAMEBUFFER, ObjectHandle::defaultFramebuffer);
		m_overscan.draw(vFullHeight, rdpRes.vi_ispal, eyeOffset, static_cast<u32>(eyeWidth) * eyes, eye == 0);
	}

	if (config.stereo.mode != stereoOff)
		gli_gSPStereoEndFrame();
	// Immersive mode: tell the headset which head pose this picture was drawn for
	gli_gSPImmersivePresent(pBuffer->m_startAddress);
	wnd.swapBuffers();
	if (m_pCurrent != nullptr) {
		gfxContext.bindFramebuffer(bufferTarget::DRAW_FRAMEBUFFER, m_pCurrent->m_FBO);
	}
	/* Resident Evil 2 draws its characters with the depth test on and never
	 * clears the depth buffer, so from the second frame on every character
	 * fails GL_LESS against its own previous depth and only the 2D quads
	 * reach the screen. Clear it between frames, as forceDepthBufferClear
	 * does. The test is on the hack rather than on the config field because
	 * the core reloads its options after ROM load and resets the
	 * frameBufferEmulation fields, while generalEmulation.hacks survives.
	 * Excluded whenever the depth map lives in what would be cleared: under
	 * N64DepthCompare the N64 depth image textures back the background
	 * occlusion, and under enableFragmentDepthWrite the map is written into
	 * the GL depth attachment itself. */
	if (config.frameBufferEmulation.forceDepthBufferClear != 0 ||
	    ((config.generalEmulation.hacks & hack_RE2) != 0 &&
	     config.frameBufferEmulation.N64DepthCompare == 0 &&
	     config.generalEmulation.enableFragmentDepthWrite == 0)) {
		drawer.clearDepthBuffer();
	}

	const s32 X = hOffset;
	const s32 Y = wnd.getHeightOffset();
	const s32 W = wnd.getWidth();
	const s32 H = wnd.getHeight();

	gfxContext.setScissor(X, Y, W, H);
	gDP.changed |= CHANGED_SCISSOR;
}

void FrameBufferList::fillRDRAM(s32 ulx, s32 uly, s32 lrx, s32 lry)
{
	if (m_pCurrent == nullptr)
		return;

	if (config.frameBufferEmulation.copyFromRDRAM !=0 && !m_pCurrent->m_isDepthBuffer)
		// Do not write to RDRAM color buffer if copyFromRDRAM enabled.
		return;

	s32 rect[4];
	gli_fb_rdram_fill(ulx, uly, lrx, lry, rect);
	m_pCurrent->setBufferClearParams(gDP.fillColor.color, rect[0], rect[1], rect[2], rect[3]);
}

void FrameBuffer_ActivateBufferTexture(u32 t, u32 _frameBufferAddress)
{
	FrameBuffer * pBuffer = frameBufferList().getBuffer(_frameBufferAddress);
	if (pBuffer == nullptr)
		return;

	CachedTexture *pTexture = pBuffer->getTexture(t);
	if (pTexture == nullptr)
		return;

//	frameBufferList().renderBuffer(pBuffer->m_startAddress);
	textureCache().activateTexture(t, pTexture);
	gDP.changed |= CHANGED_FB_TEXTURE;
}

void FrameBuffer_ActivateBufferTextureBG(u32 t, u32 _frameBufferAddress)
{
	FrameBuffer * pBuffer = frameBufferList().getBuffer(_frameBufferAddress);
	if (pBuffer == nullptr)
		return;

	CachedTexture *pTexture = pBuffer->getTextureBG(t);
	if (pTexture == nullptr)
		return;

//	frameBufferList().renderBuffer(pBuffer->m_startAddress);
	textureCache().activateTexture(t, pTexture);
	gDP.changed |= CHANGED_FB_TEXTURE;
}

void FrameBuffer_CopyToRDRAM(u32 _address, bool _sync)
{
	ColorBufferToRDRAM::get().copyToRDRAM(_address, _sync);
}

void FrameBuffer_CopyChunkToRDRAM(u32 _address)
{
	ColorBufferToRDRAM::get().copyChunkToRDRAM(_address);
}

bool FrameBuffer_CopyDepthBuffer( u32 address )
{
	FrameBufferList & fblist = frameBufferList();
	FrameBuffer * pCopyBuffer = fblist.getCopyBuffer();
	if (pCopyBuffer != nullptr) {
		// This code is mainly to emulate Zelda MM camera.
		ColorBufferToRDRAM::get().copyToRDRAM(pCopyBuffer->m_startAddress, true);
		pCopyBuffer->m_RdramCopySize = 0; // To disable validity check by RDRAM content. CPU may change content of the buffer for some unknown reason.
		fblist.setCopyBuffer(nullptr);
		return true;
	}

	if (DepthBufferToRDRAM::get().copyToRDRAM(address)) {
		fblist.depthBufferCopyRdram();
		return true;
	}

	return false;
}

bool FrameBuffer_CopyDepthBufferChunk(u32 address)
{
	return DepthBufferToRDRAM::get().copyChunkToRDRAM(address);
}

void FrameBuffer_CopyFromRDRAM(u32 _address, bool _bCFB)
{
	RDRAMtoColorBuffer::get().copyFromRDRAM(_address, _bCFB);
}

void FrameBuffer_AddAddress(u32 address, u32 _size)
{
	RDRAMtoColorBuffer::get().addAddress(address, _size);
}

void calcCoordsScales(const FrameBuffer * _pBuffer, f32 & _scaleX, f32 & _scaleY)
{
	const u32 bufferWidth = _pBuffer != nullptr ? _pBuffer->m_width : VI.width;
	const u32 bufferHeight = gli_VI_GetMaxBufferHeight(bufferWidth);
	_scaleX = 1.0f / f32(bufferWidth);
	_scaleY = 1.0f / f32(bufferHeight);
}

/* gDP's way into the list */
void gli_fb_set_color_image(u32 _address, u32 _format, u32 _size, u32 _width)
{
	frameBufferList().saveBuffer(_address, (u16)_format, (u16)_size, (u16)_width, false);
}

void gli_fb_fill_rdram(s32 _ulx, s32 _uly, s32 _lrx, s32 _lry)
{
	frameBufferList().fillRDRAM(_ulx, _uly, _lrx, _lry);
}

void gli_fb_set_changed(f32 _maxY)
{
	frameBufferList().setBufferChanged(_maxY);
}

void gli_fb_stereo_sync(void)
{
	dwnd().getDrawer().flush();
	frameBufferList().updateCurrentBufferEndAddress();
}

void gli_fb_full_sync(bool _lle)
{
	if (config.frameBufferEmulation.copyAuxToRDRAM != 0) {
		frameBufferList().copyAux();
		frameBufferList().removeAux();
	}

	dwnd().getDrawer().flush();

	frameBufferList().updateCurrentBufferEndAddress();

	FrameBuffer * pCurrentBuffer = frameBufferList().getCurrent();
	if ((config.frameBufferEmulation.copyToRDRAM != ctDisable || (config.generalEmulation.hacks & hack_subscreen) != 0) &&
		!gli_FBInfo_supported() &&
		pCurrentBuffer != nullptr &&
		!pCurrentBuffer->isAuxiliary()
	)
		FrameBuffer_CopyToRDRAM(gDP.colorImage.address, config.frameBufferEmulation.copyToRDRAM == ctSync);

	if (_lle) {
		if (config.frameBufferEmulation.copyDepthToRDRAM != cdDisable && !gli_FBInfo_supported())
			FrameBuffer_CopyDepthBuffer(gDP.colorImage.address);
	}
}

bool gli_fb_check_texture(u32 _address, u32 _width, u32 _bytes)
{
	gDP.loadTile->textureMode = TEXTUREMODE_NORMAL;
	gDP.loadTile->frameBufferAddress = 0U;
	gDP.changed |= CHANGED_TMEM;
	if (!config.frameBufferEmulation.enable)
		return false;

	FrameBufferList & fbList = frameBufferList();
	FrameBuffer *pBuffer = fbList.findBuffer(_address);
	bool bRes = pBuffer != nullptr && pBuffer->m_readable;
	while (bRes) {
		if ((config.generalEmulation.hacks & hack_blurPauseScreen) != 0) {
			if (gDP.colorImage.address == gDP.depthImageAddress && pBuffer->m_copiedToRdram) {
				memcpy(RDRAM + gDP.depthImageAddress,
					RDRAM + pBuffer->m_startAddress,
					(pBuffer->m_width*pBuffer->m_height) << pBuffer->m_size >> 1);
				pBuffer->m_copiedToRdram = false;
				fbList.getCurrent()->m_isPauseScreen = true;
			}
			if (pBuffer->m_isPauseScreen) {
				bRes = false;
				break;
			}
		}

		if (gDP.otherMode.textureLUT == G_TT_RGBA16 && (config.generalEmulation.hacks & hack_StarCraftBackgrounds) != 0) {
			bRes = false;
			break;
		}

		if (pBuffer->m_cfb) {
			fbList.removeBuffer(pBuffer->m_startAddress);
			bRes = false;
			break;
		}

		if ((config.generalEmulation.hacks & hack_noDepthFrameBuffers) != 0 && pBuffer->m_isDepthBuffer) {
			fbList.removeBuffer(pBuffer->m_startAddress);
			bRes = false;
			break;
		}

		const u32 texEndAddress = _address + _bytes - 1;
		if (_address > pBuffer->m_startAddress &&
			pBuffer->m_width != _width &&
			texEndAddress > (pBuffer->m_endAddress + (pBuffer->m_width << pBuffer->m_size >> 1))) {
			//fbList.removeBuffer(pBuffer->m_startAddress);
			bRes = false;
			break;
		}

		if (gDP.loadTile->loadType == LOADTYPE_TILE &&
			gDP.textureImage.width != pBuffer->m_width &&
			gDP.textureImage.size != pBuffer->m_size) {
			//fbList.removeBuffer(pBuffer->m_startAddress); // Does not work with Zelda MM
			bRes = false;
			break;
		}

		bRes = pBuffer->isValid(false);
		if (!bRes && pBuffer != fbList.getCurrent()) {
			fbList.removeBuffer(pBuffer->m_startAddress);
			break;
		}

		pBuffer->m_loadType = gDP.loadTile->loadType;
		pBuffer->m_loadTileOrigin.uls = gDP.loadTile->uls;
		pBuffer->m_loadTileOrigin.ult = gDP.loadTile->ult;
		gDP.loadTile->frameBufferAddress = pBuffer->m_startAddress;
		gDP.loadTile->textureMode = TEXTUREMODE_FRAMEBUFFER;
		break;
	}

	for (int nTile = gSP.texture.tile; nTile < 6; ++nTile) {
		if (gDP.tiles[nTile].tmem == gDP.loadTile->tmem) {
			gDPTile & curTile = gDP.tiles[nTile];
			curTile.textureMode = gDP.loadTile->textureMode;
			curTile.loadType = gDP.loadTile->loadType;
			curTile.frameBufferAddress = gDP.loadTile->frameBufferAddress;
			curTile.imageAddress = gDP.loadTile->imageAddress;
		}
	}
	return bRes;
}

/* VI's way in */
FrameBuffer * gli_fb_find(u32 _address)
{
	return frameBufferList().findBuffer(_address);
}

u32 gli_fb_width(const FrameBuffer * _pBuffer)
{
	return _pBuffer->m_width;
}

bool gli_fb_is_valid_scanout(FrameBuffer * _pBuffer)
{
	return _pBuffer->isValid(true);
}

bool gli_fb_rendered_uncopied(const FrameBuffer * _pBuffer)
{
	return !_pBuffer->m_cfb && !_pBuffer->m_copiedToRdram;
}

void gli_fb_copy_rdram(FrameBuffer * _pBuffer)
{
	_pBuffer->copyRdram();
}

bool gli_fb_depth_width_differs(u32 _address, u32 _width)
{
	FrameBuffer * pBuffer = frameBufferList().findBuffer(_address);
	DepthBuffer * pDepthBuffer = pBuffer != nullptr ? pBuffer->m_pDepthBuffer : nullptr;
	return pDepthBuffer != nullptr && pDepthBuffer->m_width != _width;
}

void gli_fb_vi_resized(u32 _widthPrev, u32 _width)
{
	frameBufferList().removeBuffers(_widthPrev);
	frameBufferList().removeBuffers(_width);
	gli_depth_list_destroy();
	gli_depth_list_init();
}

void gli_fb_save_cpu_buffer(u32 _address, u32 _size, u32 _width)
{
	frameBufferList().saveBuffer(_address, G_IM_FMT_RGBA, _size, _width, true);
}

void gli_fb_copy_from_rdram(u32 _address, bool _useAlpha)
{
	FrameBuffer_CopyFromRDRAM(_address, _useAlpha);
}

void gli_fb_render(void)
{
	frameBufferList().renderBuffer();
}

void gli_fb_clear_changed(void)
{
	frameBufferList().clearBuffersChanged();
}

FrameBuffer * gli_fb_current(void)
{
	return frameBufferList().getCurrent();
}

u32 gli_fb_start_address(const FrameBuffer * _pBuffer)
{
	return _pBuffer->m_startAddress;
}

void gli_fb_make_current(u32 _address)
{
	FrameBuffer * pBuffer = frameBufferList().findBuffer(_address);
	if (pBuffer != nullptr) {
		frameBufferList().setCurrent(pBuffer);
		frameBufferList().setCurrentDrawBuffer();
	}
}

bool gli_fb_copy_depth(u32 _address)
{
	return FrameBuffer_CopyDepthBuffer(_address);
}

bool gli_fb_is_cpu(const FrameBuffer * _pBuffer)
{
	return _pBuffer->m_cfb;
}

void gli_fb_remove(u32 _address)
{
	frameBufferList().removeBuffer(_address);
}

void gli_fb_bind_draw(const FrameBuffer * _pBuffer)
{
	gfxContext.bindFramebuffer(bufferTarget::DRAW_FRAMEBUFFER, _pBuffer->m_FBO);
}

void gli_fb_set_current_draw_buffer(void)
{
	frameBufferList().setCurrentDrawBuffer();
}

void gli_fb_add_address(u32 _address, u32 _size)
{
	FrameBuffer_AddAddress(_address, _size);
}

bool gli_fb_is_depth(const FrameBuffer * _pBuffer)
{
	return _pBuffer->m_isDepthBuffer;
}

void gli_fb_copy_depth_chunk(u32 _address)
{
	FrameBuffer_CopyDepthBufferChunk(_address);
}

void gli_fb_copy_chunk(u32 _address)
{
	FrameBuffer_CopyChunkToRDRAM(_address);
}

void gli_fb_copy_to_rdram(u32 _address, bool _sync)
{
	FrameBuffer_CopyToRDRAM(_address, _sync);
}

void gli_fb_fill_info(void * _pinfo, u32 _size)
{
	frameBufferList().fillBufferInfo(_pinfo, _size);
}

bool gli_depth_current(u32 * _address, u32 * _width)
{
	const DepthBuffer * pDepthBuffer = gli_depth_current_buffer();
	if (pDepthBuffer == nullptr)
		return false;
	*_address = pDepthBuffer->m_address;
	*_width = pDepthBuffer->m_width;
	return true;
}

/* the stereo left eye: a copy of the buffer's color image */
static
FrameBuffer * _createStereoBuffer(const FrameBuffer * _pMainBuffer)
{
	FrameBuffer * pLeftEye = new FrameBuffer();
	const CachedTexture * pMainTexture = _pMainBuffer->m_pTexture;
	CachedTexture * pTexture = pLeftEye->m_pTexture;
	Context::InitTextureParams initParams;
	Context::TexParameters setParams;
	Context::FrameBufferRenderTarget bufTarget;

	pLeftEye->m_startAddress = _pMainBuffer->m_startAddress;
	pLeftEye->m_width = _pMainBuffer->m_width;
	pLeftEye->m_height = _pMainBuffer->m_height;
	pLeftEye->m_size = _pMainBuffer->m_size;
	pLeftEye->m_scale = _pMainBuffer->m_scale;

	pTexture->format = G_IM_FMT_RGBA;
	pTexture->size = pMainTexture->size;
	pTexture->address = pMainTexture->address;
	pTexture->clampS = 1;
	pTexture->clampT = 1;
	pTexture->clampWidth = pMainTexture->clampWidth;
	pTexture->clampHeight = pMainTexture->clampHeight;
	pTexture->frameBufferTexture = CachedTexture::fbOneSample;
	pTexture->maskS = 0;
	pTexture->maskT = 0;
	pTexture->mirrorS = 0;
	pTexture->mirrorT = 0;
	pTexture->width = pMainTexture->width;
	pTexture->height = pMainTexture->height;
	pTexture->realWidth = pMainTexture->realWidth;
	pTexture->realHeight = pMainTexture->realHeight;
	pTexture->textureBytes = pTexture->realWidth * pTexture->realHeight * 4;

	initParams.handle = pTexture->name;
	initParams.width = pTexture->realWidth;
	initParams.height = pTexture->realHeight;
	initParams.internalFormat = gfxContext.convertInternalTextureFormat(u32(internalcolorFormat::RGBA8));
	initParams.format = colorFormat::RGBA;
	initParams.dataType = datatype::UNSIGNED_BYTE;
	gfxContext.init2DTexture(initParams);

	setParams.handle = pTexture->name;
	setParams.target = textureTarget::TEXTURE_2D;
	setParams.minFilter = textureParameters::FILTER_LINEAR;
	setParams.magFilter = textureParameters::FILTER_LINEAR;
	gfxContext.setTextureParameters(setParams);

	bufTarget.bufferHandle = pLeftEye->m_FBO;
	bufTarget.bufferTarget = bufferTarget::DRAW_FRAMEBUFFER;
	bufTarget.attachment = bufferAttachment::COLOR_ATTACHMENT0;
	bufTarget.textureTarget = textureTarget::TEXTURE_2D;
	bufTarget.textureHandle = pTexture->name;
	gfxContext.addFrameBufferRenderTarget(bufTarget);
	return pLeftEye;
}

bool gli_fb_stereo_copy(FrameBuffer * _pBuffer, FrameBuffer ** _ppLeftEye)
{
	const CachedTexture * pSource = _pBuffer->m_pTexture;
	FrameBuffer * pLeftEye = *_ppLeftEye;
	bool copied;
	ObjectHandle readBuffer;
	Context::BlitFramebuffersParams params;

	if (pLeftEye == nullptr ||
		pLeftEye->m_pTexture->realWidth != pSource->realWidth ||
		pLeftEye->m_pTexture->realHeight != pSource->realHeight) {
		delete pLeftEye;
		pLeftEye = _createStereoBuffer(_pBuffer);
		*_ppLeftEye = pLeftEye;
	}

	readBuffer = _pBuffer->m_FBO;
	if (pSource->frameBufferTexture == CachedTexture::fbMultiSample) {
		_pBuffer->resolveMultisampledTexture(true);
		readBuffer = _pBuffer->m_resolveFBO;
	}

	params.readBuffer = readBuffer;
	params.drawBuffer = pLeftEye->m_FBO;
	params.srcX0 = 0;
	params.srcY0 = 0;
	params.srcX1 = pSource->realWidth;
	params.srcY1 = pSource->realHeight;
	params.dstX0 = 0;
	params.dstY0 = 0;
	params.dstX1 = pLeftEye->m_pTexture->realWidth;
	params.dstY1 = pLeftEye->m_pTexture->realHeight;
	params.mask = blitMask::COLOR_BUFFER;
	params.filter = textureParameters::FILTER_NEAREST;

	copied = gfxContext.blitFramebuffers(params);

	gfxContext.bindFramebuffer(bufferTarget::DRAW_FRAMEBUFFER, ObjectHandle::defaultFramebuffer);
	return copied;
}

void gli_fb_stereo_delete(FrameBuffer * _pBuffer)
{
	delete _pBuffer;
}

bool gli_fb_has_texture(const FrameBuffer * _pBuffer)
{
	return _pBuffer->m_pTexture != nullptr;
}

/* post-processing */
FrameBuffer * gli_fb_pp_create(const FrameBuffer * _pMainBuffer)
{
	FrameBuffer * pResultBuffer = new FrameBuffer();
	pResultBuffer->m_width = _pMainBuffer->m_width;
	pResultBuffer->m_height = _pMainBuffer->m_height;
	pResultBuffer->m_scale = _pMainBuffer->m_scale;

	const CachedTexture * pMainTexture = _pMainBuffer->m_pTexture;
	CachedTexture * pTexture = pResultBuffer->m_pTexture;
	pTexture->format = G_IM_FMT_RGBA;
	pTexture->clampS = 1;
	pTexture->clampT = 1;
	pTexture->frameBufferTexture = CachedTexture::fbOneSample;
	pTexture->maskS = 0;
	pTexture->maskT = 0;
	pTexture->mirrorS = 0;
	pTexture->mirrorT = 0;
	pTexture->realWidth = pMainTexture->realWidth;
	pTexture->realHeight = pMainTexture->realHeight;
	pTexture->textureBytes = pTexture->realWidth * pTexture->realHeight * 4;

	Context::InitTextureParams initParams;
	initParams.handle = pTexture->name;
	initParams.width = pTexture->realWidth;
	initParams.height = pTexture->realHeight;
	initParams.internalFormat = gfxContext.convertInternalTextureFormat(u32(internalcolorFormat::RGBA8));
	initParams.format = colorFormat::RGBA;
	initParams.dataType = datatype::UNSIGNED_BYTE;
	gfxContext.init2DTexture(initParams);

	Context::TexParameters setParams;
	setParams.handle = pTexture->name;
	setParams.target = textureTarget::TEXTURE_2D;
	setParams.minFilter = textureParameters::FILTER_NEAREST;
	setParams.magFilter = textureParameters::FILTER_NEAREST;
	gfxContext.setTextureParameters(setParams);

	Context::FrameBufferRenderTarget bufTarget;
	bufTarget.bufferHandle = pResultBuffer->m_FBO;
	bufTarget.bufferTarget = bufferTarget::DRAW_FRAMEBUFFER;
	bufTarget.attachment = bufferAttachment::COLOR_ATTACHMENT0;
	bufTarget.textureTarget = textureTarget::TEXTURE_2D;
	bufTarget.textureHandle = pTexture->name;
	gfxContext.addFrameBufferRenderTarget(bufTarget);
	assert(!gfxContext.isFramebufferError());
	return pResultBuffer;
}

void gli_fb_pp_draw(FrameBuffer * _pBuffer, FrameBuffer * _pResult, gli_shader * _pShader)
{
	CachedTexture * pTextureOriginal;
	if (_pBuffer->m_pTexture->frameBufferTexture == CachedTexture::fbMultiSample) {
		_pBuffer->resolveMultisampledTexture(true);
		pTextureOriginal = _pBuffer->m_pResolveTexture;
	} else
		pTextureOriginal = _pBuffer->m_pTexture;

	gfxContext.bindFramebuffer(bufferTarget::READ_FRAMEBUFFER,
		ObjectHandle::defaultFramebuffer);

	gfxContext.bindFramebuffer(bufferTarget::DRAW_FRAMEBUFFER,
		ObjectHandle(_pResult->m_FBO));

	CachedTexture * pDstTex = _pResult->m_pTexture;
	GraphicsDrawer::CopyRectParams copyParams;
	copyParams.srcX0 = 0;
	copyParams.srcY0 = 0;
	copyParams.srcX1 = pTextureOriginal->realWidth;
	copyParams.srcY1 = pTextureOriginal->realHeight;
	copyParams.srcWidth = pTextureOriginal->realWidth;
	copyParams.srcHeight = pTextureOriginal->realHeight;
	copyParams.dstX0 = 0;
	copyParams.dstY0 = 0;
	copyParams.dstX1 = pDstTex->realWidth;
	copyParams.dstY1 = pDstTex->realHeight;
	copyParams.dstWidth = pDstTex->realWidth;
	copyParams.dstHeight = pDstTex->realHeight;
	copyParams.tex[0] = pTextureOriginal;
	copyParams.filter = textureParameters::FILTER_NEAREST;
	copyParams.combiner = reinterpret_cast<ShaderProgram*>(_pShader);

	dwnd().getDrawer().copyTexturedRect(copyParams);

	gfxContext.bindFramebuffer(bufferTarget::DRAW_FRAMEBUFFER,
		ObjectHandle::defaultFramebuffer);

	gfxContext.resetShaderProgram();
}


void gli_fb_delete(FrameBuffer * _pBuffer)
{
	delete _pBuffer;
}

gli_shader * gli_gfx_create_gamma_shader(void)
{
	return reinterpret_cast<gli_shader*>(gfxContext.createGammaCorrectionShader());
}

gli_shader * gli_gfx_create_fxaa_shader(void)
{
	return reinterpret_cast<gli_shader*>(gfxContext.createFXAAShader());
}

void gli_gfx_delete_shader(gli_shader * _pShader)
{
	delete reinterpret_cast<ShaderProgram*>(_pShader);
}

void gli_fb_clear_depth_buffer(DepthBuffer * _pDepthBuffer)
{
	frameBufferList().clearDepthBuffer(_pDepthBuffer);
}

void gli_fb_attach_depth_buffer(void)
{
	frameBufferList().attachDepthBuffer();
}

void gli_fb_set_is_depth(FrameBuffer * _pBuffer)
{
	_pBuffer->m_isDepthBuffer = true;
}
