/* Ucodes for "Indiana Jones and the Infernal Machine" and "Star Wars Episode I - Battle for Naboo".
 * Microcode decoding: olivieryuyu
 */

#include <assert.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <string.h>
#include <retro_miscellaneous.h>
#include "GLideN64.h"
#include "F3D.h"
#include "F5Indi_Naboo.h"
#include "RSP.h"
#include "RDP.h"
#include "gSP.h"
#include "gDP.h"
#include "gli_drawer.h"


#define F5INDI_MOVEMEM			0x01
#define F5INDI_SET_DLIST_ADDR	0x02
#define F5INDI_GEOMETRY_GEN		0x05
#define F5INDI_DL				0x06
#define F5INDI_BRANCHDL			0x07
#define F5INDI_TRI2				0xB4
#define F5INDI_JUMPDL			0xB5
#define F5INDI_ENDDL			0xB8
#define F5INDI_MOVEWORD			0xBC
#define F5INDI_TRI1				0xBF
#define F5INDI_TEXRECT			0xE4
#define F5INDI_SETOTHERMODE		0xBA
#define F5INDI_SETOTHERMODE_C	0xB9
#define F5NABOO_BE				0xBE

#define CAST_DMEM(type, addr) ((type)(DMEM + (addr)))
#define CAST_RDRAM(type, addr) ((type)(RDRAM + (addr)))

#define SWAP_T(_type, _a, _b) do { _type _t = (_a); (_a) = (_b); (_b) = _t; } while (0)

/* std::max(0.0f, x): a NaN gives 0 */
static f32 F5_max0(f32 _x)
{
	return (0.0f < _x) ? _x : 0.0f;
}

static u32 G_SET_DLIST_ADDR, G_BE, G_JUMPDL, G_INDI_TEXRECT;

static struct {
	s32 mtx_st_adjust[16];
	f32 mtx_vtx_gen[4][4];
} indiData;

typedef struct NabooData {
	u16 AA;
	s16 BB;
	s16 EE;
	s16 HH;
	u8 DD;
	u8 CC;
	u8 TT;
	u8 pad;
} NabooData;

static NabooData nabooData;

static
void _updateF5DL(void)
{
	RSP.F5DL[RSP.PCi] = _SHIFTR(*CAST_RDRAM(u32*, RSP.PC[RSP.PCi]), 0, 24);
}

static
void F5INDI_LoadSTMatrix(void)
{
	u32 i;
	const u16* mtx_i = CAST_DMEM(const u16*, 0xE40);
	const u16* mtx_f = CAST_DMEM(const u16*, 0xE60);
	s32 * mtx = indiData.mtx_st_adjust;
	for (i = 0; i < 16; ++i)
		mtx[i] = (mtx_i[i ^ 1] << 16) | mtx_f[i ^ 1];
}

static
void F5INDI_DMA_Direct(u32 _w0, u32 _w1)
{
	u32 i;
	u32 * pSrc = CAST_RDRAM(u32*, _SHIFTR(_w1, 0, 24));
	u32 * pDst = CAST_DMEM(u32*, _SHIFTR(_w0, 8, 12));
	const u32 count = (_SHIFTR(_w0, 0, 8) + 1) >> 2;
	for (i = 0; i < count; ++i) {
		pDst[i] = pSrc[i];
	}
}

static
void F5INDI_DMA_Segmented(u32 _w0, u32 _w1)
{
	u32 * FD4;
	u32 * FD8;
	u32 * FDC;
	s32 A0;
	u32 A2;
	s32 A3;
	s32 A1;
	u32 V1;
	u32 V0;
	u32 T0;
	if (_SHIFTR(_w0, 0, 8) == 0)
		return;
	FD4 = CAST_DMEM(u32*, 0xFD4);
	FD8 = CAST_DMEM(u32*, 0xFD8);
	FDC = CAST_DMEM(u32*, 0xFDC);
	A0 = _SHIFTR(_w0, 0, 8);
	A2 = *FDC;
	A3 = A0 + 1;
	A1 = A2 - A3;
	V1 = _SHIFTR(_w0, 8, 12);
	while (A1 < 0) {
		u32 * pSrc;
		u32 V0 = *FD4;
		A3 = A0 - A2;
		memcpy(DMEM + V1, RDRAM + _SHIFTR(V0, 0, 24), A2);

		V1 += A2;
		A0 = A3;
		_w1 = *FD8;

		pSrc = CAST_RDRAM(u32*, _SHIFTR(_w1, 0, 24));
		*FD8 = *pSrc;
		*FDC = 0x100;
		*FD4 = _w1 + 0x08;

		A2 = *FDC;
		A3 = A0 + 1;
		A1 = A2 - A3;
	}

	V0 = *FD4;
	T0 = V0 + A3;
	memcpy(DMEM + V1, RDRAM + _SHIFTR(V0, 0, 24), A0 + 1);
	*FDC = A1;
	*FD4 = T0;
}

static
void F5INDI_Lighting_Overlay1(u32 _w0, u32 _w1)
{
	u8 L;
	u32 lightAddr;
	u32 dataAddr;
	const u32 S = ((_SHIFTR(_w1, 24, 7) << 2) + 4) & 0xFFF8;
	memset(DMEM + 0xD40, 0, S);

	L = *(DMEM + (0x58B ^ 3));
	if (L == 0) {
		*CAST_DMEM(u64*, 0x05B0) = 0U;
		return;
	}

	lightAddr = 0xB10;
	dataAddr = _SHIFTR(_w0, 8, 12);

	while (true) {
		u8 * dst = DMEM + 0xD40;
		const u8* data = DMEM + dataAddr;
		const u8* light = DMEM + lightAddr;
		if (*light == 0) {
			u32 i;
			const u32 count = S >> 3;
			for (i = 0; i < count; ++i) {
				u32 j;
				const u8* factor = DMEM + 0x158;
				dst[0] = (data[0] * factor[0]) >> 8;
				for (j = 1; j < 4; ++j) {
					dst[j] = MIN(((data[j] * factor[j] * light[j]) >> 16) + dst[j], 0xFF);
				}
				factor += 4;
				dst += 4;
				data += 4;
				dst[0] = (data[0] * factor[0]) >> 8;
				for (j = 1; j < 4; ++j) {
					dst[j] = MIN(((data[j] * factor[j] * light[j]) >> 16) + dst[j], 0xFF);
				}
				dst += 4;
				data += 4;
			}
		} else {
			u32 count;
			u32 i;
			u8* offsetAddr = DMEM + 0x380;
			u32 offsetAddrIdx = 0;
			u64 vtxAddrPos = *CAST_DMEM(u64*, 0x05B0);
			u32 factors[2];
			factors[0] = *CAST_DMEM(u32*, 0x158);
			factors[1] = *CAST_DMEM(u32*, 0x15C);
			count = S >> 3;
			for (i = 0; i < count; ++i) {
				u32 j;
				u32 vtxAddr[2];
				switch (vtxAddrPos & 3) {
				case 0:
					vtxAddr[0] = vtxAddr[1] = lightAddr - 0x10;
					break;
				case 1:
					vtxAddr[0] = lightAddr - 0x08;
					vtxAddr[1] = lightAddr - 0x10;
					break;
				case 2:
					vtxAddr[0] = lightAddr - 0x10;
					vtxAddr[1] = lightAddr - 0x08;
					break;
				case 3:
					vtxAddr[0] = vtxAddr[1] = lightAddr - 0x08;
					break;
				}
				vtxAddrPos >>= 2;
				for (j = 0; j < 2; ++j) {
					s16 Z1;
					s16 X;
					s16 Y;
					s16 Z;
					u64 T;
					u64 K;
					u32 P;
					u32 P1;
					u32 D;
					u32 E;
					u32 F;
					u32 J;
					const u32 * V = CAST_DMEM(const u32*, vtxAddr[j]);
					s16 X1 = (s16)_SHIFTR(*V, 16, 16);
					s16 Y1 = (s16)_SHIFTR(*V, 0, 16);
					++V;
					Z1 = (s16)_SHIFTR(*V, 16, 16);

					V = CAST_DMEM(const u32*, 0x170 + offsetAddr[offsetAddrIdx^3]);
					offsetAddrIdx++;
					X = (s16)_SHIFTR(*V, 16, 16);
					Y = (s16)_SHIFTR(*V, 0, 16);
					++V;
					Z = (s16)_SHIFTR(*V, 16, 16);
					X -= X1;
					Y -= Y1;
					Z -= Z1;

					T = *CAST_DMEM(const u32*, lightAddr + 4);
					T = 0xFFFF000000000000 | (T << 16);
					K = (X * X + Y * Y + Z * Z) * T;
					P = K >> 32;
					P1 = 0;
					if ((P & 0xFFFF0000) == 0xFFFF0000)
						P1 = P & 0xFFFF;

					V = CAST_DMEM(const u32*, lightAddr);

					D = (_SHIFTR(*V, 24, 8) * P1) >> 16;
					E = (_SHIFTR(*V, 16, 8) * P1) >> 16;
					F = (_SHIFTR(*V, 8, 8) * P1) >> 16;
					J = (_SHIFTR(factors[j], 0, 8) * data[0x3 ^ 0x3]) >> 8;

					if (*light == 4) {
						*dst = J;
						dst++;
						*dst = MIN(*dst + F, 0xFFU);
						dst++;
						*dst = MIN(*dst + E, 0xFFU);
						dst++;
						*dst = MIN(*dst + D, 0xFFU);
						dst++;
					} else {
						D += (_SHIFTR(factors[j], 24, 8) * data[0x0 ^ 0x3]) >> 8;
						E += (_SHIFTR(factors[j], 16, 8) * data[0x1 ^ 0x3]) >> 8;
						F += (_SHIFTR(factors[j], 8, 8) * data[0x2 ^ 0x3]) >> 8;
						*dst = J;
						dst++;
						*dst = MIN(F >> 1, 0xFFU);
						dst++;
						*dst = MIN(E >> 1, 0xFFU);
						dst++;
						*dst = MIN(D >> 1, 0xFFU);
						dst++;
					}
					data += 4;
				}
			}
		}

		--L;
		if (L == 0) {
			u32 A;
			u8 * dst;
			const u8 * factor;
			u32 count;
			u32 i;
			*CAST_DMEM(u64*, 0x05B0) = 0U;
			A = *CAST_DMEM(const u32*, 0x5A8);
			if (A == 0xFFFFFFFF)
				return;
			dst = DMEM + 0xD40;
			factor = DMEM + 0x5A8;
			count = S >> 2;
			for (i = 0; i < count; ++i) {
				u32 j;
				for (j = 0; j < 4; ++j) {
					dst[j] = (dst[j] * factor[j]) >> 8;
				}
				dst += 4;
			}
			return;
		}

		lightAddr += 0x18;
	}
}

