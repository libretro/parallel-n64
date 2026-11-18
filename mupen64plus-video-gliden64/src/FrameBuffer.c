/* The frame buffer list: the buffers the game draws into, in their fixed
 * slots, made, found, cut and dropped as the colour image moves; the
 * overscan buffer; and what the VI shows, drawn into the window. C89. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "FrameBuffer.h"
#include "StereoFrames.h"
#include "gSP.h"
#include "DepthBuffer.h"
#include "N64.h"
#include "RSP.h"
#include "RDP.h"
#include "gDP.h"
#include "VI.h"
#include "Config.h"
#include "PostProcessor.h"
#include "FrameBufferInfo.h"
#include "TexrectDrawer.h"
#include "DisplayWindow.h"
#include "gli_framebuffer.h"
#include "gli_textures.h"
#include "gli_combiner.h"
#include "gli_drawer.h"

#include "BufferCopy/ColorBufferToRDRAM.h"
#include "BufferCopy/DepthBufferToRDRAM.h"
#include "BufferCopy/RDRAMtoColorBuffer.h"

#include "Graphics/gli_context.h"
#include "Graphics/VulkanContext/vk_device.h"
#include "Graphics/VulkanContext/vk_ContextImpl.h"

#define BIND_FB(_target, _name) gli_ctx.ops->bindFramebuffer(gli_ctx.impl, (_target), (_name))
#define CLEAR_COLOR() gli_ctx.ops->clearColorBuffer(gli_ctx.impl, 0.0f, 0.0f, 0.0f, 0.0f)

/* The overscan buffer: what the VI shows is drawn into it, and it into the
 * window cut by the overscan */
typedef struct overscan_buffer
{
	s32 hOffset;
	s32 vOffset;
	f32 scale;
	u32 drawingWidth;
	u32 bufferWidth;
	u32 bufferHeight;
	int enabled;

	u32 FBO;
	gli_tex *pTexture;
} overscan_buffer;

/* the buffers live in fixed slots; the C list keeps their order */
static gli_fb_list s_fbs;
static FrameBuffer s_storage[GLI_FB_MAX];
static FrameBuffer *s_pCurrent;
static FrameBuffer *s_pCopy;
static overscan_buffer s_overscan = { 0, 0, 1.0f, 0, 0, 0, 0, 0, NULL };
static gli_vi_state s_rdpUpdate;

extern uint32_t RemoveFBBlackBars;

#define SLOT(_slot) (&s_storage[_slot])
#define AT(_pos) SLOT(gli_fb_list_at(&s_fbs, _pos))
#define OLDEST() AT(s_fbs.count - 1)

static void list_remove_buffer(u32 _address);

/*---------------the overscan buffer-------------*/

static s32 overscan_h_offset(void)
{
	if (s_overscan.enabled)
		return 0;
	return s_overscan.hOffset;
}

static s32 overscan_v_offset(void)
{
	if (s_overscan.enabled)
		return 0;
	return s_overscan.vOffset;
}

static f32 overscan_scale_y(u32 _fullHeight)
{
	if (s_overscan.enabled)
		return s_overscan.scale;
	return (float)g_window.height / (float)_fullHeight;
}

static void overscan_init(void)
{
	s_overscan.enabled = config.frameBufferEmulation.enableOverscan != 0;
	if (s_overscan.enabled)
		s_overscan.FBO = gli_ctx.ops->createFramebuffer(gli_ctx.impl);

	s_overscan.hOffset = (g_window.screenWidth - g_window.width) / 2;
	s_overscan.vOffset = (g_window.screenHeight - g_window.height) / 2;
	s_overscan.scale = g_window.scaleX;
	s_overscan.drawingWidth = g_window.width;
	s_overscan.bufferWidth = g_window.screenWidth;
	s_overscan.bufferHeight = g_window.screenHeight + g_window.heightOffset;
}

static void overscan_destroy(void)
{
	gli_ctx.ops->deleteFramebuffer(gli_ctx.impl, s_overscan.FBO);
	s_overscan.FBO = 0;
	gli_textures_remove_fb(s_overscan.pTexture);
	s_overscan.pTexture = NULL;
}

static void overscan_set_input_buffer(const gli_fb *_pBuffer)
{
	const gli_tex *pSrcTexture;

	if (!s_overscan.enabled)
		return;

	if (s_overscan.pTexture != NULL &&
		s_overscan.pTexture->width == _pBuffer->m_pTexture->width &&
		s_overscan.pTexture->height == _pBuffer->m_pTexture->height &&
		s_overscan.scale == _pBuffer->m_scale)
		return;

	gli_textures_remove_fb(s_overscan.pTexture);
	s_overscan.pTexture = gli_textures_add_fb(GLI_TT_TEXTURE_2D);
	pSrcTexture = _pBuffer->m_pTexture;
	gli_fb_attach_texture(s_overscan.pTexture, s_overscan.FBO, (u16)_pBuffer->m_width,
		gli_VI_GetMaxBufferHeight((u16)_pBuffer->m_width),
		_pBuffer->m_scale, pSrcTexture->format, pSrcTexture->size);
	s_overscan.scale = _pBuffer->m_scale;
	s_overscan.drawingWidth = s_overscan.bufferWidth = s_overscan.pTexture->width;
	s_overscan.bufferHeight = s_overscan.pTexture->height;
}

static void overscan_activate(void)
{
	if (!s_overscan.enabled) {
		BIND_FB(gli_ctx.drawFramebuffer, 0);
		return;
	}
	BIND_FB(gli_ctx.drawFramebuffer, s_overscan.FBO);
}

/* draws into the window at _x, of _windowWidth, clearing it first when _clear */
static void overscan_draw(u32 _fullHeight, int _PAL, s32 _x, u32 _windowWidth, int _clear)
{
	gli_copy_params copy;
	s32 left, right, top, bottom;

	if (!s_overscan.enabled)
		return;

	BIND_FB(gli_ctx.drawFramebuffer, 0);
	memset(&copy, 0, sizeof(copy));
	if (_PAL) {
		left = (s32)(config.frameBufferEmulation.overscanPAL.left * s_overscan.scale);
		right = (s32)(config.frameBufferEmulation.overscanPAL.right * s_overscan.scale);
		top = (s32)(config.frameBufferEmulation.overscanPAL.top * s_overscan.scale);
		bottom = (s32)(config.frameBufferEmulation.overscanPAL.bottom * s_overscan.scale);
	} else {
		left = (s32)(config.frameBufferEmulation.overscanNTSC.left * s_overscan.scale);
		right = (s32)(config.frameBufferEmulation.overscanNTSC.right * s_overscan.scale);
		top = (s32)(config.frameBufferEmulation.overscanNTSC.top * s_overscan.scale);
		bottom = (s32)(config.frameBufferEmulation.overscanNTSC.bottom * s_overscan.scale);
	}
	copy.r.srcX0 = left;
	copy.r.srcY0 = (s32)(_fullHeight * s_overscan.scale) - bottom;
	copy.r.srcX1 = s_overscan.bufferWidth - right;
	copy.r.srcY1 = top;
	copy.r.srcWidth = s_overscan.pTexture->realWidth;
	copy.r.srcHeight = s_overscan.pTexture->realHeight;
	copy.r.dstX0 = _x + s_overscan.hOffset;
	copy.r.dstY0 = s_overscan.vOffset + g_window.heightOffset;
	copy.r.dstX1 = _x + s_overscan.hOffset + g_window.width;
	copy.r.dstY1 = s_overscan.vOffset + g_window.height + g_window.heightOffset;
	copy.r.dstWidth = _windowWidth;
	copy.r.dstHeight = g_window.screenHeight + g_window.heightOffset;
	copy.filter = GLI_TP_FILTER_LINEAR;
	copy.tex[0] = s_overscan.pTexture;
	copy.combiner = gli_cmb.texrectCopy;
	copy.r.invertY = 0;

	if (_clear)
		CLEAR_COLOR();
	gli_drawer_copy(&copy);
}

