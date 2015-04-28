#include "N64.h"
#include "RSP.h"
#include "GBI.h"
#include "gDP.h"
#include "Types.h"
#include "Debug.h"
#include "Common.h"
#include "gSP.h"
#include "RDP.h"
#include <string.h>

RDPInfo __RDP;

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

static void _getTexRectParams(u32 *w2, u32 *w3)
{
   if (__RSP.bLLE)
   {
      *w2 = __RDP.w2;
      *w3 = __RDP.w3;
      return;
   }

   enum {
      gspTexRect,
      gdpTexRect,
      halfTexRect
   } texRectMode = gdpTexRect;

   const u32 cmd1 = (*(u32*)&gfx_info.RDRAM[__RSP.PC[__RSP.PCi] + 0]) >> 24;
   const u32 cmd2 = (*(u32*)&gfx_info.RDRAM[__RSP.PC[__RSP.PCi] + 8]) >> 24;
   if (cmd1 == G_RDPHALF_1)
   {
      if (cmd2 == G_RDPHALF_2)
         texRectMode = gspTexRect;
   }
   else if (cmd1 == 0xB3)
   {
      if (cmd2 == 0xB2)
         texRectMode = gspTexRect;
      else
         texRectMode = halfTexRect;
   }
   else if (cmd1 == 0xF1)
      texRectMode = halfTexRect;

   switch (texRectMode)
   {
      case gspTexRect:
         *w2 = *(u32*)&gfx_info.RDRAM[__RSP.PC[__RSP.PCi] + 4];
         __RSP.PC[__RSP.PCi] += 8;

         *w3 = *(u32*)&gfx_info.RDRAM[__RSP.PC[__RSP.PCi] + 4];
         __RSP.PC[__RSP.PCi] += 8;
         break;
      case gdpTexRect:
         *w2 = *(u32*)&gfx_info.RDRAM[__RSP.PC[__RSP.PCi] + 0];
         *w3 = *(u32*)&gfx_info.RDRAM[__RSP.PC[__RSP.PCi] + 4];
         __RSP.PC[__RSP.PCi] += 8;
         break;
      case halfTexRect:
         *w2 = 0;
         *w3 = *(u32*)&gfx_info.RDRAM[__RSP.PC[__RSP.PCi] + 4];
         __RSP.PC[__RSP.PCi] += 8;
         break;
      default:
#if 0
         assert(false && "Unknown texrect mode");
#endif
	break;
   }
}

void RDP_TexRectFlip( u32 w0, u32 w1 )
{
   u32 ulx, uly, lrx, lry;
	u32 w2, w3;
	_getTexRectParams(&w2, &w3);
	ulx = _SHIFTR(w1, 12, 12);
	uly = _SHIFTR(w1, 0, 12);
	lrx = _SHIFTR(w0, 12, 12);
	lry = _SHIFTR(w0, 0, 12);
	if ((lrx >> 2) < (ulx >> 2) || (lry >> 2) < (uly >> 2))
		return;
	gDPTextureRectangleFlip(
		_FIXED2FLOAT(ulx, 2),
		_FIXED2FLOAT(uly, 2),
		_FIXED2FLOAT(lrx, 2),
		_FIXED2FLOAT(lry, 2),
		_SHIFTR(w1, 24, 3),							// tile
		_FIXED2FLOAT((s16)_SHIFTR(w2, 16, 16), 5),	// s
		_FIXED2FLOAT((s16)_SHIFTR(w2, 0, 16), 5),	// t
		_FIXED2FLOAT((s16)_SHIFTR(w3, 16, 16), 10),	// dsdx
		_FIXED2FLOAT((s16)_SHIFTR(w3, 0, 16), 10));	// dsdy
}

