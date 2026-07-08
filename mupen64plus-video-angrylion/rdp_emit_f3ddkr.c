/* rdp_emit_f3ddkr.c -- F3DDKR (Diddy Kong Racing custom microcode) display-
 * list dispatcher for the angrylion HLE path. See rdp_emit_f3ddkr.h for the
 * GBI overview. ISO C89 / MSVC-compatible.
 *
 * Decode rules are taken from the F3DDKR GBI macros (the game's own f3ddkr.h):
 *
 *   gSPVertexDKR(pkt, v, n, v0):
 *     gDma1p(G_VTX, v, ((n*8 + n) << 1) + 8, ((n-1) << 3) | ((v) & 6) | v0)
 *     -> w0 low 16 bits = byte length, command 0x04; w1 = segmented addr.
 *        n is recovered from the parameter byte: ((param >> 3) & 0x1f) + 1.
 *        v0 (destination base) is param & 1 here in practice (the (v)&6 bits
 *        are address alignment carried for the RSP DMA, not a dest index);
 *        bit 0 is the G_VTX_APPEND flag.
 *
 *   gSPPolygon(dl, ptr, numTris, texEnabled):  command G_TRIN = 0x05
 *     w0 = (G_TRIN << 24) | (((numTris-1) << 4 | texEnabled) << 16)
 *            | (numTris * 16);   w1 = segmented addr of the triangle array.
 *     Each triangle entry is 4 bytes: i0, i1, i2, flags.
 *
 *   gSPMatrixDKR(pkt, m, i):  command G_MTX = 0x01, param (i) << 6 selects the
 *     destination matrix slot (0/1/2).  gSPSelectMatrixDKR selects the active
 *     MVP via G_MOVEWORD/G_MW_MVPMATRIX.
 *
 *   gDkrDmaDisplayList: command G_DMADL = 0x07, w0 carries the command count.
 *
 * Billboarding (G_MOVEWORD G_MW_BILLBOARD): while enabled, gSPVertexDKR
 * vertices are added to vertex 0 after MVP transform, before perspective
 * divide. NOT yet implemented here (see the G_MW_BILLBOARD note below); the
 * common 3D track/model geometry path does not use it, so plain rendering is
 * correct without it -- billboard sprites are the first follow-up.
 */
#include "rdp_emit_f3ddkr.h"
#include "rdp_emit_f3d.h"

#if defined(_MSC_VER) && (_MSC_VER < 1600)
typedef signed   __int32 dkr_int32_t;
#else
#include <stdint.h>
#endif

/* ---- F3DDKR command opcodes (from f3ddkr.h) ----------------------------- */
#define DKR_G_MTX     0x01
#define JFG_G_DMA_TEX_OFFSET 0x02  /* JFG/Mickey build only */
#define DKR_G_MOVEMEM 0x03
#define DKR_G_VTX     0x04
#define DKR_G_TRIN    0x05   /* gSPPolygon */
#define DKR_G_DL      0x06
#define DKR_G_DMADL   0x07   /* gDkrDmaDisplayList */
#define DKR_G_MOVEWORD 0xBC
#define DKR_G_ENDDL   0xB8
#define DKR_G_SETGEOMETRYMODE   0xB7
#define DKR_G_CLEARGEOMETRYMODE 0xB6
#define DKR_G_TEXTURE 0xBB
#define DKR_G_SETOTHERMODE_H    0xBA
#define DKR_G_SETOTHERMODE_L    0xB9
#define JFG_G_DMA_OFFSETS       0xBF  /* JFG/Mickey build only */

/* G_MOVEWORD indices used by DKR. The index byte is the low 8 bits of w0
 * (gImmp21 packs G_MOVEWORD as cmd<<24 | offset<<8 | index); GLideN64's
 * F3DDKR_MoveWord likewise reads _SHIFTR(w0,0,8). G_MW_SEGMENT is the stock
 * F3D segment-base move that gSPSegment/rsp_segment emit: DKR uses it to point
 * SEGMENT_FRAMEBUFFER/ZBUFFER/MAIN at the live buffers each frame, so the
 * walker must record it or the SET_COLOR_IMAGE/SET_*_IMAGE seg lookups below
 * resolve against a zero base and angrylion renders over low RDRAM (incl. the
 * 0x180 exception vectors -> CPU wedges in the exception handler). */
#define DKR_MW_BILLBOARD 0x02
#define DKR_MW_FOG       0x08
#define DKR_MW_SEGMENT   0x06
#define DKR_MW_MVPMATRIX 0x0A

/* G_VTX_APPEND lives in the full w0 word (GLideN64 tests w0 & 0x00010000);
 * the gSPVertexDKR macro ORs v0's low bit, which lands here. */
#define F3DDKR_VTX_APPEND 0x00010000u

#define DKR_DL_MAX_DEPTH 10