static
void F5INDI_Lighting_Overlay2(u32 _w0, u32 _w1)
{
	u8 L;
	u32 lightAddr;
	u32 dataAddr;
	const u32 S = ((_SHIFTR(_w1, 24, 7) << 2) + 4) & 0xFFF8;
	memset(DMEM + 0xD40, 0, S + 8);

	L = *(DMEM + (0x58B ^ 3));
	if (L == 0) {
		*CAST_DMEM(u64*, 0x05B0) = 0U;
		return;
	}

	lightAddr = 0xB10;
	dataAddr = _SHIFTR(_w0, 8, 12);

	while (true) {
		u8 * dst = DMEM + 0xD40;
		const u8* data = DMEM + dataAddr;
		const u8* light = DMEM + lightAddr;
		if (*light == 0) {
			u32 i;
			const u32 count = S >> 3;
			for (i = 0; i < count; ++i) {
				u32 j;
				const u8* factor = DMEM + 0x158;
				dst[0] = (data[0] * factor[0]) >> 8;
				for (j = 1; j < 4; ++j) {
					dst[j] = MIN(((data[j] * factor[j] * light[j]) >> 16) + dst[j], 0xFF);
				}
				factor += 4;
				dst += 4;
				data += 4;
				dst[0] = (data[0] * factor[0]) >> 8;
				for (j = 1; j < 4; ++j) {
					dst[j] = MIN(((data[j] * factor[j] * light[j]) >> 16) + dst[j], 0xFF);
				}
				dst += 4;
				data += 4;
			}
		} else if (*light == 1) {
			u32 count;
			u32 i;
			u32 factor2Addr = 0x380;
			u64 vtxAddrPos = *CAST_DMEM(const u64*, 0x05B0);
			u32 factors[2];
			factors[0] = *CAST_DMEM(const u32*, 0x158);
			factors[1] = *CAST_DMEM(const u32*, 0x15C);
			count = S >> 3;
			for (i = 0; i < count; ++i) {
				u32 j;
				u32 vtxAddr[2];
				switch (vtxAddrPos & 3) {
				case 0:
					vtxAddr[0] = vtxAddr[1] = lightAddr - 0x10;
					break;
				case 1:
					vtxAddr[0] = lightAddr - 0x08;
					vtxAddr[1] = lightAddr - 0x10;
					break;
				case 2:
					vtxAddr[0] = lightAddr - 0x10;
					vtxAddr[1] = lightAddr - 0x08;
					break;
				case 3:
					vtxAddr[0] = vtxAddr[1] = lightAddr - 0x08;
					break;
				}
				vtxAddrPos >>= 2;
				for (j = 0; j < 2; ++j) {
					s16 Z;
					u32 I;
					u32 D;
					u32 E;
					u32 F;
					u32 K;
					u32 J;
					u32 H;
					u32 G;
					u32 V = *CAST_DMEM(const u32*, vtxAddr[j]);
					s16 X = (s16)_SHIFTR(V, 16, 16);
					s16 Y = (s16)_SHIFTR(V, 0, 16);
					V = *CAST_DMEM(const u32*, vtxAddr[j] + 0x04);
					Z = (s16)_SHIFTR(V, 16, 16);

					V = *CAST_DMEM(const u32*, factor2Addr);
					I = (u32)(((X * (s8)(_SHIFTR(V, 24, 8)) + Y * (s8)(_SHIFTR(V, 16, 8)) + Z * (s8)(_SHIFTR(V, 8, 8))) >> 8) & 0xFFFF);
					if ((I & 0x8000) != 0)
						I = 0;
					V = *CAST_DMEM(const u32*, lightAddr);
					D = (_SHIFTR(V, 24, 8) * I) >> 8;
					E = (_SHIFTR(V, 16, 8) * I) >> 8;
					F = (_SHIFTR(V, 8, 8) * I) >> 8;
					V = factors[j];
					K = _SHIFTR(V, 0, 8) * (*data++);
					J = _SHIFTR(V, 8, 8) * (*data++);
					H = _SHIFTR(V, 16, 8) * (*data++);
					G = _SHIFTR(V, 24, 8) * (*data++);
					*dst = K >> 8;
					dst++;
					*dst = MIN(*dst + ((F * J) >> 22), 0xFFU);
					dst++;
					*dst = MIN(*dst + ((E * H) >> 22), 0xFFU);
					dst++;
					*dst = MIN(*dst + ((D * G) >> 22), 0xFFU);
					dst++;
					factor2Addr += 4;
				}
			}
		} else {
			u32 count;
			u32 vtxAddr[2];
			u32 i;
			u32 offsetAddr = 0x380;
			count = S >> 3;
			vtxAddr[0] = lightAddr - 0x10;
			vtxAddr[1] = lightAddr - 0x10;
			for (i = 0; i < count; ++i) {
				u32 j;
				for (j = 0; j < 2; ++j) {
					s16 Z1;
					u32 offset;
					s16 X;
					s16 Y;
					s16 Z;
					u64 T;
					u64 K;
					u32 P;
					u32 P1;
					const u8 * factor;
					const u8 * light;
					u32 k;
					const u32 * V = CAST_DMEM(const u32*, vtxAddr[j]);
					s16 X1 = (s16)_SHIFTR(*V, 16, 16);
					s16 Y1 = (s16)_SHIFTR(*V, 0, 16);
					++V;
					Z1 = (s16)_SHIFTR(*V, 16, 16);

					offset = *CAST_DMEM(const u32*, offsetAddr + j * 4);
					V = CAST_DMEM(const u32*, 0x170 + _SHIFTR(offset, 0, 8));
					X = (s16)_SHIFTR(*V, 16, 16);
					Y = (s16)_SHIFTR(*V, 0, 16);
					++V;
					Z = (s16)_SHIFTR(*V, 16, 16);
					X -= X1;
					Y -= Y1;
					Z -= Z1;

					T = *CAST_DMEM(const u32*, lightAddr + 4);
					T = 0xFFFF000000000000 | (T << 16);
					K = (X * X + Y * Y + Z * Z) * T;
					P = (u32)(K >> 32);
					P1 = 0;
					if ((P & 0xFFFF0000) == 0xFFFF0000)
						P1 = P & 0xFFFF;

					factor = DMEM + 0x158 + j * 4;
					dst[0] = (data[0] * factor[0]) >> 8;
					light = DMEM + lightAddr;
					for (k = 1; k < 4; ++k) {
						dst[k] = MIN(((((light[k] * P1) >> 16) * data[k] * factor[k] + 0x8000) >> 16) + dst[k], 0xFFU);
					}
					dst += 4;
					data += 4;
				}
				offsetAddr += 8;
			}
		}

		--L;
		if (L == 0) {
			*CAST_DMEM(u64*, 0x05B0) = 0U;
			return;
		}

		lightAddr += 0x18;
	}
}

static
void F5INDI_Lighting_Basic(u32 _w0, u32 _w1)
{
	s32 count = _SHIFTR(_w1, 24, 7);
	const u8* factor = DMEM + 0x158;
	const u8* data = DMEM + _SHIFTR(_w0, 8, 12);
	u8 * dst = DMEM + 0xD40;
	while (count > 0) {
		u32 j;
		for (j = 0; j < 8; ++j) {
			dst[j] = (data[j] * factor[j]) >> 8;
		}
		dst += 8;
		data += 8;
		for (j = 0; j < 8; ++j) {
			dst[j] = (data[j] * factor[j]) >> 8;
		}
		dst += 8;
		data += 8;
		count -= 4;
	}
}

static
void F5INDI_RebuildAndAdjustColors(u32 _w0, u32 _w1)
{
	u32 i;
	const u32 addr = _SHIFTR(_w0, 8, 12);
	const u32 count = MIN(_SHIFTR(_w1, 24, 8), (0x588 - addr) >> 2);
	const u16* data = CAST_DMEM(const u16*, addr);
	u32 vres[256];
	for (i = 0; i < count; ++i) {
		u16 V = data[i ^ 1];
		u32 R = (V >> 8) & 0xF8;
		u32 G = (V & 0x7E0) >> 3;
		u32 B = (V & 0x1F) << 3;
		vres[i] = (R << 24) | (G << 16) | (B << 8) | 0xFF;
	}
	memcpy(DMEM + addr, vres, count << 2);
}

static
const SWVertex * F5INDI_LoadVtx(u32 _w0, u32 _w1, SWVertex * _vres)
{
	u32 i;
	const u32 count = _SHIFTR(_w1, 24, 8);
	const u32 dmem_addr = _SHIFTR(_w0, 8, 12);
	const u16* dmem_data = CAST_DMEM(const u16*, dmem_addr);
	const u32 base_addr = *CAST_DMEM(const u32*, 0x154);
	u32 * pres = (u32*)(_vres);
	const u16* v0_data = CAST_DMEM(const u16*, 0x128);
	const u16 X0 = v0_data[0x0 ^ 0x1];
	const u16 Y0 = v0_data[0x1 ^ 0x1];
	const u16 Z0 = v0_data[0x2 ^ 0x1];
	for (i = 0; i < count; ++i) {
		const u32 rdram_addr = dmem_data[i ^ 1] * 6 + base_addr;
		const u16* rdram_data = CAST_RDRAM(const u16*, (rdram_addr & 0x00FFFFF8));
		const u32 shift = (rdram_addr & 7) >> 1;
		const u16 X = rdram_data[(shift + 0) ^ 1] + X0;
		const u16 Y = rdram_data[(shift + 1) ^ 1] + Y0;
		const u16 Z = rdram_data[(shift + 2) ^ 1] + Z0;
		*pres++ = (X << 16) | Y;
		*pres++ = Z << 16;
	}
	return _vres;
}

static
const SWVertex * F5INDI_AdjustVtx(u32 _w0, u32 _w1, SWVertex * _vres)
{
	const u16 * v0_data;
	u16 X0;
	u16 Y0;
	u16 Z0;
	u32 count;
	u32 * pres;
	const u16 * dmem_data;
	u32 i;
	const u32 dmem_addr = _SHIFTR(_w0, 8, 12);
	if (*CAST_DMEM(const u64*, 0x128) == 0)
		return CAST_DMEM(const SWVertex*, dmem_addr);
	v0_data = CAST_DMEM(const u16*, 0x128);
	X0 = v0_data[0x0 ^ 0x1];
	Y0 = v0_data[0x1 ^ 0x1];
	Z0 = v0_data[0x2 ^ 0x1];
	count = _SHIFTR(_w1, 24, 7);
	pres = (u32*)(_vres);
	dmem_data = CAST_DMEM(const u16*, dmem_addr);
	for (i = 0; i < count; ++i) {
		const u16 X = dmem_data[0x0 ^ 0x1] + X0;
		const u16 Y = dmem_data[0x1 ^ 0x1] + Y0;
		const u16 Z = dmem_data[0x2 ^ 0x1] + Z0;
		*pres++ = (X << 16) | Y;
		*pres++ = Z << 16;
		dmem_data += 4;
	}
	return _vres;
}

static
void F5INDI_MoveMem(u32 _w0, u32 _w1)
{

	if (_SHIFTR(_w1, 0, 24) != 0)
		F5INDI_DMA_Direct(_w0, _w1);
	else
		F5INDI_DMA_Segmented(_w0, _w1);

	switch (_w0 & 0x00f00000) {
	case 0x00000000:
		switch (_w0) {
		case 0x0101300F:
			gli_gSPViewport(_w1);
			break;
		case 0x0105C03F:
			gli_gSPForceMatrix(_w1);
			break;
		case 0x010E403F:
			gli_RSP_LoadMatrix(indiData.mtx_vtx_gen, _SHIFTR(_w1, 0, 24));
			F5INDI_LoadSTMatrix();
			break;
		}
		break;

	case 0x00100000:
		{
		SWVertex vertices[128];
		const u32 n = _SHIFTR(_w1, 24, 7);
		gli_gSPSWVertexN(F5INDI_AdjustVtx(_w0, _w1, vertices), n);
		}
		break;

	case 0x00300000:
		/* Set num of lights */
		*(DMEM + (0x058B^3)) = (u8)(_w1 >> 24);
		break;

	case 0x00500000:
	{
		const u32 n = _SHIFTR(_w1, 24, 7);
		SWVertex vres[256];
		gli_gSPSWVertexN(F5INDI_LoadVtx(_w0, _w1, vres), n);
	}
	break;

	default:
		if (_SHIFTR(_w0, 8, 12) == 0x480) {
			u8 L;
			u32 C;
			/* Lighting */
			const u32 F = (_w0 >> 22);
			if ((F & 2) != 0)
				F5INDI_RebuildAndAdjustColors(_w0, _w1);
			L = *(DMEM + (0x58B ^ 3));
			C = _SHIFTR(_w1, 24, 8) | L;
			if ((C & 0x80) == 0) {
				if ((F & 1) != 0)
					F5INDI_Lighting_Overlay1(_w0, _w1);
				else
					F5INDI_Lighting_Overlay2(_w0, _w1);
			} else
				F5INDI_Lighting_Basic(_w0, _w1);
		}
	break;
	}
}

static
void F5INDI_SetDListAddr(u32 _w0, u32 _w1)
{
	const u32 count = _w0 & 0x1FF;
	const u32 * pSrc = CAST_RDRAM(const u32*, _SHIFTR(_w1, 0, 24));
	u32 * pDst = CAST_DMEM(u32*, 0xFD4);
	pDst[0] = _w1 + 8;
	pDst[1] = *pSrc;
	pDst[2] = count;
}

static
void F5INDI_DList(u32 _w0, u32 _w1)
{
	gli_gSPDisplayList(_w1);
	_updateF5DL();
}

static
void F5INDI_BranchDList(u32 _w0, u32 _w1)
{
	gli_gSPBranchList(_w1);
	_updateF5DL();
}

static
void F5INDI_CalcST(const u32* params, u32 * _st)
{
	u32 i;
	const s16 * subs = CAST_DMEM(const s16*, 0x0F0);
	const u16 * muls = CAST_DMEM(const u16*, 0x0EC);
	const s32 * mtx = indiData.mtx_st_adjust;

	const u32 num = (RSP.cmd == F5INDI_TRI2) ? 4 : 3;

	for (i = 0; i < num; ++i) {
		u64 X2;
		u64 X2_2;
		u64 Y2;
		u64 Y2_2;
		u64 Z2;
		u64 Z2_2;
		u64 R;
		u32 R1;
		u32 D;
		u32 V;
		u32 W;
		u32 S;
		u32 T;
		const u32 idx = _SHIFTR(params[i + 4], 0, 8);
		const s16 * coords = CAST_DMEM(const s16*, 0x170 + idx);
		s16 X = coords[0x0 ^ 0x1];
		s16 Y = coords[0x1 ^ 0x1];
		s16 Z = coords[0x2 ^ 0x1];
		s32 X1 = X * mtx[0 + 4 * 0] + Y * mtx[0 + 4 * 1] + Z * mtx[0 + 4 * 2] + mtx[0 + 4 * 3];
		s32 Y1 = X * mtx[1 + 4 * 0] + Y * mtx[1 + 4 * 1] + Z * mtx[1 + 4 * 2] + mtx[1 + 4 * 3];
		s32 Z1 = X * mtx[2 + 4 * 0] + Y * mtx[2 + 4 * 1] + Z * mtx[2 + 4 * 2] + mtx[2 + 4 * 3];
		X1 -= subs[0x0 ^ 0x1] << 16;
		Y1 -= subs[0x1 ^ 0x1] << 16;
		Z1 -= subs[0x2 ^ 0x1] << 16;
		X2 = (s32)(X1);
		X2_2 = (X2 * X2) >> 16;
		Y2 = (s32)(Y1);
		Y2_2 = (Y2 * Y2) >> 16;
		Z2 = (s32)(Z1);
		Z2_2 = (Z2 * Z2) >> 16;
		R = X2_2 + Y2_2 + Z2_2;
		R1 = (u32)(R);
		if (R > 0x0000FFFFFFFF)
			R1 = 0x7FFF0000 | (R & 0xFFFF);
		D = (u32)(sqrt(R1));
		D = 0xFFFFFFFF / (D * 0x300);
		V = (u32)((D * X2) >> 16);
		W = (u32)((D * Y2) >> 16);
		S = (V * muls[0x0 ^ 0x1]) >> 16;
		T = (W * muls[0x1 ^ 0x1]) >> 16;
		_st[i] = (S << 16) | T;
	}
}

