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

#include "UcodeDefs.h"

#ifndef _UCODE_H_
#define _UCODE_H_

//typedef void (*RDPInstruction)(Gfx *gfx);
typedef void (*RDPInstruction)(Gfx*);
extern RDPInstruction   *currentUcodeMap;

typedef RDPInstruction UcodeMap[256] ;


//#define UcodeFunc(name)   void name(uint32_t, uint32_t)
#define UcodeFunc(name) void name(Gfx*)

UcodeFunc(RSP_RDP_Nothing);

UcodeFunc(RSP_GBI0_Mtx);
UcodeFunc(RSP_Mtx_DKR);
UcodeFunc(RSP_GBI0_DL);
UcodeFunc(RSP_DL_In_MEM_DKR);

UcodeFunc(RSP_GBI0_Vtx);
UcodeFunc(RSP_Vtx_DKR);
UcodeFunc(RSP_Vtx_WRUS);
UcodeFunc(RSP_Vtx_ShadowOfEmpire);

UcodeFunc(RSP_GBI0_Tri4);
UcodeFunc(RSP_DMA_Tri_DKR);
UcodeFunc(DLParser_Set_Addr_Ucode6);
UcodeFunc(RSP_MoveWord_DKR);

UcodeFunc(RSP_Vtx_PD);
UcodeFunc(RSP_Set_Vtx_CI_PD);
UcodeFunc(RSP_Tri4_PD);

UcodeFunc(RSP_GBI0_Sprite2DBase);
UcodeFunc(RSP_GBI0_Sprite2DDraw);
UcodeFunc(RSP_GBI1_Sprite2DBase);
UcodeFunc(RSP_GBI1_Sprite2DScaleFlip);
UcodeFunc(RSP_GBI1_Sprite2DDraw);
UcodeFunc(RSP_GBI_Sprite2DBase);
UcodeFunc(RSP_GBI_Sprite2D_PuzzleMaster64);

UcodeFunc(RSP_GBI1_SpNoop);
UcodeFunc(RSP_GBI1_Reserved);
UcodeFunc(RSP_GBI1_Vtx);
UcodeFunc(RSP_GBI1_MoveMem);
UcodeFunc(RSP_GBI1_RDPHalf_Cont);
UcodeFunc(RSP_GBI1_RDPHalf_2);
UcodeFunc(RSP_GBI1_RDPHalf_1);
UcodeFunc(RSP_GBI1_Line3D);
UcodeFunc(RSP_GBI1_ClearGeometryMode);
UcodeFunc(RSP_GBI1_SetGeometryMode);
UcodeFunc(RSP_GBI1_EndDL);
UcodeFunc(RSP_GBI1_SetOtherModeL);
UcodeFunc(RSP_GBI1_SetOtherModeH);
UcodeFunc(RSP_GBI1_Texture);
UcodeFunc(RSP_GBI1_MoveWord);
UcodeFunc(RSP_GBI1_PopMtx);
UcodeFunc(RSP_GBI1_CullDL);
UcodeFunc(RSP_GBI1_Tri1);
UcodeFunc(RSP_GBI1_Tri2);
UcodeFunc(RSP_GBI1_Noop);
UcodeFunc(RSP_GBI1_ModifyVtx);
UcodeFunc(RSP_GBI1_BranchZ);
UcodeFunc(RSP_GBI1_LoadUCode);

UcodeFunc(DLParser_TexRect);
UcodeFunc(DLParser_TexRectFlip);
UcodeFunc(DLParser_RDPLoadSync);
UcodeFunc(DLParser_RDPPipeSync);
UcodeFunc(DLParser_RDPTileSync);
UcodeFunc(DLParser_RDPFullSync);
UcodeFunc(DLParser_SetKeyGB);
UcodeFunc(DLParser_SetKeyR);
UcodeFunc(DLParser_SetConvert);
UcodeFunc(DLParser_SetScissor);
UcodeFunc(DLParser_SetPrimDepth);
UcodeFunc(DLParser_RDPSetOtherMode);
UcodeFunc(DLParser_LoadTLut);
UcodeFunc(DLParser_SetTileSize);
UcodeFunc(DLParser_LoadBlock);
UcodeFunc(DLParser_LoadTile);
UcodeFunc(DLParser_SetTile);
UcodeFunc(DLParser_FillRect);
UcodeFunc(DLParser_SetFillColor);
UcodeFunc(DLParser_SetFogColor);
UcodeFunc(DLParser_SetBlendColor);
UcodeFunc(DLParser_SetPrimColor);
UcodeFunc(DLParser_SetEnvColor);
UcodeFunc(DLParser_SetCombine);
UcodeFunc(DLParser_SetTImg);
UcodeFunc(DLParser_SetZImg);
UcodeFunc(DLParser_SetCImg);

