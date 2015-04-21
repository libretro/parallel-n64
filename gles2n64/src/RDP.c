#include "N64.h"
#include "RSP.h"
#include "GBI.h"
#include "gDP.h"
#include "Types.h"
#include "Debug.h"
#include "Common.h"
#include "gSP.h"

void RDP_Unknown( u32 w0, u32 w1 )
{
#ifdef DEBUG
	DebugMsg( DEBUG_UNKNOWN, "RDP_Unknown\r\n" );
	DebugMsg( DEBUG_UNKNOWN, "\tUnknown RDP opcode %02X\r\n", _SHIFTR( w0, 24, 8 ) );
#endif
}

void RDP_NoOp( u32 w0, u32 w1 )
{
    gSPNoOp();
}

void RDP_SetCImg( u32 w0, u32 w1 )
{
    gDPSetColorImage( _SHIFTR( w0, 21,  3 ),        // fmt
                      _SHIFTR( w0, 19,  2 ),        // siz
                      _SHIFTR( w0,  0, 12 ) + 1,    // width
                      w1 );                         // img
}

void RDP_SetZImg( u32 w0, u32 w1 )
{
    gDPSetDepthImage( w1 ); // img
}

void RDP_SetTImg( u32 w0, u32 w1 )
{
    gDPSetTextureImage( _SHIFTR( w0, 21,  3),       // fmt
                        _SHIFTR( w0, 19,  2 ),      // siz
                        _SHIFTR( w0,  0, 12 ) + 1,  // width
                        w1 );                       // img
}

void RDP_SetCombine( u32 w0, u32 w1 )
{
    gDPSetCombine( _SHIFTR( w0, 0, 24 ),    // muxs0
                   w1 );                    // muxs1
}

void RDP_SetEnvColor( u32 w0, u32 w1 )
{
    gDPSetEnvColor( _SHIFTR( w1, 24, 8 ),       // r
                    _SHIFTR( w1, 16, 8 ),       // g
                    _SHIFTR( w1,  8, 8 ),       // b
                    _SHIFTR( w1,  0, 8 ) );     // a
}

void RDP_SetPrimColor( u32 w0, u32 w1 )
{
    gDPSetPrimColor( _SHIFTL( w0,  8, 8 ),      // m
                     _SHIFTL( w0,  0, 8 ),      // l
                     _SHIFTR( w1, 24, 8 ),      // r
                     _SHIFTR( w1, 16, 8 ),      // g
                     _SHIFTR( w1,  8, 8 ),      // b
                     _SHIFTR( w1,  0, 8 ) );    // a

}

void RDP_SetBlendColor( u32 w0, u32 w1 )
{
    gDPSetBlendColor( _SHIFTR( w1, 24, 8 ),     // r
                      _SHIFTR( w1, 16, 8 ),     // g
                      _SHIFTR( w1,  8, 8 ),     // b
                      _SHIFTR( w1,  0, 8 ) );   // a
}

void RDP_SetFogColor( u32 w0, u32 w1 )
{
    gDPSetFogColor( _SHIFTR( w1, 24, 8 ),       // r
                    _SHIFTR( w1, 16, 8 ),       // g
                    _SHIFTR( w1,  8, 8 ),       // b
                    _SHIFTR( w1,  0, 8 ) );     // a
}

void RDP_SetFillColor( u32 w0, u32 w1 )
{
    gDPSetFillColor( w1 );
}

void RDP_FillRect( u32 w0, u32 w1 )
{
	const u32 ulx = _SHIFTR(w1, 14, 10);
	const u32 uly = _SHIFTR(w1, 2, 10);
	const u32 lrx = _SHIFTR(w0, 14, 10);
	const u32 lry = _SHIFTR(w0, 2, 10);
	if (lrx < ulx || lry < uly)
		return;
	gDPFillRectangle(ulx, uly, lrx, lry);
}

void RDP_SetTile( u32 w0, u32 w1 )
{

    gDPSetTile( _SHIFTR( w0, 21, 3 ),   // fmt
                _SHIFTR( w0, 19, 2 ),   // siz
                _SHIFTR( w0,  9, 9 ),   // line
                _SHIFTR( w0,  0, 9 ),   // tmem
                _SHIFTR( w1, 24, 3 ),   // tile
                _SHIFTR( w1, 20, 4 ),   // palette
                _SHIFTR( w1, 18, 2 ),   // cmt
                _SHIFTR( w1,  8, 2 ),   // cms
                _SHIFTR( w1, 14, 4 ),   // maskt
                _SHIFTR( w1,  4, 4 ),   // masks
                _SHIFTR( w1, 10, 4 ),   // shiftt
                _SHIFTR( w1,  0, 4 ) ); // shifts
}

