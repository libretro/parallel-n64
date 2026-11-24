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

// ===========================================================================

#include <vector>

#include <retro_miscellaneous.h>

#include "ConvertImage.h"
#include "DeviceBuilder.h"
#include "FrameBuffer.h"
#include "UcodeDefs.h"
#include "RSP_Parser.h"
#include "Render.h"

#include "../../Graphics/RSP/RSP_state.h"


// 0 keeps the most recent CI info
// 1 keeps the frame buffer CI info which is being displayed now
// 2 keeps the older frame buffer CI info. This can be used if we are using triple buffer
/* Overview of framebuffer implementation
1) Check if backbuffer has changed, via different detection techniques
2) If changed, we copy the GFX card's backbuffer to main RAM
3) This is slow due to the reading process, not the writing
*/

RecentCIInfo g_RecentCIInfo[5];
RecentCIInfo *g_uRecentCIInfoPtrs[5] =
{
    &g_RecentCIInfo[0],
    &g_RecentCIInfo[1],
    &g_RecentCIInfo[2],
    &g_RecentCIInfo[3],
    &g_RecentCIInfo[4],
};

int numOfRecentCIInfos = 5;

RecentViOriginInfo g_RecentVIOriginInfo[5];
uint32_t dwBackBufferSavedAtFrame=0;

RenderTextureInfo gRenderTextureInfos[20];
int numOfTxtBufInfos = sizeof(gRenderTextureInfos)/sizeof(RenderTextureInfo);
RenderTextureInfo *g_pRenderTextureInfo = NULL;

FrameBufferManager* g_pFrameBufferManager = NULL;

bool LastCIIsNewCI=false;

FrameBufferManager::FrameBufferManager() :
    m_isRenderingToTexture(false),
    m_curRenderTextureIndex(-1),
        m_lastTextureBufferIndex(-1)
{
}

FrameBufferManager::~FrameBufferManager()
{
}

void FrameBufferManager::CloseUp()
{
   unsigned i;

   for (i=0; i<numOfTxtBufInfos; i++)
   {
      if (gRenderTextureInfos[i].pRenderTexture)
         CRenderTexture_Delete(gRenderTextureInfos[i].pRenderTexture);
      gRenderTextureInfos[i].pRenderTexture = NULL;
   }
}

void FrameBufferManager::Initialize()
{
    m_isRenderingToTexture = false;
    m_lastTextureBufferIndex = -1;
    m_curRenderTextureIndex = -1;
    
    status.bCIBufferIsRendered = false;
    status.bN64IsDrawingTextureBuffer = false;
    status.bHandleN64RenderTexture = false;
    status.bN64FrameBufferIsUsed = false;

    memset(&gRenderTextureInfos[0], 0, sizeof(RenderTextureInfo)*numOfTxtBufInfos);
}
// ===========================================================================


void FrameBufferManager::UpdateRecentCIAddr(SetImgInfo &ciinfo)
{
    if (ciinfo.dwAddr == g_uRecentCIInfoPtrs[0]->dwAddr)
        return;

    RecentCIInfo *temp;

    int i;
    for( i=1; i<numOfRecentCIInfos; i++ )
    {
        if (ciinfo.dwAddr == g_uRecentCIInfoPtrs[i]->dwAddr)
        {
            temp = g_uRecentCIInfoPtrs[i];

            for (int j=i; j>0; j--)
            {
                g_uRecentCIInfoPtrs[j] = g_uRecentCIInfoPtrs[j-1];
            }
            break;
        }
    }

    if (i >= numOfRecentCIInfos)
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

    // Fix me here for Mario Tennis
    temp->dwLastWidth = windowSetting.uViWidth;
    temp->dwLastHeight = windowSetting.uViHeight;

    temp->dwFormat = ciinfo.dwFormat;
    temp->dwAddr = ciinfo.dwAddr;
    temp->dwSize = ciinfo.dwSize;
    temp->dwWidth = ciinfo.dwWidth;
    temp->dwHeight = gRDP.scissor.bottom;
    temp->dwMemSize = (temp->dwWidth*temp->dwHeight/2)<<temp->dwSize;
    temp->bCopied = false;
    temp->lastUsedFrame = status.gDlistCount;
    temp->lastSetAtUcode = status.gUcodeCount;
}


/************************************************************************/
/* Mark the ciinfo entry that the ciinfo is used by VI origin register  */
/* in another word, this is a real frame buffer, not a fake frame buffer*/
/* Fake frame buffers are never really used by VI origin                */
/************************************************************************/
void FrameBufferManager::SetAddrBeDisplayed(uint32_t addr)
{
    uint32_t viwidth = *gfx_info.VI_WIDTH_REG;
    addr &= (g_dwRamSize-1);

    for (int i=0; i<numOfRecentCIInfos; i++)
    {
        if (g_uRecentCIInfoPtrs[i]->dwAddr+2*viwidth == addr)
        {
            g_uRecentCIInfoPtrs[i]->bUsedByVIAtFrame = status.gDlistCount;
        }
        else if (addr >= g_uRecentCIInfoPtrs[i]->dwAddr && addr < g_uRecentCIInfoPtrs[i]->dwAddr+0x1000)
        {
            g_uRecentCIInfoPtrs[i]->bUsedByVIAtFrame = status.gDlistCount;
        }
    }

    for (int i=0; i<numOfRecentCIInfos; i++)
    {
        if (g_RecentVIOriginInfo[i].addr == addr)
        {
            g_RecentVIOriginInfo[i].FrameCount = status.gDlistCount;
            return;
        }
    }

    for (int i=0; i<numOfRecentCIInfos; i++)
    {
        if (g_RecentVIOriginInfo[i].addr == 0)
        {
            // Never used
            g_RecentVIOriginInfo[i].addr = addr;
            g_RecentVIOriginInfo[i].FrameCount = status.gDlistCount;
            return;
        }
    }

    int index=0;
    uint32_t minFrameCount = 0xffffffff;

    for (int i=0; i<numOfRecentCIInfos; i++)
    {
        if( g_RecentVIOriginInfo[i].FrameCount < minFrameCount )
        {
            index = i;
            minFrameCount = g_RecentVIOriginInfo[i].FrameCount;
        }
    }

    g_RecentVIOriginInfo[index].addr = addr;
    g_RecentVIOriginInfo[index].FrameCount = status.gDlistCount;
}