void RDP_TexRect( u32 w0, u32 w1 )
{
   u32 ulx, uly, lrx, lry;
	u32 w2, w3;
	_getTexRectParams(&w2, &w3);
	ulx = _SHIFTR(w1, 12, 12);
	uly = _SHIFTR(w1,  0, 12);
	lrx = _SHIFTR(w0, 12, 12);
	lry = _SHIFTR(w0,  0, 12);
	if ((lrx >> 2) < (ulx >> 2) || (lry >> 2) < (uly >> 2))
		return;
	gDPTextureRectangle(
		_FIXED2FLOAT(ulx, 2),
		_FIXED2FLOAT(uly, 2),
		_FIXED2FLOAT(lrx, 2),
		_FIXED2FLOAT(lry, 2),
		_SHIFTR(w1, 24, 3),							// tile
		_FIXED2FLOAT((s16)_SHIFTR(w2, 16, 16), 5),	// s
		_FIXED2FLOAT((s16)_SHIFTR(w2, 0, 16), 5),	// t
		_FIXED2FLOAT((s16)_SHIFTR(w3, 16, 16), 10),	// dsdx
		_FIXED2FLOAT((s16)_SHIFTR(w3, 0, 16), 10));	// dsdy
}


//Low Level RDP Drawing Commands:
void RDP_TriFill(u32 _w0, u32 _w1)
{
	gDPTriFill(_w0, _w1);
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

void RDP_TriFillZ( u32 _w0, u32 _w1 )
{
	gDPTriFillZ(_w0, _w1);
}

void RDP_TriShadeTxtrZBuff(u32 _w0, u32 _w1)
{
	gDPTriShadeTxtrZ(_w0, _w1);
}

void RDP_TriShadeZ( u32 _w0, u32 _w1 )
{
	gDPTriShadeZ(_w0, _w1);
}

void RDP_TriTxtrZ( u32 _w0, u32 _w1 )
{
	gDPTriTxtrZ(_w0, _w1);
}

void RDP_TriShadeTxtrZ( u32 _w0, u32 _w1 )
{
	gDPTriShadeTxtrZ(_w0, _w1);
}

void RDP_Init(void)
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

   __RDP.w2 = __RDP.w3 = 0;
   __RDP.cmd_ptr = __RDP.cmd_cur = 0;
}

static
GBIFunc LLEcmd[64] = {
	/* 0x00 */
	RDP_NoOp,			RDP_Unknown,		RDP_Unknown,		RDP_Unknown,
	RDP_Unknown,		RDP_Unknown,		RDP_Unknown,		RDP_Unknown,
	RDP_TriFill,		RDP_TriFillZ,		RDP_TriTxtr,		RDP_TriTxtrZ,
	RDP_TriShade,		RDP_TriShadeZ,		RDP_TriShadeTxtr,	RDP_TriShadeTxtrZ,
	/* 0x10 */
	RDP_Unknown,		RDP_Unknown,		RDP_Unknown,		RDP_Unknown,
	RDP_Unknown,		RDP_Unknown,		RDP_Unknown,		RDP_Unknown,
	RDP_Unknown,		RDP_Unknown,		RDP_Unknown,		RDP_Unknown,
	RDP_Unknown,		RDP_Unknown,		RDP_Unknown,		RDP_Unknown,
	/* 0x20 */
	RDP_Unknown,		RDP_Unknown,		RDP_Unknown,		RDP_Unknown,
	RDP_TexRect,		RDP_TexRectFlip,	RDP_LoadSync,		RDP_PipeSync,
	RDP_TileSync,		RDP_FullSync,		RDP_SetKeyGB,		RDP_SetKeyR,
	RDP_SetConvert,		RDP_SetScissor,		RDP_SetPrimDepth,	RDP_SetOtherMode,
	/* 0x30 */
	RDP_LoadTLUT,		RDP_Unknown,		RDP_SetTileSize,	RDP_LoadBlock,
	RDP_LoadTile,		RDP_SetTile,		RDP_FillRect,		RDP_SetFillColor,
	RDP_SetFogColor,	RDP_SetBlendColor,	RDP_SetPrimColor,	RDP_SetEnvColor,
	RDP_SetCombine,		RDP_SetTImg,		RDP_SetZImg,		RDP_SetCImg
};

