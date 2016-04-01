#include <stdint.h>

#include <retro_miscellaneous.h>

#include "osal_preproc.h"
#include "float.h"
#include "ConvertImage.h"
#include "DeviceBuilder.h"
#include "FrameBuffer.h"
#include "Render.h"
#include "Timing.h"

#include "../../Graphics/GBI.h"

void ricegDPSetScissor(void *data, 
      uint32_t mode, float ulx, float uly, float lrx, float lry )
{
   ScissorType *tempScissor = (ScissorType*)data;

   tempScissor->mode = mode;
   tempScissor->x0   = ulx;
   tempScissor->y0   = uly;
   tempScissor->x1   = lrx;
   tempScissor->y1   = lry;
}

void ricegDPSetFillColor(uint32_t c)
{
   DP_Timing(DLParser_SetFillColor);
   gRDP.fillColor = Convert555ToRGBA(c);
}

void ricegDPSetFogColor(uint32_t r, uint32_t g, uint32_t b, uint32_t a)
{
   DP_Timing(DLParser_SetFogColor);
   CRender::g_pRender->SetFogColor(r, g, b, a );
}

void ricegDPFillRect(int32_t ulx, int32_t uly, int32_t lrx, int32_t lry )
{
   uint8_t *rdram_u8 = (uint8_t*)gfx_info.RDRAM;

   /* Note, in some modes, the right/bottom lines aren't drawn */

   LOG_UCODE("    (%d,%d) (%d,%d)", ulx, uly, lrx, lry);

   if( gRDP.otherMode.cycle_type >= G_CYC_COPY )
   {
      ++lrx;
      ++lry;
   }

   //TXTRBUF_DETAIL_DUMP(DebuggerAppendMsg("FillRect: X0=%d, Y0=%d, X1=%d, Y1=%d, Color=0x%08X", ulx, uly, lrx, lry, gRDP.originalFillColor););

   // Skip this
   if( status.bHandleN64RenderTexture && options.enableHackForGames == HACK_FOR_BANJO_TOOIE )
      return;

   if (IsUsedAsDI(g_CI.dwAddr))
   {
      // Clear the Z Buffer
      if( ulx != 0 || uly != 0 || windowSetting.uViWidth- lrx > 1 || windowSetting.uViHeight - lry > 1)
      {
         if( options.enableHackForGames == HACK_FOR_GOLDEN_EYE )
         {
            // GoldenEye is using double zbuffer
            if( g_CI.dwAddr == g_ZI.dwAddr )
            {
               // The zbuffer is the upper screen
               COORDRECT rect={int(ulx * windowSetting.fMultX),int(uly * windowSetting.fMultY),int(lrx * windowSetting.fMultX),int(lry * windowSetting.fMultY)};
               CRender::g_pRender->ClearBuffer(false,true,rect);   //Check me
               LOG_UCODE("    Clearing ZBuffer");
            }
            else
            {
               // The zbuffer is the lower screen
               int h = (g_CI.dwAddr-g_ZI.dwAddr)/g_CI.dwWidth/2;
               COORDRECT rect={int(ulx * windowSetting.fMultX),int((uly + h)*windowSetting.fMultY),int(lrx * windowSetting.fMultX),int((lry + h)*windowSetting.fMultY)};
               CRender::g_pRender->ClearBuffer(false,true,rect);   //Check me
               LOG_UCODE("    Clearing ZBuffer");
            }
         }
         else
         {
            COORDRECT rect={int(ulx * windowSetting.fMultX),int(uly * windowSetting.fMultY),int(lrx * windowSetting.fMultX),int(lry * windowSetting.fMultY)};
            CRender::g_pRender->ClearBuffer(false,true,rect);   //Check me
            LOG_UCODE("    Clearing ZBuffer");
         }
      }
      else
      {
         CRender::g_pRender->ClearBuffer(false,true);    //Check me
         LOG_UCODE("    Clearing ZBuffer");
      }

      DEBUGGER_PAUSE_AND_DUMP_COUNT_N( NEXT_FLUSH_TRI,{TRACE0("Pause after FillRect: ClearZbuffer\n");});
      DEBUGGER_PAUSE_AND_DUMP_COUNT_N( NEXT_FILLRECT, {DebuggerAppendMsg("ClearZbuffer: X0=%d, Y0=%d, X1=%d, Y1=%d, Color=0x%08X", ulx, uly, lrx, lry, gRDP.originalFillColor);
            DebuggerAppendMsg("Pause after ClearZbuffer: Color=%08X\n", gRDP.originalFillColor);});

      if( g_curRomInfo.bEmulateClear )
      {
         // Emulating Clear, by write the memory in RDRAM
         uint16_t color = (uint16_t)gRDP.originalFillColor;
         uint32_t pitch = g_CI.dwWidth<<1;
         int64_t base   = (int64_t)(rdram_u8 + g_CI.dwAddr);

         for( uint32_t i = uly; i < lry; i++ )
         {
            for( uint32_t j= ulx; j < lrx; j++ )
               *(uint16_t*)((base+pitch*i+j)^2) = color;
         }
      }
   }
   else if( status.bHandleN64RenderTexture )
   {
      if( !status.bCIBufferIsRendered )
         g_pFrameBufferManager->ActiveTextureBuffer();

      int cond1 =  (((int)ulx < status.leftRendered)     ? ((int)ulx) : (status.leftRendered));
      int cond2 =  (((int)uly < status.topRendered)      ? ((int)uly) : (status.topRendered));
      int cond3 =  ((status.rightRendered > ((int)lrx))  ? ((int)lrx) : (status.rightRendered));
      int cond4 =  ((status.bottomRendered > ((int)lry)) ? ((int)lry) : (status.bottomRendered));
      int cond5 =  ((g_pRenderTextureInfo->maxUsedHeight > ((int)lry)) ? ((int)lry) : (g_pRenderTextureInfo->maxUsedHeight));

      status.leftRendered = status.leftRendered < 0   ? ulx  : cond1;
      status.topRendered = status.topRendered<0       ? uly : cond2;
      status.rightRendered = status.rightRendered<0   ? lrx : cond3;
      status.bottomRendered = status.bottomRendered<0 ? lry : cond4;

      g_pRenderTextureInfo->maxUsedHeight = cond5;

      if( status.bDirectWriteIntoRDRAM || ( ulx ==0 && uly == 0 && (lrx == g_pRenderTextureInfo->N64Width || lrx == g_pRenderTextureInfo->N64Width-1 ) ) )
      {
         if( g_pRenderTextureInfo->CI_Info.dwSize == G_IM_SIZ_16b )
         {
            uint16_t color = (uint16_t)gRDP.originalFillColor;
            uint32_t pitch = g_pRenderTextureInfo->N64Width<<1;
            int64_t base   = (int64_t)(rdram_u8 + g_pRenderTextureInfo->CI_Info.dwAddr);
            for( uint32_t i = uly; i < lry; i++ )
            {
               for( uint32_t j= ulx; j< lrx; j++ )
                  *(uint16_t*)((base+pitch*i+j)^2) = color;
            }
         }
         else
         {
            uint8_t color  = (uint8_t)gRDP.originalFillColor;
            uint32_t pitch = g_pRenderTextureInfo->N64Width;
            int64_t base   = (int64_t) (rdram_u8 + g_pRenderTextureInfo->CI_Info.dwAddr);
            for( uint32_t i= uly; i< lry; i++ )
            {
               for( uint32_t j= ulx; j< lrx; j++ )
                  *(uint8_t*)((base+pitch*i+j)^3) = color;
            }
         }

         status.bFrameBufferDrawnByTriangles = false;
      }
      else
      {
         status.bFrameBufferDrawnByTriangles = true;
      }
      status.bFrameBufferDrawnByTriangles = true;

      if( !status.bDirectWriteIntoRDRAM )
      {
         status.bFrameBufferIsDrawn = true;

         //if( ulx ==0 && uly ==0 && (lrx == g_pRenderTextureInfo->N64Width || lrx == g_pRenderTextureInfo->N64Width-1 ) && gRDP.fillColor == 0)
         //{
         //  CRender::g_pRender->ClearBuffer(true,false);
         //}
         //else
         {
            if( gRDP.otherMode.cycle_type == G_CYC_FILL )
            {
               CRender::g_pRender->FillRect(ulx, uly, lrx, lry, gRDP.fillColor);
            }
            else
            {
               COLOR primColor = GetPrimitiveColor();
               CRender::g_pRender->FillRect(ulx, uly, lrx, lry, primColor);
            }
         }
      }

      DEBUGGER_PAUSE_AND_DUMP_COUNT_N( NEXT_FLUSH_TRI,{TRACE0("Pause after FillRect\n");});
      DEBUGGER_PAUSE_AND_DUMP_COUNT_N( NEXT_FILLRECT, {DebuggerAppendMsg("FillRect: X0=%d, Y0=%d, X1=%d, Y1=%d, Color=0x%08X", ulx, uly, lrx, lry, gRDP.originalFillColor);
            DebuggerAppendMsg("Pause after FillRect: Color=%08X\n", gRDP.originalFillColor);});
   }
   else
   {
      LOG_UCODE("    Filling Rectangle");
      if( frameBufferOptions.bSupportRenderTextures || frameBufferOptions.bCheckBackBufs )
      {
         int cond1 =  (((int)ulx < status.leftRendered)     ? ((int)ulx) : (status.leftRendered));
         int cond2 =  (((int)uly < status.topRendered)      ? ((int)uly) : (status.topRendered));
         int cond3 =  ((status.rightRendered > ((int)lrx))  ? ((int)lrx) : (status.rightRendered));
         int cond4 =  ((status.bottomRendered > ((int)lry)) ? ((int)lry) : (status.bottomRendered));
         int cond5 =  ((g_pRenderTextureInfo->maxUsedHeight > ((int)lry)) ? ((int)lry) : (g_pRenderTextureInfo->maxUsedHeight));

         if( !status.bCIBufferIsRendered ) g_pFrameBufferManager->ActiveTextureBuffer();

         status.leftRendered = status.leftRendered<0     ? ulx  : cond1;
         status.topRendered = status.topRendered<0       ? uly  : cond2;
         status.rightRendered = status.rightRendered<0   ? lrx  : cond3;
         status.bottomRendered = status.bottomRendered<0 ? lry  : cond4;
      }

      if( gRDP.otherMode.cycle_type == G_CYC_FILL )
      {
         if( !status.bHandleN64RenderTexture || g_pRenderTextureInfo->CI_Info.dwSize == G_IM_SIZ_16b )
            CRender::g_pRender->FillRect(ulx, uly, lrx, lry, gRDP.fillColor);
      }
      else
      {
         COLOR primColor = GetPrimitiveColor();
         //if( RGBA_GETALPHA(primColor) != 0 )
         {
            CRender::g_pRender->FillRect(ulx, uly, lrx, lry, primColor);
         }
      }
      DEBUGGER_PAUSE_AND_DUMP_COUNT_N( NEXT_FLUSH_TRI,{TRACE0("Pause after FillRect\n");});
      DEBUGGER_PAUSE_AND_DUMP_COUNT_N( NEXT_FILLRECT, {DebuggerAppendMsg("FillRect: X0=%d, Y0=%d, X1=%d, Y1=%d, Color=0x%08X",
               ulx, uly, lrx, lry, gRDP.originalFillColor);
            DebuggerAppendMsg("Pause after FillRect: Color=%08X\n", gRDP.originalFillColor);});
   }
}
