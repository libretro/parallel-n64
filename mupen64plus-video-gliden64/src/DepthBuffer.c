#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "Config.h"
#include "VI.h"
#include "gDP.h"
#include "DisplayWindow.h"
#include "gli_framebuffer.h"
#include "gli_fb_rdram.h"
#include "gli_tex.h"
#include "gli_textures.h"
#include "Graphics/gli_context.h"
#include "DepthBuffer.h"

/* More than a game has at once: past that the oldest goes */
#define MAX_DEPTH_BUFFERS GLI_DEPTH_MAX
#define ZLUT_SIZE 0x40000

static struct DepthBuffer * l_list[MAX_DEPTH_BUFFERS];
static u32 l_count;
static struct DepthBuffer * l_pCurrent;
static u16 l_zLUT[ZLUT_SIZE];
static bool l_zLUTMade;

const u16 * gli_depth_zlut(void)
{
	u32 i;
	if (l_zLUTMade)
		return l_zLUT;
	for (i = 0; i < ZLUT_SIZE; i++) {
		u32 exponent = 0;
		u32 testbit = 1 << 17;
		u32 mantissa;
		while ((i & testbit) && (exponent < 7)) {
			exponent++;
			testbit = 1 << (17 - exponent);
		}

		mantissa = (i >> (6 - (6 < exponent ? 6 : exponent))) & 0x7ff;
		l_zLUT[i] = (u16)(((exponent << 11) | mantissa) << 2);
	}
	l_zLUTMade = true;
	return l_zLUT;
}

void gli_depth_list_init(void)
{
	gli_depth_zlut();
	l_pCurrent = NULL;
}

void gli_depth_list_destroy(void)
{
	u32 i;
	l_pCurrent = NULL;
	for (i = 0; i < l_count; i++)
		gli_depth_delete(l_list[i]);
	l_count = 0;
}

void gli_depth_set_cleared(bool _cleared)
{
	u32 i;
	for (i = 0; i < l_count; i++)
		gli_depth_mark_cleared(l_list[i], _cleared);
}

struct DepthBuffer * gli_depth_find(u32 _address)
{
	u32 i = l_count;
	while (i > 0) {
		--i;
		if (gli_depth_address(l_list[i]) == _address)
			return l_list[i];
	}
	return NULL;
}

struct DepthBuffer * gli_depth_current_buffer(void)
{
	return l_pCurrent;
}

static void _removeAt(u32 _i)
{
	u32 i;
	gli_fb_clear_depth_buffer(l_list[_i]);
	gli_depth_delete(l_list[_i]);
	for (i = _i + 1; i < l_count; i++)
		l_list[i - 1] = l_list[i];
	--l_count;
}

static void _removeBuffer(u32 _address)
{
	u32 i = l_count;
	while (i > 0) {
		--i;
		if (gli_depth_address(l_list[i]) == _address) {
			_removeAt(i);
			return;
		}
	}
}

static struct DepthBuffer * _addBuffer(u32 _address, struct FrameBuffer * _pBuffer)
{
	struct DepthBuffer * pBuffer;
	if (l_count == MAX_DEPTH_BUFFERS) {
		if (l_list[0] == l_pCurrent)
			l_pCurrent = NULL;
		_removeAt(0);
	}
	pBuffer = gli_depth_new(_address, gli_fb_of(_pBuffer));
	l_list[l_count++] = pBuffer;
	return pBuffer;
}

static void _createScreenSizeBuffer(u32 _address)
{
	struct FrameBuffer * pFrameBuffer = gli_fb_find(VI.width * 2);
	if (pFrameBuffer == NULL)
		return;

	l_pCurrent = _addBuffer(_address, pFrameBuffer);
	gli_fb_attach_depth_buffer();
	l_pCurrent = NULL;
}

