#ifndef RSP_H
#define RSP_H

#include <stdint.h>

#include "N64.h"

typedef struct
{
	uint32_t PC[18], PCi, busy, halt, close, uc_start, uc_dstart, cmd, nextCmd;
	int32_t count;
	bool bLLE;
	char romname[21];
} RSPInfo;

extern RSPInfo __RSP;

extern uint32_t DepthClearColor;

#define RSP_SegmentToPhysical( segaddr ) ((gSP.segment[(segaddr >> 24) & 0x0F] + (segaddr & RDRAMSize)) & RDRAMSize)

void RSP_Init(void);
void RSP_ProcessDList(void);
void RSP_LoadMatrix( float mtx[4][4], uint32_t address );
void RSP_CheckDLCounter(void);

#endif
