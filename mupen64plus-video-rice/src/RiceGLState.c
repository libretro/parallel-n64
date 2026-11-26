/*
Copyright (C) 2003 Rice1964

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/
/* Rice's GL render state (RiceGLState.h). C89. */
#include <stdint.h>

#include <retro_miscellaneous.h>

#include "osal_opengl.h"

#define M64P_PLUGIN_PROTOTYPES 1
#include "m64p_plugin.h"
#include "Config.h"
#include "FrameBuffer.h"
#include "RenderBase.h"
#include "RiceGLState.h"
#include "RSP_Parser.h"
#include "Video.h"

void RiceGL_ZBufferEnable(bool zbuffer)
{
    gRSP.bZBufferEnabled = zbuffer;
    if (g_curRomInfo.bForceDepthBuffer)
        zbuffer = true;
    if (zbuffer)
    {
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LEQUAL);
    }
    else
    {
        glDepthMask(GL_FALSE);
        glDepthFunc(GL_ALWAYS);
    }
}

void RiceGL_SetZCompare(bool compare)
{
    if (g_curRomInfo.bForceDepthBuffer)
        compare = true;

    gRSP.bZBufferEnabled = compare;
    glDepthFunc(compare ? GL_LEQUAL : GL_ALWAYS);
}

void RiceGL_SetZUpdate(bool update)
{
    if (g_curRomInfo.bForceDepthBuffer)
        update = true;
    glDepthMask(update ? GL_TRUE : GL_FALSE);
}

void RiceGL_ApplyZBias(int bias)
{
    static int old_bias;
    /* z offset = -3.0 * max(abs(dz/dx),abs(dz/dy)) per pixel delta z
     * slope, plus -3.0 * 1 bit */
    float f = bias > 0 ? -3.0f : 0.0f;

    if (bias == old_bias)
        return;
    old_bias = bias;

    if (bias > 0)
        glEnable(GL_POLYGON_OFFSET_FILL);
    else
        glDisable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(f, f);
}

void RiceGL_ClearBuffer(bool cbuffer, bool zbuffer)
{
    GLbitfield flag = 0;
    float depth     = ((gRDP.originalFillColor&0xFFFF)>>2)/(float)0x3FFF;

    if (cbuffer)
       flag |= GL_COLOR_BUFFER_BIT;
    if (zbuffer)
       flag |= GL_DEPTH_BUFFER_BIT;
    glClearDepth(depth);
    glClear(flag);
}

void RiceGL_ClearZBuffer(float depth)
{
    glClearDepth(depth);
    glClear(GL_DEPTH_BUFFER_BIT);
}

void RiceGL_SetCullMode(bool cullFront, bool cullBack)
{
    gRSP.bCullFront = cullFront;
    gRSP.bCullBack  = cullBack;
    if (cullFront || cullBack)
    {
        glCullFace(cullFront && cullBack ? GL_FRONT_AND_BACK : cullFront ? GL_FRONT : GL_BACK);
        glEnable(GL_CULL_FACE);
    }
    else
        glDisable(GL_CULL_FACE);
}

bool RiceGL_RE2Scissor(void)
{
    uint32_t vi_width = *gfx_info.VI_WIDTH_REG & 0xFFF;
    uint32_t height;

    if (!(options.bEnableHacks && g_CI.dwWidth == 0x200 && gRDP.scissor.right == 0x200 && g_CI.dwWidth > vi_width))
        return false;

    height = (gRDP.scissor.right*gRDP.scissor.bottom)/vi_width;
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, (int)(height * windowSetting.fMultY),
          (int)(vi_width*windowSetting.fMultX), (int)(height*windowSetting.fMultY));
    return true;
}

void RiceGL_ApplyRDPScissor(bool force)
{
    if (!force && status.curScissor == RDP_SCISSOR)
        return;

    if (!RiceGL_RE2Scissor())
        glScissor((int)(gRDP.scissor.left*windowSetting.fMultX),
              (int)((windowSetting.uViHeight-gRDP.scissor.bottom)*windowSetting.fMultY),
              (int)((gRDP.scissor.right-gRDP.scissor.left)*windowSetting.fMultX),
              (int)((gRDP.scissor.bottom-gRDP.scissor.top)*windowSetting.fMultY));

    status.curScissor = RDP_SCISSOR;
}

void RiceGL_ApplyScissorWithClipRatio(bool force)
{
    if (!force && status.curScissor == RSP_SCISSOR)
        return;

    glEnable(GL_SCISSOR_TEST);
    glScissor(windowSetting.clipping.left,
          (int)((windowSetting.uViHeight-gRSP.real_clip_scissor_bottom)*windowSetting.fMultY),
          windowSetting.clipping.width, windowSetting.clipping.height);

    status.curScissor = RSP_SCISSOR;
}

