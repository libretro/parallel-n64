/* rdp_emit_f3dex2.c -- F3DEX2 display-list dispatcher for the angrylion HLE
 * path. Reads F3DEX2 commands from RDRAM, routes geometry to the C89
 * frontend (rdp_emit_frontend), and appends the resulting RDP triangle
 * commands to a FIFO the angrylion rasterizer consumes.
 *
 * Opcode/argument layout follows the documented F3DEX2 microcode (the same
 * decode GLideN64 uses); no external plugin code is linked.
 *
 * Build check:
 *   gcc -std=c89 -pedantic -Wall -Wdeclaration-after-statement -Werror
 */

#include "rdp_emit_f3dex2.h"

/* F3DEX2 command bytes (high byte of w0) */
#define F3DEX2_VTX        0x01
#define F3DEX2_TRI1       0x05
#define F3DEX2_TRI2       0x06
#define F3DEX2_QUAD       0x07
#define F3DEX2_TEXTURE    0xD7
#define F3DEX2_POPMTX     0xD8
#define F3DEX2_GEOMETRY   0xD9
#define F3DEX2_MTX        0xDA
#define F3DEX2_DL         0xDE
#define F3DEX2_ENDDL      0xDF

/* matrix param bits */
#define MTX_PROJECTION    0x04
#define MTX_LOAD          0x02
#define MTX_PUSH          0x01

#define DL_NOPUSH         0x01

#define F3DEX2_MOVEWORD   0xDB
#define G_MW_SEGMENT      0x06
#define G_MW_NUMLIGHT     0x02

#define F3DEX2_MOVEMEM    0xDC
#define G_MV_VIEWPORT     0x08
#define G_MV_LIGHT        0x0a

/* RDRAM/DMEM 32-bit words are stored host-native in this core (the RSP's
 * u32() accessor reads them directly with no byteswap), so read native. */
static unsigned int s_rdram_size;   /* set per frame; 0 => assume 8 MiB */

static unsigned int rd_u32_be(const unsigned char *r, unsigned int a)
{
    unsigned int limit = s_rdram_size ? s_rdram_size : (8u * 1024u * 1024u);
    if (a + 4u > limit)
        return 0u;          /* out of range: treat as G_SPNOOP */
    return *(const unsigned int *)(r + a);
}

/* F3DEX2 segment table: addresses are (segment << 24) | offset, resolved as
 * seg_table[segment] + offset. Set by G_MOVEWORD/G_MW_SEGMENT. Segment 0 and
 * the KSEG0 0x80-based pointers resolve to a 0 base, i.e. physical == offset. */
static unsigned int s_seg_table[16];

/* RDP render state is global on the real RSP (one set of mode registers),
 * not per-display-list. Track it module-wide so a mode set in one DL is seen
 * by sibling DLs drawn afterward; reset per frame alongside the segments. */
static int s_textured;
static int s_zbuffered;

/* RDRAM size and recursion depth bound the display-list walk so a malformed
 * or mis-segmented list cannot read past RDRAM or recurse without limit (both
 * would hard-hang the core). s_rdram_size (declared above) is set per frame by
 * the activation; 0 means "unknown", reads then assume the default 8 MiB. */
static int          s_dl_depth;

#define DL_MAX_DEPTH 32

static void seg_reset(void)
{
    int i;
    for (i = 0; i < 16; i++)
        s_seg_table[i] = 0u;
    s_textured  = 0;
    s_zbuffered = 0;
    s_dl_depth  = 0;
}

void f3dex2_seg_reset(void)
{
    seg_reset();
}

void f3dex2_set_rdram_size(unsigned int size)
{
    s_rdram_size = size;
}

void rdp_fifo_init(RdpFifo *f, unsigned char *rdram,
                   unsigned int base, unsigned int cap)
{
    f->rdram = rdram;
    f->base  = base;
    f->used  = 0;
    f->cap   = cap;
}