static
const u32 CmdLength[64] =
{
	8,                      // 0x00, No Op
	8,                      // 0x01, ???
	8,                      // 0x02, ???
	8,                      // 0x03, ???
	8,                      // 0x04, ???
	8,                      // 0x05, ???
	8,                      // 0x06, ???
	8,                      // 0x07, ???
	32,                     // 0x08, Non-Shaded Triangle
	32+16,          // 0x09, Non-Shaded, Z-Buffered Triangle
	32+64,          // 0x0a, Textured Triangle
	32+64+16,       // 0x0b, Textured, Z-Buffered Triangle
	32+64,          // 0x0c, Shaded Triangle
	32+64+16,       // 0x0d, Shaded, Z-Buffered Triangle
	32+64+64,       // 0x0e, Shaded+Textured Triangle
	32+64+64+16,// 0x0f, Shaded+Textured, Z-Buffered Triangle
	8,                      // 0x10, ???
	8,                      // 0x11, ???
	8,                      // 0x12, ???
	8,                      // 0x13, ???
	8,                      // 0x14, ???
	8,                      // 0x15, ???
	8,                      // 0x16, ???
	8,                      // 0x17, ???
	8,                      // 0x18, ???
	8,                      // 0x19, ???
	8,                      // 0x1a, ???
	8,                      // 0x1b, ???
	8,                      // 0x1c, ???
	8,                      // 0x1d, ???
	8,                      // 0x1e, ???
	8,                      // 0x1f, ???
	8,                      // 0x20, ???
	8,                      // 0x21, ???
	8,                      // 0x22, ???
	8,                      // 0x23, ???
	16,                     // 0x24, Texture_Rectangle
	16,                     // 0x25, Texture_Rectangle_Flip
	8,                      // 0x26, Sync_Load
	8,                      // 0x27, Sync_Pipe
	8,                      // 0x28, Sync_Tile
	8,                      // 0x29, Sync_Full
	8,                      // 0x2a, Set_Key_GB
	8,                      // 0x2b, Set_Key_R
	8,                      // 0x2c, Set_Convert
	8,                      // 0x2d, Set_Scissor
	8,                      // 0x2e, Set_Prim_Depth
	8,                      // 0x2f, Set_Other_Modes
	8,                      // 0x30, Load_TLUT
	8,                      // 0x31, ???
	8,                      // 0x32, Set_Tile_Size
	8,                      // 0x33, Load_Block
	8,                      // 0x34, Load_Tile
	8,                      // 0x35, Set_Tile
	8,                      // 0x36, Fill_Rectangle
	8,                      // 0x37, Set_Fill_Color
	8,                      // 0x38, Set_Fog_Color
	8,                      // 0x39, Set_Blend_Color
	8,                      // 0x3a, Set_Prim_Color
	8,                      // 0x3b, Set_Env_Color
	8,                      // 0x3c, Set_Combine
	8,                      // 0x3d, Set_Texture_Image
	8,                      // 0x3e, Set_Mask_Image
	8                       // 0x3f, Set_Color_Image
};

