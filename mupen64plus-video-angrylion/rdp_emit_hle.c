/* rdp_emit_hle.c -- activation glue for the angrylion HLE-graphics path.
 *
 * Called from angrylionProcessDList(). The core wires ProcessDList only when
 * the RSP plugin is HLE -- under cxd4/parallel the RDP is fed directly via
 * ProcessRDPList and this is never reached -- so there is no added cost to
 * the low-level configurations and no separate enable flag is needed.
 *
 * It reads the OSTask display-list pointer from DMEM, walks the display list
 * through the C89 geometry frontend (emitting RDP triangle commands into a
 * FIFO in RDRAM), then points the RDP at that FIFO and rasterizes it.
 *
 * Build check:
 *   gcc -std=c89 -pedantic -Wall -Wdeclaration-after-statement -Werror
 */

#include "rdp_emit_hle.h"
#include "rdp_emit_f3dex2.h"

/* angrylion's RDRAM/DMEM/DPC accessors (interface.c) */
extern unsigned char *plugin_get_rdram(void);
extern unsigned int   plugin_get_rdram_size(void);
extern unsigned char *plugin_get_dmem(void);
extern unsigned int **plugin_get_dp_registers(void);
extern void           n64video_process_list(void);

#define TASK_DATA_PTR_DMEM 0xff0u   /* OSTask data ptr, DMEM byte offset */

/* word-indexed DPC registers, matching plugin_get_dp_registers() ordering */
#define DPC_START   0
#define DPC_END     1
#define DPC_CURRENT 2

static GSPState s_gsp;
static RdpFifo  s_fifo;
static int      s_inited = 0;

/* DMEM/RDRAM 32-bit words are host-native in this core (the RSP's u32()
 * accessor reads them directly); read native, not big-endian. */
static unsigned int read_dmem_u32(const unsigned char *dmem, unsigned int ofs)
{
    return *(const unsigned int *)(dmem + ofs);
}

void rdp_emit_hle_process_dlist(void)
{
    unsigned char *rdram;
    unsigned char *dmem;
    unsigned int   rdram_size;
    unsigned int   dl_addr;
    unsigned int   fifo_base;
    unsigned int **dp;

    rdram      = plugin_get_rdram();
    dmem       = plugin_get_dmem();
    rdram_size = plugin_get_rdram_size();
    if (rdram == 0 || dmem == 0 || rdram_size == 0)
        return;

    if (!s_inited)
    {
        gsp_init(&s_gsp);
        s_inited = 1;
    }

    /* park the RDP command FIFO in the top 256 KiB of RDRAM */
    if (rdram_size < (512u * 1024u))
        return;
    fifo_base = rdram_size - (256u * 1024u);
    rdp_fifo_init(&s_fifo, rdram, fifo_base, 256u * 1024u);

    dl_addr = read_dmem_u32(dmem, TASK_DATA_PTR_DMEM) & 0x00ffffffu;
    if (dl_addr == 0 || dl_addr >= rdram_size)
        return;

    /* walk the display list, emitting RDP commands into the FIFO. textured/
     * z_buffered default off here; a gDP/state-translation follow-up sets the
     * render mode and the per-frame setup commands. */
    f3dex2_seg_reset();
    f3dex2_set_rdram_size(rdram_size);
    f3dex2_run_dl(&s_gsp, &s_fifo, dl_addr, 0, 0);

    /* terminate the command list with exactly one SYNC_FULL (RDP cmd 0x29).
     * angrylion needs this to flush/complete the frame; the dispatcher drops
     * the display list's own trailing G_RDPFULLSYNC (see rdp_emit_f3dex2.c) so
     * that the frame is completed once, not twice. */
    {
        int32_t sync[2];
        sync[0] = (int32_t)0x29000000u;
        sync[1] = 0;
        rdp_fifo_append(&s_fifo, sync, 2);
    }

    if (s_fifo.used == 0)
        return;

    dp = plugin_get_dp_registers();
    if (dp == 0)
        return;
    *dp[DPC_START]   = (unsigned int)fifo_base;
    *dp[DPC_CURRENT] = (unsigned int)fifo_base;
    *dp[DPC_END]     = (unsigned int)(fifo_base + s_fifo.used);

    n64video_process_list();
}
