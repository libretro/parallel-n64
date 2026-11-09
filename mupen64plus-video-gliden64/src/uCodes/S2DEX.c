#include <assert.h>
#include <retro_miscellaneous.h>
#include <retro_inline.h>
#include "S2DEX.h"
#include "F3D.h"
#include "F3DEX.h"
#include "GBI.h"
#include "gSP.h"
#include "gDP.h"
#include "RSP.h"
#include "RDP.h"

#include "Config.h"
#include "gli_drawer.h"
#include "gli_s2dex.h"

#define SWAP_F32(_a, _b) do { f32 _t = (_a); (_a) = (_b); (_b) = _t; } while (0)

#define S2DEX_MV_MATRIX			0
#define S2DEX_MV_SUBMUTRIX		2
#define S2DEX_MV_VIEWPORT		8

#define	S2DEX_BG_1CYC			0x01
#define	S2DEX_BG_COPY			0x02
#define	S2DEX_OBJ_RECTANGLE		0x03
#define	S2DEX_OBJ_SPRITE		0x04
#define	S2DEX_OBJ_MOVEMEM		0x05
#define	S2DEX_LOAD_UCODE		0xAF
#define	S2DEX_SELECT_DL			0xB0
#define	S2DEX_OBJ_RENDERMODE	0xB1
#define	S2DEX_OBJ_RECTANGLE_R	0xB2
#define	S2DEX_OBJ_LOADTXTR		0xC1
#define	S2DEX_OBJ_LDTX_SPRITE	0xC2
#define	S2DEX_OBJ_LDTX_RECT		0xC3
#define	S2DEX_OBJ_LDTX_RECT_R	0xC4
#define	S2DEX_RDPHALF_0			0xE4

/* BG flags */
#define	G_BGLT_LOADBLOCK	0x0033
#define	G_BGLT_LOADTILE		0xFFF4

#define	G_BG_FLAG_FLIPS		0x01
#define	G_BG_FLAG_FLIPT		0x10

/* Sprite object render modes */
#define	G_OBJRM_NOTXCLAMP		0x01
#define	G_OBJRM_XLU				0x02	/* Ignored */
#define	G_OBJRM_ANTIALIAS		0x04	/* Ignored */
#define	G_OBJRM_BILERP			0x08
#define	G_OBJRM_SHRINKSIZE_1	0x10
#define	G_OBJRM_SHRINKSIZE_2	0x20
#define	G_OBJRM_WIDEN			0x40

/* Sprite texture loading types */
#define	G_OBJLT_TXTRBLOCK	0x00001033
#define	G_OBJLT_TXTRTILE	0x00fc1034
#define	G_OBJLT_TLUT		0x00000030

/* Tile indices */
#define G_TX_LOADTILE			0x07
#define G_TX_RENDERTILE			0x00

typedef struct uObjBg uObjBg;
struct uObjBg {
	u16 imageW;     /* Texture width (8-byte alignment, u10.2) */
	u16 imageX;     /* x-coordinate of upper-left
					position of texture (u10.5) */
	u16 frameW;     /* Transfer destination frame width (u10.2) */
	s16 frameX;     /* x-coordinate of upper-left
					position of transfer destination frame (s10.2) */
	u16 imageH;     /* Texture height (u10.2) */
	u16 imageY;     /* y-coordinate of upper-left position of
					texture (u10.5) */
	u16 frameH;     /* Transfer destination frame height (u10.2) */
	s16 frameY;     /* y-coordinate of upper-left position of transfer
					destination  frame (s10.2) */
	u32 imagePtr;  /* Address of texture source in DRAM*/
	u8  imageSiz;   /* Texel size
					G_IM_SIZ_4b (4 bits/texel)
					G_IM_SIZ_8b (8 bits/texel)
					G_IM_SIZ_16b (16 bits/texel)
					G_IM_SIZ_32b (32 bits/texel) */
	u8  imageFmt;   /*Texel format
					G_IM_FMT_RGBA (RGBA format)
					G_IM_FMT_YUV (YUV format)
					G_IM_FMT_CI (CI format)
					G_IM_FMT_IA (IA format)
					G_IM_FMT_I (I format)  */
	u16 imageLoad;  /* Method for loading the BG image texture
					G_BGLT_LOADBLOCK (use LoadBlock)
					G_BGLT_LOADTILE (use LoadTile) */
	u16 imageFlip;  /* Image inversion on/off (horizontal
					direction only)
					0 (normal display (no inversion))
					G_BG_FLAG_FLIPS (horizontal inversion of texture image) */
	u16 imagePal;   /* Position of palette for 4-bit color
					index texture (4-bit precision, 0~15) */
	u16 tmemH;      /* Quadruple TMEM height(s13.2) which can be loaded at once
					 When normal texture 512/tmemW*4
					 When CI Texture 	256/tmemW*4 */
	u16 tmemW;      /* TMEM width Word size for frame 1 line
					 When LoadBlock GS_PIX2TMEM(imageW/4,imageSiz)
					 When LoadTile 	GS_PIX2TMEM(frameW/4,imageSiz)+1 */
	u16 tmemLoadTH; /* TH value or Stride value
					 When LoadBlock 	GS_CALC_DXT(tmemW)
					 When LoadTile  	tmemH-1 */
	u16 tmemLoadSH; /* SH value
					 When LoadBlock  	tmemSize/2-1
					 When LoadTile 	tmemW*16-1 */
	u16 tmemSize;   /* imagePtr skip value for one load iteration
					 = tmemSizeW*tmemH */
	u16 tmemSizeW;  /* imagePtr skip value for one line of image 1
					 When LoadBlock 	tmemW*2
					 When LoadTile  	GS_PIX2TMEM(imageW/4,imageSiz)*2 */
};   /* 40 bytes */

typedef struct uObjScaleBg uObjScaleBg;
struct uObjScaleBg
{
	u16 imageW;     /* Texture width (8-byte alignment, u10.2) */
	u16 imageX;     /* x-coordinate of upper-left
					position of texture (u10.5) */
	u16 frameW;     /* Transfer destination frame width (u10.2) */
	s16 frameX;     /* x-coordinate of upper-left
					position of transfer destination frame (s10.2) */

	u16 imageH;     /* Texture height (u10.2) */
	u16 imageY;     /* y-coordinate of upper-left position of
					texture (u10.5) */
	u16 frameH;     /* Transfer destination frame height (u10.2) */
	s16 frameY;     /* y-coordinate of upper-left position of transfer
					destination  frame (s10.2) */

	u32 imagePtr;  /* Address of texture source in DRAM*/
	u8  imageSiz;   /* Texel size
					G_IM_SIZ_4b (4 bits/texel)
					G_IM_SIZ_8b (8 bits/texel)
					G_IM_SIZ_16b (16 bits/texel)
					G_IM_SIZ_32b (32 bits/texel) */
	u8  imageFmt;   /*Texel format
					G_IM_FMT_RGBA (RGBA format)
					G_IM_FMT_YUV (YUV format)
					G_IM_FMT_CI (CI format)
					G_IM_FMT_IA (IA format)
					G_IM_FMT_I (I format)  */
	u16 imageLoad;  /* Method for loading the BG image texture
					G_BGLT_LOADBLOCK (use LoadBlock)
					G_BGLT_LOADTILE (use LoadTile) */
	u16 imageFlip;  /* Image inversion on/off (horizontal
					direction only)
					0 (normal display (no inversion))
					G_BG_FLAG_FLIPS (horizontal inversion of texture image) */
	u16 imagePal;   /* Position of palette for 4-bit color
					index texture (4-bit precision, 0~15) */

	u16 scaleH;      /* y-direction scale value (u5.10) */
	u16 scaleW;      /* x-direction scale value (u5.10) */
	s32 imageYorig;  /* image drawing origin (s20.5)*/

	u8  padding[4];  /* Padding */
};   /* 40 bytes */

typedef struct uObjSprite uObjSprite;
struct uObjSprite
{
	u16 scaleW;      /* Width-direction scaling (u5.10) */
	s16 objX;        /* x-coordinate of upper-left corner of OBJ (s10.2) */
	u16 paddingX;    /* Unused (always 0) */
	u16 imageW;      /* Texture width (length in s direction, u10.5)  */
	u16 scaleH;      /* Height-direction scaling (u5.10) */
	s16 objY;        /* y-coordinate of upper-left corner of OBJ (s10.2) */
	u16 paddingY;    /* Unused (always 0) */
	u16 imageH;      /* Texture height (length in t direction, u10.5)  */
	u16 imageAdrs;   /* Texture starting position in TMEM (In units of 64-bit words) */
	u16 imageStride; /* Texel wrapping width (In units of 64-bit words) */
	u8  imageFlags;  /* Display flag
					 (*) More than one of the following flags can be specified as the bit sum of the flags:
					 0 (Normal display (no inversion))
					 G_OBJ_FLAG_FLIPS (s-direction (x) inversion)
					 G_OBJ_FLAG_FLIPT (t-direction (y) inversion)  */
	u8  imagePal;    /* Position of palette for 4-bit color index texture  (4-bit precision, 0~7)  */
	u8  imageSiz;    /* Texel size
					 G_IM_SIZ_4b (4 bits/texel)
					 G_IM_SIZ_8b (8 bits/texel)
					 G_IM_SIZ_16b (16 bits/texel)
					 G_IM_SIZ_32b (32 bits/texel) */
	u8  imageFmt;    /* Texel format
					 G_IM_FMT_RGBA (RGBA format)
					 G_IM_FMT_YUV (YUV format)
					 G_IM_FMT_CI (CI format)
					 G_IM_FMT_IA (IA format)
					 G_IM_FMT_I  (I format) */
};    /* 24 bytes */

