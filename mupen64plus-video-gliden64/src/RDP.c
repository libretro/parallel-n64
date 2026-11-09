#include <assert.h>
#include <string.h>
#include <retro_inline.h>
#include "N64.h"
#include "RSP.h"
#include "RDP.h"
#include "GBI.h"
#include "gDP.h"
#include "gSP.h"
#include "Config.h"

static
void RDP_Unknown(u32 w0, u32 w1 )
{
}

static
void RDP_NoOp(u32 w0, u32 w1 )
{
	gli_gDPNoOp();
}

static
void RDP_SetCImg(u32 w0, u32 w1 )
{
	gli_gDPSetColorImage( _SHIFTR( w0, 21,  3 ),		/* fmt */
					  _SHIFTR( w0, 19,  2 ),		/* siz */
					  _SHIFTR( w0,  0, 12 ) + 1,	/* width */
					  w1 );							/* img */
}

static
void RDP_SetZImg(u32 w0, u32 w1 )
{
	gli_gDPSetDepthImage( w1 );	/* img */
}

void gli_RDP_SetTImg( u32 w0, u32 w1 )
{
	gli_gDPSetTextureImage( _SHIFTR( w0, 21,  3),		/* fmt */
						_SHIFTR( w0, 19,  2 ),		/* siz */
						_SHIFTR( w0,  0, 12 ) + 1,	/* width */
						w1 );						/* img */
}

static
void RDP_SetCombine(u32 w0, u32 w1 )
{
	gli_gDPSetCombine( _SHIFTR( w0, 0, 24 ),	/* muxs0 */
				   w1 );					/* muxs1 */
}

static
void RDP_SetEnvColor(u32 w0, u32 w1 )
{
	gli_gDPSetEnvColor( _SHIFTR( w1, 24, 8 ),		/* r */
					_SHIFTR( w1, 16, 8 ),		/* g */
					_SHIFTR( w1,  8, 8 ),		/* b */
					_SHIFTR( w1,  0, 8 ) );		/* a */
}

static
void RDP_SetPrimColor(u32 w0, u32 w1 )
{
	gli_gDPSetPrimColor( _SHIFTR( w0,  8, 5 ),		/* m */
					 _SHIFTR( w0,  0, 8 ),		/* l */
					 _SHIFTR( w1, 24, 8 ),		/* r */
					 _SHIFTR( w1, 16, 8 ),		/* g */
					 _SHIFTR( w1,  8, 8 ),		/* b */
					 _SHIFTR( w1,  0, 8 ) );	/* a */

}

static
void RDP_SetBlendColor(u32 w0, u32 w1 )
{
	gli_gDPSetBlendColor( _SHIFTR( w1, 24, 8 ),		/* r */
					  _SHIFTR( w1, 16, 8 ),		/* g */
					  _SHIFTR( w1,  8, 8 ),		/* b */
					  _SHIFTR( w1,  0, 8 ) );	/* a */
}

static
void RDP_SetFogColor(u32 w0, u32 w1 )
{
	gli_gDPSetFogColor( _SHIFTR( w1, 24, 8 ),		/* r */
					_SHIFTR( w1, 16, 8 ),		/* g */
					_SHIFTR( w1,  8, 8 ),		/* b */
					_SHIFTR( w1,  0, 8 ) );		/* a */
}

static
void RDP_SetFillColor(u32 w0, u32 w1 )
{
	gli_gDPSetFillColor( w1 );
}

static
void RDP_FillRect(u32 w0, u32 w1 )
{
	const u32 ulx = _SHIFTR(w1, 14, 10);
	const u32 uly = _SHIFTR(w1, 2, 10);
	const u32 lrx = _SHIFTR(w0, 14, 10);
	const u32 lry = _SHIFTR(w0, 2, 10);
	if (lrx < ulx || lry < uly)
		return;
	gli_gDPFillRectangle(ulx, uly, lrx, lry);
}

