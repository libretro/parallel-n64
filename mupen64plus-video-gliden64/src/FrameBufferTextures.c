/* A frame buffer's GPU side: its frame buffers and textures made and let
 * go, the multisampled one resolved, and the texture a tile or the
 * background samples, a part of it or a copy where it is drawn into
 * (gli_fb_rdram.h). C89. */
#include <assert.h>
#include <string.h>

#include "gli_fb_rdram.h"
#include "gli_fb_list.h"
#include "gli_framebuffer.h"
#include "gli_textures.h"
#include "gli_tex.h"
#include "DepthBuffer.h"
#include "DisplayWindow.h"
#include "Config.h"
#include "GBI.h"
#include "VI.h"
#include "gDP.h"
#include "gSP.h"
#include "Graphics/gli_context.h"

/* the copies are the rare way: kept out of the lookups, with the blits'
 * parameters on their own stack */
#if defined(__GNUC__)
#define COLD_PATH __attribute__((noinline))
#elif defined(_MSC_VER)
#define COLD_PATH __declspec(noinline)
#else
#define COLD_PATH
#endif

static u32 tex_target(void)
{
	return config.video.multisampling != 0 ? GLI_TT_TEXTURE_2D_MULTISAMPLE : GLI_TT_TEXTURE_2D;
}

void gli_fb_make(gli_fb *_fb)
{
	memset(_fb, 0, sizeof(gli_fb));
	_fb->m_loadType = LOADTYPE_BLOCK;
	_fb->m_pTexture = gli_textures_add_fb(tex_target());
	_fb->m_FBO = gli_ctx.ops->createFramebuffer(gli_ctx.impl);
}

void gli_fb_free(gli_fb *_fb)
{
	gli_ctx.ops->deleteFramebuffer(gli_ctx.impl, _fb->m_FBO);
	gli_ctx.ops->deleteFramebuffer(gli_ctx.impl, _fb->m_resolveFBO);
	gli_ctx.ops->deleteFramebuffer(gli_ctx.impl, _fb->m_SubFBO);
	gli_ctx.ops->deleteFramebuffer(gli_ctx.impl, _fb->m_copyFBO);

	gli_textures_remove_fb(_fb->m_pTexture);
	gli_textures_remove_fb(_fb->m_pResolveTexture);
	gli_textures_remove_fb(_fb->m_pSubTexture);
	gli_textures_remove_fb(_fb->m_pFrameBufferCopyTexture);
}

static void init_texture_at(u32 _address, f32 _scale, u16 _width, u16 _height, u16 _format, u16 _size, gli_tex *_pTexture)
{
	const gli_fb_formats *fbTexFormats = &gli_ctx.formats;

	_pTexture->width = (u16)(u32)(_width * _scale);
	_pTexture->height = (u16)(u32)(_height * _scale);
	_pTexture->format = _format;
	_pTexture->size = _size;
	_pTexture->clampS = 1;
	_pTexture->clampT = 1;
	_pTexture->address = _address;
	_pTexture->clampWidth = _width;
	_pTexture->clampHeight = _height;
	_pTexture->frameBufferTexture = GLI_TEX_FB_ONE_SAMPLE;
	_pTexture->maskS = 0;
	_pTexture->maskT = 0;
	_pTexture->mirrorS = 0;
	_pTexture->mirrorT = 0;
	_pTexture->realWidth = _pTexture->width;
	_pTexture->realHeight = _pTexture->height;
	_pTexture->textureBytes = _pTexture->realWidth * _pTexture->realHeight;
	if (_size > G_IM_SIZ_8b)
		_pTexture->textureBytes *= fbTexFormats->colorFormatBytes;
	else
		_pTexture->textureBytes *= fbTexFormats->monochromeFormatBytes;
}

static void init_texture(const gli_fb *_fb, u16 _width, u16 _height, u16 _format, u16 _size, gli_tex *_pTexture)
{
	init_texture_at(_fb->m_startAddress, _fb->m_scale, _width, _height, _format, _size, _pTexture);
}

