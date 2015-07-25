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

// Texture related ucode
#ifndef RDP_TEXTURE_H
#define RDP_TEXTURE_H

#include <algorithm>

#include <stdlib.h>
#include "Render.h"

#ifndef min
#define min(a,b) ((a) < (b) ? (a) : (b))
#endif

#ifndef max
#define max(a,b) ((a) > (b) ? (a) : (b))
#endif

uint32_t g_TmemFlag[16];
void SetTmemFlag(uint32_t tmemAddr, uint32_t size);
bool IsTmemFlagValid(uint32_t tmemAddr);
uint32_t GetValidTmemInfoIndex(uint32_t tmemAddr);

void LoadHiresTexture( TxtrCacheEntry &entry );


extern TMEMLoadMapInfo g_tmemInfo0;             // Info for Tmem=0
extern TMEMLoadMapInfo g_tmemInfo1;             // Info for Tmem=0x100

TmemType g_Tmem;

/************************************************************************/
/*                                                                      */
/************************************************************************/
uint32_t sizeShift[4] = {2,1,0,0};
uint32_t sizeIncr[4] = {3,1,0,0};
uint32_t sizeBytes[4] = {0,1,2,4};

inline uint32_t Txl2Words(uint32_t width, uint32_t size)
{
    if( size == TXT_SIZE_4b )
        return max(1, width/16);
    else
        return max(1, width*sizeBytes[size]/8);
}

inline uint32_t CalculateImgSize(uint32_t width, uint32_t height, uint32_t size)
{
    //(((width)*(height) + siz##_INCR) >> siz##_SHIFT) -1
    return (((width)*(height) + sizeIncr[size]) >> sizeShift[size]) -1;
}


inline uint32_t CalculateDXT(uint32_t txl2words)
{
    //#define CALC_DXT(width, b_txl)    ((2048 + TXL2WORDS(width, b_txl) - 1) / TXL2WORDS(width, b_txl))
    if( txl2words == 0 )
        return 1;
    else
        return (2048+txl2words-1)/txl2words;
}

inline uint32_t ReverseDXT(uint32_t val, uint32_t lrs, uint32_t width, uint32_t size)
{
    //#define TXL2WORDS(txls, b_txl)    MAX(1, ((txls)*(b_txl)/8))
    if( val == 0x800 )
        return 1;

    unsigned int low = 2047/val;
    unsigned int high = 2047/(val-1);

    if( CalculateDXT(low) > val )
        low++;

    if( low == high )
        return low;

    for( unsigned int i=low; i<=high; i++ )
    {
        if( Txl2Words(width, size) == i )
            return i;
    }

    return  (low+high)/2;   //dxt = 2047 / (dxt-1);
}

#ifdef _WIN64
#define NO_ASM
#endif
// The following inline assemble routines are borrowed from glN64, I am too tired to
// rewrite these routine by myself.
// Rice, 02/24/2004

static inline void UnswapCopy( void *src, void *dest, uint32_t numBytes )
{
   // copy leading bytes
   int leadingBytes = ((long)src) & 3;
   if (leadingBytes != 0)
   {
      leadingBytes = 4-leadingBytes;
      if ((unsigned int)leadingBytes > numBytes)
         leadingBytes = numBytes;
      numBytes -= leadingBytes;

      src = (void *)((long)src ^ 3);
      for (int i = 0; i < leadingBytes; i++)
      {
         *(uint8_t *)(dest) = *(uint8_t *)(src);
         dest = (void *)((long)dest+1);
         src  = (void *)((long)src -1);
      }
      src = (void *)((long)src+5);
   }

   // copy dwords
   int numDWords = numBytes >> 2;
   while (numDWords--)
   {
      uint32_t dword = *(uint32_t *)src;
      dword = ((dword<<24)|((dword<<8)&0x00FF0000)|((dword>>8)&0x0000FF00)|(dword>>24));
      *(uint32_t *)dest = dword;
      dest = (void *)((long)dest+4);
      src  = (void *)((long)src +4);
   }

   // copy trailing bytes
   int trailingBytes = numBytes & 3;
   if (trailingBytes)
   {
      src = (void *)((long)src ^ 3);
      for (int i = 0; i < trailingBytes; i++)
      {
         *(uint8_t *)(dest) = *(uint8_t *)(src);
         dest = (void *)((long)dest+1);
         src  = (void *)((long)src -1);
      }
   }
}

static inline void DWordInterleave( void *mem, uint32_t numDWords )
{
    int tmp;
    while( numDWords-- )
    {
        tmp = *(int *)((long)mem + 0);
        *(int *)((long)mem + 0) = *(int *)((long)mem + 4);
        *(int *)((long)mem + 4) = tmp;
        mem = (void *)((long)mem + 8);
    }
}

static inline void QWordInterleave( void *mem, uint32_t numDWords )
{
   numDWords >>= 1; // qwords
   while( numDWords-- )
   {
      int tmp0, tmp1;
      tmp0 = *(int *)((long)mem + 0);
      tmp1 = *(int *)((long)mem + 4);
      *(int *)((long)mem + 0) = *(int *)((long)mem + 8);
      *(int *)((long)mem + 8) = tmp0;
      *(int *)((long)mem + 4) = *(int *)((long)mem + 12);
      *(int *)((long)mem + 12) = tmp1;
      mem = (void *)((long)mem + 16);
   }
}

inline uint32_t swapdword( uint32_t value )
{
   return ((value & 0xff000000) >> 24) |
      ((value & 0x00ff0000) >>  8) |
      ((value & 0x0000ff00) <<  8) |
      ((value & 0x000000ff) << 24);
}

static inline uint16_t swapword( uint16_t value )
{
   return (value << 8) | (value >> 8);
}

void ComputeTileDimension(int mask, int clamp, int mirror, int width, uint32_t &widthToCreate, uint32_t &widthToLoad)
{
    int maskwidth = mask > 0 ? (1<<mask) : 0;
    widthToCreate = widthToLoad = width;

    if( mask > 0 )
    {
        if( width > maskwidth )
        {
            if( clamp == 0 )
            {
                // clamp is not used, so just use the dwTileMaskWidth as the real width
                widthToCreate = widthToLoad = maskwidth;
            }
            else
            {
                widthToLoad = maskwidth;
                //gti.WidthToCreate = dwTileWidth;
                // keep the current WidthToCreate, we will do mirror/wrap
                // during texture loading, not during rendering
            }
        }
        else if( width < maskwidth )
        {
            // dwTileWidth < dwTileMaskWidth

            if( clamp == 0 )
            {
                if( maskwidth%width == 0 )
                {
                    if( (maskwidth/width)%2 == 0 || mirror == 0 )
                    {
                        // Do nothing
                        // gti.WidthToLoad = gti.WidthToCreate = gRDP.tiles[tileno].dwWidth = dwTileWidth
                    }
                    else
                    {
                        widthToCreate = maskwidth;
                    }
                }
                else
                {
                    widthToCreate = maskwidth;
                    //widthToLoad = maskwidth;
                }
            }
            else
            {
                widthToCreate = maskwidth;
                //widthToLoad = maskwidth;
            }
        }
        else // dwTileWidth == dwTileMaskWidth
        {
        }

        // Some hacks, to limit the image size
        if( mask >= 8 )
        {
            if( maskwidth / width >= 2 )
            {
                widthToCreate = width;
            }
        }
    }
}

bool conkerSwapHack=false;

