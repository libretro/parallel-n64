#ifndef RDP_H
#define RDP_H

#ifdef __cplusplus
extern "C" {
#endif

#define MAXCMD 0x100000
static const unsigned int maxCMDMask = MAXCMD - 1;

typedef struct
{
	u32 w0, w1, w2, w3;
	u32 cmd_ptr;
	u32 cmd_cur;
	u32 cmd_data[MAXCMD + 32];
} RDPInfo;

extern RDPInfo RDP;

void gli_RDP_Init(void);
void gli_RDP_Half_1(u32 _c);
void gli_RDP_TexRect(u32 w0, u32 w1);
void gli_RDP_ProcessRDPList(void);
void gli_RDP_RepeatLastLoadBlock(void);
void gli_RDP_SetScissor(u32 w0, u32 w1);
void gli_RDP_SetTImg(u32 w0, u32 w1);
void gli_RDP_LoadBlock(u32 w0, u32 w1);
void gli_RDP_SetTile(u32 w0, u32 w1);
void gli_RDP_SetTileSize(u32 w0, u32 w1);

#ifdef __cplusplus
}
#endif

#endif

