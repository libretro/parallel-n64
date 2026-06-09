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
#include "rdp_emit_recip.h"

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

/* Matrices are s15.16 fixed point: element = (integer << 16) | fraction,
 * held in an int32. 1.0 is 0x00010000. This matches the RSP's representation
 * (it keeps the integer and fractional s16 halves separately; an s15.16 int32
 * is the concatenation of the two). */
#define FX_ONE 0x00010000

static void mtx_identity(int32_t m[4][4])
{
    int i, j;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            m[i][j] = (i == j) ? FX_ONE : 0;
}

static void mtx_copy(int32_t d[4][4], int32_t s[4][4])
{
    int i, j;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            d[i][j] = s[i][j];
}

/* d = a * b (row-vector convention: v' = v * d, matching the N64 RSP).
 * Each product of two s15.16 values is a 64-bit s31.32 intermediate; the
 * column sum is accumulated at full 64-bit width and then truncated back to
 * s15.16 (>> 16), which is the "middle 32 bits retained" behaviour the RSP's
 * 48-bit vector accumulator produces for the matrix concatenation. */
static void mtx_mul(int32_t d[4][4], int32_t a[4][4], int32_t b[4][4])
{
    int32_t r[4][4];
    int i, j, k;
    for (i = 0; i < 4; i++)
    {
        for (j = 0; j < 4; j++)
        {
            int64_t acc = 0;
            for (k = 0; k < 4; k++)
                acc += (int64_t)a[i][k] * (int64_t)b[k][j];
            r[i][j] = (int32_t)(acc >> 16);
        }
    }
    mtx_copy(d, r);
}

/* Load an N64 fixed-point 4x4 matrix from RDRAM. The RSP stores the integer
 * s16 halves of all 16 elements first (offset +0), then the fractional u16
 * halves (offset +32); an element's s15.16 value is (int << 16) | frac. The
 * matrix is stored transposed for the row-vector transform, with the
 * translation in the last row, so element (i,j) is read from linear slot
 * (i*4 + j). read_s16_be/read_u16_be apply the in-word byte swap. */
