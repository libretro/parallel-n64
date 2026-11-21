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

/* Rice's texture cache. C89. */
#include <stdlib.h>
#include <string.h>

#include <retro_miscellaneous.h>

#include "Config.h"
#include "ConvertImage.h"
#include "CombinerDefs.h"
#include "RenderBase.h"
#include "TextureManager.h"

#define S_FLAG  0
#define T_FLAG  1

/* the hash table's buckets: the first prime from 800 */
#define NUM_CACHED_TXTR_LISTS 809

static TxtrCacheEntry *m_pCacheTxtrList[NUM_CACHED_TXTR_LISTS];

static TxtrCacheEntry m_blackTextureEntry;
static TxtrCacheEntry m_PrimColorTextureEntry;
static TxtrCacheEntry m_EnvColorTextureEntry;
static TxtrCacheEntry m_LODFracTextureEntry;
static TxtrCacheEntry m_PrimLODFracTextureEntry;

/* ---- the entries ---- */

static TxtrCacheEntry *TxtrCacheEntry_New(void)
{
   /* no texture, render texture 0 */
   return (TxtrCacheEntry *)calloc(1, sizeof(TxtrCacheEntry));
}

static void TxtrCacheEntry_Delete(TxtrCacheEntry *pEntry)
{
   /* from RiceDevice_CreateTexture */
   CTexture_Delete(pEntry->pTexture);
   free(pEntry);
}

static bool TCacheEntryIsLoaded(TxtrCacheEntry *pEntry)
{
   int i;
   for (i = 0; i < MAX_TEXTURES; i++)
   {
      if (g_textures[i].pTextureEntry == pEntry)
         return true;
   }

   return false;
}

/* most textures are on a four byte boundary: the bottom bits are none */
static uint32_t Hash(uint32_t dwValue)
{
   return (dwValue >> 2) % NUM_CACHED_TXTR_LISTS;
}

static void AddTexture(TxtrCacheEntry *pEntry)
{
   uint32_t dwKey = Hash(pEntry->ti.Address);

   /* to the head: a new texture is the likelier to be asked for next */
   pEntry->pNext = m_pCacheTxtrList[dwKey];
   m_pCacheTxtrList[dwKey] = pEntry;
}

static TxtrCacheEntry *GetTxtrCacheEntry(TxtrInfo *pti)
{
   TxtrCacheEntry *pEntry;

   for (pEntry = m_pCacheTxtrList[Hash(pti->Address)]; pEntry; pEntry = pEntry->pNext)
   {
      if (TxtrInfo_Equal(&pEntry->ti, pti))
         return pEntry;
   }

   return NULL;
}

static void RemoveTexture(TxtrCacheEntry *pEntry)
{
   uint32_t dwKey = Hash(pEntry->ti.Address);
   TxtrCacheEntry *pPrev = NULL;
   TxtrCacheEntry *pCurr = m_pCacheTxtrList[dwKey];

   while (pCurr)
   {
      /* the attributes match */
      if (TxtrInfo_Equal(&pCurr->ti, &pEntry->ti))
      {
         if (pPrev != NULL)
            pPrev->pNext = pCurr->pNext;
         else
            m_pCacheTxtrList[dwKey] = pCurr->pNext;

         TxtrCacheEntry_Delete(pEntry);
         break;
      }

      pPrev = pCurr;
      pCurr = pCurr->pNext;
   }
}

/* textures not used in five seconds (at 30 frames a second), and not
   bound, let go */
void TextureManager_PurgeOldTextures(void)
{
   const uint32_t dwFramesToKill = 5 * 30;
   uint32_t i;

   for (i = 0; i < NUM_CACHED_TXTR_LISTS; i++)
   {
      TxtrCacheEntry *pEntry = m_pCacheTxtrList[i];
      while (pEntry)
      {
         TxtrCacheEntry *pNext = pEntry->pNext;

         if (status.gDlistCount - pEntry->FrameLastUsed > dwFramesToKill && !TCacheEntryIsLoaded(pEntry))
            RemoveTexture(pEntry);
         pEntry = pNext;
      }
   }
}

void TextureManager_RecycleAllTextures(void)
{
   uint32_t i;

   for (i = 0; i < NUM_CACHED_TXTR_LISTS; i++)
   {
      while (m_pCacheTxtrList[i])
      {
         TxtrCacheEntry *pTVictim = m_pCacheTxtrList[i];
         m_pCacheTxtrList[i] = pTVictim->pNext;
         TxtrCacheEntry_Delete(pTVictim);
      }
   }
}

