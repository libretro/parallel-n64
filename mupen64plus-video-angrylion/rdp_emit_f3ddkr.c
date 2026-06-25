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

#if defined(_MSC_VER) && (_MSC_VER < 1600)
typedef signed   __int32 dkr_int32_t;
#else
#include <stdint.h>
#endif

/* ---- F3DDKR command opcodes (from f3ddkr.h) ----------------------------- */
#define DKR_G_MTX     0x01
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

/* G_MOVEWORD indices used by DKR */
#define DKR_MW_BILLBOARD 0x02
#define DKR_MW_MVPMATRIX 0x0A

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
static int   s_vtx_top   = 0;   /* next free vertex slot for G_VTX_APPEND */

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
}

void f3ddkr_set_rdram(unsigned char *rdram)   { s_rdram = rdram; }
void f3ddkr_set_rdram_size(unsigned int size) { s_rdram_size = size; }

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
    unsigned short tbl[40];
    unsigned short counts_val[40];
    int counts_n[40];
    int i, j, ncv, best, op7;
    (void)uds;
    if (rdram == 0 || ud == 0 || ud + 0xbc + 40u * 2u > rdram_size)
        return 0;
    /* read the dispatch table */
    for (i = 0; i < 40; i++)
        tbl[i] = (unsigned short)(((unsigned int)rdram[(ud + 0xbc + i * 2u) ^ 3] << 8)
                                 | (unsigned int)rdram[(ud + 0xbd + i * 2u) ^ 3]);
    /* find the most frequent entry (the null/NOP handler) */
    ncv = 0;
    for (i = 0; i < 40; i++)
    {
        for (j = 0; j < ncv; j++)
            if (counts_val[j] == tbl[i]) { counts_n[j]++; break; }
        if (j == ncv) { counts_val[ncv] = tbl[i]; counts_n[ncv] = 1; ncv++; }
    }
    best = 0;
    for (j = 1; j < ncv; j++)
        if (counts_n[j] > counts_n[best]) best = j;
    /* opcode 0x07 = G_DMADL: a real (non-null) handler marks F3DDKR. Require
     * the table to actually be a dispatch table (the null entry must repeat,
     * i.e. several NOP opcodes) so random data does not pass. */
    op7 = (int)tbl[7];
    if (counts_n[best] < 4)
        return 0;
    return (op7 != (int)counts_val[best]) ? 1 : 0;
}