static void load_n64_matrix(int32_t m[4][4], const unsigned char *rdram, unsigned int addr)
{
    int i, j;
    unsigned int int_base = addr;
    unsigned int frac_base = addr + 32;
    for (i = 0; i < 4; i++)
    {
        for (j = 0; j < 4; j++)
        {
            int ofs = (i * 4 + j) * 2;
            int ip = read_s16_be(rdram, int_base + (unsigned int)ofs);
            int fp = read_u16_be(rdram, frac_base + (unsigned int)ofs);
            m[i][j] = (int32_t)(((uint32_t)(int16_t)ip << 16) | ((uint32_t)fp & 0xffffu));
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
    s->viewport.vscale_x = 160 << 16;
    s->viewport.vscale_y = 120 << 16;
    s->viewport.vtrans_x = 160 << 16;
    s->viewport.vtrans_y = 120 << 16;
    s->viewport.vscale_z = 511 << 16;
    s->viewport.vtrans_z = 511 << 16;
    s->tex_scale_s = 0x10000u;
    s->tex_scale_t = 0x10000u;
    s->persp_norm = 0xffffu; /* default until gSPPerspNormalize sets it */
    s->viewport.persp_norm = 0xffffu;

    /* default lighting: no directional lights, white ambient (255) so geometry
     * flagged G_LIGHTING before any light load is not pure black. */
    s->num_lights = 0;
    for (i = 0; i < GSP_MAX_LIGHTS; i++)
    {
        s->light_rgb[i][0] = 255;
        s->light_rgb[i][1] = 255;
        s->light_rgb[i][2] = 255;
        s->light_dir[i][0] = 0;
        s->light_dir[i][1] = 0;
        s->light_dir[i][2] = 0;
    }
    s->tex_tile = 0;
    s->tex_level = 0;
    s->tex_w = 32;
    s->tex_h = 32;
}

void gsp_matrix_load(GSPState *s, const unsigned char *rdram, unsigned int addr,
                     int projection, int load, int push)
{
    int32_t m[4][4];
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
    /* N64 Vp: vscale[0..3] then vtrans[0..3] (s16). X/Y are 10.2 fixed (the .2
     * sub-pixel), so the pixel value is raw/4 -> in s15.16 that is raw<<14. Z
     * (vscale[2]/vtrans[2] = G_MAXZ/2 = 511) is a plain integer -> raw<<16. All
     * stored as s15.16, integer-only (no float). */
    s->viewport.vscale_x = (int32_t)read_s16_be(rdram, addr + 0) << 14;
    s->viewport.vscale_y = (int32_t)read_s16_be(rdram, addr + 2) << 14;
    s->viewport.vscale_z = (int32_t)read_s16_be(rdram, addr + 4) << 16;
    s->viewport.vtrans_x = (int32_t)read_s16_be(rdram, addr + 8) << 14;
    s->viewport.vtrans_y = (int32_t)read_s16_be(rdram, addr + 10) << 14;
    s->viewport.vtrans_z = (int32_t)read_s16_be(rdram, addr + 12) << 16;
}

void gsp_set_texture(GSPState *s, unsigned int scale_s, unsigned int scale_t,
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
        int32_t ox, oy, oz;
        int64_t cx, cy, cz, cw;
        int st_s, st_t;
        GSPVertex *vt;
        if (idx < 0 || idx >= GSP_MAX_VERTICES)
            continue;
        vt = &s->vtx[idx];

        /* Model-space position is an integer s16; the combined matrix is
         * s15.16. The product (integer * s15.16) is already an s15.16 value,
         * and the column sum is accumulated at 64-bit width like the RSP's
         * vector accumulator, then read back as s15.16 (>> 16 of the .16
         * product of integer*fixed cancels to leave the s15.16 result). */
        ox = read_s16_be(rdram, base + 0); /* x */
        oy = read_s16_be(rdram, base + 2); /* y */
        oz = read_s16_be(rdram, base + 4); /* z */

        cx = (int64_t)ox * s->combined[0][0] + (int64_t)oy * s->combined[1][0]
           + (int64_t)oz * s->combined[2][0] + (int64_t)s->combined[3][0];
        cy = (int64_t)ox * s->combined[0][1] + (int64_t)oy * s->combined[1][1]
           + (int64_t)oz * s->combined[2][1] + (int64_t)s->combined[3][1];
        cz = (int64_t)ox * s->combined[0][2] + (int64_t)oy * s->combined[1][2]
           + (int64_t)oz * s->combined[2][2] + (int64_t)s->combined[3][2];
        cw = (int64_t)ox * s->combined[0][3] + (int64_t)oy * s->combined[1][3]
           + (int64_t)oz * s->combined[2][3] + (int64_t)s->combined[3][3];

        vt->cx = (int32_t)cx; vt->cy = (int32_t)cy;
        vt->cz = (int32_t)cz; vt->cw = (int32_t)cw;

        st_s = read_s16_be(rdram, base + 8);  /* s */
        st_t = read_s16_be(rdram, base + 10); /* t */
        /* Carry the raw S10.5 texel coordinate (modulated by the G_TEXTURE
         * scale, an S0.16 fraction), NOT a [0,1]-normalized value. The
         * angrylion edgewalker and texel pipeline consume S10.5 texel units
         * directly; normalizing here (the GLideN64/GL-sampler convention)
         * collapsed the sampled coordinate toward 0 and drove the texture
         * combiner black. The encoder scales these into the fixed-point
         * inverse-w envelope angrylion expects. */
        /* Carry the RAW S10.5 texel coordinate into the perspective texture
         * coefficient. The G_TEXTURE S0.16 scale is applied by the RDP tile
         * (SETTILE shift), NOT folded into the edge coefficient: empirically
         * the cxd4 LLE oracle's S coefficient matches raw_st * (1/w) * perspNorm
         * with no extra scale, and the rasterizer's tcshift_cycle applies the
         * tile shift to the recovered texel. */
        vt->s = (int32_t)st_s << 16;
        vt->t = (int32_t)st_t << 16;

        if (s->geometry_mode & 0x00020000u)     /* G_LIGHTING */
        {
            /* [12..14] are a signed s8 normal. Transform by the modelview
             * rotation (s15.16), normalize, and sum directional lights:
             * shade = ambient + S max(0, n.L_i) * rgb_i. All integer. */
            int nxb = (int)(signed char)read_u8_n64(rdram, base + 12);
            int nyb = (int)(signed char)read_u8_n64(rdram, base + 13);
            int nzb = (int)(signed char)read_u8_n64(rdram, base + 14);
            int32_t (*M)[4] = s->modelview[s->modelview_top];
            /* tx,ty,tz: s8 * s15.16 >> 0 accumulated, then >>16 -> s15.16 normal */
            int32_t tx = (int32_t)(((int64_t)nxb * M[0][0]
                       + (int64_t)nyb * M[1][0] + (int64_t)nzb * M[2][0]) >> 16);
            int32_t ty = (int32_t)(((int64_t)nxb * M[0][1]
                       + (int64_t)nyb * M[1][1] + (int64_t)nzb * M[2][1]) >> 16);
            int32_t tz = (int32_t)(((int64_t)nxb * M[0][2]
                       + (int64_t)nyb * M[1][2] + (int64_t)nzb * M[2][2]) >> 16);
            int64_t len2 = (int64_t)tx * tx + (int64_t)ty * ty + (int64_t)tz * tz;
            int32_t cr, cg, cb;
            int li;
            if (len2 > 0)
            {
                /* integer sqrt of len2 (s15.16^2 -> the magnitude in s15.16) */
                int64_t lo = 0, hi = 0x7fffffff, mid, lenfx;
                while (lo < hi)
                {
                    mid = (lo + hi + 1) >> 1;
                    if (mid * mid <= len2) lo = mid; else hi = mid - 1;
                }
                lenfx = lo;                 /* sqrt(len2) in s15.16 */
                if (lenfx != 0)
                {
                    /* normalize to s.15: n/len, scaled by 32768 */
                    tx = (int32_t)(((int64_t)tx << 15) / lenfx);
                    ty = (int32_t)(((int64_t)ty << 15) / lenfx);
                    tz = (int32_t)(((int64_t)tz << 15) / lenfx);
                }
            }
            cr = s->light_rgb[s->num_lights][0];
            cg = s->light_rgb[s->num_lights][1];
            cb = s->light_rgb[s->num_lights][2];
            for (li = 0; li < s->num_lights; li++)
            {
                /* d = n . L, both s.15 -> product >>15 gives s.15 cosine */
                int32_t d = (int32_t)(((int64_t)tx * s->light_dir[li][0]
                          + (int64_t)ty * s->light_dir[li][1]
                          + (int64_t)tz * s->light_dir[li][2]) >> 15);
                if (d > 0)
                {
                    cr += (int32_t)(((int64_t)s->light_rgb[li][0] * d) >> 15);
                    cg += (int32_t)(((int64_t)s->light_rgb[li][1] * d) >> 15);
                    cb += (int32_t)(((int64_t)s->light_rgb[li][2] * d) >> 15);
                }
            }
            if (cr > 255) cr = 255;
            if (cg > 255) cg = 255;
            if (cb > 255) cb = 255;
            vt->r = cr << 16; vt->g = cg << 16; vt->b = cb << 16;
            vt->a = (int32_t)read_u8_n64(rdram, base + 15) << 16;
        }
        else
        {
            vt->r = (int32_t)read_u8_n64(rdram, base + 12) << 16;
            vt->g = (int32_t)read_u8_n64(rdram, base + 13) << 16;
            vt->b = (int32_t)read_u8_n64(rdram, base + 14) << 16;
            vt->a = (int32_t)read_u8_n64(rdram, base + 15) << 16;
        }

        vt->clip = 0;
    }
}

#define GEOM_ZBUFFER    0x00000001u
#define GEOM_CULL_FRONT 0x00000200u
#define GEOM_CULL_BACK  0x00000400u

void gsp_set_persp_norm(GSPState *s, unsigned int pn)
{
    s->persp_norm = pn & 0xffffu;
    s->viewport.persp_norm = pn & 0xffffu;
}

void gsp_set_geometry_mode(GSPState *s, unsigned int mode)
{
    s->geometry_mode = mode;
}

unsigned int gsp_get_geometry_mode(const GSPState *s)
{
    return s->geometry_mode;
}

void gsp_set_num_lights(GSPState *s, int n)
{
    if (n < 0) n = 0;
    if (n > GSP_MAX_LIGHTS - 1) n = GSP_MAX_LIGHTS - 1;
    s->num_lights = n;
}

void gsp_set_light(GSPState *s, const unsigned char *rdram,
                   unsigned int addr, int index)
{
    int dx, dy, dz;
    int64_t len2;
    if (index < 0 || index >= GSP_MAX_LIGHTS)
        return;

    /* Light struct: bytes [0..2] = r,g,b (0..255); the direction is a signed
     * s8 vector at bytes [8..10]. rgb kept as 0..255 ints; direction normalized
     * to s.15 (value/32768). Integer-only. */
    s->light_rgb[index][0] = (int32_t)read_u8_n64(rdram, addr + 0);
    s->light_rgb[index][1] = (int32_t)read_u8_n64(rdram, addr + 1);
    s->light_rgb[index][2] = (int32_t)read_u8_n64(rdram, addr + 2);

    dx = (int)(signed char)read_u8_n64(rdram, addr + 8);
    dy = (int)(signed char)read_u8_n64(rdram, addr + 9);
    dz = (int)(signed char)read_u8_n64(rdram, addr + 10);
    len2 = (int64_t)dx * dx + (int64_t)dy * dy + (int64_t)dz * dz;
    if (len2 > 0)
    {
        int64_t lo = 0, hi = 0x10000, mid, lenfx = 0;
        while (lo < hi)                     /* integer sqrt(len2<<? ) */
        {
            mid = (lo + hi + 1) >> 1;
            if (mid * mid <= len2) lo = mid; else hi = mid - 1;
        }
        lenfx = lo;
        if (lenfx != 0)
        {
            s->light_dir[index][0] = (int32_t)(((int64_t)dx << 15) / lenfx);
            s->light_dir[index][1] = (int32_t)(((int64_t)dy << 15) / lenfx);
            s->light_dir[index][2] = (int32_t)(((int64_t)dz << 15) / lenfx);
        }
        else
        {
            s->light_dir[index][0] = 0;
            s->light_dir[index][1] = 0;
            s->light_dir[index][2] = 0;
        }
    }
    else
    {
        s->light_dir[index][0] = 0;
        s->light_dir[index][1] = 0;
        s->light_dir[index][2] = 0;
    }
}

int gsp_triangle(GSPState *s, int32_t *cmd, int i0, int i1, int i2,
                 int textured, int z_buffered)
{
    BridgeVertex v0, v1, v2;
    const GSPVertex *a, *b, *c;

    if (i0 < 0 || i0 >= GSP_MAX_VERTICES ||
        i1 < 0 || i1 >= GSP_MAX_VERTICES ||
        i2 < 0 || i2 >= GSP_MAX_VERTICES)
        return 0;

    a = &s->vtx[i0]; b = &s->vtx[i1]; c = &s->vtx[i2];

    /* Z bit comes from the G_ZBUFFER geometry-mode bit, not the othermode. */
    z_buffered = (s->geometry_mode & GEOM_ZBUFFER) ? 1 : 0;

    /* Trivial-reject (x/y vs +-w) only when all w > 0 (NoN microcode: behind
     * the near plane is passed through). Clip coords are s15.16. */
    if (a->cw > 0 && b->cw > 0 && c->cw > 0)
    {
        int ca = 0, cb = 0, cc = 0;
        if (a->cx >  a->cw) ca |= 1;
        if (a->cx < -a->cw) ca |= 2;
        if (a->cy >  a->cw) ca |= 4;
        if (a->cy < -a->cw) ca |= 8;
        if (b->cx >  b->cw) cb |= 1;
        if (b->cx < -b->cw) cb |= 2;
        if (b->cy >  b->cw) cb |= 4;
        if (b->cy < -b->cw) cb |= 8;
        if (c->cx >  c->cw) cc |= 1;
        if (c->cx < -c->cw) cc |= 2;
        if (c->cy >  c->cw) cc |= 4;
        if (c->cy < -c->cw) cc |= 8;
        if (ca & cb & cc)
            return 0;
    }

    /* Backface cull and degenerate reject on the screen-space signed area.
     * Project with the RSP fixed reciprocal of w and the viewport x/y scale,
     * then take the signed area; its sign is the winding. Integer-only. */
    {
        int32_t ra = rsp_recip_div(a->cw);
        int32_t rb = rsp_recip_div(b->cw);
        int32_t rc = rsp_recip_div(c->cw);
        int32_t sxa = (int32_t)((((int64_t)a->cx * ra) >> 15) * (int64_t)s->viewport.vscale_x >> 16);
        int32_t sya = -(int32_t)((((int64_t)a->cy * ra) >> 15) * (int64_t)s->viewport.vscale_y >> 16);
        int32_t sxb = (int32_t)((((int64_t)b->cx * rb) >> 15) * (int64_t)s->viewport.vscale_x >> 16);
        int32_t syb = -(int32_t)((((int64_t)b->cy * rb) >> 15) * (int64_t)s->viewport.vscale_y >> 16);
        int32_t sxc = (int32_t)((((int64_t)c->cx * rc) >> 15) * (int64_t)s->viewport.vscale_x >> 16);
        int32_t syc = -(int32_t)((((int64_t)c->cy * rc) >> 15) * (int64_t)s->viewport.vscale_y >> 16);
        int64_t cross = (int64_t)(sxb - sxa) * (syc - sya)
                      - (int64_t)(sxc - sxa) * (syb - sya);
        if (s->geometry_mode & (GEOM_CULL_FRONT | GEOM_CULL_BACK))
        {
            unsigned int cull = s->geometry_mode & (GEOM_CULL_FRONT | GEOM_CULL_BACK);
            if (cull == (GEOM_CULL_FRONT | GEOM_CULL_BACK))
                return 0;
            if (cull == GEOM_CULL_BACK  && cross > 0)
                return 0;
            if (cull == GEOM_CULL_FRONT && cross < 0)
                return 0;
        }
        if (cross == 0)
            return 0;                       /* degenerate */
    }

    v0.cx = a->cx; v0.cy = a->cy; v0.cz = a->cz; v0.cw = a->cw;
    v0.r = a->r; v0.g = a->g; v0.b = a->b; v0.a = a->a; v0.s = a->s; v0.t = a->t;
    v1.cx = b->cx; v1.cy = b->cy; v1.cz = b->cz; v1.cw = b->cw;
    v1.r = b->r; v1.g = b->g; v1.b = b->b; v1.a = b->a; v1.s = b->s; v1.t = b->t;
    v2.cx = c->cx; v2.cy = c->cy; v2.cz = c->cz; v2.cw = c->cw;
    v2.r = c->r; v2.g = c->g; v2.b = c->b; v2.a = c->a; v2.s = c->s; v2.t = c->t;

    return bridge_add_triangle(cmd, &v0, &v1, &v2, &s->viewport,
                               textured, z_buffered,
                               s->tex_tile, s->tex_level, s->tex_w, s->tex_h);
}