void gli_RDP_SetTile( u32 w0, u32 w1 )
{
	gli_gDPSetTile( _SHIFTR( w0, 21, 3 ),	/* fmt */
				_SHIFTR( w0, 19, 2 ),	/* siz */
				_SHIFTR( w0,  9, 9 ),	/* line */
				_SHIFTR( w0,  0, 9 ),	/* tmem */
				_SHIFTR( w1, 24, 3 ),	/* tile */
				_SHIFTR( w1, 20, 4 ),	/* palette */
				_SHIFTR( w1, 18, 2 ),	/* cmt */
				_SHIFTR( w1,  8, 2 ),	/* cms */
				_SHIFTR( w1, 14, 4 ),	/* maskt */
				_SHIFTR( w1,  4, 4 ),	/* masks */
				_SHIFTR( w1, 10, 4 ),	/* shiftt */
				_SHIFTR( w1,  0, 4 ) );	/* shifts */
}

static
void RDP_LoadTile(u32 w0, u32 w1 )
{
	gli_gDPLoadTile( _SHIFTR( w1, 24,  3 ),		/* tile */
				 _SHIFTR( w0, 12, 12 ),		/* uls */
				 _SHIFTR( w0,  0, 12 ),		/* ult */
				 _SHIFTR( w1, 12, 12 ),		/* lrs */
				 _SHIFTR( w1,  0, 12 ) );	/* lrt */
}

static u32 lbw0, lbw1;
void gli_RDP_LoadBlock( u32 w0, u32 w1 )
{
	lbw0 = w0;
	lbw1 = w1;
	gli_gDPLoadBlock( _SHIFTR( w1, 24,  3 ),	/* tile */
				  _SHIFTR( w0, 12, 12 ),	/* uls */
				  _SHIFTR( w0,  0, 12 ),	/* ult */
				  _SHIFTR( w1, 12, 12 ),	/* lrs */
				  _SHIFTR( w1,  0, 12 ) );	/* dxt */
}

void gli_RDP_RepeatLastLoadBlock(void)
{
	gli_RDP_LoadBlock(lbw0, lbw1);
}

void gli_RDP_SetTileSize( u32 w0, u32 w1 )
{
	gli_gDPSetTileSize( _SHIFTR( w1, 24,  3 ),		/* tile */
					_SHIFTR( w0, 12, 12 ),		/* uls */
					_SHIFTR( w0,  0, 12 ),		/* ult */
					_SHIFTR( w1, 12, 12 ),		/* lrs */
					_SHIFTR( w1,  0, 12 ) );	/* lrt */
}

static
void RDP_LoadTLUT(u32 w0, u32 w1 )
{
	gli_gDPLoadTLUT( _SHIFTR( w1, 24,  3 ),	/* tile */
				  _SHIFTR( w0, 12, 12 ),	/* uls */
				  _SHIFTR( w0,  0, 12 ),	/* ult */
				  _SHIFTR( w1, 12, 12 ),	/* lrs */
				  _SHIFTR( w1,  0, 12 ) );	/* lrt */
}

static
void RDP_SetOtherMode(u32 w0, u32 w1 )
{
	gli_gDPSetOtherMode( _SHIFTR( w0, 0, 24 ),	/* mode0 */
					 w1 );					/* mode1 */
}

static
void RDP_SetPrimDepth(u32 w0, u32 w1 )
{
	gli_gDPSetPrimDepth( _SHIFTR( w1, 16, 16 ),		/* z */
					 _SHIFTR( w1,  0, 16 ) );	/* dz */
}

void gli_RDP_SetScissor( u32 w0, u32 w1 )
{
	gli_gDPSetScissor( _SHIFTR( w1, 24,  2 ),	/* mode */
				   _SHIFTR( w0, 12, 12 ),	/* ulx */
				   _SHIFTR( w0,  0, 12 ),	/* uly */
				   _SHIFTR( w1, 12, 12 ),	/* lrx */
				   _SHIFTR( w1,  0, 12 ) );	/* lry */
}

static
void RDP_SetConvert(u32 w0, u32 w1 )
{
	gli_gDPSetConvert( _SHIFTR( w0, 13, 9 ),	/* k0 */
				   _SHIFTR( w0,  4, 9 ),	/* k1 */
				   _SHIFTL( w0,  5, 4 ) | _SHIFTR( w1, 27, 5 ),	/* k2 */
				   _SHIFTR( w1, 18, 9 ),	/* k3 */
				   _SHIFTR( w1,  9, 9 ),	/* k4 */
				   _SHIFTR( w1,  0, 9 ) );	/* k5 */
}

