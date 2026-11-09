#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "convert.h"
#include "N64.h"
#include "GLideN64.h"
#include "GBI.h"
#include "RDP.h"
#include "RSP.h"
#include "uCodes/F3D.h"
#include "uCodes/F3DEX.h"
#include "uCodes/F3DEX095.h"
#include "uCodes/F3DEX2.h"
#include "uCodes/F3DEX3.h"
#include "uCodes/L3D.h"
#include "uCodes/L3DEX.h"
#include "uCodes/L3DEX2.h"
#include "uCodes/S2DEX.h"
#include "uCodes/S2DEX2.h"
#include "uCodes/F3DAM.h"
#include "uCodes/F3DDKR.h"
#include "uCodes/F3DBETA.h"
#include "uCodes/F3DPD.h"
#include "uCodes/F3DSETA.h"
#include "uCodes/F3DGOLDEN.h"
#include "uCodes/F3DEX2CBFD.h"
#include "uCodes/F3DZEX2.h"
#include "uCodes/F3DTEXA.h"
#include "uCodes/F3DEX2ACCLAIM.h"
#include "uCodes/F3DFLX2.h"
#include "uCodes/F5Indi_Naboo.h"
#include "uCodes/F5Rogue.h"
#include "uCodes/ZSort.h"
#include "uCodes/ZSortBOSS.h"
#include "CRC.h"
#include "gli_framebuffer.h"

typedef struct SpecialMicrocodeInfo
{
	u32 type;
	bool NoN; /* ucode does not use near clipping */
	bool negativeY; /* Y is inverted */
	bool fast3DPerspNorm; /* ucode is from Fast3D family and has G_PERSPNORMALIZE. See #1303 */
	u32 crc;
} SpecialMicrocodeInfo;

static const SpecialMicrocodeInfo specialMicrocodes[] =
{
	{ S2DEX2,		false,	true,	false,	0x02c399dd }, /* Animal Forest */
	{ F3DEX095,		false,	false,	true,	0x0ace4c3f }, /* Mario Kart 64 */
	{ F3D,			true,	false,	false,	0x16c3a775 }, /* AeroFighters */
	{ F3DEX2CBFD,	true,	true,	false,	0x1b4ace88 }, /* Conker's Bad Fur Day */
	{ F3DPD,		true,	true,	false,	0x1c4f7869 }, /* Perfect Dark */
	{ F3D,			false,	false,	true,	0x1f24cc84 }, /* Wayne Gretzky's 3D Hockey (U) */
	{ F5Indi_Naboo,	false,	false,	false,	0x23fef05f }, /* SW Ep.1 Battle for Naboo */
	{ Turbo3D,		false,	true,	false,	0x2bdcfc8a }, /* Dark Rift, Turbo3D */
	{ F3DSETA,		false,	true,	true,	0x2edee7be }, /* RSP SW Version: 2.0D, 04-01-96 */
	{ F3DGOLDEN,	true,	true,	false,	0x302bca09 }, /* RSP SW Version: 2.0G, 09-30-96 GoldenEye */
	/* Vivid Dolls [ALECK64]: its viewport carries a negative Y scale and the */
	/* game's own transform already accounts for it; honouring the sign again */
	/* (gSPProcessVertex) draws the whole screen upside down.  negativeY off is */
	/* what makes gli_gSPViewport normalise the sign away, as for the 0x55be9bad */
	/* F3D entry below. */
	{ F3D,			false,	false,	false,	0x4AED6B3B }, /* Vivid Dolls [ALECK64] */
	{ F3D,			true,	true,	true,	0x54c558ba }, /* RSP SW Version: 2.0D, 04-01-96 Pilot Wings, Blast Corps */
	{ ZSortBOSS,	false,	false,	false,	0x553538cc }, /* World Driver Championship */
	{ F3D,			false,	false,	true,	0x55be9bad }, /* RSP SW Version: 2.0D, 04-01-96, Mischief Makers, Mortal Combat Trilogy, J.League Live */
	{ F3DEX,		true,	true,	true,	0x637b4b58 }, /* RSP SW Version: 2.0D, 04-01-96 Power League */
	{ F5Indi_Naboo,	false,	false,	false,	0x6859bf8e }, /* Indiana Jones */
	{ F3D,			false,	false,	true,	0x6932365f }, /* Super Mario 64 */
	{ ZSortBOSS,	false,	false,	false,	0x6a76f8dd }, /* Stunt Racer */
	{ F3DDKR,		false,	true,	true,	0x6e6fc893 }, /* Diddy Kong Racing */
	{ ZSortBOSS,	false,	false,	false,	0x75ed44cc }, /* World Driver Championship, European */
	{ F3D,			true,	false,	true,	0x77195a68 }, /* Dark Rift */
	{ L3D,			true,	true,	true,	0x771ce0c4 }, /* RSP SW Version: 2.0D, 04-01-96 Blast Corps */
	{ F3D,			false,	false,	false,	0x7d372819 }, /* Pachinko nichi 365 */
	{ F3DDKR,		false,	true,	true,	0x8d91244f }, /* Diddy Kong Racing */
	{ F3DBETA,		false,	true,	true,	0x94c4c833 }, /* Star Wars Shadows of Empire */
	{ S2DEX_1_05,	false,	true,	false,	0x9df31081 }, /* RSP Gfx ucode S2DEX  1.06 Yoshitaka Yasumoto Nintendo */
	{ T3DUX,		false,	true,	false,	0xbad437f2 }, /* T3DUX vers 0.83 for Toukon Road */
	{ F3DJFG,		false,	true,	true,	0xbde9d1fb }, /* Jet Force Gemini, Mickey */
	{ T3DUX,		false,	true,	false,	0xd0a1aa3d }, /* T3DUX vers 0.85 for Toukon Road 2 */
	{ F3DBETA,		false,	true,	true,	0xd17906e2 }, /* RSP SW Version: 2.0D, 04-01-96, Wave Race (U) */
	{ F3DZEX2MM,	true,	true,	false,	0xd39a0d4f }, /* Animal Forest */
	{ F3D,			false,	false,	true,	0xd3ab59b2 }, /* Cruise'n USA */
	{ F5Rogue,		false,	false,	false,	0xda51ccdb }, /* Star Wars RS */
	{ F3D,			false,	false,	false,	0xe01e14be }, /* Eikou no Saint Andrews */
	{ F3DEX2ACCLAIM,true,	true,	false,	0xe44df568 }, /* Acclaim games: Turok2 & 3, Armories and South park */
	{ F3D,			false,	true,	false,	0xe62a706d }, /* Fast3D */
};

