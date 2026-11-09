#include "GLideN64.h"
#include "F3D.h"
#include "F3DEX.h"
#include "F3DTEXA.h"
#include "N64.h"
#include "RDP.h"
#include "gSP.h"
#include "GBI.h"

#define F3DTEXA_LOADTEX			0xB5
#define F3DTEXA_SETTILESIZE		0xBE

u32 G_TEXA_LOADTEX, G_TEXA_SETTILESIZE;

void gli_F3DTTEXA_LoadTex(u32 w0, u32 w1)
{
	gli_RDP_SetTImg(0x3d100000, w1);
	gli_RDP_SetTile(0x35100000, 0x07000000);
	gli_RDP_LoadBlock(0x33000000, 0x27000000 | _SHIFTR(w0, 0, 24));
}

void gli_F3DTTEXA_SetTileSize(u32 w0, u32 w1)
{
	const u32 firstHalf = _SHIFTL(_SHIFTR(w1, 24, 8), 9, 8);
	gli_RDP_SetTile(0x35400000 | firstHalf, _SHIFTR(w0, 0, 24));
	gli_RDP_SetTileSize(0x32000000, _SHIFTR(w1, 0, 24));
}

void gli_F3DTEXA_Init(void)
{
	gli_gSPSetupFunctions();
	/* Set GeometryMode flags */
	GBI_InitFlags(F3DEX);

	gli_GBIPCStackSize = 18;

	/*          GBI Command             Command Value			Command Function */
	GBI_SetGBI(G_SPNOOP, F3D_SPNOOP, gli_F3D_SPNoOp);
	GBI_SetGBI(G_MTX, F3D_MTX, gli_F3D_Mtx);
	GBI_SetGBI(G_RESERVED0, F3D_RESERVED0, gli_F3D_Reserved0);
	GBI_SetGBI(G_MOVEMEM, F3D_MOVEMEM, gli_F3D_MoveMem);
	GBI_SetGBI(G_VTX, F3D_VTX, gli_F3DEX_Vtx);
	GBI_SetGBI(G_RESERVED1, F3D_RESERVED1, gli_F3D_Reserved1);
	GBI_SetGBI(G_DL, F3D_DL, gli_F3D_DList);
	GBI_SetGBI(G_RESERVED2, F3D_RESERVED2, gli_F3D_Reserved2);
	GBI_SetGBI(G_RESERVED3, F3D_RESERVED3, gli_F3D_Reserved3);
	GBI_SetGBI(G_SPRITE2D_BASE, F3D_SPRITE2D_BASE, gli_F3D_Sprite2D_Base);

	GBI_SetGBI(G_TRI1, F3D_TRI1, gli_F3DEX_Tri1);
	GBI_SetGBI(G_TEXA_SETTILESIZE, F3DTEXA_SETTILESIZE, gli_F3DTTEXA_SetTileSize);
	GBI_SetGBI(G_POPMTX, F3D_POPMTX, gli_F3D_PopMtx);
	GBI_SetGBI(G_MOVEWORD, F3D_MOVEWORD, gli_F3D_MoveWord);
	GBI_SetGBI(G_TEXTURE, F3D_TEXTURE, gli_F3D_Texture);
	GBI_SetGBI(G_SETOTHERMODE_H, F3D_SETOTHERMODE_H, gli_F3D_SetOtherMode_H);
	GBI_SetGBI(G_SETOTHERMODE_L, F3D_SETOTHERMODE_L, gli_F3D_SetOtherMode_L);
	GBI_SetGBI(G_ENDDL, F3D_ENDDL, gli_F3D_EndDL);
	GBI_SetGBI(G_SETGEOMETRYMODE, F3D_SETGEOMETRYMODE, gli_F3D_SetGeometryMode);
	GBI_SetGBI(G_CLEARGEOMETRYMODE, F3D_CLEARGEOMETRYMODE, gli_F3D_ClearGeometryMode);
	GBI_SetGBI(G_TEXA_LOADTEX, F3DTEXA_LOADTEX, gli_F3DTTEXA_LoadTex);
	GBI_SetGBI(G_RDPHALF_1, F3D_RDPHALF_1, gli_F3D_RDPHalf_1);
	GBI_SetGBI(G_RDPHALF_2, F3D_RDPHALF_2, gli_F3D_RDPHalf_2);
	GBI_SetGBI(G_MODIFYVTX, F3DEX_MODIFYVTX, gli_F3DEX_ModifyVtx);
	GBI_SetGBI(G_TRI2, F3DEX_TRI2, gli_F3DEX_Tri2);
	GBI_SetGBI(G_BRANCH_Z, F3DEX_BRANCH_Z, gli_F3DEX_Branch_Z);
	GBI_SetGBI(G_LOAD_UCODE, F3DEX_LOAD_UCODE, gli_F3DEX_Load_uCode);
}