void RiceGL_UpdateScissorWithClipRatio(void)
{
    WindowSettingStruct *w = &windowSetting;
    float halfx, halfy, centerx, centery;

    gRSP.real_clip_scissor_left   = MAX(gRDP.scissor.left, gRSP.clip_ratio_left);
    gRSP.real_clip_scissor_top    = MAX(gRDP.scissor.top, gRSP.clip_ratio_top);
    gRSP.real_clip_scissor_right  = MIN(gRDP.scissor.right,gRSP.clip_ratio_right);
    gRSP.real_clip_scissor_bottom = MIN(gRDP.scissor.bottom, gRSP.clip_ratio_bottom);

    gRSP.real_clip_scissor_left   = MAX(gRSP.real_clip_scissor_left, 0);
    gRSP.real_clip_scissor_top    = MAX(gRSP.real_clip_scissor_top, 0);
    gRSP.real_clip_scissor_right  = MIN(gRSP.real_clip_scissor_right,windowSetting.uViWidth-1);
    gRSP.real_clip_scissor_bottom = MIN(gRSP.real_clip_scissor_bottom, windowSetting.uViHeight-1);

    w->clipping.left = (uint32_t)(gRSP.real_clip_scissor_left*windowSetting.fMultX);
    w->clipping.top  = (uint32_t)(gRSP.real_clip_scissor_top*windowSetting.fMultY);
    w->clipping.bottom = (uint32_t)(gRSP.real_clip_scissor_bottom*windowSetting.fMultY);
    w->clipping.right = (uint32_t)(gRSP.real_clip_scissor_right*windowSetting.fMultX);

    if( w->clipping.left > 0 || w->clipping.top > 0 || w->clipping.right < (uint32_t)windowSetting.uDisplayWidth-1 ||
        w->clipping.bottom < (uint32_t)windowSetting.uDisplayHeight-1 )
        w->clipping.needToClip = true;
    else
        w->clipping.needToClip = false;

    w->clipping.width = (uint32_t)((gRSP.real_clip_scissor_right-gRSP.real_clip_scissor_left+1)*windowSetting.fMultX);
    w->clipping.height = (uint32_t)((gRSP.real_clip_scissor_bottom-gRSP.real_clip_scissor_top+1)*windowSetting.fMultY);

    halfx = gRSP.nVPWidthN/2.0f;
    halfy = gRSP.nVPHeightN/2.0f;
    centerx = gRSP.nVPLeftN+halfx;
    centery = gRSP.nVPTopN+halfy;

    gRSP.real_clip_ratio_negx = (gRSP.real_clip_scissor_left - centerx)/halfx;
    gRSP.real_clip_ratio_negy = (gRSP.real_clip_scissor_top - centery)/halfy;
    gRSP.real_clip_ratio_posx = (gRSP.real_clip_scissor_right - centerx)/halfx;
    gRSP.real_clip_ratio_posy = (gRSP.real_clip_scissor_bottom - centery)/halfy;

    RiceGL_ApplyScissorWithClipRatio(true);
}

void RiceGL_UpdateClipRectangle(void)
{
    int halfx, halfy, centerx, centery;

    if( status.bHandleN64RenderTexture )
    {
        windowSetting.vpLeftW = 0;
        windowSetting.vpTopW = 0;
        windowSetting.vpRightW = newRenderTextureInfo.bufferWidth;
        windowSetting.vpBottomW = newRenderTextureInfo.bufferHeight;
        windowSetting.vpWidthW = newRenderTextureInfo.bufferWidth;
        windowSetting.vpHeightW = newRenderTextureInfo.bufferHeight;

        gRSP.vtxXMul = windowSetting.vpWidthW/2.0f;
        gRSP.vtxXAdd = gRSP.vtxXMul + windowSetting.vpLeftW;
        gRSP.vtxYMul = -windowSetting.vpHeightW/2.0f;
        gRSP.vtxYAdd = windowSetting.vpHeightW/2.0f + windowSetting.vpTopW;

        halfx = newRenderTextureInfo.bufferWidth/2;
        halfy = newRenderTextureInfo.bufferHeight/2;
        centerx = halfx;
        centery = halfy;
    }
    else
    {
        windowSetting.vpLeftW = (int)(gRSP.nVPLeftN * windowSetting.fMultX);
        windowSetting.vpTopW = (int)(gRSP.nVPTopN  * windowSetting.fMultY);
        windowSetting.vpRightW = (int)(gRSP.nVPRightN* windowSetting.fMultX);
        windowSetting.vpBottomW = (int)(gRSP.nVPBottomN* windowSetting.fMultY);
        windowSetting.vpWidthW = (int)((gRSP.nVPRightN - gRSP.nVPLeftN + 1) * windowSetting.fMultX);
        windowSetting.vpHeightW = (int)((gRSP.nVPBottomN - gRSP.nVPTopN + 1) * windowSetting.fMultY);

        gRSP.vtxXMul = windowSetting.vpWidthW/2.0f;
        gRSP.vtxXAdd = gRSP.vtxXMul + windowSetting.vpLeftW;
        gRSP.vtxYMul = -windowSetting.vpHeightW/2.0f;
        gRSP.vtxYAdd = windowSetting.vpHeightW/2.0f + windowSetting.vpTopW;

        halfx = gRSP.nVPWidthN/2;
        halfy = gRSP.nVPHeightN/2;
        centerx = gRSP.nVPLeftN+halfx;
        centery = gRSP.nVPTopN+halfy;
    }

    /* the clip rectangle, by the scissor */
    gRSP.clip_ratio_left = centerx - halfx * gRSP.clip_ratio_negx;
    gRSP.clip_ratio_top = centery - halfy * gRSP.clip_ratio_negy;
    gRSP.clip_ratio_right = centerx + halfx * gRSP.clip_ratio_posx;
    gRSP.clip_ratio_bottom = centery + halfy * gRSP.clip_ratio_posy;

    RiceGL_UpdateScissorWithClipRatio();
}

void RiceGL_SetFogColor(uint32_t r, uint32_t g, uint32_t b, uint32_t a)
{
    gRDP.fogColor      = COLOR_RGBA(r, g, b, a);
    gRDP.fvFogColor[0] = r/255.0f;
    gRDP.fvFogColor[1] = g/255.0f;
    gRDP.fvFogColor[2] = b/255.0f;
    gRDP.fvFogColor[3] = a/255.0f;
}