/*---------------the list-------------*/

static FrameBuffer *list_find(u32 _startAddress)
{
	const u32 slot = gli_fb_list_find(&s_fbs, _startAddress);
	return slot != GLI_FB_MAX ? SLOT(slot) : NULL;
}

static FrameBuffer *list_get(u32 _startAddress)
{
	const u32 slot = gli_fb_list_get(&s_fbs, _startAddress);
	return slot != GLI_FB_MAX ? SLOT(slot) : NULL;
}

static void list_init(void)
{
	s_pCurrent = NULL;
	s_pCopy = NULL;
	BIND_FB(gli_ctx.drawFramebuffer, 0);
	overscan_init();
	gli_vi_state_init(&s_rdpUpdate);
}

static void list_destroy(void)
{
	BIND_FB(GLI_BT_FRAMEBUFFER, 0);
	while (s_fbs.count != 0)
		gli_fb_free(&SLOT(gli_fb_list_remove_at(&s_fbs, 0))->fb);
	gli_fb_list_free_copies(&s_fbs);
	gli_StereoFrames_destroy();
	s_pCurrent = NULL;
	s_pCopy = NULL;
	overscan_destroy();
}

static void list_set_current_draw_buffer(void)
{
	if (s_pCurrent != NULL)
		BIND_FB(gli_ctx.drawFramebuffer, s_pCurrent->fb.m_FBO);
	else if (s_fbs.count != 0)
		BIND_FB(gli_ctx.drawFramebuffer, OLDEST()->fb.m_FBO);
}

static FrameBuffer *list_add_front(void)
{
	FrameBuffer *pBuffer;
	u32 slot = gli_fb_list_free_slot(&s_fbs);
	if (slot == GLI_FB_MAX) {
		/* every slot in use: the oldest buffer other than the current one goes */
		u32 pos = s_fbs.count;
		while (pos-- > 0 && AT(pos) == s_pCurrent)
			;
		slot = gli_fb_list_remove_at(&s_fbs, pos);
		if (SLOT(slot) == s_pCopy)
			s_pCopy = NULL;
		gli_fb_free(&SLOT(slot)->fb);
	}
	pBuffer = SLOT(slot);
	gli_fb_make(&pBuffer->fb);
	gli_fb_list_push_front(&s_fbs, slot, &pBuffer->fb);
	return pBuffer;
}

/* destroys the buffers taken out of the list, forgetting the current one */
static void list_destroy_removed(const u8 *_removed, u32 _count)
{
	u32 i;
	for (i = 0; i < _count; ++i) {
		FrameBuffer *pBuffer = SLOT(_removed[i]);
		if (pBuffer == s_pCurrent) {
			s_pCurrent = NULL;
			BIND_FB(gli_ctx.drawFramebuffer, 0);
		}
		gli_fb_free(&pBuffer->fb);
	}
}

static void list_remove_intersections(void)
{
	u8 removed[GLI_FB_MAX];
	const u32 count = gli_fb_list_remove_intersections(&s_fbs, &s_pCurrent->fb, removed);
	u32 i;
	for (i = 0; i < count; ++i)
		gli_fb_free(&SLOT(removed[i])->fb);
}

static void list_update_current_end_address(void)
{
	if (s_pCurrent == NULL)
		return;
	gli_fb_update_end_address(&s_pCurrent->fb);
	list_remove_intersections();
}

static void list_create_screen_size_buffer(void)
{
	FrameBuffer *pBuffer;
	if (VI.height == 0)
		return;
	pBuffer = list_add_front();
	gli_fb_init(&pBuffer->fb, &s_fbs, VI.width * 2, G_IM_FMT_RGBA, G_IM_SIZ_16b, (u16)VI.width, 0);
}

static void list_attach_depth_buffer(void)
{
	FrameBuffer *pCurrent = config.frameBufferEmulation.enable == 0 ?
		(s_fbs.count != 0 ? OLDEST() : NULL) : s_pCurrent;
	DepthBuffer *pDepthBuffer;
	gli_fb *cur;

	if (pCurrent == NULL)
		return;
	cur = &pCurrent->fb;

	pDepthBuffer = gli_depth_current_buffer();

	if (cur->m_FBO != 0 && pDepthBuffer != NULL) {
		int goodDepthBufferTexture = 0;

		gli_depth_init_image_texture(pDepthBuffer, cur);
		gli_depth_init_texture(pDepthBuffer, cur);

		if (gli_ctx.depthFramebufferTextures) {
			if (gli_ctx.weakBlitFramebuffer)
				goodDepthBufferTexture = pDepthBuffer->m_pDepthBufferTexture->width == cur->m_pTexture->realWidth;
			else
				goodDepthBufferTexture = pDepthBuffer->m_pDepthBufferTexture->width >= cur->m_pTexture->realWidth ||
					abs((s32)(cur->m_width - pDepthBuffer->m_width)) < 2;
		} else {
			goodDepthBufferTexture = pDepthBuffer->m_depthRenderbufferWidth == cur->m_pTexture->realWidth;
		}

		if (goodDepthBufferTexture) {
			cur->m_pDepthBuffer = pDepthBuffer;
			gli_depth_set_attachment(pDepthBuffer, cur->m_FBO, gli_ctx.drawFramebuffer);
			if (config.frameBufferEmulation.N64DepthCompare != 0)
				gli_depth_bind_image_texture(pDepthBuffer, cur->m_FBO);
		} else
			cur->m_pDepthBuffer = NULL;
	} else
		cur->m_pDepthBuffer = NULL;

	assert(!gli_ctx.ops->isFramebufferError(gli_ctx.impl));
}

