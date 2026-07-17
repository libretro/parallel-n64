#ifndef RDP_EMIT_RS_H
#define RDP_EMIT_RS_H

#include "rdp_emit_frontend.h"

#ifdef __cplusplus
extern "C" {
#endif

int  rs_ucode_match(const unsigned char *rdram, unsigned int rdram_size,
                    unsigned int text);
void rs_set_rdram(unsigned char *rdram);
void rs_set_rdram_size(unsigned int size);
void rs_run_dl(GSPState *gsp, RdpFifo *fifo, unsigned int dl_addr);

#ifdef __cplusplus
}
#endif

#endif /* RDP_EMIT_RS_H */
