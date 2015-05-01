#include "Common.h"
#include "gles2N64.h"
#include "Types.h"
#include "VI.h"
#include "OpenGL.h"
#include "N64.h"
#include "gSP.h"
#include "gDP.h"
#include "RSP.h"
#include "Debug.h"
#include "Config.h"
#include "FrameBuffer.h"

VIInfo VI;

void VI_UpdateSize(void)
{
   f32 xScale = _FIXED2FLOAT( _SHIFTR( *gfx_info.VI_X_SCALE_REG, 0, 12 ), 10 );
   f32 xOffset = _FIXED2FLOAT( _SHIFTR( *gfx_info.VI_X_SCALE_REG, 16, 12 ), 10 );

   f32 yScale = _FIXED2FLOAT( _SHIFTR( *gfx_info.VI_Y_SCALE_REG, 0, 12 ), 10 );
   f32 yOffset = _FIXED2FLOAT( _SHIFTR( *gfx_info.VI_Y_SCALE_REG, 16, 12 ), 10 );

   u32 hEnd = _SHIFTR( *gfx_info.VI_H_START_REG, 0, 10 );
   u32 hStart = _SHIFTR( *gfx_info.VI_H_START_REG, 16, 10 );

   // These are in half-lines, so shift an extra bit
   u32 vEnd = _SHIFTR( *gfx_info.VI_V_START_REG, 1, 9 );
   u32 vStart = _SHIFTR( *gfx_info.VI_V_START_REG, 17, 9 );

   //Glide does this:
   if (hEnd == hStart) hEnd = (u32)(*gfx_info.VI_WIDTH_REG / xScale);


   VI.width = (u32)(xScale * (hEnd - hStart));
   VI.height = (u32)(yScale * 1.0126582f * (vEnd - vStart));

   if (VI.width == 0.0f) VI.width = 320;
   if (VI.height == 0.0f) VI.height = 240;
   VI.rwidth = 1.0f / VI.width;
   VI.rheight = 1.0f / VI.height;
}

void VI_UpdateScreen(void)
{
	static u32 uNumCurFrameIsShown = 0;
   bool bVIUpdated = false;

   if (*gfx_info.VI_ORIGIN_REG != VI.lastOrigin)
   {
      VI_UpdateSize();
      bVIUpdated = true;
   }

	if (config.frameBufferEmulation.enable)
   {
		const bool bCFB = config.frameBufferEmulation.detectCFB != 0 && (gSP.changed&CHANGED_CPU_FB_WRITE) == CHANGED_CPU_FB_WRITE;
		const bool bNeedUpdate = gDP.colorImage.changed != 0 || (bCFB ? true : (*gfx_info.VI_ORIGIN_REG != VI.lastOrigin));

		if (bNeedUpdate)
      {
			if ((gSP.changed&CHANGED_CPU_FB_WRITE) == CHANGED_CPU_FB_WRITE)
         {
				struct FrameBuffer * pBuffer = FrameBuffer_FindBuffer(*gfx_info.VI_ORIGIN_REG);
				if (pBuffer == NULL || pBuffer->width != VI.width)
            {
               u32 size;

					if (!bVIUpdated)
               {
						VI_UpdateSize();
						//ogl.updateScale();
						bVIUpdated = true;
					}
					size = *gfx_info.VI_STATUS_REG & 3;

					if (VI.height > 0 && size > G_IM_SIZ_8b  && VI.width > 0)
						FrameBuffer_SaveBuffer(*gfx_info.VI_ORIGIN_REG, G_IM_FMT_RGBA, size, VI.width, VI.height, true);
				}
			}
			if ((((*gfx_info.VI_STATUS_REG) & 3) > 0) && ((config.frameBufferEmulation.copyFromRDRAM && gDP.colorImage.changed) || bCFB))
         {
            if (!bVIUpdated)
            {
               VI_UpdateSize();
               bVIUpdated = true;
            }
            FrameBuffer_CopyFromRDRAM(*gfx_info.VI_ORIGIN_REG, config.frameBufferEmulation.copyFromRDRAM && !bCFB);
         }

			FrameBuffer_RenderBuffer(*gfx_info.VI_ORIGIN_REG);

			if (gDP.colorImage.changed)
				uNumCurFrameIsShown = 0;
			else
         {
            uNumCurFrameIsShown++;
            if (uNumCurFrameIsShown > 25)
               gSP.changed |= CHANGED_CPU_FB_WRITE;
         }
#if 0
         /* TODO/FIXME - implement */
			frameBufferList().clearBuffersChanged();
#endif
			VI.lastOrigin = *gfx_info.VI_ORIGIN_REG;
#ifdef DEBUG
			while (Debug.paused && !Debug.step);
			Debug.step = FALSE;
#endif
		} else {
			uNumCurFrameIsShown++;
			if (uNumCurFrameIsShown > 25)
				gSP.changed |= CHANGED_CPU_FB_WRITE;
		}
	}
   else
   {
      if (gSP.changed & CHANGED_COLORBUFFER)
      {
         OGL_SwapBuffers();
         gSP.changed &= ~CHANGED_COLORBUFFER;
         VI.lastOrigin = *gfx_info.VI_ORIGIN_REG;
      }
   }
}
