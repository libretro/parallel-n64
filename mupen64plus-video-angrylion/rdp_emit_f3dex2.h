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
    unsigned char *storage; /* host backing store for the command words */
    unsigned int   base;    /* virtual RDRAM byte address reported to the
                               DPC registers; never dereferenced into guest
                               memory (the rasterizer's command fetch is
                               redirected to `storage` via
                               n64video_set_hle_cmd_buffer) */
    unsigned int   used;    /* bytes written so far */
    unsigned int   cap;     /* capacity in bytes */
    /* Called when an append would overflow the FIFO: must drain the
     * pending commands (e.g. run the rasterizer over [base, base+used))
     * and reset `used` so the append can proceed. Without a flush hook a
     * full FIFO silently drops commands -- including a frame's final
     * SYNC_FULL, which leaves the game waiting forever on the RDP
     * interrupt (video freezes while audio keeps running). */
    void         (*flush)(struct RdpFifo *f);
} RdpFifo;

void rdp_fifo_init(RdpFifo *f, unsigned char *storage,
                   unsigned int base, unsigned int cap);
void rdp_fifo_append(RdpFifo *f, const int32_t *words, int count);

/* Walk an F3DEX2 display list starting at RDRAM byte address `addr`,
 * transforming geometry through `gsp` and appending RDP commands to `fifo`.
 * `textured`/`z_buffered` select the triangle variant for emitted tris
 * (a fuller implementation derives these from the gDP render state).
 * Recurses into nested display lists up to a small depth. */
/* reset the F3DEX2 segment table; call before each top-level display list */
void f3dex2_seg_reset(void);

/* seed the other-modes mirror from the microcode data defaults */
void f3dex2_set_othermode_init(unsigned int h, unsigned int l);

/* set the RDRAM size so the walker can bound display-list and geometry reads;
 * call before f3dex2_run_dl. 0 means assume the default 8 MiB. */
void f3dex2_set_rdram_size(unsigned int size);
void f3dex2_set_rdram(unsigned char *rdram);
void f3dex2_set_task_ucode(const unsigned char *rdram, unsigned int text);

void f3dex2_run_dl(GSPState *gsp, RdpFifo *fifo, unsigned int addr,
                   int textured, int z_buffered);

#ifdef __cplusplus
}
#endif

#endif /* RDP_EMIT_F3DEX2_H */
