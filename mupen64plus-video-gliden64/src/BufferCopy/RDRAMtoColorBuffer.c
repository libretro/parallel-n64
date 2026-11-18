/* The copy of what the CPU wrote to a frame buffer in RDRAM onto that
 * buffer's texture (RDRAMtoColorBuffer.h). C89. */
#include <stdlib.h>
#include <string.h>
#include "RDRAMtoColorBuffer.h"

#include <gli_framebuffer.h>
#include <gli_fb_rdram.h>
#include <gli_rdram_to_color.h>
#include <gli_textures.h>
#include <gli_tex.h>
#include <gli_combiner.h>
#include <gli_drawer.h>
#include <Config.h>
#include <GBI.h>
#include <N64.h>
#include <RSP.h>
#include <VI.h>
#include <gDP.h>
#include <gSP.h>
#include <Graphics/gli_context.h>

#define R2CB_MAX(_a, _b) ((_a) > (_b) ? (_a) : (_b))
#define R2CB_MIN(_a, _b) ((_a) < (_b) ? (_a) : (_b))

static struct
{
	struct FrameBuffer *pCurBuffer;
	gli_tex *pTexture;
	/* the pixels the CPU wrote, when it wrote some rather than all */
	gli_r2c_marks marks;
	u8 *pbuf;
} r2cb;

void gli_r2cb_init(void)
{
	const gli_fb_formats *fbTexFormats = &gli_ctx.formats;
	gli_init_texture_params initParams = GLI_INIT_TEXTURE_PARAMS_DEFAULT;
	gli_tex_parameters setParams = GLI_TEX_PARAMETERS_DEFAULT;
	gli_tex *t;

	t = r2cb.pTexture = gli_textures_add_fb(GLI_TT_TEXTURE_2D);
	t->format = G_IM_FMT_RGBA;
	t->size = 2;
	t->clampS = 1;
	t->clampT = 1;
	t->frameBufferTexture = GLI_TEX_FB_ONE_SAMPLE;
	t->maskS = 0;
	t->maskT = 0;
	t->mirrorS = 0;
	t->mirrorT = 0;
	t->realWidth = GLI_R2C_WIDTH;
	t->realHeight = GLI_R2C_HEIGHT;
	t->textureBytes = t->realWidth * t->realHeight * fbTexFormats->colorFormatBytes;

	initParams.handle = t->name;
	initParams.width = t->realWidth;
	initParams.height = t->realHeight;
	initParams.internalFormat = fbTexFormats->colorInternalFormat;
	initParams.format = fbTexFormats->colorFormat;
	initParams.dataType = fbTexFormats->colorType;
	gli_ctx.ops->init2DTexture(gli_ctx.impl, &initParams);

	setParams.handle = t->name;
	setParams.target = GLI_TT_TEXTURE_2D;
	setParams.textureUnitIndex = GLI_TU_TEX0;
	setParams.minFilter = GLI_TP_FILTER_LINEAR;
	setParams.magFilter = GLI_TP_FILTER_LINEAR;
	gli_ctx.ops->setTextureParameters(gli_ctx.impl, &setParams);

	/* the pixels are converted to ABGR8888 here first, then narrowed or
	 * widened in place: room for whichever is bigger */
	r2cb.pbuf = (u8 *)malloc(t->realWidth * t->realHeight * R2CB_MAX(fbTexFormats->colorFormatBytes, 4u));
}

void gli_r2cb_destroy(void)
{
	if (r2cb.pTexture != NULL) {
		gli_textures_remove_fb(r2cb.pTexture);
		r2cb.pTexture = NULL;
	}
	free(r2cb.pbuf);
	r2cb.pbuf = NULL;
}

void gli_r2cb_add_address(u32 _address, u32 _size)
{
	u32 pixelSize;
	if (r2cb.pCurBuffer == NULL) {
		r2cb.pCurBuffer = gli_fb_find(_address);
		if (r2cb.pCurBuffer == NULL)
			return;
	}

	pixelSize = 1 << gli_fb_of(r2cb.pCurBuffer)->m_size >> 1;
	if (_size != pixelSize && (_address % pixelSize) > 0)
		return;
	gli_r2c_mark(&r2cb.marks, _address);
	gDP.colorImage.changed = TRUE;
}