typedef struct uObjTxtrBlock uObjTxtrBlock;
struct uObjTxtrBlock
{
	u32   type;   /* Structure identifier (G_OBJLT_TXTRBLOCK) */
	u32   image; /* Texture source address in DRAM (8-byte alignment) */
	u16   tsize;  /* Texture size (specified by GS_TB_TSIZE) */
	u16   tmem;   /* TMEM word address where texture will be loaded (8-byte word) */
	u16   sid;    /* Status ID (multiple of 4: either 0, 4, 8, or 12) */
	u16   tline;  /* Texture line width (specified by GS_TB_TLINE) */
	u32   flag;   /* Status flag */
	u32   mask;   /* Status mask */
};     /* 24 bytes */

typedef struct uObjTxtrTile uObjTxtrTile;
struct uObjTxtrTile
{
	u32   type;   /* Structure identifier (G_OBJLT_TXTRTILE) */
	u32   image; /* Texture source address in DRAM (8-byte alignment) */
	u16   twidth; /* Texture width (specified by GS_TT_TWIDTH) */
	u16   tmem;   /* TMEM word address where texture will be loaded (8-byte word) */
	u16   sid;    /* Status ID (multiple of 4: either 0, 4, 8, or 12) */
	u16   theight;/* Texture height (specified by GS_TT_THEIGHT) */
	u32   flag;   /* Status flag */
	u32   mask;   /* Status mask  */
};      /* 24 bytes */

typedef struct uObjTxtrTLUT uObjTxtrTLUT;
struct uObjTxtrTLUT
{
	u32   type;   /* Structure identifier (G_OBJLT_TLUT) */
	u32   image; /* Texture source address in DRAM */
	u16   pnum;   /* Number of palettes to load - 1 */
	u16   phead;  /* Palette position at start of load (256~511) */
	u16   sid;    /* Status ID (multiple of 4: either 0, 4, 8, or 12) */
	u16   zero;   /* Always assign 0 */
	u32   flag;   /* Status flag */
	u32   mask;   /* Status mask */
};      /* 24 bytes */

typedef union
{
	uObjTxtrBlock      block;
	uObjTxtrTile       tile;
	uObjTxtrTLUT       tlut;
} uObjTxtr;

typedef struct uObjTxSprite uObjTxSprite;
struct uObjTxSprite
{
	uObjTxtr      txtr;
	uObjSprite    sprite;
};

typedef struct uObjMtx uObjMtx;
struct uObjMtx
{
	s32 A, B, C, D;   /* s15.16 */
	s16 Y, X;         /* s10.2 */
	u16 BaseScaleY;   /* u5.10 */
	u16 BaseScaleX;   /* u5.10 */
};

typedef struct uObjSubMtx uObjSubMtx;
struct uObjSubMtx
{
	s16 Y, X;		/* s10.2  */
	u16 BaseScaleY;	/* u5.10  */
	u16 BaseScaleX;	/* u5.10  */
};

static uObjMtx objMtx;

void gli_resetObjMtx()
{
	objMtx.A = 1 << 16;
	objMtx.B = 0;
	objMtx.C = 0;
	objMtx.D = 1 << 16;
	objMtx.X = 0;
	objMtx.Y = 0;
	objMtx.BaseScaleX = 1 << 10;
	objMtx.BaseScaleY = 1 << 10;
}

typedef enum S2DEXVersion
{
	eVer1_3,
	eVer1_5,
	eVer1_7
} S2DEXVersion;

static
S2DEXVersion gs_s2dexversion = eVer1_7;

typedef struct S2DEXCoordCorrector S2DEXCoordCorrector;
struct S2DEXCoordCorrector
{
	s16 A0, A1, A2, A3, B0, B2, B3, B5, B7;
};

static void s2dex_cc_init(S2DEXCoordCorrector * _cc)
{
	u32 O1;
	u32 O2;
	const s16 * CorrectorsB03_16;
	u32 O3;
		static const u32 CorrectorsA23[] = {
			0x0001FFFE,
			0xFFFEFFFE,
			0x00010000,
			0x00000000
		};
		static const u32 CorrectorsA01[] = {
			0x00000000,
			0x00100020,
			0x00200040,
			0x00300060,
			0x0000FFF4,
			0x00100014,
			0x00200034,
			0x00300054
		};
		static const s16 * CorrectorsA01_16 = (const s16*)(CorrectorsA01);

		static const s16 * CorrectorsA23_16 = (const s16*)(CorrectorsA23);

		O1 = (gSP.objRendermode & (G_OBJRM_SHRINKSIZE_1 | G_OBJRM_SHRINKSIZE_2 | G_OBJRM_WIDEN)) >> 3;
		_cc->A0 = CorrectorsA01_16[(0 + O1) ^ 1];
		_cc->A1 = CorrectorsA01_16[(1 + O1) ^ 1];
		O2 = (gSP.objRendermode & (G_OBJRM_SHRINKSIZE_1 | G_OBJRM_BILERP)) >> 2;
		_cc->A2 = CorrectorsA23_16[(0 + O2) ^ 1];
		_cc->A3 = CorrectorsA23_16[(1 + O2) ^ 1];

		CorrectorsB03_16 = NULL;
		O3 = 0;
		if (gs_s2dexversion == eVer1_3) {
			static const u32 CorrectorsB03_v1_3[] = {
				0xFFFC0000,
				0x00000000,
				0x00000001,
				0x00000000,
				0xFFFC0000,
				0x00000000,
				0x00000001,
				0xFFFF0001,
				0xFFFC0000,
				0x00030000,
				0x00000001,
				0x00000000,
				0xFFFC0000,
				0x00030000,
				0x00000001,
				0xFFFF0000,
				0xFFFF0003,
				0x0000FFF0,
				0x00000001,
				0x0000FFFF,
				0xFFFF0003,
				0x0000FFF0,
				0x00000001,
				0xFFFFFFFF,
				0xFFFF0003,
				0x0000FFF0,
				0x00000000,
				0x00000000,
				0xFFFF0003,
				0x0000FFF0,
				0x00000000,
				0xFFFF0000
			};
			CorrectorsB03_16 = (const s16*)(CorrectorsB03_v1_3);
			O3 = (_SHIFTL(gSP.objRendermode, 3, 16) & (G_OBJRM_SHRINKSIZE_1 | G_OBJRM_SHRINKSIZE_2 | G_OBJRM_WIDEN)) >> 1;
			_cc->B0 = CorrectorsB03_16[(0 + O3) ^ 1];
			_cc->B2 = CorrectorsB03_16[(2 + O3) ^ 1];
			_cc->B3 = CorrectorsB03_16[(3 + O3) ^ 1];
			_cc->B5 = CorrectorsB03_16[(5 + O3) ^ 1];
			_cc->B7 = CorrectorsB03_16[(7 + O3) ^ 1];
		} else {
			static const u32 CorrectorsB03[] = {
				0xFFFC0000,
				0x00000001,
				0xFFFF0003,
				0xFFF00000
			};
			CorrectorsB03_16 = (const s16*)(CorrectorsB03);
			O3 = (gSP.objRendermode & G_OBJRM_BILERP) >> 1;
			_cc->B0 = CorrectorsB03_16[(0 + O3) ^ 1];
			_cc->B2 = CorrectorsB03_16[(2 + O3) ^ 1];
			_cc->B3 = CorrectorsB03_16[(3 + O3) ^ 1];
			_cc->B5 = 0;
			_cc->B7 = 0;
		}
	}


typedef struct ObjCoordinates ObjCoordinates;
struct ObjCoordinates
{
	f32 ulx, uly, lrx, lry;
	f32 uls, ult, lrs, lrt;
	f32 z, w;
};

#define CALC_ST(_B, _scaleH) do { \
		const s16 B_ = (s16)(_B); \
		const u32 scaleH_ = (u32)(_scaleH); \
		sh = CC.A0 + B_; \
		sl = sh + _pObjSprite->imageW + CC.A0 - CC.A1 - 1; \
		th = sh - (((yh & 3) * 0x0200 * scaleH_) >> 16); \
		tl = th + _pObjSprite->imageH + CC.A0 - CC.A1 - 1; \
	} while (0)

