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
#include "rdp_emit_s2dex.h"

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
#define F3DEX2_SETOTHERMODE_L 0xE2
#define F3DEX2_SETOTHERMODE_H 0xE3
#define F3DEX2_RDPSETOTHERMODE 0xEF
#define F3DEX2_TEXRECT        0xE4
#define F3DEX2_TEXRECTFLIP    0xE5
#define F3DEX2_RDPHALF_1      0xF1
#define F3DEX2_RDPHALF_2      0xE1

/* matrix param bits */
#define MTX_PROJECTION    0x04
#define MTX_LOAD          0x02
#define MTX_PUSH          0x01

#define DL_NOPUSH         0x01

#define F3DEX2_MOVEWORD   0xDB
#define G_MW_SEGMENT      0x06
#define G_MW_NUMLIGHT     0x02
#define G_MW_LIGHTCOL     0x0a
#define G_MW_FOG          0x08
#define G_MW_PERSPNORM    0x0e

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

/* G_RDPHALF_1 (0xE1) latch: F3DZEX2's G_BRANCH_W (0x04) branches to the DL
 * address staged here by the preceding RDPHALF_1. */
static unsigned int s_half1;

/* RDP render state is global on the real RSP (one set of mode registers),
 * not per-display-list. Track it module-wide so a mode set in one DL is seen
 * by sibling DLs drawn afterward; reset per frame alongside the segments. */
static int s_textured;
static int s_zbuffered;

/* The RSP maintains the RDP "other modes" as two 32-bit words (high and low).
 * F3DEX2 updates them either wholesale (G_RDPSETOTHERMODE / 0xEF) or, far more
 * commonly, as partial bitfield writes (G_SETOTHERMODE_H/L / 0xE3/0xE2) that
 * read-modify-write a sub-range and then have the RSP emit a full RDP
 * SET_OTHER_MODES (0x2f). The cycle type (1- vs 2-cycle) lives in the high word
 * at bits 20-21 and on OoT is set through G_SETOTHERMODE_H, so the partial
 * writes MUST be merged and re-emitted as a full 0x2f or every such triangle
 * renders with a stale cycle type. We mirror that register here. The high word
 * keeps 0x2f in its command byte (bits 29-24) so the merged word is a valid
 * SET_OTHER_MODES when forwarded. */
static unsigned int s_othermode_h;
static unsigned int s_othermode_l;

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
    /* command byte 0x2f in bits 29-24 so the high word is a valid
     * SET_OTHER_MODES when forwarded; mode bits start cleared (1-cycle). */
    s_othermode_h = 0x2fu << 24;
    s_othermode_l = 0u;
}

void f3dex2_seg_reset(void)
{
    s2dex_reset();
    seg_reset();
}

void f3dex2_set_rdram_size(unsigned int size)
{
    s_rdram_size = size;
}

/* RDRAM base for display-list, vertex and matrix reads. Previously the
 * walker borrowed the FIFO's pointer, which doubled as the FIFO backing
 * store; with the FIFO moved to host memory the two are distinct. */
static unsigned char *s_rdram_base = 0;

void f3dex2_set_rdram(unsigned char *rdram)
{
    s_rdram_base = rdram;
}

void rdp_fifo_init(RdpFifo *f, unsigned char *storage,
                   unsigned int base, unsigned int cap)
{
    f->storage = storage;
    f->base    = base;
    f->used    = 0;
    f->cap     = cap;
    f->flush   = 0;
}

void rdp_fifo_append(RdpFifo *f, const int32_t *words, int count)
{
    int i;
    unsigned int off;
    if (f->used + (unsigned int)count * 4u > f->cap)
    {
        /* Heavy frames (camera swings over a full scene) can exceed the
         * FIFO; drain it mid-frame the way real hardware streams the DP
         * command buffer, then continue. Dropping commands here is not an
         * option: losing the frame-final SYNC_FULL (RDP 0x29) means the
         * DP interrupt is never raised and the game's graphics thread
         * blocks forever on the task completion. */
        if (f->flush)
            f->flush(f);
        if (f->used + (unsigned int)count * 4u > f->cap)
            return;
    }
    off = f->used;
    for (i = 0; i < count; i++)
    {
        unsigned int w = (unsigned int)words[i];
        unsigned int a = off + (unsigned int)i * 4u;
        /* RDP command words are fetched by angrylion in host-native order
         * (the overlay fetch mirrors rdram_read_idx32); store native. */
        *(int32_t *)(f->storage + a) = (int32_t)w;
    }
    f->used += (unsigned int)count * 4u;
}

