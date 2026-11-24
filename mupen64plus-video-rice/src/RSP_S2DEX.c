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
/* The S2DEX microcode (Yoshi's Story uses it). C89. */
#include <stdint.h>
#include <string.h>

#include "Config.h"
#include "Debugger.h"
#include "RenderBase.h"
#include "RSP_S2DEX.h"
#include "Timing.h"
#include "Video.h"

#include "../../Graphics/GBI.h"
#include "../../Graphics/RSP/gSP_state.h"
#include "../../Graphics/RSP/RSP_state.h"

uObjTxtr *gObjTxtr = NULL;
static uObjTxtrTLUT *gObjTlut = NULL;
static uint32_t gObjTlutAddr = 0;
static uObjMtx *gObjMtx = NULL;
static uObjSubMtx *gSubObjMtx = NULL;
uObjMtxReal gObjMtxReal = {1, 0, 0, 1, 0, 0, 0, 0};

uint32_t g_TxtLoadBy = CMD_LOAD_OBJ_TXTR;

/* Yoshi's Story uses this - 0x02 */
void RSP_S2DEX_BG_COPY(Gfx *gfx)
{
    uint8_t *rdram_u8 = (uint8_t*)gfx_info.RDRAM;
    uint32_t dwAddr;

    SP_Timing(DP_Minimal16);
    DP_Timing(DP_Minimal16);

    dwAddr = RSPSegmentAddr((gfx->words.w1));
    if (dwAddr + sizeof(uObjBg) > g_dwRamSize)
        return;
    RiceRender_ObjBGCopy((uObjBg*)(rdram_u8 + dwAddr));
}

/* Yoshi's Story uses this - 0x03 */
void RSP_S2DEX_OBJ_RECTANGLE(Gfx *gfx)
{
    uint8_t *rdram_u8 = (uint8_t*)gfx_info.RDRAM;
    uint32_t dwAddr = RSPSegmentAddr((gfx->words.w1));
    uObjTxSprite objtx;

    if (dwAddr + sizeof(uObjSprite) > g_dwRamSize)
        return;
    memcpy(&objtx.sprite, rdram_u8 + dwAddr, sizeof(uObjSprite));

    if( g_TxtLoadBy == CMD_LOAD_OBJ_TXTR )
    {
        memcpy(&(objtx.txtr.block),&(gObjTxtr->block),sizeof(uObjTxtr));
        RiceRender_LoadObjSprite(&objtx, true);
    }
    else
    {
        PrepareTextures();
    }
    RiceRender_DrawSprite(&objtx, false);
}

/* Yoshi's Story uses this - 0x04 */
void RSP_S2DEX_OBJ_SPRITE(Gfx *gfx)
{
    uint8_t *rdram_u8 = (uint8_t*)gfx_info.RDRAM;
    uint32_t dwAddr = RSPSegmentAddr((gfx->words.w1));
    uint32_t dwTile = gRSP.curTile;
    uObjTxSprite drawinfo;

    if (dwAddr + sizeof(uObjSprite) > g_dwRamSize)
        return;
    status.bAllowLoadFromTMEM = false;  /* the TLUT loaded by the ObjTlut command is wanted */
    PrepareTextures();
    status.bAllowLoadFromTMEM = true;

    memcpy( &(drawinfo.sprite), rdram_u8 + dwAddr, sizeof(uObjSprite));
    RiceRender_DrawSpriteR(&drawinfo, false, dwTile, 0, 0, drawinfo.sprite.imageW/32, drawinfo.sprite.imageH/32);
}

/* Yoshi's Story uses this - 0xb0 */
void RSP_S2DEX_SELECT_DL(Gfx *gfx)
{
    (void)gfx;
    RSP_RDP_NOIMPL("RDP: RSP_S2DEX_SELECT_DL (0x%08x 0x%08x)", (gfx->words.w0), (gfx->words.w1));
}

void RSP_S2DEX_OBJ_RENDERMODE(Gfx *gfx)
{
    (void)gfx;
}