bool FrameBufferManager::HasAddrBeenDisplayed(uint32_t addr, uint32_t width)
{
    addr &= (g_dwRamSize-1);

    for (int i=0; i<numOfRecentCIInfos; i++)
    {
        if (g_uRecentCIInfoPtrs[i]->dwAddr == 0)
            continue;

        if (g_uRecentCIInfoPtrs[i]->dwAddr == addr)
        {
            if (status.gDlistCount-g_uRecentCIInfoPtrs[i]->bUsedByVIAtFrame < 20)
                //if (g_uRecentCIInfoPtrs[i]->bUsedByVIAtFrame != 0)
            {
                return true;
            }
            else
            {
                TXTRBUF_DUMP(TRACE0("This is a new buffer address, the address is never a displayed buffer"););
                return false;
            }
        }
    }

    for (int i=0; i<numOfRecentCIInfos; i++)
    {
        if (g_RecentVIOriginInfo[i].addr != 0)
        {
            if (g_RecentVIOriginInfo[i].addr > addr && 
                (g_RecentVIOriginInfo[i].addr - addr)%width == 0 &&
                (g_RecentVIOriginInfo[i].addr - addr)/width <= 4)
            {
                if (status.gDlistCount-g_RecentVIOriginInfo[i].FrameCount < 20)
                    //if (g_RecentVIOriginInfo[i].FrameCount != 0)
                {
                    return true;
                }
                else
                {
                    TXTRBUF_DUMP(DebuggerAppendMsg("This is a new buffer address, the address is never a displayed buffer"););
                    return false;
                }
            }
        }
    }

    if (status.gDlistCount > 20)
    {
        return false;
    }
    else
    {
        TXTRBUF_DUMP({DebuggerAppendMsg("This is a new buffer address, the address is never a displayed buffer");});
        return true;
    }
}

int FrameBufferManager::FindRecentCIInfoIndex(uint32_t addr)
{
    for (int i=0; i<numOfRecentCIInfos; i++)
    {
        if (g_uRecentCIInfoPtrs[i]->dwAddr <= addr && addr < g_uRecentCIInfoPtrs[i]->dwAddr+g_uRecentCIInfoPtrs[i]->dwMemSize)
        {
            return i;
        }
    }

    return -1;
}

bool FrameBufferManager::IsDIaRenderTexture()
{
    bool foundSetScissor=false;
    bool foundFillRect=false;
    bool foundSetFillColor=false;
    bool foundSetCImg=false;
    uint32_t newFillColor = 0;

    uint32_t dwPC = __RSP.PC[__RSP.PCi];       // This points to the next instruction

    for (int i=0; i<10; i++)
    {
       uint8_t *rdram_u8 = (uint8_t*)gfx_info.RDRAM;
        uint32_t w0 = *(uint32_t *)(rdram_u8 + dwPC + i*8);
        uint32_t w1 = *(uint32_t *)(rdram_u8 + dwPC + 4 + i*8);

        if ((w0>>24) == G_SETSCISSOR)
        {
            foundSetScissor = true;
            continue;
        }

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
                if (x1 == g_CI.dwWidth)
                {
                    foundFillRect = true;
                    continue;
                }

                if (x1 == (unsigned int)(g_CI.dwWidth-1))
                {
                    foundFillRect = true;
                    continue;
                }
            }
        }   

        if ((w0>>24) == G_TEXRECT)
        {
            break;
        }

        if ((w0>>24) == G_SETCIMG)
        {
            foundSetCImg = true;
            break;
        }
    }

    /*
    bool foundSetScissor=false;
    bool foundFillRect=false;
    bool foundSetFillColor=false;
    bool foundSetCImg=false;
    bool foundTxtRect=false;
    int ucodeLength=10;
    uint32_t newFillColor;
    */

    if (foundFillRect)
    {
        if (foundSetFillColor)
        {
            if (newFillColor != 0xFFFCFFFC)
                return true;    // this is a render_texture
            else
                return false;
        }

        if (gRDP.fillColor != 0x00FFFFF7)
            return true;    // this is a render_texture
        else
            return false;   // this is a normal ZImg
    }
    else if (foundSetFillColor && newFillColor == 0xFFFCFFFC && foundSetCImg)
    {
        return false;
    }
    else
    {
        return true;
    }


    if (!foundSetCImg)
        return true;

    if (foundSetScissor)
        return true;
}

int FrameBufferManager::CheckAddrInBackBuffers(uint32_t addr, uint32_t memsize, bool copyToRDRAM)
{
    int r = FindRecentCIInfoIndex(addr);

    if (r >= 0)
    {
        // Also check if the address is overwritten by a recent render_texture
        //int t = CheckAddrInRenderTextures(addr, false);
        int t = -1;
        for (int i=0; i<numOfTxtBufInfos; i++)
        {
            uint32_t bufHeight = gRenderTextureInfos[i].knownHeight ? gRenderTextureInfos[i].N64Height : gRenderTextureInfos[i].maxUsedHeight;
            uint32_t bufMemSize = gRenderTextureInfos[i].CI_Info.dwSize*gRenderTextureInfos[i].N64Width*bufHeight;
            if (addr >=gRenderTextureInfos[i].CI_Info.dwAddr && addr < gRenderTextureInfos[i].CI_Info.dwAddr+bufMemSize)
            {
                if (g_uRecentCIInfoPtrs[r]->lastSetAtUcode < gRenderTextureInfos[i].updateAtUcodeCount)
                {
                    t = i;
                    break;
                }
            }
        }

        if (t >= 0)
            return -1;
    }

    if (r >= 0 && status.gDlistCount - g_uRecentCIInfoPtrs[r]->lastUsedFrame <= 3  && g_uRecentCIInfoPtrs[r]->bCopied == false)
        SaveBackBuffer(r, NULL, true);

    return r;
}


/* Nothing reads CPU frame-buffer writes back any more; the record they
 * used to go into was only consumed by the removed
 * UpdateFrameBufferBeforeUpdateFrame. */
void FrameBufferManager::FrameBufferWriteByCPU(uint32_t addr, uint32_t size)
{
    (void)addr;
    (void)size;
}