static
void RDP_SetKeyR(u32 w0, u32 w1 )
{
	gli_gDPSetKeyR( _SHIFTR( w1,  8,  8 ),		/* cR */
				_SHIFTR( w1,  0,  8 ),		/* sR */
				_SHIFTR( w1, 16, 12 ) );	/* wR */
}

static
void RDP_SetKeyGB(u32 w0, u32 w1 )
{
	gli_gDPSetKeyGB( _SHIFTR( w1, 24,  8 ),		/* cG */
				 _SHIFTR( w1, 16,  8 ),		/* sG */
				 _SHIFTR( w0, 12, 12 ),		/* wG */
				 _SHIFTR( w1,  8,  8 ),		/* cB */
				 _SHIFTR( w1,  0,  8 ),		/* SB */
				 _SHIFTR( w0,  0, 12 ) );	/* wB */
}

static
void RDP_FullSync(u32 w0, u32 w1 )
{
	gli_gDPFullSync();
}

static
void RDP_TileSync(u32 w0, u32 w1 )
{
	gli_gDPTileSync();
}

static
void RDP_PipeSync(u32 w0, u32 w1 )
{
	gli_gDPPipeSync();
}

static
void RDP_LoadSync(u32 w0, u32 w1 )
{
	gli_gDPLoadSync();
}

static
bool _getTexRectParams(u32 * _w2, u32 * _w3)
{
	enum {
		gspTexRect,
		gdpTexRect,
		halfTexRect
	} texRectMode;
	u32 cmd1;
	u32 cmd2;
	if (RSP.LLE) {
		*_w2 = RDP.w2;
		*_w3 = RDP.w3;
		return true;
	}

	texRectMode = gdpTexRect;

	cmd1 = (*(u32*)&RDRAM[RSP.PC[RSP.PCi] + 0]) >> 24;
	cmd2 = (*(u32*)&RDRAM[RSP.PC[RSP.PCi] + 8]) >> 24;
	if (cmd1 == G_RDPHALF_1) {
		if (cmd2 == G_RDPHALF_2)
			texRectMode = gspTexRect;
	} else if (cmd1 == 0xB3) {
		texRectMode = halfTexRect;
	} else if (cmd1 == 0xF1)
		texRectMode = halfTexRect;

	switch (texRectMode) {
	case gspTexRect:
		*_w2 = *(u32*)&RDRAM[RSP.PC[RSP.PCi] + 4];
		RSP.PC[RSP.PCi] += 8;

		*_w3 = *(u32*)&RDRAM[RSP.PC[RSP.PCi] + 4];
		RSP.PC[RSP.PCi] += 8;
		break;
	case gdpTexRect:
		if ((config.generalEmulation.hacks & hack_WinBack) != 0) {
			RSP.PC[RSP.PCi] += 8;
			return false;
		}
		{
			const u32 ucode = gli_GBI->type;
			if (ucode == F5Rogue || ucode == F5Indi_Naboo) {
				*_w2 = *(u32*)&RDRAM[RSP.PC[RSP.PCi] + 8];
				*_w3 = *(u32*)&RDRAM[RSP.PC[RSP.PCi] + 12];
				RSP.PC[RSP.PCi] += 8;
				return true;
			}
		}
		*_w2 = *(u32*)&RDRAM[RSP.PC[RSP.PCi] + 0];
		*_w3 = *(u32*)&RDRAM[RSP.PC[RSP.PCi] + 4];
		RSP.PC[RSP.PCi] += 8;
		break;
	case halfTexRect:
		*_w2 = 0;
		*_w3 = *(u32*)&RDRAM[RSP.PC[RSP.PCi] + 4];
		RSP.PC[RSP.PCi] += 8;
		break;
	default:
		assert(false && "Unknown texrect mode");
	}
	return true;
}

