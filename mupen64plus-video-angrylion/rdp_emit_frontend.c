/* rdp_emit_frontend.c -- self-contained geometry frontend for the angrylion
 * HLE path. Reimplements the N64 matrix stack and vertex transform in C89,
 * reading matrices/vertices from RDRAM and producing clip-space vertices for
 * rdp_emit_bridge. The algorithm mirrors the documented N64 RSP geometry
 * stage (and GLideN64's gSP.cpp), but uses no external plugin code.
 *
 * Inert until a microcode dispatch calls it. Build check:
 *   gcc -std=c89 -pedantic -Wall -Wdeclaration-after-statement -Werror
 */

#include "rdp_emit_frontend.h"

/* read a signed 16-bit big-endian halfword from RDRAM (N64 byte order) */
/* RDRAM is stored as host-native 32-bit words in this core, so a sub-word
 * read must undo the in-word byte order: on a little-endian host the N64
 * (big-endian) byte at offset a lives at (a ^ 2) for 16-bit and (a ^ 3) for
 * 8-bit, exactly as the RSP's u16()/u8() accessors do via S16/S8. */
#ifdef MSB_FIRST
#define RDRAM_S16 0u
#define RDRAM_S8  0u
#else
#define RDRAM_S16 2u
#define RDRAM_S8  3u
#endif

static int read_u8_n64(const unsigned char *rdram, unsigned int addr)
{
    return (int)rdram[addr ^ RDRAM_S8];
}

static int read_s16_be(const unsigned char *rdram, unsigned int addr)
{
    const unsigned short *p = (const unsigned short *)(rdram + (addr ^ RDRAM_S16));
    int v = (int)*p;
    if (v & 0x8000) v -= 0x10000;
    return v;
}

static int read_u16_be(const unsigned char *rdram, unsigned int addr)
{
    const unsigned short *p = (const unsigned short *)(rdram + (addr ^ RDRAM_S16));
    return (int)*p;
}

static void mtx_identity(float m[4][4])
{
    int i, j;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            m[i][j] = (i == j) ? 1.0f : 0.0f;
}

static void mtx_copy(float d[4][4], float s[4][4])
{
    int i, j;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            d[i][j] = s[i][j];
}

/* d = a * b (row-vector convention: v' = v * d, matching the N64 RSP) */
static void mtx_mul(float d[4][4], float a[4][4], float b[4][4])
{
    float r[4][4];
    int i, j, k;
    for (i = 0; i < 4; i++)
    {
        for (j = 0; j < 4; j++)
        {
            float s = 0.0f;
            for (k = 0; k < 4; k++)
                s += a[i][k] * b[k][j];
            r[i][j] = s;
        }
    }
    mtx_copy(d, r);
}

/* load an N64 fixed-point 4x4 matrix from RDRAM: layout is
 * s16 integer[4][4] then u16 fraction[4][4]; element = int + frac/65536,
 * with column index xor 1 for the N64 word swap. */
static void load_n64_matrix(float m[4][4], const unsigned char *rdram, unsigned int addr)
{
    int i, j;
    unsigned int int_base = addr;
    unsigned int frac_base = addr + 32;
    for (i = 0; i < 4; i++)
    {
        for (j = 0; j < 4; j++)
        {
            int ofs;
            int ip, fp;
            /* read_s16_be/read_u16_be already apply the in-word byte-swap,
             * so element (i,j) is at its natural offset; no column XOR. */
            ofs = (i * 4 + j) * 2;
            ip = read_s16_be(rdram, int_base + (unsigned int)ofs);
            fp = read_u16_be(rdram, frac_base + (unsigned int)ofs);
            m[i][j] = (float)ip + (float)fp / 65536.0f;
        }
    }
}

void gsp_init(GSPState *s)
{
    int i;
    mtx_identity(s->projection);
    for (i = 0; i < GSP_MTX_STACK; i++)
        mtx_identity(s->modelview[i]);
    s->modelview_top = 0;
    mtx_identity(s->combined);
    s->combined_valid = 0;
    s->viewport.vscale_x = 160.0f;
    s->viewport.vscale_y = -120.0f;
    s->viewport.vtrans_x = 160.0f;
    s->viewport.vtrans_y = 120.0f;
    s->tex_scale_s = 1.0f;
    s->tex_scale_t = 1.0f;
    s->tex_tile = 0;
    s->tex_level = 0;
    s->tex_w = 32;
    s->tex_h = 32;
}

void gsp_matrix_load(GSPState *s, const unsigned char *rdram, unsigned int addr,
                     int projection, int load, int push)
{
    float m[4][4];
    load_n64_matrix(m, rdram, addr);

    if (projection)
    {
        if (load)
            mtx_copy(s->projection, m);
        else
            mtx_mul(s->projection, m, s->projection);
    }
    else
    {
        if (push && s->modelview_top < GSP_MTX_STACK - 1)
        {
            mtx_copy(s->modelview[s->modelview_top + 1], s->modelview[s->modelview_top]);
            s->modelview_top++;
        }
        if (load)
            mtx_copy(s->modelview[s->modelview_top], m);
        else
            mtx_mul(s->modelview[s->modelview_top], m, s->modelview[s->modelview_top]);
    }
    s->combined_valid = 0;
}

void gsp_matrix_pop(GSPState *s)
{
    if (s->modelview_top > 0)
        s->modelview_top--;
    s->combined_valid = 0;
}

