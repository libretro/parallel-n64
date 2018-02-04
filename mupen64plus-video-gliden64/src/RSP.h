#ifndef RSP_H
#define RSP_H

#include <stdint.h>

#include "../../Graphics/RSP/RSP_state.h"

#include "N64.h"

extern uint32_t DepthClearColor;

#define RSP_SegmentToPhysical( segaddr ) ((gSP.segment[(segaddr >> 24) & 0x0F] + (segaddr & RDRAMSize)) & RDRAMSize)

void RSP_Init(void);
void RSP_ProcessDList(void);
void RSP_LoadMatrix( float mtx[4][4], uint32_t address );
void RSP_CheckDLCounter(void);

#endif
