#include <string.h>
#include "GLideN64.h"
#include "RSP.h"
#include "RDP.h"
#include "N64.h"
#include "uCodes/F3D.h"
#include "uCodes/Turbo3D.h"
#include "uCodes/T3DUX.h"
#include "VI.h"
#include "GBI.h"
#include "Config.h"
#include "TextureFilterHandler.h"
#include "StereoFrames.h"
#include "StereoJournal.h"
#include "gSP.h"
#include "gDP.h"
#include "gli_framebuffer.h"

#define SP_STATUS_HALT 0x0001
#define SP_STATUS_BROKE 0x0002
#define SP_STATUS_TASKDONE 0x0200

RSPInfo		RSP;

static
void _ProcessDList(void)
{
	while (!RSP.halt) {
		u32 pci;
		if (RSP.PC[RSP.PCi] > RDRAMSize - 8) {
			break;
		}

		RSP.w0 = *(u32*)&RDRAM[RSP.PC[RSP.PCi]];
		RSP.w1 = *(u32*)&RDRAM[RSP.PC[RSP.PCi] + 4];
		RSP.cmd = _SHIFTR(RSP.w0, 24, 8);


		RSP.PC[RSP.PCi] += 8;
		pci = RSP.PCi;
		if (RSP.count == 1 && pci != 0)
			--pci;
		/* The loop guard above only validated the current command at the
		 * pre-increment PC; this prefetch reads a word at the advanced PC
		 * (or, when count==1, a shallower stack level), which can sit one
		 * word past RDRAM at the buffer boundary. Bound it the same way. */
		if (RSP.PC[pci] <= RDRAMSize - 4)
			RSP.nextCmd = _SHIFTR(*(u32*)&RDRAM[RSP.PC[pci]], 24, 8);
		else
			RSP.nextCmd = 0;

		gli_GBIcmd[RSP.cmd](RSP.w0, RSP.w1);
		gli_RSP_CheckDLCounter();
	}
}

static
void _ProcessDListFactor5(void)
{
	static u32 vAddrToClear[7] = { 0x11C >> 2, 0x120 >> 2, 0x124 >> 2, 0x37C >> 2,
		0x58C >> 2, 0x5B0 >> 2, 0x5B4 >> 2};
	u32 * pDmem32 = (u32*)DMEM;
	u32 i;

	/* Lemmy's note: read first 64 bits of this dlist */
	if (RSP.PC[0] <= RDRAMSize - 4)
		RSP.F5DL[0] = _SHIFTR(*(u32*)&RDRAM[RSP.PC[0]], 0, 24);
	RSP.PC[0] += 8;

	for (i = 0; i < 7; ++i)
		pDmem32[vAddrToClear[i]] = 0U;

	while (!RSP.halt) {
		if (RSP.PC[RSP.PCi] > RDRAMSize - 8) {
			break;
		}

		RSP.w0 = *(u32*)&RDRAM[RSP.PC[RSP.PCi]];
		RSP.w1 = *(u32*)&RDRAM[RSP.PC[RSP.PCi] + 4];
		RSP.cmd = _SHIFTR(RSP.w0, 24, 8);


		/* Same boundary case as _ProcessDList: the guard validated the
		 * current command, but this prefetch reads a word at PC + 8. */
		if (RSP.PC[RSP.PCi] <= RDRAMSize - 12)
			RSP.nextCmd = _SHIFTR(*(u32*)&RDRAM[RSP.PC[RSP.PCi] + 8], 24, 8);
		else
			RSP.nextCmd = 0;

		gli_GBIcmd[RSP.cmd](RSP.w0, RSP.w1);
		RSP.PC[RSP.PCi] += 8;
		gli_RSP_CheckDLCounter();
	}
}

void gli_RSP_CheckDLCounter(void)
{
	if (RSP.count != -1) {
		--RSP.count;
		if (RSP.count == 0) {
			RSP.count = -1;
			if (RSP.PCi != 0)
				--RSP.PCi;
			else
				RSP.halt = true;
		}
	}
}

/* Stereoscopic 3D: the task's DMEM, RSP and RDP state, and the RDRAM the left
 * pass changed in place, for the right pass to start from */