static
void F5INDI_DoSubDList(void)
{
	const u32 dlistAddr = _SHIFTR(*CAST_DMEM(const u32*, 0x58C), 0, 24);
	if (dlistAddr == 0)
		return;


	RSP.PCi++;
	RSP.F5DL[RSP.PCi] = _SHIFTR(*CAST_RDRAM(const u32*, dlistAddr), 0, 24);
	RSP.PC[RSP.PCi] = dlistAddr + 8;

	while (true) {
		RSP.w0 = *CAST_RDRAM(u32*, RSP.PC[RSP.PCi]);
		RSP.w1 = *CAST_RDRAM(u32*, RSP.PC[RSP.PCi] + 4);

		RSP.cmd = _SHIFTR(RSP.w0, 24, 8);


		if (RSP.w0 == 0xB8000000 && RSP.w1 == 0xFFFFFFFF)
			break;

		RSP.nextCmd = _SHIFTR(*CAST_RDRAM(const u32*, RSP.PC[RSP.PCi] + 8), 24, 8);

		gli_GBIcmd[RSP.cmd](RSP.w0, RSP.w1);
		RSP.PC[RSP.PCi] += 8;

		if (RSP.nextCmd == 0xBD) {
			/* Naboo */
			u32* command = CAST_DMEM(u32*, 0xE58);
			command[0] = RSP.w0;
			command[1] = RSP.w1;
			break;
		}
	}

	RSP.PCi--;
	*CAST_DMEM(u32*, 0x58C) = 0U;
}

static
bool F5INDI_AddVertices(const u32 _vert[3], SPVertex * _vertices)
{
	u32 i;
	SPVertex * dma;
	if (GLI_IS_CLIPPED(_vertices, _vert[0], _vert[1], _vert[2])) {
		return false;
	}

	dma = gli_drawer_take_dma_vertices(3);

	for (i = 0; i < 3; ++i) {
		SPVertex * vtx = &_vertices[_vert[i]];

		if ((gSP.geometryMode & G_SHADE) == 0) {
			/* Prim shading */
			vtx->flat_r = gDP.primColor.r;
			vtx->flat_g = gDP.primColor.g;
			vtx->flat_b = gDP.primColor.b;
			vtx->flat_a = gDP.primColor.a;
		}

		if (gDP.otherMode.depthSource == G_ZS_PRIM)
			vtx->z = gDP.primDepth.z * vtx->w;

		dma[i] = *vtx;
	}

	return true;
}

void F5INDI_Tri(u32 _w0, u32 _w1)
{
	u32 w2;
	u32 S1;
	u32 S2;
	u32 S3;
	u32 S4;
	u32 C1;
	u32 C2;
	u32 C3;
	u32 C4;
	u32 colors[4];
	bool useTex;
	u32 ST[4];
	const u32 * texbase;
	bool persp;
	const u8 * colorbase;
	u32 count;
	SPVertex * vertices;
	u32 i;
	u32 vert1[3];
	bool triAdded;
	const bool bTri2 = RSP.cmd == F5INDI_TRI2;

	const u32* params = CAST_RDRAM(const u32*, RSP.PC[RSP.PCi]);
	const u32 w3 = params[3];
	const u32 v1 = (_SHIFTR(_w1, 16, 12) - 0x600) / 40;
	const u32 v2 = (_SHIFTR(_w1,  0, 12) - 0x600) / 40;
	const u32 v3 = (_SHIFTR( w3, 16, 12) - 0x600) / 40;
	const u32 v4 = (_SHIFTR( w3,  0, 12) - 0x600) / 40;
	u32 vert[4];
	vert[0] = v1;
	vert[1] = v2;
	vert[2] = v3;
	vert[3] = v4;

	w2 = params[2];
	S1 = _SHIFTR(w2, 16, 8);
	S2 = _SHIFTR(w2,  8, 8);
	S3 = _SHIFTR(w2,  0, 8);
	S4 = _SHIFTR(w2, 24, 8);

	C1 = *CAST_DMEM(const u32*, 0xD40 + S1);
	C2 = *CAST_DMEM(const u32*, 0xD40 + S2);
	C3 = *CAST_DMEM(const u32*, 0xD40 + S3);
	C4 = *CAST_DMEM(const u32*, 0xD40 + S4);
	colors[0] = C1;
	colors[1] = C2;
	colors[2] = C3;
	colors[3] = C4;

	useTex = (_w0 & 0x0200) != 0;

	if ((_w0 & 0x0800) != 0) {
		F5INDI_CalcST(params, ST);
		texbase = ST;
	} else {
		texbase = params + 4;
	}

	/* PrepareVertices */
	persp = gDP.otherMode.texturePersp != 0;
	colorbase = (const u8*)(colors);
	count = bTri2 ? 4 : 3;
	vertices = gli_drawer_vertices();
	for (i = 0; i < count; ++i) {
		SPVertex * vtx = &vertices[vert[i]];
		const u8 *color = colorbase;
		colorbase += 4;
		vtx->r = color[3] * 0.0039215689f;
		vtx->g = color[2] * 0.0039215689f;
		vtx->b = color[1] * 0.0039215689f;
		vtx->a = color[0] * 0.0039215689f;

		if (useTex) {
			const u32 st = texbase[i];
			s16 s = (s16)_SHIFTR(st, 16, 16);
			s16 t = (s16)_SHIFTR(st, 0, 16);
			if (persp) {
				vtx->s = _FIXED2FLOAT(s, 5);
				vtx->t = _FIXED2FLOAT(t, 5);
			} else {
				vtx->s = _FIXED2FLOAT(s, 4);
				vtx->t = _FIXED2FLOAT(t, 4);
			}
		}
	}

	vert1[0] = v1;
	vert1[1] = v2;
	vert1[2] = v3;
	triAdded = F5INDI_AddVertices(vert1, vertices);
	if (bTri2) {
		u32 vert2[3];
		vert2[0] = v1;
		vert2[1] = v3;
		vert2[2] = v4;
		triAdded |= F5INDI_AddVertices(vert2, vertices);
	}

	if (triAdded)
		F5INDI_DoSubDList();

	RSP.nextCmd = _SHIFTR(params[8], 24, 8);
	if (RSP.nextCmd != G_TRI1 && RSP.nextCmd != G_TRI2) {
		const u32 geometryMode = gSP.geometryMode;
		if (useTex) {
			if ((gSP.geometryMode & G_CULL_BOTH) == G_CULL_BOTH)
				gSP.geometryMode &= ~(G_CULL_FRONT);
		} else {
			/* culling not used for polygons without textures. */
			gSP.geometryMode &= ~(G_CULL_BOTH);
		}
		gli_drawer_draw_dma_pending();
		gSP.geometryMode = geometryMode;
	}

	if (useTex)
		RSP.PC[RSP.PCi] += 16;

	RSP.PC[RSP.PCi] += 8;
}

void F5INDI_GenVertices(u32 _w0, u32 _w1)
{
	const SWVertex * vertex;
	bool verticesToProcess[32];
	u32 A;
	u32 B;
	u32 count;
	f32 combined[4][4];
	memcpy(combined, gSP.matrix.combined, sizeof(combined));
	memcpy(gSP.matrix.combined, indiData.mtx_vtx_gen, sizeof(gSP.matrix.combined));
	/* This microcode brings its own combined matrix, so the stereo shear has to be applied here */
	gli_gSPApplyStereo(gSP.matrix.combined);

	vertex = CAST_DMEM(const SWVertex*, 0x170);

	A = (_w0 & 0x0000FFFF) | (_w1 & 0xFFFF0000);
	B = 1;
	count = 0;

	while (A != 0) {
		u32 C = A & B;
		verticesToProcess[count++] = C != 0;
		if (C != 0) {
			A ^= C;
		}
		B <<= 1;
	}

	gli_gSPSWVertexSelected(vertex, count, verticesToProcess);

	memcpy(gSP.matrix.combined, combined, sizeof(gSP.matrix.combined));
}

void F5INDI_GenParticlesVertices(void)
{
	const u32 * params;
	u32 L;
	u32 U;
	u32 corrector;
	u32 M;
	u32 vtxAddr;
	u32 lightAddr;
	u32 factorAddr;
	u32 colorAddr;
	u32 count;
	u32 Q;
	static u32 dpc_clock0 = 0;
	static int dpc_clock0_set = 0;
	if (!dpc_clock0_set) {
		dpc_clock0 = (u32)(time(NULL));
		dpc_clock0_set = 1;
	}
	params = CAST_RDRAM(const u32*, RSP.PC[RSP.PCi]);
	*CAST_DMEM(u64*, 0xC10) = *CAST_RDRAM(const u64*, _SHIFTR(params[3], 0, 24));
	L = *CAST_DMEM(const u32*, 0xC10);
	if (L == 0)
		return;
	U = L;
	memcpy(DMEM + 0x170, RDRAM + _SHIFTR(params[2], 0, 24), 256);
	corrector = _SHIFTR(params[0], 0, 16);
	M = _SHIFTR(params[5], 0, 16);
	vtxAddr = 0x170;
	lightAddr = 0xB00;
	factorAddr = 0x380;
	colorAddr = 0x480;
	count = 0;
	Q = 0;
	while (U != 0) {
		SWVertex * vertex = CAST_DMEM(SWVertex*, vtxAddr);
		u16* light = CAST_DMEM(u16*, lightAddr);
		u16 F = (((light[0x0 ^ 0x1] << 12) + light[0x1 ^ 0x1] * corrector) << 4) >> 16;
		bool endCycle = false;
		if ((M & 0x2000) == 0) {
			/* Step 2 */
			s16 F1;
			do {
				F = (((light[0x0 ^ 0x1] << 12) + light[0x1 ^ 0x1] * corrector) << 4) >> 16;
				F1 = F + 0xF000;
				if (F1 < 0) {
					light[0x0 ^ 0x1] = F;
					if ((M & 0x40) != 0)
						vertex->flag = (((F >> 4)&0xFC) << 8) | (vertex->flag & 0xFF);
					if ((M & 0x20) != 0)
						vertex->flag = ((F >> 4) & 0xFC) | (vertex->flag & 0xFF00);
				} else {
					light[0x0 ^ 0x1] = 0U;
					if ((M & 0x4000) != 0) {
						/*const u32 dpc_clock = *REG.DPC_CLOCK; */
						const u32 dpc_clock = (u32)(time(NULL)) - dpc_clock0;
						const u32* V = CAST_DMEM(const u32*, 0xC18);
						vertex->x = (dpc_clock        & _SHIFTR(V[0], 16, 16)) + _SHIFTR(V[2], 16, 16);
						vertex->y = ((dpc_clock >> 3) & _SHIFTR(V[0],  0, 16)) + _SHIFTR(V[2],  0, 16);
						vertex->z = ((dpc_clock >> 6) & _SHIFTR(V[1], 16, 16)) + _SHIFTR(V[3], 16, 16);
						vertex->flag &= _SHIFTR(V[1], 0, 16);
					} else {
						Q |= 1 << count;
						endCycle = true;
						break;
					}
				}
			} while (F1 >= 0);
		}
		if (!endCycle) {
			/* Step 3 */
			u32 X = (u32)_SHIFTL(vertex->x, 12, 16) + (u32)((s16)(_SHIFTR(params[4], 16, 16)) * corrector);
			u32 Y = (u32)_SHIFTL(vertex->y, 12, 16) + (u32)((s16)(_SHIFTR(params[4], 0, 16)) * corrector);
			u32 Z = (u32)_SHIFTL(vertex->z, 12, 16) + (u32)((s16)(_SHIFTR(params[5], 16, 16)) * corrector);
			if ((M & 0x0380) != 0) {
				const u32 * factor;
				u32 offset = factorAddr - 0x380;
				if ((M & 0x0200) == 0) {
					if ((M & 0x080) != 0) {
						offset = _SHIFTR(F, 4, 8) & 0xF8;
					} else {
						const u32 p = *CAST_DMEM(const u32*, lightAddr + 4);
						offset = _SHIFTR(p, 24, 8) << 3;
					}
				}
				factor = CAST_DMEM(const u32*, 0x380 + offset);
				X += (s16)(_SHIFTR(factor[0], 16, 16)) * corrector;
				Y += (s16)(_SHIFTR(factor[0], 0, 16)) * corrector;
				Z += (s16)(_SHIFTR(factor[1], 16, 16)) * corrector;
			}
			/* Step 4 */
			if ((M & 0x1C00) != 0) {
				const u32 * factor;
				u32 offset = colorAddr - 0x480;
				if ((M & 0x1000) == 0) {
					if ((M & 0x0400) != 0) {
						offset = _SHIFTR(F, 4, 8) & 0xF8;
					} else {
						const u32 p = *CAST_DMEM(const u32*, lightAddr + 4);
						offset = _SHIFTR(p, 16, 8) << 3;
					}
				}
				factor = CAST_DMEM(const u32*, 0x480 + offset);
				X += (s16)(_SHIFTR(factor[0], 16, 16)) * corrector;
				Y += (s16)(_SHIFTR(factor[0], 0, 16)) * corrector;
				Z += (s16)(_SHIFTR(factor[1], 16, 16)) * corrector;
			}
			/* Step 5 */
			X = (X << 4) >> 16;
			Y = (Y << 4) >> 16;
			Z = (Z << 4) >> 16;
			vertex->x = X;
			vertex->y = Y;
			vertex->z = Z;
			if ((M & 0x0008) != 0) {
				const u32* V = CAST_DMEM(const u32*, 0xC00);
				if (vertex->x < (s16)(_SHIFTR(V[0], 16, 16)))
					vertex->x += (s16)(_SHIFTR(V[2], 16, 16));
				if (vertex->x > (s16)(_SHIFTR(V[0], 16, 16)) + (s16)(_SHIFTR(V[2], 16, 16)))
					vertex->x -= (s16)(_SHIFTR(V[2], 16, 16));

				if (vertex->y < (s16)(_SHIFTR(V[0], 0, 16)))
					vertex->y += (s16)(_SHIFTR(V[2], 0, 16));
				if (vertex->y > (s16)(_SHIFTR(V[0], 0, 16)) + (s16)(_SHIFTR(V[2], 0, 16)))
					vertex->y -= (s16)(_SHIFTR(V[2], 0, 16));

				if (vertex->z < (s16)(_SHIFTR(V[1], 16, 16)))
					vertex->z += (s16)(_SHIFTR(V[3], 16, 16));
				if (vertex->z > (s16)(_SHIFTR(V[1], 16, 16)) + (s16)(_SHIFTR(V[3], 16, 16)))
					vertex->z -= (s16)(_SHIFTR(V[3], 16, 16));
			}
		}
		vtxAddr += 8;
		lightAddr += 8;
		factorAddr += 8;
		colorAddr += 8;
		U >>= 1;
		count++;
	}

	RSP_StereoWillWrite(_SHIFTR(params[2], 0, 24), 256);
	memcpy(RDRAM + _SHIFTR(params[2], 0, 24), DMEM + 0x170, 256);

	if ((M & 0x04) == 0) {
		RSP_StereoWillWrite(_SHIFTR(params[3], 0, 24), 4);
		*CAST_RDRAM(u32*, _SHIFTR(params[3], 0, 24)) = L & (~Q);
		RSP_StereoWillWrite(_SHIFTR(params[1], 8, 24), count * 8);
		memcpy(RDRAM + _SHIFTR(params[1], 8, 24), DMEM + 0xB00, count * 8);
	}
}

