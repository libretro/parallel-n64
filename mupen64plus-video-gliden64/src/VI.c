#include <math.h>
#include "GLideN64.h"
#include "Types.h"
#include "VI.h"
#include "N64.h"
#include "gSP.h"
#include "gDP.h"
#include "RSP.h"
#include "GBI.h"
#include "Config.h"
#include "gli_framebuffer.h"

VIInfo VI = { 0, 0, 0, 0, 0.0f, 0.0f, (u32)-1, false, false };

u16 gli_VI_GetMaxBufferHeight(u16 _width)
{
	if (_width > 320 || VI.interlaced)
		return VI.PAL ? 580 : 480;

	return VI.PAL ? 290 : 240;
}

extern u32 LegacySm64ToolsHacks;
void gli_VI_UpdateSize(void)
{
	f32 xScale;
	u32 vScale, vEnd, vStart;
	bool interlacedPrev, depthMismatch;

	if (LegacySm64ToolsHacks)
	{
		if (*REG.VI_V_SYNC == 0x0627) *REG.VI_V_SYNC = 0x0834;
	}
	
	xScale = _FIXED2FLOAT( _SHIFTR( *REG.VI_X_SCALE, 0, 12 ), 10 );

	vScale = _SHIFTR(*REG.VI_Y_SCALE, 0, 12);

	/* These are in half-lines, so shift an extra bit */
	vEnd = _SHIFTR( *REG.VI_V_START, 0, 10 );
	vStart = _SHIFTR( *REG.VI_V_START, 16, 10 );
	interlacedPrev = VI.interlaced;
	if (VI.width > 0)
		VI.widthPrev = VI.width;

	VI.real_height = vEnd > vStart ? (((vEnd - vStart) >> 1) * vScale) >> 10 : 0;
	VI.width = *REG.VI_WIDTH;
	VI.interlaced = (*REG.VI_STATUS & 0x40) != 0;

	if (VI.interlaced) {
		f32 fullWidth = 640.0f;
		if ((*REG.VI_X_SCALE) % 512 == 0)
			fullWidth *= xScale;
		if (fullWidth > 0.0f && *REG.VI_WIDTH > fullWidth) {
			const u32 scale = (u32)floor(*REG.VI_WIDTH / fullWidth + 0.5f);
			VI.width /= scale;
			VI.real_height *= scale;
		}
		if (VI.real_height % 2 == 1)
			--VI.real_height;
	}

	VI.PAL = (*REG.VI_V_SYNC & 0x3ff) > 550;
	if (VI.PAL && (vEnd - vStart) > 478) {
		VI.height = (u32)(VI.real_height*1.0041841f);
		if (VI.height > 576)
			VI.height = VI.real_height = 576;
	}
	else {
		VI.height = (u32)(VI.real_height*1.0126582f);
		if (VI.height > 480)
			VI.height = VI.real_height = 480;
	}
	if (VI.height % 2 == 1)
		--VI.height;

	depthMismatch = gli_fb_depth_width_differs(VI.lastOrigin, VI.width);
	if (config.frameBufferEmulation.enable &&
		((interlacedPrev != VI.interlaced) ||
		(VI.width > 0 && VI.width != VI.widthPrev) ||
		(!VI.interlaced && depthMismatch))) {
		gli_fb_vi_resized(VI.widthPrev, VI.width);
	}

	VI.rwidth = VI.width != 0 ? 1.0f / VI.width : 0.0f;
	VI.rheight = VI.height != 0 ? 1.0f / VI.height : 0.0f;
}