static void copy_from_rdram(u32 _height, bool _fullAlpha)
{
	/* the copy's buffer forgotten, and the current buffer put back, however
	 * it ends */
	struct FrameBuffer * const pCurrentBuffer = gli_fb_current();
	const gli_fb_formats *fbTexFormats = &gli_ctx.formats;
	gli_fb *buf = gli_fb_of(r2cb.pCurBuffer);
	gli_tex *t = r2cb.pTexture;
	const u32 address = buf->m_startAddress;
	const u32 width = buf->m_width;
	const u32 height = _height;
	const u32 x0 = 0;
	const u32 y0 = 0;
	const bool bUseAlpha = !_fullAlpha && buf->m_changed;
	u32 *pDst;
	bool bCopy;

	/* past the upload texture, and the buffer behind it */
	if (width > t->realWidth || height > t->realHeight || r2cb.pbuf == NULL)
		goto done;

	t->width = width;
	t->height = height;

	/* converted into the upload buffer itself, then put in the texture's
	 * format in place */
	pDst = (u32 *)r2cb.pbuf;

	bCopy = r2cb.marks.count == 0 ?
		gli_r2c_copy_image(address, buf->m_size, pDst, width, height, _fullAlpha) != 0 :
		gli_r2c_copy_marked(&r2cb.marks, address, buf->m_size, pDst, width, height, _fullAlpha) != 0;

	if (fbTexFormats->colorType == GLI_DT_FLOAT)
		gli_r2c_to_float(r2cb.pbuf, width * height);
	else if (fbTexFormats->colorType == GLI_DT_UNSIGNED_SHORT_5_6_5)
		gli_r2c_to_565(r2cb.pbuf, width * height);

	if (!gli_FBInfo_supported()) {
		if (bUseAlpha && config.frameBufferEmulation.copyToRDRAM == ctDisable) {
			u32 totalBytes = (width * height) << buf->m_size >> 1;
			if (address + totalBytes > RDRAMSize + 1)
				totalBytes = RDRAMSize + 1 - address;
			RSP_StereoWillWrite(address, totalBytes);
			memset(RDRAM + address, 0, totalBytes);
		}
	}

	if (!bCopy)
		goto done;

	{
		const u32 cycleType = gDP.otherMode.cycleType;
		gli_update_texture_params updateParams = GLI_UPDATE_TEXTURE_PARAMS_DEFAULT;
		gli_texrect_params texRectParams;
		gDPTile tile0;
		gDPTile *pTile0 = gSP.textureTile[0];

		gDP.otherMode.cycleType = G_CYC_COPY;
		gli_combiner_set_rect_mode(1);
		gli_combiner_update();

		updateParams.handle = t->name;
		updateParams.textureUnitIndex = GLI_TU_TEX0;
		updateParams.width = width;
		updateParams.height = height;
		updateParams.format = fbTexFormats->colorFormat;
		updateParams.dataType = fbTexFormats->colorType;
		updateParams.data = r2cb.pbuf;
		gli_ctx.ops->update2DTexture(gli_ctx.impl, &updateParams);

		t->scaleS = 1.0f / (float)t->realWidth;
		t->scaleT = 1.0f / (float)t->realHeight;
		t->shiftScaleS = 1.0f;
		t->shiftScaleT = 1.0f;
		t->offsetS = 0.0f;
		t->offsetT = 0.0f;
		gli_textures_activate(0, t);

		memset(&tile0, 0, sizeof(tile0));
		gSP.textureTile[0] = &tile0;

		gli_ctx.ops->enable(gli_ctx.impl, GLI_EN_BLEND, 1);
		gli_ctx.ops->setBlending(gli_ctx.impl, GLI_BLEND_SRC_ALPHA, GLI_BLEND_ONE_MINUS_SRC_ALPHA);
		gli_ctx.ops->enable(gli_ctx.impl, GLI_EN_DEPTH_TEST, 0);

		gli_combiner_update_parameters();

		gli_ctx.ops->bindFramebuffer(gli_ctx.impl, gli_ctx.drawFramebuffer, buf->m_FBO);

		gli_ctx.ops->enable(gli_ctx.impl, GLI_EN_SCISSOR_TEST, 0);
		texRectParams.ulx = (f32)x0;
		texRectParams.uly = (f32)y0;
		texRectParams.lrx = (f32)width;
		texRectParams.lry = (f32)height;
		texRectParams.dsdx = 1.0f;
		texRectParams.dtdy = 1.0f;
		texRectParams.s = 0;
		texRectParams.t = 0;
		texRectParams.flip = 0;
		texRectParams.forceAjustScale = 1;
		texRectParams.texrectCmd = 0;
		gli_drawer_draw_textured_rect(&texRectParams, r2cb.pCurBuffer);
		gli_ctx.ops->enable(gli_ctx.impl, GLI_EN_SCISSOR_TEST, 1);
		gDP.otherMode.cycleType = cycleType;

		gli_fb_set_current_draw_buffer();

		gSP.textureTile[0] = pTile0;

		gDP.changed |= CHANGED_RENDERMODE | CHANGED_COMBINE;
	}

done:
	r2cb.pCurBuffer = NULL;
	gli_r2c_marks_clear(&r2cb.marks);
	gli_fb_set_current(pCurrentBuffer);
}

