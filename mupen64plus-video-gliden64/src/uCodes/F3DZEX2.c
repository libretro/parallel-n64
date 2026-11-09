#include "GLideN64.h"
#include "F3D.h"
#include "F3DEX.h"
#include "F3DEX2.h"
#include "F3DZEX2.h"
#include "gSP.h"

#define	F3DZEX2_BRANCH_W	0x04

void gli_F3DZEX2_Branch_W( u32 w0, u32 w1 )
{
	gli_gSPBranchLessW(gDP.half_1, _SHIFTR( w0, 1, 7 ), w1);
}

void gli_F3DZEX2_Init(void)
{
	gli_gSPSetupFunctions();
	/* Set GeometryMode flags */
	GBI_InitFlags( F3DEX2 );

	gli_GBIPCStackSize = 18;

	/* GBI Command						Command Value				Command Function */
	GBI_SetGBI( G_RDPHALF_2,			F3DEX2_RDPHALF_2,			gli_F3D_RDPHalf_2 );
	GBI_SetGBI( G_SETOTHERMODE_H,		F3DEX2_SETOTHERMODE_H,		gli_F3DEX2_SetOtherMode_H );
	GBI_SetGBI( G_SETOTHERMODE_L,		F3DEX2_SETOTHERMODE_L,		gli_F3DEX2_SetOtherMode_L );
	GBI_SetGBI( G_RDPHALF_1,			F3DEX2_RDPHALF_1,			gli_F3D_RDPHalf_1 );
	GBI_SetGBI( G_SPNOOP,				F3DEX2_SPNOOP,				gli_F3D_SPNoOp );
	GBI_SetGBI( G_ENDDL,				F3DEX2_ENDDL,				gli_F3D_EndDL );
	GBI_SetGBI( G_DL,					F3DEX2_DL,					gli_F3D_DList );
	GBI_SetGBI( G_LOAD_UCODE,			F3DEX2_LOAD_UCODE,			gli_F3DEX_Load_uCode );
	GBI_SetGBI( G_MOVEMEM,				F3DEX2_MOVEMEM,				gli_F3DEX2_MoveMem );
	GBI_SetGBI( G_MOVEWORD,				F3DEX2_MOVEWORD,			gli_F3DEX2_MoveWord );
	GBI_SetGBI( G_MTX,					F3DEX2_MTX,					gli_F3DEX2_Mtx );
	GBI_SetGBI( G_GEOMETRYMODE,			F3DEX2_GEOMETRYMODE,		gli_F3DEX2_GeometryMode );
	GBI_SetGBI( G_POPMTX,				F3DEX2_POPMTX,				gli_F3DEX2_PopMtx );
	GBI_SetGBI( G_TEXTURE,				F3DEX2_TEXTURE,				gli_F3DEX2_Texture );
	GBI_SetGBI( G_DMA_IO,				F3DEX2_DMA_IO,				gli_F3DEX2_DMAIO );
	GBI_SetGBI( G_SPECIAL_1,			F3DEX2_SPECIAL_1,			gli_F3DEX2_Special_1 );
	GBI_SetGBI( G_SPECIAL_2,			F3DEX2_SPECIAL_2,			gli_F3DEX2_Special_2 );
	GBI_SetGBI( G_SPECIAL_3,			F3DEX2_SPECIAL_3,			gli_F3DEX2_Special_3 );

	GBI_SetGBI( G_VTX,					F3DEX2_VTX,					gli_F3DEX2_Vtx );
	GBI_SetGBI( G_MODIFYVTX,			F3DEX2_MODIFYVTX,			gli_F3DEX_ModifyVtx );
	GBI_SetGBI(	G_CULLDL,				F3DEX2_CULLDL,				gli_F3DEX_CullDL );
	GBI_SetGBI( G_BRANCH_W,				F3DZEX2_BRANCH_W,			gli_F3DZEX2_Branch_W );
	GBI_SetGBI( G_TRI1,					F3DEX2_TRI1,				gli_F3DEX2_Tri1 );
	GBI_SetGBI( G_TRI2,					F3DEX2_TRI2,				gli_F3DEX_Tri2 );
	GBI_SetGBI( G_QUAD,					F3DEX2_QUAD,				gli_F3DEX2_Quad );
	GBI_SetGBI( G_LINE3D,				F3DEX2_LINE3D,				gli_F3DEX2_Line3D );
}