void rdp_fifo_append(RdpFifo *f, const int32_t *words, int count)
{
    int i;
    unsigned int off = f->base + f->used;
    if (f->used + (unsigned int)count * 4u > f->cap)
        return;
    for (i = 0; i < count; i++)
    {
        unsigned int w = (unsigned int)words[i];
        unsigned int a = off + (unsigned int)i * 4u;
        /* RDP command words are read by angrylion in host-native order from
         * RDRAM (rdram_read_idx32 is native); store native. */
        *(int32_t *)(f->rdram + a) = (int32_t)w;
    }
    f->used += (unsigned int)count * 4u;
}

/* Resolve an N64 segmented/virtual address to a physical RDRAM byte address:
 * physical = seg_table[(addr >> 24) & 0xf] + (addr & 0xffffff). For KSEG0
 * pointers (top byte 0x80/0xA0) the segment index is 0 (table base 0), so the
 * result is just the low 24 bits -- the standard virtual->physical strip. */
static unsigned int seg_addr(unsigned int w1)
{
    unsigned int seg = (w1 >> 24) & 0x0fu;
    return (s_seg_table[seg] + (w1 & 0x00ffffffu)) & 0x00ffffffu;
}

/* true if [a, a+bytes) lies within RDRAM; used to reject mis-segmented
 * geometry pointers before the frontend dereferences them. */
static int addr_in_range(unsigned int a, unsigned int bytes)
{
    unsigned int limit = s_rdram_size ? s_rdram_size : (8u * 1024u * 1024u);
    return (a < limit) && (bytes <= limit) && (a + bytes <= limit);
}