void RDP_Half_1( u32 _c )
{
	u32 w0 = 0, w1 = _c;
	u32 cmd = _SHIFTR( _c, 24, 8 );
	if (cmd >= 0xc8 && cmd <=0xcf)
   {
      /* triangle command */
#ifdef DEBUG
		DebugMsg( DEBUG_HIGH | DEBUG_HANDLED, "gDPHalf_1 LLE Triangle\n");
#endif
		__RDP.cmd_ptr = 0;
		__RDP.cmd_cur = 0;
		do
      {
         __RDP.cmd_data[__RDP.cmd_ptr++] = w1;
         RSP_CheckDLCounter();

         w0 = *(u32*)&gfx_info.RDRAM[__RSP.PC[__RSP.PCi]];
         w1 = *(u32*)&gfx_info.RDRAM[__RSP.PC[__RSP.PCi] + 4];
         __RSP.cmd = _SHIFTR( w0, 24, 8 );

#ifdef DEBUG
         DebugRSPState( __RSP.PCi, __RSP.PC[__RSP.PCi], _SHIFTR( w0, 24, 8 ), w0, w1 );
         DebugMsg( DEBUG_LOW | DEBUG_HANDLED, "0x%08lX: CMD=0x%02lX W0=0x%08lX W1=0x%08lX\n", RSP.PC[RSP.PCi], _SHIFTR( w0, 24, 8 ), w0, w1 );
#endif

         __RSP.PC[__RSP.PCi] += 8;
         // RSP.nextCmd = _SHIFTR( *(u32*)&gfx_info.RDRAM[RSP.PC[RSP.PCi]], 24, 8 );
      } while (__RSP.cmd != 0xb3);
		__RDP.cmd_data[__RDP.cmd_ptr++] = w1;
		__RSP.cmd = (__RDP.cmd_data[__RDP.cmd_cur] >> 24) & 0x3f;
		w0 = __RDP.cmd_data[__RDP.cmd_cur+0];
		w1 = __RDP.cmd_data[__RDP.cmd_cur+1];
		LLEcmd[__RSP.cmd](w0, w1);
	}
#ifdef DEBUG
   else
   {
		DebugMsg( DEBUG_HIGH | DEBUG_IGNORED, "gDPHalf_1()\n" );
	}
#endif
}

static inline u32 GLN64_READ_RDP_DATA(u32 address)
{
	if ((*(u32*)gfx_info.DPC_STATUS_REG) & 0x1)          // XBUS_DMEM_DMA enabled
		return gfx_info.DMEM[(address & 0xfff)>>2];
   return gfx_info.RDRAM[address>>2];
}

EXPORT void CALL gln64ProcessRDPList(void)
{
   u32 i;
   const u32 length = gfx_info.DPC_END_REG - gfx_info.DPC_CURRENT_REG;

#ifdef DEBUG
   DebugMsg(DEBUG_HIGH | DEBUG_HANDLED, "ProcessRDPList()\n");
#endif

   (*(u32*)gfx_info.DPC_STATUS_REG) &= ~0x0002;

   if (gfx_info.DPC_END_REG <= gfx_info.DPC_CURRENT_REG)
      return;

   __RSP.bLLE = true;

   /* load command data */
   for (i = 0; i < length; i += 4)
   {
      __RDP.cmd_data[__RDP.cmd_ptr] = GLN64_READ_RDP_DATA(*gfx_info.DPC_CURRENT_REG + i);
      __RDP.cmd_ptr = (__RDP.cmd_ptr + 1) & maxCMDMask;
   }

   bool setZero = true;
   while (__RDP.cmd_cur != __RDP.cmd_ptr)
   {
      u32 cmd = (__RDP.cmd_data[__RDP.cmd_cur] >> 24) & 0x3f;

      if ((((__RDP.cmd_ptr - __RDP.cmd_cur)&maxCMDMask) * 4) < CmdLength[cmd])
      {
         setZero = false;
         break;
      }

      if (__RDP.cmd_cur + CmdLength[cmd] / 4 > MAXCMD)
         memcpy(__RDP.cmd_data + MAXCMD, __RDP.cmd_data, CmdLength[cmd] - (MAXCMD - __RDP.cmd_cur) * 4);

      // execute the command
      u32 w0 = __RDP.cmd_data[__RDP.cmd_cur+0];
      u32 w1 = __RDP.cmd_data[__RDP.cmd_cur+1];
      __RDP.w2 = __RDP.cmd_data[__RDP.cmd_cur+2];
      __RDP.w3 = __RDP.cmd_data[__RDP.cmd_cur + 3];
      __RSP.cmd = cmd;
      LLEcmd[cmd](w0, w1);

      __RDP.cmd_cur = (__RDP.cmd_cur + CmdLength[cmd] / 4) & maxCMDMask;
   }

   if (setZero) {
      __RDP.cmd_ptr = 0;
      __RDP.cmd_cur = 0;
   }

   __RSP.bLLE = false;
   gSP.changed |= CHANGED_COLORBUFFER;

   gfx_info.DPC_START_REG = gfx_info.DPC_CURRENT_REG = gfx_info.DPC_END_REG;
}
