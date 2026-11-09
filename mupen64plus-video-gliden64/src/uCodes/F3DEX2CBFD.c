#include "GLideN64.h"
#include "F3D.h"
#include "F3DEX.h"
#include "F3DEX2.h"
#include "F3DEX2CBFD.h"
#include "N64.h"
#include "RSP.h"
#include "RDP.h"
#include "gSP.h"
#include "gDP.h"
#include "GBI.h"

static
void F3DEX2CBFD_Vtx( u32 w0, u32 w1 )
{
	u32 n = _SHIFTR( w0, 12, 8 );

	gli_gSPCBFDVertex( w1, n, _SHIFTR( w0, 1, 7 ) - n );
}

static
void F3DEX2CBFD_CoordMod(u32 _w0, u32 _w1)
{
	u32 idx, pos;
	if ((_w0 & 8) != 0)
		return;
	idx = _SHIFTR(_w0, 1, 2);
	pos = _w0 & 0x30;
	if (pos == 0) {
		gSP.cbfd.vertexCoordMod[0 + idx] = (f32)(s16)_SHIFTR(_w1, 16, 16);
		gSP.cbfd.vertexCoordMod[1 + idx] = (f32)(s16)_SHIFTR(_w1, 0, 16);
	}
	else if (pos == 0x10) {
		gSP.cbfd.vertexCoordMod[4 + idx] = _FIXED2FLOAT(_SHIFTR(_w1, 16, 16), 16);
		gSP.cbfd.vertexCoordMod[5 + idx] = _FIXED2FLOAT(_SHIFTR(_w1, 0, 16), 16);
		gSP.cbfd.vertexCoordMod[12 + idx] = gSP.cbfd.vertexCoordMod[0 + idx] + gSP.cbfd.vertexCoordMod[4 + idx];
		gSP.cbfd.vertexCoordMod[13 + idx] = gSP.cbfd.vertexCoordMod[1 + idx] + gSP.cbfd.vertexCoordMod[5 + idx];
	}
	else if (pos == 0x20) {
		gSP.cbfd.vertexCoordMod[8 + idx] = (f32)(s16)_SHIFTR(_w1, 16, 16);
		gSP.cbfd.vertexCoordMod[9 + idx] = (f32)(s16)_SHIFTR(_w1, 0, 16);
	}
}

static
void F3DEX2CBFD_MoveWord( u32 w0, u32 w1 )
{
	switch (_SHIFTR( w0, 16, 8 ))
	{
		case G_MW_NUMLIGHT:
			gli_gSPNumLights( w1 / 48 );
			break;
		case G_MW_CLIP:
			gli_gSPClipRatio( w1 );
			break;
		case G_MW_SEGMENT:
			gli_gSPSegment( _SHIFTR( w0, 0, 16 ) >> 2, w1 & 0x00FFFFFF );
			break;
		case G_MW_FOG:
			gli_gSPFogFactor( (s16)_SHIFTR( w1, 16, 16 ), (s16)_SHIFTR( w1, 0, 16 ) );
			break;
		case G_MW_PERSPNORM:
			gli_gSPPerspNormalize( w1 );
			break;
		case G_MW_COORD_MOD:
			F3DEX2CBFD_CoordMod( w0, w1 );
			break;
	}
}

static
void F3DEX2CBFD_MoveMem( u32 w0, u32 w1 )
{
	switch (_SHIFTR( w0, 0, 8 ))
	{
		case F3DEX2_MV_VIEWPORT:
			gli_gSPViewport( w1 );
			break;
		case G_MV_LIGHT:
			{
			const u32 offset = _SHIFTR(w0, 5, 14);
			const u32 n = offset / 48;
			if (n < 2)
				gli_gSPLookAt(w1, n);
			else
				gli_gSPLightCBFD(w1, n - 2);
			}
			break;
		case G_MV_NORMALES:
			gSP.cbfd.vertexNormalBase = RSP_SegmentToPhysical(w1);
			break;
	}
}

static
void F3DEX2CBFD_Tri4( u32 w0, u32 w1 )
{
	gli_gSP4Triangles( _SHIFTR( w0, 23, 5 ), _SHIFTR( w0, 18, 5 ), (_SHIFTR( w0, 15, 3 )<<2)|_SHIFTR( w1, 30, 2 ),
				   _SHIFTR( w0, 10, 5 ), _SHIFTR( w0,  5, 5 ), _SHIFTR( w0,  0, 5 ),
				   _SHIFTR( w1, 25, 5 ), _SHIFTR( w1, 20, 5 ), _SHIFTR( w1, 15, 5 ),
				   _SHIFTR( w1, 10, 5 ), _SHIFTR( w1,  5, 5 ), _SHIFTR( w1,  0, 5 ) );
}

static
void F3DEX2CBFD_SwitchLightingMode(u32 _w0, u32 _w1)
{
	gSP.cbfd.advancedLighting = true;
}

void gli_F3DEX2CBFD_Init(void)
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
	GBI_SetGBI( G_LOAD_UCODE,			F3DEX2_LOAD_UCODE,			F3DEX2CBFD_SwitchLightingMode );
	GBI_SetGBI( G_MOVEMEM,				F3DEX2_MOVEMEM,				F3DEX2CBFD_MoveMem );
	GBI_SetGBI( G_MOVEWORD,				F3DEX2_MOVEWORD,			F3DEX2CBFD_MoveWord );
	GBI_SetGBI( G_MTX,					F3DEX2_MTX,					gli_F3DEX2_Mtx );
	GBI_SetGBI( G_GEOMETRYMODE,			F3DEX2_GEOMETRYMODE,		gli_F3DEX2_GeometryMode );
	GBI_SetGBI( G_POPMTX,				F3DEX2_POPMTX,				gli_F3DEX2_PopMtx );
	GBI_SetGBI( G_TEXTURE,				F3DEX2_TEXTURE,				gli_F3DEX2_Texture );
	GBI_SetGBI( G_DMA_IO,				F3DEX2_DMA_IO,				gli_F3DEX2_DMAIO );
	GBI_SetGBI( G_SPECIAL_1,			F3DEX2_SPECIAL_1,			gli_F3DEX2_Special_1 );
	GBI_SetGBI( G_SPECIAL_2,			F3DEX2_SPECIAL_2,			gli_F3DEX2_Special_2 );
	GBI_SetGBI( G_SPECIAL_3,			F3DEX2_SPECIAL_3,			gli_F3DEX2_Special_3 );

	GBI_SetGBI( G_VTX,					F3DEX2_VTX,					F3DEX2CBFD_Vtx );
	GBI_SetGBI( G_MODIFYVTX,			F3DEX2_MODIFYVTX,			gli_F3DEX_ModifyVtx );
	GBI_SetGBI(	G_CULLDL,				F3DEX2_CULLDL,				gli_F3DEX_CullDL );
	GBI_SetGBI( G_BRANCH_Z,				F3DEX2_BRANCH_Z,			gli_F3DEX_Branch_Z );
	GBI_SetGBI( G_TRI1,					F3DEX2_TRI1,				gli_F3DEX2_Tri1 );
	GBI_SetGBI( G_TRI2,					F3DEX2_TRI2,				gli_F3DEX_Tri2 );
	GBI_SetGBI( G_QUAD,					F3DEX2_QUAD,				gli_F3DEX2_Quad );
	GBI_SetGBI( G_LINE3D,				F3DEX2_LINE3D,				gli_F3DEX2_Line3D );
	{
		u32 i;
		for (i = 0; i < 16; ++i)
			gli_GBIcmd[F3DEX2CBFD_TRI4+i] = F3DEX2CBFD_Tri4;
	}
}