UcodeFunc(RSP_GBI2_DL);
UcodeFunc(RSP_GBI2_CullDL);
UcodeFunc(RSP_GBI2_EndDL);
UcodeFunc(RSP_GBI2_MoveWord);
UcodeFunc(RSP_GBI2_Texture);
UcodeFunc(RSP_GBI2_GeometryMode);
UcodeFunc(RSP_GBI2_SetOtherModeL);
UcodeFunc(RSP_GBI2_SetOtherModeH);
UcodeFunc(RSP_GBI2_MoveMem);
UcodeFunc(RSP_GBI2_Mtx);
UcodeFunc(RSP_GBI2_PopMtx);
UcodeFunc(RSP_GBI2_Vtx);
UcodeFunc(RSP_GBI2_Tri1);
UcodeFunc(RSP_GBI2_Tri2);
UcodeFunc(RSP_GBI2_Line3D);

UcodeFunc(RSP_GBI2_DL_Count);
UcodeFunc(RSP_GBI2_SubModule);
UcodeFunc(RSP_GBI2_0x8);
UcodeFunc(DLParser_Bomberman2TextRect);

UcodeFunc(RSP_S2DEX_BG_1CYC_2);
UcodeFunc(RSP_S2DEX_OBJ_RENDERMODE_2);

UcodeFunc(RSP_S2DEX_SPObjLoadTxtr_Ucode1);

UcodeFunc( RSP_S2DEX_BG_1CYC);
UcodeFunc( RSP_S2DEX_BG_COPY);
UcodeFunc( RSP_S2DEX_OBJ_RECTANGLE);
UcodeFunc( RSP_S2DEX_OBJ_SPRITE);
UcodeFunc( RSP_S2DEX_OBJ_MOVEMEM);
UcodeFunc( RSP_S2DEX_SELECT_DL);
UcodeFunc( RSP_S2DEX_OBJ_RENDERMODE);
UcodeFunc( RSP_S2DEX_OBJ_RECTANGLE_R);
UcodeFunc( RSP_S2DEX_SPObjLoadTxtr);
UcodeFunc( RSP_S2DEX_SPObjLoadTxSprite);
UcodeFunc( RSP_S2DEX_SPObjLoadTxRect);
UcodeFunc( RSP_S2DEX_SPObjLoadTxRectR);
UcodeFunc( RSP_S2DEX_RDPHALF_0);
UcodeFunc( RSP_S2DEX_Yoshi_Unknown);

UcodeFunc( RSP_RDP_InsertMatrix );
UcodeFunc( RSP_S2DEX_SPObjLoadTxtr );



UcodeFunc(RDP_TriFill);
UcodeFunc(RDP_TriFillZ);
UcodeFunc(RDP_TriTxtr);
UcodeFunc(RDP_TriTxtrZ);
UcodeFunc(RDP_TriShade);
UcodeFunc(RDP_TriShadeZ);
UcodeFunc(RDP_TriShadeTxtr);
UcodeFunc(RDP_TriShadeTxtrZ);



typedef RDPInstruction UcodeMap[256] ;

