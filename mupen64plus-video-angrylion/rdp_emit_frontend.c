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
    s->tex_scale_s = 0x8000u;   /* G_TEXTURE scale, S0.16 with 0x8000 == 1.0 */
    s->tex_scale_t = 0x8000u;
    s->persp_norm = 0xffffu; /* default until gSPPerspNormalize sets it */
    s->fog_m = 0;
    s->fog_o = 0;
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
        if (i < 2)
        {
            s->lookat[i][0] = 0;
            s->lookat[i][1] = 0;
            s->lookat[i][2] = 0;
        }
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

void gsp_set_lookat(GSPState *s, const unsigned char *rdram,
                    unsigned int addr, int index)
{
    int dx, dy, dz;
    int64_t len2;
    if (index < 0 || index > 1)
        return;
    dx = (int)(signed char)read_u8_n64(rdram, addr + 8);
    dy = (int)(signed char)read_u8_n64(rdram, addr + 9);
    dz = (int)(signed char)read_u8_n64(rdram, addr + 10);
    len2 = (int64_t)dx * dx + (int64_t)dy * dy + (int64_t)dz * dz;
    s->lookat[index][0] = 0;
    s->lookat[index][1] = 0;
    s->lookat[index][2] = 0;
    if (len2 > 0)
    {
        int64_t lo = 0, hi = 0x10000, mid;
        while (lo < hi)
        {
            mid = (lo + hi + 1) >> 1;
            if (mid * mid <= len2) lo = mid; else hi = mid - 1;
        }
        if (lo != 0)
        {
            s->lookat[index][0] = (int32_t)(((int64_t)dx << 15) / lo);
            s->lookat[index][1] = (int32_t)(((int64_t)dy << 15) / lo);
            s->lookat[index][2] = (int32_t)(((int64_t)dz << 15) / lo);
        }
    }
}

/* acos(x)/pi * 1024 for x = i/128, i in [0,128]; the G_TEXTURE_GEN_LINEAR
 * texture coordinate is acos(-n.L)/pi in S10.5 raw units (0..1024). */
static const short acos_pi_1024[129] = {
    512, 509, 507, 504, 502, 499, 497, 494, 492, 489, 487, 484,
    481, 479, 476, 474, 471, 469, 466, 463, 461, 458, 456, 453,
    451, 448, 445, 443, 440, 438, 435, 432, 430, 427, 424, 422,
    419, 416, 414, 411, 408, 406, 403, 400, 398, 395, 392, 389,
    387, 384, 381, 378, 376, 373, 370, 367, 364, 362, 359, 356,
    353, 350, 347, 344, 341, 338, 335, 332, 329, 326, 323, 320,
    317, 314, 311, 308, 305, 302, 298, 295, 292, 289, 285, 282,
    279, 275, 272, 268, 265, 261, 258, 254, 251, 247, 243, 239,
    236, 232, 228, 224, 220, 216, 211, 207, 203, 198, 194, 189,
    185, 180, 175, 170, 165, 159, 154, 148, 142, 136, 130, 123,
    116, 108, 100, 91, 82, 71, 58, 41, 0
};

/* Texture-coordinate generation (G_TEXTURE_GEN): map the cosine d = n . L
 * (s.15) to a raw S10.5 coordinate in [0, 32768] (0..1024 texels):
 *   plain:  raw = (d + 1) * 16384      == (d + 32768) >> 1
 *   linear: raw = acos(-d) / pi * 32768 (table + linear interpolation)
 * The result feeds the same G_TEXTURE-scale path as a vertex-supplied
 * coordinate. Calibrated against the cxd4 LLE oracle on the N64 boot logo
 * (G_TEXTURE_GEN_LINEAR, scale 0x0ED8, lookat Y zero): the constant
 * generated T at the d == 0 midpoint, 16384 * 0x0ED8 * 2 / 65536 == 1900
 * raw S10.5, matches the oracle's T coefficient exactly. */
