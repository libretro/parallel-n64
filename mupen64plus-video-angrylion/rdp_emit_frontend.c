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
#include <math.h>

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
    s->viewport.vscale_x = 160.0f;
    s->viewport.vscale_y = -120.0f;
    s->viewport.vtrans_x = 160.0f;
    s->viewport.vtrans_y = 120.0f;
    s->viewport.vscale_z = 511.0f;
    s->viewport.vtrans_z = 511.0f;
    s->tex_scale_s = 1.0f;
    s->tex_scale_t = 1.0f;

    /* default lighting: no directional lights, white ambient so geometry
     * flagged G_LIGHTING before any light load is not pure black. */
    s->num_lights = 0;
    for (i = 0; i < GSP_MAX_LIGHTS; i++)
    {
        s->light_rgb[i][0] = 1.0f;
        s->light_rgb[i][1] = 1.0f;
        s->light_rgb[i][2] = 1.0f;
        s->light_dir[i][0] = 0.0f;
        s->light_dir[i][1] = 0.0f;
        s->light_dir[i][2] = 0.0f;
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
    /* N64 Vp: vscale[0..3] then vtrans[0..3], s16 in 2.10/2 fixed; the X/Y
     * scale and translate are the .2 fixed fields (divide by 4). */
    float sx = (float)read_s16_be(rdram, addr + 0) / 4.0f;
    float sy = (float)read_s16_be(rdram, addr + 2) / 4.0f;
    float sz = (float)read_s16_be(rdram, addr + 4);
    float tx = (float)read_s16_be(rdram, addr + 8) / 4.0f;
    float ty = (float)read_s16_be(rdram, addr + 10) / 4.0f;
    float tz = (float)read_s16_be(rdram, addr + 12);
    s->viewport.vscale_x = sx;
    s->viewport.vscale_y = sy;
    s->viewport.vscale_z = sz;
    s->viewport.vtrans_x = tx;
    s->viewport.vtrans_y = ty;
    s->viewport.vtrans_z = tz;
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
        ox = read_s16_be(rdram, base + 2); /* x */
        oy = read_s16_be(rdram, base + 0); /* y */
        oz = read_s16_be(rdram, base + 6); /* z */

        cx = (int64_t)ox * s->combined[0][0] + (int64_t)oy * s->combined[1][0]
           + (int64_t)oz * s->combined[2][0] + (int64_t)s->combined[3][0];
        cy = (int64_t)ox * s->combined[0][1] + (int64_t)oy * s->combined[1][1]
           + (int64_t)oz * s->combined[2][1] + (int64_t)s->combined[3][1];
        cz = (int64_t)ox * s->combined[0][2] + (int64_t)oy * s->combined[1][2]
           + (int64_t)oz * s->combined[2][2] + (int64_t)s->combined[3][2];
        cw = (int64_t)ox * s->combined[0][3] + (int64_t)oy * s->combined[1][3]
           + (int64_t)oz * s->combined[2][3] + (int64_t)s->combined[3][3];

        /* clip coords as s15.16 -> float (the perspective divide is still
         * float this step; the fixed-point reciprocal is a later step). The
         * [3][*] translation term is s15.16 with no model-coord factor, so it
         * is already at the right scale; the ox*M terms are integer*s15.16 =
         * s15.16 too, so the whole sum is s15.16 and divides by 65536. */
        vt->x = (float)cx / 65536.0f;
        vt->y = (float)cy / 65536.0f;
        vt->z = (float)cz / 65536.0f;
        vt->w = (float)cw / 65536.0f;

        st_s = read_s16_be(rdram, base + 10); /* s */
        st_t = read_s16_be(rdram, base + 8);  /* t */
        /* Carry the raw S10.5 texel coordinate (modulated by the G_TEXTURE
         * scale, an S0.16 fraction), NOT a [0,1]-normalized value. The
         * angrylion edgewalker and texel pipeline consume S10.5 texel units
         * directly; normalizing here (the GLideN64/GL-sampler convention)
         * collapsed the sampled coordinate toward 0 and drove the texture
         * combiner black. The encoder scales these into the fixed-point
         * inverse-w envelope angrylion expects. */
        vt->s = (float)st_s * s->tex_scale_s;
        vt->t = (float)st_t * s->tex_scale_t;

        if (s->geometry_mode & 0x00020000u)     /* G_LIGHTING */
        {
            /* [12..14] are a signed normal, not a color. Transform by the
             * modelview rotation, normalize, and sum the directional lights:
             * shade = ambient + S max(0, n.L_i) * rgb_i. */
            int nxb = (int)(signed char)read_u8_n64(rdram, base + 12);
            int nyb = (int)(signed char)read_u8_n64(rdram, base + 13);
            int nzb = (int)(signed char)read_u8_n64(rdram, base + 14);
            float nx = (float)nxb, ny = (float)nyb, nz = (float)nzb;
            float tx = nx * (float)s->modelview[s->modelview_top][0][0] / 65536.0f
                     + ny * (float)s->modelview[s->modelview_top][1][0] / 65536.0f
                     + nz * (float)s->modelview[s->modelview_top][2][0] / 65536.0f;
            float ty = nx * (float)s->modelview[s->modelview_top][0][1] / 65536.0f
                     + ny * (float)s->modelview[s->modelview_top][1][1] / 65536.0f
                     + nz * (float)s->modelview[s->modelview_top][2][1] / 65536.0f;
            float tz = nx * (float)s->modelview[s->modelview_top][0][2] / 65536.0f
                     + ny * (float)s->modelview[s->modelview_top][1][2] / 65536.0f
                     + nz * (float)s->modelview[s->modelview_top][2][2] / 65536.0f;
            float len = tx * tx + ty * ty + tz * tz;
            float cr, cg, cb;
            int li;
            if (len > 0.0f)
            {
                len = (float)sqrt((double)len);
                tx /= len; ty /= len; tz /= len;
            }
            cr = s->light_rgb[s->num_lights][0];
            cg = s->light_rgb[s->num_lights][1];
            cb = s->light_rgb[s->num_lights][2];
            for (li = 0; li < s->num_lights; li++)
            {
                float d = tx * s->light_dir[li][0]
                        + ty * s->light_dir[li][1]
                        + tz * s->light_dir[li][2];
                if (d > 0.0f)
                {
                    cr += s->light_rgb[li][0] * d;
                    cg += s->light_rgb[li][1] * d;
                    cb += s->light_rgb[li][2] * d;
                }
            }
            if (cr > 1.0f) cr = 1.0f;
            if (cg > 1.0f) cg = 1.0f;
            if (cb > 1.0f) cb = 1.0f;
            vt->r = cr; vt->g = cg; vt->b = cb;
            vt->a = (float)read_u8_n64(rdram, base + 15) / 255.0f;
        }
        else
        {
            vt->r = (float)read_u8_n64(rdram, base + 12) / 255.0f;
            vt->g = (float)read_u8_n64(rdram, base + 13) / 255.0f;
            vt->b = (float)read_u8_n64(rdram, base + 14) / 255.0f;
            vt->a = (float)read_u8_n64(rdram, base + 15) / 255.0f;
        }

        vt->clip = 0;
    }
}

