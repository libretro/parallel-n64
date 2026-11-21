/*
Copyright (C) 2002 Rice1964

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

#ifndef _FRAME_BUFFER_H_
#define _FRAME_BUFFER_H_

#include "typedefs.h"
#include "RenderTexture.h"
#include "TextureManager.h"

typedef int SURFFORMAT;

#ifdef __cplusplus
extern "C" {
#endif

/* FrameBufferRDRAM.c's: texels copied into the N64's colour image in
   RDRAM, 8 bits from the tile's load, 16 bits from the tile's texture */
void TexRectToN64FrameBuffer_16b(uint32_t x0, uint32_t y0, uint32_t width, uint32_t height, uint32_t dwTile);
void TexRectToFrameBuffer_8b(uint32_t dwXL, uint32_t dwYL, uint32_t dwXH, uint32_t dwYH, float t0u0, float t0v0, float t0u1, float t0v1, uint32_t dwTile);
/* an A8R8G8B8 pixel as RGBA 5551 */
uint16_t ConvertRGBATo555(uint32_t color32);

extern RenderTextureInfo gRenderTextureInfos[];
extern RenderTextureInfo *g_pRenderTextureInfo;

/* FrameBuffer.cpp's: the render texture drawn into made active */
void RiceFB_ActiveTextureBuffer(void);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
class FrameBufferManager
{
    friend class CGraphicsContext;
    friend class CDXGraphicsContext;
public:
    FrameBufferManager();
    virtual ~FrameBufferManager();

    void Initialize();
    void CloseUp();
    void Set_CI_addr(SetImgInfo &newCI);
    void UpdateRecentCIAddr(SetImgInfo &ciinfo);
    void SetAddrBeDisplayed(uint32_t addr);
    bool HasAddrBeenDisplayed(uint32_t addr, uint32_t width);
    int FindRecentCIInfoIndex(uint32_t addr);
    bool IsDIaRenderTexture();

    int         CheckAddrInRenderTextures(uint32_t addr, bool checkcrc);
    uint32_t      ComputeRenderTextureCRCInRDRAM(int infoIdx);
    void        CheckRenderTextureCRCInRDRAM(void);
    int         CheckRenderTexturesWithNewCI(SetImgInfo &CIinfo, uint32_t height, bool byNewTxtrBuf);
    virtual void ClearN64FrameBufferToBlack(uint32_t left, uint32_t top, uint32_t width, uint32_t height);
    virtual int SetBackBufferAsRenderTexture(SetImgInfo &CIinfo, int ciInfoIdx);
    void        LoadTextureFromRenderTexture(TxtrCacheEntry* pEntry, int infoIdx);
    void        UpdateFrameBufferBeforeUpdateFrame();
    virtual void RestoreNormalBackBuffer();                 // restore the normal back buffer
    virtual void CopyBackToFrameBufferIfReadByCPU(uint32_t addr);
    virtual void SetRenderTexture(void);
    virtual void CloseRenderTexture(bool toSave);
    virtual void ActiveTextureBuffer(void);

    int IsAddrInRecentFrameBuffers(uint32_t addr);
    int CheckAddrInBackBuffers(uint32_t addr, uint32_t memsize, bool copyToRDRAM);

    uint32_t ComputeCImgHeight(SetImgInfo &info, uint32_t &height);

    int FindASlot(void);

    bool ProcessFrameWriteRecord();
    void FrameBufferWriteByCPU(uint32_t addr, uint32_t size);
    void FrameBufferReadByCPU( uint32_t addr );
    bool FrameBufferInRDRAMCheckCRC();
    void StoreRenderTextureToRDRAM(int infoIdx);

    virtual bool IsRenderingToTexture() {return m_isRenderingToTexture;}

    // Device dependent functions
    virtual void SaveBackBuffer(int ciInfoIdx, M64P_RECT* pRect, bool forceToSaveToRDRAM);         // Copy the current back buffer to temp buffer
    virtual void CopyBackBufferToRenderTexture(int idx, RecentCIInfo &ciInfo, M64P_RECT* pRect) {} // Copy the current back buffer to temp buffer
    virtual void StoreBackBufferToRDRAM(uint32_t addr, uint32_t fmt, uint32_t siz, uint32_t width, 
        uint32_t height, uint32_t bufWidth, uint32_t bufHeight, uint32_t startaddr, 
        uint32_t memsize, uint32_t pitch, SURFFORMAT surf_fmt) {}
#ifdef DEBUGGER
    virtual void DisplayRenderTexture(int infoIdx);
#endif

protected:
    bool    m_isRenderingToTexture;
    int     m_curRenderTextureIndex;
    int     m_lastTextureBufferIndex;
};

extern RenderTextureInfo newRenderTextureInfo;

#define NEW_TEXTURE_BUFFER

extern RenderTextureInfo g_ZI_saves[2];


extern FrameBufferManager* g_pFrameBufferManager;

extern RecentCIInfo g_RecentCIInfo[];
extern RecentViOriginInfo g_RecentVIOriginInfo[];
extern int numOfTxtBufInfos;
extern RecentCIInfo *g_uRecentCIInfoPtrs[5];

#endif /* __cplusplus */

#endif