static void objcoords_sprite(ObjCoordinates * _o, const uObjSprite *_pObjSprite, bool _useMatrix)
{
	s16 xh;
	s16 xl;
	s16 yh;
	s16 yl;
	s16 sh;
	s16 sl;
	s16 th;
	s16 tl;
	u16 objSpriteScaleW;
	u16 objSpriteScaleH;
		/* Fixed point coordinates calculation. Decoded by olivieryuyu */
		S2DEXCoordCorrector CC;
	s2dex_cc_init(&CC);
				objSpriteScaleW = MAX(_pObjSprite->scaleW, (u16)(1));
		objSpriteScaleH = MAX(_pObjSprite->scaleH, (u16)(1));
		if (_useMatrix) {
			const u32 scaleW = ((u32)(objMtx.BaseScaleX) * 0x40 * objSpriteScaleW) >> 16;
			const u32 scaleH = ((u32)(objMtx.BaseScaleY) * 0x40 * objSpriteScaleH) >> 16;
			if (gs_s2dexversion == eVer1_3) {
				/* XH = AND ((((objX << 0x10) * 0x0800 * (0x80007FFF/BaseScaleX)) >> 0x30) + X + A2) by B0 */
				/* XL = XH + AND (((((imageW - A1) * 0x100) *  (0x80007FFF/scaleW)) >> 0x20) + B2) by B0 */
				/* YH = AND ((((objY << 0x10) * 0x0800 * (0x80007FFF/BaseScaleY)) >> 0x30) + Y + A2) by B0 */
				/* YL = YH + AND (((((imageH - A1) * 0x100) *  (0x80007FFF/scaleH)) >> 0x20) + B2) by B0 */
				xh = (s16)((((((s64)(_pObjSprite->objX) << 27) * (0x80007FFFU / (u32)(objMtx.BaseScaleX))) >> 0x30) + objMtx.X + CC.A2) & CC.B0);
				xl = (s16)(((((((s64)(_pObjSprite->imageW) - CC.A1) << 8) * (0x80007FFFU / scaleW)) >> 0x20) + CC.B2) & CC.B0) + xh;
				yh = (s16)((((((s64)(_pObjSprite->objY) << 27) * (0x80007FFFU / (u32)(objMtx.BaseScaleY))) >> 0x30) + objMtx.Y + CC.A2) & CC.B0);
				yl = (s16)(((((((s64)(_pObjSprite->imageH) - CC.A1) << 8) * (0x80007FFFU / scaleH)) >> 0x20) + CC.B2) & CC.B0) + yh;
				CALC_ST(CC.B3, scaleH);
			} else {
				s32 xlp;
				s32 yhp;
				s32 ylp;
				/* XHP = ((objX << 16) * 0x0800 * (0x80007FFF / BaseScaleX)) >> 16 + ((AND(X + A2) by B0) << 16)) */
				/* XH = XHP >> 16 */
				/* XLP = XHP + (((ImageW - A1) << 24) * (0x80007FFF / scaleW)) >> 32 */
				/* XL = XLP >> 16 */
				/* YHP = ((objY << 16) * 0x0800 * (0x80007FFF / BaseScaleY)) >> 16 + ((AND(Y + A2) by B0) << 16)) */
				/* YH = YHP >> 16 */
				/* YLP = YHP + (((ImageH - A1) << 24) * (0x80007FFF / scaleH)) >> 32 */
				/* YL = YLP >> 16 */
				const s32 xhp = (((((s64)(_pObjSprite->objX) << 16) * 0x0800) * (0x80007FFFU / (u32)(objMtx.BaseScaleX))) >> 32) + (((objMtx.X + CC.A2) & CC.B0) << 16);
				xh = (s16)(xhp >> 16);
				xlp = xhp + (((((u64)(_pObjSprite->imageW) - CC.A1) << 24) * (0x80007FFFU / scaleW)) >> 32);
				xl = (s16)(xlp >> 16);
				yhp = (((((s64)(_pObjSprite->objY) << 16) * 0x0800) * (0x80007FFFU / (u32)(objMtx.BaseScaleY))) >> 32) + (((objMtx.Y + CC.A2) & CC.B0) << 16);
				yh = (s16)(yhp >> 16);
				ylp = yhp + (((((u64)(_pObjSprite->imageH) - CC.A1) << 24) * (0x80007FFFU / scaleH)) >> 32);
				yl = (s16)(ylp >> 16);
				CALC_ST(CC.B2, scaleH);
			}
		} else {
			/* XH = AND(objX + A2) by B0 */
			/* XL = ((AND(objX + A2) by B0) << 16) + (((ImageW - A1) << 24)*(0x80007FFF / scaleW)) >> 48 */
			/* YH = AND(objY + A2) by B0 */
			/* YL = ((AND(objY + A2) by B0) << 16) + (((ImageH - A1) << 24)*(0x80007FFF / scaleH)) >> 48 */
			xh = (_pObjSprite->objX + CC.A2) & CC.B0;
			xl = (s16)(((((u64)(_pObjSprite->imageW) - CC.A1) << 24) * (0x80007FFFU / (u32)(objSpriteScaleW))) >> 48) + xh;
			yh = (_pObjSprite->objY + CC.A2) & CC.B0;
			yl = (s16)(((((u64)(_pObjSprite->imageH) - CC.A1) << 24) * (0x80007FFFU / (u32)(objSpriteScaleH))) >> 48) + yh;
			CALC_ST(CC.B2, objSpriteScaleH);
		}

		_o->ulx = _FIXED2FLOAT(xh, 2);
		_o->lrx = _FIXED2FLOAT(xl, 2);
		_o->uly = _FIXED2FLOAT(yh, 2);
		_o->lry = _FIXED2FLOAT(yl, 2);

		_o->uls = _FIXED2FLOAT(sh, 5);
		_o->lrs = _FIXED2FLOAT(sl, 5);
		_o->ult = _FIXED2FLOAT(th, 5);
		_o->lrt = _FIXED2FLOAT(tl, 5);

		if ((_pObjSprite->imageFlags & G_BG_FLAG_FLIPS) != 0)
			SWAP_F32(_o->uls, _o->lrs);
		if ((_pObjSprite->imageFlags & G_BG_FLAG_FLIPT) != 0)
			SWAP_F32(_o->ult, _o->lrt);

		_o->z = (gDP.otherMode.depthSource == G_ZS_PRIM) ? gDP.primDepth.z : gSP.viewport.nearz;
		_o->w = 1.0f;
	}

static void objcoords_bg(ObjCoordinates * _o, const uObjScaleBg * _pObjScaleBg)
{
	f32 frameW;
	f32 frameH;
	f32 imageW;
	f32 imageH;
		const f32 frameX = _FIXED2FLOAT(_pObjScaleBg->frameX, 2);
		const f32 frameY = _FIXED2FLOAT(_pObjScaleBg->frameY, 2);
		const f32 imageX = gSP.bgImage.imageX;
		const f32 imageY = gSP.bgImage.imageY;
		f32 scaleW = gSP.bgImage.scaleW;
		f32 scaleH = gSP.bgImage.scaleH;

		/* BgRectCopyOnePiece() does not support scaleW and scaleH */
		if (gDP.otherMode.cycleType == G_CYC_COPY) {
			scaleW = 1.0f;
			scaleH = 1.0f;
		}

		frameW = _FIXED2FLOAT(_pObjScaleBg->frameW, 2);
		frameH = _FIXED2FLOAT(_pObjScaleBg->frameH, 2);
		imageW = (f32)(_pObjScaleBg->imageW >> 2);
		imageH = (f32)(_pObjScaleBg->imageH >> 2);
		/*		const f32 imageW = (f32)gSP.bgImage.width; */
		/*		const f32 imageH = (f32)gSP.bgImage.height; */

		if ((u32)(imageW) == 512 && (gli_s2dex_hacks() & hack_RE2) != 0) {
			const f32 width = (f32)(*REG.VI_WIDTH);
			const f32 scale = imageW / width;
			imageW = width;
			frameW = width;
			imageH *= scale;
			frameH *= scale;
			scaleW = 1.0f;
			scaleH = 1.0f;
		}

		_o->uls = imageX;
		_o->ult = imageY;
		_o->lrs = _o->uls + MIN(imageW, frameW * scaleW) - 1;
		_o->lrt = _o->ult + MIN(imageH, frameH * scaleH) - 1;

		gSP.bgImage.clampS = _o->lrs <= (imageW - 1) ? 1 : 0 ;
		gSP.bgImage.clampT = _o->lrt <= (imageH - 1) ? 1 : 0 ;

		/* G_CYC_COPY (BgRectCopyOnePiece()) does not allow texture filtering */
		if (gDP.otherMode.cycleType != G_CYC_COPY) {
			/* Correct texture coordinates -0.5f and +0.5 if G_OBJRM_BILERP */
			/* bilinear interpolation is set */
			if (gDP.otherMode.textureFilter == G_TF_BILERP) {
				_o->uls -= 0.5f;
				_o->ult -= 0.5f;
				_o->lrs += 0.5f;
				_o->lrt += 0.5f;
			}
			/* SHRINKSIZE_1 adds a 0.5f perimeter around the image */
			/* upper left texture coords += 0.5f; lower left texture coords -= 0.5f */
			if ((gSP.objRendermode&G_OBJRM_SHRINKSIZE_1) != 0) {
				_o->uls += 0.5f;
				_o->ult += 0.5f;
				_o->lrs -= 0.5f;
				_o->lrt -= 0.5f;
				/* SHRINKSIZE_2 adds a 1.0f perimeter */
				/* upper left texture coords += 1.0f; lower left texture coords -= 1.0f */
			}
			else if ((gSP.objRendermode&G_OBJRM_SHRINKSIZE_2) != 0) {
				_o->uls += 1.0f;
				_o->ult += 1.0f;
				_o->lrs -= 1.0f;
				_o->lrt -= 1.0f;
			}
		}

		/* Calculate _o->lrx and _o->lry width new ST values */
		_o->ulx = frameX;
		_o->uly = frameY;
		_o->lrx = _o->ulx + (_o->lrs - _o->uls) / scaleW;
		_o->lry = _o->uly + (_o->lrt - _o->ult) / scaleH;
		if (((gSP.objRendermode&G_OBJRM_BILERP) == 0 && gDP.otherMode.textureFilter != G_TF_BILERP) ||
			((gSP.objRendermode&G_OBJRM_BILERP) != 0 && gDP.otherMode.textureFilter == G_TF_POINT && (gSP.objRendermode&G_OBJRM_NOTXCLAMP) != 0)) {
			_o->lrx += 1.0f / scaleW;
			_o->lry += 1.0f / scaleH;
		}

		/* BgRect1CycOnePiece() and BgRectCopyOnePiece() do only support */
		/* imageFlip in horizontal direction */
		if ((_pObjScaleBg->imageFlip & G_BG_FLAG_FLIPS) != 0) {
			SWAP_F32(_o->ulx, _o->lrx);
		}

		_o->z = (gDP.otherMode.depthSource == G_ZS_PRIM) ? gDP.primDepth.z : gSP.viewport.nearz;
		_o->w = 1.0f;
	}

static
u16 _YUVtoRGBA(u8 y, u8 u, u8 v)
{
	float r = y + (1.370705f * (v - 128));
	float g = y - (0.698001f * (v - 128)) - (0.337633f * (u - 128));
	float b = y + (1.732446f * (u - 128));
	r *= 0.125f;
	g *= 0.125f;
	b *= 0.125f;
	/*clipping the result */
	if (r > 31) r = 31;
	if (g > 31) g = 31;
	if (b > 31) b = 31;
	if (r < 0) r = 0;
	if (g < 0) g = 0;
	if (b < 0) b = 0;

	return (u16)(((u16)(r) << 11) |
		((u16)(g) << 6) |
		((u16)(b) << 1) | 1);
}

