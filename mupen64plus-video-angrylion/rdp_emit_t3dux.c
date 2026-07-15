/* T3DUX (Turbo3D UX) microcode walker for the angrylion HLE path.
 *
 * T3DUX is Yasumoto's compact Turbo3D display-list format (data-segment tag
 * "T3DUX-0.85 Y.Yasumoto Nintendo"), used by Last Legion UX and the Toukon
 * Road games. It does not use the Fast3D/F3DEX command grammar at all: the
 * task's display list is a flat array of six-word objects, each carrying five
 * segment pointers
 *
 *     pgstate  global-state block (othermode, 16 segment bases, viewport,
 *              and an embedded raw-RDP command list)
 *     pstate   per-object state (render/geometry/texture flags, othermode,
 *              an optional force-matrix, triangle count, and its own RDP list)
 *     pvtx     vertex array (8-byte y,x,flag,z records)
 *     ptri     triangle array (8-byte T3DUXTriN records: flag,v2,v1,v0 then
 *              pal,v2tex,v1tex,v0tex)
 *     pcol     combined colour + texcoord array (4-byte a,b,g,r colours that
 *              the vertex load consumes, and 4-byte s,t texcoords the tris
 *              index into)
 *
 * plus a sixth reserved word. A null pstate terminates the list. The object
 * loop mirrors GLideN64's RunT3DUX; geometry runs through the shared
 * transform/emit pipeline (gsp_vertex_t3dux + gsp_triangle) and the embedded
 * RDP lists are forwarded to the FIFO the same way the F3DEX2 walker forwards
 * its RDP-passthrough opcodes. Verified against the cxd4 LLE RSP, which
 * renders this ucode natively.
 */

#include <stdint.h>
#include "rdp_emit_frontend.h"
#include "rdp_emit_f3dex2.h"
#include "rdp_emit_rsp.h"

/* From rdp_emit_f3dex2.c: FIFO append and RSP-style segment resolution. */
extern void rdp_fifo_append(RdpFifo *f, const int32_t *words, int count);
extern unsigned int gsp_seg_addr_rsp(unsigned int w1);
extern void rdp_fifo_fullsync_note(void);
extern void gsp_set_vertex_rgba(GSPState *s, int idx,
                                int r, int g, int b, int a);

/* Emit a SET_OTHER_MODES (RDP 0x2f) pair to the FIFO from the T3DUX state's
 * othermode0/othermode1 words and update the depth-test sniff. othermode0
 * carries the mode-high field in its low 24 bits; the RDP command word is
 * 0x2f in the opcode byte with that field, and word1 is othermode1. */
static void t3dux_emit_othermode(GSPState *gsp, RdpFifo *fifo,
                                 unsigned int oh, unsigned int ol)
{
    int32_t two[2];
    int zc = (int)((ol >> 4) & 1);
    int zu = (int)((ol >> 5) & 1);
    two[0] = (int32_t)((0x2fu << 24) | (oh & 0x00ffffffu));
    two[1] = (int32_t)ol;
    rdp_fifo_append(fifo, two, 2);
    gsp->t3d_zbuffered = (zc || zu) ? 1 : 0;
}

static unsigned char *s_rdram;
static unsigned int   s_rdram_size;

/* T3DUX keeps its own 16-entry segment table (loaded from the global-state
 * block), independent of the F3DEX2 G_MW_SEGMENT table. */
static unsigned int s_t3d_seg[16];

/* The most recent SETTILE forwarded from an embedded RDP list; T3DUX reissues
 * it with the per-triangle palette merged in, matching the reference. */
static unsigned int s_settile_w0;
static unsigned int s_settile_w1;

void t3dux_set_rdram(unsigned char *rdram) { s_rdram = rdram; }
void t3dux_set_rdram_size(unsigned int size) { s_rdram_size = size; }

/* Detect the T3DUX microcode from the "T3DUX" tag in its data segment. The
 * data segment is passed as a physical base; the reference builds carry the
 * name string "T3DUX-0.85 Y.Yasumoto Nintendo" a short distance in. Scan the
 * first part of the segment for the five-byte "T3DUX" signature. */