static void list_save_buffer(u32 _address, u16 _format, u16 _size, u16 _width, int _cfb)
{
	int bPrevIsDepth = 0;
	float scaleX;
	gli_fb *cur;

	if (_width > 640)
		return;

	if (_width == 512 && (config.generalEmulation.hacks & hack_RE2) != 0)
		_width = (u16)*REG.VI_WIDTH;

	if (config.frameBufferEmulation.enable == 0) {
		if (s_fbs.count == 0)
			list_create_screen_size_buffer();
		return;
	}

	/* stereoscopic 3D */
	if (s_pCurrent != NULL && !s_pCurrent->fb.m_isDepthBuffer && g_stereoFramesCapturing &&
		gli_StereoFrames_wasEntered(s_pCurrent->fb.m_startAddress)) {
		gli_StereoFrames_captureLeftEye(s_pCurrent);
		/* The copy binds the screen; leave the state as the walk had it */
		BIND_FB(gli_ctx.drawFramebuffer, s_pCurrent->fb.m_FBO);
	}

	if (s_pCurrent != NULL &&
		config.frameBufferEmulation.copyAuxToRDRAM != 0 &&
		(config.generalEmulation.hacks & hack_Snap) == 0) {
		if (gli_fb_is_auxiliary(&s_pCurrent->fb)) {
			gli_c2r_copy_to_rdram(s_pCurrent->fb.m_startAddress, 1);
			list_remove_buffer(s_pCurrent->fb.m_startAddress);
		}
	}

	if (s_pCurrent != NULL) {
		cur = &s_pCurrent->fb;
		bPrevIsDepth = cur->m_isDepthBuffer;
		cur->m_readable = 1;
		gli_fb_update_end_address(cur);

		if (!cur->m_isDepthBuffer &&
			!cur->m_copiedToRdram &&
			!cur->m_cfb &&
			!cur->m_cleared &&
			cur->m_RdramCopySize == 0 &&
			cur->m_height > 1) {
			gli_fb_save_rdram(cur);
		}

		list_remove_intersections();
	}

	scaleX = config.frameBufferEmulation.nativeResFactor == 0 ?
		g_window.scaleX :
		(float)config.frameBufferEmulation.nativeResFactor;

	if (s_pCurrent == NULL || s_pCurrent->fb.m_startAddress != _address || s_pCurrent->fb.m_width != _width)
		s_pCurrent = list_find(_address);

	if (s_pCurrent != NULL) {
		cur = &s_pCurrent->fb;
		switch (gli_fb_reuse(cur, g_window.buffersSwapCount, _address, _width, _size, scaleX)) {
		case GLI_FB_REUSE_SUB:
			gSP.changed |= CHANGED_VIEWPORT;
			gDP.changed |= CHANGED_SCISSOR;
			return;
		case GLI_FB_REUSE_TRUNCATE:
			s_pCurrent = NULL;
			break;
		case GLI_FB_REUSE_REMOVE:
			list_remove_buffer(cur->m_startAddress);
			s_pCurrent = NULL;
			break;
		default:
			cur->m_resolved = 0;
			BIND_FB(GLI_BT_FRAMEBUFFER, cur->m_FBO);
			if (cur->m_size != _size) {
				f32 fillColor[4];
				gli_gDPGetFillColor(fillColor);
				gli_drawer_clear_color(fillColor);
				cur->m_size = _size;
				cur->m_pTexture->format = _format;
				cur->m_pTexture->size = _size;
				if (cur->m_pResolveTexture != NULL) {
					cur->m_pResolveTexture->format = _format;
					cur->m_pResolveTexture->size = _size;
				}
				if (cur->m_copiedToRdram)
					gli_fb_save_rdram(cur);
			}
			break;
		}
	}
	if (s_pCurrent == NULL) {
		/* Wasn't found or removed, create a new one */
		s_pCurrent = list_add_front();
		gli_fb_init(&s_pCurrent->fb, &s_fbs, _address, _format, _size, _width, _cfb);
		gli_r2cb_copy_buffer_from_rdram(s_pCurrent);
		if (_cfb)
			gli_fb_save_rdram(&s_pCurrent->fb);
	}
	cur = &s_pCurrent->fb;

	if (_address == gDP.depthImageAddress)
		gli_depth_save(_address);
	else
		list_attach_depth_buffer();

	if (gli_fb_is_auxiliary(cur) &&
		cur->m_pDepthBuffer != NULL &&
		bPrevIsDepth &&
		(config.generalEmulation.hacks&hack_LoadDepthTextures) == 0) {
		/* N64 games may use partial depth buffer clear for aux buffers
		 * It will not work for GL, so we have to force clear depth buffer for aux buffer */
		gli_drawer_clear_depth();
	}

	cur->m_isDepthBuffer = _address == gDP.depthImageAddress;
	/* Immersive mode */
	if (gli_gSPImmersiveClearsShownBuffers() && !cur->m_isDepthBuffer && !gli_fb_is_auxiliary(cur) &&
		cur->m_swapCount != g_window.buffersSwapCount) {
		BIND_FB(GLI_BT_FRAMEBUFFER, cur->m_FBO);
		CLEAR_COLOR();
	}
	if (config.stereo.mode == stereoBothEyes)
		gli_StereoFrames_noteEntered(cur->m_startAddress);
	cur->m_isPauseScreen = cur->m_isOBScreen = 0;
	cur->m_copied = 0;
	cur->m_swapCount = g_window.buffersSwapCount;
}

static void list_copy_aux(void)
{
	u32 pos;
	for (pos = 0; pos < s_fbs.count; ++pos) {
		const gli_fb *pBuffer = &AT(pos)->fb;
		if (gli_fb_is_auxiliary(pBuffer))
			gli_c2r_copy_to_rdram(pBuffer->m_startAddress, 1);
	}
}

static void list_remove_aux(void)
{
	u8 removed[GLI_FB_MAX];
	list_destroy_removed(removed, gli_fb_list_remove_aux(&s_fbs, removed));
}

static void list_remove_buffer(u32 _address)
{
	u32 pos;
	for (pos = 0; pos < s_fbs.count; ++pos)
		if (AT(pos)->fb.m_startAddress == _address) {
			u8 removed = (u8)gli_fb_list_remove_at(&s_fbs, pos);
			list_destroy_removed(&removed, 1);
			return;
		}
}

static void list_remove_buffers(u32 _width)
{
	u8 removed[GLI_FB_MAX];
	s_pCurrent = NULL;
	list_destroy_removed(removed, gli_fb_list_remove_width(&s_fbs, _width, removed));
}

/*---------------what the VI shows-------------*/

