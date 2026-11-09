#include <retro_miscellaneous.h>
#include <assert.h>
#include "GLideN64.h"
#include "F3D.h"
#include "F3DEX.h"
#include "F3DEX2.h"
#include "N64.h"
#include "RSP.h"
#include "RDP.h"
#include "gSP.h"
#include "gDP.h"
#include "GBI.h"


void gli_F3DEX2_Mtx( u32 w0, u32 w1 )
{
	gli_gSPMatrix( w1, _SHIFTR( w0, 0, 8 ) ^ G_MTX_PUSH );
}

void gli_F3DEX2_MoveMem( u32 w0, u32 w1 )
{
	switch (_SHIFTR( w0, 0, 8 ))
	{
		case F3DEX2_MV_VIEWPORT:
			gli_gSPViewport( w1 );
			break;
		case G_MV_MATRIX:
			gli_gSPForceMatrix( w1 );

			/* force matrix takes two commands */
			RSP.PC[RSP.PCi] += 8;
			break;
		case G_MV_LIGHT:
			{
			const u32 offset = (_SHIFTR(w0, 5, 11))&0x7F8;
			const u32 n = offset / 24;
			if (n < 2)
				gli_gSPLookAt(w1, n);
			else
				gli_gSPLight(w1, n - 1);
			}
			break;
	}
}

void gli_F3DEX2_Vtx( u32 w0, u32 w1 )
{
	u32 n = _SHIFTR( w0, 12, 8 );

	gli_gSPVertex( w1, n, _SHIFTR( w0, 1, 7 ) - n );
}

void gli_F3DEX2_Reserved1( u32 w0, u32 w1 )
{
}

void gli_F3DEX2_Tri1( u32 w0, u32 w1 )
{
	gli_gSP1Triangle( _SHIFTR( w0, 17, 7 ),
				  _SHIFTR( w0, 9, 7 ),
				  _SHIFTR( w0, 1, 7 ));
}

void gli_F3DEX2_Line3D( u32 w0, u32 w1 )
{
	assert(false);
}

void gli_F3DEX2_PopMtx( u32 w0, u32 w1 )
{
	gli_gSPPopMatrixN( 0, w1 >> 6 );
}

void gli_F3DEX2_MoveWord( u32 w0, u32 w1 )
{
	switch (_SHIFTR( w0, 16, 8 ))
	{
		case G_MW_FORCEMTX:
			if (w1 == 0)
				gSP.changed |= CHANGED_MATRIX;
			else
				gSP.changed &= ~CHANGED_MATRIX;
			break;
		case G_MW_MATRIX:
			gli_gSPInsertMatrix( _SHIFTR( w0, 0, 16 ), w1 );
			break;
		case G_MW_NUMLIGHT:
			gli_gSPNumLights( w1 / 24 );
			break;
		case G_MW_CLIP:
			gli_gSPClipRatio( w1 );
			break;
		case G_MW_SEGMENT:
			gli_gSPSegment( _SHIFTR( w0, 2, 4 ) , w1 & 0x00FFFFFF );
			break;
		case G_MW_FOG:
			gli_gSPFogFactor( (s16)_SHIFTR( w1, 16, 16 ), (s16)_SHIFTR( w1, 0, 16 ) );
			break;
		case G_MW_LIGHTCOL:
			gli_gSPLightColor((_SHIFTR( w0, 0, 16 ) / 24) + 1, w1 );
			break;
		case G_MW_PERSPNORM:
			gli_gSPPerspNormalize( w1 );
			break;
	}
}

void gli_F3DEX2_Texture( u32 w0, u32 w1 )
{
	gli_gSPTexture( _FIXED2FLOAT( _SHIFTR( w1, 16, 16 ), 16 ),
				_FIXED2FLOAT( _SHIFTR( w1, 0, 16 ), 16 ),
				_SHIFTR( w0, 11, 3 ),
				_SHIFTR( w0, 8, 3 ),
				_SHIFTR( w0, 1, 7 ) );
}

void gli_F3DEX2_SetOtherMode_H( u32 w0, u32 w1 )
{
	const u32 length = _SHIFTR(w0, 0, 8) + 1;
	const u32 shift = MAX(0, (s32)(32 - _SHIFTR(w0, 8, 8) - length));
	gli_gSPSetOtherMode_H(length, shift, w1);
}

void gli_F3DEX2_SetOtherMode_L( u32 w0, u32 w1 )
{
	const u32 length = _SHIFTR(w0, 0, 8) + 1;
	const u32 shift = MAX(0, (s32)(32 - _SHIFTR(w0, 8, 8) - length));
	gli_gSPSetOtherMode_L(length, shift, w1);
}

void gli_F3DEX2_GeometryMode( u32 w0, u32 w1 )
{
	gli_gSPGeometryMode( ~_SHIFTR( w0, 0, 24 ), w1 );
}

void gli_F3DEX2_DMAIO( u32 w0, u32 w1 )
{
	gSP.DMAIO_address = RSP_SegmentToPhysical(w1);
}

void gli_F3DEX2_Special_1( u32 w0, u32 w1 )
{
	const u32 param = _SHIFTR(w0, 0, 8);
	if (gli_GBI_isCombineMatrices())
		gli_gSPCombineMatrices(param);
	else
		gli_gSPDlistCount(param, w1);
}

void gli_F3DEX2_Special_2( u32 w0, u32 w1 )
{
}

void gli_F3DEX2_Special_3( u32 w0, u32 w1 )
{
}

void gli_F3DEX2_Quad( u32 w0, u32 w1 )
{
	gli_gSP2Triangles( _SHIFTR( w0, 17, 7 ),
				   _SHIFTR( w0, 9, 7 ),
				   _SHIFTR( w0, 1, 7 ),
				   0,
				   _SHIFTR( w1, 17, 7 ),
				   _SHIFTR( w1, 9, 7 ),
				   _SHIFTR( w1, 1, 7 ),
				   0 );
}

void gli_F3DEX2_Init(void)
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
	GBI_SetGBI( G_BRANCH_Z,				F3DEX2_BRANCH_Z,			gli_F3DEX_Branch_Z );
	GBI_SetGBI( G_TRI1,					F3DEX2_TRI1,				gli_F3DEX2_Tri1 );
	GBI_SetGBI( G_TRI2,					F3DEX2_TRI2,				gli_F3DEX_Tri2 );
	GBI_SetGBI( G_QUAD,					F3DEX2_QUAD,				gli_F3DEX2_Quad );
	GBI_SetGBI( G_LINE3D,				F3DEX2_LINE3D,				gli_F3DEX2_Line3D );
}