/* ---- module state -------------------------------------------------------- */
static unsigned char *s_rdram = 0;
static unsigned int   s_rdram_size = 0;
static unsigned int   s_seg[16];
static int   s_textured  = 0;
static int   s_zbuffered = 0;
static int   s_dl_depth  = 0;
static int   s_mtx_slot  = 0;   /* active MVP slot selected by gSPSelectMatrixDKR */
static int   s_billboard = 0;   /* G_MW_BILLBOARD state (decode only for now) */
static unsigned int s_othermode_h = 0;
static unsigned int s_othermode_l = 0;
static int   s_vtx_top   = 0;   /* next free vertex slot for G_VTX_APPEND */
static unsigned int s_geom = 0; /* raw GBI 1 geometry mode; translated on write */
/* JFG/Mickey's-Speedway build of the microcode (see f3ddkr_is_ucode): the
 * G_VTX count field is not biased by one, G_MTX carries the slot index in
 * bits 16..19 with a multiply flag in bit 23, and two extra commands appear:
 * G_DMA_OFFSETS (0xBF) supplies base offsets added to the matrix and vertex
 * DMA addresses, and G_DMA_TEX_OFFSET (0x02) points at a u16 table of
 * per-load texture-image address shifts (scrolling-texture animation). */
static int   s_jfg = 0;
static unsigned int s_dma_mtx = 0;  /* G_DMA_OFFSETS matrix base */
static unsigned int s_dma_vtx = 0;  /* G_DMA_OFFSETS vertex base */
static unsigned int s_tex_ofs = 0;  /* G_DMA_TEX_OFFSET table address */
static unsigned int s_tex_shift = 0;
static unsigned int s_tex_count = 0;
/* Last forwarded SET_TEXTURE_IMAGE, kept so a LOAD_BLOCK that invalidates the
 * texture shift (see JFG_G_DMA_TEX_OFFSET) can re-emit it unshifted before
 * the load consumes the address. */
static unsigned int s_timg_w0 = 0;
static unsigned int s_timg_base = 0;
static int          s_timg_shifted = 0;

/* ---- helpers (mirror rdp_emit_f3d.c) ------------------------------------ */
static unsigned int rd32(const unsigned char *r, unsigned int a)
{
    return ((unsigned int)r[(a + 0) ^ 3] << 24)
         | ((unsigned int)r[(a + 1) ^ 3] << 16)
         | ((unsigned int)r[(a + 2) ^ 3] << 8)
         |  (unsigned int)r[(a + 3) ^ 3];
}

static unsigned int seg_rsp(unsigned int w1)
{
    unsigned int seg = (w1 >> 24) & 0x0f;
    return s_seg[seg] + (w1 & 0x00ffffffu);
}

static unsigned int seg_phys(unsigned int w1)
{
    return seg_rsp(w1) & 0x00ffffffu;
}

static int in_range(unsigned int a, unsigned int bytes)
{
    return (a != 0u) && (a + bytes <= s_rdram_size);
}

/* ---- public setup -------------------------------------------------------- */
void f3ddkr_seg_reset(void)
{
    int i;
    for (i = 0; i < 16; i++)
        s_seg[i] = 0u;
    s_textured  = 0;
    s_zbuffered = 0;
    s_dl_depth  = 0;
    s_mtx_slot  = 0;
    s_billboard = 0;
    s_vtx_top   = 0;
    s_geom      = 0u;
    s_othermode_h = 0;
    s_othermode_l = 0;
    s_dma_mtx   = 0u;
    s_dma_vtx   = 0u;
    s_tex_ofs   = 0u;
    s_tex_shift = 0u;
    s_tex_count = 0u;
    /* s_jfg is not reset here: f3ddkr_is_ucode() sets it per task, and the
     * hle.c dispatch calls that detector before this reset. */
}

void f3ddkr_set_rdram(unsigned char *rdram)   { s_rdram = rdram; }
void f3ddkr_set_rdram_size(unsigned int size) { s_rdram_size = size; }

void f3ddkr_seed_othermode(GSPState *gsp, const unsigned char *rdram,
                           unsigned int rdram_size, unsigned int ud)
{
    unsigned int w0, w1;
    if (rdram == 0 || ud == 0 || ud + 0x120 > rdram_size)
        return;
    w0 = ((unsigned int)rdram[(ud + 0x118) ^ 3] << 24)
       | ((unsigned int)rdram[(ud + 0x119) ^ 3] << 16)
       | ((unsigned int)rdram[(ud + 0x11a) ^ 3] << 8)
       |  (unsigned int)rdram[(ud + 0x11b) ^ 3];
    w1 = ((unsigned int)rdram[(ud + 0x11c) ^ 3] << 24)
       | ((unsigned int)rdram[(ud + 0x11d) ^ 3] << 16)
       | ((unsigned int)rdram[(ud + 0x11e) ^ 3] << 8)
       |  (unsigned int)rdram[(ud + 0x11f) ^ 3];
    if ((w0 >> 24) != 0xefu)
        return;
    s_othermode_h = w0 & 0x00ffffffu;
    s_othermode_l = w1;
    s_zbuffered = (((s_othermode_l >> 4) & 1u) ||
                   ((s_othermode_l >> 5) & 1u)) ? 1 : 0;
    gsp_set_dkr_shade_alpha_zero(gsp,
        (((s_othermode_l >> 30) & 3u) == 3u) ? 1 : 0);
}

