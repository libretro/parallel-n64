/* angrylion_clip_test -- the HLE geometry frontend's guard-band clipper
 * must produce the same RDP triangle words whatever the clipper's scratch
 * vertices held before the call.
 *
 * A textured, z-buffered triangle with one vertex behind the eye is
 * emitted twice through gsp_triangle(): once after the stack below the
 * caller was filled with 0x5a, once after it was zeroed. The two command
 * streams must match word for word, and every emitted triangle must carry
 * the transformed-vertex colour bias (shade fractions 0x8000) and the
 * perspective-normalised texture block that the standard triangle writer
 * produces, never the 2D overlay writer's raw form.
 *
 * Built by `make tools` from the tree's own emitter objects (the target is
 * in the top-level Makefile). Exit status 0 on pass.
 */
#include "rdp_emit_frontend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RDRAM_SIZE (64u * 1024u)
#define MTX_PROJ   0x1000u
#define MTX_MODEL  0x1040u
#define VIEWPORT   0x1080u
#define VERTICES   0x1100u

static unsigned char rdram[RDRAM_SIZE];

/* logical big-endian bytes into the host-native byteswapped RDRAM image */
static void put8(unsigned int addr, unsigned int v)
{
    rdram[addr ^ 3u] = (unsigned char)(v & 0xffu);
}

static void put16(unsigned int addr, int v)
{
    put8(addr, ((unsigned int)v >> 8) & 0xffu);
    put8(addr + 1, (unsigned int)v & 0xffu);
}

/* N64 fixed-point 4x4: 16 s16 integer parts, then 16 u16 fractions */
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

/* N64 Vtx: s16 x, y, z; u16 flag; s16 s, t; u8 r, g, b, a */
static void put_vertex(unsigned int addr, int x, int y, int z,
                       int s, int t, unsigned int rgba)
{
    put16(addr + 0, x);
    put16(addr + 2, y);
    put16(addr + 4, z);
    put16(addr + 6, 0);
    put16(addr + 8, s);
    put16(addr + 10, t);
    put8(addr + 12, rgba >> 24);
    put8(addr + 13, rgba >> 16);
    put8(addr + 14, rgba >> 8);
    put8(addr + 15, rgba);
}

/* Write a pattern over the stack region the frontend's frames will occupy.
 * The stores are volatile so the fill is not optimised away; the array is
 * larger than every frame between here and the triangle writer. */
static void stack_fill(unsigned char pattern)
{
    volatile unsigned char buf[32768];
    unsigned int i;
    for (i = 0; i < sizeof buf; i++)
        buf[i] = pattern;
}

static GSPState gsp;

static int emit_once(int32_t *cmd, unsigned char pattern)
{
    stack_fill(pattern);
    return gsp_triangle(&gsp, cmd, 0, 1, 2, 1, 1);
}

/* One triangle's word count by opcode: 44 for the textured, shaded,
 * z-buffered form the test asks for. */
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

int main(void)
{
    static int32_t cmd_a[GSP_TRI_CMD_WORDS];
    static int32_t cmd_b[GSP_TRI_CMD_WORDS];
    static const int proj[4][4] = {
        { 1, 0, 0, 0 },
        { 0, 1, 0, 0 },
        { 0, 0, 0, 1 },     /* w = z: the eye plane is z = 0 */
        { 0, 0, 0, 0 }
    };
    static const int ident[4][4] = {
        { 1, 0, 0, 0 },
        { 0, 1, 0, 0 },
        { 0, 0, 1, 0 },
        { 0, 0, 0, 1 }
    };
    int na, nb, i, bad = 0, tris = 0;

    put_matrix(MTX_PROJ, proj);
    put_matrix(MTX_MODEL, ident);
    /* Vp: vscale x, y (10.2), z, pad; vtrans x, y (10.2), z, pad */
    put16(VIEWPORT + 0, 160 * 4);
    put16(VIEWPORT + 2, 120 * 4);
    put16(VIEWPORT + 4, 511);
    put16(VIEWPORT + 6, 0);
    put16(VIEWPORT + 8, 160 * 4);
    put16(VIEWPORT + 10, 120 * 4);
    put16(VIEWPORT + 12, 511);
    put16(VIEWPORT + 14, 0);
    /* two vertices in front of the eye, one behind it */
    put_vertex(VERTICES + 0,   40,  30, 100,  0,    0,    0xff8040ffu);
    put_vertex(VERTICES + 16, -60,  20, 120,  1024, 0,    0x40ff80ffu);
    put_vertex(VERTICES + 32,  10, -50, -40,  0,    1024, 0x8040ffffu);

    gsp_init(&gsp);
    gsp_matrix_load(&gsp, rdram, MTX_PROJ, 1, 1, 0);
    gsp_matrix_load(&gsp, rdram, MTX_MODEL, 0, 1, 0);
    gsp_set_viewport(&gsp, rdram, VIEWPORT);
    gsp_set_texture(&gsp, 0xffffu, 0xffffu, 0, 0, 32, 32);
    gsp_set_geometry_mode(&gsp, 0x00200005u);   /* zbuffer, shade, smooth */
    gsp_vertex(&gsp, rdram, VERTICES, 3, 0);

    if (!((unsigned int)gsp.vtx[2].clip & (unsigned int)GSP_CLIP_NW))
    {
        printf("FAIL: vertex 2 is not behind the eye (clip %08x)\n",
               (unsigned int)gsp.vtx[2].clip);
        return 1;
    }

    na = emit_once(cmd_a, 0x5a);
    nb = emit_once(cmd_b, 0x00);

    if (na <= 0 || na != nb)
    {
        printf("FAIL: %d words after a 0x5a fill, %d after a zero fill\n",
               na, nb);
        return 1;
    }
    if (memcmp(cmd_a, cmd_b, (size_t)na * sizeof cmd_a[0]) != 0)
    {
        for (i = 0; i < na; i++)
            if (cmd_a[i] != cmd_b[i])
                printf("  word %d: %08x vs %08x\n", i,
                       (unsigned int)cmd_a[i], (unsigned int)cmd_b[i]);
        printf("FAIL: the clipped triangle depends on the scratch contents\n");
        return 1;
    }

    /* every triangle: 0x0f opcode, colour bias fractions, a non-zero
     * perspective-normalised W lane */
    for (i = 0; i < na; )
    {
        int n = tri_words(cmd_a[i]);
        if (n != 44)
        {
            printf("FAIL: word %d is not a TRISTZ header (%08x)\n", i,
                   (unsigned int)cmd_a[i]);
            return 1;
        }
        if ((uint32_t)cmd_a[i + 12] != 0x80008000u
            || (uint32_t)cmd_a[i + 13] != 0x80008000u)
        {
            printf("FAIL: triangle %d shade fractions %08x %08x\n", tris,
                   (unsigned int)cmd_a[i + 12], (unsigned int)cmd_a[i + 13]);
            bad = 1;
        }
        if (((uint32_t)cmd_a[i + 25] & 0xffff0000u) == 0u)
        {
            printf("FAIL: triangle %d has a zero W lane\n", tris);
            bad = 1;
        }
        tris++;
        i += n;
    }
    if (bad)
        return 1;
    printf("PASS: %d clipped triangles, %d words, stable across scratch fills\n",
           tris, na);
    return 0;
}
