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

#include <stdio.h>
#include <stdlib.h>
#include "rdp_emit_hle.h"
#include "rdp_emit_f3dex2.h"
#include "rdp_emit_rsp.h"
#include "rdp_emit_backend.h"

/* The active rasterizer backend (angrylion or parallel-rdp).  Installed via
 * rdp_emit_set_backend() at plugin connect time.  The display-list walk,
 * microcode detection and command encoding below are identical for either;
 * only RDRAM/DMEM access and the finished-FIFO submit go through this. */
static const RdpEmitBackend *s_backend = 0;

void rdp_emit_set_backend(const RdpEmitBackend *be)
{
    if (be != 0)
        s_backend = be;
}

#define TASK_DATA_PTR_DMEM 0xff0u   /* OSTask data ptr, DMEM byte offset */

static GSPState s_gsp;
static RdpFifo  s_fifo;
static int      s_inited = 0;

/* Host backing store for the synthesized RDP command FIFO. Kept out of
 * guest RDRAM: Expansion Pak games (Majora's Mask) use all 8 MiB, and
 * parking the FIFO in the top 256 KiB corrupted their heaps, ending in
 * the game's own Fault_AddHungupAndCrash. The DPC registers are pointed
 * at a virtual address range whose command fetch is redirected here via
 * n64video_set_hle_cmd_buffer; guest memory is never written. */
#define HLE_FIFO_CAP (256u * 1024u)
static unsigned char s_fifo_storage[HLE_FIFO_CAP];

/* DMEM/RDRAM 32-bit words are host-native in this core (the RSP's u32()
 * accessor reads them directly); read native, not big-endian. */
static unsigned int read_dmem_u32(const unsigned char *dmem, unsigned int ofs)
{
    return *(const unsigned int *)(dmem + ofs);
}

/* Drain the FIFO through the rasterizer and reset it. Used both as the
 * mid-frame overflow flush (no SYNC_FULL involved; angrylion just executes
 * the batch) and by the activation below for the final batch. */
static void fifo_flush_to_rdp(RdpFifo *f)
{
    if (f->used == 0)
        return;
    if (s_backend == 0 || s_backend->submit == 0)
    {
        f->used = 0;
        return;
    }
    /* The backend points its DPC registers at [base, base+used), fetches the
     * command words from f->storage (guest RDRAM at `base` is never written),
     * runs its RDP, and restores guest-RDRAM command fetch on return. */
    s_backend->submit(f->storage, f->base, f->used);
    f->used = 0;
}


static int gsp_params_at_task_start;

/* Re-evaluate every microcode-build-specific parameter from a (data, text)
 * segment pair: the triangle-setup scale constants, the clip profile, the
 * near-plane mode, the clip-lerp build, the clip-fan orientation and the
 * G_BRANCH_Z/W flavor. Called at task start and again at every mid-list
 * G_LOAD_UCODE, since titles like Excitebike 64 swap between microcode
 * builds with different schemes inside one display list. */
