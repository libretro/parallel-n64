#include <string.h>
#include "RDRAMtoColorBuffer.h"

#include <FrameBufferInfo.h>
#include <FrameBuffer.h>
#include <Combiner.h>
#include <Textures.h>
#include <Config.h>
#include <N64.h>
#include <RSP.h>
#include <VI.h>

#include <Graphics/Context.h>
#include <Graphics/Parameters.h>
#include <DisplayWindow.h>
#include <algorithm>

using namespace graphics;

RDRAMtoColorBuffer::RDRAMtoColorBuffer()
	: m_pCurBuffer(nullptr)
	, m_pTexture(nullptr)
	, m_pbuf(nullptr) {
	gli_r2c_marks_init(&m_marks);
}

RDRAMtoColorBuffer & RDRAMtoColorBuffer::get()
{
	static RDRAMtoColorBuffer toCB;
	return toCB;
}

void RDRAMtoColorBuffer::init()
{
	const FramebufferTextureFormats & fbTexFormats = gfxContext.getFramebufferTextureFormats();

	m_pTexture = textureCache().addFrameBufferTexture(textureTarget::TEXTURE_2D);
	m_pTexture->format = G_IM_FMT_RGBA;
	m_pTexture->size = 2;
	m_pTexture->clampS = 1;
	m_pTexture->clampT = 1;
	m_pTexture->frameBufferTexture = CachedTexture::fbOneSample;
	m_pTexture->maskS = 0;
	m_pTexture->maskT = 0;
	m_pTexture->mirrorS = 0;
	m_pTexture->mirrorT = 0;
	m_pTexture->realWidth = GLI_R2C_WIDTH;
	m_pTexture->realHeight = GLI_R2C_HEIGHT;
	m_pTexture->textureBytes = m_pTexture->realWidth * m_pTexture->realHeight * fbTexFormats.colorFormatBytes;

	Context::InitTextureParams initParams;
	initParams.handle = m_pTexture->name;
	initParams.width = m_pTexture->realWidth;
	initParams.height = m_pTexture->realHeight;
	initParams.internalFormat = fbTexFormats.colorInternalFormat;
	initParams.format = fbTexFormats.colorFormat;
	initParams.dataType = fbTexFormats.colorType;
	gfxContext.init2DTexture(initParams);

	Context::TexParameters setParams;
	setParams.handle = m_pTexture->name;
	setParams.target = textureTarget::TEXTURE_2D;
	setParams.textureUnitIndex = textureIndices::Tex[0];
	setParams.minFilter = textureParameters::FILTER_LINEAR;
	setParams.magFilter = textureParameters::FILTER_LINEAR;
	gfxContext.setTextureParameters(setParams);

	/* the pixels are converted to ABGR8888 here first, then narrowed or
	 * widened in place: room for whichever is bigger */
	m_pbuf = (u8*)malloc(m_pTexture->realWidth * m_pTexture->realHeight *
		std::max(fbTexFormats.colorFormatBytes, 4u));
}

void RDRAMtoColorBuffer::destroy()
{
	if (m_pTexture != nullptr) {
		textureCache().removeFrameBufferTexture(m_pTexture);
		m_pTexture = nullptr;
	}
	free(m_pbuf);
	m_pbuf = nullptr;
}

void RDRAMtoColorBuffer::addAddress(u32 _address, u32 _size)
{
	if (m_pCurBuffer == nullptr) {
		m_pCurBuffer = frameBufferList().findBuffer(_address);
		if (m_pCurBuffer == nullptr)
			return;
	}

	const u32 pixelSize = 1 << m_pCurBuffer->m_size >> 1;
	if (_size != pixelSize && (_address%pixelSize) > 0)
		return;
	gli_r2c_mark(&m_marks, _address);
	gDP.colorImage.changed = TRUE;
}

