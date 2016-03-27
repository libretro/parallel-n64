#ifndef RSP_H
#define RSP_H

#include <stdint.h>
#include <boolean.h>

#include "N64.h"
#include "GBI.h"
//#include "gSP.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RSPMSG_CLOSE            0
#define RSPMSG_UPDATESCREEN     1
#define RSPMSG_PROCESSDLIST     2
#define RSPMSG_CAPTURESCREEN    3
#define RSPMSG_DESTROYTEXTURES  4
#define RSPMSG_INITTEXTURES     5

typedef struct
{
	uint32_t PC[18], PCi, busy, halt, close, DList, uc_start, uc_dstart, cmd, nextCmd;
	int32_t count;
	bool bLLE;
	char romname[21];
   uint32_t w0;
   uint32_t w1;
} RSPInfo;

extern RSPInfo __RSP;

extern uint32_t DepthClearColor;

#define RSP_SegmentToPhysical( segaddr ) ((gSP.segment[(segaddr >> 24) & 0x0F] + (segaddr & 0x00FFFFFF)) & 0x00FFFFFF)

void RSP_Init(void);
void RSP_ProcessDList(void);
void RSP_LoadMatrix( float mtx[4][4], uint32_t address );
void RSP_CheckDLCounter();

#ifdef __cplusplus
}
#endif

#endif
