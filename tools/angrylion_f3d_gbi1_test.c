/* angrylion_f3d_gbi1_test -- the F3D walker follows the older GBI
 * conventions its microcode builds keep.
 *
 * perspNorm: F3DEX 0.95 (Mario Kart 64) keeps the Fast3D G_RDPHALF_1
 * (0xB4) handler that stores perspNorm; the build is recognised by that
 * handler in its dispatch table, and in it 0xB4 sets perspNorm while the
 * G_MW_PERSPNORM moveword does not.
 *
 * gSPCullDisplayList takes its vertex range in the encoding of the
 * walker's microcode.
 *
 * Fast3D passes vstart * 40 in the low half of w0 and (vend + 1) * 40 in
 * w1; F3DEX GBI 1 passes both indices doubled. Three vertices lie wholly
 * outside the left plane and three inside. A cull over the outside three
 * must end the list before the triangle that follows; a cull over the
 * inside three must not. Each list is run with the decode under test.
 *
 * Built by `make check-angrylion` (top-level Makefile). Exit status 0 on pass.
 */
#include "rdp_emit_f3d.h"
#include "rdp_emit_f3dex2.h"
#include "rdp_emit_frontend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RDRAM_SIZE (256u * 1024u)
#define UDATA      0x10000u     /* microcode data segment */
#define MTX_PROJ   0x20000u
#define MTX_MODEL  0x20040u
#define VIEWPORT   0x20080u
#define VERTICES   0x20100u
#define DLIST      0x21000u

static unsigned char rdram[RDRAM_SIZE];

/* The walkers reference the HLE activation glue and libretro-common for
 * mid-list microcode swaps, which a stock Fast3D list never issues; the
 * glue itself drags in the rasterizer, so those two entry points are
 * satisfied here instead. */
int s2dex1_ucode_match(const unsigned char *r, unsigned int size,
                       unsigned int ud, unsigned int ut)
{
    (void)r; (void)size; (void)ud; (void)ut;
    return 0;
}
void gsp_detect_ucode_params(GSPState *st, const unsigned char *r,
                             unsigned int size, unsigned int ud,
                             unsigned int ut)
{
    (void)st; (void)r; (void)size; (void)ud; (void)ut;
}

static void put8(unsigned int addr, unsigned int v)
{
    rdram[addr ^ 3u] = (unsigned char)(v & 0xffu);
}

static void put16(unsigned int addr, int v)
{
    put8(addr, ((unsigned int)v >> 8) & 0xffu);
    put8(addr + 1, (unsigned int)v & 0xffu);
}

static void put32(unsigned int addr, unsigned int v)
{
    put16(addr, (int)(v >> 16));
    put16(addr + 2, (int)(v & 0xffffu));
}

static void put_matrix(unsigned int addr, const int m[4][4])
{
    int i, j;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
        {
            put16(addr + (unsigned int)((i * 4 + j) * 2), m[i][j]);
            put16(addr + 32u + (unsigned int)((i * 4 + j) * 2), 0);
        }
}

static void put_vertex(unsigned int addr, int x, int y, int z,
                       unsigned int rgba)
{
    put16(addr + 0, x);
    put16(addr + 2, y);
    put16(addr + 4, z);
    put16(addr + 6, 0);
    put16(addr + 8, 0);
    put16(addr + 10, 0);
    put8(addr + 12, rgba >> 24);
    put8(addr + 13, rgba >> 16);
    put8(addr + 14, rgba >> 8);
    put8(addr + 15, rgba);
}

/* the six plane rows of a Fast3D data segment: -x, -y, +x, +y, far, near */
static void put_plane_table(unsigned int ud, int near_z)
{
    static const int rows[5][4] = {
        { 1, 0, 0,  1 }, { 0, 1, 0,  1 }, { 1, 0, 0, -1 }, { 0, 1, 0, -1 },
        { 0, 0, 1, -1 }
    };
    int i;
    for (i = 0; i < 5; i++)
    {
        put16(ud + 0x70u + (unsigned int)i * 8u + 0u, rows[i][0]);
        put16(ud + 0x70u + (unsigned int)i * 8u + 2u, rows[i][1]);
        put16(ud + 0x70u + (unsigned int)i * 8u + 4u, rows[i][2]);
        put16(ud + 0x70u + (unsigned int)i * 8u + 6u, rows[i][3]);
    }
    put16(ud + 0x98u, 0);
    put16(ud + 0x9au, 0);
    put16(ud + 0x9cu, near_z);
    put16(ud + 0x9eu, 1);
}