static
void _drawYUVImageToFrameBuffer(const ObjCoordinates * _objCoords)
{
	u32 width;
	u32 height;
	u32 * mb;
	u16 * dst;
	u16 h;
	const u32 ulx = (u32)_objCoords->ulx;
	const u32 uly = (u32)_objCoords->uly;
	const u32 lrx = (u32)_objCoords->lrx;
	const u32 lry = (u32)_objCoords->lry;
	const u32 ci_width = gDP.colorImage.width;
	const u32 ci_height = (u32)gDP.scissor.lry;
	if (ulx >= ci_width)
		return;
	if (uly >= ci_height)
		return;
	width = 16;
	height = 16;
	if (lrx > ci_width)
		width = ci_width - ulx;
	if (lry > ci_height)
		height = ci_height - uly;
	mb = (u32*)(RDRAM + gDP.textureImage.address);
	dst = (u16*)(RDRAM + gDP.colorImage.address);
	dst += ulx + uly * ci_width;
	/*yuv macro block contains 16x16 texture. we need to put it in the proper place inside cimg */
	for (h = 0; h < 16; h++) {
		u16 w;
		for (w = 0; w < 16; w += 2) {
			u32 t = *(mb++); /*each u32 contains 2 pixels */
			if ((h < height) && (w < width)) /*clipping. texture image may be larger than color image */
			{
				u8 y0 = (u8)t & 0xFF;
				u8 v = (u8)(t >> 8) & 0xFF;
				u8 y1 = (u8)(t >> 16) & 0xFF;
				u8 u = (u8)(t >> 24) & 0xFF;
				*(dst++) = _YUVtoRGBA(y0, u, v);
				*(dst++) = _YUVtoRGBA(y1, u, v);
			}
		}
		dst += ci_width - 16;
	}
	gli_s2dex_mark_current_ob_screen();
}

static
void gSPDrawObjRect(const ObjCoordinates * _coords)
{
	SPVertex * vtx1;
	SPVertex * vtx2;
	SPVertex * vtx3;
	SPVertex * pVtx = gli_drawer_dma_vertices(4);
	SPVertex * vtx0 = &pVtx[0];
	vtx0->x = _coords->ulx;
	vtx0->y = _coords->uly;
	vtx0->z = _coords->z;
	vtx0->w = _coords->w;
	vtx0->s = _coords->uls;
	vtx0->t = _coords->ult;
	vtx1 = &pVtx[1];
	vtx1->x = _coords->lrx;
	vtx1->y = _coords->uly;
	vtx1->z = _coords->z;
	vtx1->w = _coords->w;
	vtx1->s = _coords->lrs;
	vtx1->t = _coords->ult;
	vtx2 = &pVtx[2];
	vtx2->x = _coords->ulx;
	vtx2->y = _coords->lry;
	vtx2->z = _coords->z;
	vtx2->w = _coords->w;
	vtx2->s = _coords->uls;
	vtx2->t = _coords->lrt;
	vtx3 = &pVtx[3];
	vtx3->x = _coords->lrx;
	vtx3->y = _coords->lry;
	vtx3->z = _coords->z;
	vtx3->w = _coords->w;
	vtx3->s = _coords->lrs;
	vtx3->t = _coords->lrt;

	gli_drawer_draw_screen_strip(4);
}

static
void gSPSetSpriteTile(const uObjSprite *_pObjSprite)
{
	const u32 w = MAX(_pObjSprite->imageW >> 5, 1);
	const u32 h = MAX(_pObjSprite->imageH >> 5, 1);

	gli_gDPSetTile( _pObjSprite->imageFmt, _pObjSprite->imageSiz, _pObjSprite->imageStride, _pObjSprite->imageAdrs, G_TX_RENDERTILE, _pObjSprite->imagePal, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, 0, 0, 0, 0 );
	gli_gDPSetTileSize( G_TX_RENDERTILE, 0, 0, (w - 1) << 2, (h - 1) << 2 );
	gli_gSPTexture(1.0f, 1.0f, 0, 0, TRUE);
}

static
void gSPObjLoadTxtr(u32 tx)
{
	const u32 address = RSP_SegmentToPhysical(tx);
	uObjTxtr *objTxtr = (uObjTxtr*)&RDRAM[address];

	if ((gSP.status[objTxtr->block.sid >> 2] & objTxtr->block.mask) != objTxtr->block.flag) {
		switch (objTxtr->block.type) {
			case G_OBJLT_TXTRBLOCK:
				gli_gDPSetTextureImage(G_IM_FMT_RGBA, G_IM_SIZ_16b, objTxtr->block.tsize + 1, objTxtr->block.image);
				gli_gDPSetTile(G_IM_FMT_RGBA, G_IM_SIZ_16b, 0, objTxtr->block.tmem,
						   G_TX_LOADTILE, 0, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, 0, 0, 0, 0);
				gli_gDPLoadBlock( G_TX_LOADTILE, 0, 0, objTxtr->block.tsize << 2, objTxtr->block.tline );
				break;
			case G_OBJLT_TXTRTILE:
				gli_gDPSetTextureImage(G_IM_FMT_RGBA, G_IM_SIZ_16b, objTxtr->tile.twidth + 1, objTxtr->tile.image);
				gli_gDPSetTile(G_IM_FMT_RGBA, G_IM_SIZ_16b, (objTxtr->tile.twidth + 1) >> 2, objTxtr->tile.tmem,
						   G_TX_LOADTILE, 0, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, 0, 0, 0, 0);
				gli_gDPLoadTile( G_TX_LOADTILE, 0, 0, objTxtr->tile.twidth << 2, objTxtr->tile.theight );
				break;
			case G_OBJLT_TLUT:
				gli_gDPSetTextureImage(G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, objTxtr->tlut.image);
				gli_gDPSetTile(G_IM_FMT_RGBA, G_IM_SIZ_4b, 0, objTxtr->tlut.phead,
						   G_TX_LOADTILE, 0, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, 0, 0, 0, 0);
				gli_gDPLoadTLUT( G_TX_LOADTILE, 0, 0, objTxtr->tlut.pnum << 2, 0 );
				break;
		}
		gSP.status[objTxtr->block.sid >> 2] =
			(gSP.status[objTxtr->block.sid >> 2] & ~objTxtr->block.mask) | (objTxtr->block.flag & objTxtr->block.mask);
	}
}

static
void gSPObjRectangle(u32 _sp)
{
	ObjCoordinates objCoords;
	const u32 address = RSP_SegmentToPhysical(_sp);
	uObjSprite *objSprite = (uObjSprite*)&RDRAM[address];
	gSPSetSpriteTile(objSprite);

	objcoords_sprite(&objCoords, objSprite, false);
	gSPDrawObjRect(&objCoords);
}

static
void gSPObjRectangleR(u32 _sp)
{
	ObjCoordinates objCoords;
	const u32 address = RSP_SegmentToPhysical(_sp);
	const uObjSprite *objSprite = (uObjSprite*)&RDRAM[address];
	gSPSetSpriteTile(objSprite);
	objcoords_sprite(&objCoords, objSprite, true);

	if (objSprite->imageFmt == G_IM_FMT_YUV && (gli_s2dex_hacks()&hack_Ogre64)) /*Ogre Battle needs to copy YUV texture to frame buffer */
		_drawYUVImageToFrameBuffer(&objCoords);
	gSPDrawObjRect(&objCoords);

}

static INLINE f32 s2dex_calc_x(s16 _x0, s16 _x, s16 _y)
{
	const s16 X = _x0 + (s16)(((_x * objMtx.A) >> 16)) + (s16)(((_y * objMtx.B) >> 16));
	return _FIXED2FLOAT(X, 2);
}

static INLINE f32 s2dex_calc_y(s16 _y0, s16 _x, s16 _y)
{
	const s16 Y = _y0 + (s16)(((_x * objMtx.C) >> 16)) + (s16)(((_y * objMtx.D) >> 16));
	return _FIXED2FLOAT(Y, 2);
}