void gli_depth_save(u32 _address)
{
	struct FrameBuffer * pFrameBuffer;
	struct DepthBuffer * pDepthBuffer = NULL;
	struct DepthBuffer * pCurrent;

	if (config.frameBufferEmulation.enable == 0) {
		if (l_count == 0)
			_createScreenSizeBuffer(_address);
		return;
	}

	pFrameBuffer = gli_fb_find(_address);
	if (pFrameBuffer != NULL)
		gli_fb_set_is_depth(pFrameBuffer);

	if (pFrameBuffer != NULL && gli_fb_start_address(pFrameBuffer) != _address)
		pDepthBuffer = gli_depth_find(gli_fb_start_address(pFrameBuffer));
	else
		pDepthBuffer = gli_depth_find(_address);

	if (pDepthBuffer != NULL && pFrameBuffer != NULL && gli_depth_width(pDepthBuffer) != gli_fb_width(pFrameBuffer)) {
		_removeBuffer(_address);
		pDepthBuffer = NULL;
	}

	if (pDepthBuffer == NULL)
		pDepthBuffer = _addBuffer(_address, pFrameBuffer);

	pCurrent = l_pCurrent;
	l_pCurrent = pDepthBuffer;
	gli_fb_attach_depth_buffer();
	if (pFrameBuffer == NULL && (config.generalEmulation.hacks & hack_clearAloneDepthBuffer) != 0)
		gli_depth_clear();
	if (gli_depth_address(pDepthBuffer) != gDP.depthImageAddress)
		l_pCurrent = pCurrent;
}

void gli_depth_clear(void)
{
	if (l_pCurrent != NULL)
		gli_depth_mark_cleared(l_pCurrent, true);
	gli_depth_gpu_clear();
}

void gli_depth_set_image(u32 _address)
{
	gli_depth_save(_address);
}

bool gli_depthbuffer_software_target(const u16 ** _zLUT, u32 * _width)
{
	/* Current depth buffer can be null if we are loading from a save state */
	if (l_pCurrent == NULL || config.frameBufferEmulation.copyDepthToRDRAM != cdSoftwareRender)
		return false;
	*_zLUT = gli_depth_zlut();
	*_width = gli_depth_width(l_pCurrent);
	return true;
}

/* ---- the GPU side ---- */

#define CTX gli_ctx.ops
#define IMPL gli_ctx.impl

/* a texture of no N64 state but what a frame buffer's depth needs */
static void depth_texture_state(struct gli_tex *_tex)
{
	_tex->format = 0;
	_tex->size = 2;
	_tex->clampS = 1;
	_tex->clampT = 1;
	_tex->frameBufferTexture = GLI_TEX_FB_ONE_SAMPLE;
	_tex->maskS = 0;
	_tex->maskT = 0;
	_tex->mirrorS = 0;
	_tex->mirrorT = 0;
}

static void attach(u32 _fbo, u32 _attachment, u32 _target, u32 _texture)
{
	gli_fb_render_target params = GLI_FB_RENDER_TARGET_DEFAULT;
	params.bufferHandle = _fbo;
	params.bufferTarget = gli_ctx.drawFramebuffer;
	params.attachment = _attachment;
	params.textureTarget = _target;
	params.textureHandle = _texture;
	CTX->addFrameBufferRenderTarget(IMPL, &params);
}

static void nearest(u32 _texture)
{
	gli_tex_parameters params = GLI_TEX_PARAMETERS_DEFAULT;
	params.handle = _texture;
	params.target = GLI_TT_TEXTURE_2D;
	params.textureUnitIndex = GLI_TU_TEX0;
	params.minFilter = GLI_TP_FILTER_NEAREST;
	params.magFilter = GLI_TP_FILTER_NEAREST;
	CTX->setTextureParameters(IMPL, &params);
}