bool CalculateTileSizes_method_2(int tileno, TMEMLoadMapInfo *info, TxtrInfo &gti)
{
    Tile &tile = gRDP.tiles[tileno];
    Tile &loadtile = gRDP.tiles[RDP_TXT_LOADTILE];

    uint32_t dwPitch;

    // Now Initialize the texture dimension
    int dwTileWidth;
    int dwTileHeight;
    if( info->bSetBy == CMD_LOADTILE )
    {
        if( tile.sl >= tile.sh )
        {
            dwTileWidth = info->dwWidth;    // From SetTImage
            dwTileWidth = dwTileWidth << info->dwSize >> tile.dwSize;
        }
        else
        {
            dwTileWidth= tile.sh - tile.sl + 1;
        }

        if( tile.tl >= tile.th )
        {
            dwTileHeight= info->th - info->tl + 1;
        }
        else
        {
            dwTileHeight= tile.th - tile.tl + 1;
        }
    }
    else
    {
        if( tile.dwMaskS == 0 || tile.bClampS )
        {
            dwTileWidth = tile.hilite_sh - tile.hilite_sl +1;
            if( dwTileWidth < tile.sh - tile.sl +1 )
                dwTileWidth = tile.sh - tile.sl +1;
            if( dwTileWidth <= 0 )
            {
                DebuggerAppendMsg("Error");
            }
        }
        else
        {
            if( tile.dwMaskS < 8 )
                dwTileWidth = (1 << tile.dwMaskS );
            else if( tile.dwLine )
            {
                dwTileWidth = (tile.dwLine<<5)>>tile.dwSize;
            }
            else
            {
                if( tile.sl <= tile.sh )
                {
                    dwTileWidth = tile.sh - tile.sl +1;
                }
                else if( loadtile.sl <= loadtile.sh )
                {
                    dwTileWidth = loadtile.sh - loadtile.sl +1;
                }
                else
                {
                    dwTileWidth = tile.sh - tile.sl +1;
                }
            }
        }

        if( tile.dwMaskT == 0 || tile.bClampT )
        {
            dwTileHeight= tile.hilite_th - tile.hilite_tl +1;
            if( dwTileHeight < tile.th - tile.tl +1 )
                dwTileHeight = tile.th - tile.tl +1;

            if( dwTileHeight <= 0 )
            {
                DebuggerAppendMsg("Error");
            }
        }
        else
        {
            if( tile.dwMaskT < 8 )
                dwTileHeight = (1 << tile.dwMaskT );
            else if( tile.tl <= tile.th )
            {
                dwTileHeight = tile.th - tile.tl +1;
            }
            else if( loadtile.tl <= loadtile.th )
            {
                dwTileHeight = loadtile.th - loadtile.tl +1;
            }
            else
            {
                dwTileHeight = tile.th - tile.tl +1;
            }
        }
    }

    int dwTileMaskWidth = tile.dwMaskS > 0 ? (1 << tile.dwMaskS ) : 0;
    int dwTileMaskHeight = tile.dwMaskT > 0 ? (1 << tile.dwMaskT ) : 0;

    if( dwTileWidth < 0 || dwTileHeight < 0)
    {
        if( dwTileMaskWidth > 0 )
            dwTileWidth = dwTileMaskWidth;
        else if( dwTileWidth < 0 )
            dwTileWidth = -dwTileWidth;

        if( dwTileMaskHeight > 0 )
            dwTileHeight = dwTileMaskHeight;
        else if( dwTileHeight < 0 )
            dwTileHeight = -dwTileHeight;
    }



    if( dwTileWidth-dwTileMaskWidth == 1 && dwTileMaskWidth && dwTileHeight-dwTileMaskHeight == 1 && dwTileMaskHeight )
    {
        // Hack for Mario Kart
        dwTileWidth--;
        dwTileHeight--;
    }

    ComputeTileDimension(tile.dwMaskS, tile.bClampS,
        tile.bMirrorS, dwTileWidth, gti.WidthToCreate, gti.WidthToLoad);
    tile.dwWidth = gti.WidthToCreate;

    ComputeTileDimension(tile.dwMaskT, tile.bClampT,
        tile.bMirrorT, dwTileHeight, gti.HeightToCreate, gti.HeightToLoad);
    tile.dwHeight = gti.HeightToCreate;

#ifdef DEBUGGER
    if( gti.WidthToCreate < gti.WidthToLoad )
        TRACE2("Check me, width to create = %d, width to load = %d", gti.WidthToCreate, gti.WidthToLoad);
    if( gti.HeightToCreate < gti.HeightToLoad )
        TRACE2("Check me, height to create = %d, height to load = %d", gti.HeightToCreate, gti.HeightToLoad);
#endif


    gti.bSwapped = info->bSwapped;

    if( info->bSetBy == CMD_LOADTILE )
    {
        // It was a tile - the pitch is set by LoadTile
        dwPitch = info->dwWidth<<(info->dwSize-1);

        if( dwPitch == 0 )
        {
            dwPitch = 1024;     // Hack for Bust-A-Move
        }
    }
    else    //Set by LoadBlock
    {
        // It was a block load - the pitch is determined by the tile size
        if (info->dxt == 0 || info->dwTmem != tile.dwTMem )
        {
            dwPitch = tile.dwLine << 3;
            gti.bSwapped = TRUE;
            if( info->dwTmem != tile.dwTMem && info->dxt != 0 && info->dwSize == TXT_SIZE_16b && tile.dwSize == TXT_SIZE_4b )
                conkerSwapHack = true;
        }
        else
        {
            uint32_t DXT = info->dxt;
            if( info->dxt > 1 )
            {
                DXT = ReverseDXT(info->dxt, info->sh, dwTileWidth, tile.dwSize);
            }
            dwPitch = DXT << 3;
        }

        if (tile.dwSize == TXT_SIZE_32b)
            dwPitch = tile.dwLine << 4;
    }

    gti.Pitch = tile.dwPitch = dwPitch;

    if( (gti.WidthToLoad < gti.WidthToCreate || tile.bSizeIsValid == false) && tile.dwMaskS > 0 && gti.WidthToLoad != (unsigned int)dwTileMaskWidth &&
        info->bSetBy == CMD_LOADBLOCK )
        //if( (gti.WidthToLoad < gti.WidthToCreate ) && tile.dwMaskS > 0 && gti.WidthToLoad != dwTileMaskWidth &&
        //  info->bSetBy == CMD_LOADBLOCK )
    {
        // We have got the pitch now, recheck the width_to_load
        uint32_t pitchwidth = dwPitch<<1>>tile.dwSize;
        if( pitchwidth == (unsigned int)dwTileMaskWidth )
        {
            gti.WidthToLoad = pitchwidth;
        }
    }
    if( (gti.HeightToLoad < gti.HeightToCreate  || tile.bSizeIsValid == false) && tile.dwMaskT > 0 && gti.HeightToLoad != (unsigned int)dwTileMaskHeight &&
        info->bSetBy == CMD_LOADBLOCK )
        //if( (gti.HeightToLoad < gti.HeightToCreate  ) && tile.dwMaskT > 0 && gti.HeightToLoad != dwTileMaskHeight &&
        //  info->bSetBy == CMD_LOADBLOCK )
    {
        //uint32_t pitchwidth = dwPitch<<1>>tile.dwSize;
        uint32_t pitchHeight = (info->dwTotalWords<<1)/dwPitch;
        if( pitchHeight == (unsigned int)dwTileMaskHeight || gti.HeightToLoad == 1 )
        {
            gti.HeightToLoad = pitchHeight;
        }
    }
    if( gti.WidthToCreate < gti.WidthToLoad   ) gti.WidthToCreate = gti.WidthToLoad;
    if( gti.HeightToCreate < gti.HeightToLoad ) gti.HeightToCreate = gti.HeightToLoad;

    if( info->bSetBy == CMD_LOADTILE )
    {
        gti.LeftToLoad = (info->sl<<info->dwSize)>>tile.dwSize;
        gti.TopToLoad = info->tl;
    }
    else
    {
        gti.LeftToLoad = (info->sl<<info->dwSize)>>tile.dwSize;
        gti.TopToLoad = (info->tl<<info->dwSize)>>tile.dwSize;
    }

    uint32_t total64BitWordsToLoad = (gti.HeightToLoad*gti.WidthToLoad)>>(4-tile.dwSize);
    if( total64BitWordsToLoad + tile.dwTMem > 0x200 )
    {
        //TRACE0("Warning: texture loading TMEM is over range");
        if( gti.WidthToLoad > gti.HeightToLoad )
        {
            uint32_t newheight = (dwPitch << 1 )>> tile.dwSize;
            tile.dwWidth = gti.WidthToLoad = gti.WidthToCreate = min(newheight, (gti.WidthToLoad&0xFFFFFFFE));
            tile.dwHeight = gti.HeightToCreate = gti.HeightToLoad = ((0x200 - tile.dwTMem) << (4-tile.dwSize)) / gti.WidthToLoad;
        }
        else
        {
            tile.dwHeight = gti.HeightToCreate = gti.HeightToLoad = info->dwTotalWords / ((gti.WidthToLoad << tile.dwSize) >> 1);
        }
    }

    // Check the info
    if( (info->dwTotalWords>>2) < total64BitWordsToLoad+tile.dwTMem-info->dwTmem - 4 )
    {
        // Hack here
        if( (options.enableHackForGames == HACK_FOR_ZELDA||options.enableHackForGames == HACK_FOR_ZELDA_MM) && (unsigned int)tileno != gRSP.curTile )
        {
            return false;
        }

        if( total64BitWordsToLoad+tile.dwTMem-info->dwTmem <= 0x200 )
        {
            LOG_TEXTURE(TRACE4("Fix me, info is not covering this TMEM address,Info start: 0x%x, total=0x%x, TMEM start: 0x%x, total=0x%x", 
                info->dwTmem,info->dwTotalWords>>2, tile.dwTMem, total64BitWordsToLoad));
        }
    }

    //Check memory boundary
    if( gti.Address + gti.HeightToLoad*gti.Pitch >= g_dwRamSize )
    {
        WARNING(TRACE0("Warning: texture loading TMEM is over range 3"));
        gti.HeightToCreate = gti.HeightToLoad = tile.dwHeight = (g_dwRamSize-gti.Address)/gti.Pitch;
    }

    return true;
}

