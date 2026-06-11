/* rdp_emit_s2dex.h -- S2DEX2 background renderer (G_BG_1CYC / G_BG_COPY),
 * transcribed from the S2DEX2 microcode BG overlay. */
#ifndef RDP_EMIT_S2DEX_H
#define RDP_EMIT_S2DEX_H

#include <stdint.h>
#include "rdp_emit_f3dex2.h"

/* Latched S2DEX object render mode (G_OBJ_RENDERMODE w1 low byte; the
 * microcode stores it at DMEM 0x268 and the BG path reads bit 3,
 * G_OBJRM_BILERP, to reserve a filter guard row per strip). */
void s2dex_set_obj_rendermode(unsigned int w1);

/* Latched RDP scissor (G_SETSCISSOR passthrough; the microcode splits the
 * command into DMEM 0x204/0x208 and the BG path clips the frame to it). */
void s2dex_set_scissor(unsigned int w0, unsigned int w1);

void s2dex_reset(void);

/* G_BG_1CYC (S2DEX2 opcode 0x09). bg_addr is the resolved physical address
 * of the uObjScaleBg structure. Emits the RDP command stream for the
 * background into the fifo exactly as the microcode does. */
void s2dex_bg_1cyc(const unsigned char *rdram, unsigned int rdram_bytes,
                   unsigned int bg_addr, RdpFifo *fifo);

#endif
