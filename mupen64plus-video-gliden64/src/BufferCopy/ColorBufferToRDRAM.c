/* The copy of a colour frame buffer to RDRAM (ColorBufferToRDRAM.h).
 * C89. */
#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "ColorBufferToRDRAM.h"
#include "DeferredReadback.h"
#include "../gli_rdram_write.h"

#include <gli_framebuffer.h>
#include <gli_fb_rdram.h>
#include <gli_drawer.h>
#include <gli_textures.h>
#include <gli_tex.h>
#include <Config.h>
#include <GBI.h>
#include <N64.h>
#include <VI.h>
#include <gDP.h>
#include <Graphics/gli_context.h>
#include <Graphics/ColorBufferReader.h>

/* What a copy was read for */
typedef struct color_copy
{
	u32 startAddress;
	u32 numPixels;
	u32 bufferAddress;
	u32 width;
	u32 height;
	u32 size;
} color_copy;

static struct
{
	u32 FBO;
	gli_tex *pTexture;
	struct FrameBuffer *pCurFrameBuffer;
	gli_fb *cur;      /* its state */
	u32 frameCount;
	u32 startAddress;
	u32 lastBufferWidth;
	gli_cbr *bufferReader;
	gli_readback readback;
	/* what each slot's read was for */
	color_copy geometry[GLI_READBACK_MAX_SLOTS];
} c2r = { 0, NULL, NULL, NULL, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, NULL, { 1, 0, { 0, 0, 0 } }, { { 0 } } };

static const u32 allowedRealWidths[3] = { 320, 480, 640 };

/* the narrowest of the widths the texture is made at that holds @_viWidth,
 * else the widest */
static u32 real_width(u32 _viWidth)
{
	u32 index = 0;
	while (index < 2 && _viWidth > allowedRealWidths[index])
		++index;
	return allowedRealWidths[index];
}

void gli_c2r_init(void)
{
	c2r.FBO = gli_ctx.ops->createFramebuffer(gli_ctx.impl);
}

static void destroy_texture(void)
{
	/* reads still in flight die with the reader's buffers */
	gli_cbr_delete(c2r.bufferReader);
	c2r.bufferReader = NULL;
	gli_readback_reset(&c2r.readback, 1);

	if (c2r.pTexture != NULL) {
		gli_textures_remove_fb(c2r.pTexture);
		c2r.pTexture = NULL;
	}
}

void gli_c2r_destroy(void)
{
	destroy_texture();

	if (c2r.FBO != 0) {
		gli_ctx.ops->deleteFramebuffer(gli_ctx.impl, c2r.FBO);
		c2r.FBO = 0;
	}
}