static
u32 F5INDI_TexrectGenVertexMask(u32 _mask, u32 _limAddr, u32 _vtxAddr)
{
	s16 ZL;
	s16 XR;
	s16 YR;
	s16 ZR;
	u32 S;
	const SWVertex * vertex;
	u32 i;
	const u32* V = CAST_DMEM(const u32*, _limAddr);
	const s16 XL = _SHIFTR(*V, 16, 16);
	const s16 YL = _SHIFTR(*V, 0, 16);
	++V;
	ZL = _SHIFTR(*V, 16, 16);
	++V;
	XR = _SHIFTR(*V, 16, 16);
	YR = _SHIFTR(*V, 0, 16);
	++V;
	ZR = _SHIFTR(*V, 16, 16);

	S = 0xFFFFFFFE;
	vertex = CAST_RDRAM(const SWVertex*, _vtxAddr);
	for (i = 0; i < 0x20; ++i) {
		if (vertex[i].x > XL && vertex[i].x < XR &&
				vertex[i].y > YL && vertex[i].y < YR &&
				vertex[i].z > ZL && vertex[i].z < ZR) {
			_mask &= S;
		}
		S = (S << 1) | 1;
	}
	return _mask;
}

static
void F5INDI_TexrectGen(void)
{
	const u32* params = CAST_RDRAM(const u32*, RSP.PC[RSP.PCi]);
	SPVertex * vertices = gli_drawer_vertices();
/*	const u32 perspNorm = *CAST_DMEM(const u32*, 0x14C); */
	const u32 vp = *CAST_DMEM(const u32*, 0x130);
	const u32 vpx = _SHIFTR(vp, 17, 15);
	const u32 vpy = _SHIFTR(vp,  1, 15);
	const u32 maxX = (_SHIFTR(params[0],  0, 16) * vpx) >> 16;
	const u32 maxY = (_SHIFTR(params[1], 16, 16) * vpy) >> 16;
	const u64 MX = (params[4] & 0xFFFF0000) | _SHIFTR(params[5], 16, 16);
	const u64 xScale = MX * _SHIFTR(params[7], 16, 16) * 1 * vpx;
	const u64 MY = (_SHIFTL(params[4], 16, 16)) | _SHIFTR(params[5], 0, 16);
	const u64 yScale = MY * _SHIFTR(params[7], 0, 16) * vpy;
	const u32 mp = *CAST_DMEM(const u32*, 0x5C8);
	const u64 Q = (params[6] & 0xFFFF0000) | _SHIFTR(mp, 16, 16);
	const u64 R = (_SHIFTL(params[6], 16, 16)) | _SHIFTR(mp, 0, 16);
	const u32 colorScale = params[3];

	s32 count = _SHIFTR(params[0], 16, 8);
	u32 vtxAddr = _SHIFTR(params[2], 0, 24);
	for (; count > 0; --count) {
		u32 K;
		u32 B;
		u32 limAddr;
		u32 i;
		const SWVertex * vertex = CAST_RDRAM(const SWVertex*, vtxAddr);
		gli_gSPSWVertexN(vertex, 0x20);
		K = params[8];
		B = params[9] & 0xFF;
		limAddr = 0xC28;
		while (B != 0) {
			K = F5INDI_TexrectGenVertexMask(K, limAddr, vtxAddr);
			B--;
			limAddr += 0x10;
		}
		for (i = 0; i < 0x20; ++i) {
			const SPVertex * v;
			f32 screenX;
			f32 screenY;
			u32 w_i;
			u32 inv_w;
			u32 val;
			u32 A;
			u32 C;
			u64 offset_x_i;
			f32 offset_x_f;
			f32 ulx;
			s32 G;
			f32 lrx;
			u64 offset_y_i;
			f32 offset_y_f;
			f32 uly;
			s32 H;
			f32 lry;
			u64 Q1;
			u16 dsdx_i;
			u64 R1;
			u16 dtdy_i;
			u64 U;
			u16 U1;
			s16 S;
			u64 V;
			u16 V1;
			s16 T;
			f32 dsdx;
			f32 dtdy;
			u32 primColor;
			const u32 D = K & 1;
			K >>= 1;
			if (D == 0)
				continue;

			v = &vertices[i];
			if (v->clip != 0)
				continue;
			screenX = v->x / v->w * gSP.viewport.vscale[0] + gSP.viewport.vtrans[0];
			screenY = -v->y / v->w * gSP.viewport.vscale[1] + gSP.viewport.vtrans[1];
			w_i = MAX(1U, (u32)(v->w));
			inv_w = 0xFFFFFFFF / w_i;

			val = *CAST_DMEM(const u32*, 0x480 + _SHIFTR(vertex[i].flag, 8, 8));
			A = _SHIFTR(val, 16, 16);
			C = _SHIFTR(val, 0, 16);

			offset_x_i = (xScale * inv_w) >> 32;
			offset_x_i = (offset_x_i * A) >> 32;
			if (offset_x_i > maxX)
				offset_x_i = maxX;
			if (offset_x_i < 4)
				offset_x_i = 4;
			offset_x_f = _FIXED2FLOAT((u32)(offset_x_i), 2);
			ulx = screenX - offset_x_f;
			G = screenX - offset_x_f > 0.0f ? 0 : (s32)(screenX - offset_x_f);
			lrx = F5_max0(screenX + offset_x_f);
			if (lrx - ulx <= 0.0f)
				continue;

			offset_y_i = (yScale * inv_w) >> 32;
			offset_y_i = (offset_y_i * C) >> 32;
			if (offset_y_i > maxY)
				offset_y_i = maxY;
			if (offset_y_i < 4)
				offset_y_i = 4;
			offset_y_f = _FIXED2FLOAT((u32)(offset_y_i), 2);
			uly = screenY - offset_y_f;
			H = screenY - offset_y_f > 0.0f ? 0 : (s32)(screenY - offset_y_f);
			lry = F5_max0(screenY + offset_y_f);
			if (lry - uly <= 0.0f)
				continue;

			Q1 = Q * 0x40 / offset_x_i;
			dsdx_i = (u16)(Q1 >> 16);
			R1 = R * 0x40 / offset_y_i;
			dtdy_i = (u16)(R1 >> 16);

			U = MIN(Q1 * 0x40U, (u64)(0x7FFFFFFFU));
			U1 = (u16)(((0-U) * G) >> 16);
			S = (U1 - 2) * 8;

			V = MIN(R1 * 0x40U, (u64)(0x7FFFFFFFU));
			V1 = (u16)(((0-V) * H) >> 16);
			T = (V1 - 2) * 8;

			dsdx = _FIXED2FLOAT((s16)dsdx_i, 10);
			dtdy = _FIXED2FLOAT((s16)dtdy_i, 10);

			gDP.primDepth.z = v->z / v->w * gSP.viewport.vscale[2] + gSP.viewport.vtrans[2];
			gDP.primDepth.deltaZ = 0.0f;

			primColor = *CAST_DMEM(const u32*, 0x380 + _SHIFTR(vertex[i].flag, 0, 8));
			gDP.primColor.r = _FIXED2FLOATCOLOR(((_SHIFTR(primColor, 24, 8) * _SHIFTR(colorScale, 24, 8)) >> 8), 8);
			gDP.primColor.g = _FIXED2FLOATCOLOR(((_SHIFTR(primColor, 16, 8) * _SHIFTR(colorScale, 16, 8)) >> 8), 8);
			gDP.primColor.b = _FIXED2FLOATCOLOR(((_SHIFTR(primColor,  8, 8) * _SHIFTR(colorScale,  8, 8)) >> 8), 8);
			gDP.primColor.a = _FIXED2FLOATCOLOR(((_SHIFTR(primColor,  0, 8) * _SHIFTR(colorScale,  0, 8)) >> 8), 8);

			/* A glow or a spark in the world, not a HUD element */
			gli_gSPImmersiveWorldSprite(true);
			gli_gDPTextureRectangle(ulx, uly, lrx, lry, 0, S, T, dsdx, dtdy, false);
			gli_gSPImmersiveWorldSprite(false);
		}
		vtxAddr += 0x100;
	}

}

static
void F5Naboo_TexrectGen(void)
{
	f32 screenX;
	f32 screenY;
	bool flip;
	u32 w_i;
	u32 viewport;
	u32 viewportX;
	u32 viewportY;
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
	const u32* params = CAST_RDRAM(const u32*, RSP.PC[RSP.PCi]);

	const u32 vtxIdx = ((params[0] >> 5) & 0x07F8) / 40;
	const SPVertex * v = &gli_drawer_vertices()[vtxIdx];
	if (v->clip != 0)
		return;

	screenX = v->x / v->w * gSP.viewport.vscale[0] + gSP.viewport.vtrans[0];
	screenY = -v->y / v->w * gSP.viewport.vscale[1] + gSP.viewport.vtrans[1];

	flip = (params[0] & 1) != 0;

	w_i = MAX(1U, (u32)(v->w));
	viewport = *CAST_DMEM(const u32*, 0x130);
	viewportX = _SHIFTR(viewport, 17, 15);
	viewportY = _SHIFTR(viewport, 1, 15);

	perspMatrixX = (params[5] & 0xFFFF0000) | _SHIFTR(params[6], 16, 16);
	perspMatrixY = _SHIFTL(params[5], 16, 16) | _SHIFTR(params[6], 0, 16);
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

	if ((params[0] & 2) != 0) {
		dsdx_i = -dsdx_i;
		E = _SHIFTR(params[4], 16, 16);
	}

	if ((params[0] & 4) != 0) {
		dtdy_i = -dtdy_i;
		F = _SHIFTR(params[4], 0, 16);
	}

	dsdx = _FIXED2FLOAT((s16)dsdx_i, 10);
	dtdy = _FIXED2FLOAT((s16)dtdy_i, 10);

	if (flip)
		SWAP_T(u16, dsdx_i, dtdy_i);


	if (ulx > 0) {
		S = E + 0xFFF0;
	} else {
		const int ulx_i = (int)(ulx * 4.0f);
		S = ((0 - (dsdx_i << 6) * ulx_i) << 3) + E + 0xFFF0;
	}

	if (uly > 0) {
		T = F + 0xFFF0;
	} else {
		const int uly_i = (int)(uly * 4.0f);
		T = ((0 - (dtdy_i << 6) * uly_i) << 3) + F + 0xFFF0;
	}

	F5INDI_DoSubDList();

	gDP.primDepth.z = v->z / v->w * gSP.viewport.vscale[2] + gSP.viewport.vtrans[2];
	gDP.primDepth.deltaZ = 0.0f;

	primColor = params[7];
	gDP.primColor.r = _FIXED2FLOATCOLOR(_SHIFTR(primColor, 24, 8), 8);
	gDP.primColor.g = _FIXED2FLOATCOLOR(_SHIFTR(primColor, 16, 8), 8);
	gDP.primColor.b = _FIXED2FLOATCOLOR(_SHIFTR(primColor,  8, 8), 8);
	gDP.primColor.a = _FIXED2FLOATCOLOR(_SHIFTR(primColor,  0, 8), 8);

	if ((gSP.geometryMode & G_FOG) != 0) {
		const u32 fogColor = (params[7] & 0xFFFFFF00) | (u32)(v->a*255.0f);
		gli_gDPSetFogColor(_SHIFTR(fogColor, 24, 8),	/* r */
		                _SHIFTR(fogColor, 16, 8),	/* g */
		                _SHIFTR(fogColor, 8, 8),	/* b */
		                _SHIFTR(fogColor, 0, 8));	/* a */
	}

	gli_gSPImmersiveWorldSprite(true);
	gli_gDPTextureRectangle(ulx, uly, lrx, lry, gSP.texture.tile, (s16)S, (s16)T, dsdx, dtdy, flip);
	gli_gSPImmersiveWorldSprite(false);
}

