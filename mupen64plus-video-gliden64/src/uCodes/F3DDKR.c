#include "GLideN64.h"
#include "F3D.h"
#include "F3DDKR.h"
#include "N64.h"
#include "RSP.h"
#include "RDP.h"
#include "gSP.h"
#include "gDP.h"
#include "GBI.h"

void gli_F3DDKR_DMA_Mtx( u32 w0, u32 w1 )
{
	u32 index, multiply;

	if (_SHIFTR( w0, 0, 16 ) != 64) {
		return;
	}

	index = _SHIFTR( w0, 16, 4 );

	if (index == 0) {/* DKR */
		index = _SHIFTR( w0, 22, 2 );
		multiply = 0;
	}
	else { /* JFG */
		multiply = _SHIFTR( w0, 23, 1 );
	}

	gli_gSPDMAMatrix( w1, index, multiply );
}

void gli_F3DDKR_DMA_Vtx( u32 w0, u32 w1 )
{
	u32 n;
	if ((w0 & F3DDKR_VTX_APPEND)) {
		if (gSP.matrix.billboard)
			gSP.vertexi = 1;
	} else
		gSP.vertexi = 0;

	n = _SHIFTR( w0, 19, 5 ) + 1;

	gli_gSPDMAVertex( w1, n, gSP.vertexi + _SHIFTR( w0, 9, 5 ) );

	gSP.vertexi += n;
}

void gli_F3DJFG_DMA_Vtx(u32 w0, u32 w1)
{
	u32 n;
	if ((w0 & F3DDKR_VTX_APPEND)) {
		if (gSP.matrix.billboard)
			gSP.vertexi = 1;
	} else
		gSP.vertexi = 0;

	n = _SHIFTR(w0, 19, 5);

	gli_gSPDMAVertex(w1, n, gSP.vertexi + _SHIFTR(w0, 9, 5));

	gSP.vertexi += n;
}

void gli_F3DDKR_DMA_Tri(u32 w0, u32 w1)
{
	gSP.texture.on = _SHIFTR(w0, 16, 4);
	gli_gSPDMATriangles(w1, _SHIFTR(w0, 20, 4) + 1);
	gSP.vertexi = 0;
}

void gli_F3DDKR_DMA_DList( u32 w0, u32 w1 )
{
	gli_gSPDlistCount(_SHIFTR(w0, 16, 8), w1);
}

void gli_F3DDKR_DMA_Offsets( u32 w0, u32 w1 )
{
	gli_gSPSetDMAOffsets( _SHIFTR( w0, 0, 24 ), _SHIFTR( w1, 0, 24 ) );
}

void gli_F3DDKR_DMA_Tex_Offset(u32 w0, u32 w1)
{
	gli_gSPSetDMATexOffset(w1);
}

void gli_F3DDKR_MoveWord( u32 w0, u32 w1 )
{
	switch (_SHIFTR( w0, 0, 8 )) {
		case 0x02:
			gSP.matrix.billboard = w1 & 1;
			break;
		case 0x0A:
			gSP.matrix.modelViewi = _SHIFTR( w1, 6, 2 );
			gSP.changed |= CHANGED_MATRIX;
			break;
		default:
			gli_F3D_MoveWord( w0, w1 );
			break;
	}
}

void gli_F3DDKR_Init(void)
{
	gli_gSPSetupFunctions();
	/* Set GeometryMode flags */
	GBI_InitFlags( F3D );

	gli_GBIPCStackSize = 10;

	/*          GBI Command             Command Value			Command Function */
	GBI_SetGBI( G_SPNOOP,				F3D_SPNOOP,				gli_F3D_SPNoOp );
	GBI_SetGBI( G_DMA_MTX,				F3DDKR_DMA_MTX,			gli_F3DDKR_DMA_Mtx );
	GBI_SetGBI( G_DMA_TEX_OFFSET,		F3DDKR_DMA_TEX_OFFSET,	gli_F3DDKR_DMA_Tex_Offset );
	GBI_SetGBI( G_MOVEMEM,				F3D_MOVEMEM,			gli_F3D_MoveMem );
	GBI_SetGBI( G_DMA_VTX,				F3DDKR_DMA_VTX,			gli_F3DDKR_DMA_Vtx );
	GBI_SetGBI( G_DL,					F3D_DL,					gli_F3D_DList );
	GBI_SetGBI( G_DMA_DL,				F3DDKR_DMA_DL,			gli_F3DDKR_DMA_DList );
	GBI_SetGBI( G_DMA_TRI,				F3DDKR_DMA_TRI,			gli_F3DDKR_DMA_Tri );

	GBI_SetGBI( G_DMA_OFFSETS,			F3DDKR_DMA_OFFSETS,		gli_F3DDKR_DMA_Offsets );
	GBI_SetGBI( G_CULLDL,				F3D_CULLDL,				gli_F3D_CullDL );
	GBI_SetGBI( G_MOVEWORD,				F3D_MOVEWORD,			gli_F3DDKR_MoveWord );
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

	gli_gSPSetDMAOffsets( 0, 0 );
}

void gli_F3DJFG_Init(void)
{
	gli_F3DDKR_Init();
	GBI_SetGBI(G_DMA_VTX, F3DDKR_DMA_VTX, gli_F3DJFG_DMA_Vtx);
}