void f3dex2_run_dl(GSPState *gsp, RdpFifo *fifo, unsigned int addr,
                   int textured, int z_buffered)
{
    int guard = 0;
    unsigned int pc = addr;
    int running = 1;
    /* render-state is shared module-wide (see s_textured/s_zbuffered) so it
     * persists across DL recursion; the textured/z_buffered arguments only
     * seed it when non-zero (callers pass the current state down). */
    if (textured)   s_textured  = textured;
    if (z_buffered) s_zbuffered = z_buffered;

    /* bound recursion: a malformed or mis-segmented G_DL chain could otherwise
     * recurse until the C stack overflows and the core hard-hangs. */
    if (s_dl_depth >= DL_MAX_DEPTH)
        return;
    s_dl_depth++;

    while (running && guard++ < 100000)
    {
        unsigned int w0, w1;
        int cmd;
        const unsigned char *r = fifo->rdram;

        w0 = rd_u32_be(r, pc);
        w1 = rd_u32_be(r, pc + 4);
        pc += 8;
        cmd = (int)((w0 >> 24) & 0xff);

        switch (cmd)
        {
        case F3DEX2_MTX:
        {
            int param = (int)((w0 & 0xff) ^ MTX_PUSH);
            int projection = (param & MTX_PROJECTION) ? 1 : 0;
            int load = (param & MTX_LOAD) ? 1 : 0;
            int push = (param & MTX_PUSH) ? 1 : 0;
            unsigned int ma = seg_addr(w1);
            if (addr_in_range(ma, 64u))     /* 16 s16 ints + 16 u16 fracs */
                gsp_matrix_load(gsp, r, ma, projection, load, push);
            break;
        }
        case F3DEX2_POPMTX:
            gsp_matrix_pop(gsp);
            break;

        case F3DEX2_VTX:
        {
            int n  = (int)((w0 >> 12) & 0xff);
            int v0 = (int)((w0 >> 1) & 0x7f) - n;
            unsigned int va = seg_addr(w1);
            if (n > 0 && addr_in_range(va, (unsigned int)n * 16u))
                gsp_vertex(gsp, r, va, n, v0);
            break;
        }

        case F3DEX2_TRI1:
        {
            int a = (int)((w0 >> 17) & 0x7f);
            int b = (int)((w0 >> 9) & 0x7f);
            int c = (int)((w0 >> 1) & 0x7f);
            int32_t cmdw[44];
            int nc = gsp_triangle(gsp, cmdw, a, b, c, s_textured, s_zbuffered);
            if (nc > 0) rdp_fifo_append(fifo, cmdw, nc);
            break;
        }

        case F3DEX2_TRI2:
        case F3DEX2_QUAD:
        {
            int a0 = (int)((w0 >> 17) & 0x7f);
            int b0 = (int)((w0 >> 9) & 0x7f);
            int c0 = (int)((w0 >> 1) & 0x7f);
            int a1 = (int)((w1 >> 17) & 0x7f);
            int b1 = (int)((w1 >> 9) & 0x7f);
            int c1 = (int)((w1 >> 1) & 0x7f);
            int32_t cmdw[44];
            int nc;
            nc = gsp_triangle(gsp, cmdw, a0, b0, c0, s_textured, s_zbuffered);
            if (nc > 0) rdp_fifo_append(fifo, cmdw, nc);
            nc = gsp_triangle(gsp, cmdw, a1, b1, c1, s_textured, s_zbuffered);
            if (nc > 0) rdp_fifo_append(fifo, cmdw, nc);
            break;
        }

        case F3DEX2_DL:
        {
            int nopush = (int)((w0 >> 16) & 0xff) & DL_NOPUSH;
            unsigned int da = seg_addr(w1);
            if (addr_in_range(da, 8u))      /* need at least one command word */
                f3dex2_run_dl(gsp, fifo, da, s_textured, s_zbuffered);
            if (nopush)
                running = 0; /* branch (no return) ends this list */
            break;
        }

        case F3DEX2_MOVEWORD:
        {
            /* G_MOVEWORD with index G_MW_SEGMENT (0x06) sets a segment-table
             * base: segment = (w0 >> 2) & 0xf, base = w1 & 0xffffff. Other
             * MOVEWORD indices (numlight, fog, clip, ...) don't affect address
             * resolution here, so ignore them. */
            int index = (int)((w0 >> 16) & 0xff);
            if (index == G_MW_SEGMENT)
            {
                unsigned int seg = (w0 >> 2) & 0x0fu;
                s_seg_table[seg] = w1 & 0x00ffffffu;
            }
            else if (index == G_MW_NUMLIGHT)
            {
                /* number of directional lights = w1 / 24 */
                gsp_set_num_lights(gsp, (int)(w1 / 24u));
            }
            break;
        }

        case F3DEX2_MOVEMEM:
        {
            /* G_MOVEMEM with index G_MV_VIEWPORT (0x08) loads the viewport
             * (vscale/vtrans) from the segmented address in w1. Other MOVEMEM
             * targets (lights, matrices) are not needed for screen mapping. */
            int index = (int)(w0 & 0xff);
            if (index == G_MV_VIEWPORT)
            {
                unsigned int vp = seg_addr(w1);
                if (addr_in_range(vp, 16u))     /* 8 s16: vscale + vtrans */
                    gsp_set_viewport(gsp, r, vp);
            }
            else if (index == G_MV_LIGHT)
            {
                /* G_MOVEMEM/G_MV_LIGHT loads one light. The destination slot is
                 * encoded in w0 bits 5..15 as a byte offset: offset =
                 * ((w0 >> 5) & 0x7ff) & 0x7f8, n = offset / 24. n < 2 selects a
                 * LookAt entry (ignored -- only used for texture-coord gen);
                 * n >= 2 is directional/ambient light slot n - 2. */
                unsigned int offset = ((w0 >> 5) & 0x7ffu) & 0x7f8u;
                int n = (int)(offset / 24u);
                if (n >= 2)
                {
                    unsigned int la = seg_addr(w1);
                    if (addr_in_range(la, 24u))
                        gsp_set_light(gsp, r, la, n - 2);
                }
            }
            break;
        }

        case F3DEX2_ENDDL:
            running = 0;
            break;

        case F3DEX2_TEXTURE:
            /* G_TEXTURE sets the texture scale/tile/level the frontend uses
             * for emitted texcoords, and the on/off flag that selects whether
             * subsequent triangles are textured. w1 holds the S/T scale
             * (s.16 each); w0 bits select tile, max LOD level, and on. */
        {
            int on    = (int)((w0 >> 1) & 0x7f);
            int level = (int)((w0 >> 11) & 0x07);
            int tile  = (int)((w0 >> 8) & 0x07);
            float ss  = (float)((w1 >> 16) & 0xffff) / 65536.0f;
            float ts  = (float)(w1 & 0xffff) / 65536.0f;
            s_textured = (on != 0) ? 1 : 0;
            gsp_set_texture(gsp, ss, ts, tile, level, gsp->tex_w, gsp->tex_h);
            break;
        }

        case F3DEX2_GEOMETRY:
            /* G_GEOMETRYMODE clears the bits in ~(w0 & 0xffffff) and sets the
             * bits in w1: mode = (mode & (w0 & 0xffffff)) | w1. The cull-enable
             * bits (G_CULL_FRONT/G_CULL_BACK) drive backface rejection, which
             * the RSP does on the CPU before submitting triangles -- angrylion
             * (software RDP) performs no culling, so we must apply it here. */
            gsp_set_geometry_mode(gsp,
                (gsp_get_geometry_mode(gsp) & (w0 & 0x00ffffffu)) | w1);
            break;

        default:
            /* RDP render-state commands embedded in the display list
             * (G_SETxIMG, G_SETSCISSOR, G_SETCOMBINE, G_RDPSETOTHERMODE,
             * G_SETTILE/SIZE, G_LOADBLOCK/TILE/TLUT, the SET_*_COLOR commands,
             * the sync commands, and the rectangles) are already in RDP wire
             * format: angrylion decodes the command as (word0 >> 24) & 0x3f,
             * and the GBI bytes 0xE0..0xFF map exactly onto the RDP command
             * ids under that mask. So these are forwarded to the FIFO
             * verbatim -- this is the gDP -> RDP "translation", which for the
             * shared commands is a pass-through.
             *
             * The geometry commands (handled above) are the only ones the RSP
             * actually computes; everything else the RSP passes to the RDP
             * unchanged, and so do we. Commands outside the RDP range are
             * RSP-only (matrix/vertex/etc, already handled) or unknown and
             * are skipped. */
            if (cmd >= 0xc8 && cmd != F3DEX2_DL && cmd != F3DEX2_ENDDL)
            {
                int rdp_id = cmd & 0x3f;
                /* sniff Set Other Modes (0xEF -> 0x2f) for the depth-test
                 * enables so subsequent triangles pick the Z variant.
                 * z_update_en = word1 bit 5, z_compare_en = word1 bit 4. */
                if (rdp_id == 0x2f)
                {
                    int zc = (int)((w1 >> 4) & 1);
                    int zu = (int)((w1 >> 5) & 1);
                    s_zbuffered = (zc || zu) ? 1 : 0;
                }
                /* 0x24..0x3f are the RDP non-triangle commands angrylion
                 * implements; 0x31 (G_SETKEY*) is not, so skip it. SYNC_FULL
                 * (0x29) is dropped here too: the activation appends exactly
                 * one frame terminator, and forwarding the list's own trailing
                 * G_RDPFULLSYNC as well would complete the frame twice. */
                if (rdp_id >= 0x24 && rdp_id <= 0x3f &&
                    rdp_id != 0x31 && rdp_id != 0x29)
                {
                    int32_t two[2];
                    two[0] = (int32_t)w0;
                    /* SET_COLOR_IMAGE (0x3f), SET_Z_IMAGE (0x3e) and
                     * SET_TEXTURE_IMAGE (0x3d) carry a DRAM pointer in the low
                     * 24 bits of w1 that may be a segmented/virtual address.
                     * angrylion masks args[1] & 0x00ffffff with no segment
                     * table, so resolve to a physical address here. The format
                     * and width fields in the upper bits of w1 are preserved. */
                    if (rdp_id == 0x3f || rdp_id == 0x3e || rdp_id == 0x3d)
                        two[1] = (int32_t)((w1 & 0xff000000u) | (seg_addr(w1) & 0x00ffffffu));
                    else
                        two[1] = (int32_t)w1;
                    rdp_fifo_append(fifo, two, 2);
                }
            }
            break;
        }
    }

    s_dl_depth--;
}