static
void gSPObjSprite(u32 _sp)
{
	S2DEXCoordCorrector CC;
	s16 ulx;
	s16 uly;
	s16 lrx;
	s16 lry;
	f32 uls;
	f32 lrs;
	f32 ult;
	f32 lrt;
	float z;
	SPVertex * pVtx;
	SPVertex * vtx0;
	SPVertex * vtx1;
	SPVertex * vtx2;
	SPVertex * vtx3;
	s16 x0, y0;
	const u32 address = RSP_SegmentToPhysical(_sp);
	uObjSprite *objSprite = (uObjSprite*)&RDRAM[address];
	gSPSetSpriteTile(objSprite);

	/* Fixed point coordinates calculation. Decoded by olivieryuyu */
	/*	X1 = AND (X + B3) by B0 + ((objX + A3) * A) >> 16 + ((objY + A3) * B) >> 16 */
	/*	Y1 = AND (Y + B3) by B0 + ((objX + A3) * C) >> 16 + ((objY + A3) * D) >> 16 */
	/*	X2 = AND (X + B3) by B0 + (((((imageW - A1) * 0x0100)* (0x80007FFF/scaleW)) >> 32+ objX + A3) * A) >> 16  + (((((imageH - A1) * 0x0100)* (0x80007FFF/scaleH)) >> 32 + objY + A3) * B) >> 16 */
	/*	Y2 = AND (Y + B3) by B0 + (((((imageW - A1) * 0x0100)* (0x80007FFF/scaleW)) >> 32 + objX + A3) * C) >> 16 + (((((imageH - A1) * 0x0100)* (0x80007FFF/scaleH)) >> 32 + objY + A3) * D) >> 16 */
	s2dex_cc_init(&CC);
	x0 = (gs_s2dexversion == eVer1_3) ?
					((objMtx.X + CC.B5) & CC.B0) + CC.B7 :
					((objMtx.X + CC.B3) & CC.B0);
	y0 = (gs_s2dexversion == eVer1_3) ?
					((objMtx.Y + CC.B5) & CC.B0) + CC.B7 :
					((objMtx.Y + CC.B3) & CC.B0);
	ulx = objSprite->objX + CC.A3;
	uly = objSprite->objY + CC.A3;
	lrx = (((((u64)(objSprite->imageW) - CC.A1) << 8) * (0x80007FFFU / (u32)(objSprite->scaleW))) >> 32) + ulx;
	lry = (((((u64)(objSprite->imageH) - CC.A1) << 8) * (0x80007FFFU / (u32)(objSprite->scaleH))) >> 32) + uly;

	uls = 0.0f;
	lrs = _FIXED2FLOAT(objSprite->imageW, 5) - 1.0f;
	ult = 0.0f;
	lrt = _FIXED2FLOAT(objSprite->imageH, 5) - 1.0f;

	if (objSprite->imageFlags & G_BG_FLAG_FLIPS)
		SWAP_F32(uls, lrs);

	if (objSprite->imageFlags & G_BG_FLAG_FLIPT)
		SWAP_F32(ult, lrt);

	z = (gDP.otherMode.depthSource == G_ZS_PRIM) ? gDP.primDepth.z : gSP.viewport.nearz;

	pVtx = gli_drawer_dma_vertices(4);

	vtx0 = &pVtx[0];
	vtx0->x = s2dex_calc_x(x0, ulx, uly);
	vtx0->y = s2dex_calc_y(y0, ulx, uly);
	vtx0->z = z;
	vtx0->w = 1.0f;
	vtx0->s = uls;
	vtx0->t = ult;
	vtx1 = &pVtx[1];
	vtx1->x = s2dex_calc_x(x0, lrx, uly);
	vtx1->y = s2dex_calc_y(y0, lrx, uly);
	vtx1->z = z;
	vtx1->w = 1.0f;
	vtx1->s = lrs;
	vtx1->t = ult;
	vtx2 = &pVtx[2];
	vtx2->x = s2dex_calc_x(x0, ulx, lry);
	vtx2->y = s2dex_calc_y(y0, ulx, lry);
	vtx2->z = z;
	vtx2->w = 1.0f;
	vtx2->s = uls;
	vtx2->t = lrt;
	vtx3 = &pVtx[3];
	vtx3->x = s2dex_calc_x(x0, lrx, lry);
	vtx3->y = s2dex_calc_y(y0, lrx, lry);
	vtx3->z = z;
	vtx3->w = 1.0f;
	vtx3->s = lrs;
	vtx3->t = lrt;

	gli_drawer_draw_screen_strip(4);

}

static
void gSPObjMatrix(u32 mtx)
{
	objMtx = *(const uObjMtx *)(RDRAM + RSP_SegmentToPhysical(mtx));
}

static
void gSPObjSubMatrix(u32 mtx)
{
	const uObjSubMtx * pObjSubMtx = (const uObjSubMtx*)(RDRAM + RSP_SegmentToPhysical(mtx));
	objMtx.X = pObjSubMtx->X;
	objMtx.Y = pObjSubMtx->Y;
	objMtx.BaseScaleX = pObjSubMtx->BaseScaleX;
	objMtx.BaseScaleY = pObjSubMtx->BaseScaleY;
}

static
void _loadBGImage(const uObjScaleBg * _pBgInfo, bool _loadScale, bool _fbImage)
{
	u32 imageW;
	u32 imageH;
	gSP.bgImage.address = RSP_SegmentToPhysical(_pBgInfo->imagePtr);

	imageW = _pBgInfo->imageW >> 2;
	imageH = _pBgInfo->imageH >> 2;
	if (imageW == 512 && (gli_s2dex_hacks() & hack_RE2) != 0) {
		gSP.bgImage.width = *REG.VI_WIDTH;
		gSP.bgImage.height = (imageH * imageW) / gSP.bgImage.width;
	}
	else {
		gSP.bgImage.width = imageW - imageW % 2;
		gSP.bgImage.height = imageH - imageH % 2;
	}
	gSP.bgImage.format = _pBgInfo->imageFmt;
	gSP.bgImage.size = _pBgInfo->imageSiz;
	gSP.bgImage.palette = _pBgInfo->imagePal;
	gSP.bgImage.imageX = _FIXED2FLOAT(_pBgInfo->imageX, 5);
	gSP.bgImage.imageY = _FIXED2FLOAT(_pBgInfo->imageY, 5);
	if (_loadScale) {
		gSP.bgImage.scaleW = _FIXED2FLOAT(_pBgInfo->scaleW, 10);
		gSP.bgImage.scaleH = _FIXED2FLOAT(_pBgInfo->scaleH, 10);
	}
	else
		gSP.bgImage.scaleW = gSP.bgImage.scaleH = 1.0f;

	gDP.tiles[0].textureMode = TEXTUREMODE_BGIMAGE;

	if (_fbImage)
		gli_s2dex_load_fb_image();
}

static
bool _useOnePieceBgCode(u32 address, bool * fbImage)
{
	uObjScaleBg *pObjScaleBg = (uObjScaleBg*)&RDRAM[address];
	*fbImage = gli_s2dex_use_fb_image(RSP_SegmentToPhysical(pObjScaleBg->imagePtr),
			pObjScaleBg->imageSiz, pObjScaleBg->imageFmt) != 0;

	if (gli_s2dex_bg_one_piece())
		return true;

	if ((gli_s2dex_hacks() & hack_RE2) != 0)
		return true;

	return *fbImage;
}

static
void BgRect1CycOnePiece(u32 _bg, bool _fbImage)
{
	ObjCoordinates objCoords;
	uObjScaleBg *pObjScaleBg = (uObjScaleBg*)&RDRAM[_bg];
	_loadBGImage(pObjScaleBg, true, _fbImage);

	/* Zelda MM uses depth buffer copy in LoT and in pause screen. */
	/* In later case depth buffer is used as temporal color buffer, and usual rendering must be used. */
	/* Since both situations are hard to distinguish, do the both depth buffer copy and bg rendering. */
	if ((gli_s2dex_hacks() & hack_ZeldaMM) != 0 &&
		(gSP.bgImage.address == gDP.depthImageAddress || gli_s2dex_depth_buffer_at(gSP.bgImage.address))
		)
		gli_s2dex_copy_depth_buffer();

	gDP.otherMode.cycleType = G_CYC_1CYCLE;
	gDP.changed |= CHANGED_CYCLETYPE;
	gli_gSPTexture(1.0f, 1.0f, 0, 0, TRUE);

	objcoords_bg(&objCoords, pObjScaleBg);
	gSPDrawObjRect(&objCoords);

}

static
void BgRectCopyOnePiece(u32 _bg, bool _fbImage)
{
	ObjCoordinates objCoords;
	uObjScaleBg *pObjBg = (uObjScaleBg*)&RDRAM[_bg];
	_loadBGImage(pObjBg, false, _fbImage);

	/* See comment to BgRect1CycOnePiece */
	if ((gli_s2dex_hacks() & hack_ZeldaMM) != 0 &&
		(gSP.bgImage.address == gDP.depthImageAddress || gli_s2dex_depth_buffer_at(gSP.bgImage.address))
		)
		gli_s2dex_copy_depth_buffer();

	gDP.otherMode.cycleType = G_CYC_COPY;
	gDP.changed |= CHANGED_CYCLETYPE;
	gli_gSPTexture(1.0f, 1.0f, 0, 0, TRUE);

	objcoords_bg(&objCoords, pObjBg);
	gSPDrawObjRect(&objCoords);

}

/*#define runCommand(w0, w1) gli_GBIcmd[_SHIFTR(w0, 24, 8)](w0, w1) */
static INLINE
void runCommand(u32 w0, u32 w1)
{
	gli_GBIcmd[_SHIFTR(w0, 24, 8)](w0, w1);
}