static u8 l_stereoDmem[0x1000];
static gSPInfo l_stereoGSP;
static gDPInfo l_stereoGDP;
static stereo_journal_t l_stereoJournal;

void gli_RSP_StereoJournal(u32 _address, u32 _size)
{
	stereo_journal_record(&l_stereoJournal, RDRAM, RDRAMSize, _address, _size);
}

void gli_RSP_StereoDestroy(void)
{
	stereo_journal_free(&l_stereoJournal);
	RSP.stereoPass = RSP_STEREO_MONO;
}

static
void _runDisplayList(void)
{
	if (RSP.infloop) {
		RSP.infloop = false;
		RSP.halt = false;
	} else {
		u32 uc_start;
		u32 uc_dstart;
		u32 uc_dsize;
		if (*REG.VI_ORIGIN != VI.lastOrigin) {
			gli_VI_UpdateSize();
			gli_window_update_scale();
		}

		RSP.PC[0] = *(u32*)&DMEM[0x0FF0];
		RSP.PCi = 0;
		RSP.count = -1;

		RSP.halt = false;
		RSP.busy = true;

		gSP.matrix.stackSize = (*(u32*)&DMEM[0x0FE4] >> 6) < 32U ? (*(u32*)&DMEM[0x0FE4] >> 6) : 32U;
		if (gSP.matrix.stackSize == 0)
			gSP.matrix.stackSize = 32;
		gSP.matrix.modelViewi = 0;
		gSP.status[0] = gSP.status[1] = gSP.status[2] = gSP.status[3] = 0;
		gSP.geometryMode = 0U;
		gSP.changed |= CHANGED_MATRIX | CHANGED_LIGHT | CHANGED_LOOKAT | CHANGED_GEOMETRYMODE;
		gSP.tri_num = 0;
		gSP.cbfd.advancedLighting = false;
		gDP.changed &= ~CHANGED_CPU_FB_WRITE;
		gli_gDPSetTexturePersp(G_TP_PERSP);

		uc_start = *(u32*)&DMEM[0x0FD0];
		uc_dstart = *(u32*)&DMEM[0x0FD8];
		uc_dsize = *(u32*)&DMEM[0x0FDC];

		if ((uc_start != RSP.uc_start) || (uc_dstart != RSP.uc_dstart))
			gli_gSPLoadUcodeEx(uc_start, uc_dstart, uc_dsize);

		gli_depth_set_cleared(false);

		if (gli_GBI->type == ZSortBOSS) {
			RSP.PC[1] = *(u32*)&DMEM[0xff8];
			*REG.SP_STATUS &= ~0x300;  /* clear sig1 | sig2 */
			*REG.SP_STATUS |= 0x800;   /* set sig4 */
		}
	}

	switch (gli_GBI->type) {
	case Turbo3D:
		gli_RunTurbo3D();
		break;
	case T3DUX:
		gli_RunT3DUX();
		break;
	case F5Rogue:
	case F5Indi_Naboo:
		_ProcessDListFactor5();
		break;
	default:
		_ProcessDList();
		break;
	}
}