void RDP_LoadTile( u32 w0, u32 w1 )
{
    gDPLoadTile( _SHIFTR( w1, 24,  3 ),     // tile
                 _SHIFTR( w0, 12, 12 ),     // uls
                 _SHIFTR( w0,  0, 12 ),     // ult
                 _SHIFTR( w1, 12, 12 ),     // lrs
                 _SHIFTR( w1,  0, 12 ) );   // lrt
}

static u32 lbw0, lbw1;
void RDP_LoadBlock( u32 w0, u32 w1 )
{
	lbw0 = w0;
	lbw1 = w1;
    gDPLoadBlock( _SHIFTR( w1, 24,  3 ),    // tile
                  _SHIFTR( w0, 12, 12 ),    // uls
                  _SHIFTR( w0,  0, 12 ),    // ult
                  _SHIFTR( w1, 12, 12 ),    // lrs
                  _SHIFTR( w1,  0, 12 ) );  // dxt
}

void RDP_RepeatLastLoadBlock()
{
	RDP_LoadBlock(lbw0, lbw1);
}

void RDP_SetTileSize( u32 w0, u32 w1 )
{
    gDPSetTileSize( _SHIFTR( w1, 24,  3 ),      // tile
                    _SHIFTR( w0, 12, 12 ),      // uls
                    _SHIFTR( w0,  0, 12 ),      // ult
                    _SHIFTR( w1, 12, 12 ),      // lrs
                    _SHIFTR( w1,  0, 12 ) );    // lrt
}

void RDP_LoadTLUT( u32 w0, u32 w1 )
{
    gDPLoadTLUT(  _SHIFTR( w1, 24,  3 ),    // tile
                  _SHIFTR( w0, 12, 12 ),    // uls
                  _SHIFTR( w0,  0, 12 ),    // ult
                  _SHIFTR( w1, 12, 12 ),    // lrs
                  _SHIFTR( w1,  0, 12 ) );  // lrt
}

void RDP_SetOtherMode( u32 w0, u32 w1 )
{
    gDPSetOtherMode( _SHIFTR( w0, 0, 24 ),  // mode0
                     w1 );                  // mode1
}

void RDP_SetPrimDepth( u32 w0, u32 w1 )
{
    gDPSetPrimDepth( _SHIFTR( w1, 16, 16 ),     // z
                     _SHIFTR( w1,  0, 16 ) );   // dz
}

void RDP_SetScissor( u32 w0, u32 w1 )
{
    gDPSetScissor( _SHIFTR( w1, 24, 2 ),                        // mode
                   _FIXED2FLOAT( _SHIFTR( w0, 12, 12 ), 2 ),    // ulx
                   _FIXED2FLOAT( _SHIFTR( w0,  0, 12 ), 2 ),    // uly
                   _FIXED2FLOAT( _SHIFTR( w1, 12, 12 ), 2 ),    // lrx
                   _FIXED2FLOAT( _SHIFTR( w1,  0, 12 ), 2 ) );  // lry
}

void RDP_SetConvert( u32 w0, u32 w1 )
{
    gDPSetConvert( _SHIFTR( w0, 13, 9 ),    // k0
                   _SHIFTR( w0,  4, 9 ),    // k1
                   _SHIFTL( w0,  5, 4 ) | _SHIFTR( w1, 27, 5 ), // k2
                   _SHIFTR( w1, 18, 9 ),    // k3
                   _SHIFTR( w1,  9, 9 ),    // k4
                   _SHIFTR( w1,  0, 9 ) );  // k5
}

void RDP_SetKeyR( u32 w0, u32 w1 )
{
    gDPSetKeyR( _SHIFTR( w1,  8,  8 ),      // cR
                _SHIFTR( w1,  0,  8 ),      // sR
                _SHIFTR( w1, 16, 12 ) );    // wR
}

void RDP_SetKeyGB( u32 w0, u32 w1 )
{
    gDPSetKeyGB( _SHIFTR( w1, 24,  8 ),     // cG
                 _SHIFTR( w1, 16,  8 ),     // sG
                 _SHIFTR( w0, 12, 12 ),     // wG
                 _SHIFTR( w1,  8,  8 ),     // cB
                 _SHIFTR( w1,  0,  8 ),     // SB
                 _SHIFTR( w0,  0, 12 ) );   // wB
}

void RDP_FullSync( u32 w0, u32 w1 )
{
    gDPFullSync();
}

void RDP_TileSync( u32 w0, u32 w1 )
{
   gDPTileSync();
}

void RDP_PipeSync( u32 w0, u32 w1 )
{
    gDPPipeSync();
}

void RDP_LoadSync( u32 w0, u32 w1 )
{
    gDPLoadSync();
}

