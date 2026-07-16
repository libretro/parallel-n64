#ifndef RDP_EMIT_TURBO3D_H
#define RDP_EMIT_TURBO3D_H

#include "rdp_emit_frontend.h"

void turbo3d_set_rdram(unsigned char *rdram);
void turbo3d_set_rdram_size(unsigned int size);

/* Detect the original SGI Turbo3D microcode by the CRC-32 of the first 4 KiB
 * of its text segment (0x2bdcfc8a), matching GLideN64's identification. */
int turbo3d_ucode_match(const unsigned char *rdram, unsigned int rdram_size,
                        unsigned int text_seg);

/* Walk a Turbo3D four-word object-list display list, emitting RDP commands
 * to the FIFO. */
void turbo3d_run_dl(GSPState *gsp, RdpFifo *fifo, unsigned int dl_addr);

#endif /* RDP_EMIT_TURBO3D_H */
