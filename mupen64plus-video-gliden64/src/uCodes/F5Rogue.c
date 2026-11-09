/* Star Wars Rogue Squadron ucode
 * Initial implementation ported from Lemmy's LemNemu plugin
 * Microcode decoding: olivieryuyu
 */

#include <assert.h>
#include <math.h>
#include <string.h>
#include <retro_miscellaneous.h>
#include <retro_inline.h>
#include "GLideN64.h"
#include "F3D.h"
#include "F3DEX.h"
#include "F5Rogue.h"
#include "N64.h"
#include "RSP.h"
#include "RDP.h"
#include "gSP.h"
#include "gDP.h"
#include "GBI.h"
#include "gli_drawer.h"

#define F3DSWRS_VTXCOLOR			0x02
#define F3DSWRS_MOVEMEM				0x03
#define F3DSWRS_VTX					0x04
#define F3DSWRS_TRI_GEN				0x05
#define F3DSWRS_DL					0x06
#define F3DSWRS_BRANCHDL			0x07

#define F3DSWRS_SETOTHERMODE_L_EX	0xB3
#define F3DSWRS_TRI2				0xB4
#define F3DSWRS_JUMPSWDL			0xB5
#define F3DSWRS_ENDDL				0xB8
#define F3DSWRS_MOVEWORD			0xBC
#define F3DSWRS_TEXRECT_GEN			0xBD
#define F3DSWRS_SETOTHERMODE_H_EX	0xBE
#define F3DSWRS_TRI1				0xBF

#define F3DSWRS_MV_TEXSCALE			0x82
#define F3DSWRS_MW_FOG_MULTIPLIER	0x08
#define F3DSWRS_MW_FOG_OFFSET		0x0A

static u32 G_SETOTHERMODE_H_EX, G_SETOTHERMODE_L_EX, G_JUMPSWDL;
static u32 F3DSWRS_ViewportAddress;
static u32 F3DSWRS_PerspMatrixAddress;

typedef struct SWRSTriangle
{
	u32 V0, V1, V2;
	int avrgZ;

} SWRSTriangle;

/* TriGen0001 sizes are a 12 bit byte count: 3 colors per 36 bytes */
#define SWRS_MAX_COLORS 345
/* and 3 vertex shorts per 10 bytes */
#define SWRS_MAX_VTX16 1232

/* every triangle a TriGen can make: 32 default ones and the options' extras */
#define SWRS_MAX_TRIANGLES 64

typedef struct SWRSTriangleList
{
	SWRSTriangle t[SWRS_MAX_TRIANGLES];
	u32 n;
} SWRSTriangleList;

static void TriGen_add(SWRSTriangleList * _list, u32 _v0, u32 _v1, u32 _v2)
{
	SWRSTriangle * t = &_list->t[_list->n++];
	t->V0 = _v0;
	t->V1 = _v1;
	t->V2 = _v2;
	t->avrgZ = 0;
}

#define SWAP_T(_type, _a, _b) do { _type _t = (_a); (_a) = (_b); (_b) = _t; } while (0)

/* Stable, biggest Z first. n is small: no sort needs to be faster. */
static void SWRSTriangle_sort(SWRSTriangle * _t, u32 _n)
{
	u32 i;
	for (i = 1; i < _n; ++i) {
		SWRSTriangle key = _t[i];
		u32 j = i;
		while (j > 0 && _t[j - 1].avrgZ < key.avrgZ) {
			_t[j] = _t[j - 1];
			--j;
		}
		_t[j] = key;
	}
}

static
SWRSTriangle TriGen0000_defaultTriangleOrder[32] = {
	/* D1	D1	D2	D2	D3	D6	D3	D6	D4	D4 */
	/* D7	D6	D8	D7	D9	D12	D8	D11	D10	D9 */
	/* D2	D7	D3	D8	D4	D7	D9	D12	D5	D10 */
	{ 0, 6, 1, 0 },
	{ 0, 5, 6, 0 },
	{ 1, 7, 2, 0 },
	{ 1, 6, 7, 0 },
	{ 2, 8, 3, 0 },
	{ 5, 11, 6, 0 },
	{ 2, 7, 8, 0 },
	{ 5, 10, 11, 0 },
	{ 3, 9, 4, 0 },
	{ 3, 8, 9, 0 },

	/* D7	D7	D11	D8	D11	D8	D9	D12	D9	D12 */
	/* D13	D12	D17	D14	D16	D13	D15	D18	D14	D17 */
	/* D8	D13	D12	D9	D17	D14	D10	D13	D15	D18 */
	{ 6, 12, 7, 0 },
	{ 6, 11, 12, 0 },
	{ 10, 16, 11, 0 },
	{ 7, 13, 8, 0 },
	{ 10, 15, 16, 0 },
	{ 7, 12, 13, 0 },
	{ 8, 14, 9, 0 },
	{ 11, 17, 12, 0 },
	{ 8, 13, 14, 0 },
	{ 11, 16, 17, 0 },

	/* D16	D13	D16	D13	D14	D17	D17	D14	D18	D18 */
	/* D22	D19	D21	D18	D20	D23	D22	D19	D24	D23 */
	/* D17	D14	D22	D19	D15	D18	D23	D20	D19	D24 */
	{ 15, 21, 16, 0 },
	{ 12, 18, 13, 0 },
	{ 15, 20, 21, 0 },
	{ 12, 17, 18, 0 },
	{ 13, 19, 14, 0 },
	{ 16, 22, 17, 0 },
	{ 16, 21, 22, 0 },
	{ 13, 18, 19, 0 },
	{ 17, 23, 18, 0 },
	{ 17, 22, 23, 0 },

	/* D19	D19 */
	/* D25	D24 */
	/* D20	D25 */
	{ 18, 24, 19, 0 },
	{ 18, 23, 24, 0 }
};

static
SWRSTriangle TriGen0001_defaultTriangleOrder[8] = {
	/* D1	D1	D2	D2	D4	D4	D5	D5 */
	/* D4	D5	D5	D6	D7	D8	D8	D9 */
	/* D5	D2	D6	D3	D8	D5	D9	D6 */
	{ 0, 3, 4, 0 },
	{ 0, 4, 1, 0 },
	{ 1, 4, 5, 0 },
	{ 1, 5, 2, 0 },
	{ 3, 6, 7, 0 },
	{ 3, 7, 4, 0 },
	{ 4, 7, 8, 0 },
	{ 4, 8, 5, 0 }
};

static INLINE
void _updateF5DL(void)
{
	/* Lemmy's note: */
	/* differs from the other DL commands because it does skip the first command */
	/* the first 32 bits are stored, because they are */
	/* used as branch target address in the command in the QUAD "slot" */
	RSP.F5DL[RSP.PCi] = _SHIFTR(*(u32*)&RDRAM[RSP.PC[RSP.PCi]], 0, 24);
}

void F3DSWRS_Mtx(u32 w0, u32 w1)
{
	const u32 param = _SHIFTR(w0, 16, 8);
	if ((param & G_MTX_PROJECTION) != 0)
		F3DSWRS_PerspMatrixAddress = _SHIFTR(w1, 0, 24);
	gli_F3D_Mtx(w0, w1);
}