static void list_render_screen_size_buffer(void)
{
	FrameBuffer *pBuffer, *pFilteredBuffer;
	gli_blit_copy_params blitParams;
	s32 srcCoord[4], dstCoord[4];
	s32 hOffset, vOffset;
	const u32 wndWidth = g_window.width;
	const u32 wndHeight = g_window.height;
	const u32 screenWidth = g_window.screenWidth;
	const u32 screenHeight = g_window.screenHeight;
	const u32 wndHeightOffset = g_window.heightOffset;

	if (s_fbs.count == 0)
		return;

	pBuffer = OLDEST();
	pFilteredBuffer = gli_pp_run(pBuffer);

	srcCoord[0] = 0;
	srcCoord[1] = 0;
	srcCoord[2] = (s32)wndWidth;
	srcCoord[3] = (s32)wndHeight;
	hOffset = (screenWidth - wndWidth) / 2;
	vOffset = (screenHeight - wndHeight) / 2 + wndHeightOffset;
	dstCoord[0] = hOffset;
	dstCoord[1] = vOffset;
	dstCoord[2] = hOffset + (s32)wndWidth;
	dstCoord[3] = vOffset + (s32)wndHeight;
	if (gliden64_vulkan_enabled())
		gliden64_vulkan_shown(pBuffer->fb.m_startAddress, pBuffer->fb.m_width, pBuffer->fb.m_size,
			(u32)(pBuffer->fb.m_pTexture->realHeight / pBuffer->fb.m_scale), pBuffer->fb.m_scale, srcCoord, dstCoord);

	BIND_FB(gli_ctx.drawFramebuffer, 0);

	CLEAR_COLOR();

	memset(&blitParams, 0, sizeof(blitParams));
	blitParams.copy.r.srcX0 = srcCoord[0];
	blitParams.copy.r.srcY0 = srcCoord[3];
	blitParams.copy.r.srcX1 = srcCoord[2];
	blitParams.copy.r.srcY1 = srcCoord[1];
	blitParams.copy.r.srcWidth = wndWidth;
	blitParams.copy.r.srcHeight = wndHeight;
	blitParams.copy.r.dstX0 = dstCoord[0];
	blitParams.copy.r.dstY0 = dstCoord[1];
	blitParams.copy.r.dstX1 = dstCoord[2];
	blitParams.copy.r.dstY1 = dstCoord[3];
	blitParams.copy.r.dstWidth = screenWidth;
	blitParams.copy.r.dstHeight = screenHeight + wndHeightOffset;
	blitParams.copy.filter = GLI_TP_FILTER_LINEAR;
	blitParams.mask = GLI_BM_COLOR_BUFFER;
	blitParams.copy.tex[0] = pFilteredBuffer->fb.m_pTexture;
	blitParams.copy.combiner = gli_cmb.texrectCopy;
	blitParams.readBuffer = pFilteredBuffer->fb.m_FBO;

	gli_drawer_blit_or_copy(&blitParams);

	BIND_FB(gli_ctx.readFramebuffer, 0);

	if (config.stereo.mode != stereoOff)
		gli_gSPStereoEndFrame();
	gli_window_swap_buffers();
	BIND_FB(gli_ctx.drawFramebuffer, pBuffer->fb.m_FBO);
	if (config.frameBufferEmulation.forceDepthBufferClear != 0)
		gli_drawer_clear_depth();
	gDP.changed |= CHANGED_SCISSOR;
}

/* the buffer an eye reads, post-processed and resolved; the texture
 * copied from it */
static FrameBuffer *eye_source(FrameBuffer *_pSource, int _force, gli_tex **_pTexture)
{
	FrameBuffer *pEyeBuffer = gli_pp_run(_pSource);
	if (pEyeBuffer->fb.m_pTexture->frameBufferTexture == GLI_TEX_FB_MULTI_SAMPLE) {
		gli_fb_resolve_texture(&pEyeBuffer->fb, _force);
		*_pTexture = pEyeBuffer->fb.m_pResolveTexture;
	} else
		*_pTexture = pEyeBuffer->fb.m_pTexture;
	return pEyeBuffer;
}