/* every cached texture let go, and the colour textures */
bool TextureManager_CleanUp(void)
{
   TextureManager_RecycleAllTextures();

   CTexture_Delete(m_blackTextureEntry.pTexture);
   CTexture_Delete(m_PrimColorTextureEntry.pTexture);
   CTexture_Delete(m_EnvColorTextureEntry.pTexture);
   CTexture_Delete(m_LODFracTextureEntry.pTexture);
   CTexture_Delete(m_PrimLODFracTextureEntry.pTexture);
   memset(&m_blackTextureEntry, 0, sizeof(TxtrCacheEntry));
   memset(&m_PrimColorTextureEntry, 0, sizeof(TxtrCacheEntry));
   memset(&m_EnvColorTextureEntry, 0, sizeof(TxtrCacheEntry));
   memset(&m_LODFracTextureEntry, 0, sizeof(TxtrCacheEntry));
   memset(&m_PrimLODFracTextureEntry, 0, sizeof(TxtrCacheEntry));

   return true;
}

static TxtrCacheEntry *CreateNewCacheEntry(uint32_t dwAddr, uint32_t dwWidth, uint32_t dwHeight)
{
   TxtrCacheEntry *pEntry = TxtrCacheEntry_New();
   if (pEntry == NULL)
      return NULL;

   /* its CRC, palette CRC and highest index are the caller's */
   pEntry->pTexture        = RiceDevice_CreateTexture(dwWidth, dwHeight);
   pEntry->ti.Address      = dwAddr;
   pEntry->dwTimeLastUsed  = status.gRDPTime;
   pEntry->FrameLastUsed   = status.gDlistCount;

   AddTexture(pEntry);
   return pEntry;
}

/* ---- CRCs of texels in RDRAM ---- */

#define FAST_CRC_CHECKING_INC_X 13
#define FAST_CRC_CHECKING_INC_Y 11
#define FAST_CRC_MIN_Y_INC      2
#define FAST_CRC_MIN_X_INC      2
#define FAST_CRC_MAX_X_INC      7
#define FAST_CRC_MAX_Y_INC      3

/* the last CRC worked out: GetTexture reads it after CalculateRDRAMCRC */
static uint32_t dwAsmCRC;

uint32_t CalculateRDRAMCRC(void *pPhysicalAddress, uint32_t left, uint32_t top, uint32_t width, uint32_t height, uint32_t size, uint32_t pitchInBytes )
{
    uint32_t dwAsmdwBytesPerLine;

    dwAsmCRC = 0;
    dwAsmdwBytesPerLine = ((width<<size)+1)/2;

    /* Defensive bound: the loops below walk height rows, and within
     * each row read up to dwAsmdwBytesPerLine bytes (the fast path in
     * dwords, the slow path in bytes). Callers derive width/height from
     * guest data (e.g. a degenerate Sprite2D whose width wraps to a huge
     * unsigned value), so verify the whole walked region stays inside
     * RDRAM and bail with a harmless CRC rather than reading off the end. */
    {
        uint8_t *ramBase = (uint8_t*)gfx_info.RDRAM;
        uint8_t *region  = (uint8_t*)pPhysicalAddress;
        uint64_t span    = height ? ((uint64_t)(height - 1) * pitchInBytes
                                     + dwAsmdwBytesPerLine) : 0;
        if (region < ramBase ||
            (uint64_t)(region - ramBase) + span > g_dwRamSize)
        {
            return 0;
        }
    }

    if (currentRomOptions.bFastTexCRC && !options.bLoadHiResTextures && (height>=32 || (dwAsmdwBytesPerLine>>2)>=16))
    {
        uint32_t realWidthInDWORD = dwAsmdwBytesPerLine>>2;
        uint32_t xinc = realWidthInDWORD / FAST_CRC_CHECKING_INC_X;
        uint32_t yinc;
        uint32_t pitch;
        uint32_t *pStart;
        uint32_t y;
        if (xinc < FAST_CRC_MIN_X_INC)
            xinc = MIN(FAST_CRC_MIN_X_INC, width);
        if (xinc > FAST_CRC_MAX_X_INC)
            xinc = FAST_CRC_MAX_X_INC;

        yinc = height / FAST_CRC_CHECKING_INC_Y;
        if (yinc < FAST_CRC_MIN_Y_INC)
            yinc = MIN(FAST_CRC_MIN_Y_INC, height);
        if (yinc > FAST_CRC_MAX_Y_INC)
            yinc = FAST_CRC_MAX_Y_INC;

        pitch = pitchInBytes>>2;
        pStart = (uint32_t*)(pPhysicalAddress);
        pStart += (top * pitch) + (((left<<size)+1)>>3);

        /* The original assembly code had a bug in it (it incremented pStart by 'pitch' in bytes, not in dwords) */
        /* This C code implements the same algorithm as the ASM but without the bug */
        y = 0;
        while (y < height)
        {
            uint32_t x = 0;
            while (x < realWidthInDWORD)
            {
                dwAsmCRC = (dwAsmCRC << 4) + ((dwAsmCRC >> 28) & 15);
                dwAsmCRC += pStart[x];
                x += xinc;
                dwAsmCRC += x;
            }
            dwAsmCRC ^= y;
            y += yinc;
            pStart += pitch;
        }
    }
    else
    {
       uint8_t *pAsmStart = (uint8_t*)(pPhysicalAddress);
       int y = height - 1;

       pAsmStart += (top * pitchInBytes) + (((left<<size)+1)>>1);

       while (y >= 0)
       {
          uint32_t esi = 0;
          int x = dwAsmdwBytesPerLine - 4;
          while (x >= 0)
          {
             memcpy(&esi, pAsmStart + x, sizeof(esi));   /* the texels need not be aligned */
             esi ^= x;

             dwAsmCRC = (dwAsmCRC << 4) + ((dwAsmCRC >> 28) & 15);
             dwAsmCRC += esi;
             x-=4;
          }
          esi ^= y;
          dwAsmCRC += esi;
          pAsmStart += pitchInBytes;
          y--;
       }

    }
    return dwAsmCRC;
}