u32 G_RDPHALF_1, G_RDPHALF_2, G_RDPHALF_CONT;
u32 G_SPNOOP;
u32 G_SETOTHERMODE_H, G_SETOTHERMODE_L;
u32 G_DL, G_ENDDL, G_CULLDL, G_BRANCH_Z, G_BRANCH_W;
u32 G_TRISTRIP, G_TRIFAN, G_LIGHTTORDP, G_RELSEGMENT;
u32 G_LOAD_UCODE;
u32 G_MOVEMEM, G_MOVEWORD;
u32 G_MTX, G_POPMTX;
u32 G_GEOMETRYMODE, G_SETGEOMETRYMODE, G_CLEARGEOMETRYMODE;
u32 G_TEXTURE;
u32 G_DMA_IO, G_DMA_DL, G_DMA_TRI, G_DMA_MTX, G_DMA_VTX, G_DMA_TEX_OFFSET, G_DMA_OFFSETS;
u32 G_SPECIAL_1, G_SPECIAL_2, G_SPECIAL_3;
u32 G_VTX, G_MODIFYVTX, G_VTXCOLORBASE;
u32 G_TRI1, G_TRI2, G_TRIX;
u32 G_QUAD, G_LINE3D;
u32 G_RESERVED0, G_RESERVED1, G_RESERVED2, G_RESERVED3;
u32 G_SPRITE2D_BASE;
u32 G_BG_1CYC, G_BG_COPY;
u32 G_OBJ_RECTANGLE, G_OBJ_SPRITE, G_OBJ_MOVEMEM;
u32 G_SELECT_DL, G_OBJ_RENDERMODE, G_OBJ_RECTANGLE_R;
u32 G_OBJ_LOADTXTR, G_OBJ_LDTX_SPRITE, G_OBJ_LDTX_RECT, G_OBJ_LDTX_RECT_R;
u32 G_RDPHALF_0;
u32 G_PERSPNORM;
u32 G_ZOBJ, G_ZRDPCMD, G_ZWAITSIGNAL, G_ZMTXCAT, G_ZMULT_MPMTX, G_ZLIGHTING;


