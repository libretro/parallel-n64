/* Factor 5 / LucasArts custom microcode walker (Star Wars: Rogue Squadron)
 * for the angrylion HLE path.
 *
 * Rogue Squadron ships a fully custom RSP microcode with no SGI name string.
 * The task text segment is only a small loader; the graphics code proper is
 * DMA'd over IMEM from elsewhere in RDRAM as overlays, so identification
 * probes the loader's first instruction words.
 *
 * The display-list grammar is a compacted GBI 1 derivative, decoded from the
 * live IMEM of the running task (cxd4 LLE):
 *
 *   - the low opcodes 0x08..0x13 alias the classic GBI 1 handlers at
 *     0xBF..0xB4 through the same jump table (negated index), and opcodes
 *     0xC0..0xFF pass through to the RDP verbatim (G_TEXRECT carries its two
 *     extra words inline);
 *   - every display list, top-level or called, begins with an 8-byte header
 *     of RDP output-buffer pointers that the command loop never sees;
 *   - vertices are 8-byte packed records (x, y, z int16 + one spare
 *     halfword), always loaded to slot 0, transformed two per loop by the
 *     combined matrix;
 *   - matrices load into two DMEM slots: parameter bit 0 selects slot A
 *     (loaded alone), otherwise slot B is loaded and concatenated with A
 *     into the combined matrix used by the vertex transform;
 *   - triangles carry their vertex colours per-face as byte offsets into a
 *     colour list DMA'd by opcode 0x02, and (flagged) 16 bytes of inline
 *     per-vertex S/T scaled by the G_TEXTURE scale;
 *   - the triangle processor trivially rejects on the AND of the vertex
 *     outcodes masked 0x7070 (+-x, +-y, +-z; no W plane) and clips against
 *     a ratio-2 guard band with a true z+w near plane, matching the
 *     frontend's clip_near_z outcode model.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "rdp_emit_frontend.h"
#include "rdp_emit_f3dex2.h"
#include "rdp_emit_rsp.h"
#include "rdp_emit_rs.h"

extern void rdp_fifo_append(RdpFifo *f, const int32_t *words, int count);
extern void rdp_fifo_fullsync_note(void);

static unsigned char *s_rdram;
static unsigned int   s_rdram_size;

void rs_set_rdram(unsigned char *rdram) { s_rdram = rdram; }
void rs_set_rdram_size(unsigned int size) { s_rdram_size = size; }

static unsigned int rs_read_u32(unsigned int addr)
{
    unsigned int a = addr & 0x7ffffcu;
    if (!s_rdram || a + 4u > s_rdram_size)
        return 0u;
    return ((unsigned int)s_rdram[(a + 0u) ^ 3u] << 24)
         | ((unsigned int)s_rdram[(a + 1u) ^ 3u] << 16)
         | ((unsigned int)s_rdram[(a + 2u) ^ 3u] << 8)
         |  (unsigned int)s_rdram[(a + 3u) ^ 3u];
}

static unsigned int rs_read_u8(unsigned int addr)
{
    unsigned int a = addr & 0x7fffffu;
    if (!s_rdram || a >= s_rdram_size)
        return 0u;
    return s_rdram[a ^ 3u];
}

/* The loader text is identical for every Rogue Squadron task: probe its
 * first four instruction words (jal init / ori a1,0 / jal dma / nop). */
int rs_ucode_match(const unsigned char *rdram, unsigned int rdram_size,
                   unsigned int text)
{
    unsigned char *save = s_rdram;
    unsigned int   ssz  = s_rdram_size;
    unsigned int w0, w1, w2, w3;
    int ok;
    s_rdram = (unsigned char *)rdram;
    s_rdram_size = rdram_size;
    w0 = rs_read_u32(text + 0u);
    w1 = rs_read_u32(text + 4u);
    w2 = rs_read_u32(text + 8u);
    w3 = rs_read_u32(text + 12u);
    ok = (w0 == 0x0d000497u && w1 == 0x34050000u
       && w2 == 0x0d000479u && w3 == 0x00000000u);
    s_rdram = save;
    s_rdram_size = ssz;
    return ok;
}

/* --- walker state ------------------------------------------------------ */

/* GBI 1 style other-mode mirrors (partial writes splice into these). */
static unsigned int s_rs_om_h;
static unsigned int s_rs_om_l;

/* Colour list base (opcode 0x02) in RDRAM. Triangle commands index it with
 * plain byte offsets. */
static unsigned int s_rs_colors;

/* debug: running count of state (non-triangle) commands emitted */
static int s_rs_dbg_states;

/* Raw Rogue Squadron geometry-mode word (opcodes 0xB6/0xB7). */
static unsigned int s_rs_geom;

/* G_TEXTURE mirror: texture on/off gates the textured triangle variant. */
static int s_rs_tex_on;

