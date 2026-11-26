/*
Copyright (C) 2005 Rice1964

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
/* The N64 colour images Rice keeps track of, and the textures it draws
   them into (FrameBuffer.h). C89. */
#include <stdint.h>
#include <string.h>

#include <retro_miscellaneous.h>

#include "m64p_plugin.h"
#include "Config.h"
#include "ConvertImage.h"
#include "FrameBuffer.h"
#include "GraphicsContext.h"
#include "RenderBase.h"
#include "RiceGLState.h"
#include "RSP_Parser.h"
#include "UcodeDefs.h"
#include "Video.h"

#include "../../Graphics/RSP/RSP_state.h"

/* 0 keeps the most recent CI info
   1 keeps the frame buffer CI info which is being displayed now
   2 keeps the older frame buffer CI info. This can be used if we are
     using triple buffer */
RecentCIInfo g_RecentCIInfo[5];
RecentCIInfo *g_uRecentCIInfoPtrs[5] =
{
    &g_RecentCIInfo[0],
    &g_RecentCIInfo[1],
    &g_RecentCIInfo[2],
    &g_RecentCIInfo[3],
    &g_RecentCIInfo[4],
};

#define NUM_RECENT_CI_INFOS 5

RecentViOriginInfo g_RecentVIOriginInfo[5];

RenderTextureInfo gRenderTextureInfos[20];
int numOfTxtBufInfos = sizeof(gRenderTextureInfos)/sizeof(RenderTextureInfo);
RenderTextureInfo *g_pRenderTextureInfo = NULL;
RenderTextureInfo newRenderTextureInfo;

/* the render texture drawn into now, and the one drawn into last */
static int fb_cur_render_texture  = -1;
static int fb_last_render_texture = -1;

static void fb_delete_render_texture(int i)
{
    if (gRenderTextureInfos[i].pRenderTexture)
        CRenderTexture_Delete(gRenderTextureInfos[i].pRenderTexture);
    gRenderTextureInfos[i].pRenderTexture = NULL;
}

void RiceFB_CloseUp(void)
{
    int i;

    for (i = 0; i < numOfTxtBufInfos; i++)
        fb_delete_render_texture(i);
}

void RiceFB_Initialize(void)
{
    fb_last_render_texture = -1;
    fb_cur_render_texture  = -1;

    status.bCIBufferIsRendered = false;
    status.bN64IsDrawingTextureBuffer = false;
    status.bHandleN64RenderTexture = false;
    status.bN64FrameBufferIsUsed = false;

    memset(&gRenderTextureInfos[0], 0, sizeof(RenderTextureInfo)*numOfTxtBufInfos);
}

static void fb_update_recent_ci_addr(const SetImgInfo *ciinfo)
{
    RecentCIInfo *temp = NULL;
    int i;

    if (ciinfo->dwAddr == g_uRecentCIInfoPtrs[0]->dwAddr)
        return;

    for (i = 1; i < NUM_RECENT_CI_INFOS; i++)
    {
        if (ciinfo->dwAddr == g_uRecentCIInfoPtrs[i]->dwAddr)
        {
            int j;
            temp = g_uRecentCIInfoPtrs[i];

            for (j = i; j > 0; j--)
                g_uRecentCIInfoPtrs[j] = g_uRecentCIInfoPtrs[j-1];
            break;
        }
    }

    if (i >= NUM_RECENT_CI_INFOS)
    {
        temp = g_uRecentCIInfoPtrs[4];
        g_uRecentCIInfoPtrs[4] = g_uRecentCIInfoPtrs[3];
        g_uRecentCIInfoPtrs[3] = g_uRecentCIInfoPtrs[2];
        g_uRecentCIInfoPtrs[2] = g_uRecentCIInfoPtrs[1];
        g_uRecentCIInfoPtrs[1] = g_uRecentCIInfoPtrs[0];
        temp->dwCopiedAtFrame = 0;
        temp->bCopied = false;
    }

    g_uRecentCIInfoPtrs[0] = temp;

    /* Fix me here for Mario Tennis */
    temp->dwLastWidth = windowSetting.uViWidth;
    temp->dwLastHeight = windowSetting.uViHeight;

    temp->dwFormat = ciinfo->dwFormat;
    temp->dwAddr = ciinfo->dwAddr;
    temp->dwSize = ciinfo->dwSize;
    temp->dwWidth = ciinfo->dwWidth;
    temp->dwHeight = gRDP.scissor.bottom;
    temp->dwMemSize = (temp->dwWidth*temp->dwHeight/2)<<temp->dwSize;
    temp->bCopied = false;
    temp->lastUsedFrame = status.gDlistCount;
    temp->lastSetAtUcode = status.gUcodeCount;
}

/* Marks the CI entry used by the VI origin register: a real frame buffer,
   not a fake one, which the VI never shows */