/* Resolve an N64 segmented/virtual address to a physical RDRAM byte address:
 * physical = seg_table[(addr >> 24) & 0xf] + (addr & 0xffffff). For KSEG0
 * pointers (top byte 0x80/0xA0) the segment index is 0 (table base 0), so the
 * result is just the low 24 bits -- the standard virtual->physical strip. */
static unsigned int seg_addr_rsp(unsigned int w1)
{
    /* segmented_to_physical: clears the input's top byte and adds the raw
     * segment-table word, with no final mask. Games store KSEG0 pointers in
     * the table, so the sum keeps the 0x80000000 bit; the RDP and the RSP
     * DMA engine both ignore the upper address bits, but the emitted
     * SET*IMG words carry the full sum. */
    unsigned int seg = (w1 >> 24) & 0x0fu;
    return s_seg_table[seg] + (w1 & 0x00ffffffu);
}

static unsigned int seg_addr(unsigned int w1)
{
    /* physical address for this plugin's own RDRAM reads */
    return seg_addr_rsp(w1) & 0x00ffffffu;
}

/* Exported segment resolution for the S2DEX background renderers: the
 * uObjBg imagePtr is a segmented address (Zelda's pre-rendered rooms pass
 * segment-relative pointers) which the microcode resolves through the same
 * segment table before emitting it in SETTIMG. */