static
void _TexRect( u32 w0, u32 w1, bool flip )
{
	s32 ulx;
	s32 uly;
	s32 lrx;
	s32 lry;
	u32 w2, w3;
	if (!_getTexRectParams(&w2, &w3))
		return;
	RDP.w0 = w0;
	RDP.w1 = w1;
	ulx = _SHIFTR(w1, 12, 12);
	uly = _SHIFTR(w1, 0, 12);
	lrx = _SHIFTR(w0, 12, 12);
	lry = _SHIFTR(w0, 0, 12);
	if (lrx < ulx || lry < uly)
		return;
	if (gDP.otherMode.cycleType == G_CYC_COPY)
		gli_gDPTextureRectangle(
			(f32)(ulx >> 2),
			(f32)(uly >> 2),
			(f32)(lrx >> 2),
			(f32)(lry >> 2),
			_SHIFTR(w1, 24, 3),							/* tile */
			(s16)_SHIFTR(w2, 16, 16),					/* s */
			(s16)_SHIFTR(w2, 0, 16),					/* t */
			_FIXED2FLOAT((s16)_SHIFTR(w3, 16, 16), 10),	/* dsdx */
			_FIXED2FLOAT((s16)_SHIFTR(w3, 0, 16), 10),	/* dsdy */
			flip);
	else
		gli_gDPTextureRectangle(
			_FIXED2FLOAT(ulx, 2),
			_FIXED2FLOAT(uly, 2),
			_FIXED2FLOAT(lrx, 2),
			_FIXED2FLOAT(lry, 2),
			_SHIFTR(w1, 24, 3),							/* tile */
			(s16)_SHIFTR(w2, 16, 16),					/* s */
			(s16)_SHIFTR(w2, 0, 16),					/* t */
			_FIXED2FLOAT((s16)_SHIFTR(w3, 16, 16), 10),	/* dsdx */
			_FIXED2FLOAT((s16)_SHIFTR(w3, 0, 16), 10),	/* dsdy */
			flip);
}

static
void RDP_TexRectFlip(u32 w0, u32 w1 )
{
	_TexRect(w0, w1, true);
}

void gli_RDP_TexRect( u32 w0, u32 w1 )
{
	_TexRect(w0, w1, false);
}

static
void RDP_TriFill(u32 _w0, u32 _w1 )
{
	gli_gDPTriFill(_w0, _w1);
}

static
void RDP_TriShade(u32 _w0, u32 _w1 )
{
	gli_gDPTriShade(_w0, _w1);
}

static
void RDP_TriTxtr(u32 _w0, u32 _w1 )
{
	gli_gDPTriTxtr(_w0, _w1);
}

static
void RDP_TriShadeTxtr(u32 _w0, u32 _w1 )
{
	gli_gDPTriShadeTxtr(_w0, _w1);
}

static
void RDP_TriFillZ(u32 _w0, u32 _w1 )
{
	gli_gDPTriFillZ(_w0, _w1);
}

static
void RDP_TriShadeZ(u32 _w0, u32 _w1 )
{
	gli_gDPTriShadeZ(_w0, _w1);
}

static
void RDP_TriTxtrZ(u32 _w0, u32 _w1 )
{
	gli_gDPTriTxtrZ(_w0, _w1);
}

static
void RDP_TriShadeTxtrZ(u32 _w0, u32 _w1 )
{
	gli_gDPTriShadeTxtrZ(_w0, _w1);
}

RDPInfo RDP;