void RiceFB_SetAddrBeDisplayed(uint32_t addr)
{
    uint32_t viwidth = *gfx_info.VI_WIDTH_REG;
    uint32_t minFrameCount = 0xffffffff;
    int index = 0;
    int i;

    addr &= (g_dwRamSize-1);

    for (i = 0; i < NUM_RECENT_CI_INFOS; i++)
    {
        if (g_uRecentCIInfoPtrs[i]->dwAddr+2*viwidth == addr)
            g_uRecentCIInfoPtrs[i]->bUsedByVIAtFrame = status.gDlistCount;
        else if (addr >= g_uRecentCIInfoPtrs[i]->dwAddr && addr < g_uRecentCIInfoPtrs[i]->dwAddr+0x1000)
            g_uRecentCIInfoPtrs[i]->bUsedByVIAtFrame = status.gDlistCount;
    }

    for (i = 0; i < NUM_RECENT_CI_INFOS; i++)
    {
        if (g_RecentVIOriginInfo[i].addr == addr)
        {
            g_RecentVIOriginInfo[i].FrameCount = status.gDlistCount;
            return;
        }
    }

    for (i = 0; i < NUM_RECENT_CI_INFOS; i++)
    {
        if (g_RecentVIOriginInfo[i].addr == 0)
        {
            /* Never used */
            g_RecentVIOriginInfo[i].addr = addr;
            g_RecentVIOriginInfo[i].FrameCount = status.gDlistCount;
            return;
        }
    }

    for (i = 0; i < NUM_RECENT_CI_INFOS; i++)
    {
        if (g_RecentVIOriginInfo[i].FrameCount < minFrameCount)
        {
            index = i;
            minFrameCount = g_RecentVIOriginInfo[i].FrameCount;
        }
    }

    g_RecentVIOriginInfo[index].addr = addr;
    g_RecentVIOriginInfo[index].FrameCount = status.gDlistCount;
}

static bool fb_has_addr_been_displayed(uint32_t addr, uint32_t width)
{
    int i;

    addr &= (g_dwRamSize-1);

    for (i = 0; i < NUM_RECENT_CI_INFOS; i++)
    {
        if (g_uRecentCIInfoPtrs[i]->dwAddr == 0)
            continue;

        if (g_uRecentCIInfoPtrs[i]->dwAddr == addr)
            return status.gDlistCount-g_uRecentCIInfoPtrs[i]->bUsedByVIAtFrame < 20;
    }

    for (i = 0; i < NUM_RECENT_CI_INFOS; i++)
    {
        if (g_RecentVIOriginInfo[i].addr != 0)
        {
            if (g_RecentVIOriginInfo[i].addr > addr &&
                (g_RecentVIOriginInfo[i].addr - addr)%width == 0 &&
                (g_RecentVIOriginInfo[i].addr - addr)/width <= 4)
                return status.gDlistCount-g_RecentVIOriginInfo[i].FrameCount < 20;
        }
    }

    return status.gDlistCount <= 20;
}

static int fb_find_recent_ci_info_index(uint32_t addr)
{
    int i;

    for (i = 0; i < NUM_RECENT_CI_INFOS; i++)
    {
        if (g_uRecentCIInfoPtrs[i]->dwAddr <= addr && addr < g_uRecentCIInfoPtrs[i]->dwAddr+g_uRecentCIInfoPtrs[i]->dwMemSize)
            return i;
    }

    return -1;
}

/* whether the depth image the CI was just set to is drawn into as a
   render texture, from the next ten commands */
static bool fb_is_di_a_render_texture(void)
{
    bool foundFillRect = false;
    bool foundSetFillColor = false;
    bool foundSetCImg = false;
    uint32_t newFillColor = 0;
    uint32_t dwPC = __RSP.PC[__RSP.PCi];       /* the next instruction */
    uint8_t *rdram_u8 = (uint8_t*)gfx_info.RDRAM;
    int i;

    for (i = 0; i < 10; i++)
    {
        uint32_t w0 = *(uint32_t *)(rdram_u8 + dwPC + i*8);
        uint32_t w1 = *(uint32_t *)(rdram_u8 + dwPC + 4 + i*8);

        if ((w0>>24) == G_SETSCISSOR)
            continue;

        if ((w0>>24) == G_SETFILLCOLOR)
        {
            foundSetFillColor = true;
            newFillColor = w1;
            continue;
        }

        if ((w0>>24) == G_FILLRECT)
        {
            uint32_t x0   = ((w1>>12)&0xFFF)/4;
            uint32_t y0   = ((w1>>0 )&0xFFF)/4;
            uint32_t x1   = ((w0>>12)&0xFFF)/4;

            if (x0 == 0 && y0 == 0)
            {
                if (x1 == g_CI.dwWidth || x1 == (unsigned int)(g_CI.dwWidth-1))
                {
                    foundFillRect = true;
                    continue;
                }
            }
        }

        if ((w0>>24) == G_TEXRECT)
            break;

        if ((w0>>24) == G_SETCIMG)
        {
            foundSetCImg = true;
            break;
        }
    }

    if (foundFillRect)
    {
        if (foundSetFillColor)
            return newFillColor != 0xFFFCFFFC;  /* else a normal ZImg */

        return gRDP.fillColor != 0x00FFFFF7;    /* else a normal ZImg */
    }

    return !(foundSetFillColor && newFillColor == 0xFFFCFFFC && foundSetCImg);
}