unsigned char CalculateMaxCI(void *pPhysicalAddress, uint32_t left, uint32_t top, uint32_t width, uint32_t height, uint32_t size, uint32_t pitchInBytes )
{
    unsigned char *buf;
    unsigned char val = 0;

    if (G_IM_SIZ_8b == size)
    {
        uint32_t y;
        for (y = 0; y<height; y++)
        {
            uint32_t x;
            buf = (unsigned char*)pPhysicalAddress + left + pitchInBytes * (y+top);
            for (x = 0; x<width; x++)
            {
                if (buf[x] > val)
                    val = buf[x];

                if (val == 0xFF)
                    return 0xFF;
            }
        }
    }
    else
    {
        uint32_t y;
        unsigned char val1,val2;
        left >>= 1;
        width >>= 1;
        for (y = 0; y<height; y++)
        {
            uint32_t x;
            buf = (unsigned char*)pPhysicalAddress + left + pitchInBytes * (y+top);
            for (x = 0; x<width; x++)
            {
                val1 = buf[x]>>4;
                val2 = buf[x]&0xF;

                if (val1 > val) val = val1;
                if (val2 > val) val = val2;

                if (val == 0xF)
                    return 0xF;
            }
        }
    }

    return val;
}

/* ---- the texels past those loaded: clamped, mirrored or wrapped ---- */

static void ClampS32(uint32_t *array, uint32_t width, uint32_t towidth, uint32_t arrayWidth, uint32_t rows)
{
   uint32_t y;
   if ((int) width <= 0 || (int) towidth < 0)
      return;

   for( y = 0; y<rows; y++ )
   {
      uint32_t* line = array+y*arrayWidth;
      uint32_t val = line[width-1];
      uint32_t x;
      for( x = width; x<towidth; x++ )
         line[x] = val;
   }
}

static void MirrorS32(uint32_t *array, uint32_t width, uint32_t mask, uint32_t towidth, uint32_t arrayWidth, uint32_t rows)
{
   uint32_t maskval1 = (1<<mask)-1;
   uint32_t maskval2 = (1<<(mask+1))-1;
   uint32_t y;

   for( y = 0; y<rows; y++ )
   {
      uint32_t* line = array+y*arrayWidth;
      uint32_t x;
      for( x = width; x<towidth; x++ )
         line[x] = (x&maskval2)<=maskval1 ? line[x&maskval1] : line[maskval2-(x&maskval2)];
   }
}

static void WrapS32(uint32_t *array, uint32_t width, uint32_t mask, uint32_t towidth, uint32_t arrayWidth, uint32_t rows)
{
   uint32_t maskval = (1<<mask)-1;
   uint32_t y;

   for( y = 0; y<rows; y++ )
   {
      uint32_t* line = array+y*arrayWidth;
      uint32_t x;
      for( x = width; x<towidth; x++ )
         line[x] = line[(x&maskval)<width?(x&maskval):towidth-(x&maskval)];
   }
}

static void ClampS16(uint16_t *array, uint32_t width, uint32_t towidth, uint32_t arrayWidth, uint32_t rows)
{
   uint32_t y;
   if ((int) width <= 0 || (int) towidth < 0)
      return;

   for( y = 0; y<rows; y++ )
   {
      uint16_t* line = array+y*arrayWidth;
      uint16_t val = line[width-1];
      uint32_t x;
      for( x = width; x<towidth; x++ )
         line[x] = val;
   }
}

