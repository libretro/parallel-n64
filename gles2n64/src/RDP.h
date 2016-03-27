#ifndef _GLN64_RDP_H
#define _GLN64_RDP_H

#include <stdint.h>

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
	uint32_t w2, w3;
	uint32_t cmd_ptr;
	uint32_t cmd_cur;
	uint32_t cmd_data[MAXCMD + 32];
} RDPInfo;

extern RDPInfo __RDP;

void RDP_Init(void);
void RDP_Half_1(uint32_t _c);
void RDP_ProcessRDPList();
void RDP_RepeatLastLoadBlock(void);
void RDP_SetScissor(uint32_t w0, uint32_t w1);

#ifdef __cplusplus
}
#endif

#endif