void gli_RSP_ProcessDList(void)
{
	if (ConfigOpen) {
		*REG.MI_INTR |= MI_INTR_DP;
		CheckInterrupts();
		return;
	}

	if (config.stereo.mode == stereoOff) {
		_runDisplayList();
	} else {
		struct FrameBuffer * pCurrent;
		gli_gSPStereoBeginTask();
		if (config.stereo.mode != stereoBothEyes || RSP.infloop) {
			/* One eye; or a task going on after waiting on the CPU, which goes on
			 * with the eye it was started with and is shown flat */
			gli_gSPSetStereoEye(config.stereo.mode == stereoBothEyes
				? stereoLeftEye : config.stereo.mode);
			_runDisplayList();
			if (config.stereo.mode == stereoBothEyes)
				gli_StereoFrames_forget(gli_fb_current());
		} else {
			const struct FrameBuffer * pStart = gli_fb_current();
			const u32 startAddress = pStart != NULL ? gli_fb_start_address(pStart) : 0U;
			memcpy(l_stereoDmem, DMEM, sizeof(l_stereoDmem));
			l_stereoGSP = gSP;
			l_stereoGDP = gDP;
			stereo_journal_clear(&l_stereoJournal);
			gli_StereoFrames_beginTask();
			RSP.stereoPass = RSP_STEREO_LEFT;
			gli_gSPSetStereoEye(stereoLeftEye);
			gli_StereoFrames_setCapturing(true);
			_runDisplayList();
			gli_StereoFrames_setCapturing(false);
			RSP.stereoPass = RSP_STEREO_MONO;
			if (RSP.infloop && REG.SP_STATUS != NULL) {
				/* Waits on the CPU: what is left of the task is drawn once */
				gli_StereoFrames_forget(gli_fb_current());
			} else {
				/* The right eye draws into the same buffer, so put the left one aside first */
				gli_StereoFrames_captureLeftEye(gli_fb_current());
				stereo_journal_restore(&l_stereoJournal, RDRAM);
				memcpy(DMEM, l_stereoDmem, sizeof(l_stereoDmem));
				gSP = l_stereoGSP;
				gDP = l_stereoGDP;
				gSP.changed |= CHANGED_VIEWPORT | CHANGED_MATRIX | CHANGED_TEXTURE | CHANGED_GEOMETRYMODE |
					CHANGED_FOGPOSITION | CHANGED_LIGHT | CHANGED_LOOKAT | CHANGED_TEXTURESCALE | CHANGED_HW_LIGHT;
				gDP.changed |= CHANGED_RENDERMODE | CHANGED_CYCLETYPE | CHANGED_SCISSOR | CHANGED_TMEM |
					CHANGED_TILE | CHANGED_REJECT_BOX | CHANGED_COMBINE | CHANGED_ALPHACOMPARE |
					CHANGED_FOGCOLOR | CHANGED_BLENDCOLOR | CHANGED_FB_TEXTURE;
				if (pStart != NULL)
					gli_fb_make_current(startAddress);
				RSP.stereoPass = RSP_STEREO_RIGHT;
				gli_gSPSetStereoEye(stereoRightEye);
				_runDisplayList();
				RSP.stereoPass = RSP_STEREO_MONO;
			}
		}
		pCurrent = gli_fb_current();
		if (pCurrent != NULL)
			gli_gSPImmersiveFrameDrawn(gli_fb_start_address(pCurrent));
		gli_gSPSetStereoEye(config.stereo.mode);
	}

	if (RSP.infloop && REG.SP_STATUS) {
		*REG.SP_STATUS &= ~(SP_STATUS_TASKDONE | SP_STATUS_HALT | SP_STATUS_BROKE);
		return;
	}

	if (config.frameBufferEmulation.copyDepthToRDRAM != cdDisable) {
		if ((config.generalEmulation.hacks & hack_rectDepthBufferCopyCBFD) != 0) {
			; /* do nothing */
		} else if ((config.generalEmulation.hacks & hack_rectDepthBufferCopyPD) != 0) {
			if (rectDepthBufferCopyFrame == gli_window_swap_count())
				gli_fb_copy_depth(gDP.colorImage.address);
		} else if (!gli_FBInfo_supported())
			gli_fb_copy_depth(gDP.colorImage.address);
	}

	RSP.busy = false;
	gDP.changed |= CHANGED_COLORBUFFER;
}

static
void RSP_SetDefaultState(void)
{
	int i;
	int j;
	memset(&gSP, 0, sizeof(gSPInfo));

	gli_gSPTexture(1.0f, 1.0f, 0, 0, TRUE);
	gDP.loadTile = &gDP.tiles[7];
	gSP.textureTile[0] = &gDP.tiles[0];
	gSP.textureTile[1] = &gDP.tiles[1];
	gSP.lookat.xyz[0][Y] = gSP.lookat.xyz[1][X] = 1.0f;
	gSP.lookatEnable = true;

	gSP.objRendermode = 0;

	for (i = 0; i < 4; i++)
	for (j = 0; j < 4; j++)
		gSP.matrix.modelView[0][i][j] = 0.0f;

	gSP.matrix.modelView[0][0][0] = 1.0f;
	gSP.matrix.modelView[0][1][1] = 1.0f;
	gSP.matrix.modelView[0][2][2] = 1.0f;
	gSP.matrix.modelView[0][3][3] = 1.0f;

	gSP.clipRatio = 2U;

	gDP.otherMode._u64 = 0U;
	gDP.otherMode.bi_lerp0 = gDP.otherMode.bi_lerp1 = 1;
}