static void list_render_buffer(void)
{
	gli_vi_result rdpRes;
	gli_vi_source_rect src;
	gli_vi_present_in in;
	gli_vi_present_out rects;
	FrameBuffer *pBuffer, *pNextBuffer;
	const s32 *srcCoord;
	const s32 *dstCoord;
	u32 vFullHeight, stride, eyes, eye, lastLine;
	s32 hOffset, eyeWidth;
	int overscan;

	if (config.frameBufferEmulation.enable == 0) {
		list_render_screen_size_buffer();
		return;
	}

	if (!gli_vi_update(&s_rdpUpdate, &rdpRes)) {
		BIND_FB(gli_ctx.drawFramebuffer, 0);
		CLEAR_COLOR();
		gli_window_swap_buffers();
		if (s_pCurrent != NULL)
			BIND_FB(gli_ctx.drawFramebuffer, s_pCurrent->fb.m_FBO);
		return;
	}

	if (RemoveFBBlackBars && !rdpRes.vi_ispal)
	{
		rdpRes.vi_vres = 240;
		rdpRes.vi_minhpass = 0;
		rdpRes.vi_maxhpass = 0;
		rdpRes.vi_v_start = 0;
	}

	pBuffer = list_find(rdpRes.vi_origin);
	if (pBuffer == NULL)
		return;
	pBuffer->fb.m_isMainBuffer = 1;
	overscan_set_input_buffer(&pBuffer->fb);

	vFullHeight = rdpRes.vi_ispal ? 288 : 240;

	gli_vi_source(&rdpRes, pBuffer->fb.m_startAddress, pBuffer->fb.m_width, pBuffer->fb.m_height,
		pBuffer->fb.m_size, pBuffer->fb.m_scale, pBuffer->fb.m_pTexture->realHeight, &src);

	stride = pBuffer->fb.m_width << pBuffer->fb.m_size >> 1;
	lastLine = (u32)src.srcHeight - 1;
	if (pBuffer->fb.m_height - 1 < lastLine)
		lastLine = pBuffer->fb.m_height - 1;
	pNextBuffer = list_find(rdpRes.vi_origin + stride * lastLine - 1);
	if (pNextBuffer == pBuffer)
		pNextBuffer = NULL;

	/* post-processing keeps the buffer's scale and size, so the presented
	 * area is measured on it */
	in.bufScale = pBuffer->fb.m_scale;
	in.texSize[0] = pBuffer->fb.m_pTexture->realWidth;
	in.texSize[1] = pBuffer->fb.m_pTexture->realHeight;
	in.windowWidth = g_window.width;
	in.drawingWidth = s_overscan.drawingWidth;
	in.offset[0] = overscan_h_offset();
	in.offset[1] = overscan_v_offset();
	in.dstScaleY = overscan_scale_y(vFullHeight);
	in.bufferSize[0] = s_overscan.bufferWidth;
	in.bufferSize[1] = s_overscan.bufferHeight;
	in.hasNext = pNextBuffer != NULL;
	in.immersive = config.stereo.immersive != 0 && !gli_gSPImmersiveIsFlat();
	if (!gli_vi_present_rects(&rdpRes, &src, &in, &rects)) {
		list_remove_buffer(pBuffer->fb.m_startAddress);
		return;
	}
	if (gliden64_vulkan_enabled() && gliden64_upscaler_active()) {
		/* A temporal upscaler takes the buffer as it was drawn, pixel for
		 * pixel: without the VI's scaling or crop, which would smear the
		 * jitter it is drawn with */
		s32 width = (s32)pBuffer->fb.m_pTexture->realWidth;
		s32 height = (s32)pBuffer->fb.m_pTexture->realHeight;
		if ((s32)g_window.screenWidth < width)
			width = (s32)g_window.screenWidth;
		if ((s32)g_window.screenHeight < height)
			height = (s32)g_window.screenHeight;
		rects.src[0] = rects.dst[0] = 0;
		rects.src[1] = rects.dst[1] = 0;
		rects.src[2] = rects.dst[2] = width;
		rects.src[3] = rects.dst[3] = height;
		pNextBuffer = NULL;
	}
	srcCoord = rects.src;
	dstCoord = rects.dst;
	hOffset = in.offset[0];
	if (gliden64_vulkan_enabled())
		gliden64_vulkan_shown(pBuffer->fb.m_startAddress, pBuffer->fb.m_width, pBuffer->fb.m_size,
			(u32)(pBuffer->fb.m_pTexture->realHeight / pBuffer->fb.m_scale), pBuffer->fb.m_scale, srcCoord, dstCoord);

	/* Stereoscopic 3D: GLideN64 draws for one eye's window; each eye is presented into its half */
	eyes = config.stereo.mode == stereoBothEyes ? 2U : 1U;
	eyeWidth = (s32)g_window.screenWidth;
	overscan = config.frameBufferEmulation.enableOverscan != 0;
	for (eye = 0; eye < eyes; ++eye) {
		const s32 eyeOffset = (s32)eye * eyeWidth;
		const s32 dstOffset = overscan ? 0 : eyeOffset;
		const u32 dstWidth = overscan ? s_overscan.bufferWidth : s_overscan.bufferWidth * eyes;
		FrameBuffer *pSource = pBuffer;
		gli_tex *pEyeTexture;
		gli_copy_params copy;

		/* The left eye from the copy its pass left, through the same post-processing */
		if (eyes == 2 && eye == 0) {
			FrameBuffer *pLeftEye = gli_StereoFrames_leftEye(pBuffer);
			if (pLeftEye != NULL)
				pSource = pLeftEye;
		}
		eye_source(pSource, 1, &pEyeTexture);

		overscan_activate();
		/* The window holds both eyes: only the first clears it */
		if (overscan || eye == 0)
			CLEAR_COLOR();

		memset(&copy, 0, sizeof(copy));
		copy.r.srcX0 = srcCoord[0];
		copy.r.srcY0 = srcCoord[1];
		copy.r.srcX1 = srcCoord[2];
		copy.r.srcY1 = srcCoord[3];
		copy.r.srcWidth = pEyeTexture->realWidth;
		copy.r.srcHeight = pEyeTexture->realHeight;
		copy.r.dstX0 = dstCoord[0] + dstOffset;
		copy.r.dstY0 = dstCoord[1];
		copy.r.dstX1 = dstCoord[2] + dstOffset;
		copy.r.dstY1 = dstCoord[3];
		copy.r.dstWidth = dstWidth;
		copy.r.dstHeight = s_overscan.bufferHeight;
		copy.filter = GLI_TP_FILTER_LINEAR;
		copy.tex[0] = pEyeTexture;
		copy.combiner = gli_cmb.texrectCopy;
		copy.r.invertY = !overscan;

		gli_drawer_copy(&copy);

		if (pNextBuffer != NULL) {
			FrameBuffer *pEyeBuffer;
			s32 srcY1;
			pNextBuffer->fb.m_isMainBuffer = 1;
			pSource = pNextBuffer;
			if (eyes == 2 && eye == 0) {
				FrameBuffer *pLeftEye = gli_StereoFrames_leftEye(pNextBuffer);
				if (pLeftEye != NULL)
					pSource = pLeftEye;
			}
			pEyeBuffer = eye_source(pSource, 0, &pEyeTexture);
			/* The post-processors draw into their own buffer */
			overscan_activate();

			srcY1 = (s32)pEyeBuffer->fb.m_pTexture->realHeight;
			copy.r.srcY0 = 0;
			copy.r.srcY1 = srcY1 < rects.nextSrcY1 ? srcY1 : rects.nextSrcY1;
			copy.r.srcWidth = pEyeTexture->realWidth;
			copy.r.srcHeight = pEyeTexture->realHeight;
			copy.r.dstY0 = rects.nextDstY[0];
			copy.r.dstY1 = rects.nextDstY[1];
			copy.tex[0] = pEyeTexture;

			gli_drawer_copy(&copy);
		}

		BIND_FB(gli_ctx.readFramebuffer, 0);
		overscan_draw(vFullHeight, rdpRes.vi_ispal, eyeOffset, (u32)eyeWidth * eyes, eye == 0);
	}

	if (config.stereo.mode != stereoOff)
		gli_gSPStereoEndFrame();
	/* Immersive mode: tell the headset which head pose this picture was drawn for */
	gli_gSPImmersivePresent(pBuffer->fb.m_startAddress);
	gli_window_swap_buffers();
	if (s_pCurrent != NULL)
		BIND_FB(gli_ctx.drawFramebuffer, s_pCurrent->fb.m_FBO);
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
		gli_drawer_clear_depth();
	}

	gli_ctx.ops->setScissor(gli_ctx.impl, hOffset, (s32)g_window.heightOffset,
		(s32)g_window.width, (s32)g_window.height);
	gDP.changed |= CHANGED_SCISSOR;
}

/*---------------the C calls-------------*/

void gli_fb_emulation_init(void)
{
	list_init();
	if (config.frameBufferEmulation.enable != 0) {
		gli_c2r_init();
		gli_d2r_init();
		gli_r2cb_init();
	}
}

void gli_fb_emulation_destroy(void)
{
	gli_r2cb_destroy();
	gli_d2r_destroy();
	gli_c2r_destroy();
	list_destroy();
}

void gli_fb_activate_texture(u32 t, u32 _frameBufferAddress)
{
	gli_tex *pTexture;
	FrameBuffer *pBuffer = list_get(_frameBufferAddress);
	if (pBuffer == NULL)
		return;

	pTexture = gli_fb_get_texture(&pBuffer->fb, t, pBuffer == s_pCurrent);
	if (pTexture == NULL)
		return;

	gli_textures_activate(t, pTexture);
	gDP.changed |= CHANGED_FB_TEXTURE;
}

void gli_fb_activate_texture_bg(u32 t, u32 _frameBufferAddress)
{
	gli_tex *pTexture;
	FrameBuffer *pBuffer = list_get(_frameBufferAddress);
	if (pBuffer == NULL)
		return;

	pTexture = gli_fb_get_texture_bg(&pBuffer->fb, t, pBuffer == s_pCurrent);
	if (pTexture == NULL)
		return;

	gli_textures_activate(t, pTexture);
	gDP.changed |= CHANGED_FB_TEXTURE;
}