static void init_texture(void)
{
	const gli_fb_formats *fbTexFormat = &gli_ctx.formats;
	gli_init_texture_params initParams = GLI_INIT_TEXTURE_PARAMS_DEFAULT;
	gli_tex_parameters setParams = GLI_TEX_PARAMETERS_DEFAULT;
	gli_fb_render_target bufTarget = GLI_FB_RENDER_TARGET_DEFAULT;
	gli_tex *t;

	c2r.pTexture = t = gli_textures_add_fb(GLI_TT_TEXTURE_2D);
	t->format = G_IM_FMT_RGBA;
	t->size = 2;
	t->clampS = 1;
	t->clampT = 1;
	t->frameBufferTexture = GLI_TEX_FB_ONE_SAMPLE;
	t->maskS = 0;
	t->maskT = 0;
	t->mirrorS = 0;
	t->mirrorT = 0;
	/* The actual VI width is not used for texture width because most
	 * texture widths cause slowdowns in the glReadPixels call, at least on
	 * Android */
	t->realWidth = c2r.lastBufferWidth;
	t->realHeight = gli_VI_GetMaxBufferHeight((u16)c2r.lastBufferWidth);
	/* The color-buffer readers size their glReadPixels rows and source
	 * stride from width, not realWidth. addFrameBufferTexture recycles
	 * texture objects, so without this the readback runs with whatever
	 * width the previous owner left behind (Killer Instinct Gold reliably
	 * inherits ~48k) and glReadPixels overflows the PBO: heap corruption
	 * with copy color buffer to RDRAM enabled. */
	t->width = t->realWidth;
	t->height = t->realHeight;
	t->textureBytes = t->realWidth * t->realHeight * fbTexFormat->colorFormatBytes;

	initParams.handle = t->name;
	initParams.width = t->realWidth;
	initParams.height = t->realHeight;
	initParams.internalFormat = fbTexFormat->colorInternalFormat;
	initParams.format = fbTexFormat->colorFormat;
	initParams.dataType = fbTexFormat->colorType;
	gli_ctx.ops->init2DTexture(gli_ctx.impl, &initParams);

	setParams.handle = t->name;
	setParams.target = GLI_TT_TEXTURE_2D;
	setParams.textureUnitIndex = GLI_TU_TEX0;
	setParams.minFilter = GLI_TP_FILTER_LINEAR;
	setParams.magFilter = GLI_TP_FILTER_LINEAR;
	gli_ctx.ops->setTextureParameters(gli_ctx.impl, &setParams);

	bufTarget.bufferHandle = c2r.FBO;
	bufTarget.bufferTarget = gli_ctx.drawFramebuffer;
	bufTarget.attachment = GLI_BA_COLOR_ATTACHMENT0;
	bufTarget.textureTarget = GLI_TT_TEXTURE_2D;
	bufTarget.textureHandle = t->name;
	gli_ctx.ops->addFrameBufferRenderTarget(gli_ctx.impl, &bufTarget);

	/* check if everything is OK */
	assert(!gli_ctx.ops->isFramebufferError(gli_ctx.impl));

	gli_ctx.ops->bindFramebuffer(gli_ctx.impl, gli_ctx.drawFramebuffer, 0);

	c2r.bufferReader = (gli_cbr *)gli_ctx.ops->createColorBufferReader(gli_ctx.impl, t);
	gli_readback_reset(&c2r.readback, c2r.bufferReader->numSlots);
}

static void copy_white(struct FrameBuffer *_pBuffer, gli_fb *fb)
{
	gli_rdram_write_white(fb->m_startAddress, fb->m_size);
	fb->m_copiedToRdram = 1;
	gli_fb_copy_rdram(_pBuffer);
	fb->m_cleared = 0;
}