/* Recognise the F3DDKR custom microcode by a positive fingerprint in its GBI
 * command dispatch table (data+0xbc, u16 IMEM handler addresses indexed by
 * opcode). F3DDKR shares F3D's lineage -- same boot word (text+4 = 0x201d0110)
 * and the same 0xef othermode-init tag at data+0x118 as stock F3DEX v1 -- so
 * neither the boot word nor the data-segment tags distinguish it. What does
 * distinguish it is that DKR implements opcode 0x07 (G_DMADL,
 * gDkrDmaDisplayList): its table slot points at a real handler, whereas stock
 * F3DEX/F3D leave that opcode at the table's null/NOP stub. (Verified against
 * the retail F3DDKR xbus data segment: op 0x07 -> 0x145c, a real handler,
 * while the most-common "null" entry is 0x10a8; stock gspF3DEX.fifo leaves
 * op 0x07 at its own null stub 0x109c.)
 *
 * The null stub value differs per build, so compute it as the most frequent
 * table entry and test that G_DMADL's slot is something else. This is robust
 * across the DKR/JFG/Mickey F3DDKR family without a per-build constant. */
int f3ddkr_is_ucode(const unsigned char *rdram, unsigned int rdram_size,
                    unsigned int ud, unsigned int uds)
{
    unsigned short tbl[16];
    int i;
    (void)uds;
    if (rdram == 0 || ud == 0 || ud + 0xbc + 16u * 2u > rdram_size)
        return 0;
    for (i = 0; i < 16; i++)
        tbl[i] = (unsigned short)(((unsigned int)rdram[(ud + 0xbc + i * 2u) ^ 3] << 8)
                                 | (unsigned int)rdram[(ud + 0xbd + i * 2u) ^ 3]);
    /* Match F3DDKR by an exact fingerprint of its GBI command dispatch table
     * (data+0xbc, u16 IMEM handler addresses). These four handler addresses
     * at these table positions are specific to the F3DDKR microcode image and
     * are shared by the us/eu/jp DKR builds (same ucode). A non-DKR microcode
     * would have to place four identical handler addresses at the same four
     * table slots to collide, which does not happen in practice -- this is
     * what makes the detector exclusive and stops it from hijacking other
     * F3D-family tasks (the earlier slot-pattern heuristic was not opcode-
     * accurate because the dispatch index is shifted, so it pattern-matched
     * bytes and false-positived on Doom 64 / Yoshi's Story).
     *
     * Verified against the retail F3DDKR xbus data segment:
     *   tbl[0]=0x1340 tbl[5]=0x1358 tbl[7]=0x145c tbl[8]=0x146c
     * and against gspF3DEX.fifo, which matches none of them. */
    if (tbl[0] == 0x1340 && tbl[5] == 0x1358
        && tbl[7] == 0x145c && tbl[8] == 0x146c)
    {
        s_jfg = 0;
        return 1;
    }
    /* The Jet Force Gemini / Mickey's Speedway USA evolution of the same
     * microcode: the dispatch table keeps the DKR shape (same handler
     * spacing: tbl[5]-tbl[0] = 0x18, tbl[8]-tbl[7] = 0x10) but the image is
     * relocated and grew, so the handler addresses differ. Fingerprinted the
     * same way, against the retail Mickey's Speedway USA xbus data segment
     * (null stub 0x10cc at slots 1/4/12/13). This build changes the command
     * set (see s_jfg above), so record the variant for the walker. */
    if (tbl[0] == 0x1390 && tbl[5] == 0x13a8
        && tbl[7] == 0x1504 && tbl[8] == 0x1514)
    {
        s_jfg = 1;
        return 1;
    }
    return 0;
}

/* ---- the F3DDKR walker --------------------------------------------------- */
static void f3ddkr_run_dl_impl(GSPState *gsp, RdpFifo *fifo, unsigned int addr,
                               int max_cmds, int textured, int z_buffered);
static void f3ddkr_run_dl_n(GSPState *gsp, RdpFifo *fifo, unsigned int addr,
                            int n, int textured, int z_buffered);

void f3ddkr_run_dl(GSPState *gsp, RdpFifo *fifo, unsigned int addr,
                   int textured, int z_buffered)
{
    /* DKR's F3DDKR is a standard (non-NoN) F3D-family microcode: dRspInit
     * carries gsSPClipRatio(FRUSTRATIO_2) and no No-Nearclipping markers, so
     * the near plane is z + w, not w alone.  The shared GSPState defaults
     * clip_near_z to 0 (the NoN / w-only plane), and the DKR walker -- unlike
     * the F3D walker -- never raised it, so the polygon clipper intersected
     * against w = 0.  Vertices straddling the eye then clipped to cw == 0
     * (or a hair negative), which the perspective divide cannot project and
     * saturates to the screen-coordinate clamp.  Clip at z + w like the rest
     * of the F3D family so the boundary vertex lands just in front of the eye
     * with a small positive w. */
    gsp->clip_near_z = 1;
    /* DKR's vertex chain is Rare's own, not the F3D/F3DEX MAC chain the
     * bridge's rsp_vtx_screen models: the game never sends a perspNorm and
     * its raw w values run far outside the modelled domain, so the F3D
     * model displaced vertices by up to ~11 px (background batches worst).
     * The exact divide tracks the cxd4 LLE stream to sub-quarter-pixel. */
    gsp_set_rsp_screen_model(gsp, 0);
    f3ddkr_run_dl_impl(gsp, fifo, addr, 0, textured, z_buffered);
}

