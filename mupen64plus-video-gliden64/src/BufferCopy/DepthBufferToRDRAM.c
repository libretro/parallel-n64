/* The copy of a frame buffer's depth to RDRAM (DepthBufferToRDRAM.h).
 * C89. */
#include <assert.h>
#include <stddef.h>
#include "DepthBufferToRDRAM.h"
#include "DeferredReadback.h"
#include "../gli_rdram_write.h"

#include <gli_framebuffer.h>
#include <gli_fb_rdram.h>
#include <gli_textures.h>
#include <gli_tex.h>
#include <DepthBuffer.h>
#include <Config.h>
#include <GBI.h>
#include <N64.h>
#include <VI.h>
#include <gDP.h>
#include <Graphics/gli_context.h>
#include <Graphics/PixelBuffer.h>

#define DEPTH_TEX_WIDTH 640
#define DEPTH_TEX_HEIGHT 580

static struct
{
	u32 FBO;
	/* one read-back buffer per read in flight, all allocated at init */
	gli_prb *pbuf[GLI_READBACK_MAX_SLOTS];
	u32 numSlots;
	bool written;
	gli_readback readback;
	/* what each slot's read was for */
	gli_rdram_write geometry[GLI_READBACK_MAX_SLOTS];
	u32 frameCount;
	gli_tex *pColorTexture;
	gli_tex *pDepthTexture;
	struct FrameBuffer *pCurFrameBuffer;
} d2r = { 0, { NULL, NULL, NULL }, 1, 0, { 1, 0, { 0, 0, 0 } } , { { 0 } }, 0xFFFFFFFFu, NULL, NULL, NULL };

static void init_texture(gli_tex *_t, u32 _bytes)
{
	_t->format = G_IM_FMT_I;
	_t->size = 2;
	_t->clampS = 1;
	_t->clampT = 1;
	_t->frameBufferTexture = GLI_TEX_FB_ONE_SAMPLE;
	_t->maskS = 0;
	_t->maskT = 0;
	_t->mirrorS = 0;
	_t->mirrorT = 0;
	_t->width = DEPTH_TEX_WIDTH;
	_t->height = DEPTH_TEX_HEIGHT;
	_t->textureBytes = _t->width * _t->height * _bytes;
}

void gli_d2r_init(void)
{
	/* a read-back buffer per read in flight: the colour copy's setting
	 * (Async 2, TripleBuffer 3) leaves full copies in flight, Sync or Off
	 * keeps one, waited for */
	const u32 setting = config.frameBufferEmulation.copyToRDRAM;
	const gli_fb_formats *fbTexFormats = &gli_ctx.formats;
	gli_init_texture_params initParams = GLI_INIT_TEXTURE_PARAMS_DEFAULT;
	gli_tex_parameters setParams = GLI_TEX_PARAMETERS_DEFAULT;
	gli_fb_render_target bufTarget = GLI_FB_RENDER_TARGET_DEFAULT;
	u32 i;

	d2r.numSlots = setting > 1 ? (setting < GLI_READBACK_MAX_SLOTS ? setting : GLI_READBACK_MAX_SLOTS) : 1;
	for (i = 0; i < d2r.numSlots; ++i) {
		d2r.pbuf[i] = gli_ctx.ops->createPixelReadBuffer(gli_ctx.impl, DEPTH_TEX_WIDTH * DEPTH_TEX_HEIGHT * sizeof(float));
		if (d2r.pbuf[i] == NULL) {
			d2r.numSlots = i;
			break;
		}
	}
	gli_readback_reset(&d2r.readback, d2r.numSlots);
	if (d2r.numSlots == 0)
		return;

	d2r.pColorTexture = gli_textures_add_fb(GLI_TT_TEXTURE_2D);
	init_texture(d2r.pColorTexture, 1);
	d2r.pDepthTexture = gli_textures_add_fb(GLI_TT_TEXTURE_2D);
	init_texture(d2r.pDepthTexture, sizeof(float));

	initParams.handle = d2r.pColorTexture->name;
	initParams.textureUnitIndex = GLI_TU_TEX0;
	initParams.width = d2r.pColorTexture->width;
	initParams.height = d2r.pColorTexture->height;
	initParams.internalFormat = fbTexFormats->monochromeInternalFormat;
	initParams.format = fbTexFormats->monochromeFormat;
	initParams.dataType = fbTexFormats->monochromeType;
	gli_ctx.ops->init2DTexture(gli_ctx.impl, &initParams);

	setParams.handle = d2r.pColorTexture->name;
	setParams.target = GLI_TT_TEXTURE_2D;
	setParams.textureUnitIndex = GLI_TU_TEX0;
	setParams.minFilter = GLI_TP_FILTER_NEAREST;
	setParams.magFilter = GLI_TP_FILTER_NEAREST;
	gli_ctx.ops->setTextureParameters(gli_ctx.impl, &setParams);

	initParams.handle = d2r.pDepthTexture->name;
	initParams.width = d2r.pDepthTexture->width;
	initParams.height = d2r.pDepthTexture->height;
	initParams.internalFormat = fbTexFormats->depthInternalFormat;
	initParams.format = fbTexFormats->depthFormat;
	initParams.dataType = fbTexFormats->depthType;
	gli_ctx.ops->init2DTexture(gli_ctx.impl, &initParams);

	setParams.handle = d2r.pDepthTexture->name;
	gli_ctx.ops->setTextureParameters(gli_ctx.impl, &setParams);

	d2r.FBO = gli_ctx.ops->createFramebuffer(gli_ctx.impl);
	bufTarget.bufferHandle = d2r.FBO;
	bufTarget.bufferTarget = gli_ctx.drawFramebuffer;
	bufTarget.attachment = GLI_BA_COLOR_ATTACHMENT0;
	bufTarget.textureTarget = GLI_TT_TEXTURE_2D;
	bufTarget.textureHandle = d2r.pColorTexture->name;
	gli_ctx.ops->addFrameBufferRenderTarget(gli_ctx.impl, &bufTarget);

	bufTarget.attachment = GLI_BA_DEPTH_ATTACHMENT;
	bufTarget.textureHandle = d2r.pDepthTexture->name;
	gli_ctx.ops->addFrameBufferRenderTarget(gli_ctx.impl, &bufTarget);

	/* check if everything is OK */
	assert(!gli_ctx.ops->isFramebufferError(gli_ctx.impl));
	assert(!gli_ctx.ops->isError(gli_ctx.impl));
	gli_ctx.ops->bindFramebuffer(gli_ctx.impl, gli_ctx.drawFramebuffer, 0);
}

