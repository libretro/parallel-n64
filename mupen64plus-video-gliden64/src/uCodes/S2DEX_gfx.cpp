#include "gli_s2dex.h"
#include "gSP.h"
#include "gDP.h"
#include "N64.h"
#include "GBI.h"
#include "Config.h"
#include "DepthBuffer.h"
#include "FrameBuffer.h"
#include "DisplayWindow.h"
#include <Graphics/Context.h>
#include <Graphics/Parameters.h>

using namespace graphics;

u32 gli_s2dex_hacks(void) { return config.generalEmulation.hacks; }
int gli_s2dex_bg_one_piece(void) { return config.graphics2D.bgMode == BGMode::bgOnePiece; }
int gli_s2dex_native_res_texrects(void) { return config.graphics2D.enableNativeResTexrects != 0; }

void gli_s2dex_mark_current_ob_screen(void)
{
	FrameBuffer *pBuffer = frameBufferList().getCurrent();
	if (pBuffer != nullptr)
		pBuffer->m_isOBScreen = true;
}

int gli_s2dex_depth_buffer_at(u32 _address)
{
	return gli_depth_find(_address) != nullptr;
}

void gli_s2dex_load_fb_image(void)
{
	FrameBuffer *pBuffer = frameBufferList().findBuffer(gSP.bgImage.address);
	gDP.tiles[0].frameBufferAddress = pBuffer->m_startAddress;
	gDP.tiles[0].textureMode = TEXTUREMODE_FRAMEBUFFER_BG;
	gDP.tiles[0].loadType = LOADTYPE_TILE;
	gDP.changed |= CHANGED_TMEM;

	if ((config.generalEmulation.hacks & hack_ZeldaMM) != 0) {
		if (gDP.colorImage.address == gDP.depthImageAddress)
			frameBufferList().setCopyBuffer(frameBufferList().getCurrent());
	}
}

int gli_s2dex_use_fb_image(u32 _imagePtr, u32 _imageSiz, u32 _imageFmt)
{
	FrameBuffer *pBuffer;
	bool fbImage;
	if (config.frameBufferEmulation.enable == 0)
		return 0;
	pBuffer = frameBufferList().findBuffer(_imagePtr);
	fbImage = pBuffer != nullptr &&
		pBuffer->m_size == _imageSiz &&
		(!pBuffer->m_isDepthBuffer || pBuffer->m_changed) &&
		(_imageFmt != G_IM_FMT_CI || _imageSiz != G_IM_SIZ_8b);
	if (fbImage && (pBuffer->m_cfb || !pBuffer->isValid(false))) {
		frameBufferList().removeBuffer(pBuffer->m_startAddress);
		fbImage = false;
	}
	return fbImage;
}

void gli_s2dex_background_drawing(int _on)
{
	GraphicsDrawer & drawer = dwnd().getDrawer();
	if (!_on)
		drawer.flush();
	drawer.setBackgroundDrawingMode(_on != 0);
}

void gli_s2dex_copy_depth_buffer(void)
{
	if (!config.frameBufferEmulation.enable)
		return;

	if (!Context::BlitFramebuffer)
		return;

	// The game copies content of depth buffer into current color buffer
	// OpenGL has different format for color and depth buffers, so this trick can't be performed directly
	// To do that, depth buffer with address of current color buffer created and attached to the current FBO
	// It will be copy depth buffer
	gli_depth_save(gDP.colorImage.address);
	// Take any frame buffer and attach source depth buffer to it, to blit it into copy depth buffer
	FrameBufferList & fbList = frameBufferList();
	FrameBuffer * pTmpBuffer = fbList.findTmpBuffer(fbList.getCurrent()->m_startAddress);
	if (pTmpBuffer == nullptr)
		return;
	DepthBuffer * pCopyBufferDepth = gli_depth_find(gSP.bgImage.address);
	if (pCopyBufferDepth == nullptr)
		return;
	pCopyBufferDepth->setDepthAttachment(pTmpBuffer->m_FBO, bufferTarget::READ_FRAMEBUFFER);

	DisplayWindow & wnd = dwnd();
	Context::BlitFramebuffersParams blitParams;
	blitParams.readBuffer = pTmpBuffer->m_FBO;
	blitParams.drawBuffer = fbList.getCurrent()->m_FBO;
	blitParams.srcX0 = 0;
	blitParams.srcY0 = 0;
	blitParams.srcX1 = wnd.getWidth();
	blitParams.srcY1 = wnd.getHeight();
	blitParams.dstX0 = 0;
	blitParams.dstY0 = 0;
	blitParams.dstX1 = wnd.getWidth();
	blitParams.dstY1 = wnd.getHeight();
	blitParams.mask = blitMask::DEPTH_BUFFER;
	blitParams.filter = textureParameters::FILTER_NEAREST;

	gfxContext.blitFramebuffers(blitParams);

	// Restore objects
	if (pTmpBuffer->m_pDepthBuffer != nullptr)
		pTmpBuffer->m_pDepthBuffer->setDepthAttachment(fbList.getCurrent()->m_FBO, bufferTarget::READ_FRAMEBUFFER);
	gfxContext.bindFramebuffer(bufferTarget::READ_FRAMEBUFFER, ObjectHandle::defaultFramebuffer);

	// Set back current depth buffer
	gli_depth_save(gDP.depthImageAddress);
}

