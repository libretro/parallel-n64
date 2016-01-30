#include <algorithm>
#include "Debug.h"
#include "RSP.h"
#include "RDP.h"
#include "N64.h"
#include "F3D.h"
#include "Turbo3D.h"
#include "VI.h"
#include "Combiner.h"
#include "FrameBuffer.h"
#include "DepthBuffer.h"
#include "GBI.h"
#include "PluginAPI.h"
#include "Config.h"

using namespace std;

RSPInfo		__RSP;

void RSP_LoadMatrix( f32 mtx[4][4], u32 address )
{
	f32 recip = 1.5258789e-05f;
	struct _N64Matrix
	{
		SHORT integer[4][4];
		WORD fraction[4][4];
	} *n64Mat = (struct _N64Matrix *)&gfx_info.RDRAM[address];
	int i, j;

	for (i = 0; i < 4; i++)
		for (j = 0; j < 4; j++)
			mtx[i][j] = (GLfloat)(n64Mat->integer[i][j^1]) + (GLfloat)(n64Mat->fraction[i][j^1]) * recip;
}

void RSP_CheckDLCounter(void)
{
	if (__RSP.count != -1)
   {
		--__RSP.count;
		if (__RSP.count == 0)
      {
			__RSP.count = -1;
			--__RSP.PCi;
			DebugMsg( DEBUG_LOW | DEBUG_HANDLED, "End of DL\n" );
		}
	}
}

void RSP_ProcessDList(void)
{
	if (ConfigOpen || video().isResizeWindow())
   {
		gDPFullSync();
		return;
	}

	if (*REG.VI_ORIGIN != VI.lastOrigin)
   {
		VI_UpdateSize();
		video().updateScale();
	}

	__RSP.PC[0] = *(u32*)&DMEM[0x0FF0];
	__RSP.PCi = 0;
	__RSP.count = -1;

	__RSP.halt = FALSE;
	__RSP.busy = TRUE;

	gSP.matrix.stackSize = min( 32U, *(u32*)&DMEM[0x0FE4] >> 6 );
	if (gSP.matrix.stackSize == 0)
		gSP.matrix.stackSize = 32;
	gSP.matrix.modelViewi = 0;
	gSP.changed |= CHANGED_MATRIX;
	gDP.changed &= ~CHANGED_CPU_FB_WRITE;
	gDPSetTexturePersp(G_TP_PERSP);

	u32 uc_start = *(u32*)&DMEM[0x0FD0];
	u32 uc_dstart = *(u32*)&DMEM[0x0FD8];
	u32 uc_dsize = *(u32*)&DMEM[0x0FDC];

	if ((uc_start != __RSP.uc_start) || (uc_dstart != __RSP.uc_dstart))
		gSPLoadUcodeEx(uc_start, uc_dstart, uc_dsize);

	depthBufferList().setNotCleared();

	if (GBI.getMicrocodeType() == Turbo3D)
		RunTurbo3D();
	else {
		while (!__RSP.halt) {
			if ((__RSP.PC[__RSP.PCi] + 8) > RDRAMSize) {
#ifdef DEBUG
				switch (Debug.level)
				{
					case DEBUG_LOW:
					DebugMsg( DEBUG_LOW | DEBUG_ERROR, "ATTEMPTING TO EXECUTE RSP COMMAND AT INVALID RDRAM LOCATION\n" );
					break;
					case DEBUG_MEDIUM:
					DebugMsg( DEBUG_MEDIUM | DEBUG_ERROR, "Attempting to execute RSP command at invalid RDRAM location\n" );
					break;
					case DEBUG_HIGH:
					DebugMsg( DEBUG_HIGH | DEBUG_ERROR, "// Attempting to execute RSP command at invalid RDRAM location\n" );
					break;
				}
#endif
				break;
			}

			u32 w0 = *(u32*)&gfx_info.RDRAM[__RSP.PC[__RSP.PCi]];
			u32 w1 = *(u32*)&gfx_info.RDRAM[__RSP.PC[__RSP.PCi] + 4];
			__RSP.cmd = _SHIFTR(w0, 24, 8);

#ifdef DEBUG
			DebugRSPState( __RSP.PCi, __RSP.PC[__RSP.PCi], _SHIFTR( w0, 24, 8 ), w0, w1 );
			DebugMsg( DEBUG_LOW | DEBUG_HANDLED, "0x%08lX: CMD=0x%02lX W0=0x%08lX W1=0x%08lX\n", __RSP.PC[__RSP.PCi], _SHIFTR( w0, 24, 8 ), w0, w1 );
#endif

			__RSP.PC[__RSP.PCi] += 8;
			__RSP.nextCmd = _SHIFTR(*(u32*)&gfx_info.RDRAM[__RSP.PC[__RSP.PCi]], 24, 8);

			GBI.cmd[__RSP.cmd](w0, w1);
			RSP_CheckDLCounter();
		}
	}

	if (config.frameBufferEmulation.copyDepthToRDRAM != Config::ctDisable)
		FrameBuffer_CopyDepthBuffer(gDP.colorImage.address);

	__RSP.busy = FALSE;
	gDP.changed |= CHANGED_COLORBUFFER;
}