static void init_image_texture(const gli_fb *_pBuffer, struct gli_tex *_tex, u32 _clearFBO)
{
	const gli_fb_formats *f = &gli_ctx.formats;
	gli_init_texture_params params = GLI_INIT_TEXTURE_PARAMS_DEFAULT;

	depth_texture_state(_tex);
	_tex->width = _pBuffer->m_pTexture->width;
	_tex->height = _pBuffer->m_pTexture->height;
	_tex->address = _pBuffer->m_startAddress;
	_tex->clampWidth = (u16)_pBuffer->m_width;
	_tex->clampHeight = (u16)_pBuffer->m_height;
	_tex->textureBytes = (u32)_tex->width * _tex->height * f->depthImageFormatBytes;

	params.handle = _tex->name;
	params.width = _tex->width;
	params.height = _tex->height;
	params.internalFormat = f->depthImageInternalFormat;
	params.format = f->depthImageFormat;
	params.dataType = f->depthImageType;
	CTX->init2DTexture(IMPL, &params);

	nearest(_tex->name);
	attach(_clearFBO, GLI_BA_COLOR_ATTACHMENT0, GLI_TT_TEXTURE_2D, _tex->name);
}

void gli_depth_init_image_texture(DepthBuffer *_pDepth, const gli_fb *_pBuffer)
{
	if (config.frameBufferEmulation.N64DepthCompare == 0 || _pDepth->m_pDepthImageZTexture != NULL)
		return;

	_pDepth->m_pDepthImageZTexture = gli_textures_add_fb(GLI_TT_TEXTURE_2D);
	_pDepth->m_ZTextureClearFBO = CTX->createFramebuffer(IMPL);
	_pDepth->m_pDepthImageDeltaZTexture = gli_textures_add_fb(GLI_TT_TEXTURE_2D);
	_pDepth->m_DeltaZTextureClearFBO = CTX->createFramebuffer(IMPL);

	init_image_texture(_pBuffer, _pDepth->m_pDepthImageZTexture, _pDepth->m_ZTextureClearFBO);
	init_image_texture(_pBuffer, _pDepth->m_pDepthImageDeltaZTexture, _pDepth->m_DeltaZTextureClearFBO);

	gli_depth_clear();
}

/* the size of the screen's depth buffer, for no frame buffer */
static void screen_size(u32 *_width, u32 *_height, u16 _maxHeight)
{
	if (config.frameBufferEmulation.nativeResFactor == 0) {
		*_width = g_window.width;
		*_height = (u32)(_maxHeight * g_window.scaleX);
	} else {
		*_width = VI.width * config.frameBufferEmulation.nativeResFactor;
		*_height = _maxHeight * config.frameBufferEmulation.nativeResFactor;
	}
}

static void init_buffer_texture(const gli_fb *_pBuffer, struct gli_tex *_tex, bool _multisample)
{
	const gli_fb_formats *f = &gli_ctx.formats;
	gli_init_texture_params params = GLI_INIT_TEXTURE_PARAMS_DEFAULT;

	if (_pBuffer != NULL) {
		_tex->width = _pBuffer->m_pTexture->width;
		_tex->height = _pBuffer->m_pTexture->height;
		_tex->address = _pBuffer->m_startAddress;
		_tex->clampWidth = (u16)_pBuffer->m_width;
		_tex->clampHeight = gli_VI_GetMaxBufferHeight((u16)_pBuffer->m_width);
	} else {
		const u16 maxHeight = gli_VI_GetMaxBufferHeight((u16)VI.width);
		u32 width, height;
		screen_size(&width, &height, maxHeight);
		_tex->width = (u16)width;
		_tex->height = (u16)height;
		_tex->address = gDP.depthImageAddress;
		_tex->clampWidth = (u16)VI.width;
		_tex->clampHeight = maxHeight;
	}
	depth_texture_state(_tex);
	_tex->textureBytes = (u32)_tex->width * _tex->height * f->depthFormatBytes;

	params.handle = _tex->name;
	params.msaaLevel = _multisample ? config.video.multisampling : 0U;
	params.width = _tex->width;
	params.height = _tex->height;
	params.internalFormat = f->depthInternalFormat;
	params.format = f->depthFormat;
	params.dataType = f->depthType;
	CTX->init2DTexture(IMPL, &params);

	if (!_multisample)
		nearest(_tex->name);
	else
		_tex->frameBufferTexture = GLI_TEX_FB_MULTI_SAMPLE;
}