bool CalculateTileSizes_method_1(int tileno, TMEMLoadMapInfo *info, TxtrInfo &gti)
{
    Tile &tile = gRDP.tiles[tileno];
    //Tile &loadtile = gRDP.tiles[RDP_TXT_LOADTILE];

    // Now Initialize the texture dimension
    int loadwidth, loadheight;

    int maskwidth = tile.dwMaskS ? (1 << tile.dwMaskS ) : 0;
    int maskheight = tile.dwMaskT ? (1 << tile.dwMaskT ) : 0;
    int clampwidth = abs(tile.hilite_sh - tile.hilite_sl) +1;
    int clampheight = abs(tile.hilite_th - tile.hilite_tl) +1;
    int linewidth = tile.dwLine << (5 - tile.dwSize);

    gti.bSwapped = info->bSwapped;

    if( info->bSetBy == CMD_LOADTILE )
    {
        loadwidth = (abs(info->sh - info->sl) + 1) << info->dwSize >> tile.dwSize;
        loadheight = (abs(info->th - info->tl) + 1) << info->dwSize >> tile.dwSize;

        tile.dwPitch = info->dwWidth << info->dwSize >> 1;
        if( tile.dwPitch == 0 ) tile.dwPitch = 1024;        // Hack for Bust-A-Move

        gti.LeftToLoad = (info->sl<<info->dwSize)>>tile.dwSize;
        gti.TopToLoad = info->tl;
    }
    else
    {
        loadwidth = abs(tile.sh - tile.sl) +1;
        if( tile.dwMaskS )  
        {
            loadwidth = maskwidth;
        }

        loadheight = abs(tile.th - tile.tl) +1;
        if( tile.dwMaskT )  
        {
            loadheight = maskheight;
        }


        // It was a block load - the pitch is determined by the tile size
        if (tile.dwSize == TXT_SIZE_32b)
            tile.dwPitch = tile.dwLine << 4;
        else if (info->dxt == 0 )
        {
            tile.dwPitch = tile.dwLine << 3;
            gti.bSwapped = TRUE;
            if( info->dwTmem != tile.dwTMem && info->dxt != 0 && info->dwSize == TXT_SIZE_16b && tile.dwSize == TXT_SIZE_4b )
                conkerSwapHack = true;
        }
        else
        {
            uint32_t DXT = info->dxt;
            if( info->dxt > 1 )
            {
                DXT = ReverseDXT(info->dxt, info->sh, loadwidth, tile.dwSize);
            }
            tile.dwPitch = DXT << 3;
        }

        gti.LeftToLoad = (info->sl<<info->dwSize)>>tile.dwSize;
        gti.TopToLoad = (info->tl<<info->dwSize)>>tile.dwSize;
    }

    if( options.enableHackForGames == HACK_FOR_MARIO_KART )
    {
        if( loadwidth-maskwidth == 1 && tile.dwMaskS )
        {
            loadwidth--;
            if( loadheight%2 )  loadheight--;
        }

        if( loadheight-maskheight == 1 && tile.dwMaskT )
        {
            loadheight--;
            if(loadwidth%2) loadwidth--;
        }

        if( loadwidth - ((g_TI.dwWidth<<g_TI.dwSize)>>tile.dwSize) == 1 )
        {
            loadwidth--;
            if( loadheight%2 )  loadheight--;
        }
    }


    // Limit the texture size
    if( g_curRomInfo.bUseSmallerTexture )
    {
        if( tile.dwMaskS && tile.bClampS )
        {
            if( !tile.bMirrorS )
            {
                if( clampwidth/maskwidth >= 2 )
                {
                    clampwidth = maskwidth;
                    tile.bForceWrapS = true;
                }
                else if( clampwidth && maskwidth/clampwidth >= 2 )
                {
                    maskwidth = clampwidth;
                    tile.bForceClampS = true;
                }
            }
            else
            {
                if( clampwidth/maskwidth == 2 )
                {
                    clampwidth = maskwidth*2;
                    tile.bForceWrapS = false;
                }
                else if( clampwidth/maskwidth > 2 )
                {
                    clampwidth = maskwidth*2;
                    tile.bForceWrapS = true;
                }
            }
        }

        if( tile.dwMaskT && tile.bClampT )
        {
            if( !tile.bMirrorT )
            {
                if( clampheight/maskheight >= 2 )
                {
                    clampheight = maskheight;
                    tile.bForceWrapT = true;
                }
                else if( clampheight && maskheight/clampheight >= 2 )
                {
                    maskwidth = clampwidth;
                    tile.bForceClampT = true;
                }
            }
            else
            {
                if( clampheight/maskheight == 2 )
                {
                    clampheight = maskheight*2;
                    tile.bForceWrapT = false;
                }
                else if( clampheight/maskheight >= 2 )
                {
                    clampheight = maskheight*2;
                    tile.bForceWrapT = true;
                }
            }
        }
    }
    else
    {
        //if( clampwidth > linewidth )  clampwidth = linewidth;
        if( clampwidth > 512 && clampheight > 512 )
        {
            if( clampwidth > maskwidth && maskwidth && clampheight > 256 )  clampwidth = maskwidth;
            if( clampheight > maskheight && maskheight && clampheight > 256 )   clampheight = maskheight;
        }

        if( tile.dwMaskS > 8 && tile.dwMaskT > 8 )  
        {
            maskwidth = loadwidth;
            maskheight = loadheight;
        }
        else 
        {
            if( tile.dwMaskS > 10 )
                maskwidth = loadwidth;
            if( tile.dwMaskT > 10 )
                maskheight = loadheight;
        }
    }

    gti.Pitch = tile.dwPitch;

    if( tile.dwMaskS == 0 || tile.bClampS )
    {
        gti.WidthToLoad = linewidth ? min( linewidth, maskwidth ? min(clampwidth,maskwidth) : clampwidth ) : clampwidth;
        if( tile.dwMaskS && clampwidth < maskwidth )
            tile.dwWidth = gti.WidthToCreate = clampwidth;
        else
            tile.dwWidth = gti.WidthToCreate = max(clampwidth,maskwidth);
    }
    else
    {
        gti.WidthToLoad = loadwidth > 2 ? min(loadwidth,maskwidth) : maskwidth;
        if( linewidth ) gti.WidthToLoad = min( linewidth, (int)gti.WidthToLoad );
        tile.dwWidth = gti.WidthToCreate = maskwidth;
    }

    if( tile.dwMaskT == 0 || tile.bClampT )
    {
        gti.HeightToLoad = maskheight ? min(clampheight,maskheight) : clampheight;
        if( tile.dwMaskT && clampheight < maskheight )
            tile.dwHeight = gti.HeightToCreate = clampheight;
        else
            tile.dwHeight = gti.HeightToCreate = max(clampheight,maskheight);
    }
    else
    {
        gti.HeightToLoad = loadheight > 2 ? min(loadheight,maskheight) : maskheight;
        tile.dwHeight = gti.HeightToCreate = maskheight;
    }

    if( options.enableHackForGames == HACK_FOR_MARIO_KART )
    {
        if( gti.WidthToLoad - ((g_TI.dwWidth<<g_TI.dwSize)>>tile.dwSize) == 1 )
        {
            gti.WidthToLoad--;
            if( gti.HeightToLoad%2 )    gti.HeightToLoad--;
        }
    }

    // Double check
    uint32_t total64BitWordsToLoad = (gti.HeightToLoad*gti.WidthToLoad)>>(4-tile.dwSize);
    if( total64BitWordsToLoad + tile.dwTMem > 0x200 )
    {
        //TRACE0("Warning: texture loading tmem is over range");
        if( gti.WidthToLoad > gti.HeightToLoad )
        {
            uint32_t newheight = (tile.dwPitch << 1 )>> tile.dwSize;
            tile.dwWidth = gti.WidthToLoad = gti.WidthToCreate = min(newheight, (gti.WidthToLoad&0xFFFFFFFE));
            tile.dwHeight = gti.HeightToCreate = gti.HeightToLoad = ((0x200 - tile.dwTMem) << (4-tile.dwSize)) / gti.WidthToLoad;
        }
        else
        {
            tile.dwHeight = gti.HeightToCreate = gti.HeightToLoad = info->dwTotalWords / ((gti.WidthToLoad << tile.dwSize) >> 1);
        }
    }

    // Check the info
    if( (info->dwTotalWords>>2) < total64BitWordsToLoad+tile.dwTMem-info->dwTmem - 4 )
    {
        // Hack here
        if( (options.enableHackForGames == HACK_FOR_ZELDA||options.enableHackForGames == HACK_FOR_ZELDA_MM) && (unsigned int)tileno != gRSP.curTile )
        {
            return false;
        }

        if( total64BitWordsToLoad+tile.dwTMem-info->dwTmem <= 0x200 )
        {
            LOG_TEXTURE(TRACE4("Fix me, info is not covering this Tmem address,Info start: 0x%x, total=0x%x, Tmem start: 0x%x, total=0x%x", 
                info->dwTmem,info->dwTotalWords>>2, tile.dwTMem, total64BitWordsToLoad));
        }
    }

    //Check memory boundary
    if( gti.Address + gti.HeightToLoad*gti.Pitch >= g_dwRamSize )
    {
        WARNING(TRACE0("Warning: texture loading tmem is over range 3"));
        gti.HeightToCreate = gti.HeightToLoad = tile.dwHeight = (g_dwRamSize-gti.Address)/gti.Pitch;
    }

    return true;
}