void FrameBufferManager::FrameBufferReadByCPU( uint32_t addr )
{
    ///return;  // it does not work very well anyway

    if (!frameBufferOptions.bProcessCPURead)
        return;

    addr &= (g_dwRamSize-1);
    int index = FindRecentCIInfoIndex(addr);
    if (index == -1) 
    {
        // Check if this is the depth buffer
        uint32_t size = 2*g_RecentCIInfo[0].dwWidth*g_RecentCIInfo[0].dwHeight;
        addr &= 0x3FFFFFFF;

        if (addr >= g_ZI.dwAddr && addr < g_ZI.dwAddr + size)
        {
            TXTRBUF_OR_CI_DUMP(TRACE1("Depth Buffer read, reported by emulator, address=%08X", addr));
        }
        else
        {
            return;
        }
    }

    if (status.gDlistCount - g_uRecentCIInfoPtrs[index]->lastUsedFrame > 3)
    {
        // Ok, we don't have this frame any more.
        return;
    }

    //TXTRBUF_OR_CI_DUMP(TRACE1("FB Read By CPU at %08X", addr));
    if (g_uRecentCIInfoPtrs[index]->bCopied)
        return;
        
    //if (addr != g_uRecentCIInfoPtrs[index]->dwAddr)
    //    return;

    TXTRBUF_OR_CI_DUMP(TRACE1("Frame Buffer read, reported by emulator, address=%08X", addr));
    uint32_t size = 0x1000 - addr%0x1000;
    CheckAddrInBackBuffers(addr, size, true);
}



uint32_t FrameBufferManager::ComputeCImgHeight(SetImgInfo &info, uint32_t &height)
{
    uint32_t dwPC = __RSP.PC[__RSP.PCi];       // This points to the next instruction

    for (int i=0; i<10; i++)
    {
       uint8_t *rdram_u8 = (uint8_t*)gfx_info.RDRAM;
        uint32_t w0 = *(uint32_t *)(rdram_u8 + dwPC + i*8);
        uint32_t w1 = *(uint32_t *)(rdram_u8 + dwPC + 4 + i*8);

        if ((w0>>24) == G_SETSCISSOR)
        {
            height   = ((w1>>0 )&0xFFF)/4;
            TXTRBUF_DETAIL_DUMP(TRACE1("buffer height = %d", height));
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
                if (x1 == info.dwWidth)
                {
                    height = y1;
                    TXTRBUF_DETAIL_DUMP(TRACE1("buffer height = %d", height));
                    return G_FILLRECT;
                }

                if (x1 == (unsigned int)(info.dwWidth-1))
                {
                    height = y1+1;
                    TXTRBUF_DETAIL_DUMP(TRACE1("buffer height = %d", height));
                    return G_FILLRECT;
                }
            }
        }   

        if ((w0>>24) == G_SETCIMG)
        {
            goto step2;
        }

        if ((w0>>24) == G_SETCIMG)
        {
            goto step2;
        }
    }

    if (gRDP.scissor.left == 0 && gRDP.scissor.top == 0 && (unsigned int)gRDP.scissor.right == info.dwWidth)
    {
        height = gRDP.scissor.bottom;
        TXTRBUF_DETAIL_DUMP(TRACE1("buffer height = %d", height));
        return G_SETSCISSOR+1;
    }

step2:
    TXTRBUF_DETAIL_DUMP(TRACE0("Not sure about buffer height"));

    height = info.dwWidth*3/4;
    if (status.dwTvSystem == TV_SYSTEM_PAL)
    {
        height = info.dwWidth*9/11;
    }

    if (gRDP.scissor.bottom < (int)height && gRDP.scissor.bottom != 0)
    {
        height = gRDP.scissor.bottom;
    }

    if (info.dwAddr + height*info.dwWidth*info.dwSize >= g_dwRamSize)
    {
        height = info.dwWidth*3/4;
        if (status.dwTvSystem == TV_SYSTEM_PAL)
        {
            height = info.dwWidth*9/11;
        }

        if (gRDP.scissor.bottom < (int)height && gRDP.scissor.bottom != 0)
        {
            height = gRDP.scissor.bottom;
        }

        if (info.dwAddr + height*info.dwWidth*info.dwSize >= g_dwRamSize)
        {
            height = ( g_dwRamSize - info.dwAddr ) / info.dwWidth;
        }
    }

    TXTRBUF_DETAIL_DUMP(TRACE1("render_texture height = %d", height));
    return 0;
}

int FrameBufferManager::CheckRenderTexturesWithNewCI(SetImgInfo &CIinfo, uint32_t height, bool byNewTxtrBuf)
{
    int matchidx = -1;
    uint32_t memsize = ((height*CIinfo.dwWidth)>>1)<<CIinfo.dwSize;

    for (int i=0; i<numOfTxtBufInfos; i++)
    {
        RenderTextureInfo &info = gRenderTextureInfos[i];
        if (!info.isUsed)
            continue;

        bool covered = false;

        if (info.CI_Info.dwAddr == CIinfo.dwAddr)
        {
            if (info.CI_Info.dwSize == CIinfo.dwSize &&
                info.CI_Info.dwWidth == CIinfo.dwWidth &&
                info.CI_Info.dwFormat == CIinfo.dwFormat &&
                info.N64Height == height 
                )
            {
                // This is the same texture at the same address
                if (byNewTxtrBuf)
                {
                    matchidx = i;
                    break;
                }
            }

            // At the same address, but not the same size
            covered = true;
        }

        if (!covered)
        {
            uint32_t memsize2 = ((info.N64Height*info.N64Width)>>1)<<info.CI_Info.dwSize;

            if (info.CI_Info.dwAddr > CIinfo.dwAddr && info.CI_Info.dwAddr < CIinfo.dwAddr + memsize)
                covered = true;
            else if (info.CI_Info.dwAddr+memsize2 > CIinfo.dwAddr && info.CI_Info.dwAddr+memsize2 < CIinfo.dwAddr + memsize)
                covered = true;
            else if (CIinfo.dwAddr > info.CI_Info.dwAddr && CIinfo.dwAddr < info.CI_Info.dwAddr + memsize2)
                covered = true;
            else if (CIinfo.dwAddr+ memsize > info.CI_Info.dwAddr && CIinfo.dwAddr+ memsize < info.CI_Info.dwAddr + memsize2)
                covered = true;
        }

        if (covered)
        {
            if (CRenderTexture_IsBeingRendered(info.pRenderTexture))
            {
                TRACE0("Error, covering a render_texture which is being rendered");
                TRACE3("New address=%08X, width=%d, height=%d", CIinfo.dwAddr, CIinfo.dwWidth, height );
                TRACE3("Old address=%08X, width=%d, height=%d", info.CI_Info.dwAddr, info.N64Width, info.N64Height );
            }
            info.isUsed = false;
            TXTRBUF_DUMP(TRACE5("Delete texture buffer %d at %08X, covered by new CI at %08X, Width=%d, Height=%d", 
                i, info.CI_Info.dwAddr, CIinfo.dwAddr, CIinfo.dwWidth, height ));

            if (info.pRenderTexture)
               CRenderTexture_Delete(info.pRenderTexture);
            info.pRenderTexture    = NULL;
            info.txtEntry.pTexture = NULL;
            continue;
        }
    }

    return matchidx;
}