static void init_renderbuffer(DepthBuffer *_pDepth, const gli_fb *_pBuffer)
{
	gli_init_renderbuffer_params params = GLI_INIT_RENDERBUFFER_PARAMS_DEFAULT;
	u32 height;
	if (_pDepth->m_depthRenderbuffer != 0)
		return;
	if (_pBuffer != NULL) {
		_pDepth->m_depthRenderbufferWidth = _pBuffer->m_pTexture->width;
		height = _pBuffer->m_pTexture->height;
	} else
		screen_size(&_pDepth->m_depthRenderbufferWidth, &height, gli_VI_GetMaxBufferHeight((u16)VI.width));

	_pDepth->m_depthRenderbuffer = CTX->createRenderbuffer(IMPL);
	params.handle = _pDepth->m_depthRenderbuffer;
	params.target = GLI_TT_RENDERBUFFER;
	params.format = gli_ctx.formats.depthInternalFormat;
	params.width = _pDepth->m_depthRenderbufferWidth;
	params.height = height;
	CTX->initRenderbuffer(IMPL, &params);
}

void gli_depth_set_attachment(DepthBuffer *_pDepth, u32 _fbo, u32 _target)
{
	gli_fb_render_target params = GLI_FB_RENDER_TARGET_DEFAULT;
	params.attachment = GLI_BA_DEPTH_ATTACHMENT;
	params.bufferHandle = _fbo;
	params.bufferTarget = _target;
	if (gli_ctx.depthFramebufferTextures) {
		params.textureHandle = _pDepth->m_pDepthBufferTexture->name;
		params.textureTarget = config.video.multisampling != 0 ? GLI_TT_TEXTURE_2D_MULTISAMPLE : GLI_TT_TEXTURE_2D;
	} else {
		params.textureHandle = _pDepth->m_depthRenderbuffer;
		params.textureTarget = GLI_TT_RENDERBUFFER;
	}
	CTX->addFrameBufferRenderTarget(IMPL, &params);

	_pDepth->m_copied = 0;
	_pDepth->m_resolved = 0;
}

void gli_depth_init_texture(DepthBuffer *_pDepth, const gli_fb *_pBuffer)
{
	if (gli_ctx.depthFramebufferTextures) {
		if (_pDepth->m_pDepthBufferTexture == NULL) {
			_pDepth->m_pDepthBufferTexture = gli_textures_add_fb(config.video.multisampling != 0 ?
				GLI_TT_TEXTURE_2D_MULTISAMPLE : GLI_TT_TEXTURE_2D);
			init_buffer_texture(_pBuffer, _pDepth->m_pDepthBufferTexture, config.video.multisampling != 0);
		}
	} else
		init_renderbuffer(_pDepth, _pBuffer);

	if (config.video.multisampling != 0 && _pDepth->m_pResolveDepthBufferTexture == NULL) {
		_pDepth->m_pResolveDepthBufferTexture = gli_textures_add_fb(GLI_TT_TEXTURE_2D);
		init_buffer_texture(_pBuffer, _pDepth->m_pResolveDepthBufferTexture, 0);
	}
}

/* the depth of @_from blitted into @_to's, sized as @_src and @_dst, then
 * drawing back into @_from */
static void blit_depth(u32 _from, u32 _to, const struct gli_tex *_src, const struct gli_tex *_dst)
{
	gli_blit_params blit = GLI_BLIT_PARAMS_DEFAULT;
	blit.readBuffer = _from;
	blit.drawBuffer = _to;
	blit.srcX1 = _src->width;
	blit.srcY1 = _src->height;
	blit.dstX1 = _dst->width;
	blit.dstY1 = _dst->height;
	blit.mask = GLI_BM_DEPTH_BUFFER;
	blit.filter = GLI_TP_FILTER_NEAREST;
	CTX->blitFramebuffers(IMPL, &blit);

	CTX->bindFramebuffer(IMPL, gli_ctx.readFramebuffer, 0);
	CTX->bindFramebuffer(IMPL, gli_ctx.drawFramebuffer, _from);
}