TxtrCacheEntry* LoadTexture(uint32_t tileno)
{
    //TxtrCacheEntry *pEntry = NULL;
    TxtrInfo gti;
    uint32_t *rdram_uint32_t = (uint32_t*)gfx_info.RDRAM;

    Tile &tile = gRDP.tiles[tileno];

    // Retrieve the tile loading info
    uint32_t infoTmemAddr = tile.dwTMem;
    TMEMLoadMapInfo *info = &g_tmemLoadAddrMap[infoTmemAddr];
    if( !IsTmemFlagValid(infoTmemAddr) )
    {
        infoTmemAddr =  GetValidTmemInfoIndex(infoTmemAddr);
        info = &g_tmemLoadAddrMap[infoTmemAddr];
    }

    if( info->dwFormat != tile.dwFormat )
    {
        // Check the tile format, hack for Zelda's road
        if( tileno != gRSP.curTile && tile.dwTMem == gRDP.tiles[gRSP.curTile].dwTMem &&
            tile.dwFormat != gRDP.tiles[gRSP.curTile].dwFormat )
        {
            //TRACE1("Tile %d format is not matching the loaded texture format", tileno);
            return NULL;
        }
    }

    gti = tile; // Copy tile info to textureInfo entry

    gti.TLutFmt = gRDP.otherMode.text_tlut <<RSP_SETOTHERMODE_SHIFT_TEXTLUT;
    if (gti.Format == TXT_FMT_CI && gti.TLutFmt == TLUT_FMT_NONE )
        gti.TLutFmt = TLUT_FMT_RGBA16;      // Force RGBA

    gti.PalAddress = (uint8_t *) (&g_wRDPTlut[0]);
    if( !options.bUseFullTMEM && tile.dwSize == TXT_SIZE_4b )
        gti.PalAddress += 16  * 2 * tile.dwPalette; 

    gti.Address = (info->dwLoadAddress+(tile.dwTMem-infoTmemAddr)*8) & (g_dwRamSize-1) ;
    gti.pPhysicalAddress = ((uint8_t*)rdram_uint32_t) + gti.Address;
    gti.tileNo = tileno;

    if( g_curRomInfo.bTxtSizeMethod2 )
    {
        if( !CalculateTileSizes_method_2(tileno, info, gti) )
            return NULL;
    }
    else
    {
        if( !CalculateTileSizes_method_1(tileno, info, gti) )
            return NULL;
    }

    LOG_TEXTURE(
    {
        TRACE0("Loading texture:\n");
        DebuggerAppendMsg("Left: %d, Top: %d, Width: %d, Height: %d, Size to Load (%d, %d)", 
            gti.LeftToLoad, gti.TopToLoad, gti.WidthToCreate, gti.HeightToCreate, gti.WidthToLoad, gti.HeightToLoad);
        DebuggerAppendMsg("Pitch: %d, Addr: 0x%08x", gti.Pitch, gti.Address);
    });

    // Option for faster loading tiles
    if( g_curRomInfo.bFastLoadTile && info->bSetBy == CMD_LOADTILE && ((gti.Pitch<<1)>>gti.Size) <= 0x400
        //&& ((gti.Pitch<<1)>>gti.Size) > 128 && status.primitiveType == PRIM_TEXTRECT
        )
    {
        uint32_t idx = tileno-gRSP.curTile;
        status.LargerTileRealLeft[idx] = gti.LeftToLoad;
        gti.LeftToLoad=0;
        gti.WidthToLoad = gti.WidthToCreate = ((gti.Pitch<<1)>>gti.Size);
        status.UseLargerTile[idx]=true;
    }

    // Loading the textures by using texture cache manager
    return gTextureManager.GetTexture(&gti, true, true, true);  // Load the texture by using texture cache
}

void PrepareTextures()
{
    if( gRDP.textureIsChanged || !currentRomOptions.bFastTexCRC ||
        CRender::g_pRender->m_pColorCombiner->m_pDecodedMux->m_ColorTextureFlag[0] ||
        CRender::g_pRender->m_pColorCombiner->m_pDecodedMux->m_ColorTextureFlag[1] )
    {
        status.UseLargerTile[0]=false;
        status.UseLargerTile[1]=false;

        int tilenos[2];
        if( CRender::g_pRender->IsTexel0Enable() || gRDP.otherMode.cycle_type  == CYCLE_TYPE_COPY )
            tilenos[0] = gRSP.curTile;
        else
            tilenos[0] = -1;

        if( gRSP.curTile<7 && CRender::g_pRender->IsTexel1Enable() )
            tilenos[1] = gRSP.curTile+1;
        else
            tilenos[1] = -1;


        for( int i=0; i<2; i++ )
        {
            if( tilenos[i] < 0 )    continue;

            if( CRender::g_pRender->m_pColorCombiner->m_pDecodedMux->m_ColorTextureFlag[i] )
            {
                TxtrCacheEntry *pEntry = gTextureManager.GetConstantColorTexture(CRender::g_pRender->m_pColorCombiner->m_pDecodedMux->m_ColorTextureFlag[i]);
                CRender::g_pRender->SetCurrentTexture( tilenos[i], pEntry->pTexture, 4, 4, pEntry);
            }
            else
            {
                TxtrCacheEntry *pEntry = LoadTexture(tilenos[i]);
                if (pEntry && pEntry->pTexture )
                {
                    CRender::g_pRender->SetCurrentTexture( tilenos[i], 
                        pEntry->pTexture,
                        pEntry->ti.WidthToLoad, pEntry->ti.HeightToLoad, pEntry);
                }
                else
                {
                    pEntry = gTextureManager.GetBlackTexture();
                    CRender::g_pRender->SetCurrentTexture( tilenos[i], pEntry->pTexture, 4, 4, pEntry);
                    _VIDEO_DisplayTemporaryMessage("Fail to load texture, use black to replace");
                }

            }
        }

        gRDP.textureIsChanged = false;
    }
}

extern uint32_t g_TxtLoadBy;;

void DLParser_LoadTLut(Gfx *gfx)
{
   uint8_t *rdram_u8 = (uint8_t*)gfx_info.RDRAM;
    gRDP.textureIsChanged = true;

    uint32_t tileno = gfx->loadtile.tile;
    uint32_t uls = gfx->loadtile.sl/4;
    uint32_t ult = gfx->loadtile.tl/4;
    uint32_t lrs = gfx->loadtile.sh/4;
    uint32_t lrt = gfx->loadtile.th/4;

#ifdef DEBUGGER
    uint32_t dwTLutFmt = (gRDP.otherModeH >> RSP_SETOTHERMODE_SHIFT_TEXTLUT)&0x3;
#endif
    // Starting location in the palettes
    uint32_t dwTMEMOffset = gRDP.tiles[tileno].dwTMem - 256;  

    // Number to copy
    uint32_t dwCount = ((uint16_t)((gfx->words.w1) >> 14) & 0x03FF) + 1;
    uint32_t dwRDRAMOffset = 0;

    Tile &tile = gRDP.tiles[tileno];
    tile.bForceWrapS = tile.bForceWrapT = tile.bForceClampS = tile.bForceClampT = false;

    tile.hilite_sl = tile.sl = uls;
    tile.hilite_tl = tile.tl = ult;
    tile.sh = lrs;
    tile.th = lrt;
    tile.bSizeIsValid = true;

    tile.lastTileCmd = CMD_LOADTLUT;

#ifdef DEBUGGER
    /*
    if((((gfx->words.w0)>>12)&0x3) != 0 || (((gfx->words.w0))&0x3) != 0 || (((gfx->words.w1)>>12)&0x3) != 0 || (((gfx->words.w1))&0x3) != 0)
    TRACE0("Load tlut, sl,tl,sh,th are not integers");
    */
#endif

    dwCount = (lrs - uls)+1;
    dwRDRAMOffset = (uls + ult*g_TI.dwWidth )*2;
    uint32_t dwPalAddress = g_TI.dwAddr + dwRDRAMOffset;

    //Copy PAL to the PAL memory
    uint16_t *srcPal = (uint16_t*)(rdram_u8 + (dwPalAddress& (g_dwRamSize-1)) );
    for (uint32_t i=0; i<dwCount && i<0x100; i++)
        g_wRDPTlut[(i+dwTMEMOffset)^1] = srcPal[i^1];

    if( options.bUseFullTMEM )
    {
        for (uint32_t i=0; i<dwCount && i+tile.dwTMem<0x200; i++)
            *(uint16_t*)(&g_Tmem.g_Tmem64bit[tile.dwTMem+i]) = srcPal[i^1];
    }

    LOG_TEXTURE(
    {
        DebuggerAppendMsg("LoadTLut Tile: %d Start: 0x%X+0x%X, Count: 0x%X\nFmt is %s, TMEM=0x%X\n", 
            tileno, g_TI.dwAddr, dwRDRAMOffset, dwCount,textluttype[dwTLutFmt],
            dwTMEMOffset);

        DebuggerAppendMsg("    :ULS: 0x%X, ULT:0x%X, LRS: 0x%X, LRT:0x%X\n", uls, ult, lrs,lrt);

        if( pauseAtNext && eventToPause == NEXT_LOADTLUT && dwCount == 16 ) 
        {
            char buf[2000];
            strcpy(buf, "Data:\n");
            for(uint32_t i=0; i<16; i++ )
            {
                sprintf(buf+strlen(buf), "%04X ", g_wRDPTlut[dwTMEMOffset+i]);
                if(i%4 == 3)
                    sprintf(buf+strlen(buf), "\n");
            }
            sprintf(buf+strlen(buf), "\n");
            TRACE0(buf);
        }
    });

    DEBUGGER_PAUSE_COUNT_N(NEXT_LOADTLUT);

    extern bool RevTlutTableNeedUpdate;
    RevTlutTableNeedUpdate = true;
    g_TxtLoadBy = CMD_LOADTLUT;
}