#if 0
static void _getTexRectParams(u32 *w2, u32 *w3)
{
	if (RSP.bLLE) {
		*w2 = RDP.w2;
		*w3 = RDP.w3;
		return;
	}

	enum {
		gspTexRect,
		gdpTexRect,
		halfTexRect
	} texRectMode = gdpTexRect;

	const u32 cmd1 = (*(u32*)&RDRAM[RSP.PC[RSP.PCi] + 0]) >> 24;
	const u32 cmd2 = (*(u32*)&RDRAM[RSP.PC[RSP.PCi] + 8]) >> 24;
	if (cmd1 == G_RDPHALF_1) {
		if (cmd2 == G_RDPHALF_2)
			texRectMode = gspTexRect;
	} else if (cmd1 == 0xB3) {
		if (cmd2 == 0xB2)
			texRectMode = gspTexRect;
		else
			texRectMode = halfTexRect;
	} else if (cmd1 == 0xF1)
		texRectMode = halfTexRect;

	switch (texRectMode) {
	case gspTexRect:
		*w2 = *(u32*)&RDRAM[RSP.PC[RSP.PCi] + 4];
		RSP.PC[RSP.PCi] += 8;

		*w3 = *(u32*)&RDRAM[RSP.PC[RSP.PCi] + 4];
		RSP.PC[RSP.PCi] += 8;
		break;
	case gdpTexRect:
		*w2 = *(u32*)&RDRAM[RSP.PC[RSP.PCi] + 0];
		w3 = *(u32*)&RDRAM[RSP.PC[RSP.PCi] + 4];
		RSP.PC[RSP.PCi] += 8;
		break;
	case halfTexRect:
		*w2 = 0;
		w3 = *(u32*)&RDRAM[RSP.PC[RSP.PCi] + 4];
		RSP.PC[RSP.PCi] += 8;
		break;
	default:
		assert(false && "Unknown texrect mode");
	}
}
#endif

/* TODO/FIXME - update */
void RDP_TexRectFlip( u32 w0, u32 w1 )
{
	u32 w2, w3;
    w2 = *(u32*)&gfx_info.RDRAM[__RSP.PC[__RSP.PCi] + 4];
    __RSP.PC[__RSP.PCi] += 8;

    w3 = *(u32*)&gfx_info.RDRAM[__RSP.PC[__RSP.PCi] + 4];
    __RSP.PC[__RSP.PCi] += 8;

    gDPTextureRectangleFlip(
          _FIXED2FLOAT( _SHIFTR( w1, 12, 12 ), 2 ),          // ulx
          _FIXED2FLOAT( _SHIFTR( w1,  0, 12 ), 2 ),          // uly
          _FIXED2FLOAT( _SHIFTR( w0, 12, 12 ), 2 ),          // lrx
          _FIXED2FLOAT( _SHIFTR( w0,  0, 12 ), 2 ),          // lry
          _SHIFTR( w1, 24,  3 ),                             // tile
          _FIXED2FLOAT( (s16)_SHIFTR( w2, 16, 16 ), 5 ),     // s
          _FIXED2FLOAT( (s16)_SHIFTR( w2,  0, 16 ), 5 ),     // t
          _FIXED2FLOAT( (s16)_SHIFTR( w3, 16, 16 ), 10 ),    // dsdx
          _FIXED2FLOAT( (s16)_SHIFTR( w3,  0, 16 ), 10 ) );  // dsdy
}

/* TODO/FIXME - update */
void RDP_TexRect( u32 w0, u32 w1 )
{
	u32 w2, w3;
    w2 = *(u32*)&gfx_info.RDRAM[__RSP.PC[__RSP.PCi] + 4];
    __RSP.PC[__RSP.PCi] += 8;

    w3 = *(u32*)&gfx_info.RDRAM[__RSP.PC[__RSP.PCi] + 4];
    __RSP.PC[__RSP.PCi] += 8;

    gDPTextureRectangle( _FIXED2FLOAT( _SHIFTR( w1, 12, 12 ), 2 ),          // ulx
                         _FIXED2FLOAT( _SHIFTR( w1,  0, 12 ), 2 ),          // uly
                         _FIXED2FLOAT( _SHIFTR( w0, 12, 12 ), 2 ),          // lrx
                         _FIXED2FLOAT( _SHIFTR( w0,  0, 12 ), 2 ),          // lry
                         _SHIFTR( w1, 24,  3 ),                             // tile
                         _FIXED2FLOAT( (s16)_SHIFTR( w2, 16, 16 ), 5 ),     // s
                         _FIXED2FLOAT( (s16)_SHIFTR( w2,  0, 16 ), 5 ),     // t
                         _FIXED2FLOAT( (s16)_SHIFTR( w3, 16, 16 ), 10 ),    // dsdx
                         _FIXED2FLOAT( (s16)_SHIFTR( w3,  0, 16 ), 10 ) );  // dsdy
}


//Low Level RDP Drawing Commands:
void RDP_TriFill(u32 _w0, u32 _w1)
{
	gDPTriFill(_w0, _w1);
}