static bool prepare_copy(u32 *_startAddress)
{
	struct FrameBuffer *pBuffer;
	gli_fb *buf;
	u32 curFrame, numPixels, stride, bufferHeight, realWidth, readBuffer;

	if (VI.width == 0 || gli_fb_current() == NULL)
		return 0;

	pBuffer = gli_fb_find(*_startAddress);
	if (pBuffer == NULL)
		return 0;
	buf = gli_fb_of(pBuffer);
	if (buf->m_isOBScreen)
		return 0;

	curFrame = gli_window_swap_count();

	*_startAddress &= ~0xfff;
	if (*_startAddress < buf->m_startAddress)
		*_startAddress = buf->m_startAddress;

	if (c2r.frameCount == curFrame && pBuffer == c2r.pCurFrameBuffer && c2r.startAddress != *_startAddress)
		return 1;

	numPixels = buf->m_width * buf->m_height;
	if (numPixels == 0)
		return 0;

	stride = buf->m_width << buf->m_size >> 1;
	bufferHeight = cutHeight(*_startAddress, buf->m_height, stride);
	if (bufferHeight == 0)
		return 0;

	realWidth = real_width(buf->m_width);
	if (c2r.pTexture == NULL || c2r.pTexture->realWidth != realWidth
		|| c2r.pTexture->realHeight != gli_VI_GetMaxBufferHeight((u16)realWidth)) {
		destroy_texture();
		c2r.lastBufferWidth = realWidth;
		init_texture();
	}

	c2r.pCurFrameBuffer = pBuffer;
	c2r.cur = buf;

	if ((config.generalEmulation.hacks & hack_subscreen) != 0 && buf->m_width == VI.width) {
		copy_white(pBuffer, buf);
		return 0;
	}

	if (config.video.multisampling != 0) {
		gli_fb_resolve(pBuffer);
		readBuffer = buf->m_resolveFBO;
	} else {
		readBuffer = buf->m_FBO;
	}

	if (buf->m_scale != 1.0f) {
		gli_blit_params blit = GLI_BLIT_PARAMS_DEFAULT;
		u32 x0 = 0, width;
		gli_tex *pInputTexture = buf->m_pTexture;
		if (config.frameBufferEmulation.nativeResFactor == 0) {
			const u32 screenWidth = gli_window_width();
			width = screenWidth;
			if (gli_window_is_adjust_screen()) {
				width = (u32)(screenWidth * gli_window_adjust_scale());
				x0 = (screenWidth - width) / 2;
			}
		} else {
			width = pInputTexture->realWidth;
		}

		blit.readBuffer = readBuffer;
		blit.drawBuffer = c2r.FBO;
		blit.srcX0 = (s32)x0;
		blit.srcY0 = 0;
		blit.srcX1 = (s32)(x0 + width);
		blit.srcY1 = (s32)(u32)(bufferHeight * buf->m_scale);
		blit.dstX0 = 0;
		blit.dstY0 = 0;
		blit.dstX1 = (s32)buf->m_width;
		blit.dstY1 = (s32)bufferHeight;
		blit.mask = GLI_BM_COLOR_BUFFER;
		blit.filter = GLI_TP_FILTER_NEAREST;
		if (!gli_ctx.ops->blitFramebuffers(gli_ctx.impl, &blit)) {
			/* drawn instead */
			gli_copy_rect r;
			r.srcX0 = blit.srcX0;
			r.srcY0 = blit.srcY0;
			r.srcX1 = blit.srcX1;
			r.srcY1 = blit.srcY1;
			r.srcWidth = pInputTexture->realWidth;
			r.srcHeight = pInputTexture->realHeight;
			r.dstX0 = blit.dstX0;
			r.dstY0 = blit.dstY0;
			r.dstX1 = blit.dstX1;
			r.dstY1 = blit.dstY1;
			r.dstWidth = c2r.pTexture->realWidth;
			r.dstHeight = c2r.pTexture->realHeight;
			r.invertX = 0;
			r.invertY = 0;
			gli_ctx.ops->bindFramebuffer(gli_ctx.impl, gli_ctx.readFramebuffer, readBuffer);
			gli_ctx.ops->bindFramebuffer(gli_ctx.impl, gli_ctx.drawFramebuffer, c2r.FBO);
			gli_drawer_copy_texrect(&r, pInputTexture, GLI_TP_FILTER_NEAREST);
		}

		gli_ctx.ops->bindFramebuffer(gli_ctx.impl, gli_ctx.readFramebuffer, c2r.FBO);
	} else {
		gli_ctx.ops->bindFramebuffer(gli_ctx.impl, gli_ctx.readFramebuffer, readBuffer);
	}

	c2r.frameCount = curFrame;
	c2r.startAddress = *_startAddress;
	return 1;
}

static void write_rdram(const u8 *_pixels, u32 _stride, const color_copy *_g)
{
	bool clear;
	gli_rdram_write w;
	struct FrameBuffer *pBuffer;

	if (_g->height == 0)
		return;

	clear = !gli_FBInfo_supported() && config.frameBufferEmulation.copyFromRDRAM != 0;
	w.srcStride = _stride;
	w.width = _g->width;
	w.height = _g->height;
	w.numPixels = _g->numPixels;
	w.startAddress = _g->startAddress;
	w.bufferAddress = _g->bufferAddress;
	w.size = _g->size;
	if (_g->size == G_IM_SIZ_32b) {
		if (clear)
			memset(RDRAM + _g->startAddress, 0, _g->numPixels * 4);
		gli_rdram_write_rgba32((const u32 *)_pixels, &w);
	} else if (_g->size == G_IM_SIZ_16b) {
		if (clear)
			memset(RDRAM + _g->startAddress, 0, _g->numPixels * 2);
		gli_rdram_write_rgba16((const u32 *)_pixels, &w);
	} else if (_g->size == G_IM_SIZ_8b) {
		if (clear)
			memset(RDRAM + _g->startAddress, 0, _g->numPixels);
		gli_rdram_write_r8(_pixels, &w);
	}

	/* the buffer it was read from, if it is still there as it was */
	pBuffer = gli_fb_find(_g->bufferAddress);
	if (pBuffer != NULL) {
		gli_fb *fb = gli_fb_of(pBuffer);
		if (fb->m_startAddress == _g->bufferAddress && fb->m_width == _g->width && fb->m_size == _g->size) {
			fb->m_copiedToRdram = 1;
			gli_fb_copy_rdram(pBuffer);
			fb->m_cleared = 0;
		}
	}
}