void F3DSWRS_VertexColor(u32 _w0, u32 _w1)
{
	gli_gSPSetVertexColorBase(_w1);
}

void F3DSWRS_MoveMem(u32 _w0, u32 _w1)
{
	switch (_SHIFTR(_w0, 16, 8)) {
	case F3D_MV_VIEWPORT:/*G_MV_VIEWPORT: */
		F3DSWRS_ViewportAddress = _SHIFTR((RSP.PC[RSP.PCi] + 8), 0, 24);
		gli_gSPViewport(F3DSWRS_ViewportAddress);
		break;
	case F3DSWRS_MV_TEXSCALE:
		gSP.textureCoordScale[0] = *(u32*)&RDRAM[RSP.PC[RSP.PCi] + 16];
		gSP.textureCoordScale[1] = *(u32*)&RDRAM[RSP.PC[RSP.PCi] + 12];
		break;
	}
	RSP.PC[RSP.PCi] += 16;
}

void F3DSWRS_Vtx(u32 _w0, u32 _w1)
{

	const u32 address = RSP_SegmentToPhysical(_w1);
	const u32 n = _SHIFTR(_w0, 10, 6);

	if ((address + sizeof(SWVertex)* n) > RDRAMSize)
		return;

	gli_gSPSWVertexN((const SWVertex*)&RDRAM[address], n);
}

static
void F3DSWRS_PrepareVertices(const u32* _vert,
							 const u8* _colorbase,
							 const u32* _colorIdx,
							 const u8* _texbase,
							 bool _useTex,
							 bool _persp,
							 u32 _num)
{
	u32 i;
	const u32 sscale = _SHIFTR(gSP.textureCoordScale[0], 16, 16);
	const u32 tscale = _SHIFTR(gSP.textureCoordScale[0], 0, 16);
	const u32 sscale1 = _SHIFTR(gSP.textureCoordScale[1], 16, 16);
	const u32 tscale1 = _SHIFTR(gSP.textureCoordScale[1], 0, 16);

	SPVertex * vertices = gli_drawer_vertices();

	for (i = 0; i < _num; ++i) {
		SPVertex * vtx = &vertices[_vert != NULL ? _vert[i] : i];
		const u8 *color = _colorbase + _colorIdx[i];
		vtx->r = _FIXED2FLOATCOLOR(color[3], 8 );
		vtx->g = _FIXED2FLOATCOLOR(color[2], 8 );
		vtx->b = _FIXED2FLOATCOLOR(color[1], 8 );
		vtx->a = _FIXED2FLOATCOLOR(color[0], 8 );

		if (_useTex) {
			u32 VMUDN_S;
			u32 VMUDN_T;
			s16 low_acum_S;
			s16 low_acum_T;
			u32 VMADH_S;
			u32 VMADH_T;
			s16 hi_acum_S;
			s16 hi_acum_T;
			s16 scaledS;
			s16 scaledT;
			const u32 st = *(u32*)&_texbase[4 * i];
			u32 s = (s16)_SHIFTR(st, 16, 16);
			u32 t = (s16)_SHIFTR(st, 0, 16);
			if ((s & 0x8000) != 0)
				s |= ~0xffff;
			if ((t & 0x8000) != 0)
				t |= ~0xffff;
			VMUDN_S = s * sscale;
			VMUDN_T = t * tscale;
			low_acum_S = _SHIFTR(VMUDN_S, 16, 16);
			low_acum_T = _SHIFTR(VMUDN_T, 16, 16);
			VMADH_S = s * sscale1;
			VMADH_T = t * tscale1;
			hi_acum_S = _SHIFTR(VMADH_S, 0, 16);
			hi_acum_T = _SHIFTR(VMADH_T, 0, 16);
			scaledS = low_acum_S + hi_acum_S;
			scaledT = low_acum_T + hi_acum_T;

			if (_persp) {
				vtx->s = _FIXED2FLOAT(scaledS, 5);
				vtx->t = _FIXED2FLOAT(scaledT, 5);
			} else {
				vtx->s = _FIXED2FLOAT(scaledS, 4);
				vtx->t = _FIXED2FLOAT(scaledT, 4);
			}
		}
	}
}

static
void TriGen0000_PrepareColorData(const u32 * _params, u32 * _data)
{
	const u32 * data32;
	assert((_params[3] & 0x07) % 4 == 0);
	data32 = (const u32*)&RDRAM[_SHIFTR(_params[3], 0, 24)];
	memcpy(_data, data32, 25 * sizeof(u32));
}

static
void TriGen0001_PrepareColorData(const u32 * _params, u32 * _data)
{
	u32 dataSize;
	u32 dataAddr;
	const u32 * data32;
	u32 idx32;
	u32 n;
	assert((_params[3] & 0x07) % 4 == 0);
	dataSize = (((_SHIFTR(_params[4], 0, 16) + 0x13) & 0xFF0) - (_params[3] & 0x07)) / 4;
	dataAddr = (_SHIFTR(_params[3], 0, 24) & 0x00FFFFF8) +(_params[3] & 0x07);
	data32 = (const u32*)&RDRAM[dataAddr];

	idx32 = 0;
	n = 0;
	do {
		_data[n++] = data32[idx32++];
		idx32++;
		_data[n++] = data32[idx32++];
		idx32++;
		_data[n++] = data32[idx32];
		idx32 += 6;
	} while(dataSize > idx32 && n + 3 <= SWRS_MAX_COLORS);
}

static
void TriGen0000_PrepareVtxData(const u32 * _params, u16 * _data)
{
	u32 i;
	const u32 dataAddr = _SHIFTR(_params[2], 0, 24) & 0x00FFFFF8;
	const s8* rdram_data8 = (const s8*)(RDRAM + dataAddr);

	u32 idx8 = _params[2] & 0x07;
	u32 dataIdx = 0;

	for (i = 0; i < 25; ++i) {
		u16 res;
		const s8 val = rdram_data8[idx8^3];
		++idx8;
		res = _SHIFTL(val, 4, 8) | (val < 0 ? 0xF000 : 0);
		_data[dataIdx^1] = res;
		++dataIdx;
	}
}

static
void TriGen0001_PrepareVtxData(const u32 * _params, u16 * _data)
{
	u32 dataAddr;
	const s8 * rdram_data8;
	u32 idx8;
	u32 dataIdx;
	const u32 dataSize = (_SHIFTR(_params[4], 16, 16) + 0x16) & 0xFF0;
	memset(_data, 0, SWRS_MAX_VTX16 * sizeof(u16));
	dataAddr = _SHIFTR(_params[2], 0, 24) & 0x00FFFFF8;

	rdram_data8 = (const s8*)(RDRAM + dataAddr);

	idx8 = _params[2] & 0x07;
	dataIdx = 0;

	do {
		u32 i;
		for (i = 0; i < 3; ++i) {
			u16 res;
			const s8 val = rdram_data8[idx8^3];
			idx8 += 2;
			res = _SHIFTL(val, 4, 8) | (val < 0 ? 0xF000 : 0);
			_data[dataIdx^1] = res;
			++dataIdx;
		}
		idx8 += 4;
	} while(dataSize > idx8);
}