static void MirrorS16(uint16_t *array, uint32_t width, uint32_t mask, uint32_t towidth, uint32_t arrayWidth, uint32_t rows)
{
   uint32_t maskval1 = (1<<mask)-1;
   uint32_t maskval2 = (1<<(mask+1))-1;
   uint32_t y;

   for( y = 0; y<rows; y++ )
   {
      uint16_t* line = array+y*arrayWidth;
      uint32_t x;
      for( x = width; x<towidth; x++ )
         line[x] = (x&maskval2)<=maskval1 ? line[x&maskval1] : line[maskval2-(x&maskval2)];
   }
}

static void WrapS16(uint16_t *array, uint32_t width, uint32_t mask, uint32_t towidth, uint32_t arrayWidth, uint32_t rows)
{
   uint32_t maskval = (1<<mask)-1;
   uint32_t y;

   for( y = 0; y<rows; y++ )
   {
      uint16_t* line = array+y*arrayWidth;
      uint32_t x;
      for( x = width; x<towidth; x++ )
         line[x] = line[(x&maskval)<width?(x&maskval):towidth-(x&maskval)];
   }
}

static void ClampT32(uint32_t *array, uint32_t height, uint32_t toheight, uint32_t arrayWidth, uint32_t cols)
{
   uint32_t *linesrc;
   uint32_t y;
   if ((int) height <= 0 || (int) toheight < 0)
      return;

   linesrc = array+arrayWidth*(height-1);
   for( y = height; y<toheight; y++ )
   {
      uint32_t* linedst = array+arrayWidth*y;
      memcpy(linedst, linesrc, arrayWidth * sizeof(*linedst));
   }
}

static void MirrorT32(uint32_t *array, uint32_t height, uint32_t mask, uint32_t toheight, uint32_t arrayWidth, uint32_t cols)
{
   uint32_t maskval1 = (1<<mask)-1;
   uint32_t maskval2 = (1<<(mask+1))-1;
   uint32_t y;

   for( y = height; y<toheight; y++ )
   {
      uint32_t srcy = (y&maskval2)<=maskval1 ? y&maskval1 : maskval2-(y&maskval2);
      uint32_t* linesrc = array+arrayWidth*srcy;
      uint32_t* linedst = array+arrayWidth*y;
      memcpy(linedst, linesrc, arrayWidth * sizeof(*linedst));
   }
}

static void WrapT32(uint32_t *array, uint32_t height, uint32_t mask, uint32_t toheight, uint32_t arrayWidth, uint32_t cols)
{
   uint32_t maskval = (1<<mask)-1;
   uint32_t y;
   for( y = height; y<toheight; y++ )
   {
      uint32_t* linesrc = array+arrayWidth*(y>maskval?y&maskval:y-height);
      uint32_t* linedst = array+arrayWidth*y;
      memcpy(linedst, linesrc, arrayWidth * sizeof(*linedst));
   }
}

static void ClampT16(uint16_t *array, uint32_t height, uint32_t toheight, uint32_t arrayWidth, uint32_t cols)
{
   uint16_t *linesrc;
   uint32_t y;
   if ((int) height <= 0 || (int) toheight < 0)
      return;

   linesrc = array+arrayWidth*(height-1);
   for( y = height; y<toheight; y++ )
   {
      uint16_t* linedst = array+arrayWidth*y;
      memcpy(linedst, linesrc, arrayWidth * sizeof(*linedst));
   }
}

static void MirrorT16(uint16_t *array, uint32_t height, uint32_t mask, uint32_t toheight, uint32_t arrayWidth, uint32_t cols)
{
   uint32_t maskval1 = (1<<mask)-1;
   uint32_t maskval2 = (1<<(mask+1))-1;
   uint32_t y;

   for( y = height; y<toheight; y++ )
   {
      uint32_t srcy = (y&maskval2)<=maskval1 ? y&maskval1 : maskval2-(y&maskval2);
      uint16_t* linesrc = array+arrayWidth*srcy;
      uint16_t* linedst = array+arrayWidth*y;
      memcpy(linedst, linesrc, arrayWidth * sizeof(*linedst));
   }
}

static void WrapT16(uint16_t *array, uint32_t height, uint32_t mask, uint32_t toheight, uint32_t arrayWidth, uint32_t cols)
{
   uint32_t maskval = (1<<mask)-1;
   uint32_t y;
   for( y = height; y<toheight; y++ )
   {
      uint16_t* linesrc = array+arrayWidth*(y>maskval?y&maskval:y-height);
      uint16_t* linedst = array+arrayWidth*y;
      memcpy(linedst, linesrc, arrayWidth * sizeof(*linedst));
   }
}