static
void BgRect1CycStripped(u32 _bgAddr)
{
	bool keepLLE;
	s32 E2_1;
	u16 F1_1;
	u16 P;
	s16 H2;
	uObjScaleBg objBg = *(const uObjScaleBg*)(RDRAM + _bgAddr);
	const u32 imagePtr = RSP_SegmentToPhysical(objBg.imagePtr);
	gDP.otherMode.cycleType = G_CYC_1CYCLE;
	gDP.changed |= CHANGED_CYCLETYPE;
	keepLLE = RSP.LLE;
	RSP.LLE = true;


	/* Part 1 */
	{
		s16 Aw;
		s16 Bw;
		s16 Cw;
		s16 Dw;
		s16 Ew;
		s16 Ah;
		s16 Bh;
		s16 Ch;
		s16 Dh;
		s16 Eh;
		u16 Fw;
		s16 Gw;
		s32 Hw;
		u16 Fh;
		s16 Gh;
		s32 Hh;
		s32 I;
		s16 J;
		u8 J_2;
		u8 K;
		const u16 *  aFormat16;
		u16 L;
		u32 M;
		u16 N;
		u16 O;
		u16 Q;
		s32 R;
		s32 S;
		s16 T;
		s32 U;
		s16 V;
		s32 W;
		s32 Z;
		u32 A1;
		u32 B1;
		u32 C1;
		u16 D1;
		u16 E1;
		u16 F1;
		s16 A2;
		u16 B2;
		s16 A2_1;
		s16 C2;
		s16 D2;
		s32 E2;
		s32 F2;
		s32 G2;
		u32 I2;
		u32 J2;
		u32 J2_1;
		u32 K2;
		u16 L2;
		static const u32 aFormat[] = {
			0x04000400,
			0x02000400,
			0x04000000
		};
		static const u32 aSize[] = {
			0x01FF0080,
			0x00FF0100,
			0x007F0200,
			0x003F0400
		};
		/* Step 1 & 2 */
		Aw = objBg.frameW - ((((objBg.imageW << 10) / objBg.scaleW) - 1) & 0xFFFC);
		if (Aw < 0)
			Aw = 0;
		if ((objBg.imageFlip & G_BG_FLAG_FLIPS) != 0)
			objBg.frameX += Aw;
		Bw = MAX(0, gDP.scissor.xh - objBg.frameX);
		Cw = MAX(0, objBg.frameX + objBg.frameW - gDP.scissor.xl - Aw);
		if ((s16)objBg.frameW - Aw - Bw - Cw <= 0)
			{ RSP.LLE = keepLLE; return; }
		Dw = objBg.frameX + Bw;
		Ew = objBg.frameX + objBg.frameW - Aw - Cw;

		Ah = objBg.frameH - ((((objBg.imageH << 10) / objBg.scaleH) - 1) & 0xFFFC);
		if (Ah < 0)
			Ah = 0;
		Bh = MAX(0, gDP.scissor.yh - objBg.frameY);
		Ch = MAX(0, objBg.frameY + objBg.frameH - gDP.scissor.yl - Ah);
		if ((s16)objBg.frameH - Ah - Bh - Ch <= 0)
			{ RSP.LLE = keepLLE; return; }
		Dh = ((objBg.frameY + Bh) * 0x4000) >> 16;
		Eh = ((objBg.frameH - Ah - Bh - Ch) * 0x4000) >> 16;

		*(u32*)(DMEM + 0x548) = (Dw << 16) | Ew;
		*(u32*)(DMEM + 0x54C) = (Dh << 16) | Eh;

		/* Step 3 */
		Fw = objBg.imageW << 3;
		if ((objBg.imageFlip & G_BG_FLAG_FLIPS) != 0)
			Bw = Cw;
		Gw = ((objBg.scaleW * Bw * 0x0200) >> 16) + objBg.imageX;
		Hw = Gw - Fw;
		Fh = objBg.imageH << 3;
		Gh = ((objBg.scaleH * Bh * 0x0200) >> 16) + objBg.imageY;
		while (Hw >= 0) {
			Gw -= Fw;
			Gh += 0x20;
			objBg.imageYorig += 0x20;
			Hw = Gw - Fw;
		}
		Hh = Gh - Fh;
		while (Hh >= 0) {
			Gh -= Fh;
			objBg.imageYorig -= Fh;
			Hh = Gh - Fh;
		}

		I = ((s32)(Gh) - objBg.imageYorig) << 5;
		J = (objBg.scaleW * (objBg.frameW - Aw - Bw - Cw)) >> 7;
		J_2 = 1;
		if (J + Gw + 0x0B < Fw)
			J_2 = 0;
		K = (gSP.objRendermode & 0x08) >> 3;

		/* Step 4 */
		aFormat16 = (const u16*)(aFormat);
		L = aFormat16[objBg.imageFmt ^ 1];
		M = aSize[objBg.imageSiz];
		N = ((objBg.frameW * objBg.scaleW) >> 7) + (K << 5);
		O = N;
		if (N >= Fw)
			O = Fw;
		P = (((O + (M >> 16)) * (M & 0xFFFF)) >> 16) + 1;

		*(u32*)(DMEM + 0x550) = (K << 24) | (J_2 << 16) | P;
		*(s32*)(DMEM + 0x554) = I;

		/* Step 5 */
		Q = L / (P * 2) + K * 0xFFFF;
		R = (0x100000 * Q) / objBg.scaleH;

		*(u32*)(DMEM + 0x558) = R;

		/* Step 6 */
		S = (((s64)(I) * 0x4000000 / objBg.scaleH) >> 16) & 0xFFFFFC00;
		/*s16 T = (S / R); */
		T = (s16)(((0xFFFFFFFF / R) * (s64)(S)) >> 0x20);
		U = R * (T + 1);
		V = T;
		if (U <= S)
			V += 1;
		W = R * V;
		Z = R - S + W;

		*(u32*)(DMEM + 0x55C) = Z;
		*(s32*)(DMEM + 0x560) = objBg.imageYorig;

		/* Step 7 */
		A1 = S - (W & 0xFFFFFC00);
		B1 = (A1 * 0x0040) >> 16;
		C1 = B1 * objBg.scaleH;
		D1 = (u16)(((C1 * 0x0040) >> 16) & 0x0000FFFF);
		E1 = Q - D1;
		F1 = C1 & 0xFFFF;
		F1_1 = ((F1 <<11) >> 16) & 0x001F;

		/* Step 8 */
		A2 = (s16)(((u32)(Q) * (u32)(V) + (u32)(D1) + ((objBg.imageYorig << 11) >> 16)) & 0xFFFF);
		B2 = (objBg.imageH << 14) >> 16;
		A2_1 = (A2 >= 0) ? A2 : A2 + B2;
		if (A2 - B2 >= 0)
			A2_1 -= B2;
		C2 = (s16)((((s32)(Gw) * (M & 0xFFFF)) >> 16) << 3);
		D2 = (s16)((((s32)(Fw) * (M & 0xFFFF)) >> 16) << 3);
		E2 = A2_1 * D2 + C2;
		E2_1 = E2 + imagePtr;
		F2 = E1 * D2;
		G2 = Q * D2;
		H2 = Gw & (M >> 16);
		if ((objBg.imageFlip & G_BG_FLAG_FLIPS) != 0)
			H2 = (H2 + J) * 0xFFFF;
		I2 = 0xFD100000 | ((D2 >> 1) - 1);
		J2 = 0xF5100000 | (P << 9);
		J2_1 = (J2 & 0xFF00FFFF) | (((objBg.imageFmt << 5) | (objBg.imageSiz << 3)) << 16);
		K2 = (objBg.imagePal << 20) | 0x0007C1F0;
		L2 = objBg.imageH >> 2;

		*(u32*)(DMEM + 0x564) = (C2 << 16) | D2;
		*(u32*)(DMEM + 0x568) = I2;
		*(u32*)(DMEM + 0x56C) = J2;
		/**(u32*)(DMEM + 0x570) = (Q << 16) | E1; */
		*(u32*)(DMEM + 0x570) = (E1 << 16) | Q;
		*(u32*)(DMEM + 0x574) = F2;
		*(u32*)(DMEM + 0x578) = G2;
		*(u32*)(DMEM + 0x57C) = (L2 << 16) | A2_1;

		runCommand(J2, 0x27000000);
		runCommand(J2_1, K2);
		runCommand((G_SETTILESIZE<<24), 0);
	}

	if (gli_s2dex_native_res_texrects())
		gli_s2dex_background_drawing(1);

	/*Part two */
	{
		s32 CC;
		u32 DD;
		u32 EE;
		s32 FF;
		u16 JJ;
		u32 GG;
		u32 HH;
		u32 II;
		u32 step;
		s32 KK;
		s16 LL;
		s16 MM;
		s16 NN;
		s16 RR;
		s16 AAA;
		u32 SS;
		bool stop;

	u32 VV = *(u32*)(DMEM + 0x57C);
	s16 AA = _SHIFTR(VV, 16, 16) - _SHIFTR(VV, 0, 16);
	VV = *(u32*)(DMEM + 0x570);
	CC = VV >> 16;
	DD = *(u32*)(DMEM + 0x55C);
	EE = *(u32*)(DMEM + 0x558);
	VV = *(u32*)(DMEM + 0x54C);
	FF = _SHIFTR(VV,  0, 16);
	JJ = _SHIFTR(VV, 16, 16);
	GG = *(u32*)(DMEM + 0x574);
	VV = *(u32*)(DMEM + 0x548);
	HH = _SHIFTR(VV, 16, 16) << 0x0C;
	II = _SHIFTR(VV,  0, 16) << 0x0C;

	step = 2;

#define setTexture() do { \
		runCommand((*(u32*)(DMEM + 0x56C) | AAA), 0x27000000); \
		runCommand((*(u32*)(DMEM + 0x568)), SS); \
		runCommand(0xF4000000, (((P + 0x6FF)<<16) | ((RR << 2) - 1))); \
	} while (0)

	stop = false;
	while (!stop) {
		switch (step) {
			u32 w0;
			u32 w1;
		case 2:
			KK = DD >> 10;
			if (KK > 0)
				step = 5;
			else
				step = 3;
			break;
		case 3:
			AA -= CC;
			if (AA > 0)
				E2_1 += GG;
			else {
				VV = *(u32*)(DMEM + 0x564);
				E2_1 = imagePtr + _SHIFTR(VV, 16, 16) + _SHIFTR(VV, 0, 16) * (-AA);
				VV = *(u32*)(DMEM + 0x57C);
				AA += _SHIFTR(VV, 16, 16);
			}
			step = 4;
			break;
		case 4:
			DD += EE;
			VV = *(u32*)(DMEM + 0x570);
			CC = (s32)(_SHIFTR(VV, 0, 16));
			GG = *(u32*)(DMEM + 0x578);
			F1_1 = 0;
			step = 2;
			break;
		case 5:
			FF -= KK;
			DD &= 0x03FF;
			if (FF < 0) {
				CC += ((objBg.scaleH * FF) >> 10) + 1;
				KK += FF;
				VV = *(u32*)(DMEM + 0x570);
				if (CC - (s32)(_SHIFTR(VV, 0, 16)) > 0)
					CC = (s32)(_SHIFTR(VV, 0, 16));
			}
			step = 6;
			break;
		case 6:
			LL = JJ + KK;
			VV = *(u32*)(DMEM + 0x550);
			P = _SHIFTR(VV, 0, 16);
			MM = CC + _SHIFTR(VV, 24, 8);
			NN = AA - _SHIFTR(VV, 16, 8);
			if (NN - MM < 0)
				step = 7;
			else
				step = 77;
			break;
		case 7:
			RR = MM - AA;
			AAA = AA;
			if (RR > 0) {
				VV = *(u32*)(DMEM + 0x564);
				SS = imagePtr + _SHIFTR(VV, 16, 16);
				if ((AAA & 1) != 0) {
					AAA--;
					RR++;
					SS -= _SHIFTR(VV, 0, 16);
				}
				AAA *= P;
				setTexture();
			}
			step = 8;
			break;
		case 77:
			RR = MM;
			SS = E2_1;
			runCommand(*(u32*)(DMEM + 0x568), SS);
			/*0x26000000, 0x00000000 */
			runCommand(0xF4000000, (((P + 0x6FF) << 16) | ((RR << 2) - 1)));
			AA -= CC;
			E2_1 += GG;
			step = 11;
			break;
		case 8:
			VV = *(u32*)(DMEM + 0x550);
			if (_SHIFTR(VV, 16, 8) != 0) {
				s16 BBB;
				u32 CCC;
				u16 DDD;
				u32 ZZZ;
				SS = imagePtr;
				BBB = NN;
				RR = NN & 1;
				VV = *(u32*)(DMEM + 0x564);
				if (RR != 0) {
					BBB--;
					SS -= _SHIFTR(VV, 0, 16);
				}
				CCC = E2_1 + BBB * _SHIFTR(VV, 0, 16);
				RR++;
				DDD = ((_SHIFTR(VV, 0, 16) - _SHIFTR(VV, 16, 16)) * 0x2000) >> 16;
				ZZZ = BBB * P;
				P -= DDD;
				AAA = ZZZ + DDD;
				setTexture();
				SS = CCC;
				AAA = ZZZ;
				P = DDD;
				setTexture();
			}
			step = 9;
			break;
		case 9:
			AA -= CC;
			if (NN <= 0) {
				runCommand(*(u32*)(DMEM + 0x56C), 0x27000000);
			} else {
				VV = *(u32*)(DMEM + 0x550);
				P = _SHIFTR(VV, 0, 16);
				SS = E2_1;
				RR = NN;
				AAA = 0;
				setTexture();
			}
			step = 10;
			break;
		case 10:
			if (AA > 0) {
				E2_1 += GG;
			} else {
				VV = *(u32*)(DMEM + 0x564);
				E2_1 = imagePtr + _SHIFTR(VV, 16, 16) + _SHIFTR(VV, 0, 16) * (-AA);
				VV = *(u32*)(DMEM + 0x57C);
				AA += _SHIFTR(VV, 16, 16);
			}
			step = 11;
			break;
		case 11:
			/*0x27000000, 0x00000000 */
			w0 = (G_TEXRECT << 24) | (LL << 2) | II;
			w1 = (JJ << 2) | HH;
			RDP.w2 = (H2 << 16) | F1_1;
			RDP.w3 = (objBg.scaleW << 16) | objBg.scaleH;
			gli_RDP_TexRect(w0, w1);
			if (FF <= 0)
				stop = true;
			else {
				JJ = LL;
				DD = DD + EE;
				VV = *(u32*)(DMEM + 0x570);
				CC = _SHIFTR(VV, 0, 16);
				GG = *(u32*)(DMEM + 0x578);
				F1_1 = 0;
				step = 2;
			}
			break;
		}
	}
	}

	if (gli_s2dex_native_res_texrects())
		gli_s2dex_background_drawing(0);
	RSP.LLE = keepLLE;
}