// Ucode: F3DEX, for most games
UcodeMap ucodeMap1 =
{
    RSP_GBI1_SpNoop,     RSP_GBI0_Mtx,     RSP_GBI1_Reserved, RSP_GBI1_MoveMem,
    RSP_GBI1_Vtx, RSP_GBI1_Reserved, RSP_GBI0_DL,   RSP_GBI1_Reserved,
    RSP_GBI1_Reserved, RSP_GBI1_Sprite2DBase, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
//10
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
//20
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
//30
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
//40
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
//50
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
//60
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
//70
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,

//80
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
//90
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
//a0
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_GBI1_LoadUCode,
//b0
    RSP_GBI1_BranchZ, RSP_GBI1_Tri2, RSP_GBI1_ModifyVtx, RSP_GBI1_RDPHalf_2,
    RSP_GBI1_RDPHalf_1, RSP_GBI1_Line3D, RSP_GBI1_ClearGeometryMode, RSP_GBI1_SetGeometryMode,
    RSP_GBI1_EndDL, RSP_GBI1_SetOtherModeL, RSP_GBI1_SetOtherModeH, RSP_GBI1_Texture,
    RSP_GBI1_MoveWord, RSP_GBI1_PopMtx, RSP_GBI1_CullDL, RSP_GBI1_Tri1,

//c0
    RSP_GBI1_Noop,    RSP_S2DEX_SPObjLoadTxtr_Ucode1, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RDP_TriFill, RDP_TriFillZ, RDP_TriTxtr, RDP_TriTxtrZ,
    RDP_TriShade, RDP_TriShadeZ, RDP_TriShadeTxtr, RDP_TriShadeTxtrZ,
//d0
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
//e0
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    DLParser_TexRect, DLParser_TexRectFlip, DLParser_RDPLoadSync, DLParser_RDPPipeSync,
    DLParser_RDPTileSync, DLParser_RDPFullSync, DLParser_SetKeyGB, DLParser_SetKeyR,
    DLParser_SetConvert, DLParser_SetScissor, DLParser_SetPrimDepth, DLParser_RDPSetOtherMode,
//f0
    DLParser_LoadTLut, RSP_RDP_Nothing, DLParser_SetTileSize, DLParser_LoadBlock, 
    DLParser_LoadTile, DLParser_SetTile, DLParser_FillRect, DLParser_SetFillColor,
    DLParser_SetFogColor, DLParser_SetBlendColor, DLParser_SetPrimColor, DLParser_SetEnvColor,
    DLParser_SetCombine, DLParser_SetTImg, DLParser_SetZImg, DLParser_SetCImg
};


UcodeMap ucodeMap0=
{
    RSP_GBI1_SpNoop,     RSP_GBI0_Mtx,     RSP_GBI1_Reserved, RSP_GBI1_MoveMem,
    RSP_GBI0_Vtx, RSP_GBI1_Reserved, RSP_GBI0_DL,   RSP_GBI1_Reserved,
    RSP_GBI1_Reserved, RSP_GBI0_Sprite2DBase, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
//10
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
//20
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
//30
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
//40
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
//50
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
//60
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
//70
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,

//80
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
//90
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
//a0
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
//b0
    RSP_RDP_Nothing, RSP_GBI0_Tri4, RSP_GBI1_RDPHalf_Cont, RSP_GBI1_RDPHalf_2,
    RSP_GBI1_RDPHalf_1, RSP_GBI1_Line3D, RSP_GBI1_ClearGeometryMode, RSP_GBI1_SetGeometryMode,
    RSP_GBI1_EndDL, RSP_GBI1_SetOtherModeL, RSP_GBI1_SetOtherModeH, RSP_GBI1_Texture,
    RSP_GBI1_MoveWord, RSP_GBI1_PopMtx, RSP_GBI1_CullDL, RSP_GBI1_Tri1,

//c0
    RSP_GBI1_Noop,    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RDP_TriFill, RDP_TriFillZ, RDP_TriTxtr, RDP_TriTxtrZ,
    RDP_TriShade, RDP_TriShadeZ, RDP_TriShadeTxtr, RDP_TriShadeTxtrZ,
//d0
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
//e0
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    DLParser_TexRect, DLParser_TexRectFlip, DLParser_RDPLoadSync, DLParser_RDPPipeSync,
    DLParser_RDPTileSync, DLParser_RDPFullSync, DLParser_SetKeyGB, DLParser_SetKeyR,
    DLParser_SetConvert, DLParser_SetScissor, DLParser_SetPrimDepth, DLParser_RDPSetOtherMode,
//f0
    DLParser_LoadTLut, RSP_RDP_Nothing, DLParser_SetTileSize, DLParser_LoadBlock, 
    DLParser_LoadTile, DLParser_SetTile, DLParser_FillRect, DLParser_SetFillColor,
    DLParser_SetFogColor, DLParser_SetBlendColor, DLParser_SetPrimColor, DLParser_SetEnvColor,
    DLParser_SetCombine, DLParser_SetTImg, DLParser_SetZImg, DLParser_SetCImg
};