/* TODO/FIXME - remove? */
void RDP_TriFillZBuff(u32 w0, u32 w1)
{
    LOG(LOG_VERBOSE, "RSP_TRI_FILL_ZBUFF Command\n");
}

void RDP_TriTxtr(u32 _w0, u32 _w1)
{
	gDPTriTxtr(_w0, _w1);
}

void RDP_TriTxtrZBuff(u32 w0, u32 w1)
{
    LOG(LOG_VERBOSE, "RSP_TRI_TXTR_ZBUFF Command\n");
}

void RDP_TriShade(u32 _w0, u32 _w1)
{
	gDPTriShadeZ(_w0, _w1);
}

void RDP_TriShadeZBuff(u32 w0, u32 w1)
{
    LOG(LOG_VERBOSE, "RSP_TRI_SHADE_ZBUFF Command\n");
}

void RDP_TriShadeTxtr(u32 _w0, u32 _w1)
{
	gDPTriShadeTxtr(_w0, _w1);
}

void RDP_TriShadeTxtrZBuff(u32 _w0, u32 _w1)
{
	gDPTriShadeTxtrZ(_w0, _w1);
}

void RDP_Init()
{
   int i;
    // Initialize RDP commands to RDP_UNKNOWN
    for (i = 0xC8; i <= 0xCF; i++)
        GBI.cmd[i] = RDP_Unknown;

    // Initialize RDP commands to RDP_UNKNOWN
    for (i = 0xE4; i <= 0xFF; i++)
        GBI.cmd[i] = RDP_Unknown;

    // Set known GBI commands
    GBI.cmd[G_NOOP]             = RDP_NoOp;
    GBI.cmd[G_SETCIMG]          = RDP_SetCImg;
    GBI.cmd[G_SETZIMG]          = RDP_SetZImg;
    GBI.cmd[G_SETTIMG]          = RDP_SetTImg;
    GBI.cmd[G_SETCOMBINE]       = RDP_SetCombine;
    GBI.cmd[G_SETENVCOLOR]      = RDP_SetEnvColor;
    GBI.cmd[G_SETPRIMCOLOR]     = RDP_SetPrimColor;
    GBI.cmd[G_SETBLENDCOLOR]    = RDP_SetBlendColor;
    GBI.cmd[G_SETFOGCOLOR]      = RDP_SetFogColor;
    GBI.cmd[G_SETFILLCOLOR]     = RDP_SetFillColor;
    GBI.cmd[G_FILLRECT]         = RDP_FillRect;
    GBI.cmd[G_SETTILE]          = RDP_SetTile;
    GBI.cmd[G_LOADTILE]         = RDP_LoadTile;
    GBI.cmd[G_LOADBLOCK]        = RDP_LoadBlock;
    GBI.cmd[G_SETTILESIZE]      = RDP_SetTileSize;
    GBI.cmd[G_LOADTLUT]         = RDP_LoadTLUT;
    GBI.cmd[G_RDPSETOTHERMODE]  = RDP_SetOtherMode;
    GBI.cmd[G_SETPRIMDEPTH]     = RDP_SetPrimDepth;
    GBI.cmd[G_SETSCISSOR]       = RDP_SetScissor;
    GBI.cmd[G_SETCONVERT]       = RDP_SetConvert;
    GBI.cmd[G_SETKEYR]          = RDP_SetKeyR;
    GBI.cmd[G_SETKEYGB]         = RDP_SetKeyGB;
    GBI.cmd[G_RDPFULLSYNC]      = RDP_FullSync;
    GBI.cmd[G_RDPTILESYNC]      = RDP_TileSync;
    GBI.cmd[G_RDPPIPESYNC]      = RDP_PipeSync;
    GBI.cmd[G_RDPLOADSYNC]      = RDP_LoadSync;
    GBI.cmd[G_TEXRECTFLIP]      = RDP_TexRectFlip;
    GBI.cmd[G_TEXRECT]          = RDP_TexRect;

    GBI.cmd[G_RDPNOOP]          = RDP_NoOp;

    //Low Level RDP Drawing Commands:
    GBI.cmd[G_TRI_FILL]             = RDP_TriFill;
    GBI.cmd[G_TRI_FILL_ZBUFF]       = RDP_TriFillZBuff;
    GBI.cmd[G_TRI_TXTR]             = RDP_TriTxtr;
    GBI.cmd[G_TRI_TXTR_ZBUFF]       = RDP_TriTxtrZBuff;
    GBI.cmd[G_TRI_SHADE]            = RDP_TriShade;
    GBI.cmd[G_TRI_SHADE_TXTR]       = RDP_TriShadeTxtr;
    GBI.cmd[G_TRI_SHADE_TXTR_ZBUFF] = RDP_TriShadeTxtrZBuff;

}