static
void BgRectCopyStripped(u32 _bgAddr)
{
	uObjBg objBg;
	u32 imagePtr;
	bool keepLLE;
	s16 Aw;
	s16 Bw;
	s16 Cw;
	s16 Dw;
	s16 Ew;
	s16 Ah;
	s16 Bh;
	s16 Ch;
	s16 Dh;
	s16 Eh;
	s16 F;
	s16 G;
	s16 H;
	u32 I;
	u32 J;
	u32 K;
	u32 L;
	u32 M;
	const u16 * aSize16;
	u16 imageSzIdx;
	u16 N0;
	u16 N1;
	u16 N2;
	u16 O;
	u32 P;
	u16 Q;
	u32 R;
	u32 S;
	u16 T;
	u16 A1;
	u16 A2;
	u16 A3;
	u32 T0;
	u16 T1;
	u32 T2;
	s16 AT;
	s16 U;
	u32 V;
	u32 X;
	u32 Y;
	u32 Z;
	u32 AA;
	u32 w0;
	u32 w1;
	u16 S5;
	u16 BB;
	u32 step;
	bool stop;
	static const u32 aSize[] = {
		0x003F0800,
		0x10000080,
		0x001F1000,
		0x20000100,
		0x000F2000,
		0x40000200,
		0x00074000,
		0x80000400
	};
	/* Step 1 */
	objBg = *(const uObjBg*)(RDRAM + _bgAddr);
	imagePtr = RSP_SegmentToPhysical(objBg.imagePtr);
	gDP.otherMode.cycleType = G_CYC_COPY;
	gDP.changed |= CHANGED_CYCLETYPE;
	keepLLE = RSP.LLE;
	RSP.LLE = true;

	/* Step 2 */
	Aw = MAX(0, objBg.frameX + objBg.frameW - gDP.scissor.xl);
	Bw = MIN(0, objBg.frameX - gDP.scissor.xh);
	Cw = objBg.frameW + Bw - Aw;
	if (Cw <= 0)
		{ RSP.LLE = keepLLE; return; }
	Dw = (((objBg.imageX * 0x2000) >> 16) & 0xFFFC) - Bw;
	Ew = objBg.frameX - Bw;

	Ah = MAX(0, objBg.frameY + objBg.frameH - gDP.scissor.yl);
	Bh = MIN(0, objBg.frameY - gDP.scissor.yh);
	Ch = objBg.frameH + Bh - Ah;
	if (Ch <= 0)
		{ RSP.LLE = keepLLE; return; }
	Dh = (((objBg.imageY * 0x2000) >> 16) & 0xFFFC) - Bh;
	Eh = objBg.frameY - Bh;

	F = Dh - objBg.imageH;
	G = (F >= 0) ? F : Dh;
	H = (objBg.imageFlip != 0) ? Dw + Aw : Dw;

	/* Step 3 */
	I = (objBg.imageLoad == G_BGLT_LOADTILE) ? 0xFFFFFFFF : 0U;
	J = objBg.tmemW << 9;
	K = (G_SETTILE << 24) | 0x100000 | (J & I);
	L = (objBg.imageFmt << 2) | objBg.imageSiz;
	L = (L << 0x13) | J;
	M = (objBg.imagePal << 0x14) | 0x0007C1F0;

	runCommand(K, 0x27000000);
	runCommand((G_SETTILESIZE<<24), 0);
	runCommand(((G_SETTILE<<24) | L), M);

	/* Step 4 */
	aSize16 = (const u16*)(aSize);
	imageSzIdx = objBg.imageSiz << 2;
	N0 = aSize16[(0 + imageSzIdx) ^ 1];
	N1 = aSize16[(1 + imageSzIdx) ^ 1];
	N2 = aSize16[(2 + imageSzIdx) ^ 1];
	G = (G * 0x4000) >> 16;
	O = (N0 & H) + Cw;
	P = ((N1 * H) >> 16) + objBg.tmemSizeW * G;
	Q = (N2 * O) >> 16;
	R = (objBg.imageFlip != 0) ? (((1 - O) * 8) << 16) : (((N0 & H) * 8) << 16);
	S = ((P >> 1) << 3) + imagePtr;
	T = Ew + Cw - 1;

	A1 = objBg.imageH & 0xFFFC;
	A2 = G <<2;
	A3 = (N1 * H) >> 16;
	T0 = Ew << 0x0C;
	T1 = Eh;
	T2 = T << 0x0C;
	AT = Ch;
	U = A1 - A2;

	if (gli_s2dex_native_res_texrects())
		gli_s2dex_background_drawing(1);

	step = 4;
	stop = false;
	while (!stop) {
		switch (step) {
		case 4:
			if (U <= 0)
				stop = true;
			step = 5;
		break;
		case 5:
			if (A3 > 0)
				U -= 4;
			if (U > AT)
				U = AT;
			V =  0xE4000000 | T2;
			if (gs_s2dexversion == eVer1_7)
				X = (objBg.imageLoad == G_BGLT_LOADTILE) ? (Q << 2) - 1 : objBg.tmemLoadSH;
			else
				X = (objBg.imageLoad == G_BGLT_LOADTILE) ? (Q << 2) : objBg.tmemLoadSH;
			X = (X | 0x7000) << 0x0C;
			Y = 0xFD100000 | ((objBg.tmemSizeW << 1) - 1);
			AT -= U;
			step = (U <= 0) ? 8 : 55;
		break;
		case 55:
			if (gs_s2dexversion == eVer1_7)
				Z = (objBg.imageLoad == G_BGLT_LOADTILE) ? (objBg.tmemSize << 0x10) | objBg.tmemLoadSH : objBg.tmemSize;
			else
				Z = objBg.tmemSize;
			S5 = objBg.tmemH;
			AA = X | objBg.tmemLoadTH;
			step = 6;
		break;
		case 6:
			U -= S5;
			if (U < 0) {
				Z += objBg.tmemSizeW * U;
				S5 += U;
				AA = (objBg.imageLoad == G_BGLT_LOADTILE) ? X | (S5 - 1) : (((Z - 2) | 0xE000) << 0x0B) | objBg.tmemLoadTH;
			}
			step = 7;
			break;
		case 7:
			BB = T1 + S5 - 1;
			runCommand(Y, S);
			if (objBg.imageLoad == G_BGLT_LOADTILE)
				runCommand((G_LOADTILE<<24), AA);
			else
				runCommand((G_LOADBLOCK<<24), AA);
			w0 = V | BB;
			w1 = T0 | T1;
			RDP.w2 = R;
			RDP.w3 = 0x10000400;
			gli_RDP_TexRect(w0, w1);
			T1 = BB + 1;
			S += Z;
			if (U > 0)
				step = 6;
			else {
				if (AT <= 0)
					stop = true;
				step = 8;
			}
			break;
		case 8:
			if (A3 > 0) {
				A3 >>= 1;
				runCommand(Y, S);
				runCommand(((G_SETTILE<<24) | 0x35100000), 0x06000000);
				runCommand((G_LOADBLOCK<<24), (0x06000000 | (((((objBg.tmemSizeW >> 1) - A3) << 2) - 1) << 12)));
				runCommand(Y, imagePtr);
				runCommand(((G_SETTILE << 24) | 0x35100000 | ((objBg.tmemSizeW >> 1) - A3)), 0x06000000);
				runCommand((G_LOADBLOCK<<24), (0x06000000 | (((A3 << 2) - 1) << 12)));
				w0 = V | T1;
				w1 = T0 | T1;
				RDP.w2 = R;
				RDP.w3 = 0x10000400;
				gli_RDP_TexRect(w0, w1);
				T1 += 4;
				AT -= 4;
				if (AT <= 0)
					stop = true;
			}
			step = 9;
			break;
		case 9:
			S = imagePtr + (A3 << 3);
			U = AT;
			AT = 0;
			step = 55;
			break;
		}
	}

	if (gli_s2dex_native_res_texrects())
		gli_s2dex_background_drawing(0);
	RSP.LLE = keepLLE;
}

