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
#include "rdp_emit_f3d.h"
#include "rdp_emit_f3ddkr.h"
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

/* F3DEX (v1) family classifier: 0 = not this family, 1 = F3D vertex family,
 * 2 = L3D line family. Defined below; forward-declared so the per-task setup
 * can route and configure the L3DEX line ucode the same way as the dispatcher. */
static int f3d_ucode_family(const unsigned char *rdram, unsigned int rdram_size,
                            unsigned int ud, unsigned int uds);

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
        /* Plain Fast3D (Super Mario 64 et al.) is neither F3DEX2 2.05+ nor
         * the 2.04H build: its clip overlay uses the standard reciprocal
         * sign step, not the 2.04H vabs-of-zero quirk. The probe above only
         * distinguishes 2.05+ (guard instruction present) from "everything
         * else", which would wrongly hand Fast3D the 2.04H behaviour and
         * collapse the boundary-vertex fade to 0 wherever an off vertex
         * lands exactly on a clip plane (cr.off == 0) -- e.g. the SM64
         * title's screen-edge background tiles, which then lose their
         * texture and render gray. Force the standard lerp for Fast3D. The
         * L3DEX line ucode is Fast3D-family too but lacks the version word, so
         * fold in the data-segment family check (uds unknown here, the scanner
         * defaults its bound); the "fifo" guard inside it keeps F3DEX2 builds,
         * which reach this through their own path, classified as non-family. */
        if (f3d_is_ucode(rdram, rdram_size, ut)
            || f3d_ucode_family(rdram, rdram_size, ud, 0u) != 0)
            found = 1;
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

    /* F3DFLX.Rej ("FLX" rejection build, used by F-Zero X's in-race racer
     * models) does not implement the lookat texture-coordinate generator: it
     * leaves the vertex-supplied S/T in place even while G_TEXTURE_GEN is set
     * in the geometry mode. Detect the build by name so the emitter honors the
     * explicit per-vertex texcoords instead of running standard texgen. */
    {
        int flx = 0;
        if (ud != 0 && ud + 0x160 <= rdram_size)
        {
            unsigned int k;
            for (k = 0x100; k + 6 <= 0x160; k++)
            {
                if (rdram[(ud + k + 0) ^ 3] == 0x46u    /* F */
                    && rdram[(ud + k + 1) ^ 3] == 0x33u /* 3 */
                    && rdram[(ud + k + 2) ^ 3] == 0x44u /* D */
                    && rdram[(ud + k + 3) ^ 3] == 0x46u /* F */
                    && rdram[(ud + k + 4) ^ 3] == 0x4cu /* L */
                    && rdram[(ud + k + 5) ^ 3] == 0x58u)/* X */
                {
                    flx = 1;
                    break;
                }
            }
        }
        st->no_texgen = flx;
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

/* Recognise the F3DEX (v1) graphics-microcode family generically, by the
 * human-readable signature SGI embeds in every Gfx microcode's data segment:
 * "RSP Gfx ucode F3DEX ..." and its siblings (F3DLX/F3DLP/F3DEX.NoN), plus the
 * line variant "RSP Gfx ucode L3DEX ...". The task's OSTask carries the data-
 * segment pointer and size at DMEM 0xfd8/0xfdc; the string sits a few hundred
 * bytes in, so a single bounded scan over the (~2 KiB) data segment classifies
 * the microcode without a per-game text-CRC allowlist.
 *
 * Returns 0 for anything else (the plain Fast3D builds in Super Mario 64 and
 * Wave Race 64 carry an "RSP SW Version" string and never match, keeping their
 * divide-by-ten / divide-by-five decode; S2DEX/ZSort and friends fall through
 * too), 1 for the vertex family ("F3D" stem) and 2 for the line family ("L3D"
 * stem, gspL3DEX). Both 1 and 2 use the v1 (n<<10)|(16n-1) vertex count and the
 * halved triangle indices; 2 additionally draws G_LINE3D as a real two-vertex
 * line, so the automap that L3DEX renders needs both the variant decode and the
 * line expansion. F3DEX2 (GBI 2) is routed to its own walker before this runs.
 * One short scan replaces (and costs less than) the chain of full-text CRCs it
 * supersedes. */
static int f3d_ucode_family(const unsigned char *rdram,
                            unsigned int rdram_size,
                            unsigned int ud, unsigned int uds)
{
    static const char pre[] = "RSP Gfx ucode ";
    unsigned int plen = sizeof(pre) - 1u;
    unsigned int hi, b;
    if (rdram == 0 || ud == 0)
        return 0;
    if (uds == 0 || uds > 0x4000u)
        uds = 0x1000u;          /* sane bound if the size field is absent/odd */
    hi = ud + uds;
    if (hi > rdram_size)
        hi = rdram_size;
    if (hi < ud + plen + 3u)
        return 0;
    for (b = ud; b + plen + 3u <= hi; b++)
    {
        unsigned int k;
        unsigned char c0, c1, c2;
        for (k = 0; k < plen; k++)
            if (rdram[(b + k) ^ 3] != (unsigned char)pre[k])
                break;
        if (k != plen)
            continue;
        c0 = rdram[(b + plen + 0u) ^ 3];
        c1 = rdram[(b + plen + 1u) ^ 3];
        c2 = rdram[(b + plen + 2u) ^ 3];
        if (!((c0 == 'F' || c0 == 'L') && c1 == '3' && c2 == 'D'))
            return 0;           /* S2DEX, ZSortp, etc. -- not this family */
        /* L3DEX and F3DEX each exist in a GBI 1 build ("... 1.21") and a GBI 2
         * build ("... fifo 2.xx"); only GBI 1 belongs on the F3D walker. The
         * stem width varies (F3DEX, F3DEX.NoN, L3DEX), so scan the short name
         * field that follows it for the "fifo" token that tags every GBI 2
         * build and reject those. */
        {
            unsigned int j, lim = b + plen + 28u;
            if (lim > hi) lim = hi;
            for (j = b + plen + 3u; j + 4u <= lim; j++)
                if (rdram[(j + 0u) ^ 3] == 'f' && rdram[(j + 1u) ^ 3] == 'i' &&
                    rdram[(j + 2u) ^ 3] == 'f' && rdram[(j + 3u) ^ 3] == 'o')
                    return 0;
        }
        return (c0 == 'L') ? 2 : 1;
    }
    return 0;
}

/* Recognise standalone S2DEX (GBI 1) by the SGI data-segment signature
 * "RSP Gfx ucode S2DEX  1.xx ...". Yoshi's Story, Bangai-O and other
 * sprite-driven GBI 1 titles carry a build-specific text version word
 * (e.g. 0x4a00002c) that probe_ucode_class does not match, so without this
 * they fall through to the F3DEX2 walker and render nothing.
 *
 * The match keys on the task's *primary* (first) "RSP Gfx ucode " signature,
 * not on any S2DEX string anywhere in the data segment. The combined SDK
 * ucode objects shipped by a large class of F3DEX2 titles -- Dr. Mario 64,
 * AI Shougi 3, GT64, Carmageddon 64, Big Mountain 2000, 64 Oozumou 2, Densha
 * de Go!, Doraemon 3, ... -- keep a resident "RSP Gfx ucode S2DEX  1.07 ..."
 * author string deeper in the *same* data segment as their F3DEX2 microcode.
 * A scan that returned on the first S2DEX hit anywhere therefore misrouted
 * every one of those F3DEX2 tasks to the sprite decoder and rendered black.
 * The task's own microcode is named by the first signature in its segment, so
 * we require that first signature to be S2DEX (and GBI 1: not "S2DEX2", and
 * with no "fifo"/"xbus" GBI 2 token, which the c81f2018 text probe already
 * routes as UCODE_S2DEX2). */
static int s2dex1_ucode_match(const unsigned char *rdram,
                              unsigned int rdram_size,
                              unsigned int ud, unsigned int uds)
{
    static const char pre[] = "RSP Gfx ucode ";
    unsigned int plen = sizeof(pre) - 1u;   /* 14 */
    unsigned int hi, b;
    if (rdram == 0 || ud == 0)
        return 0;
    if (uds == 0 || uds > 0x4000u)
        uds = 0x1000u;
    hi = ud + uds;
    if (hi > rdram_size)
        hi = rdram_size;
    if (hi < ud + plen + 6u)
        return 0;
    for (b = ud; b + plen + 6u <= hi; b++)
    {
        unsigned int k;
        for (k = 0; k < plen; k++)
            if (rdram[(b + k) ^ 3] != (unsigned char)pre[k])
                break;
        if (k != plen)
            continue;
        /* First signature located: it identifies the task's own microcode. */
        if (rdram[(b + plen + 0u) ^ 3] != 'S' ||
            rdram[(b + plen + 1u) ^ 3] != '2' ||
            rdram[(b + plen + 2u) ^ 3] != 'D' ||
            rdram[(b + plen + 3u) ^ 3] != 'E' ||
            rdram[(b + plen + 4u) ^ 3] != 'X')
            return 0;           /* primary ucode is F3DEX/L3DEX/etc., not S2DEX */
        if (rdram[(b + plen + 5u) ^ 3] == '2')
            return 0;           /* "S2DEX2" -> GBI 2, routed by the text probe */
        /* GBI 2 S2DEX is "S2DEX       fifo 2.0x" / "... xbus 2.0x"; the GBI 1
         * build carries a bare "1.xx" version with no fifo/xbus token. Reject
         * the GBI 2 form so only the genuine S2DEX 1.xx sprite ucode matches. */
        {
            unsigned int j, lim = b + plen + 28u;
            if (lim > hi) lim = hi;
            for (j = b + plen + 5u; j + 4u <= lim; j++)
            {
                if (rdram[(j + 0u) ^ 3] == 'f' && rdram[(j + 1u) ^ 3] == 'i' &&
                    rdram[(j + 2u) ^ 3] == 'f' && rdram[(j + 3u) ^ 3] == 'o')
                    return 0;
                if (rdram[(j + 0u) ^ 3] == 'x' && rdram[(j + 1u) ^ 3] == 'b' &&
                    rdram[(j + 2u) ^ 3] == 'u' && rdram[(j + 3u) ^ 3] == 's')
                    return 0;
            }
        }
        return 1;
    }
    return 0;
}

/* Recognise the F3DEX (v1, GBI 1) family from the *structure* of its data
 * segment, with no dependence on the "RSP Gfx ucode ..." signature string.
 * The Rare engine titles (Banjo-Kazooie/Tooie, Diddy Kong Racing, Donkey
 * Kong 64, Jet Force Gemini) ship the stock gspF3DEX.fifo microcode with that
 * author string zeroed out, so f3d_ucode_family()'s name scan misses them and
 * the dispatcher leaves the F3D walker in its plain-Fast3D decode (gSPVertex
 * count ((w0>>20)&0xf)+1, gSP1Triangle indices /10). F3DEX v1 is GBI 1 and
 * needs the n<<10 / index*2 decode instead, so without recognising the family
 * their geometry collapses.
 *
 * These markers were read out of the public SDK gspF3DEX.fifo object (.data,
 * SP_UCODE_DATA_SIZE = 0x800) and hold across the whole v1 vertex family
 * (F3DEX/F3DLX, NoN and base):
 *   - data+0x1b0 is NOT the F3DEX2 v31 constant row ffff 0004 0008 7f00 (that
 *     row is the positive F3DEX2 marker the dispatcher already keys on, so its
 *     absence keeps this test off every GBI 2 build);
 *   - data+0x118 carries the 0xef-tagged G_SETOTHERMODE init word the F3D
 *     walker already reads from this offset (the dispatcher's f3d othermode
 *     seed), present in the F3DEX-family data image and not in F3DEX2's;
 *   - the GBI command dispatch table at data+0xb8 is densely populated with
 *     IMEM handler addresses (every entry in [0x1000,0x2000) until the 0x0000
 *     terminator): the v1 build fills all 39 slots, where a GBI 2 segment has
 *     no such table here at all.
 * Requiring all three keeps the test specific to the F3DEX v1 GBI-1 vertex
 * family; it is used only to *select the GBI 1 decode*, never to re-route, so
 * a miss leaves existing behaviour untouched. */
static int f3dex1_data_family(const unsigned char *rdram,
                              unsigned int rdram_size, unsigned int ud)
{
    unsigned int i, dense, total;
    if (rdram == 0 || ud == 0 || ud + 0x1c0u > rdram_size)
        return 0;
    /* reject F3DEX2 (v31 constant row present) */
    if (rdram[(ud + 0x1b0) ^ 3] == 0xffu && rdram[(ud + 0x1b1) ^ 3] == 0xffu
        && rdram[(ud + 0x1b2) ^ 3] == 0x00u && rdram[(ud + 0x1b3) ^ 3] == 0x04u
        && rdram[(ud + 0x1b4) ^ 3] == 0x00u && rdram[(ud + 0x1b5) ^ 3] == 0x08u
        && rdram[(ud + 0x1b6) ^ 3] == 0x7fu && rdram[(ud + 0x1b7) ^ 3] == 0x00u)
        return 0;
    /* F3D-family othermode-init tag */
    if (rdram[(ud + 0x118) ^ 3] != 0xefu)
        return 0;
    /* dense GBI dispatch table at data+0xb8 */
    dense = 0;
    total = 0;
    for (i = 0; i < 48u; i++)
    {
        unsigned int v = ((unsigned int)rdram[(ud + 0xb8 + i * 2u) ^ 3] << 8)
                       |  (unsigned int)rdram[(ud + 0xb9 + i * 2u) ^ 3];
        if (v == 0u)
            break;
        total++;
        if (v >= 0x1000u && v < 0x2000u)
            dense++;
    }
    return (total >= 30u && dense == total) ? 1 : 0;
}

/* Recognise a GBI 1 F3D-family data segment by its other-mode init tag alone,
 * with no dependence on the name string or the dense GBI dispatch table that
 * f3dex1_data_family() requires. Wipeout 64 re-loads its F3DLX build through a
 * second data image that carries neither: the only string in it is "RSP SW
 * Version: 2.0H" at +0x270, and the 0xb8 dispatch table is absent, so both the
 * f3d_ucode_family() name scan and f3dex1_data_family() miss it and the task
 * falls through to the F3DEX2 walker -- which then mis-decodes its GBI 1
 * vertex/triangle stream (every intro and menu screen renders black or, where
 * a mis-read opcode lands on the BG handler, as garbage).
 *
 * data+0x118 holds the 0xef-tagged G_SETOTHERMODE word the F3D walker already
 * seeds from; it is present across the F3DEX v1 (GBI 1) family and absent on
 * every F3DEX2/S2DEX2 (GBI 2) build, which instead carry 0x00 there and the
 * ffff0004 0008 7f00 v31 constant row at data+0x1b0. Requiring the tag present
 * and that v31 row absent keeps this strictly off GBI 2. It is used only for an
 * otherwise-unclassified task (neither the text version probe nor the name
 * family matched), so plain Fast3D keeps its own decode; the caller further
 * excludes the GBI 1 S2DEX sprite ucode by name, leaving only the F3DEX/F3DLX/
 * L3DEX vertex-line family to route onto the F3D walker. */
static int f3d_gbi1_othermode_data(const unsigned char *rdram,
                                   unsigned int rdram_size, unsigned int ud)
{
    if (rdram == 0 || ud == 0 || ud + 0x1c0u > rdram_size)
        return 0;
    if (rdram[(ud + 0x1b0) ^ 3] == 0xffu && rdram[(ud + 0x1b1) ^ 3] == 0xffu
        && rdram[(ud + 0x1b2) ^ 3] == 0x00u && rdram[(ud + 0x1b3) ^ 3] == 0x04u
        && rdram[(ud + 0x1b4) ^ 3] == 0x00u && rdram[(ud + 0x1b5) ^ 3] == 0x08u
        && rdram[(ud + 0x1b6) ^ 3] == 0x7fu && rdram[(ud + 0x1b7) ^ 3] == 0x00u)
        return 0;                       /* F3DEX2/S2DEX2 v31 row -> GBI 2 */
    return rdram[(ud + 0x118) ^ 3] == 0xefu;
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
    {
        unsigned int ut = read_dmem_u32(dmem, 0xfd0) & 0x00ffffffu;
        unsigned int ud = read_dmem_u32(dmem, 0xfd8) & 0x00ffffffu;
        int fam;
        int gbi1_oth;
        gsp_params_at_task_start = 1;
        gsp_detect_ucode_params(&s_gsp, rdram, rdram_size, ud, ut);
        gsp_params_at_task_start = 0;
        /* Classify the Gfx ucode family from the SGI name string in its data
         * segment: the F3D vertex family (F3DEX/F3DLX/F3DLP, fam 1) and the L3D
         * line family (L3DEX, fam 2) are both GBI 1 and both belong on the F3D
         * walker. f3d_is_ucode routes the vertex ucodes by their SM64-shaped
         * version word, but the L3DEX line ucode boots differently and has no
         * such word, so route it here by family instead -- otherwise Doom 64's
         * and Hexen's automaps fall through to the F3DEX2 walker and scatter. */
        fam = f3d_ucode_family(rdram, rdram_size, ud,
                               read_dmem_u32(dmem, 0xfdc));
        /* Otherwise-unclassified task (no text version match, no name family):
         * route a nameless GBI 1 F3D-family data image onto the F3D walker
         * instead of letting it fall through to F3DEX2. Excludes the GBI 1
         * S2DEX sprite ucode (matched by name) so only the vertex-line family
         * is re-routed. */
        gbi1_oth = (!f3d_is_ucode(rdram, rdram_size, ut) && fam == 0)
                && f3d_gbi1_othermode_data(rdram, rdram_size, ud)
                && !s2dex1_ucode_match(rdram, rdram_size, ud,
                                       read_dmem_u32(dmem, 0xfdc));
        if (f3ddkr_is_ucode(rdram, rdram_size, ud,
                            read_dmem_u32(dmem, 0xfdc)))
        {
            /* F3DDKR (Diddy Kong Racing custom microcode): a GBI 1 derivative
             * with its own command set (gSPPolygon batched triangles, indexed
             * matrices, billboarding). Neither the F3D nor F3DEX2 walker can
             * decode it; route to the dedicated walker. Checked first so its
             * custom data segment is never mis-claimed by the stock paths. */
            f3ddkr_seg_reset();
            f3ddkr_set_rdram(rdram);
            f3ddkr_set_rdram_size(rdram_size);
            f3ddkr_run_dl(&s_gsp, &s_fifo, dl_addr, 0, 0);
        }
        else if (f3d_is_ucode(rdram, rdram_size, ut) || fam != 0 || gbi1_oth)
        {
            /* Plain Fast3D (e.g. Super Mario 64): different geometry opcode
             * encoding from F3DEX2, dispatched separately. gsp_detect_ucode_
             * params() validates ud against the F3DEX2 clip-table signature
             * and zeroes it for F3D, so F3D never inherits its data-segment
             * defaults there. Seed the other-modes mirror from the F3D data
             * segment here instead: the default pair lives at ud+0x118 (not
             * the F3DEX2 ud+0xc8). Partial SETOTHERMODE_H/L writes merge into
             * this baseline; without it bit 19 (G_MDSFT_TEXTPERSP) and the
             * dither/filter defaults are zero, so perspective-correct
             * texturing is off and the deformed head mesh smears and holes. */
            f3d_seg_reset();
            f3d_set_rdram(rdram);
            f3d_set_rdram_size(rdram_size);
            /* GBI 1 (F3DEX v1) needs the n<<10 / index*2 vertex+triangle
             * decode. f3d_ucode_family() matches it by the SGI name string,
             * but the Rare-engine titles (Banjo-Kazooie/Tooie, DKR, DK64,
             * Jet Force Gemini) strip that string, so fall back to the
             * structural data-segment family probe. Either signal selects the
             * GBI 1 decode; plain Fast3D (SM64) matches neither and keeps its
             * own path. The line variant stays gated on the name family (only
             * gspL3DEX, which the structural probe does not assert). */
            f3d_set_variant(fam != 0
                            || f3dex1_data_family(rdram, rdram_size, ud)
                            || gbi1_oth);
            f3d_set_line_variant(fam == 2);
            f3d_set_variant_wr64(f3d_is_wr64_ucode(rdram, rdram_size, ut));
            /* GoldenEye 007 / Perfect Dark run an early F3DEX (GBI 1) build
             * whose RSP version word at text+4 (0x201d0110) is the same one
             * SM64's plain Fast3D carries, so it reaches this F3D walker. It
             * is NOT Fast3D: per its gbi, vertex triangle indices are scaled
             * x2 (not x10) and opcode 0xB1 is G_TRI2 (two triangles per
             * command), which carries the bulk of the world geometry. Decoded
             * as Fast3D, 0xB1 is dropped and 0xBF reads the wrong indices, so
             * most of each level fails to render. Select the F3DEX decode by
             * the build's distinct first instruction word at text+0. */
            {
                unsigned int t0 = (ut + 4u <= (rdram_size ? rdram_size
                                                          : 0x800000u))
                    ? (((unsigned int)rdram[ut ^ 3] << 24)
                       | ((unsigned int)rdram[(ut + 1) ^ 3] << 16)
                       | ((unsigned int)rdram[(ut + 2) ^ 3] << 8)
                       |  (unsigned int)rdram[(ut + 3) ^ 3])
                    : 0u;
                f3d_set_variant_f3dex(t0 == 0x090005eau);
                /* Star Wars: Shadows of the Empire runs the stock early F3DEX
                 * (GBI 1) build -- data-segment name "RSP SW Version: 2.0D,
                 * 04-01-96", text+0 boot word 0x090005f8 -- compiled with
                 * -DF3DEX_GBI. Like GoldenEye's build it carries the SM64
                 * version word at text+4 (0x201d0110), so it reaches this F3D
                 * walker, but it is NOT the Rare F3DEX above: it uses the
                 * stock SGI encoding, where gSP1Triangle indices are scaled x2
                 * (not x10), 0xB1 is G_TRI2 (two triangles, indices x2, not
                 * Rare's G_TRI4), and gSPVertex packs n<<10 with (v0)*2 -- i.e.
                 * the same x2 F3DEX-family decode as the Doom 64 / Turok path.
                 * Its data segment carries no dense GBI dispatch table and no
                 * 0xef-tagged G_SETOTHERMODE init word, so the name family scan
                 * (name is "RSP SW Version", not "RSP Gfx ucode"), the
                 * f3dex1_data_family() probe and the gbi1_oth probe all miss it
                 * and it is left on the default x10 plain-Fast3D decode: 0xBF
                 * reads the wrong slots and 0xB1 is misread, so the world never
                 * renders. Route it to the x2 decode by its boot signature. */
                if (t0 == 0x090005f8u)
                    f3d_set_variant(1);
            }
            if (ud != 0 && ud + 0x120u <= rdram_size)
            {
                unsigned int oh = ((unsigned int)rdram[(ud + 0x118) ^ 3] << 24)
                                | ((unsigned int)rdram[(ud + 0x119) ^ 3] << 16)
                                | ((unsigned int)rdram[(ud + 0x11a) ^ 3] << 8)
                                |  (unsigned int)rdram[(ud + 0x11b) ^ 3];
                unsigned int ol = ((unsigned int)rdram[(ud + 0x11c) ^ 3] << 24)
                                | ((unsigned int)rdram[(ud + 0x11d) ^ 3] << 16)
                                | ((unsigned int)rdram[(ud + 0x11e) ^ 3] << 8)
                                |  (unsigned int)rdram[(ud + 0x11f) ^ 3];
                if ((oh >> 24) == 0xefu)
                    f3d_set_othermode_init(oh, ol);
            }
            f3d_run_dl(&s_gsp, &s_fifo, dl_addr, 0, 0);
        }
        else
        {
            f3dex2_set_rdram(rdram);
            f3dex2_set_rdram_size(rdram_size);
            f3dex2_set_task_ucode(rdram, ut);
            /* Standalone S2DEX (GBI 1) is not caught by the text version probe;
             * detect it by data-segment name and force the GBI 1 sprite class. */
            if (s2dex1_ucode_match(rdram, rdram_size, ud,
                                   read_dmem_u32(dmem, 0xfdc)))
                f3dex2_force_class_s2dex1();
            f3dex2_run_dl(&s_gsp, &s_fifo, dl_addr, 0, 0);
        }
    }

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