static void Clamp(void *array, uint32_t width, uint32_t towidth, uint32_t arrayWidth, uint32_t rows, int flag, int size )
{
   if( flag == S_FLAG )    /* s */
   {
      if( size == 4 ) /* 32 bit */
         ClampS32((uint32_t*)array, width, towidth, arrayWidth, rows);
      else    /* 16 bits */
         ClampS16((uint16_t*)array, width, towidth, arrayWidth, rows);
   }
   else    /* t */
   {
      if( size == 4 ) /* 32 bit */
         ClampT32((uint32_t*)array, width, towidth, arrayWidth, rows);
      else    /* 16 bits */
         ClampT16((uint16_t*)array, width, towidth, arrayWidth, rows);
   }
}

static void Wrap(void *array, uint32_t width, uint32_t mask, uint32_t towidth, uint32_t arrayWidth, uint32_t rows, int flag, int size )
{
    if( flag == S_FLAG )    /* s */
    {
        if( size == 4 ) /* 32 bit */
            WrapS32((uint32_t*)array, width, mask, towidth, arrayWidth, rows);
        else    /* 16 bits */
            WrapS16((uint16_t*)array, width, mask, towidth, arrayWidth, rows);
    }
    else    /* t */
    {
        if( size == 4 ) /* 32 bit */
            WrapT32((uint32_t*)array, width, mask, towidth, arrayWidth, rows);
        else    /* 16 bits */
            WrapT16((uint16_t*)array, width, mask, towidth, arrayWidth, rows);
    }
}

static void Mirror(void *array, uint32_t width, uint32_t mask, uint32_t towidth, uint32_t arrayWidth, uint32_t rows, int flag, int size )
{
   if( flag == S_FLAG )    /* s */
   {
      if( size == 4 ) /* 32 bit */
         MirrorS32((uint32_t*)array, width, mask, towidth, arrayWidth, rows);
      else    /* 16 bits */
         MirrorS16((uint16_t*)array, width, mask, towidth, arrayWidth, rows);
   }
   else    /* t */
   {
      if( size == 4 ) /* 32 bit */
         MirrorT32((uint32_t*)array, width, mask, towidth, arrayWidth, rows);
      else    /* 16 bits */
         MirrorT16((uint16_t*)array, width, mask, towidth, arrayWidth, rows);
   }
}

/* Image has been loaded @sizeToLoad wide (or high): the rest of it to
   @sizeCreated, mirrored, wrapped or clamped as the tile says */
static void ExpandTexture(TxtrCacheEntry *pEntry, uint32_t sizeToLoad, uint32_t sizeToCreate, uint32_t sizeCreated,
      int arrayWidth, int flag, int mask, int mirror, int clamp, uint32_t otherSize)
{
   uint32_t maskWidth;
   int size;
   DrawInfo di;

   if (sizeToLoad >= sizeCreated)
      return;

   maskWidth = (1 << mask);
   size      = CTexture_GetPixelSize(pEntry->pTexture);

   if (!CTexture_StartUpdate(pEntry->pTexture, &di))
      return;

   if (mask == 0)
   {
      Clamp(di.lpSurface, sizeToLoad, sizeCreated, arrayWidth, otherSize, flag, size);
      CTexture_EndUpdate(pEntry->pTexture);
      return;
   }

   if (sizeToLoad == maskWidth)
   {
      uint32_t tempwidth = clamp ? sizeToCreate : sizeCreated;
      if (mirror)
         Mirror(di.lpSurface, sizeToLoad, mask, tempwidth, arrayWidth, otherSize, flag, size);
      else
         Wrap(di.lpSurface, sizeToLoad, mask, tempwidth, arrayWidth, otherSize, flag, size);

      if (tempwidth < sizeCreated)
         Clamp(di.lpSurface, tempwidth, sizeCreated, arrayWidth, otherSize, flag, size);

      CTexture_EndUpdate(pEntry->pTexture);
      return;
   }

   if (sizeToLoad < sizeToCreate && sizeToCreate == maskWidth && maskWidth == sizeCreated)
   {
      /* widthToLoad < widthToCreate = maskWidth */
      Wrap(di.lpSurface, sizeToLoad, mask, sizeCreated, arrayWidth, otherSize, flag, size);
      CTexture_EndUpdate(pEntry->pTexture);
      return;
   }

   if (sizeToLoad == sizeToCreate && sizeToCreate < maskWidth)
   {
      Clamp(di.lpSurface, sizeToLoad, sizeCreated, arrayWidth, otherSize, flag, size);
      CTexture_EndUpdate(pEntry->pTexture);
      return;
   }

   if (sizeToLoad < sizeToCreate && sizeToCreate < maskWidth)
   {
      Clamp(di.lpSurface, sizeToLoad, sizeCreated, arrayWidth, otherSize, flag, size);
      CTexture_EndUpdate(pEntry->pTexture);
      return;
   }

   CTexture_EndUpdate(pEntry->pTexture);
}