u32 G_MTX_STACKSIZE;
u32 G_MTX_MODELVIEW;
u32 G_MTX_PROJECTION;
u32 G_MTX_MUL;
u32 G_MTX_LOAD;
u32 G_MTX_NOPUSH;
u32 G_MTX_PUSH;

u32 G_TEXTURE_ENABLE;
u32 G_SHADING_SMOOTH;
u32 G_CULL_FRONT;
u32 G_CULL_BACK;
u32 G_CULL_BOTH;
u32 G_CLIPPING;

u32 G_MV_VIEWPORT;

u32 G_MWO_aLIGHT_1, G_MWO_bLIGHT_1;
u32 G_MWO_aLIGHT_2, G_MWO_bLIGHT_2;
u32 G_MWO_aLIGHT_3, G_MWO_bLIGHT_3;
u32 G_MWO_aLIGHT_4, G_MWO_bLIGHT_4;
u32 G_MWO_aLIGHT_5, G_MWO_bLIGHT_5;
u32 G_MWO_aLIGHT_6, G_MWO_bLIGHT_6;
u32 G_MWO_aLIGHT_7, G_MWO_bLIGHT_7;
u32 G_MWO_aLIGHT_8, G_MWO_bLIGHT_8;

/* Addresses and text are read once per microcode: a fixed set of them, the
 * oldest given up first, so that a game never allocates here */
#define GBI_MAX_MICROCODES 64

/* What the callers see before any microcode is loaded */
static const MicrocodeInfo l_noMicrocode = {
	0, 0, 0, NONE,
	false, false, false, true, false, true, false,
	{ true, false, false }
};

const MicrocodeInfo * gli_GBI = &l_noMicrocode;
bool gli_GBI_hwlSupported;

static MicrocodeInfo l_microcodes[GBI_MAX_MICROCODES];
static u32 l_microcodeCount;
static u32 l_microcodeNext;

GBIFunc gli_GBIcmd[256];
u32 gli_GBIPCStackSize;

static void GBI_Unknown( u32 w0, u32 w1 )
{
}

static void _flushCommands(void)
{
	u32 i;
	for (i = 0; i < 256; ++i)
		gli_GBIcmd[i] = GBI_Unknown;
}

void gli_GBI_init(void)
{
	gli_GBI_hwlSupported = true;
	gli_GBI = &l_noMicrocode;
	_flushCommands();
}

void gli_GBI_destroy(void)
{
	gli_GBI = &l_noMicrocode;
	l_microcodeCount = 0;
	l_microcodeNext = 0;
}

