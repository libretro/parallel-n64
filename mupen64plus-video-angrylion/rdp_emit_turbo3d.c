/* Turbo3D (original SGI "RSP SW Version: 2.0D") microcode walker for the
 * angrylion HLE path.
 *
 * Turbo3D is SGI's original fast/low-quality display-list microcode, used by
 * Dark Rift (and a handful of other early titles). Unlike the Fast3D/F3DEX
 * command grammar, the task display list is a flat array of four-word objects
 *
 *     pgstate  global-state block (perspNorm, othermode, 16 segment bases,
 *              viewport, and an embedded raw-RDP command list) -- loaded only
 *              when non-zero
 *     pstate   per-object state (render/texture flags, othermode, an optional
 *              force-matrix, triangle count, vertex count, and its own RDP
 *              command list)
 *     pvtx     standard N64 vertex array (16-byte vertices, loaded and
 *              transformed by the RSP exactly as gSPVertex does)
 *     ptri     triangle array (4-byte T3DTriN records: flag, v2, v1, v0)
 *
 * A null pstate terminates the list. The object loop mirrors GLideN64's
 * RunTurbo3D: each object force-loads its combined matrix, sets the geometry
 * mode, loads its vertices through the shared transform pipeline, forwards its
 * embedded RDP list to the FIFO, and emits its triangles through the shared
 * gsp_triangle path. Verified against the cxd4 LLE RSP, which runs this
 * microcode natively.
 */

#include <stdint.h>
#include "rdp_emit_frontend.h"
#include "rdp_emit_f3dex2.h"
#include "rdp_emit_rsp.h"
#include "rdp_emit_turbo3d.h"

/* From rdp_emit_f3dex2.c: FIFO append and RSP-style segment resolution. */
extern void rdp_fifo_append(RdpFifo *f, const int32_t *words, int count);
extern unsigned int gsp_seg_addr_rsp(unsigned int w1);
extern void rdp_fifo_fullsync_note(void);

static unsigned char *s_rdram;
static unsigned int   s_rdram_size;

/* Turbo3D keeps its own 16-entry segment table, loaded from the global-state
 * block, independent of the F3DEX2 G_MW_SEGMENT table. */
static unsigned int s_t3d_seg[16];

/* Whether the most recent embedded SETCOMBINE references TEXEL0/TEXEL1 in its
 * colour combiner. The microcode emits the textured triangle variant only
 * when the active combiner samples a texel, so the triangle write's texture
 * lanes follow the combiner rather than being forced on. */
static int s_t3d_textured;

void turbo3d_set_rdram(unsigned char *rdram) { s_rdram = rdram; }
void turbo3d_set_rdram_size(unsigned int size) { s_rdram_size = size; }

/* CRC-32 (reflected, poly 0x04C11DB7) of the first 4 KiB of the microcode
 * text segment, matching GLideN64's SpecialMicrocodeInfo detection. The
 * Turbo3D microcode (Dark Rift) hashes to 0x2bdcfc8a; the "RSP SW Version:
 * 2.0D" data-segment string is shared with F3D/F3DEX/L3D and cannot be used
 * on its own. The bytes are read raw (no in-word swap), the order GLideN64
 * hashes and the order this match was confirmed against the live task. */
static unsigned int s_crc_table[256];
static int s_crc_ready;

static void turbo3d_crc_init(void)
{
    unsigned int i;
    int j;
    for (i = 0; i < 256u; ++i)
    {
        unsigned int ref = 0u, c, r, v;
        unsigned int k;
        for (k = 1u; k < 9u; ++k)
            if (i & (1u << (k - 1u)))
                ref |= 1u << (8u - k);
        c = ref << 24;
        for (j = 0; j < 8; ++j)
            c = (c << 1) ^ ((c & 0x80000000u) ? 0x04C11DB7u : 0u);
        v = 0u;
        r = c;
        for (k = 1u; k < 33u; ++k)
        {
            if (r & 1u)
                v |= 1u << (32u - k);
            r >>= 1;
        }
        s_crc_table[i] = v;
    }
    s_crc_ready = 1;
}