static int tri_words(int32_t w0)
{
    switch (((uint32_t)w0 >> 24) & 0x3fu)
    {
        case 0x0f: return 44;
        case 0x0e: return 40;
        case 0x0d: return 28;
        case 0x0c: return 24;
        case 0x0b: return 28;
        case 0x0a: return 24;
        case 0x09: return 12;
        case 0x08: return 8;
    }
    return 0;
}

static GSPState gsp;
static unsigned char fifo_storage[64 * 1024];
static int failures;

static void check(int got, int want, const char *what);

static int run_half1(int d64, int half1)
{
    RdpFifo fifo;
    f3d_seg_reset();
    f3d_set_rdram(rdram);
    f3d_set_rdram_size(RDRAM_SIZE);
    f3d_set_variant(d64);
    f3d_set_half1_perspnorm(half1);
    gsp_init(&gsp);
    rdp_fifo_init(&fifo, fifo_storage, 0x700000u, sizeof fifo_storage);
    f3d_run_dl(&gsp, &fifo, DLIST, 0, 0);
    return (int)gsp.persp_norm;
}

#define UTEXT 0x30000u

static void test_half1_perspnorm(void)
{
    put32(UTEXT + 4u, 0x201d0110u);           /* Fast3D version word */
    put32(UDATA + 0xdcu, 0x00001100u);        /* 0xB4 handler at IMEM 0x1100 */
    put32(UTEXT + 0x80u, 0x08000400u);        /* j dispatch */
    put32(UTEXT + 0x84u, 0xa7b80002u);        /* sh t8,2(sp) */
    check(f3d_ucode_half1_perspnorm(rdram, RDRAM_SIZE, UDATA, UTEXT), 1,
          "perspNorm: the stored-B4 handler is recognised");
    put32(UTEXT + 0x84u, 0x00000000u);
    check(f3d_ucode_half1_perspnorm(rdram, RDRAM_SIZE, UDATA, UTEXT), 0,
          "perspNorm: another handler is not");

    put32(DLIST + 0x00, 0xb4000000u); put32(DLIST + 0x04, 0x00001234u);
    put32(DLIST + 0x08, 0xbc00000eu); put32(DLIST + 0x0c, 0x00005678u);
    put32(DLIST + 0x10, 0xb8000000u); put32(DLIST + 0x14, 0x00000000u);
    check(run_half1(1, 1), 0x1234, "perspNorm: F3DEX 0.95 takes it from 0xB4");
    check(run_half1(1, 0), 0x5678, "perspNorm: later F3DEX takes it from the moveword");
}

static int run(int d64)
{
    RdpFifo fifo;
    const int32_t *w;
    unsigned int i;
    int tris = 0;

    put_plane_table(UDATA, 1);
    f3d_seg_reset();
    f3d_set_rdram(rdram);
    f3d_set_rdram_size(RDRAM_SIZE);
    f3d_set_variant(d64);
    f3d_set_line_variant(0);
    f3d_set_variant_wr64(0);
    f3d_set_variant_f3dex(0);
    f3d_set_variant_pd(0);
    f3d_set_half1_perspnorm(0);
    f3d_set_near_plane_from_data(rdram, RDRAM_SIZE, UDATA);
    gsp_init(&gsp);
    rdp_fifo_init(&fifo, fifo_storage, 0x700000u, sizeof fifo_storage);
    f3d_run_dl(&gsp, &fifo, DLIST, 0, 0);

    w = (const int32_t *)fifo.storage;
    for (i = 0; i + 1 < fifo.used / 4u; )
    {
        int n = tri_words(w[i]);
        if (n) { tris++; i += (unsigned int)n; }
        else i += 2;
    }
    return tris;
}

