#include <algorithm>
#include <assert.h>
#include <cctype>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "convert.h"
#include "N64.h"
#include "GBI.h"
#include "RDP.h"
#include "RSP.h"
#include "F3D.h"
#include "F3DEX.h"
#include "F3DEX2.h"
#include "L3D.h"
#include "L3DEX.h"
#include "L3DEX2.h"
#include "S2DEX.h"
#include "S2DEX2.h"
#include "F3DDKR.h"
#include "F3DSWSE.h"
#include "F3DWRUS.h"
#include "F3DPD.h"
#include "F3DEX2CBFD.h"
#include "ZSort.h"
#include "CRC.h"
#include "Log.h"
#include "OpenGL.h"
#include "Debug.h"

uint32_t last_good_ucode = (uint32_t) -1;

SpecialMicrocodeInfo specialMicrocodes[] =
{
	{ F3D,		false,	0xe62a706d, "Fast3D" },
	{ F3D,		false,	0x7d372819, "Fast3D" },
	{ F3D,		false,	0x2edee7be, "Fast3D" },
	{ F3D,		false,	0xe01e14be, "Fast3D" },
   { F3D,		false,	0x4AED6B3B, "Fast3D" }, //Vivid Dolls [ALECK64]

	{ F3DWRUS,	false,	0xd17906e2, "RSP SW Version: 2.0D, 04-01-96" },
	{ F3DSWSE,	false,	0x94c4c833, "RSP SW Version: 2.0D, 04-01-96" },
	{ F3DEX,	true,	0x637b4b58, "RSP SW Version: 2.0D, 04-01-96" },
	{ F3D,		true,	0x54c558ba, "RSP SW Version: 2.0D, 04-01-96" }, // Pilot Wings
	{ F3D,		true,	0x302bca09, "RSP SW Version: 2.0G, 09-30-96" }, // GoldenEye

	{ S2DEX,	false,	0x9df31081, "RSP Gfx ucode S2DEX  1.06 Yoshitaka Yasumoto Nintendo." },

	{ F3DDKR,	false,	0x8d91244f, "Diddy Kong Racing" },
	{ F3DDKR,	false,	0x6e6fc893, "Diddy Kong Racing" },
	{ F3DJFG,	false,	0xbde9d1fb, "Jet Force Gemini" },
	{ F3DPD,	true,	0x1c4f7869, "Perfect Dark" },
	{ Turbo3D,	false,	0x2bdcfc8a, "Turbo3D" },
	{ F3DEX2CBFD, true, 0x1b4ace88, "Conker's Bad Fur Day" }
};

uint32_t G_RDPHALF_1, G_RDPHALF_2, G_RDPHALF_CONT;
uint32_t G_SPNOOP;
uint32_t G_SETOTHERMODE_H, G_SETOTHERMODE_L;
uint32_t G_DL, G_ENDDL, G_CULLDL, G_BRANCH_Z;
uint32_t G_LOAD_UCODE;
uint32_t G_MOVEMEM, G_MOVEWORD;
uint32_t G_MTX, G_POPMTX;
uint32_t G_GEOMETRYMODE, G_SETGEOMETRYMODE, G_CLEARGEOMETRYMODE;
uint32_t G_TEXTURE;
uint32_t G_DMA_IO, G_DMA_DL, G_DMA_TRI, G_DMA_MTX, G_DMA_VTX, G_DMA_TEX_OFFSET, G_DMA_OFFSETS;
uint32_t G_SPECIAL_1, G_SPECIAL_2, G_SPECIAL_3;
uint32_t G_VTX, G_MODIFYVTX, G_VTXCOLORBASE;
uint32_t G_TRI1, G_TRI2, G_TRI4;
uint32_t G_QUAD, G_LINE3D;
uint32_t G_RESERVED0, G_RESERVED1, G_RESERVED2, G_RESERVED3;
uint32_t G_SPRITE2D_BASE;
uint32_t G_BG_1CYC, G_BG_COPY;
uint32_t G_OBJ_RECTANGLE, G_OBJ_SPRITE, G_OBJ_MOVEMEM;
uint32_t G_SELECT_DL, G_OBJ_RENDERMODE, G_OBJ_RECTANGLE_R;
uint32_t G_OBJ_LOADTXTR, G_OBJ_LDTX_SPRITE, G_OBJ_LDTX_RECT, G_OBJ_LDTX_RECT_R;
uint32_t G_RDPHALF_0;

uint32_t G_MTX_STACKSIZE;
uint32_t G_MTX_MODELVIEW;
uint32_t G_MTX_PROJECTION;
uint32_t G_MTX_MUL;
uint32_t G_MTX_LOAD;
uint32_t G_MTX_NOPUSH;
uint32_t G_MTX_PUSH;

