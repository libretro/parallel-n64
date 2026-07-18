/* Factor 5 / LucasArts Naboo-era microcode walker (Star Wars Episode I:
 * Battle for Naboo, Indiana Jones and the Infernal Machine) for the
 * angrylion HLE path.
 *
 * A later revision of the Rogue Squadron engine (see rdp_emit_rs.c): a
 * persistent streaming command server with the same libultra yield
 * protocol, feeding the RDP through a DMEM FIFO (0xe80) spilled to an
 * RDRAM ring driven directly into the DPC registers.  Grammar decoded
 * from the task text (BfN text sum 0x25c16, IJ 0x25c53; dispatch at
 * text 0x2c):
 *
 *   - w0 bits 30-31 select the command class; the handler table lives
 *     in the ucode data segment at +0xb6, halfword-indexed by
 *     (w0 >> 23) & 0x1fe, each entry overlay-id (bits 12-15) :
 *     IMEM offset (bits 0-11); overlays are DMA'd on demand from the
 *     task ucode blob (loader at text 0x15c, current-overlay id at
 *     DMEM 0x143);
 *   - class 3 (0xc0-0xff) passes through to the RDP verbatim
 *     (text 0x98); G_TEXRECT (0xe4) carries its two extra words
 *     inline (text 0xb4);
 *   - the custom class-0 set (0x00-0x1f) is aliased by GBI-numbered
 *     opcodes 0xa9-0xc8 through the same table (0xb8 EndDL = slot
 *     0x0f, 0xba SetOtherModeH = slot 0x11, ...), the same compaction
 *     Rogue Squadron uses;
 *   - triangles reject on the AND of per-vertex outcodes (struct+0x24)
 *     masked 0x7070, the Rogue Squadron outcode model.
 *
 * This walker is intentionally incremental: commands it does not yet
 * implement return NABOO_R_FALLBACK and the caller reruns the slice on
 * the LLE fallback, so titles stay fully playable while the command
 * set is filled in against cxd4 A/B references. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "rdp_emit_f3dex2.h"
#include "rdp_emit_naboo.h"

extern void rdp_fifo_append(RdpFifo *f, const int32_t *words, int count);

static unsigned char *s_rdram;
static unsigned int   s_rdram_size;

void naboo_set_rdram(unsigned char *rdram) { s_rdram = rdram; }
void naboo_set_rdram_size(unsigned int size) { s_rdram_size = size; }

static unsigned int nb_read_u32(unsigned int addr)
{
    unsigned int a = addr & 0x7ffffcu;
    if (!s_rdram || a + 4u > s_rdram_size)
        return 0u;
    return ((unsigned int)s_rdram[(a + 0u) ^ 3u] << 24)
         | ((unsigned int)s_rdram[(a + 1u) ^ 3u] << 16)
         | ((unsigned int)s_rdram[(a + 2u) ^ 3u] << 8)
         |  (unsigned int)s_rdram[(a + 3u) ^ 3u];
}

/* Walk state persists across slices of one task (the server yields and
 * resumes); a fresh task launch resets it. */
static struct {
    unsigned int dl;            /* command cursor (RDRAM) */
    unsigned int active;
} nb;

void naboo_task_reset(unsigned int dl)
{
    nb.dl = dl;
    nb.active = 1;
}

int naboo_run_dl(RdpFifo *fifo, unsigned int dl_addr, int resume)
{
    if (!resume)
        naboo_task_reset(dl_addr);
    if (!nb.active)
        return NABOO_R_FALLBACK;

    for (;;) {
        unsigned int w0 = nb_read_u32(nb.dl);
        unsigned int w1 = nb_read_u32(nb.dl + 4);
        unsigned int cls = w0 >> 30;
        unsigned int op  = (w0 >> 24) & 0xffu;

        if (cls == 3u) {
            /* RDP passthrough (text 0x98).  G_TEXRECT carries two
             * extra inline words. */
            int32_t words[2];
            words[0] = (int32_t)w0;
            words[1] = (int32_t)w1;
            rdp_fifo_append(fifo, words, 2);
            nb.dl += 8;
            if (op == 0xe4u) {
                words[0] = (int32_t)nb_read_u32(nb.dl);
                words[1] = (int32_t)nb_read_u32(nb.dl + 4);
                rdp_fifo_append(fifo, words, 2);
                nb.dl += 8;
            }
            continue;
        }

        /* alias GBI-numbered opcodes onto the custom slot set */
        if (op >= 0xa9u && op <= 0xc8u)
            op -= 0xa9u;

        switch (op) {
        case 0x00:                              /* NOOP */
            nb.dl += 8;
            continue;
        case 0x0f:                              /* EndDL (GBI 0xb8) */
            nb.active = 0;
            return NABOO_R_DONE;
        default:
            /* not yet implemented: rerun this slice on the LLE
             * fallback */
            nb.active = 0;
            return NABOO_R_FALLBACK;
        }
        (void)w1;
    }
}