static void set_and_attach_texture(u32 _fbo, const gli_tex *_pTexture, u32 _t, bool _multisampling)
{
	const gli_fb_formats *fbTexFormat = &gli_ctx.formats;
	gli_init_texture_params initParams = GLI_INIT_TEXTURE_PARAMS_DEFAULT;
	gli_fb_render_target bufTarget = GLI_FB_RENDER_TARGET_DEFAULT;

	initParams.handle = _pTexture->name;
	initParams.textureUnitIndex = _t == 0 ? GLI_TU_TEX0 : GLI_TU_TEX1;
	if (_multisampling)
		initParams.msaaLevel = config.video.multisampling;
	initParams.width = _pTexture->realWidth;
	initParams.height = _pTexture->realHeight;
	if (_pTexture->size > G_IM_SIZ_8b) {
		initParams.internalFormat = fbTexFormat->colorInternalFormat;
		initParams.format = fbTexFormat->colorFormat;
		initParams.dataType = fbTexFormat->colorType;
	} else {
		initParams.internalFormat = fbTexFormat->monochromeInternalFormat;
		initParams.format = fbTexFormat->monochromeFormat;
		initParams.dataType = fbTexFormat->monochromeType;
	}
	gli_ctx.ops->init2DTexture(gli_ctx.impl, &initParams);

	if (!_multisampling) {
		gli_tex_parameters texParams = GLI_TEX_PARAMETERS_DEFAULT;
		texParams.handle = _pTexture->name;
		texParams.target = GLI_TT_TEXTURE_2D;
		texParams.textureUnitIndex = _t == 0 ? GLI_TU_TEX0 : GLI_TU_TEX1;
		texParams.minFilter = GLI_TP_FILTER_NEAREST;
		texParams.magFilter = GLI_TP_FILTER_NEAREST;
		gli_ctx.ops->setTextureParameters(gli_ctx.impl, &texParams);
	}

	bufTarget.bufferHandle = _fbo;
	bufTarget.bufferTarget = GLI_BT_FRAMEBUFFER;
	bufTarget.attachment = GLI_BA_COLOR_ATTACHMENT0;
	bufTarget.textureTarget = _multisampling ? GLI_TT_TEXTURE_2D_MULTISAMPLE : GLI_TT_TEXTURE_2D;
	bufTarget.textureHandle = _pTexture->name;
	gli_ctx.ops->addFrameBufferRenderTarget(gli_ctx.impl, &bufTarget);
	assert(!gli_ctx.ops->isFramebufferError(gli_ctx.impl));
}

void gli_fb_attach_texture(gli_tex *_pTexture, u32 _fbo, u16 _width, u16 _height, f32 _scale, u16 _format, u16 _size)
{
	init_texture_at(0, _scale, _width, _height, _format, _size, _pTexture);
	set_and_attach_texture(_fbo, _pTexture, 0, 0);
}

void gli_fb_init(gli_fb *_fb, struct gli_fb_list *_list, u32 _address, u16 _format, u16 _size, u16 _width, int _cfb)
{
	u16 maxHeight;

	_fb->m_startAddress = _address;
	_fb->m_width = _width;
	_fb->m_height = _cfb ? VI.height : 1;
	_fb->m_size = _size;
	gli_fb_update_end_address(_fb);
	if (gli_fb_is_auxiliary(_fb) && config.frameBufferEmulation.copyAuxToRDRAM != 0)
		_fb->m_scale = 1.0f;
	else if (config.frameBufferEmulation.nativeResFactor != 0 && config.frameBufferEmulation.enable != 0)
		_fb->m_scale = (f32)config.frameBufferEmulation.nativeResFactor;
	else
		_fb->m_scale = g_window.scaleX;
	_fb->m_cfb = (u8)(_cfb != 0);
	_fb->m_cleared = 0;
	_fb->m_fingerprint = 0;
	_fb->m_swapCount = g_window.buffersSwapCount;

	maxHeight = gli_VI_GetMaxBufferHeight(_width);
	init_texture(_fb, _width, maxHeight, _format, _size, _fb->m_pTexture);
	gli_fb_list_lend_copy(_list, _fb, ((u32)_width * maxHeight) << _size >> 1);

	if (config.video.multisampling != 0) {
		set_and_attach_texture(_fb->m_FBO, _fb->m_pTexture, 0, 1);
		_fb->m_pTexture->frameBufferTexture = GLI_TEX_FB_MULTI_SAMPLE;

		_fb->m_pResolveTexture = gli_textures_add_fb(GLI_TT_TEXTURE_2D);
		init_texture(_fb, _width, maxHeight, _format, _size, _fb->m_pResolveTexture);
		_fb->m_resolveFBO = gli_ctx.ops->createFramebuffer(gli_ctx.impl);
		set_and_attach_texture(_fb->m_resolveFBO, _fb->m_pResolveTexture, 0, 0);
		assert(!gli_ctx.ops->isFramebufferError(gli_ctx.impl));

		gli_ctx.ops->bindFramebuffer(gli_ctx.impl, GLI_BT_FRAMEBUFFER, _fb->m_FBO);
	} else {
		set_and_attach_texture(_fb->m_FBO, _fb->m_pTexture, 0, 0);
	}
}