int t3dux_ucode_match(const unsigned char *rdram, unsigned int rdram_size,
                      unsigned int data_seg)
{
    static const unsigned char sig[5] = { 'T', '3', 'D', 'U', 'X' };
    unsigned int a;
    if (rdram == 0 || data_seg == 0 || data_seg >= rdram_size)
        return 0;
    for (a = data_seg; a + 5u <= rdram_size && a < data_seg + 0x800u; ++a)
    {
        int k, ok = 1;
        for (k = 0; k < 5; ++k)
        {
            if (rdram[(a + (unsigned int)k) ^ 3u] != sig[k])
            {
                ok = 0;
                break;
            }
        }
        if (ok)
            return 1;
    }
    return 0;
}

static unsigned int rd_u32_be(unsigned int addr)
{
    if (s_rdram == 0 || addr + 4u > s_rdram_size)
        return 0u;
    return ((unsigned int)s_rdram[(addr + 0u) ^ 3u] << 24)
         | ((unsigned int)s_rdram[(addr + 1u) ^ 3u] << 16)
         | ((unsigned int)s_rdram[(addr + 2u) ^ 3u] << 8)
         |  (unsigned int)s_rdram[(addr + 3u) ^ 3u];
}

static int rd_u8(unsigned int addr)
{
    /* Raw (unswapped) byte access. The T3DUX struct layouts here follow the
     * GLideN64 reference declarations, which are raw overlays on the host's
     * byteswapped RDRAM -- their field order already encodes the in-word
     * swap. Reading the same declaration offsets with a ^3-swapped access
     * lands every u8 on the wrong lane (vtxCount picked up texmode, triCount
     * picked up a zero padding byte, and every object's triangles were
     * dropped), so byte fields at reference offsets must be read raw. */
    if (s_rdram == 0 || addr >= s_rdram_size)
        return 0;
    return (int)s_rdram[addr];
}

/* Resolve a T3DUX segmented address through the walker's own segment table
 * (segment id in the top byte, offset in the low 24 bits). */
static unsigned int t3d_seg_addr(unsigned int a)
{
    unsigned int seg = (a >> 24) & 0xfu;
    return (s_t3d_seg[seg] + (a & 0x00ffffffu)) & 0x00ffffffu;
}

/* Forward an embedded raw-RDP command list to the FIFO. The list is already
 * in RDP command format (opcode 0xc0|id in w0 bits 31..24); TEXRECT and
 * SETTILE carry extra words exactly as the reference splitter reads them. */
static void t3dux_process_rdp(GSPState *gsp, RdpFifo *fifo, unsigned int cmds)
{
    unsigned int addr;
    unsigned int w0, w1;
    if (cmds == 0u)
        return;
    addr = t3d_seg_addr(cmds);
    if (addr == 0u)
        return;

    w0 = rd_u32_be(addr); addr += 4u;
    w1 = rd_u32_be(addr); addr += 4u;
    while ((w0 | w1) != 0u)
    {
        int cmd = (int)((w0 >> 24) & 0xffu);
        int rdp_id = cmd & 0x3f;
        int extra = 0;

        if (cmd == 0xe4 || cmd == 0xe5)         /* G_TEXRECT / G_TEXRECTFLIP */
            extra = 2;                          /* two trailing words follow */
        else if (cmd == 0xf5)                   /* G_SETTILE: latch for tris */
        {
            s_settile_w0 = w0;
            s_settile_w1 = w1;
        }

        /* Sniff Set Other Modes for the depth enables so the tri emit picks
         * the Z variant, mirroring the F3DEX2 passthrough. */
        if (rdp_id == 0x2f)
        {
            int zc = (int)((w1 >> 4) & 1);
            int zu = (int)((w1 >> 5) & 1);
            gsp->t3d_zbuffered = (zc || zu) ? 1 : 0;
        }

        if (rdp_id == 0x29)                     /* G_RDPFULLSYNC */
            rdp_fifo_fullsync_note();

        if (rdp_id >= 0x24 && rdp_id <= 0x3f
            && rdp_id != 0x31 && rdp_id != 0x29)
        {
            int32_t two[2];
            two[0] = (int32_t)w0;
            if (rdp_id == 0x3f || rdp_id == 0x3e || rdp_id == 0x3d)
                two[1] = (int32_t)gsp_seg_addr_rsp(w1);
            else
                two[1] = (int32_t)w1;
            rdp_fifo_append(fifo, two, 2);

            if (extra == 2)
            {
                int32_t tail[2];
                tail[0] = (int32_t)rd_u32_be(addr); addr += 4u;
                tail[1] = (int32_t)rd_u32_be(addr); addr += 4u;
                rdp_fifo_append(fifo, tail, 2);
            }
        }
        else if (extra == 2)
        {
            /* Consume the trailing words even when the base command is not
             * forwarded, so the stream stays aligned. */
            addr += 8u;
        }

        w0 = rd_u32_be(addr); addr += 4u;
        w1 = rd_u32_be(addr); addr += 4u;
    }
}

