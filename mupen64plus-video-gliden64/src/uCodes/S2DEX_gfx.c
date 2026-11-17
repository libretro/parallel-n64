/* What the S2DEX microcode does with the frame buffers, the depth buffers
 * and the drawer (gli_s2dex.h). C89. */
#include "Config.h"
#include "GBI.h"
#include "gDP.h"
#include "gSP.h"
#include "gli_fb_rdram.h"
#include "gli_framebuffer.h"
#include "gli_drawer.h"
#include "DepthBuffer.h"
#include "DisplayWindow.h"
#include "TexrectDrawer.h"
#include "Graphics/gli_context.h"
#include "gli_s2dex.h"

void gli_s2dex_mark_current_ob_screen(void)
{
	struct gli_fb *pBuffer = gli_fb_of(gli_fb_current());
	if (pBuffer != NULL)
		pBuffer->m_isOBScreen = 1;
}

void gli_s2dex_load_fb_image(void)
{
	const struct gli_fb *pBuffer = gli_fb_of(gli_fb_find(gSP.bgImage.address));
	gDP.tiles[0].frameBufferAddress = pBuffer->m_startAddress;
	gDP.tiles[0].textureMode = TEXTUREMODE_FRAMEBUFFER_BG;
	gDP.tiles[0].loadType = LOADTYPE_TILE;
	gDP.changed |= CHANGED_TMEM;

	if ((config.generalEmulation.hacks & hack_ZeldaMM) != 0) {
		if (gDP.colorImage.address == gDP.depthImageAddress)
			gli_fb_set_copy_buffer(gli_fb_current());
	}
}

int gli_s2dex_use_fb_image(u32 _imagePtr, u32 _imageSiz, u32 _imageFmt)
{
	struct gli_fb *pBuffer;
	int fbImage;
	if (config.frameBufferEmulation.enable == 0)
		return 0;
	pBuffer = gli_fb_of(gli_fb_find(_imagePtr));
	fbImage = pBuffer != NULL &&
		pBuffer->m_size == _imageSiz &&
		(!pBuffer->m_isDepthBuffer || pBuffer->m_changed) &&
		(_imageFmt != G_IM_FMT_CI || _imageSiz != G_IM_SIZ_8b);
	if (fbImage && (pBuffer->m_cfb || !gli_fb_is_valid(pBuffer, 0, g_window.buffersSwapCount))) {
		gli_fb_remove(pBuffer->m_startAddress);
		fbImage = 0;
	}
	return fbImage;
}

void gli_s2dex_background_drawing(int _on)
{
	if (!_on)
		gli_texrect_draw();
	gli_drawer_set_background_mode(_on != 0);
}

/* The game copies its depth buffer into the current color buffer. Color
 * and depth formats differ, so it cannot be done directly: a depth buffer
 * at the color buffer's address is made and attached to the current frame
 * buffer, and the source depth is blitted into it through another. */
void gli_s2dex_copy_depth_buffer(void)
{
	const struct gli_fb *pCurrent, *pTmpBuffer;
	DepthBuffer *pCopyBufferDepth;
	gli_blit_params blit = GLI_BLIT_PARAMS_DEFAULT;
	if (!config.frameBufferEmulation.enable || !gli_ctx.blitFramebuffer)
		return;

	gli_depth_save(gDP.colorImage.address);
	/* Take any frame buffer and attach source depth buffer to it, to blit it into copy depth buffer */
	pCurrent = gli_fb_of(gli_fb_current());
	pTmpBuffer = gli_fb_of(gli_fb_find_tmp(pCurrent->m_startAddress));
	if (pTmpBuffer == NULL)
		return;
	pCopyBufferDepth = gli_depth_find(gSP.bgImage.address);
	if (pCopyBufferDepth == NULL)
		return;
	gli_depth_set_attachment(pCopyBufferDepth, pTmpBuffer->m_FBO, gli_ctx.readFramebuffer);

	blit.readBuffer = pTmpBuffer->m_FBO;
	blit.drawBuffer = pCurrent->m_FBO;
	blit.srcX1 = (s32)g_window.width;
	blit.srcY1 = (s32)g_window.height;
	blit.dstX1 = (s32)g_window.width;
	blit.dstY1 = (s32)g_window.height;
	blit.mask = GLI_BM_DEPTH_BUFFER;
	blit.filter = GLI_TP_FILTER_NEAREST;
	gli_ctx.ops->blitFramebuffers(gli_ctx.impl, &blit);

	/* Restore objects */
	if (pTmpBuffer->m_pDepthBuffer != NULL)
		gli_depth_set_attachment(pTmpBuffer->m_pDepthBuffer, pCurrent->m_FBO, gli_ctx.readFramebuffer);
	gli_ctx.ops->bindFramebuffer(gli_ctx.impl, gli_ctx.readFramebuffer, 0);

	/* Set back current depth buffer */
	gli_depth_save(gDP.depthImageAddress);
}

