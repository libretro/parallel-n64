/* rdp_emit_f3d.c -- F3D (Fast3D / "RSP SW 2.0X") display-list dispatcher for
 * the angrylion HLE path. See rdp_emit_f3d.h.
 *
 * Self-contained: keeps its own segment table / RDRAM pointer / other-modes
 * mirror so it never perturbs the F3DEX2 dispatcher's state (SM64 and the
 * F3DEX2 titles never run in the same task). Geometry is routed to the shared
 * frontend (gsp_*) and the RDP command FIFO (rdp_fifo_append) exactly as the
 * F3DEX2 path does; only the command decode differs.
 *
 * Opcode/argument layout follows the documented F3D microcode (the same decode
 * the in-tree gln64 F3D path uses); no external plugin code is linked.
 *
 * Build check:
 *   gcc -std=c89 -pedantic -Wall -Wdeclaration-after-statement -Werror
 */

#include "rdp_emit_f3d.h"
#include "rdp_emit_frontend.h"

/* ---- F3D command bytes (high byte of w0) ---------------------------------*/
#define F3D_MTX                0x01
#define F3D_MOVEMEM            0x03
#define F3D_VTX                0x04
#define F3D_DL                 0x06
#define F3D_RDPHALF_CONT       0xB2
#define F3D_RDPHALF_2          0xB3
#define F3D_RDPHALF_1          0xB4
#define F3D_LINE3D             0xB5   /* a.k.a. G_QUAD on F3D variants */
#define F3D_CLEARGEOMETRYMODE  0xB6
#define F3D_SETGEOMETRYMODE    0xB7
#define F3D_ENDDL              0xB8
#define F3D_SETOTHERMODE_L     0xB9
#define F3D_SETOTHERMODE_H     0xBA
#define F3D_TEXTURE            0xBB
#define F3D_MOVEWORD           0xBC
#define F3D_POPMTX             0xBD
#define F3D_CULLDL             0xBE
#define F3D_TRI1               0xBF

/* G_MTX param bits (F3D: NOT inverted, unlike F3DEX2's push bit) */
#define F3D_MTX_PROJECTION     0x01
#define F3D_MTX_LOAD           0x02
#define F3D_MTX_PUSH           0x04

/* G_DL sub-op (w0 >> 16) */
#define F3D_DL_PUSH            0x00
#define F3D_DL_NOPUSH          0x01

/* G_MOVEMEM index (w0 >> 16) */
#define F3D_MV_VIEWPORT        0x80
#define F3D_MV_LOOKATY         0x82
#define F3D_MV_LOOKATX         0x84
#define F3D_MV_L0              0x86
#define F3D_MV_MATRIX_1        0x9E

/* G_MOVEWORD index (w0 & 0xff) */
#define F3D_MW_MATRIX          0x00
#define F3D_MW_NUMLIGHT        0x02
#define F3D_MW_CLIP            0x04
#define F3D_MW_SEGMENT         0x06
#define F3D_MW_FOG             0x08
#define F3D_MW_LIGHTCOL        0x0a
#define F3D_MW_POINTS          0x0c
#define F3D_MW_PERSPNORM       0x0e

#define F3D_DL_MAX_DEPTH       32

/* ---- self-contained state ------------------------------------------------*/
static unsigned char *s_rdram;
static unsigned int   s_rdram_size;
static unsigned int   s_seg[16];
static int            s_textured;
static int            s_zbuffered;
static unsigned int   s_othermode_h;
static unsigned int   s_othermode_l;
static int            s_dl_depth;
static unsigned int   s_geom;   /* geometry mode in native F3D bit layout */

static unsigned int rd32(const unsigned char *r, unsigned int a)
{
    unsigned int limit = s_rdram_size ? s_rdram_size : (8u * 1024u * 1024u);
    if (a + 4u > limit)
        return 0u;
    /* RDRAM is stored host-native (byte-swapped on load); read native, the
     * same way the F3DEX2 walker and the rasterizer's command fetch do. */
    return *(const unsigned int *)(r + a);
}

/* Resolve a segmented/virtual address: seg_table[(w1>>24)&0xf] + low24. KSEG0
 * pointers (top byte 0x80/0xA0) map through segment 0 (base 0). */