void gli_VI_UpdateScreen(void)
{
	bool bVIUpdated;
	bool bCFB;
	bool bNeedSwap;
	struct FrameBuffer *pBuffer;
	u32 size;

	if (VI.lastOrigin == (u32)-1) /* Workaround for Mupen64Plus issue with initialization */
		gli_gfx_touch_error();


	if (ConfigOpen)
		return;

	if (gli_window_change())
		return;
	if (gli_window_resize())
		return;
	gli_window_screenshot();
	bVIUpdated = false;
	if (*REG.VI_ORIGIN != VI.lastOrigin) {
		gli_VI_UpdateSize();
		bVIUpdated = true;
		gli_window_update_scale();
	}

	if (config.frameBufferEmulation.enable) {

		pBuffer = gli_fb_find(*REG.VI_ORIGIN);
		if (pBuffer == NULL) {
			gDP.changed |= CHANGED_CPU_FB_WRITE;
		} else if (!gli_FBInfo_supported() &&
				 (config.generalEmulation.hacks & hack_RE2) == 0 &&
				 !gli_fb_is_valid_scanout(pBuffer)) {
			if (config.frameBufferEmulation.copyToRDRAM != 0 &&
				gli_fb_rendered_uncopied(pBuffer) &&
				(config.generalEmulation.hacks & hack_subscreen) == 0) {
				/* CPU writes landed under a buffer the RDP rendered. That
				 * buffer's RDRAM was never synchronized, so it does not hold
				 * the picture on screen -- handing the frame to the
				 * CPU-framebuffer path would tear the buffer down and present
				 * that stale memory. The console shows the last rendered
				 * picture here, so keep presenting the texture, and refresh
				 * the validity snapshot so the check settles once the writes
				 * stop.
				 *
				 * Only when a color-copy mechanism exists (copyToRDRAM on):
				 * m_copiedToRdram false then means the copy has not landed
				 * YET.  With the copy disabled the flag is false for every
				 * rendered buffer forever, and suppressing the takeover on
				 * it would silence the CPU-framebuffer path -- which does
				 * not need the color copy at all -- for any address that
				 * ever hosted 3D, freezing genuine CPU-drawn screens on the
				 * last rendered frame. */
				gli_fb_copy_rdram(pBuffer);
			} else {
				gDP.changed |= CHANGED_CPU_FB_WRITE;
				if (config.frameBufferEmulation.copyToRDRAM == 0 && (config.generalEmulation.hacks & hack_subscreen) == 0)
					gli_fb_copy_rdram(pBuffer);
			}
		}

		bCFB = (gDP.changed&CHANGED_CPU_FB_WRITE) == CHANGED_CPU_FB_WRITE;
		bNeedSwap = false;
		switch (config.frameBufferEmulation.bufferSwapMode) {
		case bsOnVerticalInterrupt:
			bNeedSwap = true;
			break;
		case bsOnVIOriginChange:
			bNeedSwap = bCFB ? true : (*REG.VI_ORIGIN != VI.lastOrigin);
			break;
		case bsOnColorImageChange:
			bNeedSwap = bCFB ? true : (gDP.colorImage.changed != 0);
			break;
		}

		if (bNeedSwap) {
			if (bCFB) {
				if (pBuffer == NULL || gli_fb_width(pBuffer) != VI.width) {
					if (!bVIUpdated) {
						gli_VI_UpdateSize();
						gli_window_update_scale();
						bVIUpdated = true;
					}
					size = *REG.VI_STATUS & 3;
					if (VI.height > 0 && size > G_IM_SIZ_8b  && VI.width > 0)
						gli_fb_save_cpu_buffer(*REG.VI_ORIGIN, size, VI.width);
				}
			}
			if (((*REG.VI_STATUS) & 3) > 0) {
				if (!bVIUpdated) {
					gli_VI_UpdateSize();
					bVIUpdated = true;
				}
				gli_fb_copy_from_rdram(*REG.VI_ORIGIN, bCFB);
			}
			gli_fb_render();
			gli_fb_clear_changed();
			VI.lastOrigin = *REG.VI_ORIGIN;
		}
	} else {
		if (gDP.changed & CHANGED_COLORBUFFER) {
			gli_fb_render();
			gDP.changed &= ~CHANGED_COLORBUFFER;
			VI.lastOrigin = *REG.VI_ORIGIN;
		}
	}

	if (VI.lastOrigin == (u32)-1) { /* Workaround for Mupen64Plus issue with initialization */
		gli_gfx_clear_color();
	}
}