void RDRAMtoColorBuffer::_copyFromRDRAM(u32 _height, bool _fullAlpha)
{
	Cleaner cleaner(this);
	const u32 address = m_pCurBuffer->m_startAddress;
	const u32 width = m_pCurBuffer->m_width;
	const u32 height = _height;

	const u32 x0 = 0;
	const u32 y0 = 0;
	const u32 y1 = y0 + height;

	const bool bUseAlpha = !_fullAlpha && m_pCurBuffer->m_changed;

	const FramebufferTextureFormats & fbTexFormats = gfxContext.getFramebufferTextureFormats();

	/* past the upload texture, and the buffer behind it */
	if (width > m_pTexture->realWidth || height > m_pTexture->realHeight || m_pbuf == nullptr)
		return;

	m_pTexture->width = width;
	m_pTexture->height = height;

	/* converted into the upload buffer itself, then put in the texture's
	 * format in place */
	u32 * pDst = reinterpret_cast<u32*>(m_pbuf);

	const bool bCopy = m_marks.count == 0 ?
		gli_r2c_copy_image(address, m_pCurBuffer->m_size, pDst, width, height, _fullAlpha) != 0 :
		gli_r2c_copy_marked(&m_marks, address, m_pCurBuffer->m_size, pDst, width, height, _fullAlpha) != 0;

	if (fbTexFormats.colorType == datatype::FLOAT)
		gli_r2c_to_float(m_pbuf, width * height);
	else if (fbTexFormats.colorType == datatype::UNSIGNED_SHORT_5_6_5)
		gli_r2c_to_565(m_pbuf, width * height);

	if (!gli_FBInfo_supported()) {
		if (bUseAlpha && config.frameBufferEmulation.copyToRDRAM == ctDisable) {
			u32 totalBytes = (width * height) << m_pCurBuffer->m_size >> 1;
			if (address + totalBytes > RDRAMSize + 1)
				totalBytes = RDRAMSize + 1 - address;
			RSP_StereoWillWrite(address, totalBytes);
			memset(RDRAM + address, 0, totalBytes);
		}
	}

	if (!bCopy)
		return;

	const u32 cycleType = gDP.otherMode.cycleType;
	gDP.otherMode.cycleType = G_CYC_COPY;
	CombinerInfo::get().setPolygonMode(DrawingState::TexRect);
	CombinerInfo::get().update();

	Context::UpdateTextureDataParams updateParams;
	updateParams.handle = m_pTexture->name;
	updateParams.textureUnitIndex = textureIndices::Tex[0];
	updateParams.width = width;
	updateParams.height = height;
	updateParams.format = fbTexFormats.colorFormat;
	updateParams.dataType = fbTexFormats.colorType;
	updateParams.data = m_pbuf;
	gfxContext.update2DTexture(updateParams);

	m_pTexture->scaleS = 1.0f / (float)m_pTexture->realWidth;
	m_pTexture->scaleT = 1.0f / (float)m_pTexture->realHeight;
	m_pTexture->shiftScaleS = 1.0f;
	m_pTexture->shiftScaleT = 1.0f;
	m_pTexture->offsetS = 0.0f;
	m_pTexture->offsetT = 0.0f;
	textureCache().activateTexture(0, m_pTexture);

	gDPTile tile0 = {0};
	gDPTile * pTile0 = gSP.textureTile[0];
	gSP.textureTile[0] = &tile0;

	gfxContext.enable(enable::BLEND, true);
	gfxContext.setBlending(blend::SRC_ALPHA, blend::ONE_MINUS_SRC_ALPHA);
	gfxContext.enable(enable::DEPTH_TEST, false);

	CombinerInfo::get().updateParameters();

	gfxContext.bindFramebuffer(bufferTarget::DRAW_FRAMEBUFFER, m_pCurBuffer->m_FBO);

	gfxContext.enable(enable::SCISSOR_TEST, false);
	GraphicsDrawer::TexturedRectParams texRectParams((float)x0, (float)y0, (float)width, (float)height,
										 1.0f, 1.0f, 0, 0,
										 false, true, false, m_pCurBuffer);
	dwnd().getDrawer().drawTexturedRect(texRectParams);
	gfxContext.enable(enable::SCISSOR_TEST, true);
	gDP.otherMode.cycleType = cycleType;

	frameBufferList().setCurrentDrawBuffer();

	gSP.textureTile[0] = pTile0;

	gDP.changed |= CHANGED_RENDERMODE | CHANGED_COMBINE;
}