static void ExpandTextureS(TxtrCacheEntry *pEntry)
{
   TxtrInfo *ti = &pEntry->ti;
   uint32_t textureWidth = pEntry->pTexture->m_dwCreatedTextureWidth;
   ExpandTexture(pEntry, ti->WidthToLoad, ti->WidthToCreate, textureWidth,
         textureWidth, S_FLAG, ti->maskS, ti->mirrorS, ti->clampS, ti->HeightToLoad);
}

static void ExpandTextureT(TxtrCacheEntry *pEntry)
{
   TxtrInfo *ti = &pEntry->ti;
   uint32_t textureHeight = pEntry->pTexture->m_dwCreatedTextureHeight;
   uint32_t textureWidth  = pEntry->pTexture->m_dwCreatedTextureWidth;
   ExpandTexture(pEntry, ti->HeightToLoad, ti->HeightToCreate, textureHeight,
         textureWidth, T_FLAG, ti->maskT, ti->mirrorT, ti->clampT, ti->WidthToLoad);
}

/* ---- conversion ---- */

static void ConvertTexture(TxtrCacheEntry *pEntry, bool fromTMEM)
{
   ConvertFunction pF;

   if (options.bUseFullTMEM && fromTMEM && status.bAllowLoadFromTMEM)
      pF = gConvertFunctions_FullTMEM[pEntry->ti.Format][pEntry->ti.Size];
   else
   {
      /* a YUV tile 7: the texture is too */
      uint32_t fmt = gDP.tiles[7].format == G_IM_FMT_YUV ? G_IM_FMT_YUV : pEntry->ti.Format;
      if (gRDP.otherMode.text_tlut >= 2)
         pF = gConvertTlutFunctions[fmt][pEntry->ti.Size];
      else
         pF = gConvertFunctions[fmt][pEntry->ti.Size];
   }

   if (pF)
      pF(pEntry->pTexture, &pEntry->ti);
}

static void ConvertTexture_16(TxtrCacheEntry *pEntry, bool fromTMEM)
{
   ConvertFunction pF;

   if (options.bUseFullTMEM && fromTMEM && status.bAllowLoadFromTMEM)
      pF = gConvertFunctions_16_FullTMEM[pEntry->ti.Format][pEntry->ti.Size];
   else if (gRDP.otherMode.text_tlut >= 2)
      pF = gConvertTlutFunctions_16[pEntry->ti.Format][pEntry->ti.Size];
   else
      pF = gConvertFunctions_16[pEntry->ti.Format][pEntry->ti.Size];

   if (pF)
      pF(pEntry->pTexture, &pEntry->ti);
}

/* a render texture's pixels as intensity (and alpha, or the intensity as
   alpha), for an I or IA colour image */
static void ConvertTextureRGBAtoI(TxtrCacheEntry *pEntry, bool alpha)
{
   DrawInfo srcInfo;
   if (CTexture_StartUpdate(pEntry->pTexture, &srcInfo))
   {
      int nY;
      for (nY = 0; nY < srcInfo.dwCreatedHeight; nY++)
      {
         uint32_t *buf = (uint32_t *)((uint8_t *)srcInfo.lpSurface + nY * srcInfo.lPitch);
         int nX;
         for (nX = 0; nX < srcInfo.dwCreatedWidth; nX++)
         {
            uint32_t val = buf[nX];
            uint32_t b   = (val >> 0) & 0xFF;
            uint32_t g   = (val >> 8) & 0xFF;
            uint32_t r   = (val >> 16) & 0xFF;
            uint32_t i   = (r + g + b) / 3;
            uint32_t a   = alpha ? (val & 0xFF000000) : (i << 24);
            buf[nX] = (a | (i << 16) | (i << 8) | i);
         }
      }
      CTexture_EndUpdate(pEntry->pTexture);
   }
}

/* ---- the cache ---- */