static
void F5Naboo_CopyDataFF(void)
{
	u32 * data;
	u32 V;
	const u32* params = CAST_RDRAM(const u32*, RSP.PC[RSP.PCi]);
	*CAST_DMEM(u32*, 0x100) = 0;
	data = CAST_DMEM(u32*, 0xDB8);
	data[0] = _SHIFTR(data[0], 0, 16);
	data[1] = _SHIFTL(params[0], 16, 16) | _SHIFTR(data[1], 0, 16);
	V = _SHIFTR(params[0], 16, 8);
	data[2] = _SHIFTL(V, 24, 8) | 0x00FFFF;
	data[3] = V * 2;
	data[4] = _SHIFTL(V, 24, 8);
	V = _SHIFTR(params[1], 24, 8);
	data[5] = V;
	/*data[6] */
	data[7] = (data[7] & 0xFFFF0000) | V * 2;
	/*data[8] */
	/*data[9] */
	V = _SHIFTR(params[2], 16, 16);
	data[10] = (data[10] & 0xFFFF0000) | V;
	/*data[11] */
	data[12] = (data[12] & 0xFFFF0000) | V;
	/*data[13] */
	V = _SHIFTR(params[2], 0, 16);
	data[14] = (data[14] & 0xFFFF0000) | V;
	/*data[15] */
	data[16] = (data[16] & 0xFFFF0000) | V;
	/*data[17] */
	V = _SHIFTR(params[3], 16, 16);
	data[18] = (data[18] & 0xFFFF0000) | V;
	/*data[19] */
	data[20] = (data[20] & 0xFFFF0000) | V;
	/*data[21] */
	V = _SHIFTR(params[3], 0, 16);
	data[22] = (data[22] & 0xFFFF0000) | V;
	/*data[23] */
	data[24] = (data[24] & 0xFFFF0000) | V;
	/*data[25] */
	/*data[26] */
	V = _SHIFTR(params[1], 8, 16);
	data[27] = (data[27] & 0xFFFF0000) | V;
	/*data[28] */
	data[29] = (data[29] & 0xFFFF0000) | V;
}

static const u32 data_D58[] = {
0x06000628
,0x06500678
,0x06A006C8
,0x06F00718
,0x07400768
,0x079007B8
,0x07E00808
,0x08300858
,0x088008A8
,0x08D008F8
,0x09200948
,0x09700998
,0x09C009E8
,0x0A100A38
};

static
void F5Naboo_CopyData00(void)
{
	u32 V;
	u32 * data;
	u32 * _D90;
	const u32* params = CAST_RDRAM(const u32*, RSP.PC[RSP.PCi]);

	/* Step 1 */
	*CAST_DMEM(u32*, 0x0F8) = params[3];
	V = _SHIFTR(params[0], 16, 8);
	*CAST_DMEM(u32*, 0x0FC) = (V << 24) | _SHIFTL(params[10], 16, 8);

	/* Step 2 */
	data = CAST_DMEM(u32*, 0xDD0);
	data[0] = (params[2] & 0xFFFF0000) | _SHIFTR(params[14], 16, 16);
	data[1] = _SHIFTL(params[2], 16, 16) | _SHIFTR(data[1], 0, 16);
	data[2] = (params[2] & 0xFFFF0000) | _SHIFTR(params[14], 0, 16);
	data[3] = _SHIFTL(params[2], 16, 16) | 0x0100;
	V = (params[4] & 0xFFFF0000);
	data[4] = V | _SHIFTR(data[4], 0, 16);
	/*data[5] */
	data[6] = V | _SHIFTR(data[6], 0, 16);
	/*data[7] */
	V = _SHIFTL(params[4], 16, 16);
	data[8] = V | _SHIFTR(data[8], 0, 16);
	/*data[9] */
	data[10] = V | _SHIFTR(data[10], 0, 16);
	/*data[11] */
	V = (params[5] & 0xFFFF0000);
	data[12] = V | _SHIFTR(data[12], 0, 16);
	/*data[13] */
	data[14] = V | _SHIFTR(data[14], 0, 16);
	/*data[15] */
	V = _SHIFTL(params[5], 16, 16);
	data[16] = V | _SHIFTR(data[16], 0, 16);
	/*data[17] */
	data[18] = V | _SHIFTR(data[18], 0, 16);

	/* Step 3 */
	V = (params[6] & 0xFFFF0000) | _SHIFTR(params[8], 16, 16);
	data[5] = V;
	data[7] = V;
	V = _SHIFTL(params[6], 16, 16) | _SHIFTR(params[8], 0, 16);
	data[9] = V;
	data[11] = V;
	V = (params[7] & 0xFFFF0000) | _SHIFTR(params[9], 16, 16);
	data[13] = V;
	data[15] = V;
	V = _SHIFTL(params[7], 16, 16) | _SHIFTR(params[9], 0, 16);
	data[17] = V;
	data[19] = V;

	/* Step 4 */
	V = _SHIFTL(params[0], 16, 16) | _SHIFTR(params[13], 16, 16);
	data[20] = V;
	data[21] = (params[15] & 0xFFFF0000) | _SHIFTR(data[21], 0, 16);
	data[22] = _SHIFTR(params[11], 0, 8);
	if (data[22] > 127)
		data[22] |= 0xFF00;
	V = (_SHIFTR(params[0], 0, 16) + 0x00FF) & 0xFF00;
	data[22] |= V << 16;
	data[23] = _SHIFTL(params[15], 16, 16) | _SHIFTR(data[23], 0, 16);
	data[24] = 0x7FFF7FFF;
	data[25] = params[12];
	data[26] = 0x7FFF7FFF;
	data[27] = params[12];
	data[28] = params[10];
	data[29] = params[10];
	data[30] = params[11];
	data[31] = params[11];

	/* Step 5 */
	memcpy(DMEM + 0xC68, RDRAM + _SHIFTR(params[1], 8, 24), 0xF0);

	/* Step 6 */
	*(DMEM + (0x37E ^ 3)) = 0xFF;


	memcpy(DMEM + 0xD58, data_D58, sizeof(data_D58));
	_D90 = CAST_DMEM(u32*, 0xD90);
	_D90[0] = 0x0A600000 | _SHIFTR(_D90[0], 0, 16);
}

static
void F5Naboo_doCommands(u32 addr)
{
	const u32 * commands = CAST_DMEM(const u32*, addr);
	u32 w0 = commands[0];
	u32 w1 = commands[1];
	u32 cmd = _SHIFTR(w0, 24, 8);
	gli_GBIcmd[cmd](w0, w1);
	w0 = commands[2];
	w1 = commands[3];
	cmd = _SHIFTR(w0, 24, 8);
	gli_GBIcmd[cmd](w0, w1);
}

static
void F5Naboo_PrepareAndDrawTriangle(const u32 _vert[3], SPVertex * _vertices)
{
	u16 A;
	u8 B;
	u32 C;
	u32 dlistAddr;
	u32 * pOtherMode;
	u32 E;
	u32 F;
	u8 G;
	u32 curH;
	if (!F5INDI_AddVertices(_vert, _vertices))
		return;

	A = _SHIFTR(*CAST_DMEM(const u32*, 0x100), 8, 16);
	B = *(DMEM + (0x103 ^ 3));
	C = A | B;
	CAST_DMEM(u16*, 0x100)[0x0^0x1] = C;


	dlistAddr = _SHIFTR(*CAST_DMEM(const u32*, 0x58C), 0, 24);
	if (dlistAddr == 0) {
		if ((C << 8) != B)
			F5Naboo_doCommands(0xE50 + B);
		gli_drawer_draw_dma_pending();
		return;
	}

	F5INDI_DoSubDList();

	curH = 0xEF000000 | (gDP.otherMode.h & 0x00FFFFFF);
	pOtherMode = CAST_DMEM(u32*, 0xE50);
	pOtherMode[0] = curH;
	pOtherMode[1] = gDP.otherMode.l;
	E = curH & 0xFFCFFFFF;

	*CAST_DMEM(u32*, 0xE70) = E;
	F = E | 0x00100000;
	*CAST_DMEM(u32*, 0xE60) = F;
	G = *(DMEM + (0x101 ^ 3));
	if (G != 0) {
		F5Naboo_doCommands(0xE50 + G);
	}
	gli_drawer_draw_dma_pending();
}

static
void F5Naboo_Step3(NabooData * data, SPVertex * vertices, u32 GG)
{
	u32 vtxIdx[3];
	u32 alphaSum;
	u32 i;
	const u16* idx16 = CAST_DMEM(const u16*, GG);
	const u32 Idx1 = idx16[0x09 ^ 1] + data->CC;
	const u32 Idx2 = idx16[0x0A ^ 1] + data->CC;
	const u32 Idx3 = idx16[0x0B ^ 1] + data->CC;
	u32 idxs[3];
	idxs[0] = Idx1;
	idxs[1] = Idx2;
	idxs[2] = Idx3;
	idx16 = CAST_DMEM(const u16*, 0xD58);
	alphaSum = 0;
	for (i = 0; i < 3; ++i)
	{
		SPVertex * vtx;
		u8 * color;
		const u32 vtxAddr = idx16[(idxs[i]/2) ^ 1];
		vtxIdx[i] = (vtxAddr - 0x600) / 40;
		vtx = &vertices[vtxIdx[i]];
		color = DMEM + 0x0B78 + idxs[i] * 2 + (data->HH & 0xFFF);
		vtx->r = _FIXED2FLOATCOLOR(color[0x0 ^ 0x3], 8);
		vtx->g = _FIXED2FLOATCOLOR(color[0x1 ^ 0x3], 8);
		vtx->b = _FIXED2FLOATCOLOR(color[0x2 ^ 0x3], 8);
		vtx->a = _FIXED2FLOATCOLOR(color[0x3 ^ 0x3], 8);
		alphaSum += color[0x3 ^ 0x3];
	}
	if (data->HH == 0) {
		*CAST_DMEM(u32*, 0x100) = 0;
		F5Naboo_PrepareAndDrawTriangle(vtxIdx, vertices);
	} else {
		if (alphaSum == 0) {
			*(DMEM + (0x103 ^ 3)) = 0x00;
			F5Naboo_PrepareAndDrawTriangle(vtxIdx, vertices);
		} else {
			if (data->HH > 0) {
				*(DMEM + (0x103 ^ 3)) = 0x10;
				F5Naboo_PrepareAndDrawTriangle(vtxIdx, vertices);
			} else {
				if (alphaSum != 0x02FD) { /* 0xFF * 3 */
					*(DMEM + (0x103 ^ 3)) = 0x00;
					F5Naboo_PrepareAndDrawTriangle(vtxIdx, vertices);
				} else {
					u32 i;
					u32 alphaSum2 = 0;
					u8 alphas[3];
					for (i = 0; i < 3; ++i) {
						u32 offset = 0xB00 + idxs[i] * 2 + (data->HH & 0xFFF);
						u8* color = DMEM + offset;
						alphas[i] = color[0x3 ^ 0x3];
						alphaSum2 += alphas[i];
					}
					if (alphaSum2 == 0) {
						SPVertex * vtx = &vertices[vtxIdx[0]];
						vtx->a = _FIXED2FLOATCOLOR(alphas[0], 8);
						*(DMEM + (0x103 ^ 3)) = 0x20;
						F5Naboo_PrepareAndDrawTriangle(vtxIdx, vertices);
					} else {
						u32 i;
						*(DMEM + (0x103 ^ 3)) = 0x10;
						for (i = 0; i < 3; ++i) {
							SPVertex * vtx = &vertices[vtxIdx[i]];
							vtx->a = _FIXED2FLOATCOLOR(alphas[i], 8);
						}
						F5Naboo_PrepareAndDrawTriangle(vtxIdx, vertices);
					}
				}
			}
		}
	}
}

static
void F5Naboo_DrawPolygons(void)
{
	const u32* params = CAST_RDRAM(const u32*, RSP.PC[RSP.PCi]);
	NabooData * data = &nabooData;
	SPVertex * vertices = gli_drawer_vertices();
	const u32 cmd = _SHIFTR(params[0], 24, 8);
	u32 step = 5;

	if (cmd == 0xBE) {
		u32 GG;
		const u16 * idx16;
		u32 Idx1;
		u32 Idx2;
		u32 Idx3;
		u32 Idx4;
		u32 VtxAddr1;
		u32 VtxAddr2;
		u32 VtxAddr3;
		u32 VtxAddr4;
		u32 v1;
		u32 v2;
		u32 v3;
		u32 v4;
		SPVertex * vtx1;
		SPVertex * vtx2;
		SPVertex * vtx3;
		SPVertex * vtx4;
		/* Step 1 */
		data->HH = _SHIFTR(params[0], 0, 16);

		/* Step 2 */
		GG = data->EE - data->DD;
		idx16 = CAST_DMEM(const u16*, GG);
		Idx1 = (idx16[0x09 ^ 1] + data->CC) >> 1;
		Idx2 = (idx16[0x0A ^ 1] + data->CC) >> 1;
		Idx3 = (idx16[0x0B ^ 1] + data->CC) >> 1;
		Idx4 = (idx16[0x0E ^ 1] + data->CC) >> 1;

		idx16 = CAST_DMEM(const u16*, 0xD58);
		VtxAddr1 = idx16[Idx1 ^ 1];
		VtxAddr2 = idx16[Idx2 ^ 1];
		VtxAddr3 = idx16[Idx3 ^ 1];
		VtxAddr4 = idx16[Idx4 ^ 1];

		v1 = (VtxAddr1 - 0x600) / 40;
		v2 = (VtxAddr2 - 0x600) / 40;
		v3 = (VtxAddr3 - 0x600) / 40;
		v4 = (VtxAddr4 - 0x600) / 40;

		vtx1 = &vertices[v1];
		vtx1->s = 0.0f;
		vtx1->t = _FIXED2FLOAT((s16)_SHIFTR(params[1], 0, 16), 5);
		vtx2 = &vertices[v2];
		vtx2->s = _FIXED2FLOAT((s16)_SHIFTR(params[1], 16, 16), 5);
		vtx2->t = 0.0f;
		vtx3 = &vertices[v3];
		vtx3->s = 0.0f;
		vtx3->t = 0.0f;
		vtx4 = &vertices[v4];
		vtx4->s = vtx2->s;
		vtx4->t = vtx1->t;

		step = 3;
	}


	while (true) {
		switch (step)
		{
		case 3:
			F5Naboo_Step3(data, vertices, data->EE - data->DD);
			F5Naboo_Step3(data, vertices, data->EE - data->DD + 0x08);
			step = 4;
			break;
		case 4:
			if (data->DD == 0x10) {
				step = 6;
			} else {
				data->CC += data->TT;
				if (((data->AA & 0xFF) | (data->BB & 0xFF)) == 0)
					return;
				step = 5;
			}
			break;
		case 5:
			data->DD = 0x10;
			if ((data->AA & 0x80) == 0) {
				data->AA <<= 1;
				step = 6;
			} else {
				if ((data->AA & 0x8000) == 0) {
					data->AA <<= 1;
					step = 3;
				} else {
					data->AA <<= 1;
					return;
				}
			}
			break;
		case 6:
			data->DD = 0;
			if ((data->BB & 0x80) == 0) {
				data->BB <<= 1;
				step = 4;
			} else {
				if ((data->BB & 0x8000) == 0) {
					data->BB <<= 1;
					step = 3;
				} else {
					data->BB <<= 1;
					return;
				}
			}
			break;
		}
	}
}