/* Texture-coordinate scale from the opcode 0x03 parameter 0x82 state poke
 * (DMEM 0x140/0x148: an integer row and a fraction row, s and t in lanes 0
 * and 1). The triangle S/T payload is multiplied by this full s15.16 scale
 * (vmudn/vmadh at IMEM 0x144c), not by the G_TEXTURE halfword. */
static int32_t s_rs_tsc_s;
static int32_t s_rs_tsc_t;

/* Staging buffer presented to gsp_vertex as RDRAM: 32 synthesized 16-byte
 * Fast3D vertex records (byte-swapped storage, as gsp_vertex reads it). */
static unsigned char s_rs_stage[32 * 16];

static void rs_stage_put8(unsigned int off, unsigned int v)
{
    s_rs_stage[off ^ 3u] = (unsigned char)v;
}

static void rs_emit_othermode(RdpFifo *fifo)
{
    int32_t two[2];
    two[0] = (int32_t)(s_rs_om_h | (0x2fu << 24));
    two[1] = (int32_t)s_rs_om_l;
    rdp_fifo_append(fifo, two, 2);
    s_rs_dbg_states++;
}

static int rs_zbuffered(void)
{
    return (((s_rs_om_l >> 4) & 1u) || ((s_rs_om_l >> 5) & 1u)) ? 1 : 0;
}

/* Map the Rogue Squadron geometry word onto the frontend's F3DEX2-valued
 * geometry mode: bit 0 is the fog enable (the microcode's vertex loop keys
 * its fog-alpha block on it; the 0x2000 winding cull is applied by
 * rs_cull before the shared triangle path), and 0x1000 disables the
 * z-buffered triangle variant (the LLE stream flips between the Z and
 * non-Z shade/texture triangle opcodes in exact anticorrelation with it;
 * the menu's render modes never touch the othermode z bits). */
static void rs_sync_geom(GSPState *gsp)
{
    unsigned int m = 0x00200004u;      /* shade + smooth: always on */
    if (s_rs_geom & 0x0001u)
        m |= 0x00010000u;              /* G_FOG */
    if (!(s_rs_geom & 0x1000u))
        m |= 0x00000001u;              /* G_ZBUFFER (0x1000 disables z) */
    gsp_set_geometry_mode(gsp, m);
}

/* Load n 8-byte packed vertices from addr into slots 0..n-1 through the
 * shared transform pipeline, via synthesized 16-byte Fast3D records. */
static void rs_vertex(GSPState *gsp, unsigned int addr, int n)
{
    int i;
    if (n > 32)
        n = 32;
    for (i = 0; i < n; i++)
    {
        unsigned int src = addr + (unsigned int)i * 8u;
        unsigned int dst = (unsigned int)i * 16u;
        unsigned int b;
        for (b = 0u; b < 6u; b++)
            rs_stage_put8(dst + b, rs_read_u8(src + b));
        rs_stage_put8(dst + 6u,  0u);
        rs_stage_put8(dst + 7u,  0u);
        rs_stage_put8(dst + 8u,  0u);   /* s */
        rs_stage_put8(dst + 9u,  0u);
        rs_stage_put8(dst + 10u, 0u);   /* t */
        rs_stage_put8(dst + 11u, 0u);
        rs_stage_put8(dst + 12u, 0xffu); /* rgba: white until the face says */
        rs_stage_put8(dst + 13u, 0xffu);
        rs_stage_put8(dst + 14u, 0xffu);
        rs_stage_put8(dst + 15u, 0xffu);
    }
    gsp_vertex(gsp, s_rs_stage, 0u, n, 0);
}

/* Per-face vertex attribute injection: colour words from the colour list
 * and (optionally) inline S/T for the triangle's corners. */
static void rs_poke_color(GSPState *gsp, int slot, unsigned int listoff)
{
    unsigned int c = rs_read_u32(s_rs_colors + listoff);
    gsp_modify_vertex(gsp, slot, 0x10u, c);
}

/* Rogue Squadron's winding cull (tri processor, IMEM 0x1868..0x18f0):
 * with A, B, C the command's three vertices in order, the RSP computes
 *   cross = (C-A).x * (B-A).y - (B-A).x * (C-A).y
 * on the STORED S13.2 screen halfwords, with saturating s16 deltas (VSUB)
 * and the vmudh/vmadh accumulator's clamped s16 mid slice as the result
 * register; the sign and zero tests run on that clamped 16-bit value.
 * A zero cross always rejects; a negative cross rejects when geometry
 * bit 0x2000 is set. Returns nonzero when the triangle must be dropped. */