static void _makeCurrent(const MicrocodeInfo * _pCurrent)
{
	if (_pCurrent->type == NONE) {
		return;
	}

	if ((_pCurrent->type == ZSortBOSS) && (!REG.SP_STATUS)) {
		assert(false && "ZSortBOSS ucode needs access to SP_STATUS register'n");
	}

	if (gli_GBI->type != _pCurrent->type) {
		gli_GBI = _pCurrent;
		_flushCommands();

		gli_RDP_Init();

		G_TRI1 = G_TRI2 = G_TRIX = G_QUAD = -1; /* For correct work of gli_gSPFlushTriangles() */

		switch (gli_GBI->type) {
			case F3D:
				gli_F3D_Init();
				gli_GBI_hwlSupported = true;
			break;
			case F3DEX:
				gli_F3DEX_Init();
				gli_GBI_hwlSupported = true;
			break;
			case F3DEX095:
				gli_F3DEX095_Init();
				gli_GBI_hwlSupported = true;
			break;
			case F3DEX3:
				gli_F3DEX3_Init();
				gli_GBI_hwlSupported = true;
			break;
			case F3DEX2:
				gli_F3DEX2_Init();
				gli_GBI_hwlSupported = true;
			break;
			case L3D:
				gli_L3D_Init();
				gli_GBI_hwlSupported = false;
			break;
			case L3DEX:
				gli_L3DEX_Init();
				gli_GBI_hwlSupported = false;
			break;
			case L3DEX2:
				gli_L3DEX2_Init();
				gli_GBI_hwlSupported = false;
			break;
			case S2DEX_1_03:
				gli_S2DEX_1_03_Init();
				gli_GBI_hwlSupported = false;
				break;
			case S2DEX_1_05:
				gli_S2DEX_1_05_Init();
				gli_GBI_hwlSupported = false;
				break;
			case S2DEX_1_07:
				gli_S2DEX_1_07_Init();
				gli_GBI_hwlSupported = false;
				break;
			case S2DEX2:
				gli_S2DEX2_Init();
				gli_GBI_hwlSupported = false;
				break;
			case F3DDKR:
				gli_F3DDKR_Init();
				gli_GBI_hwlSupported = false;
			break;
			case F3DJFG:
				gli_F3DJFG_Init();
				gli_GBI_hwlSupported = false;
			break;
			case F3DBETA:
				gli_F3DBETA_Init();
				gli_GBI_hwlSupported = true;
			break;
			case F3DPD:
				gli_F3DPD_Init();
				gli_GBI_hwlSupported = true;
			break;
			case F3DAM:
				gli_F3DAM_Init();
				gli_GBI_hwlSupported = true;
			break;
			case Turbo3D:
				gli_F3D_Init();
				gli_GBI_hwlSupported = true;
			break;
			case ZSortp:
				gli_ZSort_Init();
				gli_GBI_hwlSupported = true;
			break;
			case F3DEX2CBFD:
				gli_F3DEX2CBFD_Init();
				gli_GBI_hwlSupported = false;
			break;
			case F3DSETA:
				gli_F3DSETA_Init();
				gli_GBI_hwlSupported = true;
			break;
			case F3DGOLDEN:
				gli_F3DGOLDEN_Init();
				gli_GBI_hwlSupported = true;
			break;
			case F3DZEX2OOT:
				gli_F3DZEX2_Init();
				gli_GBI_hwlSupported = true;
			break;
			case F3DZEX2MM:
				gli_F3DZEX2_Init();
				gli_GBI_hwlSupported = false;
			break;
			case F3DTEXA:
				gli_F3DTEXA_Init();
				gli_GBI_hwlSupported = true;
			break;
			case T3DUX:
				gli_F3D_Init();
				gli_GBI_hwlSupported = false;
			break;
			case F3DEX2ACCLAIM:
				gli_F3DEX2ACCLAIM_Init();
				gli_GBI_hwlSupported = false;
			break;
			case F5Rogue:
				gli_F5Rogue_Init();
				gli_GBI_hwlSupported = false;
			break;
			case F3DFLX2:
				gli_F3DFLX2_Init();
				gli_GBI_hwlSupported = true;
			break;
			case ZSortBOSS:
				gli_ZSortBOSS_Init();
				gli_GBI_hwlSupported = true;
			break;
			case F5Indi_Naboo:
				gli_F5Indi_Naboo_Init();
				gli_GBI_hwlSupported = false;
				break;
		}
		gli_gfx_set_no_near_clip(gli_GBI->NoN);
		if (gli_GBI->fast3DPersp) {
			GBI_SetGBI(G_PERSPNORM, F3DBETA_PERSPNORM, gli_F3DBETA_Perpnorm);
			GBI_SetGBI(G_RDPHALF_1, F3DBETA_RDPHALF_1, gli_F3D_RDPHalf_1);
			GBI_SetGBI(G_RDPHALF_2, F3DBETA_RDPHALF_2, gli_F3D_RDPHalf_2);
		}
	} else if (gli_GBI->NoN != _pCurrent->NoN) {
		gli_gfx_set_no_near_clip(_pCurrent->NoN);
	}
	gli_GBI = _pCurrent;
}

static bool _makeExistingMicrocodeCurrent(u32 uc_start, u32 uc_dstart, u32 uc_dsize)
{
	u32 i;
	for (i = 0; i < l_microcodeCount; ++i) {
		const MicrocodeInfo * info = &l_microcodes[i];
		if (info->address == uc_start && info->dataAddress == uc_dstart && info->dataSize == uc_dsize) {
			_makeCurrent(info);
			return true;
		}
	}
	return false;
}

/* One of the name's '_' separated words */
static bool _hasFeature(const char * _name, size_t _len, const char * _feature)
{
	const size_t featureLen = strlen(_feature);
	size_t start = 0;
	while (start < _len) {
		size_t end = start;
		while (end < _len && _name[end] != '_')
			++end;
		if (end - start == featureLen && memcmp(_name + start, _feature, featureLen) == 0)
			return true;
		start = end + 1;
	}
	return false;
}

