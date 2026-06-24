/* rdp_emit_f3d.h -- F3D (Fast3D, "RSP SW 2.0X") display-list dispatcher for
 * the angrylion HLE path. Walks an F3D command list in RDRAM, routes geometry
 * commands to the shared C89 frontend (rdp_emit_frontend), and appends the
 * resulting RDP commands to the FIFO the angrylion rasterizer consumes.
 *
 * F3D is the original Fast3D microcode (Super Mario 64 and other early
 * titles). Its geometry/DMA commands live in the low opcode range (G_MTX
 * 0x01, G_MOVEMEM 0x03, G_VTX 0x04, G_DL 0x06) and its triangle/state
 * commands in the high range (G_TRI1 0xBF, G_ENDDL 0xB8, ...), unlike
 * F3DEX2. The RDP pass-through opcodes (0xC8..0xFF) are shared, so the
 * decode here mirrors rdp_emit_f3dex2.c's structure with F3D's encodings.
 * ISO C89 / MSVC-compatible.
 */
#ifndef RDP_EMIT_F3D_H
#define RDP_EMIT_F3D_H

#include "rdp_emit_f3dex2.h"   /* GSPState, RdpFifo, rdp_fifo_append */

#ifdef __cplusplus
extern "C" {
#endif

/* True if the task ucode at text (RDRAM byte address of the ucode text) is the
 * plain F3D / Fast3D microcode family rather than F3DEX2/S2DEX2/L3DEX2. The
 * HLE entry uses this to route the display list to f3d_run_dl. */
int  f3d_is_ucode(const unsigned char *rdram, unsigned int rdram_size,
                  unsigned int text);

void f3d_seg_reset(void);
void f3d_set_rdram(unsigned char *rdram);
void f3d_set_rdram_size(unsigned int size);
void f3d_set_othermode_init(unsigned int h, unsigned int l);

/* Returns nonzero if the ucode text at `text` is Doom 64's Fast3D variant
 * (shares SM64's version word but differs in geometry encoding). Call
 * f3d_set_variant() with the result before f3d_run_dl() so the walker selects
 * Doom 64's vertex/triangle decode for that task. */
int  f3d_is_doom64_ucode(const unsigned char *rdram, unsigned int rdram_size,
                         unsigned int text);
int  f3d_is_doom64_line_ucode(const unsigned char *rdram, unsigned int rdram_size,
                              unsigned int text);
/* Bomberman 64's Fast3D binary -- a different microcode from Doom 64's but the
 * same F3DEX (v1) vertex/index encoding, so it drives the same variant decode. */
int  f3d_is_bm64_ucode(const unsigned char *rdram, unsigned int rdram_size,
                       unsigned int text);
int  f3d_is_mk64_ucode(const unsigned char *rdram, unsigned int rdram_size,
                       unsigned int text);
void f3d_set_variant(int doom64);
void f3d_set_line_variant(int line);
int  f3d_is_wr64_ucode(const unsigned char *rdram, unsigned int rdram_size,
                       unsigned int text);
void f3d_set_variant_wr64(int wr64);

/* Walk an F3D display list at RDRAM byte address `addr`, transforming geometry
 * through `gsp` and appending RDP commands to `fifo`. Recurses into nested
 * display lists up to a bounded depth. */
void f3d_run_dl(GSPState *gsp, RdpFifo *fifo, unsigned int addr,
                int textured, int z_buffered);

#ifdef __cplusplus
}
#endif

#endif /* RDP_EMIT_F3D_H */