void DLParser_LoadBlock(Gfx *gfx)
{
    gRDP.textureIsChanged = true;

    uint32_t tileno   = gfx->loadtile.tile;
    uint32_t uls      = gfx->loadtile.sl;
    uint32_t ult      = gfx->loadtile.tl;
    uint32_t lrs      = gfx->loadtile.sh;
    uint32_t dxt      = gfx->loadtile.th;                 // 1.11 fixed point

    Tile &tile = gRDP.tiles[tileno];
    tile.bForceWrapS = tile.bForceWrapT = tile.bForceClampS = tile.bForceClampT = false;

    uint32_t size     = lrs+1;
    if( tile.dwSize == TXT_SIZE_32b )   size<<=1;

    SetTmemFlag(tile.dwTMem, size>>2);

    TMEMLoadMapInfo &info = g_tmemLoadAddrMap[tile.dwTMem];

    info.bSwapped = (dxt == 0? TRUE : FALSE);

    info.sl = tile.hilite_sl = tile.sl = uls;
    info.sh = tile.hilite_sh = tile.sh = lrs;
    info.tl = tile.tl = ult;
    info.th = tile.th = dxt;
    tile.bSizeIsValid = false;

    for( int i=0; i<8; i++ )
    {
        if( gRDP.tiles[i].dwTMem == tile.dwTMem )
            tile.lastTileCmd = CMD_LOADBLOCK;
    }

    info.dwLoadAddress = g_TI.dwAddr;
    info.bSetBy = CMD_LOADBLOCK;
    info.dxt = dxt;
    info.dwLine = tile.dwLine;

    info.dwFormat = g_TI.dwFormat;
    info.dwSize = g_TI.dwSize;
    info.dwWidth = g_TI.dwWidth;
    info.dwTotalWords = size;
    info.dwTmem = tile.dwTMem;

    if( gRDP.tiles[tileno].dwTMem == 0 )
    {
        if( size >= 1024 )
        {
            memcpy(&g_tmemInfo0, &info, sizeof(TMEMLoadMapInfo) );
            g_tmemInfo0.dwTotalWords = size>>2;
        }
        
        if( size == 2048 )
        {
            memcpy(&g_tmemInfo1, &info, sizeof(TMEMLoadMapInfo) );
            g_tmemInfo1.dwTotalWords = size>>2;
        }
    }
    else if( tile.dwTMem == 0x100 )
    {
        if( size == 1024 )
        {
            memcpy(&g_tmemInfo1, &info, sizeof(TMEMLoadMapInfo) );
            g_tmemInfo1.dwTotalWords = size>>2;
        }
    }

    g_TxtLoadBy = CMD_LOADBLOCK;


    if( options.bUseFullTMEM )
    {
       uint8_t *rdram_u8 = (uint8_t*)gfx_info.RDRAM;
        uint32_t bytes = (lrs + 1) << tile.dwSize >> 1;
        uint32_t address = g_TI.dwAddr + ult * g_TI.bpl + (uls << g_TI.dwSize >> 1);
        if ((bytes == 0) || ((address + bytes) > g_dwRamSize) || (((tile.dwTMem << 3) + bytes) > 4096))
        {
            return;
        }
        uint64_t* src = (uint64_t*)(rdram_u8 + address);
        uint64_t* dest = &g_Tmem.g_Tmem64bit[tile.dwTMem];

        if( dxt > 0)
        {
            void (*Interleave)( void *mem, uint32_t numDWords );

            uint32_t line = (2047 + dxt) / dxt;
            uint32_t bpl = line << 3;
            uint32_t height = bytes / bpl;

            if (tile.dwSize == TXT_SIZE_32b)
                Interleave = QWordInterleave;
            else
                Interleave = DWordInterleave;

            for (uint32_t y = 0; y < height; y++)
            {
                UnswapCopy( src, dest, bpl );
                if (y & 1) Interleave( dest, line );

                src += line;
                dest += line;
            }
        }
        else
        {
            UnswapCopy( src, dest, bytes );
        }
    }


    LOG_UCODE("    Tile:%d (%d,%d - %d) DXT:0x%04x\n", tileno, uls, ult, lrs, dxt);

    LOG_TEXTURE(
    {
        DebuggerAppendMsg("LoadBlock:%d (%d,%d,%d) DXT:0x%04x(%X)\n",
            tileno, uls, ult, (((gfx->words.w1)>>12)&0x0FFF), dxt, ((gfx->words.w1)&0x0FFF));
    });

    DEBUGGER_PAUSE_COUNT_N(NEXT_TEXTURE_CMD);
}

void swap(uint32_t &a, uint32_t &b)
{
    uint32_t temp = a;
    a = b;
    b = temp;
}
void DLParser_LoadTile(Gfx *gfx)
{
    gRDP.textureIsChanged = true;

    uint32_t tileno   = gfx->loadtile.tile;
    uint32_t uls      = gfx->loadtile.sl/4;
    uint32_t ult      = gfx->loadtile.tl/4;
    uint32_t lrs      = gfx->loadtile.sh/4;
    uint32_t lrt      = gfx->loadtile.th/4;

    Tile &tile = gRDP.tiles[tileno];
    tile.bForceWrapS = tile.bForceWrapT = tile.bForceClampS = tile.bForceClampT = false;

    if (lrt < ult) swap(lrt, ult);
    if (lrs < uls) swap(lrs, uls);

    tile.hilite_sl = tile.sl = uls;
    tile.hilite_tl = tile.tl = ult;
    tile.hilite_sh = tile.sh = lrs;
    tile.hilite_th = tile.th = lrt;
    tile.bSizeIsValid = true;

    // compute block height, and bpl of source and destination
    uint32_t bpl = (lrs - uls + 1) << tile.dwSize >> 1;
    uint32_t height = lrt - ult + 1;
    uint32_t line = tile.dwLine;
    if (tile.dwSize == TXT_SIZE_32b) line <<= 1;

    if (((tile.dwTMem << 3) + line * height) > 4096)  // check destination ending point (TMEM is 4k bytes)
        return;

    if( options.bUseFullTMEM )
    {
       uint8_t *rdram_u8 = (uint8_t*)gfx_info.RDRAM;
        void (*Interleave)( void *mem, uint32_t numDWords );

        if( g_TI.bpl == 0 )
        {
            if( options.enableHackForGames == HACK_FOR_BUST_A_MOVE )
            {
                g_TI.bpl = 1024;        // Hack for Bust-A-Move
            }
            else
            {
                TRACE0("Warning: g_TI.bpl = 0" );
            }
        }

        uint32_t address = g_TI.dwAddr + tile.tl * g_TI.bpl + (tile.sl << g_TI.dwSize >> 1);
        uint64_t* src = (uint64_t*)&rdram_u8[address];
        uint8_t* dest = (uint8_t*)&g_Tmem.g_Tmem64bit[tile.dwTMem];

        if ((address + height * bpl) > g_dwRamSize) // check source ending point
        {
            return;
        }

        // Line given for 32-bit is half what it seems it should since they split the
        // high and low words. I'm cheating by putting them together.
        if (tile.dwSize == TXT_SIZE_32b)
        {
            Interleave = QWordInterleave;
        }
        else
        {
            Interleave = DWordInterleave;
        }

        if( tile.dwLine == 0 )
        {
            //tile.dwLine = 1;
            return;
        }

        for (uint32_t y = 0; y < height; y++)
        {
            UnswapCopy( src, dest, bpl );
            if (y & 1) Interleave( dest, line );

            src += g_TI.bpl;
            dest += line;
        }
    }


    for( int i=0; i<8; i++ )
    {
        if( gRDP.tiles[i].dwTMem == tile.dwTMem )
            gRDP.tiles[i].lastTileCmd = CMD_LOADTILE;
    }

    uint32_t size = line * height;
    SetTmemFlag(tile.dwTMem,size );

    LOG_TEXTURE(
    {
        DebuggerAppendMsg("LoadTile:%d (%d,%d) -> (%d,%d) [%d x %d]\n",
            tileno, uls, ult, lrs, lrt,
            (lrs - uls)+1, (lrt - ult)+1);
    });

    
    DEBUGGER_PAUSE_COUNT_N(NEXT_TEXTURE_CMD);

    LOG_UCODE("    Tile:%d (%d,%d) -> (%d,%d) [%d x %d]",
        tileno, uls, ult, lrs, lrt,
        (lrs - uls)+1, (lrt - ult)+1);

    TMEMLoadMapInfo &info = g_tmemLoadAddrMap[tile.dwTMem];

    info.dwLoadAddress = g_TI.dwAddr;
    info.dwFormat = g_TI.dwFormat;
    info.dwSize = g_TI.dwSize;
    info.dwWidth = g_TI.dwWidth;

    info.sl = uls;
    info.sh = lrs;
    info.tl = ult;
    info.th = lrt;
    
    info.dxt = 0;
    info.dwLine = tile.dwLine;
    info.dwTmem = tile.dwTMem;
    info.dwTotalWords = size<<2;

    info.bSetBy = CMD_LOADTILE;
    info.bSwapped =FALSE;

    g_TxtLoadBy = CMD_LOADTILE;

    if( tile.dwTMem == 0 )
    {
        if( size >= 256 )
        {
            memcpy(&g_tmemInfo0, &info, sizeof(TMEMLoadMapInfo) );
            g_tmemInfo0.dwTotalWords = size;
        }

        if( size == 512 )
        {
            memcpy(&g_tmemInfo1, &info, sizeof(TMEMLoadMapInfo) );
            g_tmemInfo1.dwTotalWords = size;
        }
    }
    else if( tile.dwTMem == 0x100 )
    {
        if( size == 256 )
        {
            memcpy(&g_tmemInfo1, &info, sizeof(TMEMLoadMapInfo) );
            g_tmemInfo1.dwTotalWords = size;
        }
    }
}