// Zelda and new games, F3DEX_GBI_2
UcodeMap ucodeMap5=
{
    RSP_GBI1_Noop,   RSP_GBI2_Vtx,     RSP_GBI1_ModifyVtx, RSP_GBI2_CullDL,
    RSP_GBI1_BranchZ, RSP_GBI2_Tri1, RSP_GBI2_Tri2, RSP_GBI2_Line3D,
    RSP_GBI2_0x8, RSP_S2DEX_BG_1CYC, RSP_S2DEX_BG_COPY, RSP_S2DEX_OBJ_RENDERMODE,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
//10
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
//20
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
//30
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
//40
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
//50
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
//60
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
//70
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,

//80
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
//90
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
//a0
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_GBI1_LoadUCode,
//b0
    RSP_GBI1_BranchZ, RSP_GBI0_Tri4, RSP_GBI1_ModifyVtx, RSP_GBI1_RDPHalf_2,
    RSP_GBI1_RDPHalf_1, RSP_GBI1_Line3D, RSP_GBI1_ClearGeometryMode, RSP_GBI1_SetGeometryMode,
    RSP_GBI1_EndDL, RSP_GBI1_SetOtherModeL, RSP_GBI1_SetOtherModeH, RSP_GBI1_Texture,
    RSP_GBI1_MoveWord, RSP_GBI1_PopMtx, RSP_GBI1_CullDL, RSP_GBI1_Tri1,

//c0
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RDP_TriFill, RDP_TriFillZ, RDP_TriTxtr, RDP_TriTxtrZ,
    RDP_TriShade, RDP_TriShadeZ, RDP_TriShadeTxtr, RDP_TriShadeTxtrZ,
//d0
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_GBI2_DL_Count, RSP_GBI2_SubModule, RSP_GBI2_Texture,
    RSP_GBI2_PopMtx, RSP_GBI2_GeometryMode, RSP_GBI2_Mtx, RSP_GBI2_MoveWord,
    RSP_GBI2_MoveMem, RSP_GBI1_LoadUCode, RSP_GBI2_DL, RSP_GBI2_EndDL,
//e0
    RSP_GBI1_SpNoop, RSP_GBI1_RDPHalf_1, RSP_GBI2_SetOtherModeL, RSP_GBI2_SetOtherModeH,
    DLParser_TexRect, DLParser_TexRectFlip, DLParser_RDPLoadSync, DLParser_RDPPipeSync,
    DLParser_RDPTileSync, DLParser_RDPFullSync, DLParser_SetKeyGB, DLParser_SetKeyR,
    DLParser_SetConvert, DLParser_SetScissor, DLParser_SetPrimDepth, DLParser_RDPSetOtherMode,
//f0
    DLParser_LoadTLut, RSP_RDP_Nothing, DLParser_SetTileSize, DLParser_LoadBlock, 
    DLParser_LoadTile, DLParser_SetTile, DLParser_FillRect, DLParser_SetFillColor,
    DLParser_SetFogColor, DLParser_SetBlendColor, DLParser_SetPrimColor, DLParser_SetEnvColor,
    DLParser_SetCombine, DLParser_SetTImg, DLParser_SetZImg, DLParser_SetCImg
};


// S2DEX 1.--
UcodeMap ucodeMap7=
{
    RSP_GBI1_SpNoop,     RSP_S2DEX_BG_1CYC_2,     RSP_S2DEX_BG_COPY, RSP_S2DEX_OBJ_RECTANGLE,
    RSP_S2DEX_OBJ_SPRITE, RSP_S2DEX_OBJ_MOVEMEM, RSP_GBI0_DL,   RSP_GBI1_Reserved,
    RSP_GBI1_Reserved, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,

//10
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
//20
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
//30
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
//40
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
//50
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
//60
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
//70
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,

//80
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
//90
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
//a0
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_GBI1_LoadUCode,
//b0
    RSP_S2DEX_SELECT_DL, RSP_S2DEX_OBJ_RENDERMODE_2, RSP_S2DEX_OBJ_RECTANGLE_R, RSP_GBI1_RDPHalf_2,
    RSP_GBI1_RDPHalf_1, RSP_GBI1_Line3D, RSP_GBI1_ClearGeometryMode, RSP_GBI1_SetGeometryMode,
    RSP_GBI1_EndDL, RSP_GBI1_SetOtherModeL, RSP_GBI1_SetOtherModeH, RSP_GBI1_Texture,
    RSP_GBI1_MoveWord, RSP_GBI1_PopMtx, RSP_GBI1_CullDL, RSP_GBI1_Tri1,

//c0
    RSP_GBI1_Noop,    RSP_S2DEX_SPObjLoadTxtr, RSP_S2DEX_SPObjLoadTxSprite, RSP_S2DEX_SPObjLoadTxRect,
    RSP_S2DEX_SPObjLoadTxRectR, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RDP_TriFill, RDP_TriFillZ, RDP_TriTxtr, RDP_TriTxtrZ,
    RDP_TriShade, RDP_TriShadeZ, RDP_TriShadeTxtr, RDP_TriShadeTxtrZ,
//d0
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
//e0
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_S2DEX_RDPHALF_0, DLParser_TexRectFlip, DLParser_RDPLoadSync, DLParser_RDPPipeSync,
    DLParser_RDPTileSync, DLParser_RDPFullSync, DLParser_SetKeyGB, DLParser_SetKeyR,
    DLParser_SetConvert, DLParser_SetScissor, DLParser_SetPrimDepth, DLParser_RDPSetOtherMode,
//f0
    DLParser_LoadTLut, RSP_RDP_Nothing, DLParser_SetTileSize, DLParser_LoadBlock, 
    DLParser_LoadTile, DLParser_SetTile, DLParser_FillRect, DLParser_SetFillColor,
    DLParser_SetFogColor, DLParser_SetBlendColor, DLParser_SetPrimColor, DLParser_SetEnvColor,
    DLParser_SetCombine, DLParser_SetTImg, DLParser_SetZImg, DLParser_SetCImg
};