/* ---- the F3DDKR walker --------------------------------------------------- */
void f3ddkr_run_dl(GSPState *gsp, RdpFifo *fifo, unsigned int addr,
                   int textured, int z_buffered)
{
    int guard = 0;
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
        unsigned int w0 = rd32(r, pc);
        unsigned int w1 = rd32(r, pc + 4);
        int cmd = (int)((w0 >> 24) & 0xff);
        pc += 8;

        switch (cmd)
        {
        case DKR_G_MTX:
        {
            /* gSPMatrixDKR: param (w0 low bits) carries (slot) << 6 plus the
             * F3D load/projection bits. The matrix is 64 bytes in RDRAM. The
             * indexed slot selects which of the DKR matrix registers receives
             * it; we map slot 0 -> modelview load, and treat the active MVP
             * (s_mtx_slot) as the combined transform source. */
            int projection = (int)((w0 >> 16) & 0x01) ? 1 : 0;
            int load       = (int)((w0 >> 16) & 0x02) ? 1 : 0;
            int push       = (int)((w0 >> 16) & 0x04) ? 1 : 0;
            unsigned int ma = seg_phys(w1);
            if (in_range(ma, 64u))
            {
                gsp_matrix_load(gsp, r, ma, projection, load, push);
                gsp_combine_matrices(gsp);
            }
            break;
        }

        case DKR_G_VTX:
        {
            /* gSPVertexDKR. Recover n and the append flag from the parameter
             * word (w0 bits 16..23 per the gDma1p packing; w0 low 16 bits hold
             * the DMA byte length). The append flag (G_VTX_APPEND, bit 0)
             * places this batch after the previously-loaded vertices instead
             * of resetting the buffer to index 0. The frontend's gsp_vertex
             * has no append state, so the walker tracks the write base. */
            unsigned int param = (w0 >> 16) & 0xffu;
            int n  = (int)((param >> 3) & 0x1fu) + 1;
            int append = (int)(param & 0x01u);   /* G_VTX_APPEND */
            unsigned int va = seg_phys(w1);
            int v0 = append ? s_vtx_top : 0;
            /* TODO(billboard): when s_billboard is set, these vertices must be
             * offset by vertex 0 post-MVP / pre-perspective-divide. Not yet
             * implemented; plain 3D geometry (non-billboard) is unaffected. */
            if (n > 0 && v0 >= 0 && v0 + n <= GSP_MAX_VERTICES
                && in_range(va, (unsigned int)n * 16u))
            {
                gsp_vertex(gsp, r, va, n, v0);
                s_vtx_top = v0 + n;
            }
            break;
        }

        case DKR_G_TRIN:
        {
            /* gSPPolygon: batched triangle list. numTris-1 is in w0 bits
             * 20..27 (the macro shifts ((numTris-1)<<4|texEnabled) left 16),
             * texEnabled in bit 16; w1 -> triangle array (4 bytes each). */
            unsigned int hdr = (w0 >> 16) & 0xffu;
            int num_tris = (int)((hdr >> 4) & 0x0fu) + 1;
            int tex_en   = (int)(hdr & 0x01u);
            unsigned int ta = seg_phys(w1);
            int oldtex = s_textured;
            int t;
            if (tex_en) s_textured = 1;
            if (in_range(ta, (unsigned int)num_tris * 4u))
            {
                for (t = 0; t < num_tris; t++)
                {
                    unsigned int e = ta + (unsigned int)t * 4u;
                    int i0 = (int)r[(e + 0) ^ 3];
                    int i1 = (int)r[(e + 1) ^ 3];
                    int i2 = (int)r[(e + 2) ^ 3];
                    /* byte 3 = per-triangle flags (e.g. TRIN_*_TEXTURE); the
                     * texture-enable is honoured at the batch level above. */
                    int32_t cmdw[220];
                    int nc = gsp_triangle(gsp, cmdw, i0, i1, i2,
                                          s_textured, s_zbuffered);
                    if (nc > 0) rdp_fifo_append(fifo, cmdw, nc);
                }
            }
            s_textured = oldtex;
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
            /* gDkrDmaDisplayList: like G_DL but the count is carried in w0
             * (bits 16..23) rather than relying on a G_ENDDL terminator. We
             * still walk to the embedded terminator, which is equivalent for
             * a well-formed list. */
            unsigned int da = seg_phys(w1);
            if (in_range(da, 8u))
                f3ddkr_run_dl(gsp, fifo, da, s_textured, s_zbuffered);
            break;
        }

        case DKR_G_MOVEWORD:
        {
            unsigned int index = (w0 >> 16) & 0xffu;
            if (index == DKR_MW_BILLBOARD)
                s_billboard = (w1 != 0u) ? 1 : 0;
            else if (index == DKR_MW_MVPMATRIX)
                s_mtx_slot = (int)((w1 >> 6) & 0x03u);
            break;
        }

        case DKR_G_SETGEOMETRYMODE:
            gsp_set_geometry_mode(gsp, gsp_get_geometry_mode(gsp) | w1);
            break;

        case DKR_G_CLEARGEOMETRYMODE:
            gsp_set_geometry_mode(gsp, gsp_get_geometry_mode(gsp) & ~w1);
            break;

        case DKR_G_ENDDL:
            running = 0;
            break;

        default:
            /* RDP pass-through opcodes (0xC8..0xFF) and any DKR command not
             * yet decoded. The shared RDP commands (SET_*, TEXRECT, sync) are
             * forwarded verbatim so state/texture setup reaches angrylion. */
            if (cmd >= 0xc8 && cmd <= 0xff)
            {
                int rdp_id = cmd & 0x3f;
                if (rdp_id >= 0x24 && rdp_id <= 0x3f
                    && rdp_id != 0x31 && rdp_id != 0x29)
                {
                    int32_t two[2];
                    two[0] = (int32_t)w0;
                    if (rdp_id == 0x3f || rdp_id == 0x3e || rdp_id == 0x3d)
                        two[1] = (int32_t)seg_rsp(w1);
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