void gsp_detect_ucode_params(GSPState *st, const unsigned char *rdram,
                             unsigned int rdram_size,
                             unsigned int ud, unsigned int ut)
{
    int ut_is_task_start = gsp_params_at_task_start;
    /* The data address at a mid-list G_LOAD_UCODE comes from the staged
     * G_RDPHALF_1 word, which display lists also use for branch targets;
     * validate that it really points at an F3DEX2-family data segment
     * before trusting any of its fields. Every known build carries the
     * v31 constant row prefix ffff 0004 0008 7f00 at data + 0x1b0. */
    if (ud != 0 && ud + 0x1c0 <= rdram_size)
    {
        if (!(rdram[(ud + 0x1b0) ^ 3] == 0xffu
              && rdram[(ud + 0x1b1) ^ 3] == 0xffu
              && rdram[(ud + 0x1b2) ^ 3] == 0x00u
              && rdram[(ud + 0x1b3) ^ 3] == 0x04u
              && rdram[(ud + 0x1b4) ^ 3] == 0x00u
              && rdram[(ud + 0x1b5) ^ 3] == 0x08u
              && rdram[(ud + 0x1b6) ^ 3] == 0x7fu
              && rdram[(ud + 0x1b7) ^ 3] == 0x00u))
            ud = 0;
    }
    else
        ud = 0;
    /* Triangle-setup scale constants live in a microcode constants vector
     * loaded from the ucode data segment. Their location and lane layout
     * changed between revisions:
     *   F3DEX2/F3DZEX2 2.08: vector at data+0x1C0; dX scale lane 2
     *     (0x1000), VCR crimp bound lane 3 (0x0100), anchor fraction
     *     mask lane 5 (0xFFF8), inverse-dY scale lane 7 (0x0020).
     *   F3DEX2/F3DZEX2 2.0xH (e.g. 2.04H/2.06H): vector at data+0x1B0;
     *     fraction mask lane 0 (0xFFFF), inverse-dY lane 2 (0x0008),
     *     dX scale lane 5 (0x4000); the VCR bound (0x01CC) sits in the
     *     following vector's lane 2 (data+0x1C4).
     * The dX and inverse-dY scales split one 17-bit fixed-point factor, so
     * dxs * idys == 0x20000 in every revision; use that to validate which
     * layout the shipped data segment actually uses instead of trusting
     * offsets blindly (reading the 2.08 slots on a 2.0xH segment returns
     * unrelated constants and mis-sets every triangle edge). If neither
     * layout validates, keep the 2.0xH defaults. */
    {
        /* ud from caller */
        if (ud != 0 && ud + 0x1d0 <= rdram_size)
        {
            int32_t dxs  = (int32_t)((rdram[(ud + 0x1c4) ^ 3] << 8)
                                   |  rdram[(ud + 0x1c5) ^ 3]);
            int32_t idys = (int32_t)((rdram[(ud + 0x1ce) ^ 3] << 8)
                                   |  rdram[(ud + 0x1cf) ^ 3]);
            int32_t fmsk = (int32_t)((rdram[(ud + 0x1ca) ^ 3] << 8)
                                   |  rdram[(ud + 0x1cb) ^ 3]);
            int32_t vcrb = (int32_t)((rdram[(ud + 0x1c6) ^ 3] << 8)
                                   |  rdram[(ud + 0x1c7) ^ 3]);
            if (!(dxs > 0 && idys > 0 && (int64_t)dxs * idys == 0x20000))
            {
                dxs  = (int32_t)((rdram[(ud + 0x1ba) ^ 3] << 8)
                               |  rdram[(ud + 0x1bb) ^ 3]);
                idys = (int32_t)((rdram[(ud + 0x1b4) ^ 3] << 8)
                               |  rdram[(ud + 0x1b5) ^ 3]);
                fmsk = (int32_t)((rdram[(ud + 0x1b0) ^ 3] << 8)
                               |  rdram[(ud + 0x1b1) ^ 3]);
                vcrb = (int32_t)((rdram[(ud + 0x1c4) ^ 3] << 8)
                               |  rdram[(ud + 0x1c5) ^ 3]);
            }
            if (dxs > 0 && idys > 0 && (int64_t)dxs * idys == 0x20000)
                gsp_set_tri_scales(st, dxs, idys, fmsk, vcrb);

        }
    }


    /* Seed the other-modes mirror from the microcode data defaults
     * (pair at ucode_data + 0xc8 -> DMEM 0xc8): the baseline the partial
     * 0xE2/0xE3 writes merge into for games that never send a wholesale
     * G_RDPSETOTHERMODE. Applied after the per-task reset. */
    {
        /* ud from caller */
        if (ud != 0 && ud + 0xd0 <= rdram_size)
        {
            unsigned int oh = ((unsigned int)rdram[(ud + 0xc8) ^ 3] << 24)
                            | ((unsigned int)rdram[(ud + 0xc9) ^ 3] << 16)
                            | ((unsigned int)rdram[(ud + 0xca) ^ 3] << 8)
                            |  (unsigned int)rdram[(ud + 0xcb) ^ 3];
            unsigned int ol = ((unsigned int)rdram[(ud + 0xcc) ^ 3] << 24)
                            | ((unsigned int)rdram[(ud + 0xcd) ^ 3] << 16)
                            | ((unsigned int)rdram[(ud + 0xce) ^ 3] << 8)
                            |  (unsigned int)rdram[(ud + 0xcf) ^ 3];
            /* The microcode's own gSPLoadUcode path preserves the
             * other-modes state across the swap (Kirby 64's S2DEX
             * round-trips validate pixel-exact only without a mid-list
             * re-seed), so the data-segment defaults only apply at task
             * start. */
            if (ut_is_task_start && (oh >> 24) == 0xefu)
                f3dex2_set_othermode_init(oh, ol);

            /* near clip-plane row (clip-ratio table at data + 0x180, row 5
             * = bytes +0x1a8..0x1af): NoN microcodes store {0,0,0,1} (the
             * near plane is just w), standard near clipping {0,0,1,1}
             * (z + w). Adopt the z coefficient when the row validates. */
            if (ud + 0x1b0 <= rdram_size)
            {
                int16_t nz = (int16_t)((rdram[(ud + 0x1ac) ^ 3] << 8)
                                      | rdram[(ud + 0x1ad) ^ 3]);
                int16_t nw = (int16_t)((rdram[(ud + 0x1ae) ^ 3] << 8)
                                      | rdram[(ud + 0x1af) ^ 3]);
                if (nw == 1 && (nz == 0 || nz == 1))
                    st->clip_near_z = (int)nz;
            }
        }
    }

    /* Clip-lerp build probe: F3DEX2 2.05+ and F3DZEX2 guard the boundary
     * lerp's sign extraction with a vor 1 (opcode word 4b015f6a in every
     * such build's clip overlay); the 2.04H build feeds the raw sum to
     * vabs. Scan the microcode text image, overlays included, for the
     * instruction and pick the build accordingly. */
    {
        /* ut from caller */
        int found = 0;
        if (ut != 0 && ut + 0x1800 <= rdram_size)
        {
            unsigned int k;
            for (k = 0; k + 4 <= 0x1800; k += 4)
            {
                if (rdram[(ut + k + 0) ^ 3] == 0x4bu
                    && rdram[(ut + k + 1) ^ 3] == 0x01u
                    && rdram[(ut + k + 2) ^ 3] == 0x5fu
                    && rdram[(ut + k + 3) ^ 3] == 0x6au)
                {
                    found = 1;
                    break;
                }
            }
        }
        rsp_set_clip_lerp_204h(!found);
    }

    /* Rejection-microcode probe: F3DLX.Rej / F3DZEX.Rej carry ".Rej" in
     * the microcode ID string. The string sits in the shared text/data
     * region the ucode_data DMA brings in, at data + 0x146 (the
     * displayListStack/ID area, 0x138-0x180 in the microcode's DMEM map;
     * the v31 constant row this detector already keys on is at data +
     * 0x1b0 in the same segment). These builds have no polygon clipper:
     * a triangle past a fixed rejection bound is dropped whole rather
     * than subdivided, so the emitter must not run guard-band clipping
     * for them. Excitebike 64's title runs its 3D model under F3DLX.Rej
     * while its 2D/HUD passes use F3DEX.NoN. */
    {
        int rej = 0;
        if (ud != 0 && ud + 0x160 <= rdram_size)
        {
            unsigned int k;
            for (k = 0x130; k + 4 <= 0x160; k++)
            {
                if (rdram[(ud + k + 0) ^ 3] == 0x2eu    /* . */
                    && rdram[(ud + k + 1) ^ 3] == 0x52u /* R */
                    && rdram[(ud + k + 2) ^ 3] == 0x65u /* e */
                    && rdram[(ud + k + 3) ^ 3] == 0x6au)/* j */
                {
                    rej = 1;
                    break;
                }
            }
        }
        st->clip_reject = rej;
    }

    /* Clip-fan probe: the 2.04H clipper triangulates its output polygon
     * from the FIRST vertex with descending pairs (its draw loop reads
     * the pair through the output cursor: lhu v0, 0x3cc(s5) = 96a203cc),
     * where 2.05+/F3DZEX2 fans ascending pairs from the LAST vertex
     * (lhu v0, 0x3cc(s2)). Scan the text image for the 2.04H form. */
    {
        /* ut from caller */
        int first = 0;
        if (ut != 0 && ut + 0x1800 <= rdram_size)
        {
            unsigned int k;
            for (k = 0; k + 4 <= 0x1800; k += 4)
            {
                if (rdram[(ut + k + 0) ^ 3] == 0x96u
                    && rdram[(ut + k + 1) ^ 3] == 0xa2u
                    && rdram[(ut + k + 2) ^ 3] == 0x03u
                    && rdram[(ut + k + 3) ^ 3] == 0xccu)
                {
                    first = 1;
                    break;
                }
            }
        }
        st->clip_fan_first = first;
    }

    /* G_BRANCH_Z/W probe: opcode 0x04 compares a vertex field against the
     * command word and branches to the staged RDPHALF_1 list when it is
     * smaller. F3DEX2 builds (e.g. 2.04H) load the 32-bit screen-Z word
     * at vertex+0x1c (lhu rX, 0x380(rX); lw rY, 0x1c(rX)); F3DZEX2
     * builds load the s16 clip-W integer at vertex+0x6 (lh rY, 0x6(rX)).
     * Scan the text for the table-lookup/load pair, masking the register
     * fields. */
    {
        /* ut from caller */
        int z_mode = 0;
        if (ut != 0 && ut + 0x1800 <= rdram_size)
        {
            unsigned int k;
            for (k = 0; k + 8 <= 0x1800; k += 4)
            {
                unsigned int i0 = ((unsigned int)rdram[(ut + k + 0) ^ 3] << 24)
                                | ((unsigned int)rdram[(ut + k + 1) ^ 3] << 16)
                                | ((unsigned int)rdram[(ut + k + 2) ^ 3] << 8)
                                |  (unsigned int)rdram[(ut + k + 3) ^ 3];
                unsigned int i1 = ((unsigned int)rdram[(ut + k + 4) ^ 3] << 24)
                                | ((unsigned int)rdram[(ut + k + 5) ^ 3] << 16)
                                | ((unsigned int)rdram[(ut + k + 6) ^ 3] << 8)
                                |  (unsigned int)rdram[(ut + k + 7) ^ 3];
                if ((i0 & 0xfc00ffffu) == 0x94000380u)
                {
                    if ((i1 & 0xfc00ffffu) == 0x8c00001cu)
                    {
                        z_mode = 1;
                        break;
                    }
                    if ((i1 & 0xfc00ffffu) == 0x84000006u)
                    {
                        z_mode = 0;
                        break;
                    }
                }
            }
        }
        st->branch_z_mode = z_mode;
    }
}

