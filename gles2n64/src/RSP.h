#ifndef RSP_H
#define RSP_H

#include <stdint.h>
#include <boolean.h>

#include "N64.h"
#include "GBI.h"
//#include "gSP.h"
#include "Types.h"

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
	u32 PC[18], PCi, busy, halt, close, DList, uc_start, uc_dstart, cmd, nextCmd;
	s32 count;
	bool bLLE;
	char romname[21];
   uint32_t w0;
   uint32_t w1;
} RSPInfo;

extern RSPInfo __RSP;

extern u32 DepthClearColor;

#define RSP_SegmentToPhysical( segaddr ) ((gSP.segment[(segaddr >> 24) & 0x0F] + (segaddr & 0x00FFFFFF)) & 0x00FFFFFF)

void RSP_Init(void);
void RSP_ProcessDList(void);
void RSP_LoadMatrix( f32 mtx[4][4], u32 address );
void RSP_CheckDLCounter();

#ifdef __cplusplus
}
#endif

#endif