#define GEOM_ZBUFFER    0x00000001u
#define GEOM_CULL_FRONT 0x00000200u
#define GEOM_CULL_BACK  0x00000400u

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
    float len;
    if (index < 0 || index >= GSP_MAX_LIGHTS)
        return;

    /* Light struct: bytes [0..2] = r,g,b (0..255); the direction is a signed
     * s8 vector at bytes [8..10]. (Ambient lights carry only color.) */
    s->light_rgb[index][0] = (float)read_u8_n64(rdram, addr + 0) / 255.0f;
    s->light_rgb[index][1] = (float)read_u8_n64(rdram, addr + 1) / 255.0f;
    s->light_rgb[index][2] = (float)read_u8_n64(rdram, addr + 2) / 255.0f;

    dx = (int)(signed char)read_u8_n64(rdram, addr + 8);
    dy = (int)(signed char)read_u8_n64(rdram, addr + 9);
    dz = (int)(signed char)read_u8_n64(rdram, addr + 10);
    len = (float)(dx * dx + dy * dy + dz * dz);
    if (len > 0.0f)
    {
        len = (float)sqrt((double)len);
        s->light_dir[index][0] = (float)dx / len;
        s->light_dir[index][1] = (float)dy / len;
        s->light_dir[index][2] = (float)dz / len;
    }
    else
    {
        s->light_dir[index][0] = 0.0f;
        s->light_dir[index][1] = 0.0f;
        s->light_dir[index][2] = 0.0f;
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

    /* The RDP triangle command's Z bit (0x0d/0x0f vs 0x0c/0x0e) is set by the
     * RSP from the G_ZBUFFER geometry-mode bit, NOT from the RDP othermode
     * render-mode Z_CMP/Z_UPD bits. Those render-mode bits toggle per blender
     * pass (AA, decals, translucency) and do not change which command id the
     * RSP emits; deriving Z-buffering from them mis-declassifies depth-tested
     * triangles to the non-Z command. Read the geometry-mode bit directly. */
    z_buffered = (s->geometry_mode & GEOM_ZBUFFER) ? 1 : 0;

    /* Trivial-reject clipping, matching the RSP/GLideN64 model: compute a clip
     * outcode per vertex against the homogeneous clip volume (x,y in [-w,+w])
     * and the near plane (w < a small epsilon), then drop the whole triangle if
     * all three vertices lie outside the SAME plane. This removes triangles
     * fully behind the camera (w <= 0, whose perspective divide would otherwise
     * explode into garbage screen coordinates) and triangles fully off one
     * screen edge -- both of which the real microcode discards. Partly-visible
     * triangles are kept (angrylion's rasterizer scissors them). */
    {
        /* OoT/MM run the F3DZEX2.NoN microcode ("No Near clipping"): geometry
         * at or behind the near plane is neither clipped nor culled, it is
         * passed straight to the RDP. So there is no near-plane reject here.
         * The screen-edge (x/y) trivial-reject is only meaningful when a
         * vertex is in front of the camera (w > 0); comparing x against a
         * negative w is nonsense, so a triangle with any non-positive w is
         * never trivially rejected and is handed to the rasterizer, which
         * scissors it to the screen -- matching NoN behavior. */
        if (a->w > 0.0f && b->w > 0.0f && c->w > 0.0f)
        {
            int ca = 0, cb = 0, cc = 0;
            if (a->x >  a->w) ca |= 1;
            if (a->x < -a->w) ca |= 2;
            if (a->y >  a->w) ca |= 4;
            if (a->y < -a->w) ca |= 8;
            if (b->x >  b->w) cb |= 1;
            if (b->x < -b->w) cb |= 2;
            if (b->y >  b->w) cb |= 4;
            if (b->y < -b->w) cb |= 8;
            if (c->x >  c->w) cc |= 1;
            if (c->x < -c->w) cc |= 2;
            if (c->y >  c->w) cc |= 4;
            if (c->y < -c->w) cc |= 8;
            if (ca & cb & cc)
                return 0;
        }
    }

    /* Backface culling, applied to the screen-space triangle exactly as the
     * RSP does it (the RDP performs no culling of its own). The stored vertex
     * x/y are clip-space, so project them -- perspective divide then viewport
     * scale -- before taking the signed area; the sign of that area is the
     * triangle's winding. Under this projection a back-facing triangle has a
     * positive area. The RDP receives only the surviving triangles. */
    if (s->geometry_mode & (GEOM_CULL_FRONT | GEOM_CULL_BACK))
    {
        unsigned int cull = s->geometry_mode & (GEOM_CULL_FRONT | GEOM_CULL_BACK);
        float wa = (a->w != 0.0f) ? 1.0f / a->w : 0.0f;
        float wb = (b->w != 0.0f) ? 1.0f / b->w : 0.0f;
        float wc = (c->w != 0.0f) ? 1.0f / c->w : 0.0f;
        float sxa = a->x * wa * s->viewport.vscale_x;
        float sya = a->y * wa * s->viewport.vscale_y;
        float sxb = b->x * wb * s->viewport.vscale_x;
        float syb = b->y * wb * s->viewport.vscale_y;
        float sxc = c->x * wc * s->viewport.vscale_x;
        float syc = c->y * wc * s->viewport.vscale_y;
        float cross = (sxb - sxa) * (syc - sya) - (sxc - sxa) * (syb - sya);
        if (cull == (GEOM_CULL_FRONT | GEOM_CULL_BACK))
            return 0;                       /* cull both: nothing drawn */
        if (cull == GEOM_CULL_BACK  && cross > 0.0f)
            return 0;                       /* back-facing rejected */
        if (cull == GEOM_CULL_FRONT && cross < 0.0f)
            return 0;                       /* front-facing rejected */
    }

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