static INLINE
void TriGen00_correctVertexForOption(u16 * _vtxData, u32 _src1, u32 _src2, u32 _dst)
{
	const s16 vd = (s16)_vtxData[_src1] + (s16)_vtxData[_src2];
	_vtxData[_dst] = (u16)(vd / 2);
}

static INLINE
void TriGen00_correctColorForOption(u32* _pColor, u32 _src1, u32 _src2, u32 _dst)
{
	u32 i;
	u8* pSrc1 = (u8*)(_pColor + _src1);
	u8* pSrc2 = (u8*)(_pColor + _src2);
	u8* pDst =  (u8*)(_pColor + _dst);
	for (i = 0; i < 4; ++i) {
		const u16 c = pSrc1[i] + pSrc2[i];
		pDst[i] = _SHIFTR(c, 1, 8);
	}
}

static INLINE
void TriGen00_correctVertexForPonderation(u16 * _vtxData, u32 _src1, u32 _src2, u32 _dst, u16 ponderation)
{
	const s16 * vtxData = (const s16*)_vtxData;
	s32 vd = vtxData[_dst] * (0x10000 - ponderation);
	vd += ((vtxData[_src1] + vtxData[_src2]) * ponderation) >> 1;
	_vtxData[_dst] = _SHIFTR(vd, 16, 16);
}

static INLINE
void TriGen00_correctColorForPonderation(u32* _pColor, u32 _src1, u32 _src2, u32 _dst, u16 ponderation)
{
	u32 i;
	u8* pSrc1 = (u8*)(_pColor + _src1);
	u8* pSrc2 = (u8*)(_pColor + _src2);
	u8* pDst =  (u8*)(_pColor + _dst);
	for (i = 0; i < 4; ++i) {
		const u32 c = pDst[i] * (0x10000 - ponderation) + (((pSrc1[i] + pSrc2[i]) * ponderation) >> 1);
		pDst[i] = _SHIFTR(c, 16, 8);
	}
}

static
void TriGen0000_option_00000100(const u32 * _params, u16 * _vtxData, u32 * _colorData, SWRSTriangleList * _triangles)
{
	if ((_params[1] & 0xFF00) != 0x0100)
		return;

	/* Correct vertex data */
	TriGen00_correctVertexForOption(_vtxData, 1, 3, 0);
	TriGen00_correctVertexForOption(_vtxData, 3, 5, 2);

	/* Correct color data */
	TriGen00_correctColorForOption(_colorData, 0, 2, 1);
	TriGen00_correctColorForOption(_colorData, 2, 4, 3);

	/* Additional triangle */
	TriGen_add(_triangles, 0, 10, 5);
	TriGen_add(_triangles, 10, 20, 15);
}

static
void TriGen0000_ponderation1(const u32 * _params, u16 * _vtxData, u32 * _colorData)
{
	const u16 ponderation1 = _SHIFTR(_params[6], 0, 16);
	if (ponderation1 == 0)
		return;

	/* Correct vertex data */
	TriGen00_correctVertexForPonderation(_vtxData, 1, 3, 0, ponderation1);
	TriGen00_correctVertexForPonderation(_vtxData, 3, 5, 2, ponderation1);

	/* Correct color data */
	TriGen00_correctColorForPonderation(_colorData, 0, 2, 1, ponderation1);
	TriGen00_correctColorForPonderation(_colorData, 2, 4, 3, ponderation1);
}

static
void TriGen0000_option_00000001(const u32 * _params, u16 * _vtxData, u32 * _colorData, SWRSTriangleList * _triangles)
{
	if ((_params[1] & 0xFF) != 0x01)
		return;

	/* Correct vertex data */
	TriGen00_correctVertexForOption(_vtxData, 21, 23, 20);
	TriGen00_correctVertexForOption(_vtxData, 23, 25, 22);

	/* Correct color data */
	TriGen00_correctColorForOption(_colorData, 20, 22, 21);
	TriGen00_correctColorForOption(_colorData, 22, 24, 23);

	/* Additional triangle */
	TriGen_add(_triangles, 4, 9, 14);
	TriGen_add(_triangles, 14, 19, 24);
}

static
void TriGen0000_ponderation2(const u32 * _params, u16 * _vtxData, u32 * _colorData)
{
	const u16 ponderation2 = _SHIFTR(_params[7], 16, 16);
	if (ponderation2 == 0)
		return;

	/* Correct vertex data */
	TriGen00_correctVertexForPonderation(_vtxData, 21, 23, 20, ponderation2);
	TriGen00_correctVertexForPonderation(_vtxData, 23, 25, 22, ponderation2);

	/* Correct color data */
	TriGen00_correctColorForPonderation(_colorData, 20, 22, 21, ponderation2);
	TriGen00_correctColorForPonderation(_colorData, 22, 24, 23, ponderation2);
}

static
void TriGen0000_option_01000000(const u32 * _params, u16 * _vtxData, u32 * _colorData, SWRSTriangleList * _triangles)
{
	if ((_params[1] & 0xFF000000) != 0x01000000)
		return;

	/* Correct vertex data */
	TriGen00_correctVertexForOption(_vtxData, 1, 11, 4);
	TriGen00_correctVertexForOption(_vtxData, 11, 21,  14);

	/* Correct color data */
	TriGen00_correctColorForOption(_colorData, 0, 10, 5);
	TriGen00_correctColorForOption(_colorData, 10, 20, 15);

	/* Additional triangle */
	TriGen_add(_triangles, 0, 1, 2);
	TriGen_add(_triangles, 2, 3, 4);
}

static
void TriGen0000_ponderation3(const u32 * _params, u16 * _vtxData, u32 * _colorData)
{
	const u16 ponderation3 = _SHIFTR(_params[5], 0, 16);
	if (ponderation3 == 0)
		return;

	/* Correct vertex data */
	TriGen00_correctVertexForPonderation(_vtxData, 1, 11, 4, ponderation3);
	TriGen00_correctVertexForPonderation(_vtxData, 11, 21, 14, ponderation3);

	/* Correct color data */
	TriGen00_correctColorForPonderation(_colorData, 0, 10, 5, ponderation3);
	TriGen00_correctColorForPonderation(_colorData, 10, 20, 15, ponderation3);
}

static
void TriGen0000_option_00010000(const u32 * _params, u16 * _vtxData, u32 * _colorData, SWRSTriangleList * _triangles)
{
	if ((_params[1] & 0xFF0000) != 0x010000)
		return;

	/* Correct vertex data */
	TriGen00_correctVertexForOption(_vtxData, 5, 15, 8);
	TriGen00_correctVertexForOption(_vtxData, 15, 25, 18);

	/* Correct color data */
	TriGen00_correctColorForOption(_colorData, 4, 14, 9);
	TriGen00_correctColorForOption(_colorData, 14, 24, 19);

	/* Additional triangle */
	TriGen_add(_triangles, 22, 24, 23);
	TriGen_add(_triangles, 20, 22, 21);
}

static
void TriGen0000_ponderation4(const u32 * _params, u16 * _vtxData, u32 * _colorData)
{
	const u16 ponderation4 = _SHIFTR(_params[6], 16, 16);
	if (ponderation4 == 0)
		return;

	/* Correct vertex data */
	TriGen00_correctVertexForPonderation(_vtxData, 5, 15, 8, ponderation4);
	TriGen00_correctVertexForPonderation(_vtxData, 15, 25, 18, ponderation4);

	/* Correct color data */
	TriGen00_correctColorForPonderation(_colorData, 4, 14, 9, ponderation4);
	TriGen00_correctColorForPonderation(_colorData, 14, 24, 19, ponderation4);
}

