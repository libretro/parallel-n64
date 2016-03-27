#ifndef RDP_H
#define RDP_H

#define MAXCMD 0x100000
const unsigned int maxCMDMask = MAXCMD - 1;

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
void RDP_ProcessRDPList(void);
void RDP_RepeatLastLoadBlock();
void RDP_SetScissor(uint32_t w0, uint32_t w1);

#endif