typedef union NabooColor {
	struct {
		u8 a, b, g, r;
	} c;
	u32 color;
} NabooColor;

typedef struct NabooVertexData {
	SWVertex vtx;
	u32 E;
	u16 K, L, M, N, I, J;
	NabooColor color1, color2;
} NabooVertexData;

static NabooVertexData vd1, vd2;

static
s32 multAndClip(s32 s, s32 t, s32 max)
{
	const u32 sign1 = s & 0x80000000;
	const u32 sign2 = t & 0x80000000;
	s32 res = 0;
	if ((sign1 ^ sign2) == 0) {
		/* both positive or both negative */
		res = (s32)(MIN((s64)(max), ((s64)(s) * (s64)(t)) >> 32));
	}
	return res;
}

typedef struct NabooGen {
	u32 C2, I;
	s32 K, L, M, N, O, P, Q, R;
	u32 _E20, _E24, _E28, _E34, _E40, _E48, _DD0, _DD8;
	u8 RR, HH;
	u16 X0, Y0, Z0;
	u32 dmemSrcAddr;
} NabooGen;

static
void F5Naboo_PrepareVertexDataStep2(const NabooGen * g, u32 _V, const u32* _p, NabooVertexData * _vd)
{
	u32 B;
	u32 D;
	s64 D64;
	_vd->vtx.x = _SHIFTL(_SHIFTR(_V, 24, 8), 8, 8);
	_vd->vtx.z = _SHIFTL(_SHIFTR(_V, 8, 8), 8, 8);
	_vd->vtx.flag = _SHIFTL(_SHIFTR(_V, 0, 8), 8, 8);
	B = (s8)(_SHIFTR(_V, 8, 8)) * 0x10000;
	D = B - _SHIFTR(_p[1], 16, 16);
	D64 = (s64)((s32)(D));
	_vd->E = g->C2 + (u32)((D64 * D64) >> 16);
}

static
void F5Naboo_PrepareVertexDataStep3(const NabooGen * g, NabooVertexData * _vd)
{
	s32 K1 = (g->K - _vd->E);
	s32 K2 = multAndClip(K1, g->O, 0x7FFF);
	s32 L1 = (g->L - _vd->E);
	s32 L2 = multAndClip(L1, g->P, 0x7FFF);
	s32 M1 = (g->M - _vd->E);
	const s32 maxM = _SHIFTR(g->_E34, 16, 16);
	s32 M2 = multAndClip(M1, g->Q, maxM);
	s32 N1 = (g->N - _vd->E);
	const u32 maxN = _SHIFTR(g->_E34, 0, 16);
	s32 N2 = multAndClip(N1, g->R, maxN);
	u64 J = ((u64)(_vd->E) * (u64)(g->I)) >> 16;
	J *= J;
	_vd->K = (u16)(K2);
	_vd->L = (u16)(L2);
	_vd->M = (u16)(M2);
	_vd->N = (u16)(N2);
	_vd->J = MIN((u16)(0x7FFF), (u16)(J >> 47));
}

static
void F5Naboo_PrepareVertexDataStep4_7(const NabooGen * g, u32 _addr, NabooVertexData * _vd)
{
	u32 U2;
	u8 u1;
	u8 u2;
	u32 u3;
	u32 u4;
	u32 L1;
	u32 L2;
	u16 u5;
	u16 u6;
	u16 u7;
	u8 A1;
	u16 factor1;
	u16 factor2;
	u8 Va;
	u8 Vb;
	u8 Vc;
	u32 factor3;
	const u32 factor = 0xFFFE - _vd->M - _vd->N;
	u32 U1 = *CAST_DMEM(const u32*, _addr);
	_vd->color1.c.r = (u8)((factor * _SHIFTR(U1, 24, 8) + _vd->M * _SHIFTR(g->_E40, 24, 8) + _vd->N * _SHIFTR(g->_E48, 24, 8)) >> 16);
	_vd->color1.c.g = (u8)((factor * _SHIFTR(U1, 16, 8) + _vd->M * _SHIFTR(g->_E40, 16, 8) + _vd->N * _SHIFTR(g->_E48, 16, 8)) >> 16);
	_vd->color1.c.b = (u8)((factor * _SHIFTR(U1,  8, 8) + _vd->M * _SHIFTR(g->_E40,  8, 8) + _vd->N * _SHIFTR(g->_E48, 8, 8)) >> 16);

	/* Step 5 */
	U2 = *CAST_DMEM(const u32*, _addr + 0x78);
	u1 = ((U1 & 0xFF) + g->RR) & 0xFF;
	u2 = ((U2 & 0xFF) + g->HH) & 0xFF;
	u3 = (u32)((s8)(*(DMEM + ((0x380 + u1) ^ 3))));
	u4 = (u32)((s8)(*(DMEM + ((0x380 + u2) ^ 3))));
	L1 = _SHIFTL(_SHIFTR(g->_E28, 0, 16), 16, 16) | _SHIFTR(g->_E20, 0, 16);
	L2 = _SHIFTL(_SHIFTR(g->_DD8, 0, 16), 16, 16) | _SHIFTR(g->_DD0, 0, 16);
	u5 = (u3 * L1 + u4 * L2) >> 16;
	u6 = (u5 + _SHIFTR(g->_E20, 16, 16)) & 0xFFFF;
	u7 = ((0x7FFF - _vd->K) * u6 + _vd->K * _SHIFTR(g->_E28, 16, 16)) >> 16;
	_vd->vtx.y = ((0x7FFF - _vd->J) * u7 + _vd->J * _SHIFTR(g->_E24, 0, 16)) >> 16;

	/* Step 6 */
	A1 = *(DMEM + ((0x4FF + u4)^3));
	factor1 = 0xFF - A1;
	factor2 = 0xFF * A1;
	Va = (factor1 * _SHIFTR(U2, 24, 8) + factor2) >> 8;
	Vb = (factor1 * _SHIFTR(U2, 16, 8) + factor2) >> 8;
	Vc = (factor1 * _SHIFTR(U2,  8, 8) + factor2) >> 8;

	_vd->color2.c.r = (u8)((factor * Va + _vd->M * _SHIFTR(g->_E40, 24, 8) + _vd->N * _SHIFTR(g->_E48, 24, 8)) >> 16);
	_vd->color2.c.g = (u8)((factor * Vb + _vd->M * _SHIFTR(g->_E40, 16, 8) + _vd->N * _SHIFTR(g->_E48, 16, 8)) >> 16);
	_vd->color2.c.b = (u8)((factor * Vc + _vd->M * _SHIFTR(g->_E40,  8, 8) + _vd->N * _SHIFTR(g->_E48,  8, 8)) >> 16);

	/* Step 7 */
	factor3 = 0x7FFF - _vd->K;
	_vd->color2.c.r = (((factor3 * _vd->color2.c.r + _vd->K * _vd->color1.c.r) << 1) >> 16) + 1;
	_vd->color2.c.g = (((factor3 * _vd->color2.c.g + _vd->K * _vd->color1.c.g) << 1) >> 16) + 1;
	_vd->color2.c.b = (((factor3 * _vd->color2.c.b + _vd->K * _vd->color1.c.b) << 1) >> 16) + 1;

	_vd->color1.c.a = (u8)((_vd->K * _vd->K) >> 22);
	_vd->color2.c.a = (u8)(((((0x7FFF - _vd->L) * A1 + _vd->L * 0xFF) << 1) >> 16) + 1);

	*CAST_DMEM(u32*, _addr + 0x0F0) = _vd->color1.color;
	*CAST_DMEM(u32*, _addr + 0x168) = _vd->color2.color;

}

static
void F5Naboo_sumBytes(u32 src1, u32 src2, u8* pDst)
{
	u32 i;
	const u8* pSrc1 = DMEM + src1;
	const u8* pSrc2 = DMEM + src2;
	for (i = 0; i < 4; ++i)
		pDst[i] = (pSrc1[i] + pSrc2[i]) >> 1;
	}

static
void F5Naboo_PrepareVertexDataStep4(const NabooGen * g, u32 _addr, u32 _V, u32 _addrShift, NabooVertexData * _vd)
{
	u32 TA;
	u64 Va;
	u64 Vb;
	u64 Vc;
	u32 Vy1;
	u32 Vy2;
	u32 Vy;
	u16 Y1;
	u16 Y2;
	u32 factor3;
		const u32 addr3 = _SHIFTR(_V, 16, 16) + _addrShift + (_addrShift & 4);
	const u32 addr4 = _SHIFTR(_V, 0, 16) + _addrShift - (_addrShift & 4);
	const u32 factor1 = 0x7FFF - _vd->L;
	const u32 factor2 = 0xFFFE - _vd->M - _vd->N;
	u32 U = *CAST_DMEM(const u32*, _addr);
	u32 T = 0;
	F5Naboo_sumBytes(addr3, addr4, (u8*)(&T));
	Va = factor1 * _SHIFTR(U, 24, 8) + _vd->L * _SHIFTR(T, 24, 8);
	Va *= factor2;
	Va >>= 16;
	Vb = factor1 * _SHIFTR(U, 16, 8) + _vd->L * _SHIFTR(T, 16, 8);
	Vb *= factor2;
	Vb >>= 16;
	Vc = factor1 * _SHIFTR(U, 8, 8) + _vd->L * _SHIFTR(T, 8, 8);
	Vc *= factor2;
	Vc >>= 16;
	TA = ((*CAST_DMEM(const u32*, addr3) & 0xFF) << 7) +
		((*CAST_DMEM(const u32*, addr4) & 0xFF) << 7);
	Vy1 = (factor1 * (_SHIFTR(U, 0, 8) << 8)) >> 16;
	Vy2 = (_vd->L * TA) >> 16;
	Vy = Vy1 + Vy2;
	_vd->color1.c.r = (u8)(((Va << 1) + _vd->M * _SHIFTR(g->_E40, 24, 8) + _vd->N * _SHIFTR(g->_E48, 24, 8)) >> 16);
	_vd->color1.c.g = (u8)(((Vb << 1) + _vd->M * _SHIFTR(g->_E40, 16, 8) + _vd->N * _SHIFTR(g->_E48, 16, 8)) >> 16);
	_vd->color1.c.b = (u8)(((Vc << 1) + _vd->M * _SHIFTR(g->_E40, 8, 8) + _vd->N * _SHIFTR(g->_E48, 8, 8)) >> 16);
	Y1 = (u16)((Vy * (0x7FFF - _vd->J) + _vd->J * _SHIFTR(g->_E24, 0, 16)) >> 16);

	U = *CAST_DMEM(const u32*, _addr + 0x78);
	F5Naboo_sumBytes(addr3 + 0x78, addr4 + 0x78, (u8*)(&T));
	Va = factor1 * _SHIFTR(U, 24, 8) + _vd->L * _SHIFTR(T, 24, 8);
	Va *= factor2;
	Va >>= 16;
	Vb = factor1 * _SHIFTR(U, 16, 8) + _vd->L * _SHIFTR(T, 16, 8);
	Vb *= factor2;
	Vb >>= 16;
	Vc = factor1 * _SHIFTR(U, 8, 8) + _vd->L * _SHIFTR(T, 8, 8);
	Vc *= factor2;
	Vc >>= 16;
	TA = ((*CAST_DMEM(const u32*, addr3 + 0x78) & 0xFF) << 7) +
		((*CAST_DMEM(const u32*, addr4 + 0x78) & 0xFF) << 7);
	Vy1 = (factor1 * (_SHIFTR(U, 0, 8) << 8)) >> 16;
	Vy2 = (_vd->L * TA) >> 16;
	Vy = Vy1 + Vy2;
	_vd->color2.c.r = (u8)(((Va << 1) + _vd->M * _SHIFTR(g->_E40, 24, 8) + _vd->N * _SHIFTR(g->_E48, 24, 8)) >> 16);
	_vd->color2.c.g = (u8)(((Vb << 1) + _vd->M * _SHIFTR(g->_E40, 16, 8) + _vd->N * _SHIFTR(g->_E48, 16, 8)) >> 16);
	_vd->color2.c.b = (u8)(((Vc << 1) + _vd->M * _SHIFTR(g->_E40, 8, 8) + _vd->N * _SHIFTR(g->_E48, 8, 8)) >> 16);
	Y2 = (u16)((Vy * (0x7FFF - _vd->J) + _vd->J * _SHIFTR(g->_E24, 0, 16)) >> 16);

	/* Step 5 */
	factor3 = 0x7FFF - _vd->K;
	_vd->color2.c.r = (((factor3 * _vd->color2.c.r + _vd->K * _vd->color1.c.r) << 1) >> 16) + 1;
	_vd->color2.c.g = (((factor3 * _vd->color2.c.g + _vd->K * _vd->color1.c.g) << 1) >> 16) + 1;
	_vd->color2.c.b = (((factor3 * _vd->color2.c.b + _vd->K * _vd->color1.c.b) << 1) >> 16) + 1;
	_vd->vtx.y = ((factor3 * Y2 + _vd->K * Y1) << 1) >> 16;

	/* Step 7 */
	_vd->color1.c.a = _vd->color2.c.a = (u8)((_vd->K * _vd->K) >> 22);
	*CAST_DMEM(u32*, _addr + 0x0F0) = _vd->color1.color;
	*CAST_DMEM(u32*, _addr + 0x168) = _vd->color2.color;
}