/* count-delimited entry (gDkrDmaDisplayList): walk exactly n commands. */
static void f3ddkr_run_dl_n(GSPState *gsp, RdpFifo *fifo, unsigned int addr,
                            int n, int textured, int z_buffered)
{
    f3ddkr_run_dl_impl(gsp, fifo, addr, n, textured, z_buffered);
}

static void f3ddkr_run_dl_impl(GSPState *gsp, RdpFifo *fifo, unsigned int addr,
                               int max_cmds, int textured, int z_buffered)
{
    int guard = 0;
    int count = 0;
    unsigned int pc = addr;
    int running = 1;
    const unsigned char *r = s_rdram;

    if (textured)   s_textured  = textured;
    if (z_buffered) s_zbuffered = z_buffered;
    if (r == 0)
        return;
    if (s_dl_depth >= DKR_DL_MAX_DEPTH)
        return;
    s_dl_depth++;

    while (running && guard++ < 100000)
    {
        unsigned int w0, w1;
        int cmd;
        if (max_cmds > 0 && count >= max_cmds)
            break;
        w0 = rd32(r, pc);
        w1 = rd32(r, pc + 4);
        cmd = (int)((w0 >> 24) & 0xff);
        pc += 8;
        count++;

        switch (cmd)
        {
        case DKR_G_MTX:
        {
            /* gSPMatrixDKR. Verified layout (GLideN64 F3DDKR_DMA_Mtx): the
             * length field (w0 low 16) is 64; the matrix index lives in
             * w0 bits 22..23 for the DKR build (multiply is always 0 for
             * DKR -- the JFG build uses bit 23 for multiply, but DKR copies).
             * Projection is identity in this model, so the load goes to the
             * indexed modelview slot and that slot becomes the active MVP. */
            unsigned int len = w0 & 0xffffu;
            unsigned int ma  = (s_dma_mtx + seg_phys(w1)) & 0x00ffffffu;
            if (len == 64u && in_range(ma, 64u))
            {
                /* The JFG/Mickey build carries the slot index in bits 16..19
                 * (nonzero there means the JFG form) with a multiply flag in
                 * bit 23; the DKR form leaves 16..19 zero and packs the index
                 * in bits 22..23 with no multiply. GLideN64's F3DDKR_DMA_Mtx
                 * applies the same discrimination unconditionally, which is
                 * safe: a DKR word never has bits 16..19 set. */
                int index = (int)((w0 >> 16) & 0x0fu);
                int multiply = 0;
                if (index == 0)
                    index = (int)((w0 >> 22) & 0x03u);
                else
                    multiply = (int)((w0 >> 23) & 0x01u);
                gsp_matrix_dkr(gsp, r, ma, index, multiply);
            }
            break;
        }

        case DKR_G_VTX:
        {
            /* gSPVertexDKR. Verified bit layout (GLideN64 F3DDKR_DMA_Vtx,
             * cross-checked against the decomp gDma1p packing): vertex count
             * n = ((w0 >> 19) & 0x1f) + 1, and the destination base index is
             * ((w0 >> 9) & 0x1f). G_VTX_APPEND (w0 bit 0) appends after the
             * running write cursor; when billboarding is active an append
             * starts at index 1 (vertex 0 is the anchor). DKR vertices are
             * the 10-byte pos+RGBA format, loaded via gsp_vertex_dkr. */
            int append = (int)(w0 & F3DDKR_VTX_APPEND);
            /* The JFG/Mickey build's count field is unbiased (GLideN64
             * F3DJFG_DMA_Vtx has no +1); the DKR build stores count-1.
             * Verified against the Mickey's Speedway USA race display list:
             * every gSPPolygon batch indexes exactly (w0>>19 & 0x1f) vertices
             * (the DKR decode over-reads each block by one), and the DMA
             * length in w0's low bits is 10*n+8 under the unbiased count. */
            int n  = (int)((w0 >> 19) & 0x1fu) + (s_jfg ? 0 : 1);
            int dst;
            unsigned int va = (s_dma_vtx + seg_phys(w1)) & 0x00ffffffu;
            if (append)
            {
                if (s_billboard && s_vtx_top == 0)
                    s_vtx_top = 1;
            }
            else
                s_vtx_top = 0;
            dst = s_vtx_top + (int)((w0 >> 9) & 0x1fu);
            if (n > 0 && dst >= 0 && dst + n <= GSP_MAX_VERTICES
                && in_range(va, (unsigned int)n * 10u))
            {
                gsp_vertex_dkr(gsp, r, va, n, dst, s_billboard);
                s_vtx_top += n;
            }
            break;
        }

        case DKR_G_TRIN:
        {
            /* gSPPolygon: a batch of DKR triangles. Verified layout
             * (GLideN64 F3DDKR_DMA_Tri + DKRTriangle struct):
             *   count    = ((w0 >> 20) & 0x0f) + 1
             *   texture  =  (w0 >> 16) & 0x0f
             *   w1       -> array of 16-byte DKRTriangle entries:
             *     +0 u8 flag  +1 u8 v0  +2 u8 v1  +3 u8 v2
             *     +4 s16 s0 +6 s16 t0  +8 s16 s1 +10 s16 t1
             *     +12 s16 s2 +14 s16 t2
             * The S/T are per-vertex S10.5 texel coords carried on the
             * triangle (not the vertex), so patch them into the three cached
             * vertices before emitting. flag bit 0x40 selects cull mode but a
             * triangle is still drawn; we emit unconditionally (the rasterizer
             * honours the geometry-mode cull). */
            int num_tris = (int)((w0 >> 20) & 0x0fu) + 1;
            int tex_en   = (int)((w0 >> 16) & 0x0fu);
            unsigned int ta = seg_phys(w1);
            int oldtex = s_textured;
            int t;
            if (tex_en) s_textured = 1;
            if (in_range(ta, (unsigned int)num_tris * 16u))
            {
                for (t = 0; t < num_tris; t++)
                {
                    unsigned int e = ta + (unsigned int)t * 16u;
                    unsigned int flag = r[(e + 0) ^ 3];
                    int v0 = (int)r[(e + 1) ^ 3];
                    int v1 = (int)r[(e + 2) ^ 3];
                    int v2 = (int)r[(e + 3) ^ 3];
                    int32_t cmdw[220];
                    int nc;
                    /* Reject triangles whose indices fall outside the vertex
                     * buffer or past the highest vertex actually loaded this
                     * batch. Without this a malformed or partially-restored
                     * (savestate) display list indexes stale/garbage vertices
                     * and gsp_triangle emits a degenerate RDP triangle that
                     * can wedge the rasterizer. */
                    if (v0 < 0 || v0 >= GSP_MAX_VERTICES
                        || v1 < 0 || v1 >= GSP_MAX_VERTICES
                        || v2 < 0 || v2 >= GSP_MAX_VERTICES)
                        continue;
                    /* Per-vertex texture coordinates: S is the first s16 of
                     * each pair (GLideN64 DKRTriangle {s0,t0,...}), not the
                     * second, and DKR's microcode uses the raw S10.5 values
                     * with no G_TEXTURE scale (its two G_TEXTURE commands
                     * carry scale 0). gsp_set_vertex_st stores the raw
                     * values directly on the DKR path. */
                    gsp_set_vertex_st(gsp, v0,
                        (int)(short)((r[(e + 4) ^ 3] << 8) | r[(e + 5) ^ 3]),
                        (int)(short)((r[(e + 6) ^ 3] << 8) | r[(e + 7) ^ 3]));
                    gsp_set_vertex_st(gsp, v1,
                        (int)(short)((r[(e + 8) ^ 3] << 8) | r[(e + 9) ^ 3]),
                        (int)(short)((r[(e + 10) ^ 3] << 8) | r[(e + 11) ^ 3]));
                    gsp_set_vertex_st(gsp, v2,
                        (int)(short)((r[(e + 12) ^ 3] << 8) | r[(e + 13) ^ 3]),
                        (int)(short)((r[(e + 14) ^ 3] << 8) | r[(e + 15) ^ 3]));
                    /* DKR culls per triangle, not from the geometry mode:
                     * plain triangles are backface-culled by the microcode,
                     * and flag bit 0x40 (RENDER_BACKFACE in the DKR
                     * decompilation) marks a triangle double-sided. The
                     * per-triangle flag REPLACES the geometry-mode cull bits
                     * entirely -- Mickey's Speedway USA runs its HUD-sprite
                     * batches with G_CULL_BACK set in the geometry mode and
                     * flag 0x40 on the triangles, and the cxd4 LLE stream
                     * draws them (passing the mode bits through culled all
                     * 29 such onscreen triangles). DKR itself never sets the
                     * GBI 1 cull bits, so this is unobservable there; its
                     * boot frame still emits 1428 triangles against the
                     * oracle's 1426 (unconditional emission gave 2122,
                     * culling both classes 827). */
                    gsp_set_geometry_mode(gsp, (flag & 0x40u)
                        ? (f3d_xlate_geom(s_geom) & ~0x600u)
                        : ((f3d_xlate_geom(s_geom) & ~0x600u) | 0x400u));
                    nc = gsp_triangle(gsp, cmdw, v0, v1, v2,
                                      s_textured, s_zbuffered);
                    if (nc > 0) rdp_fifo_append(fifo, cmdw, nc);
                }
                gsp_set_geometry_mode(gsp, f3d_xlate_geom(s_geom));
            }
            s_textured = oldtex;
            s_vtx_top = 0;   /* DKR resets the vertex cursor after a polygon batch */
            break;
        }

        case DKR_G_DL:
        {
            unsigned int da = seg_phys(w1);
            if (in_range(da, 8u))
                f3ddkr_run_dl(gsp, fifo, da, s_textured, s_zbuffered);
            break;
        }

        case DKR_G_DMADL:
        {
            /* gDkrDmaDisplayList: a DMA display-list call whose length is the
             * command COUNT carried in w0 bits 16..23 -- the sublist is NOT
             * G_ENDDL-terminated (DKR's models and the texture-header `cmd`
             * arrays are emitted as fixed-length blocks). Walk exactly that
             * many 8-byte commands. This is the primary path DKR geometry
             * arrives through, so treating it like G_DL (run-to-terminator)
             * either overran or under-ran every model block. */
            unsigned int num = (w0 >> 16) & 0xffu;
            unsigned int da  = seg_phys(w1);
            if (num > 0u && in_range(da, num * 8u))
                f3ddkr_run_dl_n(gsp, fifo, da, (int)num,
                                s_textured, s_zbuffered);
            break;
        }

        case DKR_G_MOVEWORD:
        {
            /* Index is the low byte of w0 (matches GLideN64 F3DDKR_MoveWord /
             * the gImmp21 G_MOVEWORD packing), not bits 16-23. */
            unsigned int index = w0 & 0xffu;
            if (index == DKR_MW_BILLBOARD)
                s_billboard = (w1 != 0u) ? 1 : 0;
            else if (index == DKR_MW_MVPMATRIX)
            {
                s_mtx_slot = (int)((w1 >> 6) & 0x03u);
                gsp_select_matrix_dkr(gsp, s_mtx_slot);
            }
            else if (index == DKR_MW_FOG)
            {
                /* Stock F3D G_MW_FOG: DKR's dispatch keeps the F3D MoveWord
                 * behaviour for the indices it does not redefine, and the
                 * boot frame carries one (multiplier 0x6400 offset 0x9867).
                 * Record the parameters; the cxd4 LLE stream shows non-zero
                 * shade-alpha (fog) lanes on DKR triangles, so the vertex
                 * fog consumer is a pending follow-up. The clip-ratio move
                 * (index 0x04, gSPClipRatio) also appears with ratio 2 --
                 * that matches the frontend default, so it needs no case. */
                gsp_set_fog(gsp, (int)(short)(w1 >> 16),
                                 (int)(short)(w1 & 0xffffu));
            }
            else if (index == DKR_MW_SEGMENT)
            {
                /* gSPSegment(seg,base): offset field = seg*4, so the segment
                 * number is bits 10-13 of w0. Store the FULL base word: the
                 * microcode's table keeps it whole and its pass-through
                 * address resolution is base + (w1 & 0xffffff), so high bits
                 * of the base survive into the emitted RDP words. Mickey's
                 * Speedway USA runs gSPSegment(0, 0x80000000) and the cxd4
                 * LLE stream carries SET_*_IMAGE addresses with bit 31 set;
                 * masking the base here dropped it. DMA reads go through
                 * seg_phys(), which still masks to a physical address. */
                unsigned int seg = (w0 >> 10) & 0x0fu;
                s_seg[seg] = w1;
            }
            break;
        }

        case DKR_G_MOVEMEM:
        {
            /* Stock F3D G_MOVEMEM; DKR uses it for the viewport (index
             * 0x80). The boot frame switches between a 160/120-scale
             * viewport and one with vscale.y = 160: with the command
             * unhandled the frontend default rendered every batch under
             * the second viewport with the wrong Y scale. */
            unsigned int mvidx = (w0 >> 16) & 0xffu;
            unsigned int ma    = seg_phys(w1);
            if (mvidx == 0x80u && in_range(ma, 16u))
                gsp_set_viewport(gsp, r, ma);
            break;
        }

        case DKR_G_SETGEOMETRYMODE:
            /* DKR keeps the GBI 1 geometry-mode bit layout; the shared
             * frontend interprets the F3DEX2 layout, where GBI 1's
             * G_SHADING_SMOOTH (0x200) lands on G_CULL_FRONT. Keep a raw
             * shadow and translate on write, exactly like the F3D walker,
             * or every smooth-shaded batch is front-culled. */
            s_geom |= w1;
            gsp_set_geometry_mode(gsp, f3d_xlate_geom(s_geom));
            break;

        case DKR_G_CLEARGEOMETRYMODE:
            s_geom &= ~w1;
            gsp_set_geometry_mode(gsp, f3d_xlate_geom(s_geom));
            break;

        case DKR_G_SETOTHERMODE_H:
        case DKR_G_SETOTHERMODE_L:
        {
            /* F3D partial other-modes write (DKR binds these to the stock
             * F3D_SetOtherMode_H/L).  shift/length are direct (not the
             * F3DEX2 inverted form) and w1 already holds the positioned bits.
             * DKR uses these to flip the cycle type to FILL for its color /
             * Z-buffer clears -- if they are dropped the Z clear never runs in
             * FILL mode, the Z buffer is left at "near", and every 3D pixel
             * fails the depth test (the whole 3D scene renders black behind
             * the 2D HUD).  Maintain the running H/L mode words and re-emit a
             * full RDP SET_OTHER_MODES so angrylion picks up the change. */
            unsigned int shift = (w0 >> 8) & 0xffu;
            unsigned int length = w0 & 0xffu;
            unsigned int mask;
            int32_t two[2];
            if (length >= 32u || shift >= 32u)
                mask = 0xffffffffu;
            else
                mask = ((1u << length) - 1u) << shift;
            if (cmd == DKR_G_SETOTHERMODE_H)
                s_othermode_h = (s_othermode_h & ~mask) | ((unsigned int)w1 & mask);
            else
                s_othermode_l = (s_othermode_l & ~mask) | ((unsigned int)w1 & mask);
            s_zbuffered = (((s_othermode_l >> 4) & 1u) ||
                           ((s_othermode_l >> 5) & 1u)) ? 1 : 0;
            /* The RSP zeroes shade alpha under the fogged world blend
             * (cycle-1 P mux == fog colour, othermode_l bits 31:30 == 3)
             * and passes vertex alpha through otherwise (the alpha-shaded
             * actors). Track it for gsp_vertex_dkr. */
            gsp_set_dkr_shade_alpha_zero(gsp,
                (((s_othermode_l >> 30) & 3u) == 3u) ? 1 : 0);
            two[0] = (int32_t)(s_othermode_h | (0x2fu << 24));
            two[1] = (int32_t)s_othermode_l;
            rdp_fifo_append(fifo, two, 2);
            break;
        }

        case JFG_G_DMA_OFFSETS:
            /* gSPSetDMAOffsets (JFG/Mickey build): w0's low 24 bits are a
             * base added to every G_MTX DMA address, w1's low 24 bits a base
             * added to every G_VTX DMA address. The G_TRIN triangle-array
             * address is NOT offset (GLideN64 gSPDMATriangles reads the raw
             * segment address; confirmed in the Mickey's Speedway race list,
             * where the triangle arrays sit just below the vertex base and
             * are addressed absolutely). The model blocks bracket their draw
             * with a set and a clear-to-zero of these. */
            if (s_jfg)
            {
                s_dma_mtx = w0 & 0x00ffffffu;
                s_dma_vtx = w1 & 0x00ffffffu;
            }
            break;

        case JFG_G_DMA_TEX_OFFSET:
            /* gSPSetDMATexOffset (JFG/Mickey build): points at a u16 table of
             * texture-image address shifts (texture-scroll animation, e.g.
             * water). Each following RGBA SET_TEXTURE_IMAGE adds the current
             * table entry to its address; each LOAD_BLOCK whose byte length
             * divides the shift advances the table cursor, and one that does
             * not switches the mechanism off (GLideN64 gSPSetDMATexOffset /
             * gDPSetTextureImage / gDPLoadBlock). Handled in the RDP
             * pass-through below. */
            if (s_jfg)
            {
                s_tex_ofs   = seg_phys(w1);
                s_tex_shift = 0u;
                s_tex_count = 0u;
            }
            break;

        case DKR_G_ENDDL:
            running = 0;
            break;

        case 0xe4: /* G_TEXRECT */
        case 0xe5: /* G_TEXRECTFLIP */
        {
            /* TEXTURE_RECTANGLE is a 4-word RDP command (angrylion reads 16
             * bytes for ids 0x24/0x25). DKR's gsDPTextureRectangle emits it as
             * two contiguous 64-bit words: the rect word pair (here in w0/w1)
             * followed immediately by the s/t and dsdx/dtdy word pair. (DKR
             * also has a gsSPTextureRectangle form that delivers the tail as
             * two G_RDPHALF immediate commands, 0xB3/0xB2; handle both.) The
             * plain pass-through forwarded only the first 2 words, so angrylion
             * consumed the following command as the texrect tail and the
             * rectangle never rendered -- this is why DKR's texture-rectangle
             * UI (the title logo, HUD) drew nothing under HLE. Assemble the
             * full 4-word command and advance past the consumed tail. */
            unsigned int n0 = rd32(r, pc);
            unsigned int n1 = rd32(r, pc + 4);
            int32_t tr[4];
            unsigned int tail_cmd = (n0 >> 24) & 0xffu;
            tr[0] = (int32_t)w0;
            tr[1] = (int32_t)w1;
            if (tail_cmd == 0xb3u || tail_cmd == 0xb2u
                || tail_cmd == 0xf1u || tail_cmd == 0xe1u)
            {
                /* RDPHALF form: two immediate commands carry the coords. */
                unsigned int m0, m1;
                tr[2] = (int32_t)n1;          /* first RDPHALF payload  */
                m0 = rd32(r, pc + 8);
                m1 = rd32(r, pc + 12);
                (void)m0;
                tr[3] = (int32_t)m1;          /* second RDPHALF payload */
                pc += 16;
            }
            else
            {
                /* contiguous gsDP form: the next word pair IS the coords. */
                tr[2] = (int32_t)n0;          /* s, t          */
                tr[3] = (int32_t)n1;          /* dsdx, dtdy    */
                pc += 8;
            }
            rdp_fifo_append(fifo, tr, 4);
            break;
        }

        default:
            /* RDP pass-through opcodes (0xC8..0xFF) and any DKR command not
             * yet decoded. The shared RDP commands (SET_*, TEXRECT, sync) are
             * forwarded verbatim so state/texture setup reaches angrylion. */
            if (cmd >= 0xc8 && cmd <= 0xff)
            {
                int rdp_id = cmd & 0x3f;
                if (rdp_id == 0x29)
                    rdp_fifo_fullsync_note();
                if (rdp_id >= 0x24 && rdp_id <= 0x3f
                    && rdp_id != 0x31 && rdp_id != 0x29)
                {
                    int32_t two[2];
                    two[0] = (int32_t)w0;
                    /* SET_*_IMAGE address handling, verified against the
                     * cxd4 LLE stream on Mickey's Speedway USA: a word with
                     * a nonzero segment nibble is resolved through the
                     * segment table (0x01000000 -> the seg-1 base), but a
                     * seg-0 word passes VERBATIM -- both 0x80xxxxxx CPU
                     * pointers and 0x00xxxxxx physical ones keep their top
                     * byte, and the game's gSPSegment(0, 0x80000000) is
                     * never applied to them. (The RDP masks the high bits,
                     * so verbatim CPU pointers are physically correct.) */
                    if ((rdp_id == 0x3f || rdp_id == 0x3e || rdp_id == 0x3d)
                        && ((w1 >> 24) & 0x0fu) != 0u)
                        two[1] = (int32_t)((s_seg[(w1 >> 24) & 0x0fu]
                                            + (w1 & 0x00ffffffu))
                                           & 0x00ffffffu);
                    else
                        two[1] = (int32_t)w1;
                    /* JFG/Mickey texture-scroll shift (G_DMA_TEX_OFFSET):
                     * an RGBA SET_TEXTURE_IMAGE adds the current u16 table
                     * entry to its address; a LOAD_BLOCK whose byte length
                     * divides the shift advances the table cursor, one that
                     * does not re-emits the image address unshifted and
                     * switches the mechanism off. The table entry read
                     * matches GLideN64's ((u16*)(RDRAM+ofs))[count^1]: on
                     * this host-native word storage the ^1 word swap makes
                     * it the guest big-endian u16 at index `count`. */
                    if (rdp_id == 0x3d && s_tex_ofs != 0u)
                    {
                        if (((w0 >> 21) & 0x7u) == 0u /* G_IM_FMT_RGBA */
                            && s_tex_ofs + (s_tex_count ^ 1u) * 2u + 2u
                               <= s_rdram_size)
                        {
                            unsigned int e = s_tex_ofs
                                           + ((s_tex_count ^ 1u) << 1);
                            s_tex_shift = (unsigned int)r[e]
                                        | ((unsigned int)r[e + 1u] << 8);
                            s_timg_w0 = (unsigned int)w0;
                            s_timg_base = (unsigned int)two[1];
                            s_timg_shifted = 1;
                            two[1] = (int32_t)((unsigned int)two[1]
                                               + s_tex_shift);
                        }
                        else
                        {
                            s_tex_ofs   = 0u;
                            s_tex_shift = 0u;
                            s_tex_count = 0u;
                            s_timg_shifted = 0;
                        }
                    }
                    else if (rdp_id == 0x33 && s_tex_ofs != 0u
                             && s_timg_shifted)
                    {
                        unsigned int lrs = (w1 >> 12) & 0xfffu;
                        if (s_tex_shift % (((lrs >> 2) + 1u) << 3) != 0u)
                        {
                            int32_t fix[2];
                            fix[0] = (int32_t)s_timg_w0;
                            fix[1] = (int32_t)s_timg_base;
                            rdp_fifo_append(fifo, fix, 2);
                            s_tex_ofs   = 0u;
                            s_tex_shift = 0u;
                            s_tex_count = 0u;
                        }
                        else
                            s_tex_count++;
                        s_timg_shifted = 0;
                    }
                    /* A raw full SET_OTHER_MODES (0xEF) in the display list
                     * does NOT reseed the running H/L merge mirror: that is
                     * initialized from the data-segment default by
                     * f3ddkr_seed_othermode() and updated only by the
                     * G_SETOTHERMODE_H/L partial writes. (Verified against
                     * the cxd4 LLE stream on Mickey's Speedway USA: a merged
                     * partial emitted right after a raw 0xEF still carries
                     * the microcode-default low bits, not the 0xEF's.)
                     *
                     * The vertex path's rendermode-derived state is separate:
                     * the shade-alpha gate and the z-buffered triangle class
                     * DO track a raw 0xEF's L word. (Verified against the
                     * cxd4 stream on Diddy Kong Racing's character select,
                     * which sets its fogged-world rendermode with raw 0xEF
                     * commands: gating shade alpha on the seed+partial mirror
                     * alone doubled the shade mismatches, 749 vs 370 of 777
                     * triangles, and rendered the 3D scene black.) */
                    if (rdp_id == 0x2f)
                    {
                        s_zbuffered = ((((unsigned int)w1 >> 4) & 1u) ||
                                       (((unsigned int)w1 >> 5) & 1u)) ? 1 : 0;
                        gsp_set_dkr_shade_alpha_zero(gsp,
                            ((((unsigned int)w1 >> 30) & 3u) == 3u) ? 1 : 0);
                    }
                    rdp_fifo_append(fifo, two, 2);
                }
            }
            break;
        }
    }

    s_dl_depth--;
}