void RDRAMtoColorBuffer::copyFromRDRAM(u32 _address, bool _bCFB)
{
	if (m_pCurBuffer == nullptr) {
		if (_bCFB || (config.frameBufferEmulation.copyFromRDRAM != 0 && !gli_FBInfo_supported()))
			m_pCurBuffer = frameBufferList().findBuffer(_address);
	} else {
		if (m_marks.count == 0) {
			m_pCurBuffer = nullptr;
			return;
		}
//		frameBufferList().setCurrent(m_pCurBuffer);
	}

	if (m_pCurBuffer == nullptr || m_pCurBuffer->m_size < G_IM_SIZ_16b)
		return;

	if (m_pCurBuffer->m_startAddress == _address && gDP.colorImage.changed != 0)
		return;

	/* Resident Evil 2: do not paint RDRAM over a buffer the RDP owns.
	 *
	 * The guard above is meant to stop exactly that, but it only fires when the
	 * address matches the start of the buffer. In this game's interlaced
	 * cutscenes the VI origin sits a line inside the page -- 0063e6f8 against a
	 * buffer at 0063e000 -- so the addresses differ and the guard is bypassed.
	 *
	 * Measured, frame by frame: the RDP draws roughly one frame in nine, and this
	 * copy runs on every one of them, after the drawing. It covers 416 rows while
	 * RDRAM only holds 334, so the rows below that are whatever memory happens to
	 * contain, painted over what was just drawn. The subtitles start at row 333.
	 *
	 * m_cfb is cleared by setBufferChanged as soon as the RDP draws into a
	 * buffer, so !_bCFB && !m_cfb reads as: an RDRAM copy nobody asked for, into
	 * a buffer the RDP is drawing. The film still reaches the screen through the
	 * genuine CFB uploads, which keep their path. */
	if ((config.generalEmulation.hacks & hack_RE2) != 0 && !_bCFB && !m_pCurBuffer->m_cfb)
		return;

	/* Resident Evil 2: never upload past the buffer's own height.
	 *
	 * When the address does not match the buffer start, the height taken here is
	 * gli_VI_GetMaxBufferHeight -- a global ceiling rather than anything about this
	 * buffer. Read back from the GPU on the pre-rendered rooms, the band of noise
	 * under the picture measures out to m_height, the tallest row the RDP ever
	 * rasterised into this buffer, which outlives the scene that set it:
	 *
	 *   432-wide room: picture ends at texture row 436, noise runs 437..488,
	 *                  488 / 1.4815 = 329 against the 295 rows shown;
	 *   340-wide room: picture ends at 442, noise runs 471..790,
	 *                  790 / 1.882  = 420 against the 235 rows shown.
	 *
	 * Both bands begin where the picture stops and end at m_height, and the twin
	 * buffer (0076a000) is clean black over the same rows, so this is fresh paint
	 * from the upload and not stale texture content. RDRAM holds the picture and
	 * nothing after it. */
	/* Resident Evil 2: fill a little past the VI's visible height.
	 *
	 * Read back from the GPU, the cutscene buffer is painted to exactly
	 * VI.real_height rows and not one more -- 136 in the 264x136 mode, which the
	 * capture confirms at texture row 329 over a scale of 2.4242 -- and the
	 * second line of dialogue is sliced through its glyphs at that boundary.
	 *
	 * Four rows, measured from the buffer captures rather than chosen: the text
	 * ends at N64 row 137.8 against a visible height of 136, and stale texture
	 * content begins at 140.3. Sixteen was the first attempt and stages four rows
	 * of that stale content for nothing.
	 *
	 * Not gli_VI_GetMaxBufferHeight: that adds 154 unwritten rows in this mode, which
	 * is where the band of noise under the picture came from. Anything below the
	 * picture is content nobody wrote this scene, so the margin has to stay
	 * small. */
	u32 fillHeight = VI.real_height;
	if ((config.generalEmulation.hacks & hack_RE2) != 0)
		fillHeight = std::min((u32)gli_VI_GetMaxBufferHeight(m_pCurBuffer->m_width),
		                      fillHeight + 4u);

	u32 maxHeight = gli_VI_GetMaxBufferHeight(m_pCurBuffer->m_width);
	if ((config.generalEmulation.hacks & hack_RE2) != 0) {
		if (m_pCurBuffer->m_height > 0)
			maxHeight = std::min(maxHeight, (u32)m_pCurBuffer->m_height);
		/* fillHeight, not VI.real_height: the confirmed subtitle fix lives in
		 * those four rows, and the VI origin sits a line inside the page in the
		 * cutscenes, so it may well be this branch that runs there. */
		maxHeight = std::min(maxHeight, fillHeight);
	}

	const u32 height = cutHeight(m_pCurBuffer->m_startAddress,
		m_pCurBuffer->m_startAddress == _address ?
			fillHeight :
			maxHeight, m_pCurBuffer->m_width << m_pCurBuffer->m_size >> 1);
	if (height == 0)
		return;

	_copyFromRDRAM(height, _bCFB);
}

void RDRAMtoColorBuffer::copyFromRDRAM(FrameBuffer * _pBuffer)
{
	if (_pBuffer == nullptr)
		return;
	m_pCurBuffer = _pBuffer;
	/* Resident Evil 2: same limit as the address form above.
	 *
	 * This overload takes gli_VI_GetMaxBufferHeight bare -- 290 for a 320-wide buffer
	 * and 580 above that. On the boot screens the noise ends at texture row 579
	 * of 580, exactly that ceiling, so this call is the one that paints it there.
	 * Bound it to what the VI actually shows. */
	u32 uploadHeight = gli_VI_GetMaxBufferHeight(m_pCurBuffer->m_width);
	if ((config.generalEmulation.hacks & hack_RE2) != 0 && VI.real_height > 0)
		uploadHeight = std::min(uploadHeight, VI.real_height + 4u);
	const u32 height = cutHeight(m_pCurBuffer->m_startAddress,
		uploadHeight, m_pCurBuffer->m_width << m_pCurBuffer->m_size >> 1);
	_copyFromRDRAM(height, true);
}

void RDRAMtoColorBuffer::reset()
{
	m_pCurBuffer = nullptr;
	gli_r2c_marks_clear(&m_marks);
}