const char *pszOnOff[2] = {"Off", "On"};
uint32_t lastSetTile;
void DLParser_SetTile(Gfx *gfx)
{
    gRDP.textureIsChanged = true;

    uint32_t tileno       = gfx->settile.tile;
    Tile &tile = gRDP.tiles[tileno];
    tile.bForceWrapS = tile.bForceWrapT = tile.bForceClampS = tile.bForceClampT = false;

    lastSetTile = tileno;

    tile.dwFormat   = gfx->settile.fmt;
    tile.dwSize     = gfx->settile.siz;
    tile.dwLine     = gfx->settile.line;
    tile.dwTMem     = gfx->settile.tmem;

    tile.dwPalette  = gfx->settile.palette;
    tile.bClampT    = gfx->settile.ct;
    tile.bMirrorT   = gfx->settile.mt;
    tile.dwMaskT    = gfx->settile.maskt;
    tile.dwShiftT   = gfx->settile.shiftt;
    tile.bClampS    = gfx->settile.cs;
    tile.bMirrorS   = gfx->settile.ms;
    tile.dwMaskS    = gfx->settile.masks;
    tile.dwShiftS   = gfx->settile.shifts;


    tile.fShiftScaleS = 1.0f;
    if( tile.dwShiftS )
    {
        if( tile.dwShiftS > 10 )
        {
            tile.fShiftScaleS = (float)(1 << (16 - tile.dwShiftS));
        }
        else
        {
            tile.fShiftScaleS = (float)1.0f/(1 << tile.dwShiftS);
        }
    }

    tile.fShiftScaleT = 1.0f;
    if( tile.dwShiftT )
    {
        if( tile.dwShiftT > 10 )
        {
            tile.fShiftScaleT = (float)(1 << (16 - tile.dwShiftT));
        }
        else
        {
            tile.fShiftScaleT = (float)1.0f/(1 << tile.dwShiftT);
        }
    }

    // Hack for DK64
    /*
    if( tile.dwMaskS > 0 && tile.dwMaskT > 0 && tile.dwMaskS < 8 && tile.dwMaskT < 8 )
    {
        tile.sh = tile.sl + (1<<tile.dwMaskS);
        tile.th = tile.tl + (1<<tile.dwMaskT);
        tile.hilite_sl = tile.sl;
        tile.hilite_tl = tile.tl;
    }
    */

    tile.lastTileCmd = CMD_SETTILE;

    LOG_TEXTURE(
    {
    DebuggerAppendMsg("SetTile:%d  Fmt: %s/%s Line:%d TMem:0x%04x Palette:%d\n",
        tileno, pszImgFormat[tile.dwFormat], pszImgSize[tile.dwSize],
        tile.dwLine,  tile.dwTMem, tile.dwPalette);
    DebuggerAppendMsg("         S: Clamp: %s Mirror:%s Mask:0x%x Shift:0x%x\n",
        pszOnOff[tile.bClampS],pszOnOff[tile.bMirrorS],
        tile.dwMaskS, tile.dwShiftS);
    DebuggerAppendMsg("         T: Clamp: %s Mirror:%s Mask:0x%x Shift:0x%x\n",
        pszOnOff[tile.bClampT],pszOnOff[tile.bMirrorT],
        tile.dwMaskT, tile.dwShiftT);
    });

    DEBUGGER_PAUSE_COUNT_N(NEXT_TEXTURE_CMD);

    LOG_UCODE("    Tile:%d  Fmt: %s/%s Line:%d TMem:0x%04x Palette:%d",
        tileno, pszImgFormat[tile.dwFormat], pszImgSize[tile.dwSize],
        tile.dwLine, tile.dwTMem, tile.dwPalette);
    LOG_UCODE("         S: Clamp: %s Mirror:%s Mask:0x%x Shift:0x%x",
        pszOnOff[tile.bClampS],pszOnOff[tile.bMirrorS],
        tile.dwMaskS, tile.dwShiftS);
    LOG_UCODE("         T: Clamp: %s Mirror:%s Mask:0x%x Shift:0x%x",
        pszOnOff[tile.bClampT],pszOnOff[tile.bMirrorT],
        tile.dwMaskT, tile.dwShiftT);
}

void DLParser_SetTileSize(Gfx *gfx)
{
    gRDP.textureIsChanged = true;

    uint32_t tileno   = gfx->loadtile.tile;
    int sl      = gfx->loadtile.sl;
    int tl      = gfx->loadtile.tl;
    int sh      = gfx->loadtile.sh;
    int th      = gfx->loadtile.th;

    Tile &tile = gRDP.tiles[tileno];
    tile.bForceWrapS = tile.bForceWrapT = tile.bForceClampS = tile.bForceClampT = false;

    if( options.bUseFullTMEM )
    {
        tile.bSizeIsValid = true;
        tile.hilite_sl = tile.sl = sl / 4;
        tile.hilite_tl = tile.tl = tl / 4;
        tile.hilite_sh = tile.sh = sh / 4;
        tile.hilite_th = tile.th = th / 4;

        tile.fhilite_sl = tile.fsl = sl / 4.0f;
        tile.fhilite_tl = tile.ftl = tl / 4.0f;
        tile.fhilite_sh = tile.fsh = sh / 4.0f;
        tile.fhilite_th = tile.fth = th / 4.0f;

        tile.lastTileCmd = CMD_SETTILE_SIZE;
    }
    else
    {
        if( tile.lastTileCmd != CMD_SETTILE_SIZE )
        {
            tile.bSizeIsValid = true;
            if( sl/4 > sh/4 || tl/4 > th/4 || (sh == 0 && tile.dwShiftS==0 && th == 0 && tile.dwShiftT ==0 ) )
            {
#ifdef DEBUGGER
                if( sl != 0 || tl != 0 || sh != 0 || th != 0 )
                {
                    if( tile.dwMaskS==0 || tile.dwMaskT==0 )
                        TRACE0("Check me, setTileSize is not correct");
                }
#endif
                tile.bSizeIsValid = false;
            }
            tile.hilite_sl = tile.sl = sl / 4;
            tile.hilite_tl = tile.tl = tl / 4;
            tile.hilite_sh = tile.sh = sh / 4;
            tile.hilite_th = tile.th = th / 4;

            tile.fhilite_sl = tile.fsl = sl / 4.0f;
            tile.fhilite_tl = tile.ftl = tl / 4.0f;
            tile.fhilite_sh = tile.fsh = sh / 4.0f;
            tile.fhilite_th = tile.fth = th / 4.0f;

            tile.lastTileCmd = CMD_SETTILE_SIZE;
        }
        else
        {
            tile.fhilite_sh = tile.fsh;
            tile.fhilite_th = tile.fth;
            tile.fhilite_sl = tile.fsl = (sl>0x7ff ? (sl-0xfff) : sl)/4.0f;
            tile.fhilite_tl = tile.ftl = (tl>0x7ff ? (tl-0xfff) : tl)/4.0f;

            tile.hilite_sl = sl>0x7ff ? (sl-0xfff) : sl;
            tile.hilite_tl = tl>0x7ff ? (tl-0xfff) : tl;
            tile.hilite_sl /= 4;
            tile.hilite_tl /= 4;
            tile.hilite_sh = sh/4;
            tile.hilite_th = th/4;

            tile.lastTileCmd = CMD_SETTILE_SIZE;
        }
    }

    LOG_TEXTURE(
    {
    DebuggerAppendMsg("SetTileSize:%d (%d/4,%d/4) -> (%d/4,%d/4) [%d x %d]\n",
        tileno, sl, tl, sh, th, 
        ((sh/4) - (sl/4)) + 1, ((th/4) - (tl/4)) + 1);
    });
    DEBUGGER_PAUSE_COUNT_N(NEXT_TEXTURE_CMD);

    LOG_UCODE("    Tile:%d (%d,%d) -> (%d,%d) [%d x %d]",
        tileno, sl/4, tl/4, sh/4, th/4,
        ((sh/4) - (sl/4)) + 1, ((th/4) - (tl/4)) + 1);
}

extern const char *pszImgFormat[8];// = {"RGBA", "YUV", "CI", "IA", "I", "?1", "?2", "?3"};
extern const char *pszImgSize[4];// = {"4", "8", "16", "32"};
void DLParser_SetTImg(Gfx *gfx)
{
    gRDP.textureIsChanged = true;

    g_TI.dwFormat   = gfx->setimg.fmt;
    g_TI.dwSize     = gfx->setimg.siz;
    g_TI.dwWidth    = gfx->setimg.width + 1;
    g_TI.dwAddr     = RSPSegmentAddr((gfx->setimg.addr));
    g_TI.bpl        = g_TI.dwWidth << g_TI.dwSize >> 1;

#ifdef DEBUGGER
    if( g_TI.dwAddr == 0x00ffffff)
    {
        TRACE0("Check me here in setTimg");
    }

    LOG_TEXTURE(TRACE4("SetTImage: 0x%08x Fmt: %s/%s Width in Pixel: %d\n", g_TI.dwAddr,
            pszImgFormat[g_TI.dwFormat], pszImgSize[g_TI.dwSize], g_TI.dwWidth));

    DEBUGGER_PAUSE_COUNT_N(NEXT_TEXTURE_CMD);

    LOG_UCODE("Image: 0x%08x Fmt: %s/%s Width in Pixel: %d", g_TI.dwAddr,
        pszImgFormat[g_TI.dwFormat], pszImgSize[g_TI.dwSize], g_TI.dwWidth);
#endif
}