static
void TriGen0000_ponderation5(const u32 * _params, u16 * _vtxData, u32 * _colorData)
{
	const u16 ponderation5 = _SHIFTR(_params[5], 16, 16);
	if (ponderation5 == 0)
		return;

	/* Correct vertex data */
	TriGen00_correctVertexForPonderation(_vtxData, 1, 13, 7, ponderation5);
	TriGen00_correctVertexForPonderation(_vtxData, 3, 13, 6, ponderation5);
	TriGen00_correctVertexForPonderation(_vtxData, 3, 15, 9, ponderation5);
	TriGen00_correctVertexForPonderation(_vtxData, 11, 13, 10, ponderation5);
	TriGen00_correctVertexForPonderation(_vtxData, 13, 15, 12, ponderation5);
	TriGen00_correctVertexForPonderation(_vtxData, 11, 23, 17, ponderation5);
	TriGen00_correctVertexForPonderation(_vtxData, 13, 23, 16, ponderation5);
	TriGen00_correctVertexForPonderation(_vtxData, 13, 25, 19, ponderation5);

	/* Correct color data */
	TriGen00_correctColorForPonderation(_colorData, 0, 12, 6, ponderation5);
	TriGen00_correctColorForPonderation(_colorData, 2, 12, 7, ponderation5);
	TriGen00_correctColorForPonderation(_colorData, 2, 14, 8, ponderation5);
	TriGen00_correctColorForPonderation(_colorData, 10, 12, 11, ponderation5);
	TriGen00_correctColorForPonderation(_colorData, 12, 14, 13, ponderation5);
	TriGen00_correctColorForPonderation(_colorData, 10, 22, 16, ponderation5);
	TriGen00_correctColorForPonderation(_colorData, 12, 22, 17, ponderation5);
	TriGen00_correctColorForPonderation(_colorData, 12, 24, 18, ponderation5);
}

static
void TriGen0001_option_01010201(const u32 * _params,
								u16 * _vtxData,
								u32 * _colorData,
								SWRSTriangleList * _triangles)
{
	if ((_params[1] & 0xFF00) != 0x0200)
		return;

	/* Correct vertex data */
	TriGen00_correctVertexForOption(_vtxData, 1, 3, 0);

	/* Correct color data */
	TriGen00_correctColorForOption(_colorData, 0, 2, 1);

	/* Additional triangle */
	TriGen_add(_triangles, 0, 6, 3);
}

static
void TriGen0001_ponderation1(const u32 * _params, u16 * _vtxData, u32 * _colorData)
{
	const u16 ponderation1 = _SHIFTR(_params[6], 0, 16);
	if (ponderation1 == 0)
		return;

	/* Correct vertex data */
	TriGen00_correctVertexForPonderation(_vtxData, 1, 3, 0, ponderation1);

	/* Correct color data */
	TriGen00_correctColorForPonderation(_colorData, 0, 2, 1, ponderation1);
}

static
void TriGen0001_option_01010102(const u32 * _params,
								u16 * _vtxData,
								u32 * _colorData,
								SWRSTriangleList * _triangles)
{
	if ((_params[1] & 0xFF) != 0x02)
		return;

	/* Correct vertex data */
	TriGen00_correctVertexForOption(_vtxData, 7, 9, 8);

	/* Correct color data */
	TriGen00_correctColorForOption(_colorData, 6, 8, 7);

	/* Additional triangle */
	TriGen_add(_triangles, 2, 5, 8);
}

static
void TriGen0001_ponderation2(const u32 * _params, u16 * _vtxData, u32 * _colorData)
{
	const u16 ponderation2 = _SHIFTR(_params[7], 16, 16);
	if (ponderation2 == 0)
		return;

	/* Correct vertex data */
	TriGen00_correctVertexForPonderation(_vtxData, 7, 9, 6, ponderation2);

	/* Correct color data */
	TriGen00_correctColorForPonderation(_colorData, 6, 8, 7, ponderation2);
}

static
void TriGen0001_option_02010101(const u32 * _params,
								u16 * _vtxData,
								u32 * _colorData,
								SWRSTriangleList * _triangles)
{
	if ((_params[1] & 0xFF000000) != 0x02000000)
		return;

	/* Correct vertex data */
	TriGen00_correctVertexForOption(_vtxData, 1, 7, 2);

	/* Correct color data */
	TriGen00_correctColorForOption(_colorData, 0, 6, 3);

	/* Additional triangle */
	TriGen_add(_triangles, 0, 1, 2);
}

static
void TriGen0001_ponderation3(const u32 * _params, u16 * _vtxData, u32 * _colorData)
{
	const u16 ponderation3 = _SHIFTR(_params[5], 0, 16);
	if (ponderation3 == 0)
		return;

	/* Correct vertex data */
	TriGen00_correctVertexForPonderation(_vtxData, 1, 7, 2, ponderation3);

	/* Correct color data */
	TriGen00_correctColorForPonderation(_colorData, 0, 6, 3, ponderation3);
}

static
void TriGen0001_option_01020101(const u32 * _params,
								u16 * _vtxData,
								u32 * _colorData,
								SWRSTriangleList * _triangles)
{
	if ((_params[1] & 0x00FF0000) != 0x00020000)
		return;

	/* Correct vertex data */
	TriGen00_correctVertexForOption(_vtxData, 3, 9, 4);

	/* Correct color data */
	TriGen00_correctColorForOption(_colorData, 2, 8, 5);

	/* Additional triangle */
	TriGen_add(_triangles, 6, 8, 7);
}

static
void TriGen0001_ponderation4(const u32 * _params, u16 * _vtxData, u32 * _colorData)
{
	const u16 ponderation4 = _SHIFTR(_params[6], 16, 16);
	if (ponderation4 == 0)
		return;

	/* Correct vertex data */
	TriGen00_correctVertexForPonderation(_vtxData, 3, 9, 4, ponderation4);

	/* Correct color data */
	TriGen00_correctColorForPonderation(_colorData, 2, 8, 5, ponderation4);
}

static
void TriGen0001_ponderation5(const u32 * _params, u16 * _vtxData, u32 * _colorData)
{
	const u16 ponderation5 = _SHIFTR(_params[5], 16, 16);
	if (ponderation5 == 0)
		return;

	/* Correct vertex data */
	TriGen00_correctVertexForPonderation(_vtxData, 1, 9, 5, ponderation5);

	/* Correct color data */
	TriGen00_correctColorForPonderation(_colorData, 0, 8, 4, ponderation5);
}

typedef struct Vector2 {
	union {
		u16 vec16[4];
		u32 vec32[2];
		u64 qw;
	} data;
} Vector2;

static Vector2 Vector2_ADD(Vector2 _a, const Vector2 * _b)
{
	u32 i;
	for (i = 0; i < 4; ++i)
		_a.data.vec16[i] = _a.data.vec16[i] + _b->data.vec16[i];
	return _a;
}