static uint32_t fb_compute_render_texture_crc_in_rdram(int infoIdx)
{
    uint8_t *rdram_u8 = (uint8_t*)gfx_info.RDRAM;
    RenderTextureInfo *info;
    uint32_t height, pitch;
    uint8_t *pAddr;

    if (infoIdx >= numOfTxtBufInfos || infoIdx < 0 || !gRenderTextureInfos[infoIdx].isUsed)
        return 0;

    info   = &gRenderTextureInfos[infoIdx];
    height = info->knownHeight ? info->N64Height : info->maxUsedHeight;
    pAddr  = (uint8_t*)(rdram_u8 + info->CI_Info.dwAddr);
    pitch  = (info->N64Width << info->CI_Info.dwSize ) >> 1;

    return CalculateRDRAMCRC(pAddr, 0, 0, info->N64Width, height, info->CI_Info.dwSize, pitch);
}

/* Drops the render textures a new CI covers; with byNewTxtrBuf, the one at
   the same address and size is kept, and its index given */
static int fb_check_render_textures_with_new_ci(const SetImgInfo *CIinfo, uint32_t height, bool byNewTxtrBuf)
{
    int matchidx = -1;
    uint32_t memsize = ((height*CIinfo->dwWidth)>>1)<<CIinfo->dwSize;
    int i;

    for (i = 0; i < numOfTxtBufInfos; i++)
    {
        RenderTextureInfo *info = &gRenderTextureInfos[i];
        bool covered = false;

        if (!info->isUsed)
            continue;

        if (info->CI_Info.dwAddr == CIinfo->dwAddr)
        {
            if (info->CI_Info.dwSize == CIinfo->dwSize &&
                info->CI_Info.dwWidth == CIinfo->dwWidth &&
                info->CI_Info.dwFormat == CIinfo->dwFormat &&
                info->N64Height == height)
            {
                /* This is the same texture at the same address */
                if (byNewTxtrBuf)
                {
                    matchidx = i;
                    break;
                }
            }

            /* At the same address, but not the same size */
            covered = true;
        }

        if (!covered)
        {
            uint32_t memsize2 = ((info->N64Height*info->N64Width)>>1)<<info->CI_Info.dwSize;

            if (info->CI_Info.dwAddr > CIinfo->dwAddr && info->CI_Info.dwAddr < CIinfo->dwAddr + memsize)
                covered = true;
            else if (info->CI_Info.dwAddr+memsize2 > CIinfo->dwAddr && info->CI_Info.dwAddr+memsize2 < CIinfo->dwAddr + memsize)
                covered = true;
            else if (CIinfo->dwAddr > info->CI_Info.dwAddr && CIinfo->dwAddr < info->CI_Info.dwAddr + memsize2)
                covered = true;
            else if (CIinfo->dwAddr+ memsize > info->CI_Info.dwAddr && CIinfo->dwAddr+ memsize < info->CI_Info.dwAddr + memsize2)
                covered = true;
        }

        if (covered)
        {
            info->isUsed = false;
            fb_delete_render_texture(i);
            info->txtEntry.pTexture = NULL;
        }
    }

    return matchidx;
}

/* An empty slot, else the oldest, its texture deleted */
static int fb_find_a_slot(void)
{
    int idx = -1;
    int i;

    for (i = 0; i < numOfTxtBufInfos; i++)
    {
        if (!gRenderTextureInfos[i].isUsed && gRenderTextureInfos[i].updateAtFrame < status.gDlistCount)
        {
            idx = i;
            break;
        }
    }

    if (idx < 0)
    {
        uint32_t oldestCount = 0xFFFFFFFF;
        idx = 0;
        for (i = 0; i < numOfTxtBufInfos; i++)
        {
            if (gRenderTextureInfos[i].updateAtUcodeCount < oldestCount)
            {
                oldestCount = gRenderTextureInfos[i].updateAtUcodeCount;
                idx = i;
            }
        }
    }

    fb_delete_render_texture(idx);
    return idx;
}

/* The back buffer saved as a render texture (MUDLORD: the frame buffer
   code sees the back buffer's address change and copies it into a
   texture) */
