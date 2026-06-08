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

static unsigned int rd_u32_be(const unsigned char *r, unsigned int a)
{
    return ((unsigned int)r[a] << 24) | ((unsigned int)r[a + 1] << 16)
         | ((unsigned int)r[a + 2] << 8) | (unsigned int)r[a + 3];
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

/* mask the lower 24 bits to an RDRAM byte address */
static unsigned int seg_addr(unsigned int w1)
{
    return w1 & 0x00ffffffu;
}

void f3dex2_run_dl(GSPState *gsp, RdpFifo *fifo, unsigned int addr,
                   int textured, int z_buffered)
{
    int guard = 0;
    unsigned int pc = addr;
    int running = 1;

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
            gsp_matrix_load(gsp, r, seg_addr(w1), projection, load, push);
            break;
        }
        case F3DEX2_POPMTX:
            gsp_matrix_pop(gsp);
            break;

        case F3DEX2_VTX:
        {
            int n  = (int)((w0 >> 12) & 0xff);
            int v0 = (int)((w0 >> 1) & 0x7f) - n;
            gsp_vertex(gsp, r, seg_addr(w1), n, v0);
            break;
        }

        case F3DEX2_TRI1:
        {
            int a = (int)((w0 >> 17) & 0x7f) / 2;
            int b = (int)((w0 >> 9) & 0x7f) / 2;
            int c = (int)((w0 >> 1) & 0x7f) / 2;
            int32_t cmdw[44];
            int nc = gsp_triangle(gsp, cmdw, a, b, c, textured, z_buffered);
            if (nc > 0) rdp_fifo_append(fifo, cmdw, nc);
            break;
        }

        case F3DEX2_TRI2:
        case F3DEX2_QUAD:
        {
            int a0 = (int)((w0 >> 17) & 0x7f) / 2;
            int b0 = (int)((w0 >> 9) & 0x7f) / 2;
            int c0 = (int)((w0 >> 1) & 0x7f) / 2;
            int a1 = (int)((w1 >> 17) & 0x7f) / 2;
            int b1 = (int)((w1 >> 9) & 0x7f) / 2;
            int c1 = (int)((w1 >> 1) & 0x7f) / 2;
            int32_t cmdw[44];
            int nc;
            nc = gsp_triangle(gsp, cmdw, a0, b0, c0, textured, z_buffered);
            if (nc > 0) rdp_fifo_append(fifo, cmdw, nc);
            nc = gsp_triangle(gsp, cmdw, a1, b1, c1, textured, z_buffered);
            if (nc > 0) rdp_fifo_append(fifo, cmdw, nc);
            break;
        }

        case F3DEX2_DL:
        {
            int nopush = (int)((w0 >> 16) & 0xff) & DL_NOPUSH;
            f3dex2_run_dl(gsp, fifo, seg_addr(w1), textured, z_buffered);
            if (nopush)
                running = 0; /* branch (no return) ends this list */
            break;
        }

        case F3DEX2_ENDDL:
            running = 0;
            break;

        case F3DEX2_TEXTURE:
            /* G_TEXTURE sets the texture scale/tile/level the frontend uses
             * for emitted texcoords. w1 holds the S/T scale (s.16 each),
             * w0 bits select tile and max LOD level. */
        {
            int level = (int)((w0 >> 11) & 0x07);
            int tile  = (int)((w0 >> 8) & 0x07);
            float ss  = (float)((w1 >> 16) & 0xffff) / 65536.0f;
            float ts  = (float)(w1 & 0xffff) / 65536.0f;
            gsp_set_texture(gsp, ss, ts, tile, level, gsp->tex_w, gsp->tex_h);
            break;
        }

        case F3DEX2_GEOMETRY:
            /* geometry-mode bits (lighting, shading, culling). The render
             * state these imply is reflected by the gDP commands that follow;
             * no direct RDP command to emit here. */
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
                /* 0x24..0x3f are the RDP non-triangle commands angrylion
                 * implements; 0x31 (G_SETKEY*) is not, so skip it. */
                if (rdp_id >= 0x24 && rdp_id <= 0x3f && rdp_id != 0x31)
                {
                    int32_t two[2];
                    two[0] = (int32_t)w0;
                    two[1] = (int32_t)w1;
                    rdp_fifo_append(fifo, two, 2);
                }
            }
            break;
        }
    }
}
