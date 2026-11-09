#include "GLideN64.h"
#include "F3D.h"
#include "N64.h"
#include "RSP.h"
#include "RDP.h"
#include "gSP.h"
#include "gDP.h"
#include "GBI.h"

void gli_F3D_SPNoOp( u32 w0, u32 w1 )
{
	gli_gSPNoOp();
}

void gli_F3D_Mtx( u32 w0, u32 w1 )
{
	if (_SHIFTR( w0, 0, 16 ) != 64) {
		return;
	}

	gli_gSPMatrix( w1, _SHIFTR( w0, 16, 8 ) );
}

void gli_F3D_Reserved0( u32 w0, u32 w1 )
{
}

void gli_F3D_MoveMem( u32 w0, u32 w1 )
{
	switch (_SHIFTR( w0, 16, 8 )) {
		case F3D_MV_VIEWPORT:
			gli_gSPViewport( w1 );
			break;
		case G_MV_MATRIX_1:
			gli_gSPForceMatrix( w1 );

			/* force matrix takes four commands */
			RSP.PC[RSP.PCi] += 24;
			break;
		case G_MV_L0:
			gli_gSPLight( w1, LIGHT_1 );
			break;
		case G_MV_L1:
			gli_gSPLight( w1, LIGHT_2 );
			break;
		case G_MV_L2:
			gli_gSPLight( w1, LIGHT_3 );
			break;
		case G_MV_L3:
			gli_gSPLight( w1, LIGHT_4 );
			break;
		case G_MV_L4:
			gli_gSPLight( w1, LIGHT_5 );
			break;
		case G_MV_L5:
			gli_gSPLight( w1, LIGHT_6 );
			break;
		case G_MV_L6:
			gli_gSPLight( w1, LIGHT_7 );
			break;
		case G_MV_L7:
			gli_gSPLight( w1, LIGHT_8 );
			break;
		case G_MV_LOOKATX:
			gli_gSPLookAt(w1, 0);
			break;
		case G_MV_LOOKATY:
			gli_gSPLookAt(w1, 1);
			break;
	}
}

void gli_F3D_Vtx( u32 w0, u32 w1 )
{
	gli_gSPVertex( w1, _SHIFTR( w0, 20, 4 ) + 1, _SHIFTR( w0, 16, 4 ) );
}

void gli_F3D_Reserved1( u32 w0, u32 w1 )
{
}

extern unsigned int LegacySm64ToolsHacks;
void gli_F3D_DList( u32 w0, u32 w1 )
{
	switch (_SHIFTR( w0, 16, 8 ))
	{
		case G_DL_PUSH:
			gli_gSPDisplayList( w1 );
			break;
		case G_DL_NOPUSH:
			gli_gSPBranchList( w1 );
			break;
		default:
			if (LegacySm64ToolsHacks)
				gli_gSPBranchList( w1 );

			break;
	}
}

void gli_F3D_Reserved2( u32 w0, u32 w1 )
{
}

void gli_F3D_Reserved3( u32 w0, u32 w1 )
{
}

void gli_F3D_Sprite2D_Base( u32 w0, u32 w1 )
{
	gli_gSPSprite2DBase( w1 );
}

void gli_F3D_Tri1( u32 w0, u32 w1 )
{
	gli_gSP1Triangle( _SHIFTR( w1, 16, 8 ) / 10,
				  _SHIFTR( w1, 8, 8 ) / 10,
				  _SHIFTR( w1, 0, 8 ) / 10);
}

void gli_F3D_CullDL( u32 w0, u32 w1 )
{
	gli_gSPCullDisplayList( _SHIFTR( w0, 0, 24 ) / 40, (w1 / 40) - 1 );
}

void gli_F3D_PopMtx( u32 w0, u32 w1 )
{
	gli_gSPPopMatrix( w1 );
}