extern RecentCIInfo *g_uRecentCIInfoPtrs[5];
RenderTextureInfo newRenderTextureInfo;

int FrameBufferManager::FindASlot(void)
{
    int idx;

    // Find an empty slot
    bool found = false;
    for (int i=0; i<numOfTxtBufInfos; i++)
    {
        if (!gRenderTextureInfos[i].isUsed && gRenderTextureInfos[i].updateAtFrame < status.gDlistCount)
        {
            found = true;
            idx = i;
            break;
        }
    }

    // If cannot find an empty slot, find the oldest slot and reuse the slot
    if (!found)
    {
        uint32_t oldestCount=0xFFFFFFFF;
        uint32_t oldestIdx = 0;
        for (int i=0; i<numOfTxtBufInfos; i++)
        {
            if (gRenderTextureInfos[i].updateAtUcodeCount < oldestCount)
            {
                oldestCount = gRenderTextureInfos[i].updateAtUcodeCount;
                oldestIdx = i;
            }
        }

        idx = oldestIdx;
    }

    if (gRenderTextureInfos[idx].pRenderTexture)
       CRenderTexture_Delete(gRenderTextureInfos[idx].pRenderTexture);
    gRenderTextureInfos[idx].pRenderTexture = NULL;

    return idx;
}


void FrameBufferManager::SetRenderTexture(void)
{
    memcpy(&(newRenderTextureInfo.CI_Info), &g_CI, sizeof(SetImgInfo));

    newRenderTextureInfo.N64Width = newRenderTextureInfo.CI_Info.dwWidth;
    newRenderTextureInfo.knownHeight = ComputeCImgHeight(g_CI, newRenderTextureInfo.N64Height);

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

    newRenderTextureInfo.scaleX = newRenderTextureInfo.bufferWidth / float(newRenderTextureInfo.N64Width);
    newRenderTextureInfo.scaleY = newRenderTextureInfo.bufferHeight / float(newRenderTextureInfo.N64Height);

    status.bFrameBufferIsDrawn = false;
    status.bFrameBufferDrawnByTriangles = false;

    newRenderTextureInfo.updateAtFrame = status.gDlistCount;
    newRenderTextureInfo.updateAtUcodeCount = status.gUcodeCount;

    // Delay activation of the render_texture until the 1st rendering
}

int FrameBufferManager::SetBackBufferAsRenderTexture(SetImgInfo &CIinfo, int ciInfoIdx)
{
    // MUDLORD:
    // OK, here's the drill!
    //
    // We  set the graphics card's back buffer's contents as a render_texure
    // This is done due to how the current framebuffer implementation detects
    // changes to the backbuffer memory pointer and then we do a texture
    // copy. This might be slow since it doesn't use hardware auxiliary buffers

    RenderTextureInfo tempRenderTextureInfo;

    memcpy(&(tempRenderTextureInfo.CI_Info), &CIinfo, sizeof(SetImgInfo));

    tempRenderTextureInfo.N64Width = g_uRecentCIInfoPtrs[ciInfoIdx]->dwLastWidth;
    tempRenderTextureInfo.N64Height = g_uRecentCIInfoPtrs[ciInfoIdx]->dwLastHeight;
    tempRenderTextureInfo.knownHeight = true;
    tempRenderTextureInfo.maxUsedHeight = 0;

    tempRenderTextureInfo.bufferWidth = windowSetting.uDisplayWidth;
    tempRenderTextureInfo.bufferHeight = windowSetting.uDisplayHeight;

    tempRenderTextureInfo.scaleX = tempRenderTextureInfo.bufferWidth / float(tempRenderTextureInfo.N64Width);
    tempRenderTextureInfo.scaleY = tempRenderTextureInfo.bufferHeight / float(tempRenderTextureInfo.N64Height);

    status.bFrameBufferIsDrawn = false;
    status.bFrameBufferDrawnByTriangles = false;

    tempRenderTextureInfo.updateAtFrame = status.gDlistCount;
    tempRenderTextureInfo.updateAtUcodeCount = status.gUcodeCount;

    // Checking against previous render_texture infos
    //uint32_t memsize = ((tempRenderTextureInfo.N64Height*tempRenderTextureInfo.N64Width)>>1)<<tempRenderTextureInfo.CI_Info.dwSize;
    int matchidx = CheckRenderTexturesWithNewCI(CIinfo,tempRenderTextureInfo.N64Height,false);
    int idxToUse = (matchidx >= 0) ? matchidx : FindASlot();

    if (gRenderTextureInfos[idxToUse].pRenderTexture == NULL || matchidx < 0)
    {
        gRenderTextureInfos[idxToUse].pRenderTexture = 
        CRenderTexture_New(tempRenderTextureInfo.bufferWidth, tempRenderTextureInfo.bufferHeight, &gRenderTextureInfos[idxToUse], AS_BACK_BUFFER_SAVE);
    }

    // Need to set all variables for gRenderTextureInfos[idxToUse]
    CRenderTexture *pRenderTexture = gRenderTextureInfos[idxToUse].pRenderTexture;
    memcpy(&gRenderTextureInfos[idxToUse], &tempRenderTextureInfo, sizeof(RenderTextureInfo) );
    gRenderTextureInfos[idxToUse].pRenderTexture = pRenderTexture;
    gRenderTextureInfos[idxToUse].isUsed = true;
    gRenderTextureInfos[idxToUse].txtEntry.pTexture = pRenderTexture->m_pTexture;
    gRenderTextureInfos[idxToUse].txtEntry.txtrBufIdx = idxToUse+1;

    return idxToUse;
}