void gli_d2r_destroy(void)
{
	u32 i;
	if (d2r.pbuf[0] == NULL)
		return;
	/* the reads in flight are dropped with their buffers */
	gli_readback_reset(&d2r.readback, d2r.numSlots);

	gli_ctx.ops->deleteFramebuffer(gli_ctx.impl, d2r.FBO);
	d2r.FBO = 0;
	if (d2r.pColorTexture != NULL) {
		gli_textures_remove_fb(d2r.pColorTexture);
		d2r.pColorTexture = NULL;
	}
	if (d2r.pDepthTexture != NULL) {
		gli_textures_remove_fb(d2r.pDepthTexture);
		d2r.pDepthTexture = NULL;
	}
	for (i = 0; i < GLI_READBACK_MAX_SLOTS; ++i) {
		if (d2r.pbuf[i] != NULL)
			d2r.pbuf[i]->ops->destroy(d2r.pbuf[i]);
		d2r.pbuf[i] = NULL;
	}
	d2r.numSlots = 1;
}

static bool prepare_copy(u32 *_startAddress, bool _copyChunk)
{
	const u32 curFrame = gli_window_swap_count();
	struct FrameBuffer *pBuffer, *pDepthFrameBuffer;
	gli_fb *buf, *cur;
	gli_blit_params blitParams = GLI_BLIT_PARAMS_DEFAULT;
	u32 numPixels, bufferOrigin, height, readBuffer;

	if (_copyChunk && d2r.frameCount == curFrame)
		return 1;

	if ((VI.width | VI.height) == 0) /* Incorrect buffer size. Don't copy */
		return 0;

	pBuffer = gli_fb_find(*_startAddress);
	if (pBuffer == NULL)
		return 0;
	buf = gli_fb_of(pBuffer);
	if (gli_fb_is_auxiliary(buf) || buf->m_pDepthBuffer == NULL || !buf->m_pDepthBuffer->m_cleared)
		return 0;

	pDepthFrameBuffer = gli_fb_find(buf->m_pDepthBuffer->m_address);
	d2r.pCurFrameBuffer = pDepthFrameBuffer != NULL ? pDepthFrameBuffer : pBuffer;
	cur = gli_fb_of(d2r.pCurFrameBuffer);

	if (cur->m_width != buf->m_pDepthBuffer->m_width)
		return 0;

	numPixels = cur->m_width * cur->m_height;
	bufferOrigin = cur->m_pDepthBuffer->m_address;
	if (bufferOrigin + numPixels * 2 > RDRAMSize + 1)
		return 0;

	height = cutHeight(bufferOrigin, cur->m_height, cur->m_width * 2);
	if (height == 0)
		return 0;

	*_startAddress &= ~0xfff;
	if (*_startAddress < bufferOrigin)
		*_startAddress = bufferOrigin;

	readBuffer = buf->m_FBO;
	if (config.video.multisampling != 0) {
		gli_depth_resolve_texture(cur->m_pDepthBuffer, cur);
		readBuffer = cur->m_resolveFBO;
	}

	blitParams.readBuffer = readBuffer;
	blitParams.drawBuffer = d2r.FBO;
	blitParams.srcX0 = 0;
	blitParams.srcY0 = 0;
	blitParams.srcX1 = cur->m_pTexture->realWidth;
	blitParams.srcY1 = (s32)(cur->m_height * cur->m_scale);
	blitParams.dstX0 = 0;
	blitParams.dstY0 = 0;
	blitParams.dstX1 = cur->m_width;
	blitParams.dstY1 = cur->m_height;
	blitParams.mask = GLI_BM_DEPTH_BUFFER;
	blitParams.filter = GLI_TP_FILTER_NEAREST;

	gli_ctx.ops->blitFramebuffers(gli_ctx.impl, &blitParams);

	gli_fb_set_current_draw_buffer();
	d2r.frameCount = curFrame;
	return 1;
}