static void RSP_SetDefaultState(void)
{
	memset(&gSP, 0, sizeof(gSPInfo));

	gSPTexture(1.0f, 1.0f, 0, 0, TRUE);
	gDP.loadTile = &gDP.tiles[7];
	gSP.textureTile[0] = &gDP.tiles[0];
	gSP.textureTile[1] = &gDP.tiles[1];
	gSP.lookat[0].x = gSP.lookat[1].x = 1.0f;
	gSP.lookatEnable = false;

	gSP.objMatrix.A = 1.0f;
	gSP.objMatrix.B = 0.0f;
	gSP.objMatrix.C = 0.0f;
	gSP.objMatrix.D = 1.0f;
	gSP.objMatrix.X = 0.0f;
	gSP.objMatrix.Y = 0.0f;
	gSP.objMatrix.baseScaleX = 1.0f;
	gSP.objMatrix.baseScaleY = 1.0f;
	gSP.objRendermode = 0;

	for (int i = 0; i < 4; i++)
	for (int j = 0; j < 4; j++)
		gSP.matrix.modelView[0][i][j] = 0.0f;

	gSP.matrix.modelView[0][0][0] = 1.0f;
	gSP.matrix.modelView[0][1][1] = 1.0f;
	gSP.matrix.modelView[0][2][2] = 1.0f;
	gSP.matrix.modelView[0][3][3] = 1.0f;

	gDP.otherMode._u64 = 0U;
}

u32 DepthClearColor = 0xfffcfffc;

static void setDepthClearColor(void)
{
	if (strstr(__RSP.romname, (const char *)"Elmo's") != NULL)
		DepthClearColor = 0xFFFFFFFF;
	else if (strstr(__RSP.romname, (const char *)"Taz Express") != NULL)
		DepthClearColor = 0xFFBCFFBC;
	else if (strstr(__RSP.romname, (const char *)"NFL QBC 2000") != NULL || strstr(__RSP.romname, (const char *)"NFL Quarterback Club") != NULL || strstr(RSP.romname, (const char *)"Jeremy McGrath Super") != NULL)
		DepthClearColor = 0xFFFDFFFC;
	else
		DepthClearColor = 0xFFFCFFFC;
}

void RSP_Init(void)
{
#ifdef OS_WINDOWS
	// Calculate RDRAM size by intentionally causing an access violation
	u32 test;
	try
	{
		test = RDRAM[0x007FFFFF] + 1;
	}
	catch (...)
	{
		test = 0;
	}
	if (test > 0)
		RDRAMSize = 0x7FFFFF;
	else
		RDRAMSize = 0x3FFFFF;
#else // OS_WINDOWS
	RDRAMSize = 1024 * 1024 * 8 - 1;
#endif // OS_WINDOWS

	__RSP.uc_start = __RSP.uc_dstart = 0;
	__RSP.bLLE = false;

	// get the name of the ROM
	char romname[21];
	for (int i = 0; i < 20; ++i)
		romname[i] = HEADER[(32 + i) ^ 3];
	romname[20] = 0;

	// remove all trailing spaces
	while (romname[strlen(romname) - 1] == ' ')
		romname[strlen(romname) - 1] = 0;

	if (strcmp(__RSP.romname, romname) != 0)
		TFH.shutdown();

	strncpy(__RSP.romname, romname, 21);
	setDepthClearColor();
	config.generalEmulation.hacks = 0;
	if (strstr(__RSP.romname, (const char *)"OgreBattle64") != NULL)
		config.generalEmulation.hacks |= hack_Ogre64;
	else if (strstr(__RSP.romname, (const char *)"MarioGolf64") != NULL ||
		strstr(__RSP.romname, (const char *)"F1 POLE POSITION 64") != NULL
		)
		config.generalEmulation.hacks |= hack_noDepthFrameBuffers;
	else if (strstr(__RSP.romname, (const char *)"CONKER BFD") != NULL ||
		strstr(__RSP.romname, (const char *)"MICKEY USA") != NULL
		)
		config.generalEmulation.hacks |= hack_blurPauseScreen;
	else if (strstr(__RSP.romname, (const char *)"MarioTennis64") != NULL)
		config.generalEmulation.hacks |= hack_scoreboardJ;
	else if (strstr(__RSP.romname, (const char *)"MarioTennis") != NULL)
		config.generalEmulation.hacks |= hack_scoreboard;
	else if (strstr(__RSP.romname, (const char *)"Pilot Wings64") != NULL)
		config.generalEmulation.hacks |= hack_pilotWings;
	else if (strstr(__RSP.romname, (const char *)"THE LEGEND OF ZELDA") != NULL ||
		strstr(__RSP.romname, (const char *)"ZELDA MASTER QUEST") != NULL ||
		strstr(__RSP.romname, (const char *)"DOUBUTSUNOMORI") != NULL
		)
		config.generalEmulation.hacks |= hack_subscreen;
	else if (strstr(__RSP.romname, (const char *)"Blast") != NULL)
		config.generalEmulation.hacks |= hack_blastCorps;
	else if (strstr(__RSP.romname, (const char *)"SPACE INVADERS") != NULL)
		config.generalEmulation.hacks |= hack_ignoreVIHeightChange;
	else if (strstr(__RSP.romname, (const char *)"MASK") != NULL) // Zelda MM
		config.generalEmulation.hacks |= hack_skipVIChangeCheck | hack_ZeldaCamera;

	api().FindPluginPath(__RSP.pluginpath);

	RSP_SetDefaultState();
}