/* The first entry not below _crc */
static const SpecialMicrocodeInfo * _findSpecialMicrocode(u32 _crc)
{
	size_t first = 0;
	size_t count = sizeof(specialMicrocodes) / sizeof(specialMicrocodes[0]);
	while (count > 0) {
		const size_t step = count / 2;
		if (specialMicrocodes[first + step].crc < _crc) {
			first += step + 1;
			count -= step + 1;
		} else
			count = step;
	}
	return first < sizeof(specialMicrocodes) / sizeof(specialMicrocodes[0]) ? &specialMicrocodes[first] : NULL;
}

/* A free entry, never the one in use */
static MicrocodeInfo * _newMicrocode(void)
{
	MicrocodeInfo * current;
	if (l_microcodeCount < GBI_MAX_MICROCODES)
		current = &l_microcodes[l_microcodeCount++];
	else {
		if (gli_GBI == &l_microcodes[l_microcodeNext])
			l_microcodeNext = (l_microcodeNext + 1) % GBI_MAX_MICROCODES;
		current = &l_microcodes[l_microcodeNext];
		l_microcodeNext = (l_microcodeNext + 1) % GBI_MAX_MICROCODES;
	}
	memset(current, 0, sizeof(*current));
	current->type = NONE;
	current->cullBoth = true;
	current->negativeY = true;
	current->texturePersp = true;
	current->f3dex3.legacyVertexPipeline = true;
	return current;
}

static int ascii_tolower(int c)
{
	if (isupper(c)) return c | 32;
	return c;
}