void DLParser_TexRect(Gfx *gfx)
{
   uint8_t *rdram_u8 = (uint8_t*)gfx_info.RDRAM;
    //Gtexrect *gtextrect = (Gtexrect *)gfx;

    if( !status.bCIBufferIsRendered ) g_pFrameBufferManager->ActiveTextureBuffer();

    status.primitiveType = PRIM_TEXTRECT;

    // This command used 128bits, and not 64 bits. This means that we have to look one 
    // Command ahead in the buffer, and update the PC.
    uint32_t dwPC = gDlistStack[gDlistStackPointer].pc;       // This points to the next instruction
    uint32_t dwCmd2 = *(uint32_t *)(rdram_u8 + dwPC+4);
    uint32_t dwCmd3 = *(uint32_t *)(rdram_u8 + dwPC+4+8);
    uint32_t dwHalf1 = *(uint32_t *)(rdram_u8 + dwPC);
    uint32_t dwHalf2 = *(uint32_t *)(rdram_u8 + dwPC+8);

    if( options.enableHackForGames == HACK_FOR_ALL_STAR_BASEBALL || options.enableHackForGames == HACK_FOR_MLB )
    {
        if( ((dwHalf1>>24) == 0xb4 || (dwHalf1>>24) == 0xb3 || (dwHalf1>>24) == 0xb2 || (dwHalf1>>24) == 0xe1) && 
            ((dwHalf2>>24) == 0xb4 || (dwHalf2>>24) == 0xb3 || (dwHalf2>>24) == 0xb2 || (dwHalf2>>24) == 0xf1) )
        {
            // Increment PC so that it points to the right place
            gDlistStack[gDlistStackPointer].pc += 16;
        }
        else
        {
            // Hack for some games, All_Star_Baseball_2000
            gDlistStack[gDlistStackPointer].pc += 8;
            dwCmd3 = dwCmd2;
            //dwCmd2 = dwHalf1;
            //dwCmd2 = 0;

            // fix me here
            dwCmd2 = (((dwHalf1>>12)&0x03FF)<<17) | (((dwHalf1)&0x03FF)<<1);
        }   
    }
    else
    {
        gDlistStack[gDlistStackPointer].pc += 16;
    }


    // Hack for Mario Tennis
    if( !status.bHandleN64RenderTexture && g_CI.dwAddr == g_ZI.dwAddr )
    {
        return;
    }


    LOG_UCODE("0x%08x: %08x %08x", dwPC, *(uint32_t *)(rdram_u8 + dwPC+0), *(uint32_t *)(rdram_u8 + dwPC+4));
    LOG_UCODE("0x%08x: %08x %08x", dwPC+8, *(uint32_t *)(rdram_u8 + dwPC+8), *(uint32_t *)(rdram_u8 + dwPC+8+4));

    uint32_t dwXH     = (((gfx->words.w0)>>12)&0x0FFF)/4;
    uint32_t dwYH     = (((gfx->words.w0)    )&0x0FFF)/4;
    uint32_t tileno   = ((gfx->words.w1)>>24)&0x07;
    uint32_t dwXL     = (((gfx->words.w1)>>12)&0x0FFF)/4;
    uint32_t dwYL     = (((gfx->words.w1)    )&0x0FFF)/4;
    uint16_t uS       = (uint16_t)(  dwCmd2>>16)&0xFFFF;
    uint16_t uT       = (uint16_t)(  dwCmd2    )&0xFFFF;
    uint16_t  uDSDX   = (uint16_t)((  dwCmd3>>16)&0xFFFF);
    uint16_t  uDTDY       = (uint16_t)((  dwCmd3    )&0xFFFF);
    

    if( (int)dwXL >= gRDP.scissor.right || (int)dwYL >= gRDP.scissor.bottom || (int)dwXH < gRDP.scissor.left || (int)dwYH < gRDP.scissor.top )
    {
        // Clipping
        return;
    }

    short s16S = *(short*)(&uS);
    short s16T = *(short*)(&uT);

    short    s16DSDX  = *(short*)(&uDSDX);
    short  s16DTDY  = *(short*)(&uDTDY);

    uint32_t curTile = gRSP.curTile;
    ForceMainTextureIndex(tileno);

    float fS0 = s16S / 32.0f;
    float fT0 = s16T / 32.0f;

    float fDSDX = s16DSDX / 1024.0f;
    float fDTDY = s16DTDY / 1024.0f;

    uint32_t cycletype = gRDP.otherMode.cycle_type;

    if (cycletype == CYCLE_TYPE_COPY)
    {
        fDSDX /= 4.0f;  // In copy mode 4 pixels are copied at once.
        dwXH++;
        dwYH++;
    }
    else if (cycletype == CYCLE_TYPE_FILL)
    {
        dwXH++;
        dwYH++;
    }

    if( fDSDX == 0 )    fDSDX = 1;
    if( fDTDY == 0 )    fDTDY = 1;

    float fS1 = fS0 + (fDSDX * (dwXH - dwXL));
    float fT1 = fT0 + (fDTDY * (dwYH - dwYL));

    LOG_UCODE("    Tile:%d Screen(%d,%d) -> (%d,%d)", tileno, dwXL, dwYL, dwXH, dwYH);
    LOG_UCODE("           Tex:(%#5f,%#5f) -> (%#5f,%#5f) (DSDX:%#5f DTDY:%#5f)",
                                            fS0, fT0, fS1, fT1, fDSDX, fDTDY);
    LOG_UCODE("");

    float t0u0 = (fS0-gRDP.tiles[tileno].hilite_sl) * gRDP.tiles[tileno].fShiftScaleS;
    float t0v0 = (fT0-gRDP.tiles[tileno].hilite_tl) * gRDP.tiles[tileno].fShiftScaleT;
    float t0u1 = t0u0 + (fDSDX * (dwXH - dwXL))*gRDP.tiles[tileno].fShiftScaleS;
    float t0v1 = t0v0 + (fDTDY * (dwYH - dwYL))*gRDP.tiles[tileno].fShiftScaleT;

    if( dwXL==0 && dwYL==0 && dwXH==windowSetting.fViWidth-1 && dwYH==windowSetting.fViHeight-1 &&
        t0u0 == 0 && t0v0==0 && t0u1==0 && t0v1==0 )
    {
        //Using TextRect to clear the screen
    }
    else
    {
        if( status.bHandleN64RenderTexture && //status.bDirectWriteIntoRDRAM && 
            g_pRenderTextureInfo->CI_Info.dwFormat == gRDP.tiles[tileno].dwFormat && 
            g_pRenderTextureInfo->CI_Info.dwSize == gRDP.tiles[tileno].dwSize && 
            gRDP.tiles[tileno].dwFormat == TXT_FMT_CI && gRDP.tiles[tileno].dwSize == TXT_SIZE_8b )
        {
            if( options.enableHackForGames == HACK_FOR_YOSHI )
            {
                // Hack for Yoshi background image
                PrepareTextures();
                TexRectToFrameBuffer_8b(dwXL, dwYL, dwXH, dwYH, t0u0, t0v0, t0u1, t0v1, tileno);
                DEBUGGER_PAUSE_AT_COND_AND_DUMP_COUNT_N((eventToPause == NEXT_FLUSH_TRI || eventToPause == NEXT_TEXTRECT), {
                    DebuggerAppendMsg("TexRect: tile=%d, X0=%d, Y0=%d, X1=%d, Y1=%d,\nfS0=%f, fT0=%f, ScaleS=%f, ScaleT=%f\n",
                        gRSP.curTile, dwXL, dwYL, dwXH, dwYH, fS0, fT0, fDSDX, fDTDY);
                    DebuggerAppendMsg("Pause after TexRect for Yoshi\n");
                });

            }
            else
            {
                if( frameBufferOptions.bUpdateCIInfo )
                {
                    PrepareTextures();
                    TexRectToFrameBuffer_8b(dwXL, dwYL, dwXH, dwYH, t0u0, t0v0, t0u1, t0v1, tileno);
                }

                if( !status.bDirectWriteIntoRDRAM )
                {
                    CRender::g_pRender->TexRect(dwXL, dwYL, dwXH, dwYH, fS0, fT0, fDSDX, fDTDY, false, 0xFFFFFFFF);

                    status.dwNumTrisRendered += 2;
                }
            }
        }
        else
        {
            CRender::g_pRender->TexRect(dwXL, dwYL, dwXH, dwYH, fS0, fT0, fDSDX, fDTDY, false, 0xFFFFFFFF);
            status.bFrameBufferDrawnByTriangles = true;

            status.dwNumTrisRendered += 2;
        }
    }

    if( status.bHandleN64RenderTexture )    g_pRenderTextureInfo->maxUsedHeight = max(g_pRenderTextureInfo->maxUsedHeight,(int)dwYH);

    ForceMainTextureIndex(curTile);
}