void gsp_combine_matrices(GSPState *s)
{
    mtx_mul(s->combined, s->modelview[s->modelview_top], s->projection);
    s->combined_valid = 1;
}

void gsp_set_viewport(GSPState *s, const unsigned char *rdram, unsigned int addr)
{
    /* N64 Vp: vscale[0..3] then vtrans[0..3], s16 in 2.10/2 fixed; the X/Y
     * scale and translate are the .2 fixed fields (divide by 4). */
    float sx = (float)read_s16_be(rdram, addr + 0) / 4.0f;
    float sy = (float)read_s16_be(rdram, addr + 2) / 4.0f;
    float tx = (float)read_s16_be(rdram, addr + 8) / 4.0f;
    float ty = (float)read_s16_be(rdram, addr + 10) / 4.0f;
    s->viewport.vscale_x = sx;
    s->viewport.vscale_y = sy;
    s->viewport.vtrans_x = tx;
    s->viewport.vtrans_y = ty;
}

void gsp_set_texture(GSPState *s, float scale_s, float scale_t,
                     int tile, int level, int tex_w, int tex_h)
{
    s->tex_scale_s = scale_s;
    s->tex_scale_t = scale_t;
    s->tex_tile = tile;
    s->tex_level = level;
    s->tex_w = tex_w;
    s->tex_h = tex_h;
}

/* N64 Vertex struct in RDRAM (16 bytes), big-endian:
 *   s16 y, x;  u16 flag; s16 z;  s16 t, s;  color/normal[4] */
void gsp_vertex(GSPState *s, const unsigned char *rdram, unsigned int addr,
                int n, int v0)
{
    int i;
    if (!s->combined_valid)
        gsp_combine_matrices(s);

    for (i = 0; i < n; i++)
    {
        unsigned int base = addr + (unsigned int)i * 16;
        int idx = v0 + i;
        float ox, oy, oz;
        int st_s, st_t;
        GSPVertex *vt;
        if (idx < 0 || idx >= GSP_MAX_VERTICES)
            continue;
        vt = &s->vtx[idx];

        ox = (float)read_s16_be(rdram, base + 2); /* x */
        oy = (float)read_s16_be(rdram, base + 0); /* y */
        oz = (float)read_s16_be(rdram, base + 6); /* z */

        vt->x = ox * s->combined[0][0] + oy * s->combined[1][0]
              + oz * s->combined[2][0] + s->combined[3][0];
        vt->y = ox * s->combined[0][1] + oy * s->combined[1][1]
              + oz * s->combined[2][1] + s->combined[3][1];
        vt->z = ox * s->combined[0][2] + oy * s->combined[1][2]
              + oz * s->combined[2][2] + s->combined[3][2];
        vt->w = ox * s->combined[0][3] + oy * s->combined[1][3]
              + oz * s->combined[2][3] + s->combined[3][3];

        st_s = read_s16_be(rdram, base + 10); /* s */
        st_t = read_s16_be(rdram, base + 8);  /* t */
        /* N64 texcoords are s10.5 scaled by the tile/scale; normalize to 0..1
         * over the texture size for the encoder (which re-scales). */
        vt->s = ((float)st_s / 32.0f) * s->tex_scale_s / (float)s->tex_w;
        vt->t = ((float)st_t / 32.0f) * s->tex_scale_t / (float)s->tex_h;

        /* vertex color (RGBA bytes at offset 12) -> 0..1 */
        vt->r = (float)read_u8_n64(rdram, base + 12) / 255.0f;
        vt->g = (float)read_u8_n64(rdram, base + 13) / 255.0f;
        vt->b = (float)read_u8_n64(rdram, base + 14) / 255.0f;
        vt->a = (float)read_u8_n64(rdram, base + 15) / 255.0f;

        vt->clip = 0;
    }
}

int gsp_triangle(GSPState *s, int32_t *cmd, int i0, int i1, int i2,
                 int textured, int z_buffered)
{
    float v0[10], v1[10], v2[10];
    const GSPVertex *a, *b, *c;

    if (i0 < 0 || i0 >= GSP_MAX_VERTICES ||
        i1 < 0 || i1 >= GSP_MAX_VERTICES ||
        i2 < 0 || i2 >= GSP_MAX_VERTICES)
        return 0;

    a = &s->vtx[i0]; b = &s->vtx[i1]; c = &s->vtx[i2];

    v0[0]=a->x; v0[1]=a->y; v0[2]=a->z; v0[3]=a->w;
    v0[4]=a->r; v0[5]=a->g; v0[6]=a->b; v0[7]=a->a; v0[8]=a->s; v0[9]=a->t;
    v1[0]=b->x; v1[1]=b->y; v1[2]=b->z; v1[3]=b->w;
    v1[4]=b->r; v1[5]=b->g; v1[6]=b->b; v1[7]=b->a; v1[8]=b->s; v1[9]=b->t;
    v2[0]=c->x; v2[1]=c->y; v2[2]=c->z; v2[3]=c->w;
    v2[4]=c->r; v2[5]=c->g; v2[6]=c->b; v2[7]=c->a; v2[8]=c->s; v2[9]=c->t;

    return bridge_add_triangle(cmd, v0, v1, v2, &s->viewport,
                               textured, z_buffered,
                               s->tex_tile, s->tex_level, s->tex_w, s->tex_h);
}
