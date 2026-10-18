#ifndef RSP_H
#define RSP_H

#include "Types.h"
#include "N64.h"

typedef struct
{
	u32 PC[18], PCi;
	u32 F5DL[10];
	u32 uc_start, uc_dstart, cmd, nextCmd;
	u32 w0, w1;
	u32 stereoPass;
	s32 count;
	bool busy, halt, infloop;
	bool LLE;
	char romname[21];
	wchar_t pluginpath[PLUGIN_PATH_SIZE];
} RSPInfo;

extern RSPInfo RSP;

extern u32 DepthClearColor;
extern u32 rectDepthBufferCopyFrame;

/* Stereoscopic 3D runs a task once per eye. The left pass only draws: what
 * the task leaves for the CPU (RDRAM, interrupts) comes from the right pass,
 * which starts from the RDRAM the left one started from. */
#define RSP_STEREO_MONO  0
#define RSP_STEREO_LEFT  1
#define RSP_STEREO_RIGHT 2

/* Called before a read-modify-write of RDRAM, which the right pass must
 * see undone */
#define RSP_StereoWillWrite(_address, _size) do { \
	if (RSP.stereoPass == RSP_STEREO_LEFT) \
		RSP_StereoJournal((_address), (_size)); \
	} while (0)

#define RSP_SegmentToPhysical( segaddr ) ((gSP.segment[(segaddr >> 24) & 0x0F] + (segaddr & RDRAMSize)) & RDRAMSize)

void RSP_Init();
void RSP_ProcessDList();
void RSP_LoadMatrix( f32 mtx[4][4], u32 address );
void RSP_CheckDLCounter();
void RSP_StereoJournal(u32 _address, u32 _size);
void RSP_StereoDestroy();

#endif