/* MTX, MTX, viewport, geometry mode, six vertices, cull, one triangle */
static void build(int d64, unsigned int cull_w0, unsigned int cull_w1, unsigned int tri_w1)
{
    put32(DLIST + 0x00, 0x01030040u); put32(DLIST + 0x04, MTX_PROJ);
    put32(DLIST + 0x08, 0x01020040u); put32(DLIST + 0x0c, MTX_MODEL);
    put32(DLIST + 0x10, 0x03800010u); put32(DLIST + 0x14, VIEWPORT);
    put32(DLIST + 0x18, 0xb7000000u); put32(DLIST + 0x1c, 0x00000205u);
    if (d64)
        put32(DLIST + 0x20, 0x0400185fu);   /* G_VTX 6 at v0, n << 10 */
    else
        put32(DLIST + 0x20, 0x0450005fu);   /* G_VTX 6 at v0 */
    put32(DLIST + 0x24, VERTICES);
    put32(DLIST + 0x28, cull_w0);    put32(DLIST + 0x2c, cull_w1);
    put32(DLIST + 0x30, 0xbf000000u); put32(DLIST + 0x34, tri_w1);
    put32(DLIST + 0x38, 0xb8000000u); put32(DLIST + 0x3c, 0x00000000u);
}

static void check(int got, int want, const char *what)
{
    if (got != want)
    {
        printf("FAIL: %s (%d triangles, expected %d)\n", what, got, want);
        failures++;
    }
}

int main(void)
{
    static const int proj[4][4] = {
        { 1, 0, 0, 0 },
        { 0, 1, 0, 0 },
        { 0, 0, 1, 1 },     /* w = z */
        { 0, 0, -100, 0 }
    };
    static const int ident[4][4] = {
        { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 }
    };

    test_half1_perspnorm();

    put_matrix(MTX_PROJ, proj);
    put_matrix(MTX_MODEL, ident);
    put16(VIEWPORT + 0, 160 * 4); put16(VIEWPORT + 2, 120 * 4);
    put16(VIEWPORT + 4, 511);     put16(VIEWPORT + 6, 0);
    put16(VIEWPORT + 8, 160 * 4); put16(VIEWPORT + 10, 120 * 4);
    put16(VIEWPORT + 12, 511);    put16(VIEWPORT + 14, 0);

    /* Fast3D: v0..v2 outside (x < -w), v3..v5 inside */
    put_vertex(VERTICES + 0,  -900,   5, 200, 0xff8040ffu);
    put_vertex(VERTICES + 16, -950,   8, 200, 0x40ff80ffu);
    put_vertex(VERTICES + 32, -800, -40, 200, 0x8040ffffu);
    put_vertex(VERTICES + 48,   10,   5, 200, 0xff8040ffu);
    put_vertex(VERTICES + 64,  -12,   8, 200, 0x40ff80ffu);
    put_vertex(VERTICES + 80,   20, -40, 200, 0x8040ffffu);

    build(0, 0xbe000000u, 3u * 40u, 0x001e2832u);   /* cull 0..2, draw 3,4,5 */
    check(run(0), 0, "Fast3D: a range wholly outside ends the list");
    build(0, 0xbe000000u | (3u * 40u), 6u * 40u, 0x001e2832u);
    check(run(0), 1, "Fast3D: a visible range does not");

    /* F3DEX GBI 1: v0..v2 inside, v3..v5 outside */
    put_vertex(VERTICES + 0,    10,   5, 200, 0xff8040ffu);
    put_vertex(VERTICES + 16,  -12,   8, 200, 0x40ff80ffu);
    put_vertex(VERTICES + 32,   20, -40, 200, 0x8040ffffu);
    put_vertex(VERTICES + 48,  -900,   5, 200, 0xff8040ffu);
    put_vertex(VERTICES + 64,  -950,   8, 200, 0x40ff80ffu);
    put_vertex(VERTICES + 80,  -800, -40, 200, 0x8040ffffu);

    build(1, 0xbe000000u | 6u, 10u, 0x00000204u);   /* cull 3..5, draw 0,1,2 */
    check(run(1), 0, "F3DEX: a range wholly outside ends the list");
    build(1, 0xbe000000u, 4u, 0x00000204u);
    check(run(1), 1, "F3DEX: a visible range does not");

    if (failures)
    {
        printf("angrylion_f3d_gbi1_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("angrylion_f3d_gbi1_test: all checks passed\n");
    return 0;
}