static
void TriGen02_BuildVtxData(const u32 * _params, u32 * _output)
{
	Vector2  v0, v1, v2, v3;
	u16 V0, V1;
	v0.data.qw = v1.data.qw = v2.data.qw = v3.data.qw = 0;
	V0 = _SHIFTR(_params[9], 0, 16);
	V1 = _SHIFTR(_params[8], 0, 16);
	V1 <<= 4;
	v0.data.vec16[1] = (_SHIFTR(_params[8], 16, 16));
	v0.data.vec16[0] = (V1);
	v0.data.vec16[3] = (_SHIFTR(_params[9], 16, 16));
	v2.data.vec16[1] = (V0);
	v3.data.vec16[3] = (V0);
	v1.data.vec16[0] = (_SHIFTR(_params[1], 16, 16));
	v1 = Vector2_ADD(v1, &v0);
	_output[0] = v1.data.vec32[0];
	_output[1] = v1.data.vec32[1];
	v1 = v2;
	v1.data.vec16[0] = (_SHIFTR(_params[1], 0, 16));
	v1 = Vector2_ADD(v1, &v0);
	_output[2] = v1.data.vec32[0];
	_output[3] = v1.data.vec32[1];
	v1 = v3;
	v1.data.vec16[0] = (_SHIFTR(_params[2], 16, 16));
	v1 = Vector2_ADD(v1, &v0);
	_output[4] = v1.data.vec32[0];
	_output[5] = v1.data.vec32[1];
	v1 = Vector2_ADD(v2, &v3);
	v1.data.vec16[0] = (_SHIFTR(_params[2], 0, 16));
	v1 = Vector2_ADD(v1, &v0);
	_output[6] = v1.data.vec32[0];
	_output[7] = v1.data.vec32[1];
}

static
u32 TriGen00_BuildVtxData(const u32 * _params, u32 _step, const u16 * _input, u32 * _output)
{
	u16 V0;
	u16 V1;
	u32 bound;
	u32 startDataIdx;
	u32 n = 0;
	Vector2 v0, v1, v2, v3;
	v0.data.qw = v1.data.qw = v2.data.qw = v3.data.qw = 0;
	V0 = _SHIFTR(_params[9], 0, 16);
	V1 = _SHIFTR(_params[8], 0, 16);
	V1 <<= 4;
	v0.data.vec16[1] = (_SHIFTR(_params[8], 16, 16));
	v0.data.vec16[3] = (_SHIFTR(_params[9], 16, 16));
	v0.data.vec16[0] = (V1);
	v2.data.vec16[1] = (V0);
	v3.data.vec16[3] = (V0);

	bound = _step * (_step - 1) + 1;
	for (startDataIdx = 0; startDataIdx < _step; ++startDataIdx) {
		u32 idx;
		for (idx = 0; idx < bound; idx += _step) {
			Vector2 v4;
			v1.data.vec16[0] = (_input[(startDataIdx + idx) ^ 1]);
			v4 = Vector2_ADD(v1, &v0);
			_output[n++] = v4.data.vec32[0];
			_output[n++] = v4.data.vec32[1];
			v1 = Vector2_ADD(v1, &v3);
		}
		v1 = Vector2_ADD(v1, &v2);
		v1.data.vec16[3] = (0);
	}
	return n;
}

static
void TriGen00_BuildColorIndices(u32 _step, u32 _vtxSize, u32 * _colorIndices)
{
	u32 i;
	u32 startColorIdx = 0;
	u32 colorIdx = 0;
	for (i = 0; i < _vtxSize; i += _step) {
		u32 j;
		for (j = 0; j < _step; ++j) {
			_colorIndices[colorIdx++] = (startColorIdx + j * _step) << 2;
		}
		startColorIdx++;
	}
}

static
void TriGen00_BuildTextureCoords(u32 _step, const u32 * _params, u32 * _texCoords)
{
	u32 texIdx;
	u32 i;
	const u16 tex = _SHIFTR(_params[7], 0, 16);
	union {
		u16 w[2];
		u32 dw;
	} texCoord;
	texIdx = 0;
	for (i = 0; i < _step; ++i) {
		u32 j;
		texCoord.w[0] = tex * (_step - 1);
		texCoord.w[1] = tex * i;
		for (j = 0; j < _step; ++j) {
			_texCoords[texIdx++] = texCoord.dw;
			texCoord.dw -= tex;
		}
	}
}

static INLINE
int screenZ(const SPVertex * _v)
{
	return (int)(((_v->z / _v->w) * gSP.viewport.vscale[2] + gSP.viewport.vtrans[2])*32768.0f);
}

static
int calcAvrgZ(const SPVertex * _vertices, const SWRSTriangle * _t)
{
	int zsum = screenZ(&_vertices[_t->V0]);
	zsum += screenZ(&_vertices[_t->V1]);
	zsum += screenZ(&_vertices[_t->V2]);
	return zsum / 3;
}

static
void TriGen0000(void)
{
	u16 vtxData16[26];
	SWRSTriangleList triangles;
	u32 vtxData32[64];
	u32 words;
	u32 vtxSize;
	u32 colorIndices[32];
	u32 texCoords[32];
	const SPVertex * vertices;
	u32 i;
	const u32* params = (const u32*)&RDRAM[RSP.PC[RSP.PCi]];

	/* Step 1. Load data from RDRAM */

	u32 colorData[25];
	TriGen0000_PrepareColorData(params, colorData);

	memset(vtxData16, 0, sizeof(vtxData16));
	TriGen0000_PrepareVtxData(params, vtxData16);

	triangles.n = 0;

	/* Step 1.1 Options */

	TriGen0000_option_00000100(params, vtxData16, colorData, &triangles);
	TriGen0000_ponderation1(params, vtxData16, colorData);
	TriGen0000_option_00000001(params, vtxData16, colorData, &triangles);
	TriGen0000_ponderation2(params, vtxData16, colorData);
	TriGen0000_option_01000000(params, vtxData16, colorData, &triangles);
	TriGen0000_ponderation3(params, vtxData16, colorData);
	TriGen0000_option_00010000(params, vtxData16, colorData, &triangles);
	TriGen0000_ponderation4(params, vtxData16, colorData);
	TriGen0000_ponderation5(params, vtxData16, colorData);

	/* Step 2. Build vertex data from bytes obtained on Step 1. */

	words = TriGen00_BuildVtxData(params, 5, vtxData16, vtxData32);

	/* Step 3. Process vertices */
	vtxSize = words / 2;
	gli_gSPSWVertexN((const SWVertex*)vtxData32, vtxSize);

	/* Step 4. Prepare color indices and texture coordinates. Prepare vertices for rendering */

	TriGen00_BuildColorIndices(5, vtxSize, colorIndices);

	TriGen00_BuildTextureCoords(5, params, texCoords);

	F3DSWRS_PrepareVertices(NULL, (u8*)colorData,
							colorIndices, (u8*)texCoords, true, false, vtxSize);

	/* Step 5. Prepare triangles, sort them by z and draw. */

	vertices = gli_drawer_vertices();

	for (i = 0; i < triangles.n; ++i)
		triangles.t[i].avrgZ = calcAvrgZ(vertices, &triangles.t[i]);
	for (i = 0; i < 32; ++i) {
		triangles.t[triangles.n] = TriGen0000_defaultTriangleOrder[i];
		triangles.t[triangles.n].avrgZ = calcAvrgZ(vertices, &TriGen0000_defaultTriangleOrder[i]);
		triangles.n++;
	}
	SWRSTriangle_sort(triangles.t, triangles.n);

	for (i = 0; i < triangles.n; ++i)
		gli_gSP1Triangle(triangles.t[i].V0, triangles.t[i].V1, triangles.t[i].V2);
	gli_drawer_draw_triangles();
}

