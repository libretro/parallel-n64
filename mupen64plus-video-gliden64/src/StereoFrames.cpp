#include <string.h>

#include "N64.h"
#include "StereoFrames.h"
#include "FrameBuffer.h"
#include "Config.h"

#include <Graphics/Context.h>
#include <Graphics/Parameters.h>

using namespace graphics;

/* A frame has a handful of color images; past that the oldest slot goes */
#define STEREO_FRAMES_SLOTS 16

#define STEREO_FRAME_CAPTURED      0x1U  /* the copy holds this buffer's left eye */
#define STEREO_FRAME_CAPTURED_TASK 0x2U  /* made by this task's left pass */
#define STEREO_FRAME_ENTERED_PASS  0x4U  /* entered during this left pass */

struct StereoFrameSlot
{
	FrameBuffer * pLeftEye;
	u32 address;
	u32 flags;
};

static struct StereoFrameSlot l_slots[STEREO_FRAMES_SLOTS];
static u32 l_slotCount = 0;
static u32 l_slotNext = 0;

bool g_stereoFramesCapturing = false;

static
struct StereoFrameSlot * _findSlot(u32 _address)
{
	u32 i;
	for (i = 0; i < l_slotCount; ++i) {
		if (l_slots[i].address == _address)
			return &l_slots[i];
	}
	return NULL;
}

static
struct StereoFrameSlot * _getSlot(u32 _address)
{
	struct StereoFrameSlot * pSlot = _findSlot(_address);
	if (pSlot != NULL)
		return pSlot;
	if (l_slotCount < STEREO_FRAMES_SLOTS) {
		pSlot = &l_slots[l_slotCount++];
	} else {
		pSlot = &l_slots[l_slotNext];
		l_slotNext = (l_slotNext + 1) % STEREO_FRAMES_SLOTS;
		delete pSlot->pLeftEye;
	}
	pSlot->pLeftEye = NULL;
	pSlot->address = _address;
	pSlot->flags = 0;
	return pSlot;
}

static
FrameBuffer * _createBuffer(const FrameBuffer * _pMainBuffer)
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

void StereoFrames_beginTask()
{
	u32 i;
	for (i = 0; i < l_slotCount; ++i)
		l_slots[i].flags &= ~STEREO_FRAME_CAPTURED_TASK;
}

void StereoFrames_setCapturing(bool _capturing)
{
	u32 i;
	g_stereoFramesCapturing = _capturing;
	for (i = 0; i < l_slotCount; ++i)
		l_slots[i].flags &= ~STEREO_FRAME_ENTERED_PASS;
}

void StereoFrames_noteEntered(u32 _address)
{
	struct StereoFrameSlot * pSlot;
	if (g_stereoFramesCapturing) {
		_getSlot(_address)->flags |= STEREO_FRAME_ENTERED_PASS;
		return;
	}
	pSlot = _findSlot(_address);
	if (pSlot != NULL && (pSlot->flags & STEREO_FRAME_CAPTURED_TASK) == 0)
		pSlot->flags &= ~STEREO_FRAME_CAPTURED;
}

bool StereoFrames_wasEntered(u32 _address)
{
	const struct StereoFrameSlot * pSlot = _findSlot(_address);
	return pSlot != NULL && (pSlot->flags & STEREO_FRAME_ENTERED_PASS) != 0;
}

void StereoFrames_captureLeftEye(FrameBuffer * _pBuffer)
{
	struct StereoFrameSlot * pSlot;
	const CachedTexture * pSource;
	ObjectHandle readBuffer;
	Context::BlitFramebuffersParams params;

	if (_pBuffer == NULL || _pBuffer->m_pTexture == NULL)
		return;

	pSource = _pBuffer->m_pTexture;
	pSlot = _getSlot(_pBuffer->m_startAddress);
	if (pSlot->pLeftEye == NULL ||
		pSlot->pLeftEye->m_pTexture->realWidth != pSource->realWidth ||
		pSlot->pLeftEye->m_pTexture->realHeight != pSource->realHeight) {
		delete pSlot->pLeftEye;
		pSlot->pLeftEye = _createBuffer(_pBuffer);
	}

	readBuffer = _pBuffer->m_FBO;
	if (pSource->frameBufferTexture == CachedTexture::fbMultiSample) {
		_pBuffer->resolveMultisampledTexture(true);
		readBuffer = _pBuffer->m_resolveFBO;
	}

	params.readBuffer = readBuffer;
	params.drawBuffer = pSlot->pLeftEye->m_FBO;
	params.srcX0 = 0;
	params.srcY0 = 0;
	params.srcX1 = pSource->realWidth;
	params.srcY1 = pSource->realHeight;
	params.dstX0 = 0;
	params.dstY0 = 0;
	params.dstX1 = pSlot->pLeftEye->m_pTexture->realWidth;
	params.dstY1 = pSlot->pLeftEye->m_pTexture->realHeight;
	params.mask = blitMask::COLOR_BUFFER;
	params.filter = textureParameters::FILTER_NEAREST;

	if (gfxContext.blitFramebuffers(params))
		pSlot->flags |= STEREO_FRAME_CAPTURED | STEREO_FRAME_CAPTURED_TASK;
	else
		pSlot->flags &= ~(STEREO_FRAME_CAPTURED | STEREO_FRAME_CAPTURED_TASK);

	gfxContext.bindFramebuffer(bufferTarget::DRAW_FRAMEBUFFER, ObjectHandle::defaultFramebuffer);
}

FrameBuffer * StereoFrames_leftEye(const FrameBuffer * _pBuffer)
{
	const struct StereoFrameSlot * pSlot;
	if (_pBuffer == NULL)
		return NULL;
	pSlot = _findSlot(_pBuffer->m_startAddress);
	return pSlot != NULL && (pSlot->flags & STEREO_FRAME_CAPTURED) != 0 ? pSlot->pLeftEye : NULL;
}

void StereoFrames_forget(const FrameBuffer * _pBuffer)
{
	struct StereoFrameSlot * pSlot;
	if (_pBuffer == NULL)
		return;
	pSlot = _findSlot(_pBuffer->m_startAddress);
	if (pSlot != NULL)
		pSlot->flags &= ~(STEREO_FRAME_CAPTURED | STEREO_FRAME_CAPTURED_TASK);
}

void StereoFrames_destroy()
{
	u32 i;
	for (i = 0; i < l_slotCount; ++i)
		delete l_slots[i].pLeftEye;
	memset(l_slots, 0, sizeof(l_slots));
	l_slotCount = 0;
	l_slotNext = 0;
	g_stereoFramesCapturing = false;
}