void FrameBufferManager::CloseRenderTexture(bool toSave)
{
    if (m_curRenderTextureIndex < 0)
        return;

    status.bHandleN64RenderTexture = false;
    if (status.bDirectWriteIntoRDRAM)
    {
        // TODO: Implement
    }
    else 
    {
        RestoreNormalBackBuffer();
        if (!toSave || !status.bFrameBufferIsDrawn || !status.bFrameBufferDrawnByTriangles)
        {
            TXTRBUF_DUMP(TRACE0("Closing render_texture without save"););
            if (gRenderTextureInfos[m_curRenderTextureIndex].pRenderTexture)
               CRenderTexture_Delete(gRenderTextureInfos[m_curRenderTextureIndex].pRenderTexture);
            gRenderTextureInfos[m_curRenderTextureIndex].pRenderTexture = NULL;
            gRenderTextureInfos[m_curRenderTextureIndex].isUsed         = false;
            TXTRBUF_DUMP(TRACE1("Delete render_texture %d",m_curRenderTextureIndex););
        }
        else
        {
            TXTRBUF_DUMP(TRACE1("Closing render_texture %d", m_curRenderTextureIndex););
            StoreRenderTextureToRDRAM(-1);

            if (frameBufferOptions.bRenderTextureWriteBack)
            {
               if (gRenderTextureInfos[m_curRenderTextureIndex].pRenderTexture)
                  CRenderTexture_Delete(gRenderTextureInfos[m_curRenderTextureIndex].pRenderTexture);
               gRenderTextureInfos[m_curRenderTextureIndex].pRenderTexture = NULL;
               gRenderTextureInfos[m_curRenderTextureIndex].isUsed = false;
               TXTRBUF_DUMP(TRACE1("Delete render_texture %d after writing back to RDRAM",m_curRenderTextureIndex););
            }
            else
            {
                g_pRenderTextureInfo->crcInRDRAM = ComputeRenderTextureCRCInRDRAM(m_curRenderTextureIndex);
                g_pRenderTextureInfo->crcCheckedAtFrame = status.gDlistCount;
            }
        }
    }

    SetScreenMult(windowSetting.uDisplayWidth/windowSetting.fViWidth, windowSetting.uDisplayHeight/windowSetting.fViHeight);
    CRender::g_pRender->UpdateClipRectangle();
    CRender::g_pRender->ApplyScissorWithClipRatio(false);
}

void FrameBufferManager::ClearN64FrameBufferToBlack(uint32_t left, uint32_t top, uint32_t width, uint32_t height)
{
   uint8_t *rdram_u8 = (uint8_t*)gfx_info.RDRAM;
    RecentCIInfo &p = *(g_uRecentCIInfoPtrs[0]);
    uint16_t *frameBufferBase = (uint16_t*)(rdram_u8 + p.dwAddr);
    uint32_t pitch = p.dwWidth;

    if (width == 0 || height == 0)
    {
        uint32_t len = p.dwHeight*p.dwWidth*p.dwSize;

        if (p.dwSize == G_IM_SIZ_4b)
            len = (p.dwHeight*p.dwWidth)>>1;

        memset(frameBufferBase, 0, len);
    }
    else
    {
        for (uint32_t y=0; y<height; y++)
        {
            for (uint32_t x=0; x<width; x++)
            {
                *(frameBufferBase+(y+top)*pitch+x+left) = 0;
            }
        }
    }
}



// Copies backbuffer to N64 framebuffer by notification by emu core
// **buggy**
void FrameBufferManager::CopyBackToFrameBufferIfReadByCPU(uint32_t addr)
{
    int i = FindRecentCIInfoIndex(addr);
    if (i != -1)
    {
        //if (i == 0) CGraphicsContext::Get()->UpdateFrame(false);
        RecentCIInfo *info = g_uRecentCIInfoPtrs[i];

        StoreBackBufferToRDRAM(info->dwAddr, info->dwFormat, info->dwSize, info->dwWidth, info->dwHeight, 
            windowSetting.uDisplayWidth, windowSetting.uDisplayHeight, addr, 0x1000-addr%0x1000, 0, SURFFMT_A8R8G8B8);

        TRACE1("Copy back for CI Address = %08X", info->dwAddr);
    }
}

// We do these checks to see if a render_texture operation is occurring...
void FrameBufferManager::CheckRenderTextureCRCInRDRAM(void)
{
    for (int i=0; i<numOfTxtBufInfos; i++)
    {
        if (!gRenderTextureInfos[i].isUsed)
            continue;

        if (CRenderTexture_IsBeingRendered(gRenderTextureInfos[i].pRenderTexture))
            continue;

        if (gRenderTextureInfos[i].crcCheckedAtFrame < status.gDlistCount)
        {
            uint32_t crc = ComputeRenderTextureCRCInRDRAM(i);
            if (gRenderTextureInfos[i].crcInRDRAM != crc)
            {
                // RDRAM has been modified by CPU core
                TXTRBUF_DUMP(TRACE2("Delete texture buffer %d at %08X, CRC in RDRAM changed", i, gRenderTextureInfos[i].CI_Info.dwAddr ));

                if (gRenderTextureInfos[i].pRenderTexture)
                   CRenderTexture_Delete(gRenderTextureInfos[i].pRenderTexture);
                gRenderTextureInfos[i].pRenderTexture = NULL;
                gRenderTextureInfos[i].isUsed         = false;
                continue;
            }
            else
            {
                gRenderTextureInfos[i].crcCheckedAtFrame = status.gDlistCount;
            }
        }
    }
}

