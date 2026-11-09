#include "GLideN64.h"
#include "F3D.h"
#include "F3DGOLDEN.h"
#include "F3DPD.h"
#include "N64.h"
#include "gSP.h"

#define F3DPD_VTXCOLORBASE	0x07

void gli_F3DPD_Vtx( u32 w0, u32 w1 )
{
	gli_gSPCIVertex( w1, _SHIFTR( w0, 20, 4 ) + 1, _SHIFTR( w0, 16, 4 ) );
}

void gli_F3DPD_VtxColorBase( u32 w0, u32 w1 )
{
	gli_gSPSetVertexColorBase( w1 );
}

void gli_F3DPD_Init(void)
{
	gli_gSPSetupFunctions();
	/* Set GeometryMode flags */
	GBI_InitFlags( F3D );

	gli_GBIPCStackSize = 10;

	/*          GBI Command             Command Value			Command Function */
	GBI_SetGBI( G_SPNOOP,				F3D_SPNOOP,				gli_F3D_SPNoOp );
	GBI_SetGBI( G_MTX,					F3D_MTX,				gli_F3D_Mtx );
	GBI_SetGBI( G_RESERVED0,			F3D_RESERVED0,			gli_F3D_Reserved0 );
	GBI_SetGBI( G_MOVEMEM,				F3D_MOVEMEM,			gli_F3D_MoveMem );
	GBI_SetGBI( G_VTX,					F3D_VTX,				gli_F3DPD_Vtx );
	GBI_SetGBI( G_RESERVED1,			F3D_RESERVED1,			gli_F3D_Reserved1 );
	GBI_SetGBI( G_DL,					F3D_DL,					gli_F3D_DList );
	GBI_SetGBI( G_VTXCOLORBASE,			F3DPD_VTXCOLORBASE,		gli_F3DPD_VtxColorBase );
	GBI_SetGBI( G_RESERVED3,			F3D_RESERVED3,			gli_F3D_Reserved3 );
	GBI_SetGBI( G_SPRITE2D_BASE,		F3D_SPRITE2D_BASE,		gli_F3D_Sprite2D_Base );

	GBI_SetGBI( G_TRI1,					F3D_TRI1,				gli_F3D_Tri1 );
	GBI_SetGBI( G_CULLDL,				F3D_CULLDL,				gli_F3D_CullDL );
	GBI_SetGBI( G_POPMTX,				F3D_POPMTX,				gli_F3D_PopMtx );
	GBI_SetGBI( G_MOVEWORD,				F3D_MOVEWORD,			gli_F3D_MoveWord );
	GBI_SetGBI( G_TEXTURE,				F3D_TEXTURE,			gli_F3D_Texture );
	GBI_SetGBI( G_SETOTHERMODE_H,		F3D_SETOTHERMODE_H,		gli_F3D_SetOtherMode_H );
	GBI_SetGBI( G_SETOTHERMODE_L,		F3D_SETOTHERMODE_L,		gli_F3D_SetOtherMode_L );
	GBI_SetGBI( G_ENDDL,				F3D_ENDDL,				gli_F3D_EndDL );
	GBI_SetGBI( G_SETGEOMETRYMODE,		F3D_SETGEOMETRYMODE,	gli_F3D_SetGeometryMode );
	GBI_SetGBI( G_CLEARGEOMETRYMODE,	F3D_CLEARGEOMETRYMODE,	gli_F3D_ClearGeometryMode );
	GBI_SetGBI( G_QUAD,					F3D_QUAD,				gli_F3D_Quad );
	GBI_SetGBI( G_RDPHALF_1,			F3D_RDPHALF_1,			gli_F3D_RDPHalf_1 );
	GBI_SetGBI( G_RDPHALF_2,			F3D_RDPHALF_2,			gli_F3D_RDPHalf_2 );
	GBI_SetGBI( G_RDPHALF_CONT,			F3D_RDPHALF_CONT,		gli_F3D_RDPHalf_Cont );
	GBI_SetGBI( G_TRIX,					F3D_TRIX,				gli_F3D_TriX );

	gli_gSPSetDMAOffsets( 0, 0 );
}