static int ready_op(void *_ctx, u32 _slot, int _wait)
{
	(void)_ctx;
	return d2r.pbuf[_slot]->ops->ready(d2r.pbuf[_slot], _wait != 0);
}

static void write_slot(u32 _slot, const gli_rdram_write *_w)
{
	gli_prb *buffer = d2r.pbuf[_slot];
	const u8 *pixelData;
	buffer->ops->bind(buffer);
	/* converted straight out of the mapped pack buffer */
	pixelData = (const u8 *)buffer->ops->map(buffer, 0, _w->width * _w->height * gli_ctx.formats.depthFormatBytes);
	d2r.written = pixelData != NULL;
	if (pixelData != NULL) {
		gli_rdram_write_depth((const f32 *)pixelData, _w);
		buffer->ops->unmap(buffer);
	}
	buffer->ops->unbind(buffer);
}

static void write_op(void *_ctx, u32 _slot)
{
	(void)_ctx;
	write_slot(_slot, &d2r.geometry[_slot]);
}

static const gli_readback_ops readback_ops = { ready_op, write_op, NULL };

static bool copy(u32 _startAddress, u32 _endAddress, bool _sync)
{
	gli_fb *cur = gli_fb_of(d2r.pCurFrameBuffer);
	DepthBuffer *pDepthBuffer = cur->m_pDepthBuffer;
	const u32 stride = cur->m_width << 1;
	const u32 max_height = cutHeight(_startAddress, cur->m_height, stride);
	const u32 width = cur->m_width;
	u32 numPixels = (_endAddress - _startAddress) >> 1;
	s32 x0, y0;
	u32 y1, height, slot;
	gli_rdram_write w;
	struct FrameBuffer *pBuffer;

	if (numPixels / cur->m_width > max_height) {
		_endAddress = _startAddress + (max_height * stride);
		numPixels = (_endAddress - _startAddress) >> 1;
	}

	x0 = 0;
	y0 = (s32)((_startAddress - pDepthBuffer->m_address) / stride);
	y1 = (_endAddress - pDepthBuffer->m_address) / stride;
	height = 1u + y1 - (u32)y0;
	if (max_height < height)
		height = max_height;

	w.srcStride = width;
	w.width = width;
	w.height = height;
	w.numPixels = numPixels;
	w.startAddress = _startAddress;
	w.bufferAddress = pDepthBuffer->m_address;
	w.size = G_IM_SIZ_16b;

	/* an older read still in flight must land before this one */
	slot = _sync ? gli_readback_drain(&d2r.readback, &readback_ops) : gli_readback_begin(&d2r.readback, &readback_ops);

	gli_ctx.ops->bindFramebuffer(gli_ctx.impl, gli_ctx.readFramebuffer, d2r.FBO);
	{
		gli_prb *buffer = d2r.pbuf[slot];
		buffer->ops->bind(buffer);
		buffer->ops->read_pixels(buffer, x0, y0, width, height, gli_ctx.formats.depthFormat, gli_ctx.formats.depthType);
		buffer->ops->unbind(buffer);
	}

	if (_sync) {
		d2r.pbuf[slot]->ops->ready(d2r.pbuf[slot], 1);
		write_slot(slot, &w);
		if (!d2r.written)
			return 0;
	} else {
		d2r.geometry[slot] = w;
		gli_readback_issued(&d2r.readback);
	}

	pDepthBuffer->m_cleared = 0;
	pBuffer = gli_fb_find(pDepthBuffer->m_address);
	if (pBuffer != NULL)
		gli_fb_of(pBuffer)->m_cleared = 0;

	gDP.changed |= CHANGED_SCISSOR;
	return 1;
}

bool gli_d2r_copy_to_rdram(u32 _address)
{
	gli_fb *cur;
	u32 endAddress;
	if (config.frameBufferEmulation.copyDepthToRDRAM == cdSoftwareRender)
		return 1;

	if (d2r.pbuf[0] == NULL)
		return 0;

	if (!prepare_copy(&_address, 0))
		return 0;

	cur = gli_fb_of(d2r.pCurFrameBuffer);
	endAddress = cur->m_pDepthBuffer->m_address + cur->m_width * cur->m_height * 2;

	return copy(cur->m_pDepthBuffer->m_address, endAddress, d2r.numSlots == 1);
}

bool gli_d2r_copy_chunk_to_rdram(u32 _startAddress)
{
	u32 endAddress;
	if (config.frameBufferEmulation.copyDepthToRDRAM == cdSoftwareRender)
		return 1;

	if (d2r.pbuf[0] == NULL)
		return 0;

	endAddress = (_startAddress & ~0xfff) + 0x1000;

	if (!prepare_copy(&_startAddress, 1))
		return 0;

	return copy(_startAddress, endAddress, 1);
}