// Check render_texture memory addresses
int FrameBufferManager::CheckAddrInRenderTextures(uint32_t addr, bool checkcrc)
{
    for (int i=0; i<numOfTxtBufInfos; i++)
    {
        if (!gRenderTextureInfos[i].isUsed)
            continue;

        if (CRenderTexture_IsBeingRendered(gRenderTextureInfos[i].pRenderTexture))
            continue;

        uint32_t bufHeight = gRenderTextureInfos[i].knownHeight ? gRenderTextureInfos[i].N64Height : gRenderTextureInfos[i].maxUsedHeight;
        uint32_t bufMemSize = gRenderTextureInfos[i].CI_Info.dwSize*gRenderTextureInfos[i].N64Width*bufHeight;
        if (addr >=gRenderTextureInfos[i].CI_Info.dwAddr && addr < gRenderTextureInfos[i].CI_Info.dwAddr+bufMemSize)
        {
            if (checkcrc)
            {
                // Check the CRC in RDRAM
                if( gRenderTextureInfos[i].crcCheckedAtFrame < status.gDlistCount )
                {
                    uint32_t crc = ComputeRenderTextureCRCInRDRAM(i);
                    if (gRenderTextureInfos[i].crcInRDRAM != crc)
                    {
                        // RDRAM has been modified by CPU core
                        TRACE3("Buffer %d CRC in RDRAM changed from %08X to %08X", i, gRenderTextureInfos[i].crcInRDRAM, crc );
                        TXTRBUF_DUMP(TRACE2("Delete texture buffer %d at %08X, crcInRDRAM failed.", i, gRenderTextureInfos[i].CI_Info.dwAddr ));

                        if (gRenderTextureInfos[i].pRenderTexture)
                           CRenderTexture_Delete(gRenderTextureInfos[i].pRenderTexture);
                        gRenderTextureInfos[i].pRenderTexture = NULL;
                        gRenderTextureInfos[i].isUsed         = false;
                        continue;
                    }
                    else
                    {
                        gRenderTextureInfos[i].crcCheckedAtFrame = status.gDlistCount;
                    }
                }
            }

            TXTRBUF_DUMP(TRACE2("Loading texture address = %08X from texture buffer %d", addr, i));
            return i;
        }
    }

    return -1;
}

// Load texture from render_texture buffer
void FrameBufferManager::LoadTextureFromRenderTexture(TxtrCacheEntry* pEntry, int infoIdx)
{
    if (infoIdx < 0 || infoIdx >= numOfTxtBufInfos)
    {
        infoIdx = CheckAddrInRenderTextures(pEntry->ti.Address, true);
    }

    if (infoIdx >= 0 && gRenderTextureInfos[infoIdx].isUsed && gRenderTextureInfos[infoIdx].pRenderTexture)
    {
        TXTRBUF_DUMP(TRACE1("Loading from render_texture %d", infoIdx));
        CRenderTexture_LoadTexture(gRenderTextureInfos[infoIdx].pRenderTexture, pEntry);
    }
}

void FrameBufferManager::RestoreNormalBackBuffer()
{
    if (m_curRenderTextureIndex >= 0 && m_curRenderTextureIndex < numOfTxtBufInfos)
    {
        if( gRenderTextureInfos[m_curRenderTextureIndex].pRenderTexture )
            CRenderTexture_SetAsRenderTarget(gRenderTextureInfos[m_curRenderTextureIndex].pRenderTexture, false);
        m_isRenderingToTexture = false;
        m_lastTextureBufferIndex = m_curRenderTextureIndex;
    }

    if (!status.bFrameBufferIsDrawn || !status.bFrameBufferDrawnByTriangles)
    {
        gRenderTextureInfos[m_curRenderTextureIndex].isUsed = false;
        TXTRBUF_DUMP(TRACE2("Delete texture buffer %d at %08X, it is never rendered", m_curRenderTextureIndex, gRenderTextureInfos[m_curRenderTextureIndex].CI_Info.dwAddr ));

        if (gRenderTextureInfos[m_curRenderTextureIndex].pRenderTexture)
           CRenderTexture_Delete(gRenderTextureInfos[m_curRenderTextureIndex].pRenderTexture);
        gRenderTextureInfos[m_curRenderTextureIndex].pRenderTexture = NULL;
    }
}

uint32_t FrameBufferManager::ComputeRenderTextureCRCInRDRAM(int infoIdx)
{
    if (infoIdx >= numOfTxtBufInfos || infoIdx < 0 || !gRenderTextureInfos[infoIdx].isUsed)
        return 0;

    uint8_t *rdram_u8 = (uint8_t*)gfx_info.RDRAM;
    RenderTextureInfo &info = gRenderTextureInfos[infoIdx];
    uint32_t height = info.knownHeight ? info.N64Height : info.maxUsedHeight;
    uint8_t *pAddr = (uint8_t*)(rdram_u8 + info.CI_Info.dwAddr);
    uint32_t pitch = (info.N64Width << info.CI_Info.dwSize ) >> 1;

    return CalculateRDRAMCRC(pAddr, 0, 0, info.N64Width, height, info.CI_Info.dwSize, pitch);
}