static
void TriGen0001(void)
{
	u16 vtxData16[SWRS_MAX_VTX16];
	SWRSTriangleList triangles;
	u32 vtxData32[64];
	u32 words;
	u32 vtxSize;
	u32 colorIndices[32];
	u32 texCoords[32];
	const SPVertex * vertices;
	u32 i;
	const u32* params = (const u32*)&RDRAM[RSP.PC[RSP.PCi]];

	/* Step 1. Load data from RDRAM */

	u32 colorData[SWRS_MAX_COLORS];
	TriGen0001_PrepareColorData(params, colorData);

	TriGen0001_PrepareVtxData(params, vtxData16);

	triangles.n = 0;

	/* Step 1.1 Options */

	TriGen0001_option_01010201(params, vtxData16, colorData, &triangles);
	TriGen0001_ponderation1(params, vtxData16, colorData);
	TriGen0001_option_01010102(params, vtxData16, colorData, &triangles);
	TriGen0001_ponderation2(params, vtxData16, colorData);
	TriGen0001_option_02010101(params, vtxData16, colorData, &triangles);
	TriGen0001_ponderation3(params, vtxData16, colorData);
	TriGen0001_option_01020101(params, vtxData16, colorData, &triangles);
	TriGen0001_ponderation4(params, vtxData16, colorData);
	TriGen0001_ponderation5(params, vtxData16, colorData);

	/* Step 2. Build vertex data from bytes obtained on Step 1. */

	words = TriGen00_BuildVtxData(params, 3, vtxData16, vtxData32);

	/* Step 3. Process vertices */
	vtxSize = words / 2;
	gli_gSPSWVertexN((const SWVertex*)vtxData32, vtxSize);

	/* Step 4. Prepare color indices and texture coordinates. Prepare vertices for rendering */

	TriGen00_BuildColorIndices(3, vtxSize, colorIndices);

	TriGen00_BuildTextureCoords(3, params, texCoords);

	F3DSWRS_PrepareVertices(NULL, (u8*)colorData,
							colorIndices, (u8*)texCoords, true, false, vtxSize);

	/* Step 5. Prepare triangles, sort them by z and draw. */

	vertices = gli_drawer_vertices();

	for (i = 0; i < triangles.n; ++i)
		triangles.t[i].avrgZ = calcAvrgZ(vertices, &triangles.t[i]);
	for (i = 0; i < 8; ++i) {
		triangles.t[triangles.n] = TriGen0001_defaultTriangleOrder[i];
		triangles.t[triangles.n].avrgZ = calcAvrgZ(vertices, &TriGen0001_defaultTriangleOrder[i]);
		triangles.n++;
	}
	SWRSTriangle_sort(triangles.t, triangles.n);

	for (i = 0; i < triangles.n; ++i)
		gli_gSP1Triangle(triangles.t[i].V0, triangles.t[i].V1, triangles.t[i].V2);
	gli_drawer_draw_triangles();
}

static
void TriGen02(void)
{
	u32 v1;
	u32 v2;
	u32 v3;
	u32 v4;
	u32 vert[4];
	u32 colorbase[4];
	u32 color[4];
	u32 tex;
	u32 texbase[4];
	SPVertex * vertices = gli_drawer_vertices();
	const SPVertex * vtx2;
	const SPVertex * vtx3;
	const u32* params = (const u32*)&RDRAM[RSP.PC[RSP.PCi]];

	u32 vecdata[8];
	TriGen02_BuildVtxData(params, vecdata);
	gli_gSPSWVertexN((const SWVertex*)&vecdata[0], 4);

	v1 = 0;
	v2 = 1;
	v3 = 2;
	v4 = 3;
	vert[0] = v1;
	vert[1] = v2;
	vert[2] = v3;
	vert[3] = v4;

	colorbase[0] = params[3];
	colorbase[1] = params[4];
	colorbase[2] = params[5];
	colorbase[3] = params[6];
	color[0] = 0;
	color[1] = 4;
	color[2] = 8;
	color[3] = 12;

	tex = _SHIFTR(params[7], 0, 16);
	texbase[0] = tex;
	texbase[1] = tex | (tex << 16);
	texbase[2] = 0;
	texbase[3] = (tex << 16);

	F3DSWRS_PrepareVertices(vert, (u8*)colorbase, color, (u8*)texbase, true, false, 4);

	vtx2 = &vertices[v2];
	vtx3 = &vertices[v3];

	if (vtx3->z / vtx3->w > vtx2->z / vtx2->w)
		gli_gSP2Triangles(v1, v2, v4, 0, v1, v4, v3, 0);
	else
		gli_gSP2Triangles(v1, v4, v3, 0, v1, v2, v4, 0);
	gli_drawer_draw_triangles();
}

void F3DSWRS_TriGen(u32 _w0, u32 _w1)
{
	u32 mode;

	const u32 nextCmd = RSP.nextCmd;
	RSP.nextCmd = G_TRI1;

	mode = _SHIFTR(_w0, 8, 8);
	switch (mode) {
	case 0x00:
	{
		const u32 mode00 = _SHIFTR(_w0, 0, 8);
		switch (mode00) {
		case 0x00:
			TriGen0000();
			break;
		case 0x01:
			TriGen0001();
			break;
		default:
			break;
		}
	}
		break;
	case 0x02:
		TriGen02();
		break;
	default:
		break;
	}

	RSP.nextCmd = nextCmd;
	RSP.PC[RSP.PCi] += 32;
}

void F3DSWRS_JumpSWDL(u32 _w0, u32 _w1)
{
	RSP.PC[RSP.PCi] = RSP.F5DL[RSP.PCi];
	_updateF5DL();
}

void F3DSWRS_DList(u32 _w0, u32 _w1)
{
	gli_gSPDisplayList(_w1);
	_updateF5DL();
}

void F3DSWRS_BranchDList(u32 _w0, u32 _w1)
{
	gli_gSPBranchList(_w1);
	_updateF5DL();
}

void F3DSWRS_EndDisplayList(u32 _w0, u32 _w1)
{
	gli_gSPEndDisplayList();
/*	_updateSWDL(); */
}