bool gli_fb_copy_depth(u32 _address)
{
	FrameBuffer *pCopyBuffer = s_pCopy;
	if (pCopyBuffer != NULL) {
		/* This code is mainly to emulate Zelda MM camera. */
		gli_c2r_copy_to_rdram(pCopyBuffer->fb.m_startAddress, 1);
		pCopyBuffer->fb.m_RdramCopySize = 0; /* To disable validity check by RDRAM content. CPU may change content of the buffer for some unknown reason. */
		s_pCopy = NULL;
		return 1;
	}

	if (gli_d2r_copy_to_rdram(_address)) {
		FrameBuffer *pCurrentDepthBuffer = list_find(gDP.depthImageAddress);
		if (pCurrentDepthBuffer != NULL)
			gli_fb_save_rdram(&pCurrentDepthBuffer->fb);
		return 1;
	}

	return 0;
}

void gli_fb_coords_scales(const FrameBuffer *_pBuffer, f32 *_x, f32 *_y)
{
	const u32 bufferWidth = _pBuffer != NULL ? _pBuffer->fb.m_width : VI.width;
	const u32 bufferHeight = gli_VI_GetMaxBufferHeight((u16)bufferWidth);
	*_x = 1.0f / (f32)bufferWidth;
	*_y = 1.0f / (f32)bufferHeight;
}

/* gDP's way into the list */
void gli_fb_set_color_image(u32 _address, u32 _format, u32 _size, u32 _width)
{
	list_save_buffer(_address, (u16)_format, (u16)_size, (u16)_width, 0);
}

void gli_fb_fill_rdram(s32 _ulx, s32 _uly, s32 _lrx, s32 _lry)
{
	if (s_pCurrent == NULL)
		return;

	if (config.frameBufferEmulation.copyFromRDRAM != 0 && !s_pCurrent->fb.m_isDepthBuffer)
		/* Do not write to RDRAM color buffer if copyFromRDRAM enabled. */
		return;

	gli_fb_rdram_fill_buffer(&s_pCurrent->fb, _ulx, _uly, _lrx, _lry);
}

void gli_fb_set_changed(f32 _maxY)
{
	const u32 maxY = (u32)_maxY;
	const u32 lry = (u32)gDP.scissor.lry;
	gDP.colorImage.changed = TRUE;
	if (gDP.colorImage.height < maxY)
		gDP.colorImage.height = maxY;
	if (lry < gDP.colorImage.height)
		gDP.colorImage.height = lry;
	if (s_pCurrent != NULL) {
		gli_fb *cur = &s_pCurrent->fb;
		if (cur->m_height < gDP.colorImage.height)
			cur->m_height = gDP.colorImage.height;
		cur->m_cfb = 0;
		cur->m_changed = 1;
		cur->m_copiedToRdram = 0;
	}
}

void gli_fb_stereo_sync(void)
{
	gli_texrect_draw();
	list_update_current_end_address();
}

void gli_fb_full_sync(bool _lle)
{
	if (config.frameBufferEmulation.copyAuxToRDRAM != 0) {
		list_copy_aux();
		list_remove_aux();
	}

	gli_texrect_draw();

	list_update_current_end_address();

	if ((config.frameBufferEmulation.copyToRDRAM != ctDisable || (config.generalEmulation.hacks & hack_subscreen) != 0) &&
		!gli_FBInfo_supported() &&
		s_pCurrent != NULL &&
		!gli_fb_is_auxiliary(&s_pCurrent->fb)
	)
		gli_c2r_copy_to_rdram(gDP.colorImage.address, config.frameBufferEmulation.copyToRDRAM == ctSync);

	if (_lle) {
		if (config.frameBufferEmulation.copyDepthToRDRAM != cdDisable && !gli_FBInfo_supported())
			gli_fb_copy_depth(gDP.colorImage.address);
	}
}

bool gli_fb_check_texture(u32 _address, u32 _width, u32 _bytes)
{
	FrameBuffer *pBuffer;
	bool bRes = 0;

	gli_fb_texture_reset();
	if (!config.frameBufferEmulation.enable)
		return 0;

	pBuffer = list_find(_address);
	if (pBuffer != NULL && pBuffer->fb.m_readable) {
		int remove;
		bRes = gli_fb_texture_use(&pBuffer->fb, s_pCurrent != NULL ? &s_pCurrent->fb : NULL,
			_address, _width, _bytes, g_window.buffersSwapCount, &remove) != 0;
		if (remove)
			list_remove_buffer(pBuffer->fb.m_startAddress);
	}
	gli_fb_texture_tiles();
	return bRes;
}

/* VI's way in */
FrameBuffer *gli_fb_find(u32 _address)
{
	return list_find(_address);
}

u32 gli_fb_width(const FrameBuffer *_pBuffer)
{
	return _pBuffer->fb.m_width;
}

bool gli_fb_is_valid_scanout(FrameBuffer *_pBuffer)
{
	/* the check stamps the swap it ran in */
	return gli_fb_is_valid(&_pBuffer->fb, 1, g_window.buffersSwapCount) != 0;
}

bool gli_fb_rendered_uncopied(const FrameBuffer *_pBuffer)
{
	return !_pBuffer->fb.m_cfb && !_pBuffer->fb.m_copiedToRdram;
}

void gli_fb_copy_rdram(FrameBuffer *_pBuffer)
{
	gli_fb_save_rdram(&_pBuffer->fb);
}

void gli_fb_resolve(FrameBuffer *_pBuffer)
{
	gli_fb_resolve_texture(&_pBuffer->fb, 0);
}

bool gli_fb_depth_width_differs(u32 _address, u32 _width)
{
	FrameBuffer *pBuffer = list_find(_address);
	DepthBuffer *pDepthBuffer = pBuffer != NULL ? pBuffer->fb.m_pDepthBuffer : NULL;
	return pDepthBuffer != NULL && pDepthBuffer->m_width != _width;
}

void gli_fb_vi_resized(u32 _widthPrev, u32 _width)
{
	list_remove_buffers(_widthPrev);
	list_remove_buffers(_width);
	gli_depth_list_destroy();
	gli_depth_list_init();
}

void gli_fb_save_cpu_buffer(u32 _address, u32 _size, u32 _width)
{
	list_save_buffer(_address, G_IM_FMT_RGBA, (u16)_size, (u16)_width, 1);
}

void gli_fb_copy_from_rdram(u32 _address, bool _useAlpha)
{
	gli_r2cb_copy_from_rdram(_address, _useAlpha);
}

void gli_fb_render(void)
{
	list_render_buffer();
}

void gli_fb_clear_changed(void)
{
	FrameBuffer *pBuffer;
	gDP.colorImage.changed = FALSE;
	pBuffer = list_find(*REG.VI_ORIGIN);
	if (pBuffer != NULL)
		pBuffer->fb.m_changed = 0;
}

FrameBuffer *gli_fb_current(void)
{
	return s_pCurrent;
}

void gli_fb_set_current(FrameBuffer *_pBuffer)
{
	s_pCurrent = _pBuffer;
}

FrameBuffer *gli_fb_drawn(void)
{
	if (s_pCurrent != NULL || config.frameBufferEmulation.enable != 0 || s_fbs.count == 0)
		return s_pCurrent;
	return OLDEST();
}