uint32_t G_TEXTURE_ENABLE;
uint32_t G_SHADING_SMOOTH;
uint32_t G_CULL_FRONT;
uint32_t G_CULL_BACK;
uint32_t G_CULL_BOTH;
uint32_t G_CLIPPING;

uint32_t G_MV_VIEWPORT;

uint32_t G_MWO_aLIGHT_1, G_MWO_bLIGHT_1;
uint32_t G_MWO_aLIGHT_2, G_MWO_bLIGHT_2;
uint32_t G_MWO_aLIGHT_3, G_MWO_bLIGHT_3;
uint32_t G_MWO_aLIGHT_4, G_MWO_bLIGHT_4;
uint32_t G_MWO_aLIGHT_5, G_MWO_bLIGHT_5;
uint32_t G_MWO_aLIGHT_6, G_MWO_bLIGHT_6;
uint32_t G_MWO_aLIGHT_7, G_MWO_bLIGHT_7;
uint32_t G_MWO_aLIGHT_8, G_MWO_bLIGHT_8;

GBIInfo GBI;

void GBI_Unknown( uint32_t w0, uint32_t w1 )
{
#ifdef DEBUG
	if (Debug.level == DEBUG_LOW)
		DebugMsg( DEBUG_LOW | DEBUG_UNKNOWN, "UNKNOWN GBI COMMAND 0x%02X", _SHIFTR( w0, 24, 8 ) );
	if (Debug.level == DEBUG_MEDIUM)
		DebugMsg( DEBUG_MEDIUM | DEBUG_UNKNOWN, "Unknown GBI Command 0x%02X", _SHIFTR( w0, 24, 8 ) );
	else if (Debug.level == DEBUG_HIGH)
		DebugMsg( DEBUG_HIGH | DEBUG_UNKNOWN, "// Unknown GBI Command 0x%02X", _SHIFTR( w0, 24, 8 ) );
#endif
}

void GBIInfo::init()
{
	m_pCurrent = NULL;
	_flushCommands();
}

void GBIInfo::destroy()
{
	m_pCurrent = NULL;
	m_list.clear();
}

bool GBIInfo::isHWLSupported() const
{
	if (m_pCurrent == NULL)
		return false;
	switch (m_pCurrent->type) {
		case S2DEX:
		case S2DEX2:
		case F3DDKR:
		case F3DJFG:
		case F3DEX2CBFD:
		return false;
	}
	return true;
}

void GBIInfo::_flushCommands()
{
	std::fill(std::begin(cmd), std::end(cmd), GBI_Unknown);
}

void GBIInfo::_makeCurrent(MicrocodeInfo * _pCurrent)
{
	if (_pCurrent->type == NONE) {
		LOG(LOG_ERROR, "[GLideN64]: error - unknown ucode!!!\n");
		return;
	}

	if (m_pCurrent == NULL || (m_pCurrent->type != _pCurrent->type)) {
		m_pCurrent = _pCurrent;
		_flushCommands();

		RDP_Init();

		G_TRI1 = G_TRI2 = G_TRI4 = G_QUAD = -1; // For correct work of gSPFlushTriangles()

		switch (m_pCurrent->type) {
			case F3D:		F3D_Init();		break;
			case F3DEX:		F3DEX_Init();	break;
			case F3DEX2:	F3DEX2_Init();	break;
			case L3D:		L3D_Init();		break;
			case L3DEX:		L3DEX_Init();	break;
			case L3DEX2:	L3DEX2_Init();	break;
			case S2DEX:		S2DEX_Init();	break;
			case S2DEX2:	S2DEX2_Init();	break;
			case F3DDKR:	F3DDKR_Init();	break;
			case F3DJFG:	F3DJFG_Init();	break;
			case F3DSWSE:	F3DSWSE_Init();	break;
			case F3DWRUS:	F3DWRUS_Init();	break;
			case F3DPD:		F3DPD_Init();	break;
			case Turbo3D:	F3D_Init();		break;
			case ZSortp:	ZSort_Init();	break;
			case F3DEX2CBFD:F3DEX2CBFD_Init(); break;
		}

#ifndef GLESX
		if (m_pCurrent->NoN) {
			// Disable near and far plane clipping
			glEnable(GL_DEPTH_CLAMP);
			// Enable Far clipping plane in vertex shader
			glEnable(GL_CLIP_DISTANCE0);
		} else {
			glDisable(GL_DEPTH_CLAMP);
			glDisable(GL_CLIP_DISTANCE0);
		}
#endif
	} else if (m_pCurrent->NoN != _pCurrent->NoN) {
#ifndef GLESX
		if (_pCurrent->NoN) {
			// Disable near and far plane clipping
			glEnable(GL_DEPTH_CLAMP);
			// Enable Far clipping plane in vertex shader
			glEnable(GL_CLIP_DISTANCE0);
		}
		else {
			glDisable(GL_DEPTH_CLAMP);
			glDisable(GL_CLIP_DISTANCE0);
		}
#endif
	}
	m_pCurrent = _pCurrent;
}