static int fb_set_back_buffer_as_render_texture(const SetImgInfo *CIinfo, int ciInfoIdx)
{
    RenderTextureInfo tempRenderTextureInfo;
    CRenderTexture *pRenderTexture;
    int matchidx, idxToUse;

    /* all of it is copied into the slot: zeros, not the stack's bytes, in
       what is not set here */
    memset(&tempRenderTextureInfo, 0, sizeof(tempRenderTextureInfo));
    memcpy(&tempRenderTextureInfo.CI_Info, CIinfo, sizeof(SetImgInfo));

    tempRenderTextureInfo.N64Width = g_uRecentCIInfoPtrs[ciInfoIdx]->dwLastWidth;
    tempRenderTextureInfo.N64Height = g_uRecentCIInfoPtrs[ciInfoIdx]->dwLastHeight;
    tempRenderTextureInfo.knownHeight = true;
    tempRenderTextureInfo.maxUsedHeight = 0;

    tempRenderTextureInfo.bufferWidth = windowSetting.uDisplayWidth;
    tempRenderTextureInfo.bufferHeight = windowSetting.uDisplayHeight;

    tempRenderTextureInfo.scaleX = tempRenderTextureInfo.bufferWidth / (float)tempRenderTextureInfo.N64Width;
    tempRenderTextureInfo.scaleY = tempRenderTextureInfo.bufferHeight / (float)tempRenderTextureInfo.N64Height;

    status.bFrameBufferIsDrawn = false;
    status.bFrameBufferDrawnByTriangles = false;

    tempRenderTextureInfo.updateAtFrame = status.gDlistCount;
    tempRenderTextureInfo.updateAtUcodeCount = status.gUcodeCount;

    matchidx = fb_check_render_textures_with_new_ci(CIinfo, tempRenderTextureInfo.N64Height, false);
    idxToUse = (matchidx >= 0) ? matchidx : fb_find_a_slot();

    if (gRenderTextureInfos[idxToUse].pRenderTexture == NULL || matchidx < 0)
        gRenderTextureInfos[idxToUse].pRenderTexture =
            CRenderTexture_New(tempRenderTextureInfo.bufferWidth, tempRenderTextureInfo.bufferHeight, &gRenderTextureInfos[idxToUse], AS_BACK_BUFFER_SAVE);

    pRenderTexture = gRenderTextureInfos[idxToUse].pRenderTexture;
    memcpy(&gRenderTextureInfos[idxToUse], &tempRenderTextureInfo, sizeof(RenderTextureInfo));
    gRenderTextureInfos[idxToUse].pRenderTexture = pRenderTexture;
    gRenderTextureInfos[idxToUse].isUsed = true;
    gRenderTextureInfos[idxToUse].txtEntry.pTexture = pRenderTexture->m_pTexture;
    gRenderTextureInfos[idxToUse].txtEntry.txtrBufIdx = idxToUse+1;

    return idxToUse;
}

/* Saves the back buffer, for frame-buffer effects, when the frame buffer
   changes. Writing it back into RDRAM is not done: that branch only
   marks it copied. */
static void fb_save_back_buffer(int ciInfoIdx, bool forceToSaveToRDRAM)
{
    RecentCIInfo *ciInfo = g_uRecentCIInfoPtrs[ciInfoIdx];
    SetImgInfo tempinfo;
    int idx;

    if (ciInfoIdx == 1)    /* to save the current front buffer */
        RiceGC_UpdateFrame();

    if (frameBufferOptions.bWriteBackBufToRDRAM || forceToSaveToRDRAM)
    {
        ciInfo->bCopied = true;
        if (ciInfoIdx == 1)
            RiceGC_UpdateFrame();
        return;
    }

    memset(&tempinfo, 0, sizeof(tempinfo));   /* its bpl too, never read */
    tempinfo.dwAddr = ciInfo->dwAddr;
    tempinfo.dwFormat = ciInfo->dwFormat;
    tempinfo.dwSize = ciInfo->dwSize;
    tempinfo.dwWidth = ciInfo->dwWidth;

    idx = fb_set_back_buffer_as_render_texture(&tempinfo, ciInfoIdx);

    gRenderTextureInfos[idx].crcCheckedAtFrame = status.gDlistCount;
    gRenderTextureInfos[idx].crcInRDRAM = fb_compute_render_texture_crc_in_rdram(idx);

    ciInfo->bCopied = true;
}

static int fb_check_addr_in_back_buffers(uint32_t addr)
{
    int r = fb_find_recent_ci_info_index(addr);

    if (r >= 0)
    {
        /* not if a recent render texture has overwritten it */
        int i;
        for (i = 0; i < numOfTxtBufInfos; i++)
        {
            uint32_t bufHeight = gRenderTextureInfos[i].knownHeight ? gRenderTextureInfos[i].N64Height : gRenderTextureInfos[i].maxUsedHeight;
            uint32_t bufMemSize = gRenderTextureInfos[i].CI_Info.dwSize*gRenderTextureInfos[i].N64Width*bufHeight;
            if (addr >= gRenderTextureInfos[i].CI_Info.dwAddr && addr < gRenderTextureInfos[i].CI_Info.dwAddr+bufMemSize)
            {
                if (g_uRecentCIInfoPtrs[r]->lastSetAtUcode < gRenderTextureInfos[i].updateAtUcodeCount)
                    return -1;
            }
        }

        if (status.gDlistCount - g_uRecentCIInfoPtrs[r]->lastUsedFrame <= 3 && g_uRecentCIInfoPtrs[r]->bCopied == false)
            fb_save_back_buffer(r, true);
    }

    return r;
}