static int rs_cull(const GSPState *gsp, int i0, int i1, int i2)
{
    int32_t ax, ay, bx, by, cx, cy;
    int32_t d1x, d1y, d2x, d2y;
    int64_t acc;
    int32_t cross;
    const GSPVertex *a = &gsp->vtx[i0];
    const GSPVertex *b = &gsp->vtx[i1];
    const GSPVertex *c = &gsp->vtx[i2];
    if (!a->rsp_ok || !b->rsp_ok || !c->rsp_ok)
        return 0;                      /* behind-eye path: leave to clip */
    ax = a->scr_x >> 14; ay = a->scr_y >> 14;
    bx = b->scr_x >> 14; by = b->scr_y >> 14;
    cx = c->scr_x >> 14; cy = c->scr_y >> 14;
#define RS_SSAT(v) ((v) > 32767 ? 32767 : ((v) < -32768 ? -32768 : (v)))
    d1x = RS_SSAT(bx - ax); d1y = RS_SSAT(by - ay);
    d2x = RS_SSAT(cx - ax); d2y = RS_SSAT(cy - ay);
#undef RS_SSAT
    acc = (int64_t)d2x * d1y - (int64_t)d1x * d2y;
    if (acc > 32767)
        cross = 32767;
    else if (acc < -32768)
        cross = -32768;
    else
        cross = (int32_t)acc;
    if (cross == 0)
        return 1;
    if ((getenv("RS_CPOS") ? cross > 0 : cross < 0) && (s_rs_geom & 0x2000u))
        return 1;
    return 0;
}

static void rs_poke_st(GSPState *gsp, int slot, unsigned int st)
{
    int16_t rs = (int16_t)((st >> 16) & 0xffffu);
    int16_t rt = (int16_t)(st & 0xffffu);
    int32_t os = (int32_t)(((int64_t)rs * (int64_t)s_rs_tsc_s) >> 16);
    int32_t ot = (int32_t)(((int64_t)rt * (int64_t)s_rs_tsc_t) >> 16);
    gsp_set_vertex_st(gsp, slot, (int)os, (int)ot);
}