/* Yoshi's Story uses this - 0xb1 */
void RSP_S2DEX_OBJ_RENDERMODE_2(Gfx *gfx)
{
    if( ((gfx->words.w0)&0xFFFFFF) != 0 || ((gfx->words.w1)&0xFFFFFF00) != 0 )
    {
        /* This is a TRI2 cmd */
        RSP_GBI1_Tri2(gfx);
        return;
    }

    RSP_S2DEX_OBJ_RENDERMODE(gfx);
}

void RSP_S2DEX_SPObjLoadTxtr(Gfx *gfx)
{
    uint8_t *rdram_u8 = (uint8_t*)gfx_info.RDRAM;
    gObjTxtr = (uObjTxtr*)(rdram_u8 + (RSPSegmentAddr((gfx->words.w1))&(g_dwRamSize-1)));
    if( gObjTxtr->block.type == S2DEX_OBJLT_TLUT )
    {
        int size, offset, i;
        uint32_t addr;

        gObjTlut = (uObjTxtrTLUT*)gObjTxtr;
        gObjTlutAddr = (uint32_t)(RSPSegmentAddr(gObjTlut->image));

        /* Copy tlut */
        size = gObjTlut->pnum+1;
        offset = gObjTlut->phead-0x100;

        if( offset+size>0x100)
            size = 0x100 - offset;

        addr = gObjTlutAddr;

        for( i=offset; i<offset+size; i++ )
        {
            g_wRDPTlut[i^1] = RDRAM_UHALF(addr);
            addr += 2;
        }
    }
    else
    {
        /* Loading ObjSprite */
        g_TxtLoadBy = CMD_LOAD_OBJ_TXTR;
    }
}

/* Yoshi's Story uses this - 0xc2 */
void RSP_S2DEX_SPObjLoadTxSprite(Gfx *gfx)
{
    uint8_t *rdram_u8 = (uint8_t*)gfx_info.RDRAM;
    uObjTxSprite* ptr = (uObjTxSprite*)(rdram_u8 + (RSPSegmentAddr((gfx->words.w1))&(g_dwRamSize-1)));
    gObjTxtr = (uObjTxtr*)ptr;

    /* Now draw the sprite */
    RiceRender_LoadObjSprite(ptr, false);
    RiceRender_DrawSpriteR(ptr, true, 0, 0, 0, 0, 0);
}

/* Yoshi's Story uses this - 0xc3 */
void RSP_S2DEX_SPObjLoadTxRect(Gfx *gfx)
{
    uint8_t *rdram_u8 = (uint8_t*)gfx_info.RDRAM;
    uObjTxSprite* ptr = (uObjTxSprite*)(rdram_u8 + (RSPSegmentAddr((gfx->words.w1))&(g_dwRamSize-1)));
    gObjTxtr = (uObjTxtr*)ptr;

    /* Now draw the sprite */
    RiceRender_LoadObjSprite(ptr, false);
    RiceRender_DrawSprite(ptr, false);
}

/* Yoshi's Story uses this - 0xc4 */
void RSP_S2DEX_SPObjLoadTxRectR(Gfx *gfx)
{
    uint8_t *rdram_u8 = (uint8_t*)gfx_info.RDRAM;
    uObjTxSprite* ptr = (uObjTxSprite*)(rdram_u8 + (RSPSegmentAddr((gfx->words.w1))&(g_dwRamSize-1)));
    gObjTxtr = (uObjTxtr*)ptr;

    /* Now draw the sprite */
    RiceRender_LoadObjSprite(ptr, false);
    RiceRender_DrawSprite(ptr, true);
}

/* Yoshi's Story uses this - 0xe4 */
void RSP_S2DEX_RDPHALF_0(Gfx *gfx)
{
    /* RDP: RSP_S2DEX_RDPHALF_0 (0xe449c0a8 0x003b40a4)
     * 0x001d3c88: e449c0a8 003b40a4 RDP_TEXRECT
     * 0x001d3c90: b4000000 00000000 RSP_RDPHALF_1
     * 0x001d3c98: b3000000 04000400 RSP_RDPHALF_2 */
    uint8_t *rdram_u8    = (uint8_t*)gfx_info.RDRAM;
    uint32_t dwPC        = __RSP.PC[__RSP.PCi];       /* the next instruction */
    uint32_t dwNextUcode = *(uint32_t *)(rdram_u8 + dwPC);

    /* Pokemon Puzzle League */
    if( (dwNextUcode>>24) != S2DEX_SELECT_DL && (dwNextUcode>>24) == 0xB4 )
        DLParser_TexRect(gfx);
}