void gli_GBI_loadMicrocode(u32 uc_start, u32 uc_dstart, u16 uc_dsize)
{
	u32 uc_crc;
	char uc_data[2048];
	char uc_str[256];
	u32 i;
	MicrocodeInfo * current;
	const SpecialMicrocodeInfo * it;
	if (_makeExistingMicrocodeCurrent(uc_start, uc_dstart, uc_dsize))
		return;

	current = _newMicrocode();
	current->address = uc_start;
	current->dataAddress = uc_dstart;
	current->dataSize = uc_dsize;
	current->type = NONE;

	/* See if we can identify it by CRC */
	/* both addresses come from the guest's task */
	uc_crc = (uc_start & 0x1FFFFFFF) <= RDRAMSize + 1 - 4096
		? CRC_Calculate_Strict( 0xFFFFFFFF, &RDRAM[uc_start & 0x1FFFFFFF], 4096 ) : 0;
	it = _findSpecialMicrocode(uc_crc);
	if (it != NULL && it->crc == uc_crc)	{
		current->type = it->type;
		current->NoN = it->NoN;
		current->negativeY = it->negativeY;
		current->fast3DPersp = it->fast3DPerspNorm;
		_makeCurrent(current);
		return;
	}

	/* See if we can identify it by text */
	if ((uc_dstart & 0x1FFFFFFF) <= RDRAMSize + 1 - 2048)
		gli_UnswapCopyWrap(RDRAM, uc_dstart & 0x1FFFFFFF, (u8*)uc_data, 0, 0x7FF, 2048);
	else
		memset(uc_data, 0, sizeof(uc_data));
	strcpy(uc_str, "Not Found");

	/* Check for F3DEX3 microcode. Tharo's F3DEX3 places its name string */
	/* at offset 0x138 in the microcode data segment, in the form */
	/* "F3DEX3_LVP_BrZ_NOC" (uppercase). Existence of the literal "f3dex3" */
	/* prefix (case-insensitive) at that offset is the detection signal; */
	/* the underscore-separated suffix encodes which compile-time features */
	/* the microcode was built with. */
	{
		static const char F3DEX3_NAME[] = "f3dex3";
		const char * probe = &uc_data[0x138];
		char name_buffer[sizeof(F3DEX3_NAME) - 1];
		size_t k;
		memcpy(name_buffer, probe, sizeof(F3DEX3_NAME) - 1);
		for (k = 0; k < sizeof(F3DEX3_NAME) - 1; ++k)
			name_buffer[k] = (char)ascii_tolower(name_buffer[k]);
		if (0 == memcmp(name_buffer, F3DEX3_NAME, sizeof(F3DEX3_NAME) - 1))
		{
			current->type = F3DEX3;
			current->NoN = true;
			current->negativeY = false;
			current->fast3DPersp = false;
			current->combineMatrices = false;

			{
				/* 0x180 is generous; the name field is space-terminated. */
				const char * name_end = (const char*)memchr(probe, ' ', 0x180);
				const size_t name_len = name_end != NULL ? (size_t)(name_end - probe) : 0;
				char name[0x180];
				for (k = 0; k < name_len; ++k)
					name[k] = (char)ascii_tolower(probe[k]);
				current->f3dex3.legacyVertexPipeline = _hasFeature(name, name_len, "lvp");
				current->f3dex3.noOcclusionPlane = _hasFeature(name, name_len, "noc");
				current->f3dex3.branchOnZ = _hasFeature(name, name_len, "brz");
			}

			_makeCurrent(current);
			return;
		}
	}

	for (i = 0; i < 2046; ++i) {
		if ((uc_data[i] == 'R') && (uc_data[i+1] == 'S') && (uc_data[i+2] == 'P')) {
			int type;
			u32 j = 0;
			/* uc_data is guest-controlled; bound the copy to uc_str */
			/* and to the source buffer. */
			while ((i + j) < 2048 && j < sizeof(uc_str) - 1 &&
				uc_data[i+j] > 0x0A) {
				uc_str[j] = uc_data[i+j];
				j++;
			}

			uc_str[j] = 0x00;

			type = NONE;

			if (strncmp(&uc_str[4], "SW", 2) == 0) {
				type = F3D;
			} else if (strncmp(&uc_str[4], "Gfx", 3) == 0) {
				current->NoN = (strstr( uc_str + 4, ".NoN") != NULL);
				current->Rej = (strstr(uc_str + 4, ".Rej") != NULL);

				if (strncmp( &uc_str[14], "F3D", 3 ) == 0) {
					if (uc_str[28] == '1' || strncmp(&uc_str[28], "0.95", 4) == 0 || strncmp(&uc_str[28], "0.96", 4) == 0)
						type = F3DEX;
					else if (uc_str[31] == '2') {
						type = F3DEX2;
						if (uc_str[35] == 'H')
							current->combineMatrices = true;
					}
					if (strncmp(&uc_str[14], "F3DFLX", 6) == 0) {
						type = F3DFLX2;
						current->NoN = true;
					} else if (strncmp(&uc_str[14], "F3DZEX", 6) == 0) {
						/* Zelda games */
						if (uc_str[34] == '6')
							type = F3DZEX2OOT;
						else
							type = F3DZEX2MM;
						current->combineMatrices = false;
					} else if (strncmp(&uc_str[14], "F3DTEX/A", 8) == 0)
						type = F3DTEXA;
					else if (strncmp(&uc_str[14], "F3DAM", 5) == 0)
						type = F3DAM;
					else if (strncmp(&uc_str[14], "F3DLX.Rej", 9) == 0) {
						current->NoN = true;
						current->cullBoth = false;
					} else if (strncmp(&uc_str[14], "F3DLP.Rej", 9) == 0) {
						current->texturePersp = false;
						current->NoN = true;
					}
				}
				else if (strncmp( &uc_str[14], "L3D", 3 ) == 0) {
					u32 t = 22;
					while (!isdigit(uc_str[t]) && t++ < j);
					if (uc_str[t] == '1')
						type = L3DEX;
					else if (uc_str[t] == '2')
						type = L3DEX2;
				}
				else if (strncmp( &uc_str[14], "S2D", 3 ) == 0) {
					u32 t = 20;
					while (!isdigit(uc_str[t]) && t++ < j);
					if (uc_str[t] == '1') {
						if (strncmp(&uc_str[21], "1.03", 4) == 0)
							type = S2DEX_1_03;
						else if (strncmp(&uc_str[21], "1.05", 4) == 0)
							type = S2DEX_1_05;
						else
							type = S2DEX_1_07;
					} else if (uc_str[t] == '2')
						type = S2DEX2;
					current->texturePersp = false;
				}
				else if (strncmp(&uc_str[14], "ZSortp", 6) == 0) {
					type = ZSortp;
				}
			}

			if (type != NONE) {
				current->type = type;
				_makeCurrent(current);
				return;
			}

			break;
		}
	}

	assert(false && "unknown ucode!!!'n");
	_makeCurrent(current);
}