// Ucode 3 - S2DEX GBI2
UcodeMap ucodeMap3=
{
    RSP_GBI1_Noop,   RSP_S2DEX_OBJ_RECTANGLE,     RSP_S2DEX_OBJ_SPRITE, RSP_GBI2_CullDL,
    RSP_S2DEX_SELECT_DL, RSP_S2DEX_SPObjLoadTxtr, RSP_S2DEX_SPObjLoadTxSprite,  RSP_S2DEX_SPObjLoadTxRect,
    RSP_S2DEX_SPObjLoadTxRectR, RSP_S2DEX_BG_1CYC, RSP_S2DEX_BG_COPY, RSP_S2DEX_OBJ_RENDERMODE,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    //10
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    //20
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    //30
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    //40
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    //50
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    //60
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    //70
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,

    //80
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    //90
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    //a0
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_GBI1_LoadUCode,
    //b0
    RSP_GBI1_BranchZ, RSP_GBI0_Tri4, RSP_GBI1_ModifyVtx, RSP_GBI1_RDPHalf_2,
    RSP_GBI1_RDPHalf_1, RSP_GBI1_Line3D, RSP_GBI1_ClearGeometryMode, RSP_GBI1_SetGeometryMode,
    RSP_GBI1_EndDL, RSP_GBI1_SetOtherModeL, RSP_GBI1_SetOtherModeH, RSP_GBI1_Texture,
    RSP_GBI1_MoveWord, RSP_GBI1_PopMtx, RSP_GBI1_CullDL, RSP_GBI1_Tri1,

    //c0
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RDP_TriFill, RDP_TriFillZ, RDP_TriTxtr, RDP_TriTxtrZ,
    RDP_TriShade, RDP_TriShadeZ, RDP_TriShadeTxtr, RDP_TriShadeTxtrZ,
    //d0
    RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing, RSP_RDP_Nothing,
    RSP_RDP_Nothing, RSP_GBI2_DL_Count, RSP_GBI2_SubModule, RSP_GBI2_Texture,
    RSP_GBI2_PopMtx, RSP_GBI2_GeometryMode, RSP_GBI2_Mtx, RSP_GBI2_MoveWord,
    RSP_GBI2_MoveMem, RSP_GBI1_LoadUCode, RSP_GBI2_DL, RSP_GBI2_EndDL,
    //e0
    RSP_GBI1_SpNoop, RSP_GBI1_RDPHalf_1, RSP_GBI2_SetOtherModeL, RSP_GBI2_SetOtherModeH,
    DLParser_TexRect, DLParser_TexRectFlip, DLParser_RDPLoadSync, DLParser_RDPPipeSync,
    DLParser_RDPTileSync, DLParser_RDPFullSync, DLParser_SetKeyGB, DLParser_SetKeyR,
    DLParser_SetConvert, DLParser_SetScissor, DLParser_SetPrimDepth, DLParser_RDPSetOtherMode,
    //f0
    DLParser_LoadTLut, RSP_RDP_Nothing, DLParser_SetTileSize, DLParser_LoadBlock, 
    DLParser_LoadTile, DLParser_SetTile, DLParser_FillRect, DLParser_SetFillColor,
    DLParser_SetFogColor, DLParser_SetBlendColor, DLParser_SetPrimColor, DLParser_SetEnvColor,
    DLParser_SetCombine, DLParser_SetTImg, DLParser_SetZImg, DLParser_SetCImg
};

RDPInstruction *currentUcodeMap = ucodeMap1;

#endif