struct gli_tex *gli_depth_resolve_texture(DepthBuffer *_pDepth, const gli_fb *_pBuffer)
{
	if (config.video.multisampling == 0)
		return _pDepth->m_pDepthBufferTexture;

	if (_pDepth->m_resolved)
		return _pDepth->m_pResolveDepthBufferTexture;

	attach(_pBuffer->m_resolveFBO, GLI_BA_DEPTH_ATTACHMENT, GLI_TT_TEXTURE_2D, _pDepth->m_pResolveDepthBufferTexture->name);
	blit_depth(_pBuffer->m_FBO, _pBuffer->m_resolveFBO, _pDepth->m_pDepthBufferTexture, _pDepth->m_pResolveDepthBufferTexture);

	_pDepth->m_resolved = 1;
	return _pDepth->m_pResolveDepthBufferTexture;
}

struct gli_tex *gli_depth_copy_texture(DepthBuffer *_pDepth, const gli_fb *_pBuffer)
{
	const struct gli_tex *color;
	if (_pDepth->m_copied)
		return _pDepth->m_pDepthBufferCopyTexture;

	if (_pDepth->m_pDepthBufferCopyTexture == NULL) {
		_pDepth->m_pDepthBufferCopyTexture = gli_textures_add_fb(GLI_TT_TEXTURE_2D);
		init_buffer_texture(_pBuffer, _pDepth->m_pDepthBufferCopyTexture, 0);
	}

	color = _pBuffer->m_pTexture->frameBufferTexture == GLI_TEX_FB_MULTI_SAMPLE ?
		_pBuffer->m_pResolveTexture : _pBuffer->m_pTexture;
	attach(_pDepth->m_copyFBO, GLI_BA_COLOR_ATTACHMENT0, GLI_TT_TEXTURE_2D, color->name);
	attach(_pDepth->m_copyFBO, GLI_BA_DEPTH_ATTACHMENT, GLI_TT_TEXTURE_2D, _pDepth->m_pDepthBufferCopyTexture->name);
	blit_depth(_pBuffer->m_FBO, _pDepth->m_copyFBO, _pDepth->m_pDepthBufferTexture, _pDepth->m_pDepthBufferTexture);

	_pDepth->m_copied = 1;
	return _pDepth->m_pDepthBufferCopyTexture;
}

void gli_depth_activate_texture(DepthBuffer *_pDepth, const gli_fb *_pBuffer)
{
	gli_textures_activate(0, gli_depth_resolve_texture(_pDepth, _pBuffer));
	CTX->textureBarrier(IMPL);
}

void gli_depth_bind_image_texture(DepthBuffer *_pDepth, u32 _fbo)
{
	if (gli_ctx.imageTextures) {
		gli_bind_image_texture_params params = GLI_BIND_IMAGE_TEXTURE_PARAMS_DEFAULT;
		params.imageUnit = GLI_IU_DepthZ;
		params.texture = _pDepth->m_pDepthImageZTexture->name;
		params.accessMode = GLI_IA_READ_WRITE;
		params.textureFormat = gli_ctx.formats.depthImageInternalFormat;
		CTX->bindImageTexture(IMPL, &params);

		params.imageUnit = GLI_IU_DepthDeltaZ;
		params.texture = _pDepth->m_pDepthImageDeltaZTexture->name;
		CTX->bindImageTexture(IMPL, &params);
	} else if (gli_ctx.framebufferFetch) {
		attach(_fbo, GLI_BA_COLOR_ATTACHMENT1, GLI_TT_TEXTURE_2D, _pDepth->m_pDepthImageZTexture->name);
		attach(_fbo, GLI_BA_COLOR_ATTACHMENT2, GLI_TT_TEXTURE_2D, _pDepth->m_pDepthImageDeltaZTexture->name);
		CTX->setDrawBuffers(IMPL, 3);
	}
}