// Activates texture buffer for drawing
void FrameBufferManager::ActiveTextureBuffer(void)
{
    status.bCIBufferIsRendered = true;

    if (status.bHandleN64RenderTexture)
    {
        // Checking against previous render_texture infos
        int matchidx = -1;

        //uint32_t memsize = ((newRenderTextureInfo.N64Height*newRenderTextureInfo.N64Width)>>1)<<newRenderTextureInfo.CI_Info.dwSize;

        matchidx = CheckRenderTexturesWithNewCI(g_CI, newRenderTextureInfo.N64Height, true);

        int idxToUse=-1;
        if (matchidx >= 0)
        {
            // Reuse the matched slot
            idxToUse = matchidx;
        }
        else
        {
            idxToUse = FindASlot();
        }

        if (gRenderTextureInfos[idxToUse].pRenderTexture == NULL || matchidx < 0)
        {
            int w = newRenderTextureInfo.bufferWidth;
            if (newRenderTextureInfo.knownHeight == G_SETSCISSOR && newRenderTextureInfo.CI_Info.dwAddr == g_ZI.dwAddr)
            {
                w = gRDP.scissor.right;
            }

            gRenderTextureInfos[idxToUse].pRenderTexture = 
                CRenderTexture_New(w, newRenderTextureInfo.bufferHeight, &gRenderTextureInfos[idxToUse], AS_RENDER_TARGET);
        }

        // Need to set all variables for gRenderTextureInfos[idxToUse]
        CRenderTexture *pRenderTexture = gRenderTextureInfos[idxToUse].pRenderTexture;
        memcpy(&gRenderTextureInfos[idxToUse], &newRenderTextureInfo, sizeof(RenderTextureInfo) );
        gRenderTextureInfos[idxToUse].pRenderTexture = pRenderTexture;
        gRenderTextureInfos[idxToUse].isUsed = true;
        gRenderTextureInfos[idxToUse].txtEntry.pTexture = pRenderTexture->m_pTexture;
        gRenderTextureInfos[idxToUse].txtEntry.txtrBufIdx = idxToUse+1;

        g_pRenderTextureInfo = &gRenderTextureInfos[idxToUse];

        // Active the render_texture
        if (m_curRenderTextureIndex >= 0 && gRenderTextureInfos[m_curRenderTextureIndex].isUsed && gRenderTextureInfos[m_curRenderTextureIndex].pRenderTexture)
        {
            CRenderTexture_SetAsRenderTarget(gRenderTextureInfos[m_curRenderTextureIndex].pRenderTexture, false);
            m_isRenderingToTexture = false;
        }

        if (CRenderTexture_SetAsRenderTarget(gRenderTextureInfos[idxToUse].pRenderTexture, true))
        {
            m_isRenderingToTexture = true;

            //Clear(CLEAR_COLOR_AND_DEPTH_BUFFER, 0x80808080, 1.0f);
            if (frameBufferOptions.bFillRectNextTextureBuffer)
            {
                CGraphicsContext::g_pGraphicsContext->Clear(CLEAR_COLOR_BUFFER, gRDP.fillColor, 1.0f);
            }
            else if( options.enableHackForGames == HACK_FOR_MARIO_TENNIS && g_pRenderTextureInfo->N64Width > 64 && g_pRenderTextureInfo->N64Width < 300 )
            {
                CGraphicsContext::g_pGraphicsContext->Clear(CLEAR_COLOR_BUFFER, 0, 1.0f);
            }
            else if( options.enableHackForGames == HACK_FOR_MARIO_TENNIS && g_pRenderTextureInfo->N64Width < 64 && g_pRenderTextureInfo->N64Width > 32 )
            {
                CGraphicsContext::g_pGraphicsContext->Clear(CLEAR_COLOR_BUFFER, 0, 1.0f);
            }

            m_curRenderTextureIndex = idxToUse;

            status.bDirectWriteIntoRDRAM = false;

            //SetScreenMult(1, 1);
            SetScreenMult(gRenderTextureInfos[m_curRenderTextureIndex].scaleX, gRenderTextureInfos[m_curRenderTextureIndex].scaleY);
            CRender::g_pRender->UpdateClipRectangle();

            // If needed, draw RDRAM into the render_texture
            //if (frameBufferOptions.bLoadRDRAMIntoRenderTexture)
            //{
            //    CRender::GetRender()->LoadTxtrBufFromRDRAM();
            //}
        }
        else
        {
            if (CDeviceBuilder::m_deviceGeneralType == DIRECTX_DEVICE)
            {
                TRACE1("Error to set Render Target: %d", idxToUse);
                TRACE1("Address = %08X", gRenderTextureInfos[idxToUse].CI_Info.dwAddr);
                TRACE2("Width = %d, Height=%d", gRenderTextureInfos[idxToUse].N64Width, gRenderTextureInfos[idxToUse].N64Height);
            }
        }   
    }
    else
    {
        UpdateRecentCIAddr(g_CI);
        CheckRenderTexturesWithNewCI(g_CI, gRDP.scissor.bottom, false);
    }
}

#define SAVE_CI {g_CI.dwAddr = newCI.dwAddr;g_CI.dwFormat = newCI.dwFormat;g_CI.dwSize = newCI.dwSize;g_CI.dwWidth = newCI.dwWidth;g_CI.bpl=newCI.bpl;}

// Sets CI address for framebuffer copies
void FrameBufferManager::Set_CI_addr(SetImgInfo &newCI)
{
    bool wasDrawingTextureBuffer = status.bN64IsDrawingTextureBuffer;
    status.bN64IsDrawingTextureBuffer = ( newCI.dwSize != G_IM_SIZ_16b || newCI.dwFormat != G_IM_FMT_RGBA || newCI.dwWidth < 200 || ( newCI.dwAddr != g_ZI.dwAddr && newCI.dwWidth != 512 && !g_pFrameBufferManager->HasAddrBeenDisplayed(newCI.dwAddr, newCI.dwWidth)) );
    status.bN64FrameBufferIsUsed = status.bN64IsDrawingTextureBuffer;

    if (!wasDrawingTextureBuffer && g_CI.dwAddr == g_ZI.dwAddr && status.bCIBufferIsRendered)
    {
        TXTRBUF_DUMP(TRACE0("ZI is rendered"));

        if (options.enableHackForGames != HACK_FOR_CONKER && g_uRecentCIInfoPtrs[0]->bCopied == false)
        {
            // Conker is not actually using a backbuffer
            g_pFrameBufferManager->UpdateRecentCIAddr(g_CI);
            if (status.leftRendered != -1 && status.topRendered != -1 && status.rightRendered != -1 && status.bottomRendered != -1)
            {
                M64P_RECT rect={status.leftRendered,status.topRendered,status.rightRendered,status.bottomRendered};
                g_pFrameBufferManager->SaveBackBuffer(0, &rect, false);
            }
            else
            {
                g_pFrameBufferManager->SaveBackBuffer(0, NULL, false);
            }
        }
    }

    frameBufferOptions.bFillRectNextTextureBuffer = false;
    if (g_CI.dwAddr == newCI.dwAddr && status.bHandleN64RenderTexture && (g_CI.dwFormat != newCI.dwFormat || g_CI.dwSize != newCI.dwSize || g_CI.dwWidth != newCI.dwWidth))
    {
        // Mario Tennis player shadow
        g_pFrameBufferManager->CloseRenderTexture(true);
        if (options.enableHackForGames == HACK_FOR_MARIO_TENNIS)
            frameBufferOptions.bFillRectNextTextureBuffer = true;   // Hack for Mario Tennis
    }

    SAVE_CI;

    if (g_CI.dwAddr == g_ZI.dwAddr && !status.bN64IsDrawingTextureBuffer)
    {
        if (g_pFrameBufferManager->IsDIaRenderTexture())
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
        {
            status.curRenderBuffer = g_CI.dwAddr;
        }
        else if (status.curRenderBuffer != g_CI.dwAddr)
        {
            status.curDisplayBuffer = status.curRenderBuffer;
            CGraphicsContext::Get()->UpdateFrame(false);
            status.curRenderBuffer = g_CI.dwAddr;
        }
    }

    if (frameBufferOptions.bAtEachFrameUpdate && !status.bHandleN64RenderTexture)
    {
        if (status.curRenderBuffer != g_CI.dwAddr)
        {
            if (status.gDlistCount%(currentRomOptions.N64FrameBufferWriteBackControl+1) == 0)
            {
                g_pFrameBufferManager->StoreBackBufferToRDRAM(status.curRenderBuffer, 
                    newCI.dwFormat, newCI.dwSize, windowSetting.uViWidth, windowSetting.uViHeight,
                    windowSetting.uDisplayWidth, windowSetting.uDisplayHeight, 0xFFFFFFFF, 0xFFFFFFFF, 0, SURFFMT_A8R8G8B8);
            }
        }

        //status.curDisplayBuffer = status.curRenderBuffer;
        status.curRenderBuffer = g_CI.dwAddr;
    }


    switch (currentRomOptions.N64RenderToTextureEmuType)
    {
    case TXT_BUF_NONE:
        if (status.bHandleN64RenderTexture)
            g_pFrameBufferManager->CloseRenderTexture(false);
        status.bHandleN64RenderTexture = false; // Don't handle N64 render_texture stuffs
        if (!status.bN64IsDrawingTextureBuffer)
            g_pFrameBufferManager->UpdateRecentCIAddr(g_CI);
        break;
    default:
        if (status.bHandleN64RenderTexture)
        {
            g_pFrameBufferManager->CloseRenderTexture(true);
        }

        status.bHandleN64RenderTexture = status.bN64IsDrawingTextureBuffer;
        if (status.bHandleN64RenderTexture)
        {
            if (options.enableHackForGames != HACK_FOR_BANJO_TOOIE)
            {
                g_pFrameBufferManager->SetRenderTexture();
            }
        }
        else
        {
            //g_pFrameBufferManager->UpdateRecentCIAddr(g_CI);      // Delay this until the CI buffer is actally drawn
        }
        break;
    }
}