u32 DepthClearColor = 0xfffcfffc;

static
void setDepthClearColor(void)
{
	if (strstr(RSP.romname, (const char *)"Elmo's") != NULL)
		DepthClearColor = 0xFFFFFFFF;
	else if (strstr(RSP.romname, (const char *)"Taz Express") != NULL)
		DepthClearColor = 0xFFBCFFBC;
	else if (strstr(RSP.romname, (const char *)"NFL QBC 2000") != NULL || strstr(RSP.romname, (const char *)"NFL Quarterback Club") != NULL || strstr(RSP.romname, (const char *)"Jeremy McGrath Super") != NULL)
		DepthClearColor = 0xFFFDFFFC;
	else
		DepthClearColor = 0xFFFCFFFC;
}

void gli_RSP_Init(void)
{
	if (RDRAMSize == 0) {
#ifdef OS_WINDOWS
		/* RDRAM, like HEADER, is only populated once the core has called
		 * initiateGFX. On the early-RomOpen path it is still NULL: leave
		 * RDRAMSize at 0 so the real RomOpen sets it. */
		if (RDRAM != NULL)
			RDRAMSize = 0x7FFFFF;
#else
		RDRAMSize = 1024 * 1024 * 8 - 1;
#endif
	}

	RSP.uc_start = RSP.uc_dstart = 0;
	RSP.LLE = false;
	RSP.infloop = false;
	RSP.stereoPass = RSP_STEREO_MONO;
	gli_gSPStereoReset();

	/* HEADER points at the ROM header and is only populated once the core has
	 * called initiateGFX (CommonAPIImpl::_initiateGFX: HEADER = _gfxInfo.HEADER).
	 * On some front-end paths (e.g. the libretro core on macOS arm64) a GL context
	 * reset drives RomOpen -> gli_RSP_Init before initiateGFX has run, so HEADER is
	 * still NULL; reading HEADER[(32+i)^3] then dereferences a near-NULL address
	 * and crashes. Only derive the ROM name (and its per-game hacks) when HEADER
	 * is valid -- gli_RSP_Init runs again from the real RomOpen once it is set. The
	 * tail (setDepthClearColor / FindPluginPath / RSP_SetDefaultState) must still
	 * run on every call, so it stays outside this guard. */
	if (HEADER != NULL)
	{
		int i;
		/* get the name of the ROM */
		char romname[21];
		size_t len;
		for (i = 0; i < 20; ++i)
			romname[i] = HEADER[(32 + i) ^ 3];
		romname[20] = 0;

		/* remove all trailing spaces */
		for (len = strlen(romname); len > 0 && romname[len - 1] == ' '; --len)
			romname[len - 1] = 0;

		if (strcmp(RSP.romname, romname) != 0)
			gli_TFH_shutdown();

		strncpy(RSP.romname, romname, 21);
	}
	setDepthClearColor();
	config.generalEmulation.hacks = 0;
	if (strstr(RSP.romname, (const char *)"OgreBattle64") != NULL)
		config.generalEmulation.hacks |= hack_Ogre64;
	else if (strstr(RSP.romname, (const char *)"F1 POLE POSITION 64") != NULL)
		config.generalEmulation.hacks |= hack_noDepthFrameBuffers;
	else if (strstr(RSP.romname, (const char *)"ROADSTERS TROPHY") != NULL)
		config.generalEmulation.hacks |= hack_noDepthFrameBuffers;
	else if (strstr(RSP.romname, (const char *)"VIGILANTE 8") != NULL)
		config.generalEmulation.hacks |= hack_noDepthFrameBuffers;
	else if (strstr(RSP.romname, (const char *)"CONKER BFD") != NULL)
		config.generalEmulation.hacks |= hack_blurPauseScreen | hack_rectDepthBufferCopyCBFD;
	else if (strstr(RSP.romname, (const char *)"MICKEY USA") != NULL)
		config.generalEmulation.hacks |= hack_blurPauseScreen;
	else if (strstr(RSP.romname, (const char *)"GOLDENEYE") != NULL)
		config.generalEmulation.hacks |= hack_clearAloneDepthBuffer;
	else if (strstr(RSP.romname, (const char *)"STARCRAFT 64") != NULL)
		config.generalEmulation.hacks |= hack_StarCraftBackgrounds;
	else if (strstr(RSP.romname, (const char *)"POKEMON STADIUM 2") != NULL ||
			 strstr(RSP.romname, (const char *)"Bottom of the 9th") != NULL)
		config.generalEmulation.hacks |= hack_texrect_shade_alpha;
	else if (strstr(RSP.romname, (const char *)"THE LEGEND OF ZELDA") != NULL ||
			 strstr(RSP.romname, (const char *)"ZELDA MASTER QUEST") != NULL)
		config.generalEmulation.hacks |= hack_subscreen | hack_ZeldaMonochrome;
	else if (strstr(RSP.romname, (const char *)"DOUBUTSUNOMORI") != NULL ||
			 strstr(RSP.romname, (const char *)"ANIMAL FOREST") != NULL)
		config.generalEmulation.hacks |= hack_subscreen;
	else if (strstr(RSP.romname, (const char *)"Lode Runner 3D") != NULL)
		config.generalEmulation.hacks |= hack_LodeRunner;
	else if (strstr(RSP.romname, (const char *)"Blast") != NULL)
		config.generalEmulation.hacks |= hack_blastCorps;
	else if (strstr(RSP.romname, (const char *)"MASK") != NULL) /* Zelda MM */
		config.generalEmulation.hacks |= hack_ZeldaMonochrome | hack_ZeldaMM;
	else if (strstr(RSP.romname, (const char *)"Perfect Dark") != NULL ||
			 strstr(RSP.romname, (const char *)"PERFECT DARK") != NULL)
		config.generalEmulation.hacks |= hack_rectDepthBufferCopyPD | hack_clearAloneDepthBuffer;
	else if (strstr(RSP.romname, (const char *)"Jeremy McGrath Super") != NULL)
		config.generalEmulation.hacks |= hack_ModifyVertexXyInShader;
	else if (strstr(RSP.romname, (const char *)"RAT ATTACK") != NULL)
		config.generalEmulation.hacks |= hack_ModifyVertexXyInShader;
	else if (strstr(RSP.romname, (const char *)"Quake") != NULL)
		config.generalEmulation.hacks |= hack_doNotResetOtherModeH|hack_doNotResetOtherModeL;
	else if (strstr(RSP.romname, (const char *)"QUAKE II") != NULL ||
			 strstr(RSP.romname, (const char *)"GAUNTLET LEGENDS") != NULL)
		config.generalEmulation.hacks |= hack_doNotResetOtherModeH;
	else if (strstr(RSP.romname, (const char *)"quarterback_club_98") != NULL)
		config.generalEmulation.hacks |= hack_LoadDepthTextures;
	else if (strstr(RSP.romname, (const char *)"WIN BACK") != NULL ||
		strstr(RSP.romname, (const char *)"OPERATION WINBACK") != NULL)
		config.generalEmulation.hacks |= hack_WinBack;
	else if (strstr(RSP.romname, (const char *)"POKEMON SNAP") != NULL)
		config.generalEmulation.hacks |= hack_Snap;
	else if (strstr(RSP.romname, (const char *)"MARIOKART64") != NULL)
		config.generalEmulation.hacks |= hack_MK64;
	else if (strstr(RSP.romname, (const char *)"Resident Evil II") ||
			 strstr(RSP.romname, (const char *)"BioHazard II"))
		config.generalEmulation.hacks |= hack_RE2 | hack_ModifyVertexXyInShader | hack_LoadDepthTextures;
	else if (strstr(RSP.romname, (const char *)"THPS") != NULL)
		config.generalEmulation.hacks |= hack_TonyHawk;
	else if (strstr(RSP.romname, (const char *)"NITRO64") != NULL)
		config.generalEmulation.hacks |= hack_WCWNitro;
	else if (strstr(RSP.romname, (const char *)"SUPER MARIO 64") != NULL)
		config.generalEmulation.hacks |= hack_SM64;

	RSP_SetDefaultState();
}