/* @_src blitted into @_dst at the same place, nearest, and the draw buffer
 * the current buffer's again */
static void blit(u32 _read, u32 _draw, s32 _x0, s32 _y0, s32 _srcX1, s32 _srcY1, s32 _dstX1, s32 _dstY1)
{
	gli_blit_params blitParams = GLI_BLIT_PARAMS_DEFAULT;
	blitParams.readBuffer = _read;
	blitParams.drawBuffer = _draw;
	blitParams.srcX0 = _x0;
	blitParams.srcY0 = _y0;
	blitParams.srcX1 = _srcX1;
	blitParams.srcY1 = _srcY1;
	blitParams.dstX0 = 0;
	blitParams.dstY0 = 0;
	blitParams.dstX1 = _dstX1;
	blitParams.dstY1 = _dstY1;
	blitParams.mask = GLI_BM_COLOR_BUFFER;
	blitParams.filter = GLI_TP_FILTER_NEAREST;

	gli_ctx.ops->blitFramebuffers(gli_ctx.impl, &blitParams);

	gli_ctx.ops->bindFramebuffer(gli_ctx.impl, gli_ctx.readFramebuffer, 0);

	gli_fb_set_current_draw_buffer();
}

void gli_fb_resolve_texture(gli_fb *_fb, int _bForce)
{
	if (!gli_ctx.multisampling)
		return;

	if (_fb->m_resolved && !_bForce)
		return;

	blit(_fb->m_FBO, _fb->m_resolveFBO, 0, 0, _fb->m_pTexture->realWidth, _fb->m_pTexture->realHeight,
		_fb->m_pResolveTexture->realWidth, _fb->m_pResolveTexture->realHeight);
	_fb->m_resolved = 1;
}

static bool init_sub_texture(gli_fb *_fb, u32 _t)
{
	const gDPTile *pTile = gSP.textureTile[_t];
	u32 width, height;

	if (_fb->m_SubFBO == 0)
		_fb->m_SubFBO = gli_ctx.ops->createFramebuffer(gli_ctx.impl);

	if (pTile->lrs < pTile->uls || pTile->lrt < pTile->ult)
		return 0;
	width = pTile->lrs - pTile->uls + 1;
	height = pTile->lrt - pTile->ult + 1;

	if (_fb->m_pSubTexture != NULL) {
		if (_fb->m_pSubTexture->size == _fb->m_pTexture->size &&
			_fb->m_pSubTexture->clampWidth == width &&
			_fb->m_pSubTexture->clampHeight == height)
			return 1;
		gli_textures_remove_fb(_fb->m_pSubTexture);
	}

	_fb->m_pSubTexture = gli_textures_add_fb(GLI_TT_TEXTURE_2D);
	init_texture(_fb, (u16)width, (u16)height, _fb->m_pTexture->format, _fb->m_pTexture->size, _fb->m_pSubTexture);

	_fb->m_pSubTexture->clampS = pTile->clamps;
	_fb->m_pSubTexture->clampT = pTile->clampt;
	_fb->m_pSubTexture->offsetS = 0.0f;
	_fb->m_pSubTexture->offsetT = 0.0f;

	set_and_attach_texture(_fb->m_SubFBO, _fb->m_pSubTexture, _t, 0);

	return 1;
}

static COLD_PATH gli_tex *get_sub_texture(gli_fb *_fb, u32 _t)
{
	s32 x0, y0, copyWidth, copyHeight;
	u32 readFBO;

	if (!gli_ctx.blitFramebuffer)
		return _fb->m_pTexture;

	if (!init_sub_texture(_fb, _t))
		return _fb->m_pTexture;

	x0 = (s32)(_fb->m_pTexture->offsetS * _fb->m_scale);
	y0 = (s32)(_fb->m_pTexture->offsetT * _fb->m_scale);
	copyWidth = _fb->m_pSubTexture->realWidth;
	if (x0 + copyWidth > _fb->m_pTexture->realWidth)
		copyWidth = _fb->m_pTexture->realWidth - x0;
	copyHeight = _fb->m_pSubTexture->realHeight;
	if (y0 + copyHeight > _fb->m_pTexture->realHeight)
		copyHeight = _fb->m_pTexture->realHeight - y0;

	readFBO = _fb->m_FBO;
	if (gli_ctx.weakBlitFramebuffer && _fb->m_pTexture->frameBufferTexture == GLI_TEX_FB_MULTI_SAMPLE) {
		gli_fb_resolve_texture(_fb, 1);
		readFBO = _fb->m_resolveFBO;
	}

	blit(readFBO, _fb->m_SubFBO, x0, y0, x0 + copyWidth, y0 + copyHeight, copyWidth, copyHeight);

	return _fb->m_pSubTexture;
}