static int texgen_coord(int32_t d, int linear)
{
    if (!linear)
        return (int)((d + 32768) >> 1);
    else
    {
        int neg = (d < 0);
        int32_t a = neg ? -d : d;
        int idx  = (int)(a >> 8);
        int frac = (int)(a & 0xff);
        int v0, v1, v;
        if (idx > 128) idx = 128;
        v0 = acos_pi_1024[idx];
        v1 = acos_pi_1024[idx < 128 ? idx + 1 : 128];
        v  = v0 + (((v1 - v0) * frac) >> 8);
        return (neg ? v : 1024 - v) << 5;
    }
}

void gsp_set_fog(GSPState *s, int fm, int fo)
{
    s->fog_m = fm;
    s->fog_o = fo;
}

void gsp_task_reset(GSPState *s)
{
    /* The RSP task boot resets the matrix-stack pointer; display lists are
     * free to leave pushes unbalanced. Without this per-task reset the HLE
     * stack ratchets up to its cap over a few frames and every pushed
     * (skeleton/nested) draw transforms with a stale top-of-stack matrix. */
    s->modelview_top = 0;
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
        /* The RSP texel coordinate is (raw S10.5 st * G_TEXTURE scale) >> 15:
         * the S0.16 scale field reads 0x8000 as 1.0 (so gSPTexture(0xFFFF)
         * roughly doubles). Calibrated against the cxd4 LLE oracle on the
         * pause menu: draws with scale 0x8000 match raw st exactly, draws
         * with scale 0xFFFF carry exactly twice the raw value; folding the
         * scale at >> 16 (or not at all) halves the sampled texel for the
         * 0xFFFF case. Result kept in s15.16:
         * (st << 16) * scale >> 15 == (st * scale) << 1. */
        /* (assigned below; G_TEXTURE_GEN overrides st_s/st_t first) */

        if (s->geometry_mode & 0x00020000u)     /* G_LIGHTING */
        {
            /* [12..14] are a signed s8 normal. Transform by the modelview
             * rotation (s15.16), normalize, and sum directional lights:
             * shade = ambient + S max(0, n.L_i) * rgb_i. All integer. */
            int nxb = (int)(signed char)read_u8_n64(rdram, base + 12);
            int nyb = (int)(signed char)read_u8_n64(rdram, base + 13);
            int nzb = (int)(signed char)read_u8_n64(rdram, base + 14);
            int32_t (*M)[4] = s->modelview[s->modelview_top];
            /* Accumulate the rotated normal at full s8 * s15.16 precision
             * (up to ~2^40) and normalize the 64-bit vector directly. The
             * previous >>16 before normalization quantized the components
             * to a handful of integer steps whenever the modelview carried
             * a small scale -- OoT actors draw their limbs at ~0.01 matrix
             * scale (entries ~655), so s8 * 655 >> 16 left only -2..2 and
             * every normal collapsed onto a few axis directions, flattening
             * the per-vertex shading (Link's hair and face rendered an
             * unshaded constant grey). An adaptive pre-shift keeps the
             * squared length inside int64 for any matrix scale. */
            int64_t ax = (int64_t)nxb * M[0][0]
                       + (int64_t)nyb * M[1][0] + (int64_t)nzb * M[2][0];
            int64_t ay = (int64_t)nxb * M[0][1]
                       + (int64_t)nyb * M[1][1] + (int64_t)nzb * M[2][1];
            int64_t az = (int64_t)nxb * M[0][2]
                       + (int64_t)nyb * M[1][2] + (int64_t)nzb * M[2][2];
            int32_t tx = 0, ty = 0, tz = 0;
            int64_t mx, len2;
            int32_t cr, cg, cb;
            int li;
            mx = ax < 0 ? -ax : ax;
            if ((ay < 0 ? -ay : ay) > mx) mx = ay < 0 ? -ay : ay;
            if ((az < 0 ? -az : az) > mx) mx = az < 0 ? -az : az;
            while (mx >= ((int64_t)1 << 30))
            {
                ax >>= 1; ay >>= 1; az >>= 1; mx >>= 1;
            }
            len2 = ax * ax + ay * ay + az * az;
            if (len2 > 0)
            {
                /* integer sqrt; result fits 31 bits since len2 < 3 * 2^60 */
                int64_t lo = 0, hi = 0x7fffffff, mid, lenfx;
                while (lo < hi)
                {
                    mid = (lo + hi + 1) >> 1;
                    if (mid * mid <= len2) lo = mid; else hi = mid - 1;
                }
                lenfx = lo;
                if (lenfx != 0)
                {
                    /* normalize to s.15: n/len, scaled by 32768 */
                    tx = (int32_t)((ax << 15) / lenfx);
                    ty = (int32_t)((ay << 15) / lenfx);
                    tz = (int32_t)((az << 15) / lenfx);
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

            if (s->geometry_mode & 0x00040000u) /* G_TEXTURE_GEN */
            {
                /* Generate texture coordinates from the transformed,
                 * normalized normal dotted with the lookat X/Y vectors
                 * (MOVEMEM light slots 0/1). The generated S10.5 raw values
                 * replace the vertex-supplied ones and take the same
                 * G_TEXTURE-scale path below. The boot-logo N is the OoT
                 * test case (TEXTURE_GEN_LINEAR, scale 0x0ED8). */
                int linear = (s->geometry_mode & 0x00080000u) ? 1 : 0;
                int32_t d0 = (int32_t)(((int64_t)tx * s->lookat[0][0]
                            + (int64_t)ty * s->lookat[0][1]
                            + (int64_t)tz * s->lookat[0][2]) >> 15);
                int32_t d1 = (int32_t)(((int64_t)tx * s->lookat[1][0]
                            + (int64_t)ty * s->lookat[1][1]
                            + (int64_t)tz * s->lookat[1][2]) >> 15);
                st_s = texgen_coord(d0, linear);
                st_t = texgen_coord(d1, linear);
            }
        }
        else
        {
            vt->r = (int32_t)read_u8_n64(rdram, base + 12) << 16;
            vt->g = (int32_t)read_u8_n64(rdram, base + 13) << 16;
            vt->b = (int32_t)read_u8_n64(rdram, base + 14) << 16;
            vt->a = (int32_t)read_u8_n64(rdram, base + 15) << 16;
        }

        vt->s = (int32_t)(((int64_t)st_s * (int64_t)s->tex_scale_s) << 1);
        vt->t = (int32_t)(((int64_t)st_t * (int64_t)s->tex_scale_t) << 1);

        if (s->geometry_mode & 0x00010000u)     /* G_FOG */
        {
            /* With fog enabled the RSP replaces the shade alpha with the fog
             * factor: alpha = clamp(ndc_z * fog_m + fog_o, 0, 255), where
             * fog_m/fog_o come from the G_MW_FOG moveword (gSPFogPosition).
             * Verified against the cxd4 LLE oracle: indoors every actor
             * triangle carries shade alpha 0 (no fog at any visible depth);
             * passing the vertex alpha (255) through instead makes the
             * G_RM_FOG_SHADE_A blender output pure fog colour, which is what
             * rendered every lit actor as a black silhouette. ndc_z is the
             * exact s15.16 z/w; for w <= 0 the vertex is headed for the
             * clipper and the factor is irrelevant, so 0 is used. */
            int32_t fa = 0;
            if (vt->cw > 0)
            {
                int32_t ndcz = (int32_t)(((int64_t)vt->cz << 16) / vt->cw);
                fa = (int32_t)((((int64_t)ndcz * s->fog_m) >> 16) + s->fog_o);
                if (fa < 0)   fa = 0;
                if (fa > 255) fa = 255;
            }
            vt->a = fa << 16;
        }

        /* Store the RSP's per-vertex clip outcode (the VCH sign-aware
         * screen-plane compare, bits N/P per x, y, z axis -- the SCRN half
         * of VTX_CLIP). G_CULLDL and the triangle trivial reject test the
         * AND of these flags, and unlike a plain w > 0 frustum test the
         * VCH rule also flags vertices behind the eye, which is what lets
         * the RSP reject the near geometry slivers a guard-band clipper
         * would otherwise rasterize. */
        {
            int32_t comps[3];
            int ax;
            unsigned int fl = 0;
            comps[0] = vt->cx; comps[1] = vt->cy; comps[2] = vt->cz;
            for (ax = 0; ax < 3; ax++)
            {
                int sn = ((comps[ax] ^ vt->cw) < 0);
                int nb = sn ? (comps[ax] <= -vt->cw) : (vt->cw < 0);
                int pb = sn ? (vt->cw < 0) : (comps[ax] >= vt->cw);
                if (nb) fl |= 1u << (ax * 2);
                if (pb) fl |= 2u << (ax * 2);
            }
            vt->clip = (int)fl;
        }
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
    /* The RSP does not normalize the light direction: the s8 vector is used
     * as a fraction of 128, so its magnitude scales the contribution. OoT
     * relies on this -- Lights_BindPoint encodes its CPU-side point lights
     * with the distance attenuation in the color and a direction of
     * magnitude 120 (not 127), and other binders pass scene-authored
     * vectors of arbitrary magnitude straight through. Normalizing here
     * over-brightened any light authored below full magnitude (Link under
     * the house's bound point light gained a flat +40-ish cast).
     * s8/128 in s.15 is simply dir << 8. */
    (void)len2;
    s->light_dir[index][0] = (int32_t)(dx << 8);
    s->light_dir[index][1] = (int32_t)(dy << 8);
    s->light_dir[index][2] = (int32_t)(dz << 8);
}

/* Guard-band plane distance for clip-space vertex v (s15.16) against plane p:
 * p0: 2w - x >= 0   (x <=  2w)
 * p1: 2w + x >= 0   (x >= -2w)
 * p2: 2w - y >= 0   (y <=  2w)
 * p3: 2w + y >= 0   (y >= -2w)
 * The clip ratio of 2 matches the RSP guard band measured from the cxd4 LLE
 * stream (clipped skybox vertices land exactly on screen = vtrans +- 2*vscale).
 * The four side planes jointly bound w > 0, so Sutherland-Hodgman against
 * them alone also disposes of behind-the-eye (w <= 0) vertices, which is the
 * NoN ("no near clip") microcode behaviour. Evaluated in 64-bit: |2w| + |x|
 * can exceed 31 bits. */
static int64_t clip_plane_dist(const GSPVertex *v, int p)
{
    int64_t w2 = (int64_t)v->cw * 2;
    switch (p)
    {
        case 0:  return w2 - v->cx;
        case 1:  return w2 + v->cx;
        case 2:  return w2 - v->cy;
        default: return w2 + v->cy;
    }
}

/* Lerp all components of a clip-space vertex: out = v0 + (v1 - v0) * t where
 * t = d0 / (d0 - d1) is formed as a 0.30 fixed fraction. d0 >= 0 > d1, so
 * 0 <= t <= 1 and den = d0 - d1 > 0. The >> 4 pre-shift keeps d << 30 inside
 * 64 bits (the distances are at most a few times 2^31). */
static void clip_lerp_vertex(GSPVertex *out, const GSPVertex *v0,
                             const GSPVertex *v1, int64_t d0, int64_t d1)
{
    int64_t num = d0 >> 4;
    int64_t den = (d0 - d1) >> 4;
    int64_t t30;
    if (den <= 0)
    {
        *out = *v0;
        return;
    }
    t30 = (num << 30) / den;
    if (t30 < 0)               t30 = 0;
    if (t30 > ((int64_t)1 << 30)) t30 = (int64_t)1 << 30;
    out->cx = v0->cx + (int32_t)(((int64_t)(v1->cx - v0->cx) * t30) >> 30);
    out->cy = v0->cy + (int32_t)(((int64_t)(v1->cy - v0->cy) * t30) >> 30);
    out->cz = v0->cz + (int32_t)(((int64_t)(v1->cz - v0->cz) * t30) >> 30);
    out->cw = v0->cw + (int32_t)(((int64_t)(v1->cw - v0->cw) * t30) >> 30);
    out->r  = v0->r  + (int32_t)(((int64_t)(v1->r  - v0->r ) * t30) >> 30);
    out->g  = v0->g  + (int32_t)(((int64_t)(v1->g  - v0->g ) * t30) >> 30);
    out->b  = v0->b  + (int32_t)(((int64_t)(v1->b  - v0->b ) * t30) >> 30);
    out->a  = v0->a  + (int32_t)(((int64_t)(v1->a  - v0->a ) * t30) >> 30);
    out->s  = v0->s  + (int32_t)(((int64_t)(v1->s  - v0->s ) * t30) >> 30);
    out->t  = v0->t  + (int32_t)(((int64_t)(v1->t  - v0->t ) * t30) >> 30);
    out->clip = 0;
}

/* Sutherland-Hodgman polygon clip against the four guard-band planes.
 * in/out arrays hold up to GSP_CLIP_MAX vertices; returns the vertex count
 * (0..GSP_CLIP_MAX). A triangle gains at most one vertex per plane. */
#define GSP_CLIP_MAX 8

static int clip_polygon_guard(GSPVertex *poly, int n)
{
    GSPVertex tmp[GSP_CLIP_MAX];
    int p;
    for (p = 0; p < 4 && n > 0; p++)
    {
        int i, m = 0;
        for (i = 0; i < n; i++)
        {
            const GSPVertex *cur = &poly[i];
            const GSPVertex *nxt = &poly[(i + 1) % n];
            int64_t dc = clip_plane_dist(cur, p);
            int64_t dn = clip_plane_dist(nxt, p);
            if (dc >= 0)
            {
                if (m < GSP_CLIP_MAX)
                    tmp[m++] = *cur;
                if (dn < 0 && m < GSP_CLIP_MAX)
                {
                    clip_lerp_vertex(&tmp[m], cur, nxt, dc, dn);
                    m++;
                }
            }
            else if (dn >= 0)
            {
                if (m < GSP_CLIP_MAX)
                {
                    clip_lerp_vertex(&tmp[m], nxt, cur, dn, dc);
                    m++;
                }
            }
        }
        for (i = 0; i < m; i++)
            poly[i] = tmp[i];
        n = m;
    }
    return n;
}

int gsp_triangle(GSPState *s, int32_t *cmd, int i0, int i1, int i2,
                 int textured, int z_buffered)
{
    GSPVertex poly[GSP_CLIP_MAX];
    int np;
    const GSPVertex *a, *b, *c;
    unsigned int oa, ob, oc;
    int k;

    if (i0 < 0 || i0 >= GSP_MAX_VERTICES ||
        i1 < 0 || i1 >= GSP_MAX_VERTICES ||
        i2 < 0 || i2 >= GSP_MAX_VERTICES)
        return 0;

    a = &s->vtx[i0]; b = &s->vtx[i1]; c = &s->vtx[i2];

    /* Z bit comes from the G_ZBUFFER geometry-mode bit, not the othermode. */
    z_buffered = (s->geometry_mode & GEOM_ZBUFFER) ? 1 : 0;

    /* Trivial reject: the RSP tests the AND of the three vertices' stored
     * VCH clip outcodes against CLIP_ALL_SCRN -- if every vertex lies
     * outside the same screen plane (including the z lanes, and including
     * the behind-the-eye encodings the VCH compare produces for w <= 0),
     * the triangle is dropped before any clipping. The guard-band clipper
     * below only sees triangles that survive this. */
    if (a->clip & b->clip & c->clip)
        return 0;

    /* Guard-band outcodes (ratio-2 planes, w <= 0 handled implicitly). */
    oa = ob = oc = 0;
    for (k = 0; k < 4; k++)
    {
        if (clip_plane_dist(a, k) < 0) oa |= (1u << k);
        if (clip_plane_dist(b, k) < 0) ob |= (1u << k);
        if (clip_plane_dist(c, k) < 0) oc |= (1u << k);
    }
    if (oa & ob & oc)
        return 0;                           /* all out the same guard plane */

    poly[0] = *a;
    poly[1] = *b;
    poly[2] = *c;
    np = 3;
    if (oa | ob | oc)
    {
        /* Polygon clip against the guard band the way the RSP microcode
         * does: triangles that straddle w <= 0 cannot be passed through to
         * the rasterizer (the 1/w attribute planes of such a triangle are
         * not the planes of its visible portion, so scissoring is not
         * equivalent), and steep overhang past the guard band loses
         * coefficient precision. */
        np = clip_polygon_guard(poly, np);
        if (np < 3)
            return 0;
    }

    /* Backface cull and degenerate reject on the screen-space signed area of
     * the first (clipped) triangle; clipping preserves winding and leaves
     * every vertex with w > 0, so the fixed reciprocal is valid. */
    {
        int32_t ra = rsp_recip_div(poly[0].cw);
        int32_t rb = rsp_recip_div(poly[1].cw);
        int32_t rc = rsp_recip_div(poly[2].cw);
        int32_t sxa = (int32_t)((((int64_t)poly[0].cx * ra) >> 15) * (int64_t)s->viewport.vscale_x >> 16);
        int32_t sya = -(int32_t)((((int64_t)poly[0].cy * ra) >> 15) * (int64_t)s->viewport.vscale_y >> 16);
        int32_t sxb = (int32_t)((((int64_t)poly[1].cx * rb) >> 15) * (int64_t)s->viewport.vscale_x >> 16);
        int32_t syb = -(int32_t)((((int64_t)poly[1].cy * rb) >> 15) * (int64_t)s->viewport.vscale_y >> 16);
        int32_t sxc = (int32_t)((((int64_t)poly[2].cx * rc) >> 15) * (int64_t)s->viewport.vscale_x >> 16);
        int32_t syc = -(int32_t)((((int64_t)poly[2].cy * rc) >> 15) * (int64_t)s->viewport.vscale_y >> 16);
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

    /* Fan-triangulate the (possibly clipped) polygon and emit. */
    {
        int total = 0;
        int i;
        for (i = 1; i + 1 < np; i++)
        {
            BridgeVertex v0, v1, v2;
            int nc;
            v0.cx = poly[0].cx; v0.cy = poly[0].cy; v0.cz = poly[0].cz; v0.cw = poly[0].cw;
            v0.r = poly[0].r; v0.g = poly[0].g; v0.b = poly[0].b; v0.a = poly[0].a;
            v0.s = poly[0].s; v0.t = poly[0].t;
            v1.cx = poly[i].cx; v1.cy = poly[i].cy; v1.cz = poly[i].cz; v1.cw = poly[i].cw;
            v1.r = poly[i].r; v1.g = poly[i].g; v1.b = poly[i].b; v1.a = poly[i].a;
            v1.s = poly[i].s; v1.t = poly[i].t;
            v2.cx = poly[i + 1].cx; v2.cy = poly[i + 1].cy; v2.cz = poly[i + 1].cz; v2.cw = poly[i + 1].cw;
            v2.r = poly[i + 1].r; v2.g = poly[i + 1].g; v2.b = poly[i + 1].b; v2.a = poly[i + 1].a;
            v2.s = poly[i + 1].s; v2.t = poly[i + 1].t;
            nc = bridge_add_triangle(cmd + total, &v0, &v1, &v2, &s->viewport,
                                     textured, z_buffered,
                                     s->tex_tile, s->tex_level, s->tex_w, s->tex_h);
            if (nc > 0)
                total += nc;
        }
        return total;
    }
}