/* Yoshi's Story uses this - 0x05 */
void RSP_S2DEX_OBJ_MOVEMEM(Gfx *gfx)
{
    uint32_t dwCommand = ((gfx->words.w0)>>16)&0xFF;
    uint32_t dwLength  = ((gfx->words.w0))    &0xFFFF;
    uint32_t dwAddr    = RSPSegmentAddr((gfx->words.w1));
    uint8_t *rdram_u8  = (uint8_t*)gfx_info.RDRAM;

    if( dwLength == 0 && dwCommand == 23 )
    {
        gObjMtx = (uObjMtx *)(rdram_u8 + dwAddr);
        gObjMtxReal.A = gObjMtx->A/65536.0f;
        gObjMtxReal.B = gObjMtx->B/65536.0f;
        gObjMtxReal.C = gObjMtx->C/65536.0f;
        gObjMtxReal.D = gObjMtx->D/65536.0f;
        gObjMtxReal.X = (float)(gObjMtx->X>>2);
        gObjMtxReal.Y = (float)(gObjMtx->Y>>2);
        gObjMtxReal.BaseScaleX = gObjMtx->BaseScaleX/1024.0f;
        gObjMtxReal.BaseScaleY = gObjMtx->BaseScaleY/1024.0f;
    }
    else if( dwLength == 2 && dwCommand == 7 )
    {
        gSubObjMtx = (uObjSubMtx*)(rdram_u8 + dwAddr);
        gObjMtxReal.X = (float)(gSubObjMtx->X>>2);
        gObjMtxReal.Y = (float)(gSubObjMtx->Y>>2);
        gObjMtxReal.BaseScaleX = gSubObjMtx->BaseScaleX/1024.0f;
        gObjMtxReal.BaseScaleY = gSubObjMtx->BaseScaleY/1024.0f;
    }
}

/* Yoshi's Story uses this - 0x01 */
void RSP_S2DEX_BG_1CYC(Gfx *gfx)
{
    uint8_t *rdram_u8 = (uint8_t*)gfx_info.RDRAM;
    uint32_t dwAddr;

    SP_Timing(DP_Minimal16);
    DP_Timing(DP_Minimal16);

    dwAddr = RSPSegmentAddr((gfx->words.w1));
    if (dwAddr + sizeof(uObjScaleBg) > g_dwRamSize)
        return;
    RiceRender_ObjBG1CYC((uObjScaleBg *)(rdram_u8 + dwAddr));
}

void RSP_S2DEX_BG_1CYC_2(Gfx *gfx)
{
    if( ((gfx->words.w0)&0x00FFFFFF) != 0 )
    {
        RSP_GBI0_Mtx(gfx);
        return;
    }

    RSP_S2DEX_BG_1CYC(gfx);
}

/* Yoshi's Story uses this - 0xb2 */
void RSP_S2DEX_OBJ_RECTANGLE_R(Gfx *gfx)
{
    uint8_t *rdram_u8 = (uint8_t*)gfx_info.RDRAM;
    uint32_t dwAddr = RSPSegmentAddr((gfx->words.w1));
    uObjTxSprite objtx;

    if (dwAddr + sizeof(uObjSprite) > g_dwRamSize)
        return;
    memcpy(&objtx.sprite, rdram_u8 + dwAddr, sizeof(uObjSprite));

    /* Now draw the sprite */
    if( g_TxtLoadBy == CMD_LOAD_OBJ_TXTR )
    {
        memcpy(&(objtx.txtr.block), &(gObjTxtr->block), sizeof(uObjTxtr));
        RiceRender_LoadObjSprite(&objtx, true);
    }
    else
    {
        PrepareTextures();
    }
    RiceRender_DrawSprite(&objtx, true);
}