static unsigned int seg_rsp(unsigned int w1)
{
    unsigned int seg = (w1 >> 24) & 0x0fu;
    return s_seg[seg] + (w1 & 0x00ffffffu);
}

static unsigned int seg_phys(unsigned int w1)
{
    return seg_rsp(w1) & 0x00ffffffu;
}

static int in_range(unsigned int a, unsigned int bytes)
{
    unsigned int limit = s_rdram_size ? s_rdram_size : (8u * 1024u * 1024u);
    return (a < limit) && (bytes <= limit) && (a + bytes <= limit);
}

/* ---- setup ---------------------------------------------------------------*/
/* F3D packs the cull / smooth-shade geometry-mode bits differently from
 * F3DEX2; the shared frontend interprets the mode in the F3DEX2 layout. Remap
 * the three bits that moved so culling and flat/smooth shading are correct:
 *   F3D G_SHADING_SMOOTH 0x000200 -> F3DEX2 0x200000
 *   F3D G_CULL_FRONT     0x001000 -> F3DEX2 0x000200
 *   F3D G_CULL_BACK      0x002000 -> F3DEX2 0x000400
 * All other bits (G_ZBUFFER/G_SHADE/G_FOG/G_LIGHTING/G_TEXTURE_GEN/...) share
 * the same position in both. */
static unsigned int f3d_xlate_geom(unsigned int m)
{
    unsigned int o = m & ~(0x000200u | 0x001000u | 0x002000u);
    if (m & 0x000200u) o |= 0x200000u;
    if (m & 0x001000u) o |= 0x000200u;
    if (m & 0x002000u) o |= 0x000400u;
    return o;
}

void f3d_seg_reset(void)
{
    int i;
    for (i = 0; i < 16; i++)
        s_seg[i] = 0u;
    s_textured  = 0;
    s_zbuffered = 0;
    s_dl_depth  = 0;
    s_geom      = 0;
}

void f3d_set_rdram(unsigned char *rdram)        { s_rdram = rdram; }
void f3d_set_rdram_size(unsigned int size)      { s_rdram_size = size; }

void f3d_set_othermode_init(unsigned int h, unsigned int l)
{
    s_othermode_h = h | (0x2fu << 24);
    s_othermode_l = l;
    s_zbuffered = (((s_othermode_l >> 4) & 1u) ||
                   ((s_othermode_l >> 5) & 1u)) ? 1 : 0;
}

/* SM64's F3D ucode signature: rd32(text+4) (native host word of the second
 * ucode-text word). F3DEX2/S2DEX2/L3DEX2 builds carry 0xc81f20xx here; the
 * Fast3D builds do not. Matching positively on the known F3D word keeps the
 * F3DEX2 titles on their existing path unchanged. */
int f3d_is_ucode(const unsigned char *rdram, unsigned int rdram_size,
                 unsigned int text)
{
    unsigned int w1;
    unsigned int limit = rdram_size ? rdram_size : (8u * 1024u * 1024u);
    if (rdram == 0 || text == 0 || text + 8u > limit)
        return 0;
    w1 = *(const unsigned int *)(rdram + text + 4u);
    if (w1 == 0x201d0110u)      /* RSP SW Version 2.0X (Super Mario 64) */
        return 1;
    return 0;
}

/* ---- RDP pass-through (microcode-independent), mirrors rdp_emit_f3dex2.c --*/
static void rdp_passthrough(GSPState *gsp, RdpFifo *fifo, int cmd,
                            unsigned int w0, unsigned int w1)
{
    int rdp_id = cmd & 0x3f;

    /* Set Other Modes (0xEF -> 0x2f): depth enables select the Z tri variant */
    if (rdp_id == 0x2f)
    {
        int zc = (int)((w1 >> 4) & 1);
        int zu = (int)((w1 >> 5) & 1);
        s_zbuffered = (zc || zu) ? 1 : 0;
    }
    /* Set Tile (0xF5 -> 0x35): cache the wrap mask exponents the texcoord fold
     * in gsp_triangle needs. */
    if (rdp_id == 0x35)
    {
        unsigned int ti = (w1 >> 24) & 7;
        gsp->tile_mask_t[ti] = (unsigned char)((w1 >> 14) & 0xf);
        gsp->tile_mask_s[ti] = (unsigned char)((w1 >> 4) & 0xf);
    }
    /* 0x24..0x3f are the RDP non-triangle commands angrylion implements; skip
     * 0x31 (G_SETKEY*, unimplemented) and 0x29 (SYNC_FULL, the activation
     * appends exactly one frame terminator itself). */
    if (rdp_id >= 0x24 && rdp_id <= 0x3f && rdp_id != 0x31 && rdp_id != 0x29)
    {
        int32_t two[2];
        two[0] = (int32_t)w0;
        /* SET_COLOR/Z/TEXTURE_IMAGE carry a (possibly segmented) DRAM pointer
         * in w1; resolve it the way the RSP does before the RDP sees it. */
        if (rdp_id == 0x3f || rdp_id == 0x3e || rdp_id == 0x3d)
            two[1] = (int32_t)seg_rsp(w1);
        else
            two[1] = (int32_t)w1;
        rdp_fifo_append(fifo, two, 2);
    }
}