static
void F5Naboo_processVertices(const NabooGen * g, u32 _param, u32 _dmemSrcAddr, u32 _dstOffset)
{
	u32 offset;
	u32 vtxSrcAddr;
	u32 vtxDstAddr;
	const SWVertex * pVtxSrc;
	size_t i;
	SWVertex vtxData[32];
	const u32 numVtx = _param & 0x1F;
	if (numVtx == 0)
		return;
	offset = _param >> 5;
	vtxSrcAddr = _dmemSrcAddr + offset;
	vtxDstAddr = _SHIFTR(*CAST_DMEM(const u32*, g->dmemSrcAddr + _dstOffset), 16, 16) + offset * 5;
	if (!(g->X0 | g->Y0 | g->Z0) != 0) {
		gli_gSPSWVertexAt(CAST_DMEM(const SWVertex*, vtxSrcAddr), (vtxDstAddr - 0x600) / 40, numVtx);
		return;
	}
	pVtxSrc = CAST_DMEM(const SWVertex*, vtxSrcAddr);
	for (i = 0; i < numVtx; ++i) {
		vtxData[i] = pVtxSrc[i];
		vtxData[i].x += g->X0;
		vtxData[i].y += g->Y0;
		vtxData[i].x += g->Z0;
	}
	gli_gSPSWVertexAt(vtxData, (vtxDstAddr - 0x600) / 40, numVtx);
}

static
void F5Naboo_GenVertices0C(void)
{
	const u32 * params;
	u32 dmemSrcAddr;
	u32 dmemDstAddr;
	u32 rdramAddr;
	u32 dmemSrcAddr2;
	u32 dmemDstAddr2;
	u32 rdramAddrShift;
	u32 flag;
	u32 A;
	u32 C32;
	s64 C64;
	u32 C2;
	u32 F;
	u32 G;
	u32 _E20;
	u32 _E24;
	u32 _E28;
	u32 _E2C;
	u32 I;
	u32 _DE0;
	u32 _DE4;
	u32 _DF0;
	u32 _DF4;
	s32 K;
	s32 L;
	s32 M;
	s32 N;
	u32 _E00;
	u32 _E04;
	u32 _E10;
	u32 _E14;
	s32 O;
	s32 P;
	s32 Q;
	s32 R;
	u32 _E34;
	u32 startAddr;
	u32 endAddr;
	u32 _E40;
	u32 _E48;
	u32 _DD0;
	u32 _DD4;
	u32 _DD6;
	u32 _DD8;
	u8 RR;
	u8 HH;
	u32 addrShift;
	const u16 * v0_data;
	u16 X0;
	u16 Y0;
	u16 Z0;
	NabooData * data;
	u8 * params8;
	u16 * params16;
	NabooGen g;
	const u32 rdramAddr0 = _SHIFTR(*CAST_DMEM(const u32*, 0x0F8), 0, 24);
	memcpy(DMEM + 0x380, RDRAM + rdramAddr0, 512);

	params = CAST_RDRAM(const u32*, RSP.PC[RSP.PCi]);

	/* Step 1 */
	dmemSrcAddr = (params[0] & 0xFFF);
	dmemDstAddr = _SHIFTR(*CAST_DMEM(const u32*, dmemSrcAddr), 16, 16);
	rdramAddr = _SHIFTR(params[4], 0, 24);
	memcpy(DMEM + dmemDstAddr, RDRAM + rdramAddr, 40);

	dmemSrcAddr2 = dmemSrcAddr + 8;
	dmemDstAddr2 = _SHIFTR(*CAST_DMEM(const u32*, dmemSrcAddr2), 16, 16);
	rdramAddrShift = _SHIFTR(*CAST_DMEM(const u32*, 0xDBC), 16, 16);
	memcpy(DMEM + dmemDstAddr2, RDRAM + rdramAddr + rdramAddrShift, 40);

	flag = _SHIFTR(params[0], 12, 4);
	if (flag > 7) {
		u32 rdramAddr2 = _SHIFTR(params[5], 0, 24);
		memcpy(DMEM + dmemDstAddr + 0x78, RDRAM + rdramAddr2, 40);
		memcpy(DMEM + dmemDstAddr2 + 0x78, RDRAM + rdramAddr2 + rdramAddrShift, 40);
	}

	/* Step 2 */
	A = (s8)(_SHIFTR(params[1], 24, 8)) * 0x10000;
	C32 = A - _SHIFTR(*CAST_DMEM(const u32*, 0xDD0), 16, 16);
	C64 = (s64)((s32)(C32));
	C2 = (u32)((C64 * C64) >> 16);
	F = _SHIFTL(*CAST_DMEM(const u32*, 0xDCC), 16, 16);
	G = C32 * _SHIFTR(*CAST_DMEM(const u32*, 0xDC4), 0, 16) + F;


	g.C2 = C2;
	F5Naboo_PrepareVertexDataStep2(&g, params[1] + _SHIFTR(*CAST_DMEM(const u32*, 0xDC0), 16, 16), CAST_DMEM(const u32*, 0xDD0), &vd1);
	F5Naboo_PrepareVertexDataStep2(&g, params[1], CAST_DMEM(const u32*, 0xDD8), &vd2);

	_E20 = *CAST_DMEM(const u32*, 0xE20);
	_E24 = *CAST_DMEM(const u32*, 0xE24);
	_E28 = *CAST_DMEM(const u32*, 0xE28);
	_E2C = *CAST_DMEM(const u32*, 0xE2C);
	I = (_E2C & 0xFFFF0000) | _SHIFTR(_E24, 16, 16);

	_DE0 = *CAST_DMEM(const u32*, 0xDE0);
	_DE4 = *CAST_DMEM(const u32*, 0xDE4);
	_DF0 = *CAST_DMEM(const u32*, 0xDF0);
	_DF4 = *CAST_DMEM(const u32*, 0xDF4);
	K = ((s32)((_DF0 & 0xFFFF0000) | _SHIFTR(_DE0, 16, 16)));
	L = ((s32)(_SHIFTL(_SHIFTR(_DF0, 0, 16), 16, 16) | _SHIFTR(_DE0, 0, 16)));
	M = ((s32)((_DF4 & 0xFFFF0000) | _SHIFTR(_DE4, 16, 16)));
	N = ((s32)(_SHIFTL(_SHIFTR(_DF4, 0, 16), 16, 16) | _SHIFTR(_DE4, 0, 16)));

	_E00 = *CAST_DMEM(const u32*, 0xE00);
	_E04 = *CAST_DMEM(const u32*, 0xE04);
	_E10 = *CAST_DMEM(const u32*, 0xE10);
	_E14 = *CAST_DMEM(const u32*, 0xE14);
	O = ((s32)((_E10 & 0xFFFF0000) | _SHIFTR(_E00, 16, 16)));
	P = ((s32)(_SHIFTL(_SHIFTR(_E10, 0, 16), 16, 16) | _SHIFTR(_E00, 0, 16)));
	Q = ((s32)((_E14 & 0xFFFF0000) | _SHIFTR(_E04, 16, 16)));
	R = ((s32)(_SHIFTL(_SHIFTR(_E14, 0, 16), 16, 16) | _SHIFTR(_E04, 0, 16)));

	_E34 = *CAST_DMEM(const u32*, 0xE34);
	startAddr = _SHIFTR(params[4], 24, 8);
	endAddr = _SHIFTR(params[5], 24, 8);

	/* Step 3 */

	_E40 = *CAST_DMEM(const u32*, 0xE40);
	_E48 = *CAST_DMEM(const u32*, 0xE48);
	_DD0 = *CAST_DMEM(const u32*, 0xDD0);
	_DD4 = *CAST_DMEM(const u32*, 0xDD4);
	_DD6 = _SHIFTL(_SHIFTR(_DD4, 0, 16), 16, 16);
	_DD8 = *CAST_DMEM(const u32*, 0xDD8);
	RR = *(DMEM + (0x0FC ^ 3));
	HH = *(DMEM + (0x0FD ^ 3));


	g.I = I;
	g.K = K;
	g.L = L;
	g.M = M;
	g.N = N;
	g.O = O;
	g.P = P;
	g.Q = Q;
	g.R = R;
	g._E34 = _E34;
	g._E20 = _E20;
	g._E24 = _E24;
	g._E28 = _E28;
	g._E40 = _E40;
	g._E48 = _E48;
	g._DD0 = _DD0;
	g._DD8 = _DD8;
	g.RR = RR;
	g.HH = HH;

	for (addrShift = startAddr; addrShift <= endAddr; addrShift += 4) {
		u32 addr1;
		u32 addr2;
		const u32 * V;

		F5Naboo_PrepareVertexDataStep3(&g, &vd1);
		F5Naboo_PrepareVertexDataStep3(&g, &vd2);

		/* Step 4 */
		addr1 = _SHIFTR(*CAST_DMEM(const u32*, dmemSrcAddr), 16, 16) + addrShift;
		addr2 = _SHIFTR(*CAST_DMEM(const u32*, dmemSrcAddr + 8), 16, 16) + addrShift;
		F5Naboo_PrepareVertexDataStep4_7(&g, addr1, &vd1);
		F5Naboo_PrepareVertexDataStep4_7(&g, addr2, &vd2);

		/* Step 8 */
		*CAST_DMEM(SWVertex*, 0x170 + addrShift * 2) = vd1.vtx;
		*CAST_DMEM(SWVertex*, 0x1C0 + addrShift * 2) = vd2.vtx;

		/* Step 9 */
		V = CAST_DMEM(const u32*, 0xDC0);
		vd1.vtx.x += _SHIFTR(V[0], 16, 16);
		vd1.vtx.flag += _SHIFTR(V[1], 0, 16);
		vd2.vtx.x += _SHIFTR(V[0], 16, 16);
		vd2.vtx.flag += _SHIFTR(V[3], 0, 16);

		vd1.E += G;
		vd2.E += G;

		G += _DD6;
	}

	/* Step 10 */
	v0_data = CAST_DMEM(const u16*, 0x128);
	X0 = v0_data[0x0 ^ 0x1];
	Y0 = v0_data[0x1 ^ 0x1];
	Z0 = v0_data[0x2 ^ 0x1];

	g.X0 = X0;
	g.Y0 = Y0;
	g.Z0 = Z0;
	g.dmemSrcAddr = dmemSrcAddr;
	F5Naboo_processVertices(&g, _SHIFTR(params[2], 16, 16), 0x170, 0x10);
	F5Naboo_processVertices(&g, _SHIFTR(params[2], 0, 16), 0x1C0, 0x18);

	/* Step 11 */
	data = &nabooData;
	params8 = RDRAM + RSP.PC[RSP.PCi];
	params16 = (u16*)(params8);
	data->AA = params16[0x6 ^ 0x1];
	data->BB = params16[0x7 ^ 0x1];
	data->CC = params8[0x1 ^ 0x3];
	data->TT = params8[0x5 ^ 0x3];
	data->EE = dmemSrcAddr;
	data->HH = _SHIFTR(*CAST_DMEM(const u32*, 0xDB8), 16, 16);
	data->DD = 0;
	F5Naboo_DrawPolygons();
}

