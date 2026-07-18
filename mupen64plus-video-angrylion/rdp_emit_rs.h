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
int  rs_run_dl_streaming(GSPState *gsp, RdpFifo *fifo,
                         unsigned int dl_addr, int resume);
void rs_seed_fog_row(const unsigned char *dmem);

/* Rogue Squadron submits terrain fog through a separate setup task (a Factor 5
 * custom overlay, ucode text word0 0x40065800, OSTask type 2) that the RSP-HLE
 * frontend runs immediately before the graphics task. That setup task's
 * ucode_data pointer is the RDRAM fog block; the graphics task's own DMEM fog
 * row (0x160) is still zero at seed time, so the frontend captures the setup
 * task's ucode_data here and rs_seed_fog_row reads the coefficients from
 * fog_block + 0x160. */
void rs_set_fog_block(unsigned int rdram_addr);

#ifdef __cplusplus
}
#endif

#endif /* RDP_EMIT_RS_H */
