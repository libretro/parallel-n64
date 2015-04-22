#ifndef _GLN64_RDP_H
#define _GLN64_RDP_H

#include "Types.h"

#ifndef MAXCMD
#define MAXCMD 0x100000
#endif

#ifndef maxCMDMask
#define maxCMDMask (MAXCMD - 1)
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
	u32 w2, w3;
	u32 cmd_ptr;
	u32 cmd_cur;
	u32 cmd_data[MAXCMD + 32];
} RDPInfo;

extern RDPInfo __RDP;

void RDP_Init(void);
void RDP_Half_1(u32 _c);
void RDP_ProcessRDPList();
void RDP_RepeatLastLoadBlock(void);
void RDP_SetScissor(u32 w0, u32 w1);

#ifdef __cplusplus
}
#endif

#endif