/* Load the global-state block: othermode, the 16 segment bases, the viewport
 * (80 bytes into the block), and its embedded RDP list. */
static void t3dux_load_globstate(GSPState *gsp, RdpFifo *fifo,
                                 unsigned int pgstate)
{
    unsigned int addr = t3d_seg_addr(pgstate);
    unsigned int oh, ol, rdpcmds;
    int s;

    /* struct T3DUXGlobState: pad0/perspNorm (4), flag (4), othermode0 (4),
     * othermode1 (4), segBases[16] (64), viewport (16), rdpCmds (4). */
    {
        /* The perspective normalizer occupies the logical big-endian u16 at
         * +0 (the reference declares {u16 pad0; u16 perspNorm} as a raw
         * overlay, mirroring the halfwords). Last Legion UX runs everything
         * at perspNorm 4 -- the same value its F3DLX lists send via
         * gSPPerspNormalize -- and the reciprocal chain's precision depends
         * on it, so skipping it left every T3DUX screen coordinate a few
         * ULPs off the RSP's. */
        unsigned int pn = ((unsigned int)rd_u8(addr + 3u) << 8)
                          | (unsigned int)rd_u8(addr + 2u);
        if (pn != 0u)
            gsp_set_persp_norm(gsp, pn);
    }
    oh = rd_u32_be(addr + 8u);
    ol = rd_u32_be(addr + 12u);
    t3dux_emit_othermode(gsp, fifo, oh, ol);

    for (s = 0; s < 16; ++s)
        s_t3d_seg[s] = rd_u32_be(addr + 16u + (unsigned int)s * 4u)
                       & 0x00ffffffu;

    /* Viewport lives 80 bytes into the block. */
    gsp_set_viewport(gsp, s_rdram, (addr + 80u) & 0x00ffffffu);

    rdpcmds = rd_u32_be(addr + 96u);
    t3dux_process_rdp(gsp, fifo, rdpcmds);
}