unsigned int gsp_seg_addr_rsp(unsigned int w1)
{
    return seg_addr_rsp(w1);
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
        const unsigned char *r = s_rdram_base;

        if (r == 0)
            return;

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
        {
            /* F3DEX2 G_POPMTX pops w1 / 64 matrices (64 bytes per matrix), not
             * always one. Skeleton rendering pops several levels at once; only
             * popping one leaves the stack too deep and transforms subsequent
             * draws with a stale limb matrix. */
            unsigned int npop = w1 >> 6;
            while (npop--)
                gsp_matrix_pop(gsp);
            break;
        }

        case F3DEX2_VTX:
        {
            int n  = (int)((w0 >> 12) & 0xff);
            int v0 = (int)((w0 >> 1) & 0x7f) - n;
            unsigned int va = seg_addr(w1);
            if (n > 0 && addr_in_range(va, (unsigned int)n * 16u))
                gsp_vertex(gsp, r, va, n, v0);
            break;
        }

        case 0x09: /* S2DEX2 G_BG_1CYC */
        {
            /* Zelda's pre-rendered backgrounds switch the RSP to the
             * S2DEX2 microcode (gSPLoadUcodeL) for one gSPBgRect1Cyc
             * command. The BG opcodes are unused in F3DZEX2, so route
             * them to the transcribed S2DEX2 background renderer. */
            unsigned int bga = seg_addr(w1);
            if (addr_in_range(bga, 40u))
                s2dex_bg_1cyc(r, s_rdram_size ? s_rdram_size
                                              : (8u * 1024u * 1024u),
                              bga, fifo);
            break;
        }

        case 0x0a: /* S2DEX2 G_BG_COPY */
        {
            /* gSPBgRectCopy: the transcribed copy-mode renderer */
            unsigned int bga = seg_addr(w1);
            if (addr_in_range(bga, 40u))
                s2dex_bg_copy(r, s_rdram_size ? s_rdram_size
                                              : (8u * 1024u * 1024u),
                              bga, fifo);
            break;
        }

        case 0x0b: /* S2DEX2 G_OBJ_RENDERMODE */
            s2dex_set_obj_rendermode(w1);
            break;

        case F3DEX2_TRI1:
        {
            int a = (int)((w0 >> 17) & 0x7f);
            int b = (int)((w0 >> 9) & 0x7f);
            int c = (int)((w0 >> 1) & 0x7f);
            int32_t cmdw[220];
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
            int32_t cmdw[220];
            int nc;
            /* The G_TRI2 handler stores the second triangle's indices and
             * jals tri_to_rdp before falling through to the G_TRI1 path
             * for the first triangle: the microcode emits the SECOND
             * triangle of the pair first. */
            nc = gsp_triangle(gsp, cmdw, a1, b1, c1, s_textured, s_zbuffered);
            if (nc > 0) rdp_fifo_append(fifo, cmdw, nc);
            nc = gsp_triangle(gsp, cmdw, a0, b0, c0, s_textured, s_zbuffered);
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
                s_seg_table[seg] = w1;
            }
            else if (index == G_MW_NUMLIGHT)
            {
                /* number of directional lights = w1 / 24 */
                gsp_set_num_lights(gsp, (int)(w1 / 24u));
            }
            else if (index == G_MW_LIGHTCOL)
            {
                /* gSPLightColor: recolor a light without touching its
                 * direction. Two movewords per call (col and colc copies,
                 * 4 bytes apart) carry the same rgba32; the light index is
                 * the 24-byte slot of the 16-bit offset. The ambient is the
                 * last light, so index == numlights recolors the ambient
                 * with no special casing. */
                unsigned int ofs = w0 & 0xffffu;
                if ((ofs % 24u) == 0)
                    gsp_set_light_color(gsp, (int)(ofs / 24u),
                                        (int32_t)((w1 >> 24) & 0xffu),
                                        (int32_t)((w1 >> 16) & 0xffu),
                                        (int32_t)((w1 >> 8) & 0xffu));
            }
            else if (index == G_MW_FOG)
            {
                /* gSPFogFactor / gSPFogPosition: fog multiplier in the high
                 * s16 of w1, offset in the low s16. */
                gsp_set_fog(gsp, (int)(short)((w1 >> 16) & 0xffffu),
                                 (int)(short)(w1 & 0xffffu));
            }
            else if (index == 0x04)
            {
                /* G_MW_CLIP (gSPClipRatio): four words set the guard-band
                 * clip ratio, offsets 0x04/0x0c = +RX/+RY (positive value)
                 * and 0x14/0x1c = -RX/-RY (negative as u16). The gbi sets
                 * all four consistently; take the magnitude from the
                 * positive words. OoT's pause screen sets ratio 1 (clip at
                 * the screen edges) and the scene restores 2; the scaled
                 * outcodes and the clipper's plane rows both depend on it. */
                unsigned int off = w0 & 0xffffu;
                if (off == 0x04u || off == 0x0cu)
                {
                    int rv = (int)(int16_t)(w1 & 0xffffu);
                    if (rv > 0 && rv <= 4)
                        gsp->clip_ratio = rv;
                }
            }
            else if (index == G_MW_PERSPNORM)
            {
                /* gSPPerspNormalize: the low 16 bits are the perspective
                 * normalization scale the RSP applies to 1/w. */
                gsp_set_persp_norm(gsp, w1 & 0xffffu);
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
                else
                {
                    /* slots 0/1: the texgen lookat X/Y directions */
                    unsigned int la = seg_addr(w1);
                    if (addr_in_range(la, 24u))
                        gsp_set_lookat(gsp, r, la, n);
                }
            }
            break;
        }

        case 0xE1:                       /* G_RDPHALF_1: stage branch target */
            s_half1 = w1;
            break;


        case 0x03:                       /* G_CULLDL (gSPCullDisplayList) */
        {
            /* The RSP tests the AND of the clip flags of vertices
             * vstart..vend (inclusive): if every vertex lies outside the
             * same screen frustum plane (x or y beyond +-w), it jumps to
             * the ENDDL handler, terminating the remainder of the current
             * display list. OoT bounds each room mesh section and many
             * actors this way; without it the walker renders sub-lists the
             * RSP rejects, and their guard-band-clipped remnants land as
             * extra layers over near geometry. Vertices with w <= 0 carry
             * no outside flags here, which can only under-cull (render
             * more), never over-cull. */
            int v0 = (int)((w0 & 0xffffu) >> 1);
            int v1 = (int)((w1 & 0xffffu) >> 1);
            if (v0 >= 0 && v1 >= v0 && v1 < GSP_MAX_VERTICES)
            {
                unsigned int all = GSP_CLIP_REJECT;
                int vi;
                for (vi = v0; vi <= v1 && all; vi++)
                    all &= (unsigned int)gsp->vtx[vi].clip;
                if (all)
                    running = 0;
            }
            break;
        }

        case 0x04:                       /* F3DZEX2 G_BRANCH_W (gSPBranchLessW) */
        {
            /* Branch (no return) to the DL staged by the preceding RDPHALF_1
             * when vertex[(w0>>1)&0x7f]'s clip-space w integer part is less
             * than w1. OoT's F3DZEX2 uses this for LOD selection and
             * distance-based skip; ignoring it renders the wrong variant. */
            int bv = (int)((w0 >> 1) & 0x7f);
            unsigned int ba = seg_addr(s_half1);
            if (bv >= 0 && bv < GSP_MAX_VERTICES &&
                (gsp->vtx[bv].cw >> 16) < (int32_t)w1 &&
                addr_in_range(ba, 8u))
                pc = ba;
            break;
        }

        case F3DEX2_ENDDL:
            running = 0;
            break;

        case F3DEX2_TEXRECT:
        case F3DEX2_TEXRECTFLIP:
        {
            /* TEXTURE_RECTANGLE is a 4-word RDP command (angrylion reads 16
             * bytes for ids 0x24/0x25), but F3DEX2 delivers it as three DL
             * commands: the G_TEXRECT(FLIP) word pair carries the rectangle
             * (xl/yl/xh/yh + tile) and the following two G_RDPHALF commands
             * carry the texture coordinates -- RDPHALF_2 (0xE1) the s/t pair
             * and RDPHALF_1 (0xF1) the dsdx/dtdy pair. The previous pass-through
             * forwarded only the first 2 words, so angrylion consumed the next
             * command (typically the following G_SETTIMG) as the texrect's tail,
             * dropping that SET_TEXTURE_IMAGE and corrupting the rectangle's
             * coordinates. Assemble the full 4-word command and skip the two
             * RDPHALF words we consumed. */
            unsigned int h0 = 0u, h1 = 0u;
            unsigned int c0, c1;
            int32_t tr[4];
            c0 = rd_u32_be(r, pc);
            c1 = rd_u32_be(r, pc + 4);
            /* first RDPHALF */
            if (((c0 >> 24) & 0xff) == F3DEX2_RDPHALF_2)
                h0 = rd_u32_be(r, pc + 4);
            else if (((c0 >> 24) & 0xff) == F3DEX2_RDPHALF_1)
                h1 = rd_u32_be(r, pc + 4);
            pc += 8;
            /* second RDPHALF */
            c0 = rd_u32_be(r, pc);
            c1 = rd_u32_be(r, pc + 4);
            if (((c0 >> 24) & 0xff) == F3DEX2_RDPHALF_2)
                h0 = rd_u32_be(r, pc + 4);
            else if (((c0 >> 24) & 0xff) == F3DEX2_RDPHALF_1)
                h1 = rd_u32_be(r, pc + 4);
            pc += 8;
            (void)c1;
            tr[0] = (int32_t)w0;    /* keep the raw 0xE4/0xE5 byte, as the RSP does */
            tr[1] = (int32_t)w1;
            tr[2] = (int32_t)h0;   /* s, t   (RDPHALF_2) */
            tr[3] = (int32_t)h1;   /* dsdx, dtdy (RDPHALF_1) */
            rdp_fifo_append(fifo, tr, 4);
            break;
        }

        case F3DEX2_SETOTHERMODE_H:
        case F3DEX2_SETOTHERMODE_L:
        {
            /* Partial other-modes update. F3DEX2 encodes a bitfield write as
             * length = (w0 & 0xff) + 1 bits, placed at
             * shift = 32 - ((w0 >> 8) & 0xff) - length (clamped at 0); the new
             * bits come from w1. Merge into the mirrored high (0xE3) or low
             * (0xE2) word, then emit a full RDP SET_OTHER_MODES so angrylion
             * sees the resulting cycle type / render mode. */
            unsigned int len = ((w0 >> 0) & 0xffu) + 1u;
            unsigned int shb = (w0 >> 8) & 0xffu;
            unsigned int shift;
            unsigned int mask;
            int32_t two[2];
            if ((32u - shb) < len)
                shift = 0u;
            else
                shift = 32u - shb - len;
            if (len >= 32u)
                mask = 0xffffffffu;
            else
                mask = ((1u << len) - 1u) << shift;
            if (cmd == F3DEX2_SETOTHERMODE_H)
                s_othermode_h = (s_othermode_h & ~mask)
                              | ((unsigned int)w1 & mask)
                              | (0x2fu << 24);
            else
                s_othermode_l = (s_othermode_l & ~mask)
                              | ((unsigned int)w1 & mask);
            s_zbuffered = (((s_othermode_l >> 4) & 1u) ||
                           ((s_othermode_l >> 5) & 1u)) ? 1 : 0;
            two[0] = (int32_t)(s_othermode_h | (0x2fu << 24));
            two[1] = (int32_t)s_othermode_l;
            rdp_fifo_append(fifo, two, 2);
            break;
        }

        case F3DEX2_RDPSETOTHERMODE:
        {
            /* Wholesale other-modes write: w0 carries the high word (with the
             * command byte already in bits 29-24), w1 the low word. Mirror both
             * and forward verbatim as SET_OTHER_MODES. */
            int32_t two[2];
            s_othermode_h = (unsigned int)w0;
            s_othermode_l = (unsigned int)w1;
            s_zbuffered = (((s_othermode_l >> 4) & 1u) ||
                           ((s_othermode_l >> 5) & 1u)) ? 1 : 0;
            two[0] = (int32_t)w0;
            two[1] = (int32_t)w1;
            rdp_fifo_append(fifo, two, 2);
            break;
        }

        case F3DEX2_TEXTURE:
            /* G_TEXTURE sets the texture scale/tile/level the frontend uses
             * for emitted texcoords, and the on/off flag that selects whether
             * subsequent triangles are textured. w1 holds the S/T scale
             * (s.16 each); w0 bits select tile, max LOD level, and on. */
        {
            int on    = (int)((w0 >> 1) & 0x7f);
            int level = (int)((w0 >> 11) & 0x07);
            int tile  = (int)((w0 >> 8) & 0x07);
            unsigned int ss = (unsigned int)((w1 >> 16) & 0xffff);
            unsigned int ts = (unsigned int)(w1 & 0xffff);
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
                /* sniff Set Scissor (0xED -> 0x2d) for the S2DEX background
                 * renderer, mirroring the microcode's command splitter. */
                if (rdp_id == 0x2d)
                    s2dex_set_scissor(w0, w1);
                /* sniff Set Tile (0xF5 -> 0x35) for the wrap masks: the
                 * texture-coordinate wrap fold in gsp_triangle may shift a
                 * triangle's S/T plane only by multiples of the tile's mask
                 * period, so it needs the mask exponents of the tile the
                 * draw will sample. */
                if (rdp_id == 0x35)
                {
                    unsigned int ti = (w1 >> 24) & 7;
                    gsp->tile_mask_t[ti] = (unsigned char)((w1 >> 14) & 0xf);
                    gsp->tile_mask_s[ti] = (unsigned char)((w1 >> 4) & 0xf);
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
                     * SET_TEXTURE_IMAGE (0x3d): w1 is the image DRAM pointer
                     * and may be a segmented address (segment id in the top
                     * byte); the format/size/width fields all live in w0.
                     * The RSP resolves the segment before the command reaches
                     * the RDP, so resolve fully here -- keeping the segment id
                     * byte only works for identity-mapped segments. */
                    if (rdp_id == 0x3f || rdp_id == 0x3e || rdp_id == 0x3d)
                        two[1] = (int32_t)seg_addr_rsp(w1);
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