void FrameBufferManager::StoreRenderTextureToRDRAM(int infoIdx)
{
    if (!frameBufferOptions.bRenderTextureWriteBack)
        return;

    if (infoIdx < 0)
        infoIdx = m_lastTextureBufferIndex;

    if (!gRenderTextureInfos[infoIdx].pRenderTexture)
        return;

    if (CRenderTexture_IsBeingRendered(gRenderTextureInfos[infoIdx].pRenderTexture))
    {
        TXTRBUF_DUMP(TRACE1("Cannot SaveTextureBuffer %d, it is being rendered", infoIdx));
        return;
    }

    CRenderTexture_StoreToRDRAM(gRenderTextureInfos[infoIdx].pRenderTexture, infoIdx);
}


//does FB copy to N64 RDAM structure



// Saves backbuffer
// this is the core to the current framebuffer code
// We need to save backbuffer when changed by framebuffer
// so that we can use it for framebuffer effects
void FrameBufferManager::SaveBackBuffer(int ciInfoIdx, M64P_RECT* pSrcRect, bool forceToSaveToRDRAM)
{
    RecentCIInfo &ciInfo = *g_uRecentCIInfoPtrs[ciInfoIdx];

    if (ciInfoIdx == 1)    // to save the current front buffer
    {
        CGraphicsContext::g_pGraphicsContext->UpdateFrame(true);
    }

    if (frameBufferOptions.bWriteBackBufToRDRAM || forceToSaveToRDRAM)
    {
        uint32_t width = ciInfo.dwWidth;
        uint32_t height = ciInfo.dwHeight;

        if (ciInfo.dwWidth == *gfx_info.VI_WIDTH_REG && ciInfo.dwWidth != windowSetting.uViWidth)
        {
            width = windowSetting.uViWidth;
            height = windowSetting.uViHeight;
        }

        StoreBackBufferToRDRAM( ciInfo.dwAddr, ciInfo.dwFormat, ciInfo.dwSize, width, height, 
            windowSetting.uDisplayWidth, windowSetting.uDisplayHeight, 0xFFFFFFFF, 0xFFFFFFFF, 0, SURFFMT_A8R8G8B8);

        g_uRecentCIInfoPtrs[ciInfoIdx]->bCopied = true;
        if (ciInfoIdx == 1)    // to save the current front buffer
        {
            CGraphicsContext::g_pGraphicsContext->UpdateFrame(true);
        }
        return;
    }


    SetImgInfo tempinfo;
    tempinfo.dwAddr = ciInfo.dwAddr;
    tempinfo.dwFormat = ciInfo.dwFormat;
    tempinfo.dwSize = ciInfo.dwSize;
    tempinfo.dwWidth = ciInfo.dwWidth;

    int idx = SetBackBufferAsRenderTexture(tempinfo, ciInfoIdx);

    CopyBackBufferToRenderTexture(idx, ciInfo, pSrcRect);

    gRenderTextureInfos[idx].crcCheckedAtFrame = status.gDlistCount;
    gRenderTextureInfos[idx].crcInRDRAM = ComputeRenderTextureCRCInRDRAM(idx);

    g_uRecentCIInfoPtrs[ciInfoIdx]->bCopied = true;
}

/* ---- what the texture cache (TextureManager.c) asks ---- */

extern "C" int RiceFB_CheckAddrInRenderTextures(uint32_t addr)
{
    return g_pFrameBufferManager->CheckAddrInRenderTextures(addr, true);
}

extern "C" int RiceFB_CheckAddrInBackBuffers(uint32_t addr, uint32_t memsize)
{
    return g_pFrameBufferManager->CheckAddrInBackBuffers(addr, memsize, false);
}

extern "C" TxtrCacheEntry *RiceFB_RenderTextureEntry(int idx, const TxtrInfo *ti)
{
    RenderTextureInfo &info = gRenderTextureInfos[idx];
    if (!info.pRenderTexture || info.CI_Info.dwFormat != ti->Format || info.CI_Info.dwSize != ti->Size)
        return NULL;
    info.txtEntry.ti = *ti;
    return &info.txtEntry;
}

extern "C" uint32_t RiceFB_RenderTextureCRC(int idx)       { return gRenderTextureInfos[idx].crcInRDRAM; }
extern "C" uint32_t RiceFB_RenderTextureUpdatedAt(int idx) { return gRenderTextureInfos[idx].updateAtFrame; }
extern "C" uint32_t RiceFB_CIFormat(void)                  { return g_pRenderTextureInfo->CI_Info.dwFormat; }

extern "C" void RiceFB_LoadTextureFromRenderTexture(TxtrCacheEntry *pEntry, int idx)
{
    g_pFrameBufferManager->LoadTextureFromRenderTexture(pEntry, idx);
}

extern "C" void RiceFB_CloseRenderTexture(bool toSave)
{
    g_pFrameBufferManager->CloseRenderTexture(toSave);
}

extern "C" void RiceFB_ActiveTextureBuffer(void)
{
   g_pFrameBufferManager->ActiveTextureBuffer();
}