TxtrCacheEntry *TextureManager_GetTexture(TxtrInfo *pgti, bool fromTMEM, bool doCRCCheck, bool AutoExtendTexture)
{
   TxtrCacheEntry *pEntry;
   uint32_t dwPalCRC = 0;
   bool loadFromTextureBuffer = false;
   int txtBufIdxToLoadFrom = -1;
   int maxCI = 0;

   if (g_curRomInfo.bDisableTextureCRC)
      doCRCCheck = false;

   gRDP.texturesAreReloaded = true;

   dwAsmCRC = 0;

   pEntry = GetTxtrCacheEntry(pgti);
   if ((frameBufferOptions.bCheckRenderTextures && !frameBufferOptions.bWriteBackBufToRDRAM)
         || (frameBufferOptions.bCheckBackBufs && !frameBufferOptions.bWriteBackBufToRDRAM))
   {
      txtBufIdxToLoadFrom = RiceFB_CheckAddrInRenderTextures(pgti->Address);
      if (txtBufIdxToLoadFrom >= 0)
      {
         /* the render texture itself, if of the same format and size */
         TxtrCacheEntry *pRender = RiceFB_RenderTextureEntry(txtBufIdxToLoadFrom, pgti);
         loadFromTextureBuffer = true;
         if (pRender)
            return pRender;
      }
   }

   if (frameBufferOptions.bCheckBackBufs
         && RiceFB_CheckAddrInBackBuffers(pgti->Address, pgti->HeightToLoad * pgti->Pitch) >= 0)
   {
      if (!frameBufferOptions.bWriteBackBufToRDRAM)
      {
         /* from a recent back buffer */
         txtBufIdxToLoadFrom = RiceFB_CheckAddrInRenderTextures(pgti->Address);
         if (txtBufIdxToLoadFrom >= 0)
         {
            TxtrCacheEntry *pRender = RiceFB_RenderTextureEntry(txtBufIdxToLoadFrom, pgti);
            loadFromTextureBuffer = true;
            if (pRender)
               return pRender;
         }
      }
   }

   /* already a CRC this frame (but a palette may change) */
   if (pEntry && pEntry->dwTimeLastUsed == status.gRDPTime && status.gDlistCount != 0 && !status.bN64FrameBufferIsUsed)
      dwAsmCRC = pEntry->dwCRC;
   else if (doCRCCheck)
   {
      if (loadFromTextureBuffer)
         dwAsmCRC = RiceFB_RenderTextureCRC(txtBufIdxToLoadFrom);
      else
         CalculateRDRAMCRC(pgti->pPhysicalAddress, pgti->LeftToLoad, pgti->TopToLoad, pgti->WidthToLoad,
               pgti->HeightToLoad, pgti->Size, pgti->Pitch);
   }

   if (doCRCCheck && (pgti->Format == G_IM_FMT_CI || (pgti->Format == G_IM_FMT_RGBA && pgti->Size <= G_IM_SIZ_8b)))
   {
      /* the palette's CRC, over the entries the texels use */
      uint8_t *pStart;
      uint32_t dwOffset;
      uint32_t dwAsmCRCSave;

      if (!pEntry || pEntry->dwCRC != dwAsmCRC || pEntry->maxCI < 0)
         maxCI = CalculateMaxCI(pgti->pPhysicalAddress, pgti->LeftToLoad, pgti->TopToLoad, pgti->WidthToLoad,
               pgti->HeightToLoad, pgti->Size, pgti->Pitch);
      else
         maxCI = pEntry->maxCI;

      /* 8-bit: all 256 entries; 4-bit: the tile's palette of 16 */
      dwOffset = pgti->Size == G_IM_SIZ_8b ? 0 : pgti->Palette << 4;

      pStart       = (uint8_t *)pgti->PalAddress + dwOffset * 2;
      dwAsmCRCSave = dwAsmCRC;
      /* one row: no pitch */
      dwPalCRC     = CalculateRDRAMCRC(pStart, 0, 0, maxCI + 1, 1, G_IM_SIZ_16b, 0);
      dwAsmCRC     = dwAsmCRCSave;
   }

   if (pEntry && doCRCCheck)
   {
      if (pEntry->dwCRC == dwAsmCRC && pEntry->dwPalCRC == dwPalCRC &&
            (!loadFromTextureBuffer || RiceFB_RenderTextureUpdatedAt(txtBufIdxToLoadFrom) < pEntry->FrameLastUsed))
      {
         /* the same texels: as it is */
         pEntry->dwTimeLastUsed = status.gRDPTime;
         pEntry->FrameLastUsed  = status.gDlistCount;
         return pEntry;
      }
   }

   if (pEntry == NULL)
   {
      /* a new entry, in the hash table */
      pEntry = CreateNewCacheEntry(pgti->Address, pgti->WidthToCreate, pgti->HeightToCreate);
      if (pEntry == NULL)
         return NULL;
   }

   pEntry->ti       = *pgti;
   pEntry->dwCRC    = dwAsmCRC;
   pEntry->dwPalCRC = dwPalCRC;
   pEntry->maxCI    = maxCI;

   if (pEntry->pTexture != NULL)
   {
      TextureFmt dwType;

      /* made smaller than asked: loaded to the size made */
      if (pEntry->pTexture->m_dwCreatedTextureWidth < pgti->WidthToCreate)
         pEntry->ti.WidthToLoad = pEntry->pTexture->m_dwCreatedTextureWidth;
      if (pEntry->pTexture->m_dwCreatedTextureHeight < pgti->HeightToCreate)
         pEntry->ti.HeightToLoad = pEntry->pTexture->m_dwCreatedTextureHeight;

      dwType = CTexture_GetSurfaceFormat(pEntry->pTexture);

      /* converted and expanded, then on the GPU once */
      CTexture_HoldUpload(pEntry->pTexture, true);
      if (dwType != TEXTURE_FMT_UNKNOWN)
      {
         if (loadFromTextureBuffer)
         {
            RiceFB_LoadTextureFromRenderTexture(pEntry, txtBufIdxToLoadFrom);

            if (RiceFB_CIFormat() == G_IM_FMT_I)
               ConvertTextureRGBAtoI(pEntry, false);
            else if (RiceFB_CIFormat() == G_IM_FMT_IA)
               ConvertTextureRGBAtoI(pEntry, true);
         }
         else
         {
            if (dwType == TEXTURE_FMT_A8R8G8B8)
               ConvertTexture(pEntry, fromTMEM);
            else
               ConvertTexture_16(pEntry, fromTMEM);
         }
      }

      pEntry->ti.WidthToLoad  = pgti->WidthToLoad;
      pEntry->ti.HeightToLoad = pgti->HeightToLoad;

      if (AutoExtendTexture)
      {
         ExpandTextureS(pEntry);
         ExpandTextureT(pEntry);
      }
      CTexture_HoldUpload(pEntry->pTexture, false);
   }

   return pEntry;
}