/* Load one object's per-state, vertices and triangles and emit them. */
static void t3dux_load_object(GSPState *gsp, RdpFifo *fifo,
                              unsigned int pstate, unsigned int pvtx,
                              unsigned int ptri, unsigned int pcol)
{
    unsigned int saddr = t3d_seg_addr(pstate);
    unsigned int oh, ol;
    int vtxCount, triCount, texmode, geommode, matrixFlag;
    unsigned int renderState;
    unsigned int rdpcmds;
    unsigned int caddr;
    int t;
    int flatShading, texturing;
    int32_t flatr = 0, flatg = 0, flatb = 0, flata = 0;

    /* struct T3DUXState: renderState (4); dmemVtxAddr,vtxCount,texmode,
     * geommode (4); dmemVtxAttribsAddr,attribsCount,matrixFlag,triCount (4);
     * rdpCmds (4); othermode0 (4); othermode1 (4). */
    {
        renderState = rd_u32_be(saddr + 0u);
        vtxCount   = rd_u8(saddr + 5u);
        texmode    = rd_u8(saddr + 6u);
        geommode   = rd_u8(saddr + 7u);
        matrixFlag = rd_u8(saddr + 10u);
        triCount   = rd_u8(saddr + 11u);
        rdpcmds    = rd_u32_be(saddr + 12u);
        oh         = rd_u32_be(saddr + 16u);
        ol         = rd_u32_be(saddr + 20u);
    }

    t3dux_emit_othermode(gsp, fifo, oh, ol);

    /* matrixFlag bit 0 clear => load the object's force matrix, which sits
     * immediately after the 24-byte state struct. The N64 matrix packs the
     * 16 integer halves (32 bytes) then the 16 fraction halves (32 bytes);
     * gsp_force_matrix_chunk takes eight elements per call keyed by offset. */
    if ((matrixFlag & 1) == 0)
    {
        unsigned int ma = saddr + 24u;
        gsp_force_matrix_chunk(gsp, s_rdram, ma + 0u,  0u);
        gsp_force_matrix_chunk(gsp, s_rdram, ma + 16u, 16u);
        gsp_force_matrix_chunk(gsp, s_rdram, ma + 32u, 32u);
        gsp_force_matrix_chunk(gsp, s_rdram, ma + 48u, 48u);
    }

    /* T3DUX draws with smooth shading, shade, z-buffer and back-face cull
     * on top of the object's render state, with lighting/fog cleared (the
     * vertex colours are used directly). In this walker's geometry-mode bit
     * assignment: G_ZBUFFER = 0x1 (selects the Z triangle variant), G_SHADE =
     * 0x4 and G_SHADING_SMOOTH = 0x00200000 (select the shaded variant and
     * per-vertex interpolation), and cull-back is bit 10 of the cull field at
     * bits 9..10. */
    gsp_set_geometry_mode(gsp, (unsigned int)renderState
                          | 0x00000001u | 0x00000004u
                          | 0x00200000u | 0x00000400u);

    if (pvtx != 0u)
    {
        unsigned int vaddr = t3d_seg_addr(pvtx);
        unsigned int col0  = t3d_seg_addr(pcol);
        gsp_vertex_t3dux(gsp, s_rdram, vaddr, col0, vtxCount);
    }

    t3dux_process_rdp(gsp, fifo, rdpcmds);

    if (ptri == 0u)
        return;

    caddr = t3d_seg_addr(pcol);
    flatShading = (geommode & 0x0f) == 0;
    texturing   = texmode != 1;

    for (t = 0; t < triCount; ++t)
    {
        unsigned int te = t3d_seg_addr(ptri) + (unsigned int)t * 8u;
        int flag = rd_u8(te + 0u);
        int v2   = rd_u8(te + 1u);
        int v1   = rd_u8(te + 2u);
        int v0   = rd_u8(te + 3u);
        int pal  = rd_u8(te + 4u);
        int v2t  = rd_u8(te + 5u);
        int v1t  = rd_u8(te + 6u);
        int v0t  = rd_u8(te + 7u);
        int32_t cmdw[64];
        int nc;

        if (texturing && pal != 0)
        {
            /* The real microcode (per the cxd4 oracle stream) reissues the
             * latched SETTILE before every textured triangle record whose
             * palette byte is set, with the raw palette byte placed in w1
             * bits 24..31 -- which the RDP ignores -- so the reissue is
             * pixel-neutral but keeps the command stream in step. (The
             * GLideN64 reference merges the byte at bits 20..23 instead;
             * that would actively repoint the CI palette, which the LLE
             * stream shows never happens: bits 20..23 stay those of the
             * object's own SETTILE.) No change-gating: the pair is emitted
             * per qualifying record, before index validation. */
            int32_t two[2];
            two[0] = (int32_t)(0x27u << 24);        /* G_RDPPIPESYNC */
            two[1] = 0;
            rdp_fifo_append(fifo, two, 2);
            two[0] = (int32_t)((0x35u << 24)        /* G_SETTILE */
                               | (s_settile_w0 & 0x00ffffffu));
            two[1] = (int32_t)((s_settile_w1 & 0x00ffffffu)
                               | ((unsigned int)pal << 24));
            rdp_fifo_append(fifo, two, 2);
        }

        if (v0 >= vtxCount || v1 >= vtxCount || v2 >= vtxCount)
            continue;

        if (texturing)
        {
            /* Per-vertex S/T come from the colour/texcoord array indexed by
             * the triangle's texcoord slots: a u32 with s in bits 31..16 and
             * t in bits 15..0, both S10.5. */
            unsigned int t0 = rd_u32_be(caddr + (unsigned int)v0t * 4u);
            unsigned int t1 = rd_u32_be(caddr + (unsigned int)v1t * 4u);
            unsigned int t2 = rd_u32_be(caddr + (unsigned int)v2t * 4u);
            /* The triangle write's affine texture lanes carry the record
             * texcoords doubled (the oracle's S/T bases and dS/dX slopes
             * are exactly twice the raw s10.5 shorts). */
            gsp_set_vertex_st(gsp, v0, (int)(short)(t0 >> 16) << 1,
                                       (int)(short)(t0 & 0xffffu) << 1);
            gsp_set_vertex_st(gsp, v1, (int)(short)(t1 >> 16) << 1,
                                       (int)(short)(t1 & 0xffffu) << 1);
            gsp_set_vertex_st(gsp, v2, (int)(short)(t2 >> 16) << 1,
                                       (int)(short)(t2 & 0xffffu) << 1);
        }

        if (flatShading)
        {
            /* Flat-shade colour is the a,b,g,r record the tri's flag byte
             * indexes into the colour array ((flag << 2) & 0x3fc). */
            unsigned int ca = caddr + (unsigned int)((flag << 2) & 0x3fc);
            flata = (int32_t)rd_u8(ca + 0u);
            flatb = (int32_t)rd_u8(ca + 1u);
            flatg = (int32_t)rd_u8(ca + 2u);
            flatr = (int32_t)rd_u8(ca + 3u);
            gsp_set_vertex_rgba(gsp, v0, flatr, flatg, flatb, flata);
            gsp_set_vertex_rgba(gsp, v1, flatr, flatg, flatb, flata);
            gsp_set_vertex_rgba(gsp, v2, flatr, flatg, flatb, flata);
        }

        nc = gsp_triangle(gsp, cmdw, v0, v1, v2, texturing,
                          gsp->t3d_zbuffered);
        if (nc > 0)
            rdp_fifo_append(fifo, cmdw, nc);
    }
}

