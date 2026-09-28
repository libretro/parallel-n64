/* angrylion_f3d_near_test -- the Fast3D walker takes its near clip plane
 * from the microcode data segment.
 *
 * The plane table at data + 0x70 ends in the near row: {0,0,1,1} clips
 * at z + w = 0, {0,0,0,1} (a NoN build) only at w = 0. A triangle whose
 * vertices all lie in front of the eye (w > 0) but partly between the eye
 * and the z + w = 0 plane must reach the rasterizer whole under the NoN
 * row -- one triangle spanning the three stored screen positions -- and
 * must be subdivided under the z + w row. Both display lists are stock
 * Fast3D: G_MTX, G_MOVEMEM viewport, G_SETGEOMETRYMODE, G_VTX, G_TRI1.
 *
 * Built by `make tools` from the tree's own emitter objects (the target
 * is in the top-level Makefile). Exit status 0 on pass.
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

/* run the display list under one near row; returns the triangle count and
 * copies the first triangle's words */
static int run(int near_z, int32_t *first, int *first_words)
{
    RdpFifo fifo;
    const int32_t *w;
    unsigned int i;
    int tris = 0;

    put_plane_table(UDATA, near_z);
    f3d_seg_reset();
    f3d_set_rdram(rdram);
    f3d_set_rdram_size(RDRAM_SIZE);
    f3d_set_variant(0);
    f3d_set_line_variant(0);
    f3d_set_variant_wr64(0);
    f3d_set_variant_f3dex(0);
    f3d_set_variant_pd(0);
    f3d_set_near_plane_from_data(rdram, RDRAM_SIZE, UDATA);
    gsp_init(&gsp);
    rdp_fifo_init(&fifo, fifo_storage, 0x700000u, sizeof fifo_storage);
    f3d_run_dl(&gsp, &fifo, DLIST, 0, 0);

    w = (const int32_t *)fifo.storage;
    *first_words = 0;
    for (i = 0; i + 1 < fifo.used / 4u; )
    {
        int n = tri_words(w[i]);
        if (n)
        {
            if (tris == 0)
            {
                memcpy(first, w + i, (size_t)n * sizeof w[0]);
                *first_words = n;
            }
            tris++;
            i += (unsigned int)n;
        }
        else
            i += 2;
    }
    return tris;
}

int main(void)
{
    static const int proj[4][4] = {
        { 1, 0, 0, 0 },
        { 0, 1, 0, 0 },
        { 0, 0, 1, 1 },     /* z' = z - 100, w = z */
        { 0, 0, -100, 0 }
    };
    static const int ident[4][4] = {
        { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 }
    };
    static int32_t tri_non[GSP_TRI_CMD_WORDS], tri_z[GSP_TRI_CMD_WORDS];
    int n_non, n_z, w_non, w_z;
    int ys[3], yh, yl, k;

    put_matrix(MTX_PROJ, proj);
    put_matrix(MTX_MODEL, ident);
    put16(VIEWPORT + 0, 160 * 4); put16(VIEWPORT + 2, 120 * 4);
    put16(VIEWPORT + 4, 511);     put16(VIEWPORT + 6, 0);
    put16(VIEWPORT + 8, 160 * 4); put16(VIEWPORT + 10, 120 * 4);
    put16(VIEWPORT + 12, 511);    put16(VIEWPORT + 14, 0);
    /* z' + w = 2z - 100: negative for the two vertices at z = 30, positive
     * at z = 200; w is positive for all three */
    put_vertex(VERTICES + 0,   10,   5,  30, 0xff8040ffu);
    put_vertex(VERTICES + 16, -12,   8,  30, 0x40ff80ffu);
    put_vertex(VERTICES + 32,  20, -40, 200, 0x8040ffffu);

    /* the display list, stock Fast3D encodings */
    put32(DLIST + 0x00, 0x01030040u); put32(DLIST + 0x04, MTX_PROJ);   /* G_MTX proj|load */
    put32(DLIST + 0x08, 0x01020040u); put32(DLIST + 0x0c, MTX_MODEL);  /* G_MTX load */
    put32(DLIST + 0x10, 0x03800010u); put32(DLIST + 0x14, VIEWPORT);   /* G_MOVEMEM viewport */
    put32(DLIST + 0x18, 0xb7000000u); put32(DLIST + 0x1c, 0x00000205u); /* zbuffer|shade|smooth */
    put32(DLIST + 0x20, 0x0420002fu); put32(DLIST + 0x24, VERTICES);   /* G_VTX 3 at v0 */
    put32(DLIST + 0x28, 0xbf000000u); put32(DLIST + 0x2c, 0x00000a14u); /* G_TRI1 0,1,2 (x10) */
    put32(DLIST + 0x30, 0xb8000000u); put32(DLIST + 0x34, 0x00000000u); /* G_ENDDL */

    n_non = run(0, tri_non, &w_non);
    for (k = 0; k < 3; k++)
        ys[k] = gsp.vtx[k].scr_y >> 14;   /* 10.2 */
    yh = ys[0]; yl = ys[0];
    for (k = 1; k < 3; k++)
    {
        if (ys[k] < yh) yh = ys[k];
        if (ys[k] > yl) yl = ys[k];
    }
    n_z = run(1, tri_z, &w_z);

    if (n_non != 1)
    {
        printf("FAIL: NoN near row: %d triangles emitted, expected the one whole triangle\n", n_non);
        return 1;
    }
    if (w_non != 28)
    {
        printf("FAIL: NoN near row: first command is not a shaded z triangle (%d words)\n", w_non);
        return 1;
    }
    if (((int)(tri_non[0] & 0x3fff) != yl) || ((int)(tri_non[1] & 0x3fff) != yh))
    {
        printf("FAIL: NoN near row: triangle spans y %d..%d, vertices span %d..%d (10.2)\n",
               (int)(tri_non[1] & 0x3fff), (int)(tri_non[0] & 0x3fff), yh, yl);
        return 1;
    }
    if (n_z == 1 && w_z == w_non && memcmp(tri_z, tri_non, (size_t)w_non * sizeof tri_non[0]) == 0)
    {
        printf("FAIL: z + w near row: the triangle was not clipped\n");
        return 1;
    }
    printf("PASS: NoN row keeps the triangle whole (y %d.%02d..%d.%02d), z + w row clips it\n",
           yh >> 2, (yh & 3) * 25, yl >> 2, (yl & 3) * 25);
    return 0;
}