void rdp_emit_hle_process_dlist(void)
{
    unsigned char *rdram;
    unsigned char *dmem;
    unsigned int   rdram_size;
    unsigned int   dl_addr;
    unsigned int   fifo_base;

    if (s_backend == 0 || s_backend->get_rdram == 0
        || s_backend->get_dmem == 0 || s_backend->get_rdram_size == 0)
        return;

    rdram      = s_backend->get_rdram();
    dmem       = s_backend->get_dmem();
    rdram_size = s_backend->get_rdram_size();
    if (rdram == 0 || dmem == 0 || rdram_size == 0)
        return;


    if (!s_inited)
    {
        gsp_init(&s_gsp);
        s_inited = 1;
    }
    gsp_task_reset(&s_gsp);

    /* the FIFO lives in host memory; the top 256 KiB of RDRAM is used
     * only as the virtual address range the DPC registers report */
    if (rdram_size < (512u * 1024u))
        return;
    fifo_base = rdram_size - HLE_FIFO_CAP;
    rdp_fifo_init(&s_fifo, s_fifo_storage, fifo_base, HLE_FIFO_CAP);
    s_fifo.flush = fifo_flush_to_rdp;

    dl_addr = read_dmem_u32(dmem, TASK_DATA_PTR_DMEM) & 0x00ffffffu;
    if (dl_addr == 0 || dl_addr >= rdram_size)
        return;

    /* walk the display list, emitting RDP commands into the FIFO. textured/
     * z_buffered default off here; a gDP/state-translation follow-up sets the
     * render mode and the per-frame setup commands. */
    f3dex2_seg_reset();
    gsp_params_at_task_start = 1;
    gsp_detect_ucode_params(&s_gsp, rdram, rdram_size,
                            read_dmem_u32(dmem, 0xfd8) & 0x00ffffffu,
                            read_dmem_u32(dmem, 0xfd0) & 0x00ffffffu);
    gsp_params_at_task_start = 0;
    f3dex2_set_rdram(rdram);
    f3dex2_set_rdram_size(rdram_size);
    f3dex2_set_task_ucode(rdram, read_dmem_u32(dmem, 0xfd0) & 0x00ffffffu);
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

    /* submit the final batch (everything since the last overflow flush,
     * or the whole frame when no flush happened). */
    fifo_flush_to_rdp(&s_fifo);

}