void gli_RDP_Init(void)
{
	int i;
	/* Initialize RDP commands to RDP_UNKNOWN */
	for (i = 0xC8; i <= 0xCF; i++)
		gli_GBIcmd[i] = RDP_Unknown;

	/* Initialize RDP commands to RDP_UNKNOWN */
	for (i = 0xE4; i <= 0xFF; i++)
		gli_GBIcmd[i] = RDP_Unknown;

	/* Set known GBI commands */
	gli_GBIcmd[G_NOOP]				= RDP_NoOp;
	gli_GBIcmd[G_SETCIMG]			= RDP_SetCImg;
	gli_GBIcmd[G_SETZIMG]			= RDP_SetZImg;
	gli_GBIcmd[G_SETTIMG]			= gli_RDP_SetTImg;
	gli_GBIcmd[G_SETCOMBINE]		= RDP_SetCombine;
	gli_GBIcmd[G_SETENVCOLOR]		= RDP_SetEnvColor;
	gli_GBIcmd[G_SETPRIMCOLOR]		= RDP_SetPrimColor;
	gli_GBIcmd[G_SETBLENDCOLOR]	= RDP_SetBlendColor;
	gli_GBIcmd[G_SETFOGCOLOR]		= RDP_SetFogColor;
	gli_GBIcmd[G_SETFILLCOLOR]		= RDP_SetFillColor;
	gli_GBIcmd[G_FILLRECT]			= RDP_FillRect;
	gli_GBIcmd[G_SETTILE]			= gli_RDP_SetTile;
	gli_GBIcmd[G_LOADTILE]			= RDP_LoadTile;
	gli_GBIcmd[G_LOADBLOCK]		= gli_RDP_LoadBlock;
	gli_GBIcmd[G_SETTILESIZE]		= gli_RDP_SetTileSize;
	gli_GBIcmd[G_LOADTLUT]			= RDP_LoadTLUT;
	gli_GBIcmd[G_RDPSETOTHERMODE]	= RDP_SetOtherMode;
	gli_GBIcmd[G_SETPRIMDEPTH]		= RDP_SetPrimDepth;
	gli_GBIcmd[G_SETSCISSOR]		= gli_RDP_SetScissor;
	gli_GBIcmd[G_SETCONVERT]		= RDP_SetConvert;
	gli_GBIcmd[G_SETKEYR]			= RDP_SetKeyR;
	gli_GBIcmd[G_SETKEYGB]			= RDP_SetKeyGB;
	gli_GBIcmd[G_RDPFULLSYNC]		= RDP_FullSync;
	gli_GBIcmd[G_RDPTILESYNC]		= RDP_TileSync;
	gli_GBIcmd[G_RDPPIPESYNC]		= RDP_PipeSync;
	gli_GBIcmd[G_RDPLOADSYNC]		= RDP_LoadSync;
	gli_GBIcmd[G_TEXRECTFLIP]		= RDP_TexRectFlip;
	gli_GBIcmd[G_TEXRECT]			= gli_RDP_TexRect;
	gli_GBIcmd[G_RDPNOOP]			= RDP_NoOp;

	RDP.w0 = RDP.w1 = RDP.w2 = RDP.w3 = 0;
	RDP.cmd_ptr = RDP.cmd_cur = 0;
	RDP.cmd_lend = RDP.cmd_data;
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
	gli_RDP_TexRect,		RDP_TexRectFlip,	RDP_LoadSync,		RDP_PipeSync,
	RDP_TileSync,		RDP_FullSync,		RDP_SetKeyGB,		RDP_SetKeyR,
	RDP_SetConvert,		gli_RDP_SetScissor,		RDP_SetPrimDepth,	RDP_SetOtherMode,
	/* 0x30 */
	RDP_LoadTLUT,		RDP_Unknown,		gli_RDP_SetTileSize,	gli_RDP_LoadBlock,
	RDP_LoadTile,		gli_RDP_SetTile,		RDP_FillRect,		RDP_SetFillColor,
	RDP_SetFogColor,	RDP_SetBlendColor,	RDP_SetPrimColor,	RDP_SetEnvColor,
	RDP_SetCombine,		gli_RDP_SetTImg,		RDP_SetZImg,		RDP_SetCImg
};

