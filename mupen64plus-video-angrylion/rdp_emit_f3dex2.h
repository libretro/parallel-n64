/* rdp_emit_f3dex2.h -- F3DEX2 display-list dispatcher for the angrylion HLE
 * path. Walks an F3DEX2 command list in RDRAM, routes geometry commands to
 * the C89 frontend, and appends the resulting RDP triangle commands to a
 * command FIFO that the angrylion rasterizer then consumes.
 * ISO C89 / MSVC-compatible. */
#ifndef RDP_EMIT_F3DEX2_H
#define RDP_EMIT_F3DEX2_H

#include "rdp_emit_frontend.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A growable RDP command FIFO living in RDRAM. The dispatcher appends
 * encoded triangle commands here; the caller then points DPC_CURRENT/DPC_END
 * at [base, base+used) and invokes the rasterizer. */
typedef struct RdpFifo
{
    unsigned char *rdram;   /* RDRAM base */
    unsigned int   base;    /* byte offset of the FIFO in RDRAM */
    unsigned int   used;    /* bytes written so far */
    unsigned int   cap;     /* capacity in bytes */
} RdpFifo;

void rdp_fifo_init(RdpFifo *f, unsigned char *rdram,
                   unsigned int base, unsigned int cap);
void rdp_fifo_append(RdpFifo *f, const int32_t *words, int count);

/* Walk an F3DEX2 display list starting at RDRAM byte address `addr`,
 * transforming geometry through `gsp` and appending RDP commands to `fifo`.
 * `textured`/`z_buffered` select the triangle variant for emitted tris
 * (a fuller implementation derives these from the gDP render state).
 * Recurses into nested display lists up to a small depth. */
void f3dex2_run_dl(GSPState *gsp, RdpFifo *fifo, unsigned int addr,
                   int textured, int z_buffered);

#ifdef __cplusplus
}
#endif

#endif /* RDP_EMIT_F3DEX2_H */