void RiceFB_FrameBufferReadByCPU(uint32_t addr)
{
    int index;

    if (!frameBufferOptions.bProcessCPURead)
        return;

    addr &= (g_dwRamSize-1);
    index = fb_find_recent_ci_info_index(addr);
    /* the depth buffer too: it went on to read the entry before the
       recent CI list's first, then looked for a colour image again, found
       none and did nothing */
    if (index == -1)
        return;

    if (status.gDlistCount - g_uRecentCIInfoPtrs[index]->lastUsedFrame > 3)
        return;     /* we don't have this frame any more */

    if (g_uRecentCIInfoPtrs[index]->bCopied)
        return;

    fb_check_addr_in_back_buffers(addr);
}

/* The CI's height from the next ten commands, the scissor or a guess */
static uint32_t fb_compute_cimg_height(const SetImgInfo *info, uint32_t *height)
{
    uint32_t dwPC = __RSP.PC[__RSP.PCi];       /* the next instruction */
    uint8_t *rdram_u8 = (uint8_t*)gfx_info.RDRAM;
    int i;

    for (i = 0; i < 10; i++)
    {
        uint32_t w0 = *(uint32_t *)(rdram_u8 + dwPC + i*8);
        uint32_t w1 = *(uint32_t *)(rdram_u8 + dwPC + 4 + i*8);

        if ((w0>>24) == G_SETSCISSOR)
        {
            *height = ((w1>>0 )&0xFFF)/4;
            return G_SETSCISSOR;
        }

        if ((w0>>24) == G_FILLRECT)
        {
            uint32_t x0   = ((w1>>12)&0xFFF)/4;
            uint32_t y0   = ((w1>>0 )&0xFFF)/4;
            uint32_t x1   = ((w0>>12)&0xFFF)/4;
            uint32_t y1   = ((w0>>0 )&0xFFF)/4;

            if (x0 == 0 && y0 == 0)
            {
                if (x1 == info->dwWidth)
                {
                    *height = y1;
                    return G_FILLRECT;
                }

                if (x1 == (unsigned int)(info->dwWidth-1))
                {
                    *height = y1+1;
                    return G_FILLRECT;
                }
            }
        }

        if ((w0>>24) == G_SETCIMG)
            goto step2;
    }

    if (gRDP.scissor.left == 0 && gRDP.scissor.top == 0 && (unsigned int)gRDP.scissor.right == info->dwWidth)
    {
        *height = gRDP.scissor.bottom;
        return G_SETSCISSOR+1;
    }

step2:
    *height = info->dwWidth*3/4;
    if (status.dwTvSystem == TV_SYSTEM_PAL)
        *height = info->dwWidth*9/11;

    if (gRDP.scissor.bottom < (int)*height && gRDP.scissor.bottom != 0)
        *height = gRDP.scissor.bottom;

    if (info->dwAddr + *height*info->dwWidth*info->dwSize >= g_dwRamSize)
    {
        *height = info->dwWidth*3/4;
        if (status.dwTvSystem == TV_SYSTEM_PAL)
            *height = info->dwWidth*9/11;

        if (gRDP.scissor.bottom < (int)*height && gRDP.scissor.bottom != 0)
            *height = gRDP.scissor.bottom;

        if (info->dwAddr + *height*info->dwWidth*info->dwSize >= g_dwRamSize)
            *height = ( g_dwRamSize - info->dwAddr ) / info->dwWidth;
    }

    return 0;
}

/* The CI drawn into as a render texture from its first draw on */
static void fb_set_render_texture(void)
{
    memcpy(&newRenderTextureInfo.CI_Info, &g_CI, sizeof(SetImgInfo));

    newRenderTextureInfo.N64Width = newRenderTextureInfo.CI_Info.dwWidth;
    newRenderTextureInfo.knownHeight = fb_compute_cimg_height(&g_CI, &newRenderTextureInfo.N64Height);

    status.bHandleN64RenderTexture = true;
    newRenderTextureInfo.maxUsedHeight = 0;

    if (defaultRomOptions.bInN64Resolution)
    {
        newRenderTextureInfo.bufferWidth = newRenderTextureInfo.N64Width;
        newRenderTextureInfo.bufferHeight = newRenderTextureInfo.N64Height;
    }
    else if (defaultRomOptions.bDoubleSizeForSmallTxtrBuf && newRenderTextureInfo.N64Width<=128 && newRenderTextureInfo.N64Height<=128)
    {
        newRenderTextureInfo.bufferWidth = newRenderTextureInfo.N64Width*2;
        newRenderTextureInfo.bufferHeight = newRenderTextureInfo.N64Height*2;
    }
    else
    {
        newRenderTextureInfo.bufferWidth = newRenderTextureInfo.N64Width;
        newRenderTextureInfo.bufferHeight = newRenderTextureInfo.N64Height;
    }

    newRenderTextureInfo.scaleX = newRenderTextureInfo.bufferWidth / (float)newRenderTextureInfo.N64Width;
    newRenderTextureInfo.scaleY = newRenderTextureInfo.bufferHeight / (float)newRenderTextureInfo.N64Height;

    status.bFrameBufferIsDrawn = false;
    status.bFrameBufferDrawnByTriangles = false;

    newRenderTextureInfo.updateAtFrame = status.gDlistCount;
    newRenderTextureInfo.updateAtUcodeCount = status.gUcodeCount;
}