void gli_S2DEX_BG_1Cyc(u32 w0, u32 w1)
{
	const u32 bgAddr = RSP_SegmentToPhysical(w1);
	bool fbImage = false;
	if (_useOnePieceBgCode(bgAddr, &fbImage))
		BgRect1CycOnePiece(bgAddr, fbImage);
	else
		BgRect1CycStripped(bgAddr);
}

void gli_S2DEX_BG_Copy(u32 w0, u32 w1)
{
	const u32 bgAddr = RSP_SegmentToPhysical(w1);
	bool fbImage = false;
	if (_useOnePieceBgCode(bgAddr, &fbImage))
		BgRectCopyOnePiece(bgAddr, fbImage);
	else
		BgRectCopyStripped(bgAddr);
}

void gli_S2DEX_Obj_MoveMem(u32 w0, u32 w1)
{
	switch (_SHIFTR(w0, 0, 16)) {
	case S2DEX_MV_MATRIX:
		gSPObjMatrix(w1);
		break;
	case S2DEX_MV_SUBMUTRIX:
		gSPObjSubMatrix(w1);
		break;
	case S2DEX_MV_VIEWPORT:
		gli_gSPViewport(w1);
		break;
	}
}

void gli_S2DEX_MoveWord(u32 w0, u32 w1)
{
	switch (_SHIFTR(w0, 0, 8))
	{
	case G_MW_GENSTAT:
		gli_gSPSetStatus(_SHIFTR(w0, 0, 16), w1);
		break;
	default:
		gli_F3D_MoveWord(w0, w1);
		break;
	}
}

void gli_S2DEX_RDPHalf_0(u32 w0, u32 w1) {
	if (RSP.nextCmd == G_SELECT_DL) {
		gSP.selectDL.addr = _SHIFTR(w0, 0, 16);
		gSP.selectDL.sid = _SHIFTR(w0, 18, 8);
		gSP.selectDL.flag = w1;
		return;
	}
	if (RSP.nextCmd == G_RDPHALF_1) {
		gli_RDP_TexRect(w0, w1);
		return;
	}
	assert(false);
}

void gli_S2DEX_Select_DL(u32 w0, u32 w1)
{
	u8 sid;
	u32 flag;
	u32 mask;
	gSP.selectDL.addr |= (_SHIFTR(w0, 0, 16)) << 16;
	sid = gSP.selectDL.sid;
	flag = gSP.selectDL.flag;
	mask = w1;
	if ((gSP.status[sid] & mask) == flag)
		/* Do nothing; */
		return;

	gSP.status[sid] = (gSP.status[sid] & ~mask) | (flag & mask);

	switch (_SHIFTR(w0, 16, 8))
	{
	case G_DL_PUSH:
		gli_gSPDisplayList(gSP.selectDL.addr);
		break;
	case G_DL_NOPUSH:
		gli_gSPBranchList(gSP.selectDL.addr);
		break;
	}
}

void gli_S2DEX_Obj_RenderMode(u32 w0, u32 w1)
{
	gSP.objRendermode = w1;
}

void gli_S2DEX_Obj_Rectangle(u32 w0, u32 w1)
{
	gSPObjRectangle(w1);
}

void gli_S2DEX_Obj_Rectangle_R(u32 w0, u32 w1)
{
	gSPObjRectangleR(w1);
}

void gli_S2DEX_Obj_Sprite(u32 w0, u32 w1)
{
	gSPObjSprite(w1);
}

void gli_S2DEX_Obj_LoadTxtr(u32 w0, u32 w1)
{
	gSPObjLoadTxtr(w1);
}

void gli_S2DEX_Obj_LdTx_Rect(u32 w0, u32 w1)
{
	gSPObjLoadTxtr(w1);
	gSPObjRectangle(w1 + sizeof(uObjTxtr));
}

void gli_S2DEX_Obj_LdTx_Rect_R(u32 w0, u32 w1)
{
	gSPObjLoadTxtr(w1);
	gSPObjRectangleR(w1 + sizeof(uObjTxtr));
}

void gli_S2DEX_Obj_LdTx_Sprite(u32 w0, u32 w1)
{
	gSPObjLoadTxtr(w1);
	gSPObjSprite(w1 + sizeof(uObjTxtr));
}

void gli_S2DEX_Init()
{
	gli_gSPSetupFunctions();
	/* Set GeometryMode flags */
	GBI_InitFlags( F3DEX );
	gli_resetObjMtx();

	gli_GBIPCStackSize = 18;

	/*          GBI Command             Command Value			Command Function */
	GBI_SetGBI( G_SPNOOP,				F3D_SPNOOP,				gli_F3D_SPNoOp );
	GBI_SetGBI( G_BG_1CYC,				S2DEX_BG_1CYC,			gli_S2DEX_BG_1Cyc );
	GBI_SetGBI( G_BG_COPY,				S2DEX_BG_COPY,			gli_S2DEX_BG_Copy );
	GBI_SetGBI( G_OBJ_RECTANGLE,		S2DEX_OBJ_RECTANGLE,	gli_S2DEX_Obj_Rectangle );
	GBI_SetGBI( G_OBJ_SPRITE,			S2DEX_OBJ_SPRITE,		gli_S2DEX_Obj_Sprite );
	GBI_SetGBI( G_OBJ_MOVEMEM,			S2DEX_OBJ_MOVEMEM,		gli_S2DEX_Obj_MoveMem );
	GBI_SetGBI( G_DL,					F3D_DL,					gli_F3D_DList );
	GBI_SetGBI( G_SELECT_DL,			S2DEX_SELECT_DL,		gli_S2DEX_Select_DL );
	GBI_SetGBI( G_OBJ_RENDERMODE,		S2DEX_OBJ_RENDERMODE,	gli_S2DEX_Obj_RenderMode );
	GBI_SetGBI( G_OBJ_RECTANGLE_R,		S2DEX_OBJ_RECTANGLE_R,	gli_S2DEX_Obj_Rectangle_R );
	GBI_SetGBI( G_OBJ_LOADTXTR,			S2DEX_OBJ_LOADTXTR,		gli_S2DEX_Obj_LoadTxtr );
	GBI_SetGBI( G_OBJ_LDTX_SPRITE,		S2DEX_OBJ_LDTX_SPRITE,	gli_S2DEX_Obj_LdTx_Sprite );
	GBI_SetGBI( G_OBJ_LDTX_RECT,		S2DEX_OBJ_LDTX_RECT,	gli_S2DEX_Obj_LdTx_Rect );
	GBI_SetGBI( G_OBJ_LDTX_RECT_R,		S2DEX_OBJ_LDTX_RECT_R,	gli_S2DEX_Obj_LdTx_Rect_R );
	GBI_SetGBI( G_MOVEWORD,				F3D_MOVEWORD,			gli_S2DEX_MoveWord );
	GBI_SetGBI( G_SETOTHERMODE_H,		F3D_SETOTHERMODE_H,		gli_F3D_SetOtherMode_H );
	GBI_SetGBI( G_SETOTHERMODE_L,		F3D_SETOTHERMODE_L,		gli_F3D_SetOtherMode_L );
	GBI_SetGBI( G_ENDDL,				F3D_ENDDL,				gli_F3D_EndDL );
	GBI_SetGBI( G_RDPHALF_0,			S2DEX_RDPHALF_0,		gli_S2DEX_RDPHalf_0 );
	GBI_SetGBI( G_RDPHALF_1,			F3D_RDPHALF_1,			gli_F3D_RDPHalf_1 );
	GBI_SetGBI( G_RDPHALF_2,			F3D_RDPHALF_2,			gli_F3D_RDPHalf_2 );
	GBI_SetGBI(	G_LOAD_UCODE,			S2DEX_LOAD_UCODE,		gli_F3DEX_Load_uCode );
}

void gli_S2DEX_1_03_Init()
{
	gli_S2DEX_Init();
	gs_s2dexversion = eVer1_3;
}

void gli_S2DEX_1_05_Init()
{
	gli_S2DEX_Init();
	gs_s2dexversion = eVer1_5;
}

void gli_S2DEX_1_07_Init()
{
	gli_S2DEX_Init();
	gs_s2dexversion = eVer1_7;
}