static
void _addVertices(const u32 _vert[3], SPVertex * _vertices)
{
	const SPVertex * vtx0;
	SPVertex * dma;
	u32 i;

	if (GLI_IS_CLIPPED(_vertices, _vert[0], _vert[1], _vert[2]))
		return;

	vtx0 = &_vertices[_vert[(((RSP.w1 >> 24) & 3) % 3)]];
	dma = gli_drawer_take_dma_vertices(3);

	for (i = 0; i < 3; ++i) {
		SPVertex * vtx = &_vertices[_vert[i]];

		if ((gSP.geometryMode & G_SHADE) == 0) {
			/* Prim shading */
			vtx->flat_r = gDP.primColor.r;
			vtx->flat_g = gDP.primColor.g;
			vtx->flat_b = gDP.primColor.b;
			vtx->flat_a = gDP.primColor.a;
		} else if ((gSP.geometryMode & G_SHADING_SMOOTH) == 0) {
			/* Flat shading */
			vtx->r = vtx->flat_r = vtx0->r;
			vtx->g = vtx->flat_g = vtx0->g;
			vtx->b = vtx->flat_b = vtx0->b;
			vtx->a = vtx->flat_a = vtx0->a;
		}

		if (gDP.otherMode.depthSource == G_ZS_PRIM)
			vtx->z = gDP.primDepth.z * vtx->w;

		dma[i] = *vtx;
	}
}

void F3DSWRS_Tri(u32 _w0, u32 _w1)
{
	u32 colorParam;
	u32 colorIdx[4];
	bool useTex;
	const u8 * texbase;
	SPVertex * vertices;
	u32 vert1[3];
	const bool bTri2 = RSP.cmd == F3DSWRS_TRI2;
	const u32 v1 = (_SHIFTR(_w1, 13, 11) & 0x7F8) / 40;
	const u32 v2 = (_SHIFTR( _w1,  5, 11 ) & 0x7F8) / 40;
	const u32 v3 = ((_w1 <<  3) & 0x7F8) / 40;
	const u32 v4 = (_SHIFTR( _w1,  21, 11 ) & 0x7F8) / 40;
	u32 vert[4];
	vert[0] = v1;
	vert[1] = v2;
	vert[2] = v3;
	vert[3] = v4;

	colorParam = *(u32*)&RDRAM[RSP.PC[RSP.PCi] + 8];
	colorIdx[0] = _SHIFTR(colorParam, 16, 8);
	colorIdx[1] = _SHIFTR(colorParam, 8, 8);
	colorIdx[2] = _SHIFTR(colorParam, 0, 8);
	colorIdx[3] = _SHIFTR(colorParam, 24, 8);

	useTex = (_w0 & 2) != 0;
	texbase = RDRAM + RSP.PC[RSP.PCi] + 16;
	F3DSWRS_PrepareVertices(vert, RDRAM + gSP.vertexColorBase, colorIdx, texbase, useTex, gDP.otherMode.texturePersp != 0, bTri2 ? 4 : 3);

	if (useTex)
		RSP.PC[RSP.PCi] += 16;

	RSP.nextCmd = _SHIFTR(*(u32*)&RDRAM[RSP.PC[RSP.PCi] + 16], 24, 8);
	vertices = gli_drawer_vertices();
	vert1[0] = v1;
	vert1[1] = v2;
	vert1[2] = v3;
	_addVertices(vert1, vertices);
	if (bTri2) {
		u32 vert2[3];
		vert2[0] = v1;
		vert2[1] = v3;
		vert2[2] = v4;
		_addVertices(vert2, vertices);
	}
	if (RSP.nextCmd != G_TRI1 && RSP.nextCmd != G_TRI2) {
		const u32 geometryMode = gSP.geometryMode;
		if ((gSP.geometryMode & G_CULL_BOTH) == G_CULL_BOTH)
			gSP.geometryMode &= ~(G_CULL_FRONT);
		gli_drawer_draw_dma_pending();
		gSP.geometryMode = geometryMode;
	}
	RSP.PC[RSP.PCi] += 8;
}

void F3DSWRS_MoveWord(u32 _w0, u32 _w1)
{
	switch (_SHIFTR(_w0, 0, 8)){
	case G_MW_CLIP:
		gli_gSPClipRatio( _w1 );
		break;
	case G_MW_SEGMENT:
		gli_gSPSegment( _SHIFTR( _w0, 8, 16 ) >> 2, _w1 & 0x00FFFFFF );
		break;
	case F3DSWRS_MW_FOG_MULTIPLIER:
		gSP.fog.multiplierf = _FIXED2FLOAT((s32)_w1, 16);
		gSP.changed |= CHANGED_FOGPOSITION;
		break;
	case F3DSWRS_MW_FOG_OFFSET:
		gSP.fog.offsetf = _FIXED2FLOAT((s32)_w1, 16);
		gSP.changed |= CHANGED_FOGPOSITION;
		break;
	case G_MW_PERSPNORM:
		gli_gSPPerspNormalize( _w1 );
		break;
	}
}