void DLParser_TexRectFlip(Gfx *gfx)
{ 
   uint8_t *rdram_u8 = (uint8_t*)gfx_info.RDRAM;
    status.bCIBufferIsRendered = true;
    status.primitiveType = PRIM_TEXTRECTFLIP;

    // This command used 128bits, and not 64 bits. This means that we have to look one 
    // Command ahead in the buffer, and update the PC.
    uint32_t dwPC = gDlistStack[gDlistStackPointer].pc;       // This points to the next instruction
    uint32_t dwCmd2 = *(uint32_t *)(rdram_u8 + dwPC+4);
    uint32_t dwCmd3 = *(uint32_t *)(rdram_u8 + dwPC+4+8);

    // Increment PC so that it points to the right place
    gDlistStack[gDlistStackPointer].pc += 16;

    uint32_t dwXH     = (((gfx->words.w0)>>12)&0x0FFF)/4;
    uint32_t dwYH     = (((gfx->words.w0)    )&0x0FFF)/4;
    uint32_t tileno   = ((gfx->words.w1)>>24)&0x07;
    uint32_t dwXL     = (((gfx->words.w1)>>12)&0x0FFF)/4;
    uint32_t dwYL     = (((gfx->words.w1)    )&0x0FFF)/4;
    uint32_t dwS      = (  dwCmd2>>16)&0xFFFF;
    uint32_t dwT      = (  dwCmd2    )&0xFFFF;
    int  nDSDX     = (int)(short)((  dwCmd3>>16)&0xFFFF);
    int  nDTDY     = (int)(short)((  dwCmd3    )&0xFFFF);

    uint32_t curTile = gRSP.curTile;
    ForceMainTextureIndex(tileno);
    
    float fS0 = (float)dwS / 32.0f;
    float fT0 = (float)dwT / 32.0f;

    float fDSDX = (float)nDSDX / 1024.0f;
    float fDTDY = (float)nDTDY / 1024.0f;

    uint32_t cycletype = gRDP.otherMode.cycle_type;

    if (cycletype == CYCLE_TYPE_COPY)
    {
        fDSDX /= 4.0f;  // In copy mode 4 pixels are copied at once.
        dwXH++;
        dwYH++;
    }
    else if (cycletype == CYCLE_TYPE_FILL)
    {
        dwXH++;
        dwYH++;
    }

    float fS1 = fS0 + (fDSDX * (dwYH - dwYL));
    float fT1 = fT0 + (fDTDY * (dwXH - dwXL));
    
    LOG_UCODE("    Tile:%d (%d,%d) -> (%d,%d)",
        tileno, dwXL, dwYL, dwXH, dwYH);
    LOG_UCODE("    Tex:(%#5f,%#5f) -> (%#5f,%#5f) (DSDX:%#5f DTDY:%#5f)",
        fS0, fT0, fS1, fT1, fDSDX, fDTDY);
    LOG_UCODE("");

    float t0u0 = (fS0) * gRDP.tiles[tileno].fShiftScaleS-gRDP.tiles[tileno].sl;
    float t0v0 = (fT0) * gRDP.tiles[tileno].fShiftScaleT-gRDP.tiles[tileno].tl;
    float t0u1 = t0u0 + (fDSDX * (dwYH - dwYL))*gRDP.tiles[tileno].fShiftScaleS;
    float t0v1 = t0v0 + (fDTDY * (dwXH - dwXL))*gRDP.tiles[tileno].fShiftScaleT;

    CRender::g_pRender->TexRectFlip(dwXL, dwYL, dwXH, dwYH, t0u0, t0v0, t0u1, t0v1);
    status.dwNumTrisRendered += 2;

    if( status.bHandleN64RenderTexture )    g_pRenderTextureInfo->maxUsedHeight = max(g_pRenderTextureInfo->maxUsedHeight,int(dwYL+(dwXH-dwXL)));

    ForceMainTextureIndex(curTile);
}

/************************************************************************/
/*                                                                      */
/************************************************************************/
/*
 *  TMEM emulation
 *  There are 0x200's 64bits entry in TMEM
 *  Usually, textures are loaded into TMEM at 0x0, and TLUT is loaded at 0x100
 *  of course, the whole TMEM can be used by textures if TLUT is not used, and TLUT
 *  can be at other address of TMEM.
 *
 *  We don't want to emulate TMEM by creating a block of memory for TMEM and load
 *  everything into the block of memory, this will be slow.
 */
typedef struct TmemInfoEntry{
    uint32_t start;
    uint32_t length;
    uint32_t rdramAddr;
    TmemInfoEntry* next;
} TmemInfoEntry;

const int tmenMaxEntry=20;
TmemInfoEntry tmenEntryBuffer[20]={{0}};
TmemInfoEntry *g_pTMEMInfo=NULL;
TmemInfoEntry *g_pTMEMFreeList=tmenEntryBuffer;

void TMEM_Init()
{
    g_pTMEMInfo=NULL;
    g_pTMEMFreeList=tmenEntryBuffer;

    int i;
    for( i=0; (i < (tmenMaxEntry-1)); i++ )
    {
        tmenEntryBuffer[i].start=0;
        tmenEntryBuffer[i].length=0;
        tmenEntryBuffer[i].rdramAddr=0;
        tmenEntryBuffer[i].next = &(tmenEntryBuffer[i+1]);
    }
    tmenEntryBuffer[i-1].next = NULL;
}

void TMEM_SetBlock(uint32_t tmemstart, uint32_t length, uint32_t rdramaddr)
{
    TmemInfoEntry *p=g_pTMEMInfo;

    if( p == NULL )
    {
        // Move an entry from freelist and link it to the header
        p = g_pTMEMFreeList;
        g_pTMEMFreeList = g_pTMEMFreeList->next;

        p->start = tmemstart;
        p->length = length;
        p->rdramAddr = rdramaddr;
        p->next = NULL;
    }
    else
    {
        while ( tmemstart > (p->start+p->length) )
        {
            if( p->next != NULL ) {
                p = p->next;
                continue;
            }
            else {
                break;
            }
        }

        if ( p->start == tmemstart ) 
        {
            // need to replace the block of 'p'
            // or append a new block depend the block lengths
            if( length == p->length )
            {
                p->rdramAddr = rdramaddr;
                return;
            }
            else if( length < p->length )
            {
                TmemInfoEntry *newentry = g_pTMEMFreeList;
                g_pTMEMFreeList = g_pTMEMFreeList->next;

                newentry->length = p->length - length;
                newentry->next = p->next;
                newentry->rdramAddr = p->rdramAddr + p->length;
                newentry->start = p->start + p->length;

                p->length = length;
                p->next = newentry;
                p->rdramAddr = rdramaddr;
            }
        }
        else if( p->start > tmemstart )
        {
            // p->start > tmemstart, need to insert the new block before 'p'
            TmemInfoEntry *newentry = g_pTMEMFreeList;
            g_pTMEMFreeList = g_pTMEMFreeList->next;

            if( length+tmemstart < p->start+p->length )
            {
                newentry->length = p->length - length;
                newentry->next = p->next;
                newentry->rdramAddr = p->rdramAddr + p->length;
                newentry->start = p->start + p->length;

                p->length = length;
                p->next = newentry;
                p->rdramAddr = rdramaddr;
                p->start = tmemstart;
            }
            else if( length+tmemstart == p->start+p->length )
            {
                // TODO: Implement
            }
        }
        else
        {
            // TODO: Implement
        }
    }
}

uint32_t TMEM_GetRdramAddr(uint32_t tmemstart, uint32_t length)
{
    return 0;
}


/*
 *  New implementation of texture loading
 */

bool IsTmemFlagValid(uint32_t tmemAddr)
{
    uint32_t index = tmemAddr>>5;
    uint32_t bitIndex = (tmemAddr&0x1F);
    return ((g_TmemFlag[index] & (1<<bitIndex))!=0);
}

uint32_t GetValidTmemInfoIndex(uint32_t tmemAddr)
{
    uint32_t index = tmemAddr>>5;
    uint32_t bitIndex = (tmemAddr&0x1F);

    if ((g_TmemFlag[index] & (1<<bitIndex))!=0 )    //This address is valid
    {
        return tmemAddr;
    }
    else
    {
        for( uint32_t x=index+1; x != 0; x-- )
        {
            uint32_t i = x - 1;
            if( g_TmemFlag[i] != 0 )
            {
                for( uint32_t y=0x20; y != 0; y-- )
                {
                    uint32_t j = y - 1;
                    if( (g_TmemFlag[i] & (1<<j)) != 0 )
                    {
                        return ((i<<5)+j);
                    }
                }
            }
        }
        TRACE0("Error, check me");
        return 0;
    }
}


void SetTmemFlag(uint32_t tmemAddr, uint32_t size)
{
    uint32_t index = tmemAddr>>5;
    uint32_t bitIndex = (tmemAddr&0x1F);

#ifdef DEBUGGER
    if( size > 0x200 )
    {
        DebuggerAppendMsg("Check me: tmemaddr=%X, size=%x", tmemAddr, size);
        size = 0x200-tmemAddr;
    }
#endif

    if( bitIndex == 0 )
    {
        uint32_t i;
        for( i=0; i< (size>>5); i++ )
        {
            g_TmemFlag[index+i] = 0;
        }

        if( (size&0x1F) != 0 )
        {
            //ErrorMsg("Check me: tmemaddr=%X, size=%x", tmemAddr, size);
            g_TmemFlag[index+i] &= ~((1<<(size&0x1F))-1);
        }

        g_TmemFlag[index] |= 1;
    }
    else
    {
        if( bitIndex + size <= 0x1F )
        {
            uint32_t val = g_TmemFlag[index];
            uint32_t mask = (1<<(bitIndex))-1;
            mask |= ~((1<<(bitIndex + size))-1);
            val &= mask;
            val |= (1<<bitIndex);
            g_TmemFlag[index] = val;
        }
        else
        {
            //ErrorMsg("Check me: tmemaddr=%X, size=%x", tmemAddr, size);
            uint32_t val = g_TmemFlag[index];
            uint32_t mask = (1<<bitIndex)-1;
            val &= mask;
            val |= (1<<bitIndex);
            g_TmemFlag[index] = val;
            index++;
            size -= (0x20-bitIndex);

            uint32_t i;
            for( i=0; i< (size>>5); i++ )
            {
                g_TmemFlag[index+i] = 0;
            }

            if( (size&0x1F) != 0 )
            {
                //ErrorMsg("Check me: tmemaddr=%X, size=%x", tmemAddr, size);
                g_TmemFlag[index+i] &= ~((1<<(size&0x1F))-1);
            }
        }
    }
}

#undef min
#undef max

#endif
