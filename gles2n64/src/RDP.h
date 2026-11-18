#ifndef _GLN64_RDP_H
#define _GLN64_RDP_H

#include <stdint.h>

#include "../../Graphics/RDP/RDP_state.h"

#ifndef maxCMDMask
#define maxCMDMask (MAXCMD - 1)
#endif

#ifdef __cplusplus
extern "C" {
#endif

void RDP_Init(void);
void RDP_Half_1(uint32_t _c);
void RDP_ProcessRDPList();
void RDP_RepeatLastLoadBlock(void);
void RDP_SetScissor(uint32_t w0, uint32_t w1);
/* the coverage a fill rectangle may draw (Graphics/rdp_coverage.h) */
void RDP_FillRectCoverage(int32_t _ulx, int32_t _uly, int32_t _lrx, int32_t _lry);

#ifdef __cplusplus
}
#endif

#endif