void gli_depth_gpu_clear(void)
{
	const gli_fb *pColorBuffer;
	DepthBuffer *pDepth;
	if (config.frameBufferEmulation.enable == 0 || config.frameBufferEmulation.N64DepthCompare == 0) {
		CTX->clearDepthBuffer(IMPL);
		return;
	}

	/* Clear depth image texture */
	pColorBuffer = gli_fb_current_info();
	if (pColorBuffer == NULL || pColorBuffer->m_pDepthBuffer == NULL)
		return;
	pDepth = pColorBuffer->m_pDepthBuffer;

	CTX->bindFramebuffer(IMPL, gli_ctx.drawFramebuffer, pDepth->m_ZTextureClearFBO);
	CTX->clearColorBuffer(IMPL, 1.0f, 0.0f, 0.0f, 0.0f);

	CTX->bindFramebuffer(IMPL, gli_ctx.drawFramebuffer, pDepth->m_DeltaZTextureClearFBO);
	CTX->clearColorBuffer(IMPL, 1.0f, 0.0f, 0.0f, 0.0f);
	gli_fb_set_current_draw_buffer();
}

/* The depth buffers live in fixed slots: nothing is allocated as they come
 * and go. The list above deletes its oldest before it adds past
 * GLI_DEPTH_MAX, so a slot is always free. */
static DepthBuffer s_depth[GLI_DEPTH_MAX];
static bool s_depthUsed[GLI_DEPTH_MAX];

DepthBuffer *gli_depth_new(u32 _address, const gli_fb *_pBuffer)
{
	u32 slot = 0;
	DepthBuffer *pDepth;
	while (slot < GLI_DEPTH_MAX && s_depthUsed[slot])
		++slot;
	assert(slot < GLI_DEPTH_MAX);
	if (slot < GLI_DEPTH_MAX) {
		s_depthUsed[slot] = 1;
		pDepth = &s_depth[slot];
		memset(pDepth, 0, sizeof(*pDepth));
	} else {
		pDepth = (DepthBuffer *)calloc(1, sizeof(*pDepth));
		if (pDepth == NULL)
			return NULL;
	}
	pDepth->m_copyFBO = CTX->createFramebuffer(IMPL);
	pDepth->m_address = _address;
	pDepth->m_width = _pBuffer != NULL ? _pBuffer->m_width : VI.width;
	gli_depth_init_texture(pDepth, _pBuffer);
	return pDepth;
}

void gli_depth_delete(DepthBuffer *_pDepth)
{
	if (_pDepth == NULL)
		return;
	/* the renderbuffer went to deleteFramebuffer in the C++ too: the
	 * context has no call to delete one */
	CTX->deleteFramebuffer(IMPL, _pDepth->m_depthRenderbuffer);
	CTX->deleteFramebuffer(IMPL, _pDepth->m_copyFBO);
	CTX->deleteFramebuffer(IMPL, _pDepth->m_ZTextureClearFBO);
	CTX->deleteFramebuffer(IMPL, _pDepth->m_DeltaZTextureClearFBO);

	gli_textures_remove_fb(_pDepth->m_pDepthImageZTexture);
	gli_textures_remove_fb(_pDepth->m_pDepthImageDeltaZTexture);
	gli_textures_remove_fb(_pDepth->m_pDepthBufferTexture);
	gli_textures_remove_fb(_pDepth->m_pResolveDepthBufferTexture);
	gli_textures_remove_fb(_pDepth->m_pDepthBufferCopyTexture);

	if (_pDepth < s_depth || _pDepth >= s_depth + GLI_DEPTH_MAX) {
		free(_pDepth);
		return;
	}
	s_depthUsed[_pDepth - s_depth] = 0;
}

u32 gli_depth_address(const DepthBuffer *_pDepth)
{
	return _pDepth->m_address;
}

u32 gli_depth_width(const DepthBuffer *_pDepth)
{
	return _pDepth->m_width;
}

void gli_depth_mark_cleared(DepthBuffer *_pDepth, bool _cleared)
{
	_pDepth->m_cleared = _cleared;
}