/* ---- the F3D walker ------------------------------------------------------*/
void f3d_run_dl(GSPState *gsp, RdpFifo *fifo, unsigned int addr,
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
    if (s_dl_depth == 0)
        /* Fast3D clips triangles against the near plane (z + w >= 0); it is
         * not a NoN ("no near clip") microcode like F3DEX2, so enable the
         * frontend's near-plane clip for this task instead of letting
         * near-crossing geometry be disposed by the guard band. */
        gsp->clip_near_z = 1;
    if (s_dl_depth >= F3D_DL_MAX_DEPTH)
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
        case F3D_MTX:
        {
            int projection = (int)((w0 >> 16) & F3D_MTX_PROJECTION) ? 1 : 0;
            int load       = (int)((w0 >> 16) & F3D_MTX_LOAD)       ? 1 : 0;
            int push       = (int)((w0 >> 16) & F3D_MTX_PUSH)       ? 1 : 0;
            unsigned int ma = seg_phys(w1);
            if ((w0 & 0xffffu) == 64u && in_range(ma, 64u))
                gsp_matrix_load(gsp, r, ma, projection, load, push);
            break;
        }

        case F3D_POPMTX:
            /* F3D pops one modelview level (w1 selects modelview/projection). */
            gsp_matrix_pop(gsp);
            break;

        case F3D_VTX:
        {
            int n  = (int)((w0 >> 20) & 0x0f) + 1;
            int v0 = (int)((w0 >> 16) & 0x0f);
            unsigned int va = seg_phys(w1);
            if (n > 0 && in_range(va, (unsigned int)n * 16u))
                gsp_vertex(gsp, r, va, n, v0);
            break;
        }

        case F3D_TRI1:
        {
            int a = (int)((w1 >> 16) & 0xff) / 10;
            int b = (int)((w1 >>  8) & 0xff) / 10;
            int c = (int)((w1 >>  0) & 0xff) / 10;
            int32_t cw[220];
            int nc = gsp_triangle(gsp, cw, a, b, c, s_textured, s_zbuffered);
            if (nc > 0) rdp_fifo_append(fifo, cw, nc);
            break;
        }

        case F3D_LINE3D:
        {
            /* G_QUAD on F3D builds that use 0xB5 for quads: four indices in w1,
             * emitted as two triangles (a,b,c) + (a,c,d). Lines proper are not
             * modeled; the quad reading is harmless for line content. */
            int a = (int)((w1 >> 24) & 0xff) / 10;
            int b = (int)((w1 >> 16) & 0xff) / 10;
            int c = (int)((w1 >>  8) & 0xff) / 10;
            int d = (int)((w1 >>  0) & 0xff) / 10;
            int32_t cw[220];
            int nc;
            nc = gsp_triangle(gsp, cw, a, b, c, s_textured, s_zbuffered);
            if (nc > 0) rdp_fifo_append(fifo, cw, nc);
            nc = gsp_triangle(gsp, cw, a, c, d, s_textured, s_zbuffered);
            if (nc > 0) rdp_fifo_append(fifo, cw, nc);
            break;
        }

        case F3D_DL:
        {
            int nopush = (int)((w0 >> 16) & 0xff) == F3D_DL_NOPUSH;
            unsigned int da = seg_phys(w1);
            if (in_range(da, 8u))
                f3d_run_dl(gsp, fifo, da, s_textured, s_zbuffered);
            if (nopush)
                running = 0;   /* branch (no return) ends this list */
            break;
        }

        case F3D_ENDDL:
            running = 0;
            break;

        case F3D_SETGEOMETRYMODE:
            s_geom |= w1;
            gsp_set_geometry_mode(gsp, f3d_xlate_geom(s_geom));
            break;

        case F3D_CLEARGEOMETRYMODE:
            s_geom &= ~w1;
            gsp_set_geometry_mode(gsp, f3d_xlate_geom(s_geom));
            break;

        case F3D_TEXTURE:
        {
            int on    = (int)(w0 & 0xff);
            int level = (int)((w0 >> 11) & 0x07);
            int tile  = (int)((w0 >> 8) & 0x07);
            unsigned int ss = (unsigned int)((w1 >> 16) & 0xffff);
            unsigned int ts = (unsigned int)(w1 & 0xffff);
            s_textured = (on != 0) ? 1 : 0;
            gsp_set_texture(gsp, ss, ts, tile, level, gsp->tex_w, gsp->tex_h);
            break;
        }

        case F3D_SETOTHERMODE_H:
        case F3D_SETOTHERMODE_L:
        {
            /* F3D partial other-modes write: shift and bit-length are direct
             * (not the F3DEX2 32-shift-length inverted form). w1 holds the new
             * field bits already positioned. Merge and re-emit a full RDP
             * SET_OTHER_MODES so angrylion picks up the cycle type / render
             * mode. */
            unsigned int shift = (w0 >> 8) & 0xffu;
            unsigned int length = w0 & 0xffu;
            unsigned int mask;
            int32_t two[2];
            if (length >= 32u || shift >= 32u)
                mask = 0xffffffffu;
            else
                mask = ((1u << length) - 1u) << shift;
            if (cmd == F3D_SETOTHERMODE_H)
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

        case F3D_MOVEMEM:
        {
            unsigned int idx = (w0 >> 16) & 0xffu;
            unsigned int ma  = seg_phys(w1);
            if (idx == F3D_MV_VIEWPORT)
            {
                if (in_range(ma, 16u))
                    gsp_set_viewport(gsp, r, ma);
            }
            else if (idx == F3D_MV_LOOKATX)
            {
                if (in_range(ma, 16u))
                    gsp_set_lookat(gsp, r, ma, 0);
            }
            else if (idx == F3D_MV_LOOKATY)
            {
                if (in_range(ma, 16u))
                    gsp_set_lookat(gsp, r, ma, 1);
            }
            else if (idx >= F3D_MV_L0)
            {
                /* G_MV_L0/L1/...: directional/ambient lights, step 2 per slot */
                int li = (int)((idx - F3D_MV_L0) >> 1);
                if (li >= 0 && li < GSP_MAX_LIGHTS && in_range(ma, 16u))
                    gsp_set_light(gsp, r, ma, li);
            }
            break;
        }

        case F3D_MOVEWORD:
        {
            unsigned int index = w0 & 0xffu;
            switch (index)
            {
            case F3D_MW_SEGMENT:
                s_seg[(w0 >> 10) & 0x0fu] = w1;
                break;
            case F3D_MW_NUMLIGHT:
                gsp_set_num_lights(gsp,
                    (int)(((w1 - 0x80000000u) >> 5) - 1u));
                break;
            case F3D_MW_FOG:
                gsp_set_fog(gsp, (int)(int16_t)(w1 >> 16),
                                 (int)(int16_t)(w1 & 0xffff));
                break;
            case F3D_MW_PERSPNORM:
                gsp_set_persp_norm(gsp, w1 & 0xffffu);
                break;
            case F3D_MW_POINTS:
            {
                unsigned int val = (w0 >> 8) & 0xffffu;
                gsp_modify_vertex(gsp, (int)(val / 40u), val % 40u, w1);
                break;
            }
            case F3D_MW_MATRIX:
            {
                /* gSPInsertMatrix: patch one 32-bit word of the combined MVP
                 * (two s16 halves). Offsets 0x00-0x1f the integer halves,
                 * 0x20-0x3f the fractions. */
                unsigned int off = (w0 >> 8) & 0xffffu;
                if (off < 0x40u && (off & 3u) == 0u)
                {
                    int mr = (int)((off & 0x1fu) >> 3);
                    int mc = (int)((off & 7u) >> 1);
                    int32_t *e0 = &gsp->combined[mr][mc];
                    int32_t *e1 = &gsp->combined[mr][mc + 1];
                    if (off < 0x20u)
                    {
                        *e0 = (int32_t)((w1 & 0xffff0000u)
                                        | ((uint32_t)*e0 & 0xffffu));
                        *e1 = (int32_t)(((w1 & 0xffffu) << 16)
                                        | ((uint32_t)*e1 & 0xffffu));
                    }
                    else
                    {
                        *e0 = (int32_t)(((uint32_t)*e0 & 0xffff0000u)
                                        | ((w1 >> 16) & 0xffffu));
                        *e1 = (int32_t)(((uint32_t)*e1 & 0xffff0000u)
                                        | (w1 & 0xffffu));
                    }
                }
                break;
            }
            default:
                break;
            }
            break;
        }

        case F3D_CULLDL:
            /* gSPCullDisplayList: rejects the whole list if a vertex span is
             * fully off-screen. Not modeled -- drawing the content is correct,
             * only slower. */
            break;

        case F3D_RDPHALF_CONT:
        case F3D_RDPHALF_2:
            /* Staged texrect coordinates; consumed by the G_TEXRECT handler
             * below. A stray RDPHALF outside a texrect is a no-op. */
            break;

        case F3D_RDPHALF_1:
            /* On the old F3D microcode (retail Super Mario 64) gSPPerspNormalize
             * is not a G_MOVEWORD: it stores the perspective-normalize factor in
             * the RDPHALF_1 slot with a bare G_RDPHALF_1 word, and the vertex
             * transform reads perspNorm from that slot. (The newer GBI / F3DEX2
             * path uses G_MOVEWORD/G_MW_PERSPNORM, handled above.) The texrect
             * tail RDPHALF words are consumed inline by the 0xE4/0xE5 assembler
             * and never reach here, so a G_RDPHALF_1 seen at this point is the
             * perspective-normalize write; mirror the slot into perspNorm. */
            gsp_set_persp_norm(gsp, w1 & 0xffffu);
            break;

        default:
            /* TEXTURE_RECTANGLE (0xE4) / _FLIP (0xE5) is a 4-word RDP command,
             * but F3D delivers it split: the G_TEXRECT word pair carries the
             * rectangle + tile, and the following two G_RDPHALF commands carry
             * the texture coordinates -- RDPHALF_2 (0xB3) the s/t pair and
             * RDPHALF_1 (0xB4) the dsdx/dtdy pair. Forwarding only the first
             * two words leaves angrylion to consume the next command as the
             * rectangle's tail, corrupting the HUD and sky tiles. Assemble the
             * full command and skip the two RDPHALF words consumed. */
            if (cmd == 0xe4 || cmd == 0xe5)
            {
                unsigned int h_st = 0u, h_dsdt = 0u;
                int32_t tr[4];
                int k;
                /* The two coordinate words follow as RDPHALF-family commands.
                 * SM64 uses RDPHALF_2 (0xB3) for the s/t pair and RDPHALF_CONT
                 * (0xB2) for the dsdx/dtdy pair, but assign by position (first
                 * tail word -> s/t = RDP word2, second -> dsdx/dtdy = word3) so
                 * the exact opcode the build chose does not matter. */
                for (k = 0; k < 2; k++)
                {
                    unsigned int c0 = rd32(r, pc);
                    int hc = (int)((c0 >> 24) & 0xff);
                    if (hc != F3D_RDPHALF_CONT && hc != F3D_RDPHALF_2 &&
                        hc != F3D_RDPHALF_1)
                        break;
                    if (k == 0)
                        h_st = rd32(r, pc + 4);
                    else
                        h_dsdt = rd32(r, pc + 4);
                    pc += 8;
                }
                tr[0] = (int32_t)w0;
                tr[1] = (int32_t)w1;
                tr[2] = (int32_t)h_st;
                tr[3] = (int32_t)h_dsdt;
                rdp_fifo_append(fifo, tr, 4);
            }
            else if (cmd >= 0xc8)
                rdp_passthrough(gsp, fifo, cmd, w0, w1);
            break;
        }
    }

    s_dl_depth--;
}