void gli_r2cb_copy_from_rdram(u32 _address, bool _bCFB)
{
	gli_fb *buf;
	u32 fillHeight, maxHeight, height;

	if (r2cb.pCurBuffer == NULL) {
		if (_bCFB || (config.frameBufferEmulation.copyFromRDRAM != 0 && !gli_FBInfo_supported()))
			r2cb.pCurBuffer = gli_fb_find(_address);
	} else {
		if (r2cb.marks.count == 0) {
			r2cb.pCurBuffer = NULL;
			return;
		}
	}

	if (r2cb.pCurBuffer == NULL)
		return;
	buf = gli_fb_of(r2cb.pCurBuffer);
	if (buf->m_size < G_IM_SIZ_16b)
		return;

	if (buf->m_startAddress == _address && gDP.colorImage.changed != 0)
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
	if ((config.generalEmulation.hacks & hack_RE2) != 0 && !_bCFB && !buf->m_cfb)
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
	fillHeight = VI.real_height;
	if ((config.generalEmulation.hacks & hack_RE2) != 0)
		fillHeight = R2CB_MIN((u32)gli_VI_GetMaxBufferHeight((u16)buf->m_width), fillHeight + 4u);

	maxHeight = gli_VI_GetMaxBufferHeight((u16)buf->m_width);
	if ((config.generalEmulation.hacks & hack_RE2) != 0) {
		if (buf->m_height > 0)
			maxHeight = R2CB_MIN(maxHeight, (u32)buf->m_height);
		/* fillHeight, not VI.real_height: the confirmed subtitle fix lives in
		 * those four rows, and the VI origin sits a line inside the page in the
		 * cutscenes, so it may well be this branch that runs there. */
		maxHeight = R2CB_MIN(maxHeight, fillHeight);
	}

	height = cutHeight(buf->m_startAddress,
		buf->m_startAddress == _address ? fillHeight : maxHeight,
		buf->m_width << buf->m_size >> 1);
	if (height == 0)
		return;

	copy_from_rdram(height, _bCFB);
}

void gli_r2cb_copy_buffer_from_rdram(struct FrameBuffer *_pBuffer)
{
	gli_fb *buf;
	u32 uploadHeight, height;
	if (_pBuffer == NULL)
		return;
	r2cb.pCurBuffer = _pBuffer;
	buf = gli_fb_of(_pBuffer);
	/* Resident Evil 2: same limit as the address form above.
	 *
	 * This overload takes gli_VI_GetMaxBufferHeight bare -- 290 for a 320-wide buffer
	 * and 580 above that. On the boot screens the noise ends at texture row 579
	 * of 580, exactly that ceiling, so this call is the one that paints it there.
	 * Bound it to what the VI actually shows. */
	uploadHeight = gli_VI_GetMaxBufferHeight((u16)buf->m_width);
	if ((config.generalEmulation.hacks & hack_RE2) != 0 && VI.real_height > 0)
		uploadHeight = R2CB_MIN(uploadHeight, VI.real_height + 4u);
	height = cutHeight(buf->m_startAddress, uploadHeight, buf->m_width << buf->m_size >> 1);
	copy_from_rdram(height, 1);
}