static void write_slot(u32 _slot, const color_copy *_g)
{
	u32 srcStride = 0;
	color_copy g = *_g;
	const u8 *pPixels = gli_cbr_slot_pixels(c2r.bufferReader, _slot, g.width, &g.height, g.size, &srcStride);
	if (pPixels != NULL)
		write_rdram(pPixels, srcStride, &g);
	c2r.bufferReader->ops->clean_up(c2r.bufferReader);
}

static int ready_op(void *_ctx, u32 _slot, int _wait)
{
	(void)_ctx;
	return c2r.bufferReader->ops->slot_ready(c2r.bufferReader, _slot, _wait ? 1 : 0) != 0;
}

static void write_op(void *_ctx, u32 _slot)
{
	(void)_ctx;
	write_slot(_slot, &c2r.geometry[_slot]);
}

static const gli_readback_ops readback_ops = { ready_op, write_op, NULL };

static void copy(u32 _startAddress, u32 _endAddress, bool _sync)
{
	const gli_fb *cur = c2r.cur;
	const u32 stride = cur->m_width << cur->m_size >> 1;
	const u32 maxBufferHeight = gli_VI_GetMaxBufferHeight((u16)cur->m_width);
	const u32 cut = cutHeight(_startAddress, cur->m_height, stride);
	const u32 max_height = maxBufferHeight < cut ? maxBufferHeight : cut;
	const u32 width = cur->m_width;
	const s32 x0 = 0;
	u32 numPixels = (_endAddress - _startAddress) >> (cur->m_size - 1);
	s32 y0;
	u32 y1, height, slot;
	color_copy g;

	if (numPixels / cur->m_width > max_height) {
		_endAddress = _startAddress + (max_height * stride);
		numPixels = (_endAddress - _startAddress) >> (cur->m_size - 1);
	}

	y0 = (s32)((_startAddress - cur->m_startAddress) / stride);
	y1 = (_endAddress - cur->m_startAddress) / stride;
	height = 1u + y1 - (u32)y0;
	if (max_height < height)
		height = max_height;

	g.startAddress = _startAddress;
	g.numPixels = numPixels;
	g.bufferAddress = cur->m_startAddress;
	g.width = width;
	g.size = cur->m_size;

	if (c2r.bufferReader->numSlots == 0) {
		u32 srcStride = width;
		const u8 *pPixels = gli_cbr_read_pixels(c2r.bufferReader, x0, y0, width, &height, g.size, _sync, &srcStride);
		gli_fb_set_current_draw_buffer();
		if (pPixels == NULL)
			return;
		g.height = height;
		write_rdram(pPixels, srcStride, &g);
		c2r.bufferReader->ops->clean_up(c2r.bufferReader);
		gDP.changed |= CHANGED_SCISSOR;
		return;
	}

	g.height = height < c2r.pTexture->realHeight ? height : c2r.pTexture->realHeight;

	/* An asynchronous copy writes the reads the GPU has finished and
	 * leaves this one in flight; a synchronous one writes those first,
	 * waiting for them, then its own. */
	slot = _sync ? gli_readback_drain(&c2r.readback, &readback_ops) : gli_readback_begin(&c2r.readback, &readback_ops);
	c2r.bufferReader->ops->read_to_slot(c2r.bufferReader, slot, x0, y0, g.height, g.size);
	gli_fb_set_current_draw_buffer();
	if (_sync) {
		c2r.bufferReader->ops->slot_ready(c2r.bufferReader, slot, 1);
		write_slot(slot, &g);
	} else {
		c2r.geometry[slot] = g;
		gli_readback_issued(&c2r.readback);
	}

	gDP.changed |= CHANGED_SCISSOR;
}

void gli_c2r_copy_to_rdram(u32 _address, bool _sync)
{
	const gli_fb *cur;
	u32 numBytes;
	if (!prepare_copy(&_address))
		return;
	cur = c2r.cur;
	numBytes = (cur->m_width * cur->m_height) << cur->m_size >> 1;
	copy(cur->m_startAddress, cur->m_startAddress + numBytes, _sync);
}

void gli_c2r_copy_chunk_to_rdram(u32 _startAddress)
{
	const u32 endAddress = (_startAddress & ~0xfff) + 0x1000;

	if (!prepare_copy(&_startAddress))
		return;
	copy(_startAddress, endAddress, 1);
}