static void fb_store_render_texture_to_rdram(int infoIdx)
{
    if (!frameBufferOptions.bRenderTextureWriteBack)
        return;

    if (infoIdx < 0)
        infoIdx = fb_last_render_texture;

    if (!gRenderTextureInfos[infoIdx].pRenderTexture)
        return;

    if (CRenderTexture_IsBeingRendered(gRenderTextureInfos[infoIdx].pRenderTexture))
        return;

    CRenderTexture_StoreToRDRAM(gRenderTextureInfos[infoIdx].pRenderTexture, infoIdx);
}

static void fb_restore_normal_back_buffer(void)
{
    if (fb_cur_render_texture >= 0 && fb_cur_render_texture < numOfTxtBufInfos)
    {
        if (gRenderTextureInfos[fb_cur_render_texture].pRenderTexture)
            CRenderTexture_SetAsRenderTarget(gRenderTextureInfos[fb_cur_render_texture].pRenderTexture, false);
        fb_last_render_texture = fb_cur_render_texture;
    }

    if (!status.bFrameBufferIsDrawn || !status.bFrameBufferDrawnByTriangles)
    {
        /* never drawn into */
        gRenderTextureInfos[fb_cur_render_texture].isUsed = false;
        fb_delete_render_texture(fb_cur_render_texture);
    }
}

void RiceFB_CloseRenderTexture(bool toSave)
{
    if (fb_cur_render_texture < 0)
        return;

    status.bHandleN64RenderTexture = false;
    if (!status.bDirectWriteIntoRDRAM)
    {
        fb_restore_normal_back_buffer();
        if (!toSave || !status.bFrameBufferIsDrawn || !status.bFrameBufferDrawnByTriangles)
        {
            fb_delete_render_texture(fb_cur_render_texture);
            gRenderTextureInfos[fb_cur_render_texture].isUsed = false;
        }
        else
        {
            fb_store_render_texture_to_rdram(-1);

            if (frameBufferOptions.bRenderTextureWriteBack)
            {
                fb_delete_render_texture(fb_cur_render_texture);
                gRenderTextureInfos[fb_cur_render_texture].isUsed = false;
            }
            else
            {
                g_pRenderTextureInfo->crcInRDRAM = fb_compute_render_texture_crc_in_rdram(fb_cur_render_texture);
                g_pRenderTextureInfo->crcCheckedAtFrame = status.gDlistCount;
            }
        }
    }

    SetScreenMult(windowSetting.uDisplayWidth/windowSetting.fViWidth, windowSetting.uDisplayHeight/windowSetting.fViHeight);
    RiceGL_UpdateClipRectangle();
    RiceGL_ApplyScissorWithClipRatio(false);
}

/* Drops the render textures RDRAM no longer holds, the CPU having
   written over them */
void RiceFB_CheckRenderTextureCRCInRDRAM(void)
{
    int i;

    for (i = 0; i < numOfTxtBufInfos; i++)
    {
        if (!gRenderTextureInfos[i].isUsed)
            continue;

        if (CRenderTexture_IsBeingRendered(gRenderTextureInfos[i].pRenderTexture))
            continue;

        if (gRenderTextureInfos[i].crcCheckedAtFrame < status.gDlistCount)
        {
            if (gRenderTextureInfos[i].crcInRDRAM != fb_compute_render_texture_crc_in_rdram(i))
            {
                fb_delete_render_texture(i);
                gRenderTextureInfos[i].isUsed = false;
            }
            else
                gRenderTextureInfos[i].crcCheckedAtFrame = status.gDlistCount;
        }
    }
}

/* The render texture addr is in, its RDRAM checked once a frame */
int RiceFB_CheckAddrInRenderTextures(uint32_t addr)
{
    int i;

    for (i = 0; i < numOfTxtBufInfos; i++)
    {
        uint32_t bufHeight, bufMemSize;

        if (!gRenderTextureInfos[i].isUsed)
            continue;

        if (CRenderTexture_IsBeingRendered(gRenderTextureInfos[i].pRenderTexture))
            continue;

        bufHeight = gRenderTextureInfos[i].knownHeight ? gRenderTextureInfos[i].N64Height : gRenderTextureInfos[i].maxUsedHeight;
        bufMemSize = gRenderTextureInfos[i].CI_Info.dwSize*gRenderTextureInfos[i].N64Width*bufHeight;
        if (addr >= gRenderTextureInfos[i].CI_Info.dwAddr && addr < gRenderTextureInfos[i].CI_Info.dwAddr+bufMemSize)
        {
            if (gRenderTextureInfos[i].crcCheckedAtFrame < status.gDlistCount)
            {
                if (gRenderTextureInfos[i].crcInRDRAM != fb_compute_render_texture_crc_in_rdram(i))
                {
                    /* RDRAM has been modified by CPU core */
                    fb_delete_render_texture(i);
                    gRenderTextureInfos[i].isUsed = false;
                    continue;
                }
                gRenderTextureInfos[i].crcCheckedAtFrame = status.gDlistCount;
            }

            return i;
        }
    }

    return -1;
}