bool GBIInfo::_makeExistingMicrocodeCurrent(uint32_t uc_start, uint32_t uc_dstart, uint32_t uc_dsize)
{
	auto iter = std::find_if(m_list.begin(), m_list.end(), [=](const MicrocodeInfo& info) {
		return info.address == uc_start && info.dataAddress == uc_dstart && info.dataSize == uc_dsize;
	});

	if (iter == m_list.end())
		return false;

	_makeCurrent(&*iter);
	return true;
}

void GBIInfo::loadMicrocode(uint32_t uc_start, uint32_t uc_dstart, uint16_t uc_dsize)
{
	if (_makeExistingMicrocodeCurrent(uc_start, uc_dstart, uc_dsize))
		return;

	m_list.emplace_front();
	MicrocodeInfo & current = m_list.front();
	current.address = uc_start;
	current.dataAddress = uc_dstart;
	current.dataSize = uc_dsize;
	current.NoN = false;
	current.textureGen = true;
	current.branchLessZ = true;
	current.type = NONE;

	// See if we can identify it by CRC
	const uint32_t uc_crc = CRC_Calculate( 0xFFFFFFFF, &RDRAM[uc_start & 0x1FFFFFFF], 4096 );
	const uint32_t numSpecialMicrocodes = sizeof(specialMicrocodes) / sizeof(SpecialMicrocodeInfo);
	for (uint32_t i = 0; i < numSpecialMicrocodes; ++i) {
		if (uc_crc == specialMicrocodes[i].crc) {
			current.type = specialMicrocodes[i].type;
			current.NoN = specialMicrocodes[i].NoN;
			_makeCurrent(&current);
			return;
		}
	}

	// See if we can identify it by text
	char uc_data[2048];
	UnswapCopyWrap(RDRAM, uc_dstart & 0x1FFFFFFF, (uint8_t*)uc_data, 0, 0x7FF, 2048);
	char uc_str[256];
	strcpy(uc_str, "Not Found");

	for (uint32_t i = 0; i < 2048; ++i) {
		if ((uc_data[i] == 'R') && (uc_data[i+1] == 'S') && (uc_data[i+2] == 'P')) {
			uint32_t j = 0;
			while (uc_data[i+j] > 0x0A) {
				uc_str[j] = uc_data[i+j];
				j++;
			}

			uc_str[j] = 0x00;

			int type = NONE;

			if (strncmp( &uc_str[4], "SW", 2 ) == 0)
				type = F3D;
			else if (strncmp( &uc_str[4], "Gfx", 3 ) == 0) {
				current.NoN = (strstr( uc_str + 4, ".NoN") != NULL);

				if (strncmp( &uc_str[14], "F3D", 3 ) == 0) {
					if (uc_str[28] == '1' || strncmp(&uc_str[28], "0.95", 4) == 0 || strncmp(&uc_str[28], "0.96", 4) == 0)
						type = F3DEX;
					else if (uc_str[31] == '2')
						type = F3DEX2;
					if (strncmp(&uc_str[14], "F3DF", 4) == 0)
						current.textureGen = false;
					else if (strncmp(&uc_str[14], "F3DZ", 4) == 0)
						current.branchLessZ = false;
				}
				else if (strncmp( &uc_str[14], "L3D", 3 ) == 0) {
					uint32_t t = 22;
					while (!std::isdigit(uc_str[t]) && t++ < j);
					if (uc_str[t] == '1')
						type = L3DEX;
					else if (uc_str[t] == '2')
						type = L3DEX2;
				}
				else if (strncmp( &uc_str[14], "S2D", 3 ) == 0) {
					uint32_t t = 20;
					while (!std::isdigit(uc_str[t]) && t++ < j);
					if (uc_str[t] == '1')
						type = S2DEX;
					else if (uc_str[t] == '2')
						type = S2DEX2;
				}
				else if (strncmp(&uc_str[14], "ZSortp", 6) == 0) {
					type = ZSortp;
				}
			}

			if (type != NONE) {
				current.type = type;
				_makeCurrent(&current);
				return;
			}

			break;
		}
	}

	assert(false && "unknown ucode!!!'n");
	_makeCurrent(&current);
}