gli_fb *gli_fb_of(FrameBuffer *_pBuffer)
{
	return _pBuffer != NULL ? &_pBuffer->fb : NULL;
}

const gli_fb *gli_fb_current_info(void)
{
	return s_pCurrent != NULL ? &s_pCurrent->fb : NULL;
}

bool gli_fb_current_has_depth(void)
{
	return s_pCurrent != NULL && s_pCurrent->fb.m_pDepthBuffer != NULL;
}

bool gli_fb_is_at(u32 _address)
{
	return list_get(_address) != NULL;
}

u32 gli_fb_start_address(const FrameBuffer *_pBuffer)
{
	return _pBuffer->fb.m_startAddress;
}

void gli_fb_make_current(u32 _address)
{
	FrameBuffer *pBuffer = list_find(_address);
	if (pBuffer != NULL) {
		s_pCurrent = pBuffer;
		list_set_current_draw_buffer();
	}
}

bool gli_fb_is_cpu(const FrameBuffer *_pBuffer)
{
	return _pBuffer->fb.m_cfb;
}

void gli_fb_remove(u32 _address)
{
	list_remove_buffer(_address);
}

void gli_fb_set_copy_buffer(FrameBuffer *_pBuffer)
{
	s_pCopy = _pBuffer;
}

FrameBuffer *gli_fb_find_tmp(u32 _address)
{
	const u32 slot = gli_fb_list_find_other(&s_fbs, _address);
	return slot != GLI_FB_MAX ? SLOT(slot) : NULL;
}

void gli_fb_bind_draw(const FrameBuffer *_pBuffer)
{
	BIND_FB(gli_ctx.drawFramebuffer, _pBuffer->fb.m_FBO);
}

void gli_fb_set_current_draw_buffer(void)
{
	list_set_current_draw_buffer();
}

void gli_fb_add_address(u32 _address, u32 _size)
{
	gli_r2cb_add_address(_address, _size);
}

bool gli_fb_is_depth(const FrameBuffer *_pBuffer)
{
	return _pBuffer->fb.m_isDepthBuffer;
}

void gli_fb_copy_depth_chunk(u32 _address)
{
	gli_d2r_copy_chunk_to_rdram(_address);
}

void gli_fb_copy_chunk(u32 _address)
{
	gli_c2r_copy_chunk_to_rdram(_address);
}

void gli_fb_copy_to_rdram(u32 _address, bool _sync)
{
	gli_c2r_copy_to_rdram(_address, _sync);
}

void gli_fb_fill_info(void *_pinfo, u32 _size)
{
	FBInfoEntry *pInfo = (FBInfoEntry *)_pinfo;
	u32 idx = 0;
	u32 pos;
	for (pos = 0; pos < s_fbs.count; ++pos) {
		const gli_fb *pBuffer = &AT(pos)->fb;
		if (pBuffer->m_width == VI.width && !pBuffer->m_cfb && !pBuffer->m_isDepthBuffer) {
			pInfo[idx].addr = pBuffer->m_startAddress;
			pInfo[idx].width = pBuffer->m_width;
			pInfo[idx].height = pBuffer->m_height;
			pInfo[idx++].size = pBuffer->m_size;
			if (idx >= _size)
				return;
		}
	}
}

bool gli_depth_current(u32 *_address, u32 *_width)
{
	const DepthBuffer *pDepthBuffer = gli_depth_current_buffer();
	if (pDepthBuffer == NULL)
		return 0;
	*_address = pDepthBuffer->m_address;
	*_width = pDepthBuffer->m_width;
	return 1;
}

void gli_fb_clear_depth_buffer(DepthBuffer *_pDepthBuffer)
{
	u32 pos;
	for (pos = 0; pos < s_fbs.count; ++pos) {
		gli_fb *pBuffer = &AT(pos)->fb;
		if (pBuffer->m_pDepthBuffer == _pDepthBuffer)
			pBuffer->m_pDepthBuffer = NULL;
	}
}

void gli_fb_attach_depth_buffer(void)
{
	list_attach_depth_buffer();
}

void gli_fb_set_is_depth(FrameBuffer *_pBuffer)
{
	_pBuffer->fb.m_isDepthBuffer = 1;
}

bool gli_fb_has_texture(const FrameBuffer *_pBuffer)
{
	return _pBuffer->fb.m_pTexture != NULL;
}

/*---------------buffers of their own: the stereo left eye, post-processing-------------*/

static FrameBuffer *own_buffer_new(void)
{
	FrameBuffer *pBuffer = (FrameBuffer *)malloc(sizeof(FrameBuffer));
	if (pBuffer != NULL)
		gli_fb_make(&pBuffer->fb);
	return pBuffer;
}

static void own_buffer_delete(FrameBuffer *_pBuffer)
{
	if (_pBuffer == NULL)
		return;
	gli_fb_free(&_pBuffer->fb);
	free(_pBuffer);
}

/* a texture for a buffer of its own, attached to its frame buffer */
static void own_buffer_attach(FrameBuffer *_pBuffer, u32 _filter)
{
	gli_tex *pTexture = _pBuffer->fb.m_pTexture;
	gli_init_texture_params initParams = GLI_INIT_TEXTURE_PARAMS_DEFAULT;
	gli_tex_parameters setParams = GLI_TEX_PARAMETERS_DEFAULT;
	gli_fb_render_target bufTarget = GLI_FB_RENDER_TARGET_DEFAULT;

	initParams.handle = (u32)pTexture->name;
	initParams.width = pTexture->realWidth;
	initParams.height = pTexture->realHeight;
	initParams.internalFormat = gli_ctx.ops->convertInternalTextureFormat(gli_ctx.impl, GLI_ICF_RGBA8);
	initParams.format = GLI_CF_RGBA;
	initParams.dataType = GLI_DT_UNSIGNED_BYTE;
	gli_ctx.ops->init2DTexture(gli_ctx.impl, &initParams);

	setParams.handle = (u32)pTexture->name;
	setParams.target = GLI_TT_TEXTURE_2D;
	setParams.minFilter = _filter;
	setParams.magFilter = _filter;
	gli_ctx.ops->setTextureParameters(gli_ctx.impl, &setParams);

	bufTarget.bufferHandle = _pBuffer->fb.m_FBO;
	bufTarget.bufferTarget = gli_ctx.drawFramebuffer;
	bufTarget.attachment = GLI_BA_COLOR_ATTACHMENT0;
	bufTarget.textureTarget = GLI_TT_TEXTURE_2D;
	bufTarget.textureHandle = pTexture->name;
	gli_ctx.ops->addFrameBufferRenderTarget(gli_ctx.impl, &bufTarget);
}