/* Walk the T3DUX object list. dl_addr is the physical task display-list
 * pointer; each object is six words and a null pstate halts the list. */
void t3dux_run_dl(GSPState *gsp, RdpFifo *fifo, unsigned int dl_addr)
{
    unsigned int addr = dl_addr;
    int guard = 0;
    int s;

    for (s = 0; s < 16; ++s)
        s_t3d_seg[s] = 0u;
    s_settile_w0 = 0u;
    s_settile_w1 = 0u;
    gsp->t3d_zbuffered = 0;

    /* T3DUX stores vertex screen Y rounded half-up to whole pixels, the
     * same 480-line-interlaced whole-line convention as the sibling F3DLX
     * build (both Yasumoto ucodes; the cxd4 oracle's mech task emits not a
     * single fractional Y edge). Without it, half-pixel Y fractions shift
     * every edge against the LLE stream. */
    rsp_set_vtx_y_round(1);
    rsp_set_vtx_x_round(1);
    rsp_set_vtx_z_quant(1);
    rsp_set_keep_degenerate(1);
    rsp_set_affine_tex(1);

    while (guard++ < 4096)
    {
        unsigned int pgstate = rd_u32_be(addr + 0u);
        unsigned int pstate  = rd_u32_be(addr + 4u);
        unsigned int pvtx    = rd_u32_be(addr + 8u);
        unsigned int ptri    = rd_u32_be(addr + 12u);
        unsigned int pcol    = rd_u32_be(addr + 16u);

        if (pstate == 0u)
            break;

        if (pgstate != 0u)
            t3dux_load_globstate(gsp, fifo, pgstate);

        t3dux_load_object(gsp, fifo, pstate, pvtx, ptri, pcol);

        addr += 24u;
    }
}