void gli_F3D_MoveWord( u32 w0, u32 w1 )
{
	switch (_SHIFTR( w0, 0, 8 )) {
		case G_MW_MATRIX:
			gli_gSPInsertMatrix( _SHIFTR( w0, 8, 16 ), w1 );
			break;
		case G_MW_NUMLIGHT:
			gli_gSPNumLights( ((w1 - 0x80000000) >> 5) - 1 );
			break;
		case G_MW_CLIP:
			gli_gSPClipRatio( w1 );
			break;
		case G_MW_SEGMENT:
			gli_gSPSegment( _SHIFTR( w0, 10, 4 ), w1 & 0x00FFFFFF );
			break;
		case G_MW_FOG:
/*			u32 fm, fo, min, max;

			fm = _SHIFTR( w1, 16, 16 );
			fo = _SHIFTR( w1, 0, 16 );

			min = 500 - (fo * (128000 / fm)) / 256;
			max = (128000 / fm) + min;*/

			gli_gSPFogFactor( (s16)_SHIFTR( w1, 16, 16 ), (s16)_SHIFTR( w1, 0, 16 ) );
			break;
		case G_MW_LIGHTCOL:
			switch (_SHIFTR( w0, 8, 16 ))
			{
				case F3D_MWO_aLIGHT_1:
					gli_gSPLightColor( LIGHT_1, w1 );
					break;
				case F3D_MWO_aLIGHT_2:
					gli_gSPLightColor( LIGHT_2, w1 );
					break;
				case F3D_MWO_aLIGHT_3:
					gli_gSPLightColor( LIGHT_3, w1 );
					break;
				case F3D_MWO_aLIGHT_4:
					gli_gSPLightColor( LIGHT_4, w1 );
					break;
				case F3D_MWO_aLIGHT_5:
					gli_gSPLightColor( LIGHT_5, w1 );
					break;
				case F3D_MWO_aLIGHT_6:
					gli_gSPLightColor( LIGHT_6, w1 );
					break;
				case F3D_MWO_aLIGHT_7:
					gli_gSPLightColor( LIGHT_7, w1 );
					break;
				case F3D_MWO_aLIGHT_8:
					gli_gSPLightColor( LIGHT_8, w1 );
					break;
			}
			break;
		case G_MW_POINTS:
			{
			  const u32 val = _SHIFTR(w0, 8, 16);
			  gli_gSPModifyVertex(val / 40, val % 40, w1);
			}
			break;
		case G_MW_PERSPNORM:
			gli_gSPPerspNormalize( w1 );
			break;
	}
}

void gli_F3D_Texture( u32 w0, u32 w1 )
{
	gli_gSPTexture( _FIXED2FLOAT( _SHIFTR( w1, 16, 16 ), 16 ),
				_FIXED2FLOAT( _SHIFTR( w1, 0, 16 ), 16 ),
				_SHIFTR( w0, 11, 3 ),
				_SHIFTR( w0, 8, 3 ),
				_SHIFTR( w0, 0, 8 ) );
}

void gli_F3D_SetOtherMode_H( u32 w0, u32 w1 )
{
	const u32 length = _SHIFTR(w0, 0, 8);
	const u32 shift = _SHIFTR(w0, 8, 8);
	gli_gSPSetOtherMode_H(length, shift, w1);
}

void gli_F3D_SetOtherMode_L( u32 w0, u32 w1 )
{
	const u32 length = _SHIFTR(w0, 0, 8);
	const u32 shift = _SHIFTR(w0, 8, 8);
	gli_gSPSetOtherMode_L(length, shift, w1);
}

void gli_F3D_EndDL( u32 w0, u32 w1 )
{
	gli_gSPEndDisplayList();
}

void gli_F3D_SetGeometryMode( u32 w0, u32 w1 )
{
	gli_gSPSetGeometryMode( w1 );
}

void gli_F3D_ClearGeometryMode( u32 w0, u32 w1 )
{
	gli_gSPClearGeometryMode( w1 );
}

void gli_F3D_Quad( u32 w0, u32 w1 )
{
	gli_gSP1Quadrangle( _SHIFTR( w1, 24, 8 ) / 10, _SHIFTR( w1, 16, 8 ) / 10, _SHIFTR( w1, 8, 8 ) / 10, _SHIFTR( w1, 0, 8 ) / 10 );
}

void gli_F3D_RDPHalf_1( u32 w0, u32 w1 )
{
	gDP.half_1 = w1;
	gli_RDP_Half_1(w1);
}

void gli_F3D_RDPHalf_2( u32 w0, u32 w1 )
{
	gDP.half_2 = w1;
}

void gli_F3D_RDPHalf_Cont( u32 w0, u32 w1 )
{
}

void gli_F3D_Init(void)
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
	GBI_SetGBI( G_VTX,					F3D_VTX,				gli_F3D_Vtx );
	GBI_SetGBI( G_RESERVED1,			F3D_RESERVED1,			gli_F3D_Reserved1 );
	GBI_SetGBI( G_DL,					F3D_DL,					gli_F3D_DList );
	GBI_SetGBI( G_RESERVED2,			F3D_RESERVED2,			gli_F3D_Reserved2 );
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
}