/* ---- textures of a colour ---- */

static void updateColorTexture(CTexture *ptexture, uint32_t color)
{
   DrawInfo di;
   int i;

   if (!CTexture_StartUpdate(ptexture, &di))
      return;

   switch (CTexture_GetPixelSize(ptexture))
   {
      case 2: /* 16 bits */
         {
            uint16_t *buf = (uint16_t *)di.lpSurface;
            uint16_t color16 = (uint16_t)((color >> 4) & 0xF);
            color16 |= ((color >> 12) & 0xF) << 4;
            color16 |= ((color >> 20) & 0xF) << 8;
            color16 |= ((color >> 28) & 0xF) << 12;
            for (i = 0; i < 16; i++)
               buf[i] = color16;
         }
         break;
      case 4: /* 32 bits */
         {
            uint32_t *buf = (uint32_t *)di.lpSurface;
            for (i = 0; i < 16; i++)
               buf[i] = color;
         }
         break;
   }

   CTexture_EndUpdate(ptexture);
}

/* @entry's texture, made 4x4 of @color the first time and again when
   @color is not @last's */
static TxtrCacheEntry *ColorTexture(TxtrCacheEntry *entry, uint32_t color, uint32_t *last)
{
   if (entry->pTexture == NULL)
   {
      entry->pTexture = RiceDevice_CreateTexture(4, 4);
      entry->ti.WidthToCreate  = 4;
      entry->ti.HeightToCreate = 4;
      updateColorTexture(entry->pTexture, color);
      gRDP.texturesAreReloaded = true;
   }
   else if (*last != color)
   {
      updateColorTexture(entry->pTexture, color);
      gRDP.texturesAreReloaded = true;
   }

   *last = color;
   return entry;
}

TxtrCacheEntry *TextureManager_GetBlackTexture(void)
{
   if (m_blackTextureEntry.pTexture == NULL)
   {
      m_blackTextureEntry.pTexture = RiceDevice_CreateTexture(4, 4);
      m_blackTextureEntry.ti.WidthToCreate  = 4;
      m_blackTextureEntry.ti.HeightToCreate = 4;
      updateColorTexture(m_blackTextureEntry.pTexture, 0x00000000);
   }
   return &m_blackTextureEntry;
}

/* a fraction in all four bytes */
static uint32_t FracColor(uint8_t fac)
{
   uint32_t color = fac;
   color |= (uint32_t)fac << 8;
   color |= color << 16;
   return color;
}

TxtrCacheEntry *TextureManager_GetConstantColorTexture(uint32_t constant)
{
   static uint32_t prim, env, lodFrac, primLodFrac;

   switch (constant)
   {
      case MUX_PRIM:
         return ColorTexture(&m_PrimColorTextureEntry, gRDP.primitiveColor, &prim);
      case MUX_ENV:
         return ColorTexture(&m_EnvColorTextureEntry, gRDP.envColor, &env);
      case MUX_LODFRAC:
         return ColorTexture(&m_LODFracTextureEntry, FracColor((uint8_t)gRDP.LODFrac), &lodFrac);
      default: /* MUX_PRIMLODFRAC */
         break;
   }

   return ColorTexture(&m_PrimLODFracTextureEntry, FracColor((uint8_t)gRDP.primLODFrac), &primLodFrac);
}