int turbo3d_ucode_match(const unsigned char *rdram, unsigned int rdram_size,
                        unsigned int text_seg)
{
    unsigned int crc = 0xffffffffu;
    unsigned int i;
    if (rdram == 0 || text_seg == 0 || text_seg + 4096u > rdram_size)
        return 0;
    if (!s_crc_ready)
        turbo3d_crc_init();
    for (i = 0; i < 4096u; ++i)
        crc = (crc >> 8) ^ s_crc_table[(crc & 0xffu) ^ rdram[text_seg + i]];
    crc ^= 0xffffffffu;
    return crc == 0x2bdcfc8au;
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

/* Raw (unswapped) byte access. The Turbo3D struct layouts follow the GLideN64
 * reference declarations, which are raw overlays on the host's byteswapped
 * RDRAM -- their field order already encodes the in-word swap. Reading the
 * struct's u8 fields (flag/triCount/vtxV0/vtxCount, and the triangle index
 * bytes) with a ^3-swapped access lands each byte on the wrong lane; the
 * reference offsets must be read raw. */
static int rd_u8(unsigned int addr)
{
    if (s_rdram == 0 || addr >= s_rdram_size)
        return 0;
    return (int)s_rdram[addr];
}

/* Signed 16-bit read. The captured RDRAM is stored little-endian within each
 * word (rd_u32_be and rd_u8 both read it directly), so a halfword field is the
 * low byte followed by the high byte -- the pre-transformed vertex records'
 * screen coordinates land correctly this way, where a byte-swapped read put
 * yscrn and xscrn on each other's lanes. */
static int rd_s16(unsigned int addr)
{
    unsigned int v;
    if (s_rdram == 0 || addr + 2u > s_rdram_size)
        return 0;
    v = (unsigned int)s_rdram[addr]
      | ((unsigned int)s_rdram[addr + 1u] << 8);
    return (int)(short)(unsigned short)v;
}

/* Resolve a Turbo3D segmented address through the walker's own segment table
 * (segment id in the top byte, offset in the low 24 bits). */
static unsigned int t3d_seg_addr(unsigned int a)
{
    unsigned int seg = (a >> 24) & 0xfu;
    return (s_t3d_seg[seg] + (a & 0x00ffffffu)) & 0x00ffffffu;
}

/* Emit a SET_OTHER_MODES (RDP 0x2f) pair from the object/global othermode
 * words and update the depth-test sniff. othermode0 carries the mode-high
 * field in its low 24 bits. */
static void turbo3d_emit_othermode(GSPState *gsp, RdpFifo *fifo,
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

/* Forward an embedded raw-RDP command list to the FIFO. The list is a stream
 * of w0/w1 pairs terminated by a zero pair; TEXRECT/TEXRECTFLIP carry two
 * trailing words, exactly as the reference splitter reads them. SETTIMG/
 * SETZIMG/SETCIMG carry a segmented DRAM pointer that is resolved before the
 * command reaches the RDP. */
static void turbo3d_process_rdp(GSPState *gsp, RdpFifo *fifo, unsigned int cmds)
{
    unsigned int addr;
    unsigned int w0, w1;
    int guard = 0;
    if (cmds == 0u)
        return;
    addr = t3d_seg_addr(cmds);
    if (addr == 0u)
        return;

    w0 = rd_u32_be(addr); addr += 4u;
    w1 = rd_u32_be(addr); addr += 4u;
    while ((w0 | w1) != 0u && guard++ < 65536)
    {
        int cmd = (int)((w0 >> 24) & 0xffu);
        int rdp_id = cmd & 0x3f;
        int extra = 0;

        if (cmd == 0xe4 || cmd == 0xe5)         /* G_TEXRECT / G_TEXRECTFLIP */
            extra = 2;

        if (rdp_id == 0x2f)
        {
            int zc = (int)((w1 >> 4) & 1);
            int zu = (int)((w1 >> 5) & 1);
            gsp->t3d_zbuffered = (zc || zu) ? 1 : 0;
        }

        if (rdp_id == 0x3c)                     /* G_SETCOMBINE */
        {
            /* Textured triangles are emitted only when the active colour
             * combiner samples a texel. The colour combiner's four input
             * slots (sub_a/sub_b/mul/add, two cycles) carry TEXEL0 = 1 and
             * TEXEL1 = 2; if none of them do, the object shades flat/gouraud
             * and the microcode uses the untextured triangle variant. */
            int sa0 = (int)((w0 >> 20) & 0xf), sa1 = (int)((w0 >> 5) & 0xf);
            int mu0 = (int)((w0 >> 15) & 0x1f), mu1 = (int)(w0 & 0x1f);
            int sb0 = (int)((w1 >> 28) & 0xf), sb1 = (int)((w1 >> 24) & 0xf);
            int ad0 = (int)((w1 >> 15) & 0x7), ad1 = (int)((w1 >> 6) & 0x7);
            s_t3d_textured =
                (sa0 == 1 || sa0 == 2 || sa1 == 1 || sa1 == 2 ||
                 sb0 == 1 || sb0 == 2 || sb1 == 1 || sb1 == 2 ||
                 mu0 == 1 || mu0 == 2 || mu1 == 1 || mu1 == 2 ||
                 ad0 == 1 || ad0 == 2 || ad1 == 1 || ad1 == 2) ? 1 : 0;
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
            addr += 8u;
        }

        w0 = rd_u32_be(addr); addr += 4u;
        w1 = rd_u32_be(addr); addr += 4u;
    }
}

/* Load the global-state block: perspNorm, othermode, the 16 segment bases,
 * the viewport (80 bytes into the block), and its embedded RDP list. */
static void turbo3d_load_globstate(GSPState *gsp, RdpFifo *fifo,
                                   unsigned int pgstate)
{
    unsigned int addr = t3d_seg_addr(pgstate);
    unsigned int oh, ol, rdpcmds;
    int s;

    /* struct T3DGlobState: pad0/perspNorm (4), flag (4), othermode0 (4),
     * othermode1 (4), segBases[16] (64), viewport (16), rdpCmds (4).
     * perspNorm is the big-endian u16 at +2. */
    {
        /* struct T3DGlobState begins {u16 pad0; u16 perspNorm}. In the
         * captured little-endian-within-word RDRAM the perspNorm halfword is
         * the high 16 bits of the first word (rd_u32_be gives the big-endian
         * value; its high half is perspNorm, its low half pad0). Dark Rift
         * runs this at 11 -- a small normalizer like the other Turbo-family
         * ucodes (Last Legion 4, Super Smash Bros. 8). Reading the low half
         * instead left perspNorm at the 0xFFFF default, which does not scale
         * the large post-transform w down, so every transformed character
         * vertex tripped the positive-W clip and its whole model was
         * rejected -- only the untextured screen-space shadows survived. */
        unsigned int pn = (rd_u32_be(addr) >> 16) & 0xffffu;
        if (pn != 0u)
            gsp_set_persp_norm(gsp, pn);
    }
    oh = rd_u32_be(addr + 8u);
    ol = rd_u32_be(addr + 12u);
    turbo3d_emit_othermode(gsp, fifo, oh, ol);

    for (s = 0; s < 16; ++s)
        s_t3d_seg[s] = rd_u32_be(addr + 16u + (unsigned int)s * 4u)
                       & 0x00ffffffu;

    gsp_set_viewport(gsp, s_rdram, (addr + 80u) & 0x00ffffffu);

    rdpcmds = rd_u32_be(addr + 96u);
    turbo3d_process_rdp(gsp, fifo, rdpcmds);
}

/* GT_FLAG values (GLideN64 Turbo3D.cpp). */
#define GT_FLAG_NOMTX   0x01    /* don't load the matrix */
#define GT_FLAG_NO_XFM  0x02    /* vertices are pre-transformed screen space */

/* Load one object's per-state, vertices and triangles and emit them. */
static void turbo3d_load_object(GSPState *gsp, RdpFifo *fifo,
                                unsigned int pstate, unsigned int pvtx,
                                unsigned int ptri)
{
    unsigned int saddr = t3d_seg_addr(pstate);
    unsigned int renderState;
    int flag, triCount, vtxV0, vtxCount;
    unsigned int rdpcmds;
    unsigned int oh, ol;
    int t;
    int textured;

    /* struct T3DState: renderState (4), textureState (4),
     * {flag, triCount, vtxV0, vtxCount} (4), rdpCmds (4),
     * othermode0 (4), othermode1 (4). The four count/flag bytes are read raw
     * (see rd_u8): flag@8, triCount@9, vtxV0@10, vtxCount@11. */
    renderState = rd_u32_be(saddr + 0u);
    flag     = rd_u8(saddr + 8u);
    triCount = rd_u8(saddr + 9u);
    vtxV0    = rd_u8(saddr + 10u);
    vtxCount = rd_u8(saddr + 11u);
    rdpcmds  = rd_u32_be(saddr + 12u);
    oh       = rd_u32_be(saddr + 16u);
    ol       = rd_u32_be(saddr + 20u);

    turbo3d_emit_othermode(gsp, fifo, oh, ol);

    /* Load the object's force matrix (the combined MVP) unless the state
     * flags say to keep the previous one. It sits immediately after the
     * 24-byte state struct: 16 integer halves then 16 fraction halves. */
    if (flag != GT_FLAG_NOMTX)
    {
        unsigned int ma = saddr + 24u;
        gsp_force_matrix_chunk(gsp, s_rdram, ma + 0u,  0u);
        gsp_force_matrix_chunk(gsp, s_rdram, ma + 16u, 16u);
        gsp_force_matrix_chunk(gsp, s_rdram, ma + 32u, 32u);
        gsp_force_matrix_chunk(gsp, s_rdram, ma + 48u, 48u);
    }

    /* The reference clears lighting/fog and forces smooth shade, shade and
     * back-face cull on top of the object's render state. The Z-buffer
     * triangle variant, though, follows the object's other-mode z-compare/
     * z-update bits (sniffed into t3d_zbuffered by the othermode emit above):
     * the microcode selects the plain shade triangle when the object draws
     * with depth off (Dark Rift's full-screen backgrounds), matching the
     * oracle's TRI_SHADE vs TRI_SHADE_Z choice. G_ZBUFFER = 0x1 selects the
     * Z triangle variant in gsp_triangle. */
    /* renderState | SHADE | SHADING_SMOOTH | (Z from other-mode) | cull.
     * The reference sets G_CULL_BACK, but the Turbo3D force matrix feeds this
     * transform with the opposite screen-space winding to the F3DEX2 pipeline
     * the bridge's cull convention was tuned against, so the equivalent cull
     * here is G_CULL_FRONT (0x200): with G_CULL_BACK every transformed
     * character model was culled and only the untextured screen-space shadows
     * survived. Verified pixel-exact against the cxd4 oracle on the in-match
     * fight scene. */
    gsp_set_geometry_mode(gsp, (renderState & ~(0x00010000u | 0x00020000u))
                          | (gsp->t3d_zbuffered ? 0x00000001u : 0u)
                          | 0x00000004u
                          | 0x00200000u | 0x00000200u);

    /* Texturing follows the active combiner (sniffed from the embedded
     * SETCOMBINE): the microcode emits the textured triangle variant only
     * when the combiner samples a texel. Process the object's RDP list first
     * so its SETCOMBINE updates the flag before the triangles are written. */
    turbo3d_process_rdp(gsp, fifo, rdpcmds);
    textured = s_t3d_textured;

    if (flag & GT_FLAG_NO_XFM)
    {
        /* Pre-transformed screen-space object: the vertex array is VtxOut
         * records (16 bytes: yscrn s10.2, xscrn s10.2, zscrn s15.16, t, s,
         * then a,b,g,r colour). The microcode writes them straight to the
         * screen with no transform, so inject them as flat-2D vertices and
         * emit the triangles directly. Dark Rift draws its full-screen
         * backgrounds and 2D overlays this way. */
        unsigned int va = t3d_seg_addr(pvtx);
        int i;
        if (pvtx == 0u || ptri == 0u)
            return;
        /* Screen-space 2D geometry is not back-face culled: the reference
         * draws it through drawScreenSpaceTriangle, which bypasses the cull
         * the transformed path applies. Re-assert the geometry mode without
         * the cull-back bit so Dark Rift's full-screen backgrounds (wound
         * CCW in screen space) are not dropped. */
        gsp_set_geometry_mode(gsp, (renderState & ~(0x00010000u | 0x00020000u))
                              | (gsp->t3d_zbuffered ? 0x00000001u : 0u)
                              | 0x00000004u | 0x00200000u);
        for (i = 0; i < vtxCount && i < GSP_MAX_VERTICES; ++i)
        {
            unsigned int vr = va + (unsigned int)i * 16u;
            GSPVertex *vt = &gsp->vtx[i];
            int ys = rd_s16(vr + 0u);
            int xs = rd_s16(vr + 2u);
            int32_t zs = (int32_t)rd_u32_be(vr + 4u);
            int tt = rd_s16(vr + 8u);
            int ss = rd_s16(vr + 10u);
            int a = rd_u8(vr + 12u);
            int b = rd_u8(vr + 13u);
            int g = rd_u8(vr + 14u);
            int r = rd_u8(vr + 15u);
            vt->scr_x = (int32_t)xs << 14;
            vt->scr_y = (int32_t)ys << 14;
            vt->scr_z = zs;
            vt->cx = vt->scr_x; vt->cy = vt->scr_y;
            vt->cz = zs; vt->cw = (int32_t)(1u << 16);
            vt->r = (int32_t)r << 16; vt->g = (int32_t)g << 16;
            vt->b = (int32_t)b << 16; vt->a = (int32_t)a << 16;
            vt->sv = (int16_t)ss; vt->tv = (int16_t)tt;
            vt->s = (int32_t)ss << 16; vt->t = (int32_t)tt << 16;
            vt->clip = 0;
            vt->rsp_ok = 1;
            vt->rsp_invw = (int32_t)(1u << 16);
            vt->w_raw = (int64_t)1 << 16;
            vt->flat2d = 1;
        }
        for (t = 0; t < triCount; ++t)
        {
            unsigned int te = t3d_seg_addr(ptri) + (unsigned int)t * 4u;
            int v2 = rd_u8(te + 1u);
            int v1 = rd_u8(te + 2u);
            int v0 = rd_u8(te + 3u);
            int32_t cmdw[64];
            int nc;
            if (v0 >= vtxCount || v1 >= vtxCount || v2 >= vtxCount)
                continue;
            nc = gsp_triangle(gsp, cmdw, v0, v1, v2, textured,
                              gsp->t3d_zbuffered);
            if (nc > 0)
                rdp_fifo_append(fifo, cmdw, nc);
        }
        return;
    }

    if (pvtx != 0u)
        gsp_vertex(gsp, s_rdram, t3d_seg_addr(pvtx), vtxCount, vtxV0);


    if (ptri == 0u)
        return;

    for (t = 0; t < triCount; ++t)
    {
        unsigned int te = t3d_seg_addr(ptri) + (unsigned int)t * 4u;
        int v2 = rd_u8(te + 1u);
        int v1 = rd_u8(te + 2u);
        int v0 = rd_u8(te + 3u);
        int32_t cmdw[64];
        int nc;

        if (v0 >= vtxCount || v1 >= vtxCount || v2 >= vtxCount)
            continue;

        nc = gsp_triangle(gsp, cmdw, v0, v1, v2, textured,
                          gsp->t3d_zbuffered);
        if (nc > 0)
            rdp_fifo_append(fifo, cmdw, nc);
    }
}

/* Walk the Turbo3D object list. dl_addr is the physical task display-list
 * pointer; each object is four words and a null pstate halts the list. */
void turbo3d_run_dl(GSPState *gsp, RdpFifo *fifo, unsigned int dl_addr)
{
    unsigned int addr = dl_addr;
    int guard = 0;
    int s;

    for (s = 0; s < 16; ++s)
        s_t3d_seg[s] = 0u;
    gsp->t3d_zbuffered = 0;
    s_t3d_textured = 0;

    while (guard++ < 8192)
    {
        unsigned int pgstate = rd_u32_be(addr + 0u);
        unsigned int pstate  = rd_u32_be(addr + 4u);
        unsigned int pvtx    = rd_u32_be(addr + 8u);
        unsigned int ptri    = rd_u32_be(addr + 12u);

        if (pstate == 0u)
            break;

        if (pgstate != 0u)
            turbo3d_load_globstate(gsp, fifo, pgstate);

        turbo3d_load_object(gsp, fifo, pstate, pvtx, ptri);

        addr += 16u;
    }
}