void rs_run_dl(GSPState *gsp, RdpFifo *fifo, unsigned int dl_addr)
{
    unsigned int stack[20];
    int sp = 0;
    unsigned int pc;
    unsigned int page;
    unsigned int pend;
    int guard = 0;

    s_rs_om_h = 0x2fu << 24;
    s_rs_om_l = 0u;
    s_rs_colors = 0u;
    s_rs_geom = 0u;
    s_rs_tex_on = 0;
    s_rs_dbg_states = 0;
    s_rs_tsc_s = 0x10000;
    s_rs_tsc_t = 0x10000;

    gsp->clip_near_z = 1;
    gsp->clip_ratio = 2;
    gsp->mvp_trans_last = 1;
    gsp->viewport.rs_model = 0;
    /* Triangle-writer constants from the live IMEM (0x1928..0x19c8):
     * edge dX scaled by v30[4] == 0x1000 with the y reciprocal scaled by
     * v31[4] == 0x20, and the slope fraction masked to v31[1] == 0xfff8
     * before the anchor back-walk. With these, 807 of 956 aligned menu
     * triangles carry a byte-exact edge coefficient block. */
    gsp->viewport.tri_dx_scale = 0x1000;
    gsp->viewport.tri_idy_scale = 0x20;
    gsp->viewport.tri_frac_mask = (int32_t)0xfff8;
    /* Rogue Squadron's reciprocal doubles the vrcph/vrcpl estimate and
     * refines it as r' = r * (2 - r * w) (2.0 staged in DMEM 0x50) --
     * algebraically the F3DEX2 r * (4 - 4rw) form, whose default model
     * lands closest here; the exact staging is left for the numeric
     * exactness pass. */

    /* The display list is a linked chain of 0x108-byte pages: an 8-byte
     * header whose first word points at the next page, then 0x100 bytes of
     * commands (zero-padded; opcode 0 is a nop). The microcode stages one
     * page at a time in DMEM 0x170 and refills when the cursor runs off the
     * end; opcode 0xB5 jumps to the next page early. Calls and branches
     * land on the head of another page chain. */
    page = dl_addr & 0xfffffful;
    pc = page + 8u;
    pend = page + 0x108u;

    while (guard++ < 400000)
    {
        unsigned int w0, w1, op;
        unsigned int size = 8u;

        if (pc >= pend)
        {
            /* ran off the page: follow the header's next-page link */
            page = rs_read_u32(page) & 0xfffffful;
            if (page == 0u)
                return;
            pc = page + 8u;
            pend = page + 0x108u;
        }

        w0 = rs_read_u32(pc);
        w1 = rs_read_u32(pc + 4u);
        op = w0 >> 24;

        if (getenv("RS_ALLDBG"))
            fprintf(stderr, "g=%d pc=%06x op=%02x w0=%08x w1=%08x\n",
                    s_rs_dbg_states, pc, op, w0, w1);

        if (op >= 0xc0u)
        {
            /* Raw RDP passthrough. G_TEXRECT carries two extra words. */
            int32_t words[6];
            int nw = 2;
            words[0] = (int32_t)w0;
            words[1] = (int32_t)w1;
            if (op == 0xe4u)
            {
                words[2] = (int32_t)rs_read_u32(pc + 8u);
                words[3] = (int32_t)rs_read_u32(pc + 12u);
                nw = 4;
                size = 24u;
                /* the RSP splits texrect into cmd + two half words; the
                 * live stream shows all 16 payload bytes forwarded */
                words[2] = (int32_t)rs_read_u32(pc + 8u);
                words[3] = (int32_t)rs_read_u32(pc + 12u);
                words[4] = (int32_t)rs_read_u32(pc + 16u);
                words[5] = (int32_t)rs_read_u32(pc + 20u);
                nw = 6;
            }
            if (op == 0xdfu || op == 0xe9u)
                rdp_fifo_fullsync_note();
            rdp_fifo_append(fifo, words, nw);
            s_rs_dbg_states += nw / 2;
            pc += size;
            continue;
        }

        switch (op)
        {
        case 0x00u:
        case 0x0au:                    /* microcode overlay load */
        case 0xbdu:
            break;

        case 0x05u:                    /* vertex generator overlay */
            if ((w0 >> 8) & 0x2u)
            {
                /* Quad-cell synthesizer (overlay 0x0c, entry 0x1f64 when
                 * parameter byte 2 has bit 1 set): four 8-byte vertex
                 * records are generated into slots 0..3 from an X/Z grid
                 * cell of side W with per-corner heights, then transformed
                 * like an opcode 0x04 load. Heights are (base << 4) + dy,
                 * with the four dy halfwords in w1 and the following word.
                 * X, base, Z, W live at +32..+38 of the 40-byte command. */
                unsigned int X  = (rs_read_u8(pc + 32u) << 8) | rs_read_u8(pc + 33u);
                unsigned int Yb = ((rs_read_u8(pc + 34u) << 8) | rs_read_u8(pc + 35u)) << 4;
                unsigned int Z  = (rs_read_u8(pc + 36u) << 8) | rs_read_u8(pc + 37u);
                unsigned int Wd = (rs_read_u8(pc + 38u) << 8) | rs_read_u8(pc + 39u);
                unsigned int dy0 = (w1 >> 16) & 0xffffu;
                unsigned int dy1 = w1 & 0xffffu;
                unsigned int w2 = rs_read_u32(pc + 8u);
                unsigned int dy2 = (w2 >> 16) & 0xffffu;
                unsigned int dy3 = w2 & 0xffffu;
                unsigned int cell[4][3];
                int k;
                cell[0][0] = X;      cell[0][1] = Yb + dy0; cell[0][2] = Z;
                cell[1][0] = X + Wd; cell[1][1] = Yb + dy1; cell[1][2] = Z;
                cell[2][0] = X;      cell[2][1] = Yb + dy2; cell[2][2] = Z + Wd;
                cell[3][0] = X + Wd; cell[3][1] = Yb + dy3; cell[3][2] = Z + Wd;
                for (k = 0; k < 4; k++)
                {
                    unsigned int dst = (unsigned int)k * 16u;
                    rs_stage_put8(dst + 0u, cell[k][0] >> 8);
                    rs_stage_put8(dst + 1u, cell[k][0]);
                    rs_stage_put8(dst + 2u, cell[k][1] >> 8);
                    rs_stage_put8(dst + 3u, cell[k][1]);
                    rs_stage_put8(dst + 4u, cell[k][2] >> 8);
                    rs_stage_put8(dst + 5u, cell[k][2]);
                    rs_stage_put8(dst + 6u,  0u);
                    rs_stage_put8(dst + 7u,  0u);
                    rs_stage_put8(dst + 8u,  0u);
                    rs_stage_put8(dst + 9u,  0u);
                    rs_stage_put8(dst + 10u, 0u);
                    rs_stage_put8(dst + 11u, 0u);
                    rs_stage_put8(dst + 12u, 0xffu);
                    rs_stage_put8(dst + 13u, 0xffu);
                    rs_stage_put8(dst + 14u, 0xffu);
                    rs_stage_put8(dst + 15u, 0xffu);
                }
                gsp_vertex(gsp, s_rs_stage, 0u, 4, 0);
                /* The overlay draws the cell itself: two triangles over
                 * the four corners, with the per-corner colours inlined
                 * at +12..+27 of the command. */
                {
                    unsigned int c0 = rs_read_u32(pc + 12u);
                    unsigned int c1 = rs_read_u32(pc + 16u);
                    unsigned int c2 = rs_read_u32(pc + 20u);
                    unsigned int c3 = rs_read_u32(pc + 24u);
                    int32_t cmd[GSP_TRI_CMD_WORDS];
                    int nw;
                    gsp_modify_vertex(gsp, 0, 0x10u, c0);
                    gsp_modify_vertex(gsp, 1, 0x10u, c1);
                    gsp_modify_vertex(gsp, 2, 0x10u, c2);
                    gsp_modify_vertex(gsp, 3, 0x10u, c3);
                    /* Corner order of the two triangles (validated
                     * against the LLE stream: 808/940 header-exact vs
                     * 692 for the other diagonal). */
                    if (!rs_cull(gsp, 2, 0, 3))
                    {
                        nw = gsp_triangle(gsp, cmd, 2, 0, 3,
                                          s_rs_tex_on, rs_zbuffered());
                        if (nw > 0)
                            rdp_fifo_append(fifo, cmd, nw);
                    }
                    if (!rs_cull(gsp, 0, 1, 3))
                    {
                        nw = gsp_triangle(gsp, cmd, 0, 1, 3,
                                          s_rs_tex_on, rs_zbuffered());
                        if (nw > 0)
                            rdp_fifo_append(fifo, cmd, nw);
                    }
                }
                size = 40u;
            }
            else
            {
                /* Terrain patch (overlay chain 0x0c -> 0x14 -> 0x18 ->
                 * 0x10 -> 0x1c): a coarse height/colour grid is sampled
                 * from two streams, optionally midpoint-refined, built
                 * into an N x N vertex grid over the cell at +32..+38,
                 * given generated texture coordinates stepped by the
                 * halfword at +30, and drawn as quads.
                 *
                 * The decoder samples heights as s8 << 4 with a column
                 * stride of (byte3 << 1) bytes and a row stride of
                 *(byte3 << byte1) bytes; colours are 4-byte entries on a
                 * k0-scaled row walk, averaged between grid points when
                 * the sample lands on an upsampled (masked) position.
                 * The grid dimension is ((byte1 - 1) >> byte3) + 1.
                 *
                 * The overlays then midpoint-refine odd rows/columns and
                 * temporally lerp every sample against the values the
                 * PREVIOUS task left in the persistent DMEM work buffers
                 * (0xCB4/0xB70) using per-row factors at +20..+28 -- the
                 * animated drifting-dunes effect. That cross-task state
                 * is not modelled yet, so each patch renders its raw
                 * decoded frame (verified sample-exact for unrefined
                 * rows against the LLE DMEM). */
                unsigned int b1 = (w0 >> 16) & 0xffu;
                unsigned int b2 = (w0 >> 8) & 0xffu;
                unsigned int b3 = w0 & 0xffu;
                unsigned int srcA = rs_read_u32(pc + 8u) & 0xfffffful;
                unsigned int srcB = rs_read_u32(pc + 12u) & 0xfffffful;
                unsigned int X  = (rs_read_u8(pc + 32u) << 8) | rs_read_u8(pc + 33u);
                unsigned int Yb = (((rs_read_u8(pc + 34u) << 8) | rs_read_u8(pc + 35u)) << 4) & 0xffffu;
                unsigned int Z  = (rs_read_u8(pc + 36u) << 8) | rs_read_u8(pc + 37u);
                unsigned int Wd = (rs_read_u8(pc + 38u) << 8) | rs_read_u8(pc + 39u);
                unsigned int stp = (rs_read_u8(pc + 30u) << 8) | rs_read_u8(pc + 31u);
                int n = (b1 >= 1u) ? (int)(((b1 - 1u) >> b3) + 1u) : 1;
                int t0 = (int)(b3 << 1);
                int t1 = (int)(b3 << b1);
                unsigned int t6h = (b2 & 0xf0u) >> 4;
                int hgt[8][8];
                unsigned int col[8][8];
                int xi, zi;
                if (n > 8) n = 8;
                /* height/colour sampling, exact integer walk */
                {
                    int t6s = (int)t6h - (int)b3;
                    unsigned int t7 = (t6s >= 0) ? (0x100u >> t6s)
                                                 : (0x100u << (unsigned int)(-t6s));
                    unsigned int k1m = (t6s > 0)
                        ? ((~(0xffffu << t6s)) & 0xffffu) : 0u;
                    unsigned int k0 = (((t6h >> (b1 - 1u)) + 1u) << 2);
                    unsigned int t9 = 0u, a3o = 0u;
                    for (zi = 0; zi < n; zi++)
                    {
                        unsigned int t8 = ((t9 & 0xff00u) * k0) & 0xffffffffu;
                        unsigned int a2o = a3o;
                        for (xi = 0; xi < n; xi++)
                        {
                            unsigned int vsel = (unsigned int)xi & k1m;
                            unsigned int hsel = (unsigned int)zi & k1m;
                            unsigned int o0 = ((t8 >> 8) << 2);
                            if ((vsel | hsel) == 0u)
                            {
                                col[zi][xi] = rs_read_u32(srcB + o0
                                    + (vsel ? 4u : 0u) + (hsel ? k0 : 0u));
                            }
                            else
                            {
                                /* upsampled position: average of the two
                                 * bracketing grid entries */
                                unsigned int o1 = o0
                                    + 2u * ((vsel ? 4u : 0u) + (hsel ? k0 : 0u));
                                unsigned int c0 = rs_read_u32(srcB + o0);
                                unsigned int c1 = rs_read_u32(srcB + o1);
                                unsigned int k, r = 0u;
                                for (k = 0u; k < 4u; k++)
                                {
                                    unsigned int x0 = (c0 >> (k * 8u)) & 0xffu;
                                    unsigned int x1 = (c1 >> (k * 8u)) & 0xffu;
                                    r |= (((x0 + x1) >> 1) & 0xffu) << (k * 8u);
                                }
                                col[zi][xi] = r;
                            }
                            {
                                unsigned int hb = rs_read_u8(srcA + a2o);
                                int hs = (int)(int8_t)(unsigned char)hb;
                                hgt[zi][xi] = (hs << 4) & 0xffff;
                            }
                            a2o += (unsigned int)t0;
                            t8 = (t8 + t7) & 0xffffffffu;
                        }
                        t9 = (t9 + t7) & 0xffffffffu;
                        a3o += (unsigned int)t1;
                    }
                }
                /* vertex grid, column-major like the microcode */
                if (n >= 2 && n * n <= 25)
                {
                    int idx = 0;
                    for (xi = 0; xi < n; xi++)
                        for (zi = 0; zi < n; zi++)
                        {
                            unsigned int vx = (X + (unsigned int)xi * Wd) & 0xffffu;
                            unsigned int vy = (Yb + (unsigned int)hgt[zi][xi]) & 0xffffu;
                            unsigned int vz = (Z + (unsigned int)zi * Wd) & 0xffffu;
                            unsigned int dst = (unsigned int)idx * 16u;
                            rs_stage_put8(dst + 0u, vx >> 8);
                            rs_stage_put8(dst + 1u, vx);
                            rs_stage_put8(dst + 2u, vy >> 8);
                            rs_stage_put8(dst + 3u, vy);
                            rs_stage_put8(dst + 4u, vz >> 8);
                            rs_stage_put8(dst + 5u, vz);
                            rs_stage_put8(dst + 6u, 0u);
                            rs_stage_put8(dst + 7u, 0u);
                            rs_stage_put8(dst + 8u, 0u);
                            rs_stage_put8(dst + 9u, 0u);
                            rs_stage_put8(dst + 10u, 0u);
                            rs_stage_put8(dst + 11u, 0u);
                            rs_stage_put8(dst + 12u, 0xffu);
                            rs_stage_put8(dst + 13u, 0xffu);
                            rs_stage_put8(dst + 14u, 0xffu);
                            rs_stage_put8(dst + 15u, 0xffu);
                            idx++;
                        }
                    gsp_vertex(gsp, s_rs_stage, 0u, n * n, 0);
                    for (xi = 0; xi < n; xi++)
                        for (zi = 0; zi < n; zi++)
                        {
                            int sl = xi * n + zi;
                            gsp_modify_vertex(gsp, sl, 0x10u, col[zi][xi]);
                            gsp_set_vertex_st(gsp, sl,
                                (int)(int16_t)((unsigned int)xi * stp),
                                (int)(int16_t)((unsigned int)zi * stp));
                        }
                    /* quads between adjacent grid columns/rows */
                    {
                        int32_t cmd[GSP_TRI_CMD_WORDS];
                        int nw;
                        for (xi = 0; xi < n - 1; xi++)
                            for (zi = 0; zi < n - 1; zi++)
                            {
                                int q0 = xi * n + zi;
                                int q1 = (xi + 1) * n + zi;
                                int q2 = xi * n + zi + 1;
                                int q3 = (xi + 1) * n + zi + 1;
                                if (!rs_cull(gsp, q2, q0, q3))
                                {
                                    nw = gsp_triangle(gsp, cmd, q2, q0, q3,
                                                      s_rs_tex_on, rs_zbuffered());
                                    if (nw > 0)
                                        rdp_fifo_append(fifo, cmd, nw);
                                }
                                if (!rs_cull(gsp, q0, q1, q3))
                                {
                                    nw = gsp_triangle(gsp, cmd, q0, q1, q3,
                                                      s_rs_tex_on, rs_zbuffered());
                                    if (nw > 0)
                                        rdp_fifo_append(fifo, cmd, nw);
                                }
                            }
                    }
                }
                size = 40u;
            }
            break;

        case 0xb5u:                    /* jump to the next page early */
            pc = pend;
            continue;

        case 0x01u:                    /* matrix load */
        {
            unsigned int param = (w0 >> 16) & 0xffu;
            unsigned int addr = w1 & 0x7ffffful;
            if (addr + 64u > s_rdram_size)
                break;
            if (param & 1u)
            {
                /* Parameter bit 0 set loads DMEM slot 0x590, the RIGHT
                 * factor of the concat (the projection side; the branch
                 * delay slot at IMEM 0x1514 preloads 0x5d0 and the
                 * fall-through at 0x1518 overwrites it for this case) and
                 * returns without touching the combined matrix -- freeze
                 * the frontend's combine so no stale-side recombine runs
                 * before the next full concat. */
                gsp_matrix_load(gsp, s_rdram, addr, 1, 1, 0);
                gsp->combined_valid = 1;
            }
            else
            {
                /* Parameter bit 0 clear loads slot 0x5d0, the LEFT
                 * (vertex-side) factor, and concats 0x5d0 x 0x590 into
                 * the combined slot (verified against the truncated menu
                 * task: DMEM 0x610 == slot5d0 x slot590). */
                gsp_matrix_load(gsp, s_rdram, addr, 0, 1, 0);
                gsp_combine_matrices(gsp);
            }
            break;
        }

        case 0x02u:                    /* per-face colour list */
            s_rs_colors = w1 & 0xfffffful;
            break;

        case 0x03u:                    /* 16-byte state poke (movemem) */
        {
            unsigned int param = (w0 >> 16) & 0xffu;
            if (param == 0x82u)
            {
                /* 16-byte poke of the texture-scale rows: +0..7 integer
                 * lanes, +8..15 fraction lanes; s in lane 0, t in lane 1. */
                int32_t si = (int32_t)(int16_t)((rs_read_u8(pc + 8u) << 8)
                                               | rs_read_u8(pc + 9u));
                int32_t ti = (int32_t)(int16_t)((rs_read_u8(pc + 10u) << 8)
                                               | rs_read_u8(pc + 11u));
                int32_t sf = (int32_t)((rs_read_u8(pc + 16u) << 8)
                                       | rs_read_u8(pc + 17u));
                int32_t tf = (int32_t)((rs_read_u8(pc + 18u) << 8)
                                       | rs_read_u8(pc + 19u));
                s_rs_tsc_s = (si << 16) | sf;
                s_rs_tsc_t = (ti << 16) | tf;
            }
            if (param == 0x80u)
            {
                /* The payload is the classic 8-short vscale/vtrans block,
                 * but in raw S13.2 pixels: Rogue Squadron's microcode
                 * multiplies it into the NDC directly, without the GBI's
                 * doubled-scale convention the frontend divides back out.
                 * Feed a doubled staging copy so the shared path lands on
                 * the same screen coordinates. */
                static unsigned char vp[16];
                unsigned int k;
                for (k = 0u; k < 8u; k++)
                {
                    unsigned int hw = (rs_read_u8(pc + 8u + k * 2u) << 8)
                                    |  rs_read_u8(pc + 9u + k * 2u);
                    hw = (hw << 1) & 0xffffu;
                    vp[(k * 2u + 0u) ^ 3u] = (unsigned char)(hw >> 8);
                    vp[(k * 2u + 1u) ^ 3u] = (unsigned char)hw;
                }
                gsp_set_viewport(gsp, vp, 0u);
                gsp->viewport.rs_model = 1;
                gsp->viewport.rs_vs[0] = (int32_t)(int16_t)
                    ((rs_read_u8(pc + 8u) << 8) | rs_read_u8(pc + 9u));
                gsp->viewport.rs_vs[1] = (int32_t)(int16_t)
                    ((rs_read_u8(pc + 10u) << 8) | rs_read_u8(pc + 11u));
                gsp->viewport.rs_vs[2] = (int32_t)(int16_t)
                    ((rs_read_u8(pc + 12u) << 8) | rs_read_u8(pc + 13u));
                gsp->viewport.rs_vt[0] = (int32_t)(int16_t)
                    ((rs_read_u8(pc + 16u) << 8) | rs_read_u8(pc + 17u));
                gsp->viewport.rs_vt[1] = (int32_t)(int16_t)
                    ((rs_read_u8(pc + 18u) << 8) | rs_read_u8(pc + 19u));
                gsp->viewport.rs_vt[2] = (int32_t)(int16_t)
                    ((rs_read_u8(pc + 20u) << 8) | rs_read_u8(pc + 21u));
            }
            size = 24u;
            break;
        }

        case 0x04u:                    /* packed vertex load, slot 0 */
        {
            int n = (int)((w0 >> 10) & 0x3fu);
            rs_vertex(gsp, w1 & 0xfffffful, n);
            break;
        }

        case 0x06u:                    /* call display list */
            if (sp < 10)
            {
                stack[sp * 2 + 0] = pc + 8u;
                stack[sp * 2 + 1] = page;
                sp++;
            }
            page = w1 & 0xfffffful;
            pc = page + 8u;
            pend = page + 0x108u;
            continue;

        case 0x07u:                    /* branch display list */
            page = w1 & 0xfffffful;
            pc = page + 8u;
            pend = page + 0x108u;
            continue;

        case 0x08u: case 0xbfu:        /* triangle */
        case 0xb4u:                    /* quad */
        {
            int i0 = (int)(((w1 >> 16) & 0xffu) / 5u);
            int i1 = (int)(((w1 >> 8) & 0xffu) / 5u);
            int i2 = (int)((w1 & 0xffu) / 5u);
            int i3 = (int)(((w1 >> 24) & 0xffu) / 5u);
            unsigned int aux = rs_read_u32(pc + 8u);
            int quad = (op == 0xb4u);
            int32_t cmd[GSP_TRI_CMD_WORDS];
            int nw;
            size = 16u;
            /* aux byte order: +9 -> v0, +10 -> v1, +11 -> v2, +8 -> v3 */
            rs_poke_color(gsp, i0, (aux >> 16) & 0xffu);
            rs_poke_color(gsp, i1, (aux >> 8) & 0xffu);
            rs_poke_color(gsp, i2, aux & 0xffu);
            if (quad && (w0 & 4u))
                rs_poke_color(gsp, i3, (aux >> 24) & 0xffu);
            if (w0 & 2u)
            {
                unsigned int st0 = rs_read_u32(pc + 16u);
                unsigned int st1 = rs_read_u32(pc + 20u);
                unsigned int st2 = rs_read_u32(pc + 24u);
                unsigned int st3 = rs_read_u32(pc + 28u);
                rs_poke_st(gsp, i0, st0);
                rs_poke_st(gsp, i1, st1);
                rs_poke_st(gsp, i2, st2);
                if (quad)
                    rs_poke_st(gsp, i3, st3);
                size += 16u;
            }
            if (getenv("RS_GAPDBG"))
                fprintf(stderr, "g=%d pc=%06x t=(%d,%d,%d,%d) cull=%d,%d\n",
                        s_rs_dbg_states, pc, i0, i1, i2, i3,
                        rs_cull(gsp, i0, i1, i2),
                        quad ? rs_cull(gsp, i0, i2, i3) : -1);
            if (!rs_cull(gsp, i0, i1, i2))
            {
                nw = gsp_triangle(gsp, cmd, i0, i1, i2,
                                  s_rs_tex_on, rs_zbuffered());
                if (nw > 0)
                    rdp_fifo_append(fifo, cmd, nw);
            }
            if (quad && !rs_cull(gsp, i0, i2, i3))
            {
                nw = gsp_triangle(gsp, cmd, i0, i2, i3,
                                  s_rs_tex_on, rs_zbuffered());
                if (nw > 0)
                    rdp_fifo_append(fifo, cmd, nw);
            }
            break;
        }

        case 0x09u: case 0xbeu:        /* othermode_h masked write */
        case 0xb3u:                    /* othermode_l masked write */
        {
            /* new = (mask & old) | w1, mask in the following word; the
             * handler emits the merged pair immediately. */
            unsigned int mask = rs_read_u32(pc + 8u);
            if (op == 0xb3u)
                s_rs_om_l = (mask & s_rs_om_l) | w1;
            else
                s_rs_om_h = ((mask & s_rs_om_h) | w1) | (0x2fu << 24);
            rs_emit_othermode(fifo);
            rs_sync_geom(gsp);
            size = 16u;
            break;
        }

        case 0x0bu: case 0xbcu:        /* moveword */
        {
            unsigned int index = w0 & 0xffu;
            unsigned int fm, fo;
            if (index == 0x08u)
            {
                fm = (w1 >> 16) & 0xffffu;
                fo = w1 & 0xffffu;
                gsp_set_fog(gsp, (int)(int16_t)fm, (int)(int16_t)fo);
            }
            else if (index == 0x0eu)
            {
                gsp_set_persp_norm(gsp, w1 & 0xffffu);
            }
            break;
        }

        case 0x0cu: case 0xbbu:        /* G_TEXTURE (GBI 1) */
        {
            unsigned int on = w0 & 0xffu;
            unsigned int tile = (w0 >> 8) & 7u;
            unsigned int level = (w0 >> 11) & 7u;
            s_rs_tex_on = on ? 1 : 0;
            gsp_set_texture(gsp, (w1 >> 16) & 0xffffu, w1 & 0xffffu,
                            (int)tile, (int)level, 0, 0);
            break;
        }

        case 0x0du: case 0xbau:        /* G_SETOTHERMODE_H (GBI 1) */
        case 0x0eu: case 0xb9u:        /* G_SETOTHERMODE_L (GBI 1) */
        {
            unsigned int length = w0 & 0xffu;
            unsigned int shift  = (w0 >> 8) & 0xffu;
            unsigned int mask;
            if (length >= 32u)
                mask = 0xffffffffu;
            else
                mask = ((1u << length) - 1u) << shift;
            if (op == 0x0du || op == 0xbau)
                s_rs_om_h = (s_rs_om_h & ~mask)
                          | (w1 & mask) | (0x2fu << 24);
            else
                s_rs_om_l = (s_rs_om_l & ~mask) | (w1 & mask);
            rs_emit_othermode(fifo);
            rs_sync_geom(gsp);
            break;
        }

        case 0x0fu: case 0xb8u:        /* end display list */
            if (sp > 0)
            {
                sp--;
                pc = stack[sp * 2 + 0];
                page = stack[sp * 2 + 1];
                pend = page + 0x108u;
                continue;
            }
            return;

        case 0xb6u:                    /* clear geometry mode */
            s_rs_geom &= ~w1;
            rs_sync_geom(gsp);
            break;

        case 0xb7u:                    /* set geometry mode */
            s_rs_geom |= w1;
            rs_sync_geom(gsp);
            break;

        default:
            break;
        }

        pc += size;
    }
}