static
const u32 CmdLength[64] =
{
	8,                      /* 0x00, No Op */
	8,                      /* 0x01, ??? */
	8,                      /* 0x02, ??? */
	8,                      /* 0x03, ??? */
	8,                      /* 0x04, ??? */
	8,                      /* 0x05, ??? */
	8,                      /* 0x06, ??? */
	8,                      /* 0x07, ??? */
	32,                     /* 0x08, Non-Shaded Triangle */
	32+16,          /* 0x09, Non-Shaded, Z-Buffered Triangle */
	32+64,          /* 0x0a, Textured Triangle */
	32+64+16,       /* 0x0b, Textured, Z-Buffered Triangle */
	32+64,          /* 0x0c, Shaded Triangle */
	32+64+16,       /* 0x0d, Shaded, Z-Buffered Triangle */
	32+64+64,       /* 0x0e, Shaded+Textured Triangle */
	32+64+64+16,/* 0x0f, Shaded+Textured, Z-Buffered Triangle */
	8,                      /* 0x10, ??? */
	8,                      /* 0x11, ??? */
	8,                      /* 0x12, ??? */
	8,                      /* 0x13, ??? */
	8,                      /* 0x14, ??? */
	8,                      /* 0x15, ??? */
	8,                      /* 0x16, ??? */
	8,                      /* 0x17, ??? */
	8,                      /* 0x18, ??? */
	8,                      /* 0x19, ??? */
	8,                      /* 0x1a, ??? */
	8,                      /* 0x1b, ??? */
	8,                      /* 0x1c, ??? */
	8,                      /* 0x1d, ??? */
	8,                      /* 0x1e, ??? */
	8,                      /* 0x1f, ??? */
	8,                      /* 0x20, ??? */
	8,                      /* 0x21, ??? */
	8,                      /* 0x22, ??? */
	8,                      /* 0x23, ??? */
	16,                     /* 0x24, Texture_Rectangle */
	16,                     /* 0x25, Texture_Rectangle_Flip */
	8,                      /* 0x26, Sync_Load */
	8,                      /* 0x27, Sync_Pipe */
	8,                      /* 0x28, Sync_Tile */
	8,                      /* 0x29, Sync_Full */
	8,                      /* 0x2a, Set_Key_GB */
	8,                      /* 0x2b, Set_Key_R */
	8,                      /* 0x2c, Set_Convert */
	8,                      /* 0x2d, Set_Scissor */
	8,                      /* 0x2e, Set_Prim_Depth */
	8,                      /* 0x2f, Set_Other_Modes */
	8,                      /* 0x30, Load_TLUT */
	8,                      /* 0x31, ??? */
	8,                      /* 0x32, Set_Tile_Size */
	8,                      /* 0x33, Load_Block */
	8,                      /* 0x34, Load_Tile */
	8,                      /* 0x35, Set_Tile */
	8,                      /* 0x36, Fill_Rectangle */
	8,                      /* 0x37, Set_Fill_Color */
	8,                      /* 0x38, Set_Fog_Color */
	8,                      /* 0x39, Set_Blend_Color */
	8,                      /* 0x3a, Set_Prim_Color */
	8,                      /* 0x3b, Set_Env_Color */
	8,                      /* 0x3c, Set_Combine */
	8,                      /* 0x3d, Set_Texture_Image */
	8,                      /* 0x3e, Set_Mask_Image */
	8                       /* 0x3f, Set_Color_Image */
};

void gli_RDP_Half_1( u32 _c )
{
	u32 w0 = 0, w1 = _c;
	u32 cmd = _SHIFTR( _c, 24, 8 );
	if (cmd >= 0xc8 && cmd <=0xcf) {/*triangle command */
		RDP.cmd_ptr = 0;
		RDP.cmd_cur = 0;
		do {
			RDP.cmd_data[RDP.cmd_ptr++] = w1;
			gli_RSP_CheckDLCounter();

			w0 = *(u32*)&RDRAM[RSP.PC[RSP.PCi]];
			w1 = *(u32*)&RDRAM[RSP.PC[RSP.PCi] + 4];
			RSP.cmd = _SHIFTR( w0, 24, 8 );


			RSP.PC[RSP.PCi] += 8;
			/* RSP.nextCmd = _SHIFTR( *(u32*)&RDRAM[RSP.PC[RSP.PCi]], 24, 8 ); */
		} while (RSP.cmd != 0xb3);
		RDP.cmd_data[RDP.cmd_ptr++] = w1;
		RDP.cmd_lend = RDP.cmd_data + RDP.cmd_cur;
		RSP.cmd = (RDP.cmd_data[RDP.cmd_cur] >> 24) & 0x3f;
		w0 = RDP.cmd_data[RDP.cmd_cur+0];
		w1 = RDP.cmd_data[RDP.cmd_cur+1];
		LLEcmd[RSP.cmd](w0, w1);
	} else {
	}
}

#define rdram ((u32*)RDRAM)
#define rsp_dmem ((u32*)DMEM)

#define dp_start (*(u32*)REG.DPC_START)
#define dp_end (*(u32*)REG.DPC_END)
#define dp_current (*(u32*)REG.DPC_CURRENT)
#define dp_status (*(u32*)REG.DPC_STATUS)