/* the stereo left eye: a copy of the buffer's color image */
static FrameBuffer *create_stereo_buffer(const FrameBuffer *_pMainBuffer)
{
	FrameBuffer *pLeftEye = own_buffer_new();
	const gli_fb *main = &_pMainBuffer->fb;
	const gli_tex *pMainTexture = main->m_pTexture;
	gli_tex *pTexture;

	if (pLeftEye == NULL)
		return NULL;
	pTexture = pLeftEye->fb.m_pTexture;

	pLeftEye->fb.m_startAddress = main->m_startAddress;
	pLeftEye->fb.m_width = main->m_width;
	pLeftEye->fb.m_height = main->m_height;
	pLeftEye->fb.m_size = main->m_size;
	pLeftEye->fb.m_scale = main->m_scale;

	pTexture->format = G_IM_FMT_RGBA;
	pTexture->size = pMainTexture->size;
	pTexture->address = pMainTexture->address;
	pTexture->clampS = 1;
	pTexture->clampT = 1;
	pTexture->clampWidth = pMainTexture->clampWidth;
	pTexture->clampHeight = pMainTexture->clampHeight;
	pTexture->frameBufferTexture = GLI_TEX_FB_ONE_SAMPLE;
	pTexture->maskS = 0;
	pTexture->maskT = 0;
	pTexture->mirrorS = 0;
	pTexture->mirrorT = 0;
	pTexture->width = pMainTexture->width;
	pTexture->height = pMainTexture->height;
	pTexture->realWidth = pMainTexture->realWidth;
	pTexture->realHeight = pMainTexture->realHeight;
	pTexture->textureBytes = pTexture->realWidth * pTexture->realHeight * 4;

	own_buffer_attach(pLeftEye, GLI_TP_FILTER_LINEAR);
	return pLeftEye;
}

bool gli_fb_stereo_copy(FrameBuffer *_pBuffer, FrameBuffer **_ppLeftEye)
{
	const gli_tex *pSource = _pBuffer->fb.m_pTexture;
	FrameBuffer *pLeftEye = *_ppLeftEye;
	bool copied;
	gli_blit_params params = GLI_BLIT_PARAMS_DEFAULT;

	if (pLeftEye == NULL ||
		pLeftEye->fb.m_pTexture->realWidth != pSource->realWidth ||
		pLeftEye->fb.m_pTexture->realHeight != pSource->realHeight) {
		own_buffer_delete(pLeftEye);
		pLeftEye = create_stereo_buffer(_pBuffer);
		*_ppLeftEye = pLeftEye;
		if (pLeftEye == NULL)
			return 0;
	}

	params.readBuffer = _pBuffer->fb.m_FBO;
	if (pSource->frameBufferTexture == GLI_TEX_FB_MULTI_SAMPLE) {
		gli_fb_resolve_texture(&_pBuffer->fb, 1);
		params.readBuffer = _pBuffer->fb.m_resolveFBO;
	}

	params.drawBuffer = pLeftEye->fb.m_FBO;
	params.srcX0 = 0;
	params.srcY0 = 0;
	params.srcX1 = pSource->realWidth;
	params.srcY1 = pSource->realHeight;
	params.dstX0 = 0;
	params.dstY0 = 0;
	params.dstX1 = pLeftEye->fb.m_pTexture->realWidth;
	params.dstY1 = pLeftEye->fb.m_pTexture->realHeight;
	params.mask = GLI_BM_COLOR_BUFFER;
	params.filter = GLI_TP_FILTER_NEAREST;

	copied = gli_ctx.ops->blitFramebuffers(gli_ctx.impl, &params);

	BIND_FB(gli_ctx.drawFramebuffer, 0);
	return copied;
}

void gli_fb_stereo_delete(FrameBuffer *_pBuffer)
{
	own_buffer_delete(_pBuffer);
}

/* post-processing */
FrameBuffer *gli_fb_pp_create(const FrameBuffer *_pMainBuffer)
{
	FrameBuffer *pResultBuffer = own_buffer_new();
	const gli_tex *pMainTexture = _pMainBuffer->fb.m_pTexture;
	gli_tex *pTexture;

	if (pResultBuffer == NULL)
		return NULL;
	pResultBuffer->fb.m_width = _pMainBuffer->fb.m_width;
	pResultBuffer->fb.m_height = _pMainBuffer->fb.m_height;
	pResultBuffer->fb.m_scale = _pMainBuffer->fb.m_scale;

	pTexture = pResultBuffer->fb.m_pTexture;
	pTexture->format = G_IM_FMT_RGBA;
	pTexture->clampS = 1;
	pTexture->clampT = 1;
	pTexture->frameBufferTexture = GLI_TEX_FB_ONE_SAMPLE;
	pTexture->maskS = 0;
	pTexture->maskT = 0;
	pTexture->mirrorS = 0;
	pTexture->mirrorT = 0;
	pTexture->realWidth = pMainTexture->realWidth;
	pTexture->realHeight = pMainTexture->realHeight;
	pTexture->textureBytes = pTexture->realWidth * pTexture->realHeight * 4;

	own_buffer_attach(pResultBuffer, GLI_TP_FILTER_NEAREST);
	assert(!gli_ctx.ops->isFramebufferError(gli_ctx.impl));
	return pResultBuffer;
}

void gli_fb_pp_draw(FrameBuffer *_pBuffer, FrameBuffer *_pResult, struct gli_cprog *_pShader)
{
	gli_tex *pTextureOriginal;
	gli_tex *pDstTex;
	gli_copy_params copyParams;

	if (_pBuffer->fb.m_pTexture->frameBufferTexture == GLI_TEX_FB_MULTI_SAMPLE) {
		gli_fb_resolve_texture(&_pBuffer->fb, 1);
		pTextureOriginal = _pBuffer->fb.m_pResolveTexture;
	} else
		pTextureOriginal = _pBuffer->fb.m_pTexture;

	BIND_FB(gli_ctx.readFramebuffer, 0);
	BIND_FB(gli_ctx.drawFramebuffer, _pResult->fb.m_FBO);

	pDstTex = _pResult->fb.m_pTexture;
	memset(&copyParams, 0, sizeof(copyParams));
	copyParams.r.srcX0 = 0;
	copyParams.r.srcY0 = 0;
	copyParams.r.srcX1 = pTextureOriginal->realWidth;
	copyParams.r.srcY1 = pTextureOriginal->realHeight;
	copyParams.r.srcWidth = pTextureOriginal->realWidth;
	copyParams.r.srcHeight = pTextureOriginal->realHeight;
	copyParams.r.dstX0 = 0;
	copyParams.r.dstY0 = 0;
	copyParams.r.dstX1 = pDstTex->realWidth;
	copyParams.r.dstY1 = pDstTex->realHeight;
	copyParams.r.dstWidth = pDstTex->realWidth;
	copyParams.r.dstHeight = pDstTex->realHeight;
	copyParams.tex[0] = pTextureOriginal;
	copyParams.filter = GLI_TP_FILTER_NEAREST;
	copyParams.combiner = _pShader;

	gli_drawer_copy(&copyParams);

	BIND_FB(gli_ctx.drawFramebuffer, 0);

	gli_ctx.ops->resetShaderProgram(gli_ctx.impl);
}

void gli_fb_delete(FrameBuffer *_pBuffer)
{
	own_buffer_delete(_pBuffer);
}