static
void F5Naboo_GenVertices09(void)
{
	u32 dmemSrcAddr2;
	u32 dmemDstAddr2;
	u32 rdramAddrShift;
	u32 flag;
	u32 A;
	u32 C32;
	s64 C64;
	u32 C2;
	u32 F;
	u32 G;
	u32 _E24;
	u32 _E2C;
	u32 I;
	u32 _DE0;
	u32 _DE4;
	u32 _DF0;
	u32 _DF4;
	s32 K;
	s32 L;
	s32 M;
	s32 N;
	u32 _E00;
	u32 _E04;
	u32 _E10;
	u32 _E14;
	s32 O;
	s32 P;
	s32 Q;
	s32 R;
	u32 _E34;
	u32 startAddr;
	u32 endAddr;
	u32 _E40;
	u32 _E48;
	u32 addrShift;
	const u16 * v0_data;
	u16 X0;
	u16 Y0;
	u16 Z0;
	NabooData * data;
	u8 * params8;
	u16 * params16;
	NabooGen g;
	const u32* params = CAST_RDRAM(const u32*, RSP.PC[RSP.PCi]);

	/* Step 1 */
	u32 dmemSrcAddr = (params[0] & 0xFFF);
	u32 dmemDstAddr = _SHIFTR(*CAST_DMEM(const u32*, dmemSrcAddr), 16, 16);
	u32 rdramAddr = _SHIFTR(params[4], 0, 24);
	memcpy(DMEM + dmemDstAddr, RDRAM + rdramAddr, 40);

	dmemSrcAddr2 = dmemSrcAddr + 8;
	dmemDstAddr2 = _SHIFTR(*CAST_DMEM(const u32*, dmemSrcAddr2), 16, 16);
	rdramAddrShift = _SHIFTR(*CAST_DMEM(const u32*, 0xDBC), 16, 16);
	memcpy(DMEM + dmemDstAddr2, RDRAM + rdramAddr + rdramAddrShift, 40);

	flag = _SHIFTR(params[0], 12, 4);
	if (flag > 7) {
		u32 rdramAddr2 = _SHIFTR(params[5], 0, 24);
		memcpy(DMEM + dmemDstAddr + 0x78, RDRAM + rdramAddr2, 40);
		memcpy(DMEM + dmemDstAddr2 + 0x78, RDRAM + rdramAddr2 + rdramAddrShift, 40);
	}

	/* Step 2 */
	A = (s8)(_SHIFTR(params[1], 24, 8)) * 0x10000;
	C32 = A - _SHIFTR(*CAST_DMEM(const u32*, 0xDD0), 16, 16);
	C64 = (s64)((s32)(C32));
	C2 = (u32)((C64 * C64) >> 16);
	F = _SHIFTL(*CAST_DMEM(const u32*, 0xDCC), 16, 16);
	G = C32 * _SHIFTR(*CAST_DMEM(const u32*, 0xDC4), 0, 16) + F;


	g.C2 = C2;
	F5Naboo_PrepareVertexDataStep2(&g, params[1] + _SHIFTR(*CAST_DMEM(const u32*, 0xDC0), 16, 16), CAST_DMEM(const u32*, 0xDD0), &vd1);
	F5Naboo_PrepareVertexDataStep2(&g, params[1], CAST_DMEM(const u32*, 0xDD8), &vd2);

	_E24 = *CAST_DMEM(const u32*, 0xE24);
	_E2C = *CAST_DMEM(const u32*, 0xE2C);
	I = (_E2C & 0xFFFF0000) | _SHIFTR(_E24, 16, 16);

	_DE0 = *CAST_DMEM(const u32*, 0xDE0);
	_DE4 = *CAST_DMEM(const u32*, 0xDE4);
	_DF0 = *CAST_DMEM(const u32*, 0xDF0);
	_DF4 = *CAST_DMEM(const u32*, 0xDF4);
	K = ((s32)((_DF0 & 0xFFFF0000) | _SHIFTR(_DE0, 16, 16)));
	L = ((s32)(_SHIFTL(_SHIFTR(_DF0, 0, 16), 16, 16) | _SHIFTR(_DE0, 0, 16)));
	M = ((s32)((_DF4 & 0xFFFF0000) | _SHIFTR(_DE4, 16, 16)));
	N = ((s32)(_SHIFTL(_SHIFTR(_DF4, 0, 16), 16, 16) | _SHIFTR(_DE4, 0, 16)));

	_E00 = *CAST_DMEM(const u32*, 0xE00);
	_E04 = *CAST_DMEM(const u32*, 0xE04);
	_E10 = *CAST_DMEM(const u32*, 0xE10);
	_E14 = *CAST_DMEM(const u32*, 0xE14);
	O = ((s32)((_E10 & 0xFFFF0000) | _SHIFTR(_E00, 16, 16)));
	P = ((s32)(_SHIFTL(_SHIFTR(_E10, 0, 16), 16, 16) | _SHIFTR(_E00, 0, 16)));
	Q = ((s32)((_E14 & 0xFFFF0000) | _SHIFTR(_E04, 16, 16)));
	R = ((s32)(_SHIFTL(_SHIFTR(_E14, 0, 16), 16, 16) | _SHIFTR(_E04, 0, 16)));

	_E34 = *CAST_DMEM(const u32*, 0xE34);
	startAddr = _SHIFTR(params[4], 24, 8);
	endAddr = _SHIFTR(params[5], 24, 8);

	/* Step 3 */

	_E40 = *CAST_DMEM(const u32*, 0xE40);
	_E48 = *CAST_DMEM(const u32*, 0xE48);


	g.I = I;
	g.K = K;
	g.L = L;
	g.M = M;
	g.N = N;
	g.O = O;
	g.P = P;
	g.Q = Q;
	g.R = R;
	g._E34 = _E34;
	g._E24 = _E24;
	g._E40 = _E40;
	g._E48 = _E48;

	for (addrShift = startAddr; addrShift <= endAddr; addrShift += 4) {
		u32 addr1;
		u32 addr2;
		const u32 * V;
		u32 _DD4;
		u32 _DD6;

		F5Naboo_PrepareVertexDataStep3(&g, &vd1);
		F5Naboo_PrepareVertexDataStep3(&g, &vd2);

		/* Step 4 */
		addr1 = _SHIFTR(*CAST_DMEM(const u32*, dmemSrcAddr), 16, 16) + addrShift;
		addr2 = _SHIFTR(*CAST_DMEM(const u32*, dmemSrcAddr + 8), 16, 16) + addrShift;
		/*const u32 addrShift2 = addrShift + (addrShift & 4); */

		F5Naboo_PrepareVertexDataStep4(&g, addr1, *CAST_DMEM(const u32*, dmemSrcAddr + 0x20), addrShift, &vd1);
		F5Naboo_PrepareVertexDataStep4(&g, addr2, *CAST_DMEM(const u32*, dmemSrcAddr + 0x24), addrShift, &vd2);

		/* Step 6 */
		*CAST_DMEM(SWVertex*, 0x170 + addrShift * 2) = vd1.vtx;
		*CAST_DMEM(SWVertex*, 0x1C0 + addrShift * 2) = vd2.vtx;

		/* Step 8 */
		V = CAST_DMEM(const u32*, 0xDC0);
		vd1.vtx.x += _SHIFTR(V[0], 16, 16);
		vd1.vtx.flag += _SHIFTR(V[1], 0, 16);
		vd2.vtx.x += _SHIFTR(V[0], 16, 16);
		vd2.vtx.flag += _SHIFTR(V[3], 0, 16);

		vd1.E += G;
		vd2.E += G;

		_DD4 = *CAST_DMEM(const u32*, 0xDD4);
		_DD6 = _SHIFTL(_SHIFTR(_DD4, 0, 16), 16, 16);
		G += _DD6;
	}

	/* Step 9 */
	v0_data = CAST_DMEM(const u16*, 0x128);
	X0 = v0_data[0x0 ^ 0x1];
	Y0 = v0_data[0x1 ^ 0x1];
	Z0 = v0_data[0x2 ^ 0x1];

	g.X0 = X0;
	g.Y0 = Y0;
	g.Z0 = Z0;
	g.dmemSrcAddr = dmemSrcAddr;
	F5Naboo_processVertices(&g, _SHIFTR(params[2], 16, 16), 0x170, 0x10);
	F5Naboo_processVertices(&g, _SHIFTR(params[2], 0, 16), 0x1C0, 0x18);

	/* Step 10 */
	data = &nabooData;
	params8 = RDRAM + RSP.PC[RSP.PCi];
	params16 = (u16*)(params8);
	data->AA = params16[0x6 ^ 0x1];
	data->BB = params16[0x7 ^ 0x1];
	data->CC = params8[0x1 ^ 0x3];
	data->TT = params8[0x5 ^ 0x3];
	data->EE = dmemSrcAddr;
	data->HH = _SHIFTR(*CAST_DMEM(const u32*, 0xDB8), 16, 16);
	data->DD = 0;
	F5Naboo_DrawPolygons();
}

static
void F5NABOO_TexturedPolygons(u32 _w0, u32 _w1)
{
	F5Naboo_DrawPolygons();
}

static
void F5INDI_GeometryGen(u32 _w0, u32 _w1)
{
	const u32 mode = _SHIFTR(_w1, 0, 8);
	switch (mode) {
	case 0x09: /* Naboo */
		F5Naboo_GenVertices09();
		RSP.PC[RSP.PCi] += 16;
		break;
	case 0x0C: /* Naboo */
		F5Naboo_GenVertices0C();
		RSP.PC[RSP.PCi] += 16;
		break;
	case 0x0F: /* Naboo */
		if (*(DMEM + (0x37E ^ 3)) == 0) {
			F5Naboo_CopyData00();
			RSP.PC[RSP.PCi] += 56;
		} else {
			F5Naboo_CopyDataFF();
			RSP.PC[RSP.PCi] += 8;
		}
		break;
	case 0x15: /* Naboo */
		F5Naboo_TexrectGen();
		RSP.PC[RSP.PCi] += 24;
		break;
	case 0x18:
		F5INDI_GenVertices(_w0, _w1);
		break;
	case 0x24:
		F5INDI_DoSubDList();
		F5INDI_TexrectGen();
		RSP.PC[RSP.PCi] += 32;
		break;
	case 0x27:
		F5INDI_GenParticlesVertices();
		RSP.PC[RSP.PCi] += 16;
		break;
	case 0x4F: /* Naboo */
		*(DMEM + (0x37E ^ 3)) = 0;
		break;
	}
}

static
void F5INDI_Texrect(u32 _w0, u32 _w1)
{
	F5INDI_DoSubDList();
	gli_RDP_TexRect(_w0, _w1);
}

static
void F5INDI_JumpDL(u32 _w0, u32 _w1)
{
	RSP.PC[RSP.PCi] = RSP.F5DL[RSP.PCi];
	_updateF5DL();
}

static
void F5INDI_EndDisplayList(u32 _w0, u32 _w1)
{
	gli_gSPEndDisplayList();
}

static
void F5INDI_MoveWord(u32 _w0, u32 _w1)
{
	const u32 destAddr = _SHIFTR(_w0, 0, 12);
	*CAST_DMEM(u32*, destAddr) = _w1;

	switch (destAddr) {
	case G_MWO_CLIP_RNX:
	case G_MWO_CLIP_RNY:
	case G_MWO_CLIP_RPX:
	case G_MWO_CLIP_RPY:
		gli_gSPClipRatio(_w1);
		break;
	case G_MW_SEGMENT:
		assert(0);
		break;
	case 0x158:
	case 0x15C:
		/* Used to adjust colors (See command 0x01 - Case 02) */
		break;
	case 0x160:
		gSP.fog.multiplierf = _FIXED2FLOAT((s32)_w1, 16);
		gSP.changed |= CHANGED_FOGPOSITION;
		break;
	case 0x164:
		gSP.fog.offsetf = _FIXED2FLOAT((s32)_w1, 16);
		gSP.changed |= CHANGED_FOGPOSITION;
		break;
	case 0x14C:
		gli_gSPPerspNormalize(_w1);
		break;
	case 0x58C:
		/* sub dlist address; */
		break;
	case 0x5B0:
	case 0x5B4:
		/* Used to adjust colors (See command 0x01 - Case 02) */
		break;
	}
}

static
void F5INDI_SetOtherMode(u32 w0, u32 w1)
{
	u32 A0;
	u32 mask = (s32)0x80000000 >> _SHIFTR(w0, 0, 5);
	mask >>= _SHIFTR(w0, 8, 5);

	A0 = _SHIFTR(w0, 16, 3);
	if (A0 == 0) {
		gDP.otherMode.h = (gDP.otherMode.h&(~mask)) | w1;
		if (mask & 0x00300000)  /* cycle type */
			gDP.changed |= CHANGED_CYCLETYPE;
	} else if (A0 == 4) {
		gDP.otherMode.l = (gDP.otherMode.l&(~mask)) | w1;

		if (mask & 0x00000003)  /* alpha compare */
			gDP.changed |= CHANGED_ALPHACOMPARE;

		if (mask & 0xFFFFFFF8)  /* rendermode / blender bits */
			gDP.changed |= CHANGED_RENDERMODE;
	}
}

static
void F5INDI_SetOtherMode_Conditional(u32 w0, u32 w1)
{
	if (_SHIFTR(w0, 23, 1) != *CAST_DMEM(const u32*, 0x11C))
		return;

	F5INDI_SetOtherMode(w0, w1);
}

static
void F5INDI_ClearGeometryMode(u32 w0, u32 w1)
{
	gli_gSPClearGeometryMode(~w1);
}

static
void F5INDI_Texture(u32 w0, u32 w1)
{
	u32 V0;
	gli_F3D_Texture(w0, w1);
	*CAST_DMEM(u32*, 0x148) = w0;
	V0 = (gSP.geometryMode ^ w0) & 2;
	gSP.geometryMode ^= V0;
}

void gli_F5Indi_Naboo_Init(void)
{
	srand((unsigned int)time(NULL));

	gli_gSPSetupFunctions();
	/* Set GeometryMode flags */
	GBI_InitFlags(F3D);
	G_SHADING_SMOOTH = G_SHADE;

	gli_GBIPCStackSize = 10;

	/*			GBI Command				Command Value				Command Function */
	GBI_SetGBI(G_SPNOOP,				F3D_SPNOOP,					gli_F3D_SPNoOp);
	GBI_SetGBI(G_MOVEMEM,				F5INDI_MOVEMEM,				F5INDI_MoveMem);
	GBI_SetGBI(G_SET_DLIST_ADDR,		F5INDI_SET_DLIST_ADDR,		F5INDI_SetDListAddr);
	GBI_SetGBI(G_RESERVED1,				F5INDI_GEOMETRY_GEN,		F5INDI_GeometryGen);
	GBI_SetGBI(G_DL,					F5INDI_DL,					F5INDI_DList);
	GBI_SetGBI(G_RESERVED2,				F5INDI_BRANCHDL,			F5INDI_BranchDList);
	GBI_SetGBI(G_TRI2,					F5INDI_TRI2,				F5INDI_Tri);
	GBI_SetGBI(G_JUMPDL,				F5INDI_JUMPDL,				F5INDI_JumpDL);
	GBI_SetGBI(G_SETGEOMETRYMODE,		F3D_SETGEOMETRYMODE,		gli_F3D_SetGeometryMode);
	GBI_SetGBI(G_CLEARGEOMETRYMODE,		F3D_CLEARGEOMETRYMODE,		F5INDI_ClearGeometryMode);
	GBI_SetGBI(G_ENDDL,					F5INDI_ENDDL,				F5INDI_EndDisplayList);
	GBI_SetGBI(G_SETOTHERMODE_L,		F5INDI_SETOTHERMODE_C,		F5INDI_SetOtherMode_Conditional);
	GBI_SetGBI(G_SETOTHERMODE_H,		F5INDI_SETOTHERMODE,		F5INDI_SetOtherMode);
	GBI_SetGBI(G_TEXTURE,				F3D_TEXTURE,				F5INDI_Texture );
	GBI_SetGBI(G_MOVEWORD,				F5INDI_MOVEWORD,			F5INDI_MoveWord);
	GBI_SetGBI(G_BE,					F5NABOO_BE,					F5NABOO_TexturedPolygons);
	GBI_SetGBI(G_TRI1,					F5INDI_TRI1,				F5INDI_Tri);
	GBI_SetGBI(G_INDI_TEXRECT,			F5INDI_TEXRECT,				F5INDI_Texrect);
}