static INLINE u32 READ_RDP_DATA(u32 address)
{
	if (dp_status & 0x1)          /* XBUS_DMEM_DMA enabled */
		return rsp_dmem[(address & 0xfff)>>2];
	else
		return rdram[(address & 0xffffff)>>2];
}

/* Runs the whole commands of a list straight from guest memory and keeps only a
 * trailing partial command in the ring, for the next list to complete. */
static
void RDP_ProcessLentList(const u32 * _cmd, u32 _words)
{
	while (_words != 0) {
		const u32 cmd = (_cmd[0] >> 24) & 0x3f;
		const u32 cmdWords = CmdLength[cmd] / 4;

		if (cmdWords > _words)
			break;

		RDP.w0 = _cmd[0];
		RDP.w1 = _cmd[1];
		RDP.w2 = _cmd[2];
		RDP.w3 = _cmd[3];
		RDP.cmd_lend = _cmd;
		RSP.cmd = cmd;
		LLEcmd[cmd](RDP.w0, RDP.w1);

		_cmd += cmdWords;
		_words -= cmdWords;
	}

	memcpy(RDP.cmd_data, _cmd, _words * sizeof(u32));
	RDP.cmd_cur = 0;
	RDP.cmd_ptr = _words;
}

void gli_RDP_ProcessRDPList(void)
{
	u32 length;
	u32 i;
	bool setZero;
	bool dmem;
	const u32 * base;
	u32 memSize;
	u32 offset;
	if (ConfigOpen) {
		dp_start = dp_current = dp_end;
		gli_gDPFullSync();
		return;
	}

	length = dp_end - dp_current;

	if (dp_end <= dp_current) return;

	RSP.LLE = true;

	dmem = (dp_status & 0x1) != 0;   /* XBUS_DMEM_DMA enabled */
	base = dmem ? rsp_dmem : rdram;
	memSize = dmem ? 0x1000 : RDRAMSize + 1;
	offset = dp_current & (dmem ? 0xfff : 0xffffff);
	setZero = true;

	/* 16: a command always reads four words, even a two word one */
	if (RDP.cmd_cur == RDP.cmd_ptr && offset + length + 16 <= memSize) {
		RDP_ProcessLentList(base + (offset >> 2), (length + 3) >> 2);
	} else {
		/* load command data */
		for (i = 0; i < length; i += 4) {
			RDP.cmd_data[RDP.cmd_ptr] = READ_RDP_DATA(dp_current + i);
			RDP.cmd_ptr = (RDP.cmd_ptr + 1) & maxCMDMask;
		}
	}

	while (RDP.cmd_cur != RDP.cmd_ptr) {
		u32 cmd = (RDP.cmd_data[RDP.cmd_cur] >> 24) & 0x3f;

		if ((((RDP.cmd_ptr - RDP.cmd_cur)&maxCMDMask) * 4) < CmdLength[cmd]) {
			setZero = false;
			break;
		}

		if (RDP.cmd_cur + CmdLength[cmd] / 4 > MAXCMD)
			memcpy(RDP.cmd_data + MAXCMD, RDP.cmd_data, CmdLength[cmd] - (MAXCMD - RDP.cmd_cur) * 4);

		/* execute the command */
		RDP.w0 = RDP.cmd_data[RDP.cmd_cur + 0];
		RDP.w1 = RDP.cmd_data[RDP.cmd_cur + 1];
		RDP.w2 = RDP.cmd_data[RDP.cmd_cur + 2];
		RDP.w3 = RDP.cmd_data[RDP.cmd_cur + 3];
		RDP.cmd_lend = RDP.cmd_data + RDP.cmd_cur;
		RSP.cmd = cmd;
		LLEcmd[cmd](RDP.w0, RDP.w1);

		RDP.cmd_cur = (RDP.cmd_cur + CmdLength[cmd] / 4) & maxCMDMask;
	}

	if (setZero) {
		RDP.cmd_ptr = 0;
		RDP.cmd_cur = 0;
	}

	RSP.LLE = false;
	gDP.changed |= CHANGED_COLORBUFFER;
	gDP.changed &= ~CHANGED_CPU_FB_WRITE;

	dp_start = dp_current = dp_end;
}
