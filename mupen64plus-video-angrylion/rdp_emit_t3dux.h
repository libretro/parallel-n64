#ifndef RDP_EMIT_T3DUX_H
#define RDP_EMIT_T3DUX_H

#include "rdp_emit_frontend.h"
#include "rdp_emit_f3dex2.h"

#ifdef __cplusplus
extern "C" {
#endif

void t3dux_set_rdram(unsigned char *rdram);
void t3dux_set_rdram_size(unsigned int size);

/* Detect the T3DUX (Turbo3D UX) microcode by the "T3DUX" tag in the data
 * segment (passed as a physical base). Returns 1 on a match. */
int t3dux_ucode_match(const unsigned char *rdram, unsigned int rdram_size,
                      unsigned int data_seg);

/* Walk a T3DUX object-list display list, emitting RDP commands to the FIFO. */
void t3dux_run_dl(GSPState *gsp, RdpFifo *fifo, unsigned int dl_addr);

#ifdef __cplusplus
}
#endif

#endif