/* Activates the render texture for drawing, on the CI's first draw */
void RiceFB_ActiveTextureBuffer(void)
{
    status.bCIBufferIsRendered = true;

    if (status.bHandleN64RenderTexture)
    {
        CRenderTexture *pRenderTexture;
        int matchidx = fb_check_render_textures_with_new_ci(&g_CI, newRenderTextureInfo.N64Height, true);
        int idxToUse = (matchidx >= 0) ? matchidx : fb_find_a_slot();

        if (gRenderTextureInfos[idxToUse].pRenderTexture == NULL || matchidx < 0)
        {
            int w = newRenderTextureInfo.bufferWidth;
            if (newRenderTextureInfo.knownHeight == G_SETSCISSOR && newRenderTextureInfo.CI_Info.dwAddr == g_ZI.dwAddr)
                w = gRDP.scissor.right;

            gRenderTextureInfos[idxToUse].pRenderTexture =
                CRenderTexture_New(w, newRenderTextureInfo.bufferHeight, &gRenderTextureInfos[idxToUse], AS_RENDER_TARGET);
        }

        pRenderTexture = gRenderTextureInfos[idxToUse].pRenderTexture;
        memcpy(&gRenderTextureInfos[idxToUse], &newRenderTextureInfo, sizeof(RenderTextureInfo));
        gRenderTextureInfos[idxToUse].pRenderTexture = pRenderTexture;
        gRenderTextureInfos[idxToUse].isUsed = true;
        gRenderTextureInfos[idxToUse].txtEntry.pTexture = pRenderTexture->m_pTexture;
        gRenderTextureInfos[idxToUse].txtEntry.txtrBufIdx = idxToUse+1;

        g_pRenderTextureInfo = &gRenderTextureInfos[idxToUse];

        if (fb_cur_render_texture >= 0 && gRenderTextureInfos[fb_cur_render_texture].isUsed && gRenderTextureInfos[fb_cur_render_texture].pRenderTexture)
            CRenderTexture_SetAsRenderTarget(gRenderTextureInfos[fb_cur_render_texture].pRenderTexture, false);

        if (CRenderTexture_SetAsRenderTarget(gRenderTextureInfos[idxToUse].pRenderTexture, true))
        {
            if (frameBufferOptions.bFillRectNextTextureBuffer)
                RiceGC_Clear(CLEAR_COLOR_BUFFER, gRDP.fillColor, 1.0f);
            else if (options.enableHackForGames == HACK_FOR_MARIO_TENNIS && g_pRenderTextureInfo->N64Width > 64 && g_pRenderTextureInfo->N64Width < 300)
                RiceGC_Clear(CLEAR_COLOR_BUFFER, 0, 1.0f);
            else if (options.enableHackForGames == HACK_FOR_MARIO_TENNIS && g_pRenderTextureInfo->N64Width < 64 && g_pRenderTextureInfo->N64Width > 32)
                RiceGC_Clear(CLEAR_COLOR_BUFFER, 0, 1.0f);

            fb_cur_render_texture = idxToUse;

            status.bDirectWriteIntoRDRAM = false;

            SetScreenMult(gRenderTextureInfos[fb_cur_render_texture].scaleX, gRenderTextureInfos[fb_cur_render_texture].scaleY);
            RiceGL_UpdateClipRectangle();
        }
    }
    else
    {
        fb_update_recent_ci_addr(&g_CI);
        fb_check_render_textures_with_new_ci(&g_CI, gRDP.scissor.bottom, false);
    }
}

