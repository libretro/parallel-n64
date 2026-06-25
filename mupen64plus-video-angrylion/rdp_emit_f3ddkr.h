/* rdp_emit_f3ddkr.h -- F3DDKR (Diddy Kong Racing custom microcode) display-
 * list dispatcher for the angrylion HLE path. Walks an F3DDKR command list in
 * RDRAM, routes geometry through the shared C89 frontend (rdp_emit_frontend),
 * and appends the resulting RDP triangle commands to the FIFO the angrylion
 * rasterizer consumes. ISO C89 / MSVC-compatible.
 *
 * F3DDKR ("F3D Diddy Kong Racing") is Rare's bespoke graphics microcode, also
 * used by Jet Force Gemini and (a descendant) by other Rare engines. It is a
 * GBI 1 derivative but with a different command set from F3DEX/F3D:
 *
 *   - G_VTX (0x04) carries the DKR vertex command (gSPVertexDKR): the byte
 *     length is ((n*8 + n) << 1) + 8 and the parameter word packs
 *     ((n-1) << 3) | ((v) & 6) | v0, where the low bit of v0 is the
 *     G_VTX_APPEND flag (append to the vertex buffer instead of resetting it).
 *   - G_TRIN (0x05) is gSPPolygon: a *batched* triangle-list draw. w0 holds
 *     ((numTris-1) << 4 | texEnabled) in bits 16..23 and (numTris*16) in bits
 *     0..15; w1 points at a packed triangle-index array (3 index bytes + 1
 *     flag byte per triangle). F3DEX has no equivalent -- this is DKR's core
 *     geometry primitive.
 *   - G_MTX (0x01) is gSPMatrixDKR: an *indexed* matrix model. The index
 *     (slot 0/1/2) is carried in (i) << 6 of the parameter; gSPSelectMatrixDKR
 *     (G_MOVEWORD G_MW_MVPMATRIX) selects the active MVP slot. This replaces
 *     F3DEX's push/pop stack.
 *   - G_DMADL (0x07) is gDkrDmaDisplayList: a DMA display-list call carrying a
 *     command count rather than a NULL-terminated sublist.
 *   - G_MOVEWORD G_MW_BILLBOARD (0x02) toggles billboarding: while enabled,
 *     gSPVertexDKR vertices are added to vertex 0 after MVP transform but
 *     before the perspective divide (used to draw 3D-anchored sprites).
 *
 * The transform and rasterisation math is identical to the other walkers, so
 * this dispatcher only implements the DKR command decode and the indexed-
 * matrix / billboard state on top of the shared frontend.
 */
#ifndef RDP_EMIT_F3DDKR_H
#define RDP_EMIT_F3DDKR_H

#include "rdp_emit_f3dex2.h"   /* GSPState, RdpFifo, rdp_fifo_append */

#ifdef __cplusplus
extern "C" {
#endif

/* True if the task's data segment (ud = RDRAM byte address of the ucode data
 * segment, uds = its size from the OSTask) is the F3DDKR custom microcode.
 * The HLE entry uses this to route the display list to f3ddkr_run_dl. */
int  f3ddkr_is_ucode(const unsigned char *rdram, unsigned int rdram_size,
                     unsigned int ud, unsigned int uds);

void f3ddkr_seg_reset(void);
void f3ddkr_set_rdram(unsigned char *rdram);
void f3ddkr_set_rdram_size(unsigned int size);

/* Walk an F3DDKR display list at RDRAM byte address `addr`, transforming
 * geometry through `gsp` and appending RDP commands to `fifo`. Recurses into
 * nested (G_DL / G_DMADL) display lists up to a bounded depth. */
void f3ddkr_run_dl(GSPState *gsp, RdpFifo *fifo, unsigned int addr,
                   int textured, int z_buffered);

#ifdef __cplusplus
}
#endif

#endif /* RDP_EMIT_F3DDKR_H */