static COLD_PATH gli_tex *copy_texture(gli_fb *_fb)
{
	if (_fb->m_copied)
		return _fb->m_pFrameBufferCopyTexture;

	if (_fb->m_pFrameBufferCopyTexture == NULL) {
		_fb->m_copyFBO = gli_ctx.ops->createFramebuffer(gli_ctx.impl);
		_fb->m_pFrameBufferCopyTexture = gli_textures_add_fb(tex_target());
		init_texture(_fb, (u16)_fb->m_width, gli_VI_GetMaxBufferHeight((u16)_fb->m_width), _fb->m_pTexture->format,
			_fb->m_pTexture->size, _fb->m_pFrameBufferCopyTexture);
		set_and_attach_texture(_fb->m_copyFBO, _fb->m_pFrameBufferCopyTexture, 0, config.video.multisampling != 0);
		if (config.video.multisampling != 0)
			_fb->m_pFrameBufferCopyTexture->frameBufferTexture = GLI_TEX_FB_MULTI_SAMPLE;
	}

	blit(_fb->m_FBO, _fb->m_copyFBO, 0, 0, _fb->m_pTexture->realWidth, _fb->m_pTexture->realHeight,
		_fb->m_pTexture->realWidth, _fb->m_pTexture->realHeight);

	_fb->m_copied = 1;
	return _fb->m_pFrameBufferCopyTexture;
}

static f32 shift_scale(u32 _shift)
{
	if (_shift > 10)
		return (f32)(1 << (16 - _shift));
	if (_shift > 0)
		return 1.0f / (f32)(1 << _shift);
	return 1.0f;
}

gli_tex *gli_fb_get_texture(gli_fb *_fb, u32 _t, int _drawnInto)
{
	const gDPTile *pTile = gSP.textureTile[_t];
	const bool getDepthTexture = _fb->m_isDepthBuffer &&
		gDP.colorImage.address == gDP.depthImageAddress &&
		_fb->m_pDepthBuffer != NULL &&
		(config.generalEmulation.hacks & hack_ZeldaMM) == 0;
	gli_tex *pTexture = getDepthTexture ? _fb->m_pDepthBuffer->m_pDepthBufferTexture : _fb->m_pTexture;
	u32 shift, factor;

	if (_drawnInto) {
		if (gli_ctx.textureBarrier)
			gli_ctx.ops->textureBarrier(gli_ctx.impl);
		else if (gli_ctx.blitFramebuffer)
			pTexture = getDepthTexture ? gli_depth_copy_texture(_fb->m_pDepthBuffer, _fb) : copy_texture(_fb);
	}

	shift = (pTile->imageAddress - _fb->m_startAddress) >> (_fb->m_size - 1);
	factor = _fb->m_width;
	if (_fb->m_loadType == LOADTYPE_TILE) {
		pTexture->offsetS = (f32)(_fb->m_loadTileOrigin.uls + (shift % factor));
		pTexture->offsetT = (f32)(_fb->m_loadTileOrigin.ult + shift / factor);
	} else {
		pTexture->offsetS = (f32)(shift % factor);
		pTexture->offsetT = (f32)(shift / factor);
	}

	if (!getDepthTexture && (pTile->clamps == 0 || pTile->clampt == 0))
		pTexture = get_sub_texture(_fb, _t);

	pTexture->scaleS = _fb->m_scale / (f32)pTexture->realWidth;
	pTexture->scaleT = _fb->m_scale / (f32)pTexture->realHeight;

	pTexture->shiftScaleS = shift_scale(pTile->shifts);
	pTexture->shiftScaleT = shift_scale(pTile->shiftt);

	return pTexture;
}

gli_tex *gli_fb_get_texture_bg(gli_fb *_fb, u32 _t, int _drawnInto)
{
	gli_tex *pTexture = _fb->m_pTexture;
	(void)_t;

	if (_drawnInto) {
		if (gli_ctx.textureBarrier)
			gli_ctx.ops->textureBarrier(gli_ctx.impl);
		else if (gli_ctx.blitFramebuffer)
			pTexture = copy_texture(_fb);
	}

	pTexture->scaleS = _fb->m_scale / (f32)pTexture->realWidth;
	pTexture->scaleT = _fb->m_scale / (f32)pTexture->realHeight;

	pTexture->shiftScaleS = 1.0f;
	pTexture->shiftScaleT = 1.0f;

	pTexture->offsetS = gSP.bgImage.imageX;
	pTexture->offsetT = gSP.bgImage.imageY;
	return pTexture;
}