/* Sets the CI address, for frame-buffer copies */
void RiceFB_SetCIAddr(const SetImgInfo *newCI)
{
    bool wasDrawingTextureBuffer = status.bN64IsDrawingTextureBuffer;
    status.bN64IsDrawingTextureBuffer = ( newCI->dwSize != G_IM_SIZ_16b || newCI->dwFormat != G_IM_FMT_RGBA || newCI->dwWidth < 200 || ( newCI->dwAddr != g_ZI.dwAddr && newCI->dwWidth != 512 && !fb_has_addr_been_displayed(newCI->dwAddr, newCI->dwWidth)) );
    status.bN64FrameBufferIsUsed = status.bN64IsDrawingTextureBuffer;

    if (!wasDrawingTextureBuffer && g_CI.dwAddr == g_ZI.dwAddr && status.bCIBufferIsRendered)
    {
        /* Conker is not actually using a backbuffer */
        if (options.enableHackForGames != HACK_FOR_CONKER && g_uRecentCIInfoPtrs[0]->bCopied == false)
        {
            fb_update_recent_ci_addr(&g_CI);
            fb_save_back_buffer(0, false);
        }
    }

    frameBufferOptions.bFillRectNextTextureBuffer = false;
    if (g_CI.dwAddr == newCI->dwAddr && status.bHandleN64RenderTexture && (g_CI.dwFormat != newCI->dwFormat || g_CI.dwSize != newCI->dwSize || g_CI.dwWidth != newCI->dwWidth))
    {
        /* Mario Tennis player shadow */
        RiceFB_CloseRenderTexture(true);
        if (options.enableHackForGames == HACK_FOR_MARIO_TENNIS)
            frameBufferOptions.bFillRectNextTextureBuffer = true;   /* Hack for Mario Tennis */
    }

    g_CI.dwAddr = newCI->dwAddr;
    g_CI.dwFormat = newCI->dwFormat;
    g_CI.dwSize = newCI->dwSize;
    g_CI.dwWidth = newCI->dwWidth;
    g_CI.bpl = newCI->bpl;

    if (g_CI.dwAddr == g_ZI.dwAddr && !status.bN64IsDrawingTextureBuffer)
    {
        if (fb_is_di_a_render_texture())
        {
            status.bN64IsDrawingTextureBuffer = true;
            status.bN64FrameBufferIsUsed = status.bN64IsDrawingTextureBuffer;
        }
    }

    status.bCIBufferIsRendered = false;
    status.leftRendered = status.topRendered = status.rightRendered = status.bottomRendered = -1;

    if (currentRomOptions.screenUpdateSetting==SCREEN_UPDATE_AT_CI_CHANGE && !status.bN64IsDrawingTextureBuffer)
    {
        if (status.curRenderBuffer == 0)
            status.curRenderBuffer = g_CI.dwAddr;
        else if (status.curRenderBuffer != g_CI.dwAddr)
        {
            status.curDisplayBuffer = status.curRenderBuffer;
            RiceGC_UpdateFrame();
            status.curRenderBuffer = g_CI.dwAddr;
        }
    }

    /* writing the back buffer into RDRAM at each frame is not done */
    if (frameBufferOptions.bAtEachFrameUpdate && !status.bHandleN64RenderTexture)
        status.curRenderBuffer = g_CI.dwAddr;

    switch (currentRomOptions.N64RenderToTextureEmuType)
    {
    case TXT_BUF_NONE:
        if (status.bHandleN64RenderTexture)
            RiceFB_CloseRenderTexture(false);
        status.bHandleN64RenderTexture = false; /* Don't handle N64 render_texture stuffs */
        if (!status.bN64IsDrawingTextureBuffer)
            fb_update_recent_ci_addr(&g_CI);
        break;
    default:
        if (status.bHandleN64RenderTexture)
            RiceFB_CloseRenderTexture(true);

        status.bHandleN64RenderTexture = status.bN64IsDrawingTextureBuffer;
        if (status.bHandleN64RenderTexture && options.enableHackForGames != HACK_FOR_BANJO_TOOIE)
            fb_set_render_texture();
        break;
    }
}

/* ---- what the texture cache (TextureManager.c) asks ---- */

int RiceFB_CheckAddrInBackBuffers(uint32_t addr, uint32_t memsize)
{
    (void)memsize;
    return fb_check_addr_in_back_buffers(addr);
}

TxtrCacheEntry *RiceFB_RenderTextureEntry(int idx, const TxtrInfo *ti)
{
    RenderTextureInfo *info = &gRenderTextureInfos[idx];
    if (!info->pRenderTexture || info->CI_Info.dwFormat != ti->Format || info->CI_Info.dwSize != ti->Size)
        return NULL;
    info->txtEntry.ti = *ti;
    return &info->txtEntry;
}

uint32_t RiceFB_RenderTextureCRC(int idx)       { return gRenderTextureInfos[idx].crcInRDRAM; }
uint32_t RiceFB_RenderTextureUpdatedAt(int idx) { return gRenderTextureInfos[idx].updateAtFrame; }
uint32_t RiceFB_CIFormat(void)                  { return g_pRenderTextureInfo->CI_Info.dwFormat; }

/* Loads a texture from the render texture it is in */
void RiceFB_LoadTextureFromRenderTexture(TxtrCacheEntry *pEntry, int infoIdx)
{
    if (infoIdx < 0 || infoIdx >= numOfTxtBufInfos)
        infoIdx = RiceFB_CheckAddrInRenderTextures(pEntry->ti.Address);

    if (infoIdx >= 0 && gRenderTextureInfos[infoIdx].isUsed && gRenderTextureInfos[infoIdx].pRenderTexture)
        CRenderTexture_LoadTexture(gRenderTextureInfos[infoIdx].pRenderTexture, pEntry);
}