void F3DSWRS_TexrectGen(u32 _w0, u32 _w1)
{
	const SPVertex * v;
	f32 screenX;
	f32 screenY;
	bool flip;
	u32 w_i;
	u32 viewport;
	u32 viewportX;
	u32 viewportY;
	const u32 * perspMatrix;
	u32 perspMatrixX;
	u32 perspMatrixY;
	u64 param3X;
	u64 param3Y;
	u32 offset_x_i;
	u32 offset_y_i;
	f32 offset_x_f;
	f32 offset_y_f;
	f32 ulx;
	f32 lrx;
	f32 uly;
	f32 lry;
	u32 param4X;
	u32 param4Y;
	float intpart;
	f32 frac_x_f;
	u32 combMatrixFracX;
	f32 frac_y_f;
	u32 combMatrixFracY;
	u16 dsdx_i;
	u16 dtdy_i;
	u16 E;
	u16 F;
	f32 dsdx;
	f32 dtdy;
	u16 S;
	u16 T;
	u32 primColor;

	const u32 vtxIdx = ((_w0 >> 5) & 0x07F8) / 40;

	const u32* params = (const u32*)&RDRAM[RSP.PC[RSP.PCi]];

	RSP.PC[RSP.PCi] += 16;

	v = &gli_drawer_vertices()[vtxIdx];
	if (v->clip != 0)
		return;

	screenX = v->x / v->w * gSP.viewport.vscale[0] + gSP.viewport.vtrans[0];
	screenY = -v->y / v->w * gSP.viewport.vscale[1] + gSP.viewport.vtrans[1];

	flip = (_w0 & 1) != 0;

	w_i = MAX(1U, (u32)(v->w));
	viewport = *(u32*)&RDRAM[F3DSWRS_ViewportAddress];
	viewportX = _SHIFTR(viewport, 17, 15);
	viewportY = _SHIFTR(viewport, 1, 15);
	perspMatrix = (u32*)&RDRAM[F3DSWRS_PerspMatrixAddress];
	perspMatrixX = (perspMatrix[0] & 0xFFFF0000) | _SHIFTR(perspMatrix[8], 16, 16);
	perspMatrixY = _SHIFTL(perspMatrix[2], 16, 16) | _SHIFTR(perspMatrix[10], 0, 16);
	param3X = _SHIFTR(params[3], 16, 16);
	param3Y = _SHIFTR(params[3], 0, 16);
	if (flip)
		SWAP_T(u64, param3X, param3Y);

	offset_x_i = (u32)(((param3X * viewportX * perspMatrixX) / w_i) >> 16);
	offset_y_i = (u32)(((param3Y * viewportY * perspMatrixY) / w_i) >> 16);
	offset_x_f = _FIXED2FLOAT(offset_x_i, 2);
	offset_y_f = _FIXED2FLOAT(offset_y_i, 2);

	ulx = screenX - offset_x_f;
	lrx = screenX + offset_x_f;
	if (lrx - ulx <= 0.0f)
		return;
	uly = screenY - offset_y_f;
	lry = screenY + offset_y_f;
	if (lry - uly <= 0.0f)
		return;

	param4X = params[4] & 0xFFFF0000;
	param4Y = _SHIFTL(params[4], 16, 16);
	frac_x_f = fabs(modff(gSP.matrix.combined[0][0], &intpart));
	combMatrixFracX = (u32)(frac_x_f*65536.0f);
	frac_y_f = fabs(modff(gSP.matrix.combined[0][1], &intpart));
	combMatrixFracY = (u32)(frac_y_f*65536.0f);
	param4X |= combMatrixFracX;
	param4Y |= combMatrixFracY;

	if (flip)
		SWAP_T(u32, offset_x_i, offset_y_i);

	dsdx_i = (u16)((param4X / offset_x_i) >> 10);
	dtdy_i = (u16)((param4Y / offset_y_i) >> 10);
	E = 0;
	F = 0;

	if ((_w0 & 2) != 0) {
		dsdx_i = -dsdx_i;
		E = _SHIFTR(params[4], 16, 16);
	}

	if ((_w0 & 4) != 0) {
		dtdy_i = -dtdy_i;
		F = _SHIFTR(params[4], 0, 16);
	}

	dsdx = _FIXED2FLOAT((s16)dsdx_i, 10);
	dtdy = _FIXED2FLOAT((s16)dtdy_i, 10);

	if (flip)
		SWAP_T(u16, dsdx_i, dtdy_i);


	if (ulx > 0)  {
		S = E + 0xFFF0;
	} else {
		const int ulx_i = (int)(ulx * 4.0f);
		S = ((0 - (dsdx_i << 6) * ulx_i) << 3) + E + 0xFFF0;
	}

	if (uly > 0)  {
		T = F + 0xFFF0;
	} else {
		const int uly_i = (int)(uly * 4.0f);
		T = ((0 - (dtdy_i << 6) * uly_i) << 3) + F + 0xFFF0;
	}

	gDP.primDepth.z = v->z / v->w * gSP.viewport.vscale[2] + gSP.viewport.vtrans[2];
	gDP.primDepth.deltaZ = 0.0f;

	primColor = params[1];
	gDP.primColor.r = _FIXED2FLOATCOLOR(_SHIFTR(primColor, 24, 8), 8);
	gDP.primColor.g = _FIXED2FLOATCOLOR(_SHIFTR(primColor, 16, 8), 8);
	gDP.primColor.b = _FIXED2FLOATCOLOR(_SHIFTR(primColor,  8, 8), 8);
	gDP.primColor.a = _FIXED2FLOATCOLOR(_SHIFTR(primColor,  0, 8), 8);

	if ((gSP.geometryMode & G_FOG) != 0) {
		const u32 fogColor = (params[1] & 0xFFFFFF00) | (u32)(v->a*255.0f);
		gli_gDPSetFogColor( _SHIFTR( fogColor, 24, 8 ),	/* r */
		                _SHIFTR( fogColor, 16, 8 ),	/* g */
		                _SHIFTR( fogColor,  8, 8 ),	/* b */
		                _SHIFTR( fogColor,  0, 8 ) );	/* a */
	}

	gli_gDPTextureRectangle(ulx, uly, lrx, lry, gSP.texture.tile, (s16)S, (s16)T, dsdx, dtdy, flip);
}

void F3DSWRS_SetOtherMode_H_EX(u32 _w0, u32 _w1)
{
	RSP.PC[RSP.PCi] += 8;
	gDP.otherMode.h &= *(u32*)&RDRAM[RSP.PC[RSP.PCi]];
	gDP.otherMode.h |= _w1;
}

void F3DSWRS_SetOtherMode_L_EX(u32 _w0, u32 _w1)
{
	RSP.PC[RSP.PCi] += 8;
	gDP.otherMode.l &= *(u32*)&RDRAM[RSP.PC[RSP.PCi]];
	gDP.otherMode.l |= _w1;
}

void gli_F5Rogue_Init(void)
{
	gli_gSPSetupFunctions();
	/* Set GeometryMode flags */
	GBI_InitFlags( F3D );

	gli_GBIPCStackSize = 10;

	/*          GBI Command             Command Value				Command Function */
	GBI_SetGBI( G_SPNOOP,				F3D_SPNOOP,					gli_F3D_SPNoOp );
	GBI_SetGBI( G_MTX,					F3D_MTX,					F3DSWRS_Mtx );
	GBI_SetGBI( G_RESERVED0,			F3DSWRS_VTXCOLOR,			F3DSWRS_VertexColor );
	GBI_SetGBI( G_MOVEMEM,				F3DSWRS_MOVEMEM,			F3DSWRS_MoveMem );
	GBI_SetGBI( G_VTX,					F3DSWRS_VTX,				F3DSWRS_Vtx );
	GBI_SetGBI( G_RESERVED1,			F3DSWRS_TRI_GEN,			F3DSWRS_TriGen );
	GBI_SetGBI( G_DL,					F3DSWRS_DL,					F3DSWRS_DList );
	GBI_SetGBI( G_RESERVED2,			F3DSWRS_BRANCHDL,			F3DSWRS_BranchDList );
	GBI_SetGBI( G_RESERVED3,			F3D_RESERVED3,				gli_F3D_Reserved3 );

	GBI_SetGBI( G_TRI1,					F3DSWRS_TRI1,				F3DSWRS_Tri );
	GBI_SetGBI( G_SETOTHERMODE_H_EX,	F3DSWRS_SETOTHERMODE_H_EX,	F3DSWRS_SetOtherMode_H_EX );
	GBI_SetGBI( G_POPMTX,				F3DSWRS_TEXRECT_GEN,		F3DSWRS_TexrectGen );
	GBI_SetGBI( G_MOVEWORD,				F3DSWRS_MOVEWORD,			F3DSWRS_MoveWord );
	GBI_SetGBI( G_TEXTURE,				F3D_TEXTURE,				gli_F3D_Texture );
	GBI_SetGBI( G_SETOTHERMODE_H,		F3D_SETOTHERMODE_H,			gli_F3D_SetOtherMode_H );
	GBI_SetGBI( G_SETOTHERMODE_L,		F3D_SETOTHERMODE_L,			gli_F3D_SetOtherMode_L );
	GBI_SetGBI( G_ENDDL,				F3DSWRS_ENDDL,				F3DSWRS_EndDisplayList );
	GBI_SetGBI( G_SETGEOMETRYMODE,		F3D_SETGEOMETRYMODE,		gli_F3D_SetGeometryMode );
	GBI_SetGBI( G_CLEARGEOMETRYMODE,	F3D_CLEARGEOMETRYMODE,		gli_F3D_ClearGeometryMode );
	GBI_SetGBI( G_JUMPSWDL,				F3DSWRS_JUMPSWDL,			F3DSWRS_JumpSWDL );
	GBI_SetGBI( G_TRI2,					F3DSWRS_TRI2,				F3DSWRS_Tri );
	GBI_SetGBI( G_SETOTHERMODE_L_EX,	F3DSWRS_SETOTHERMODE_L_EX,	F3DSWRS_SetOtherMode_L_EX );
}
