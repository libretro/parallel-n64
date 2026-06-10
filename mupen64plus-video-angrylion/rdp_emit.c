/* rdp_emit.c -- self-contained RDP triangle-command encoder for angrylion.
 *
 * Encodes screen-space triangles (per-vertex color, texcoords, perspective
 * W, depth) into the low-level RDP triangle commands angrylion's edgewalker
 * decodes: 0x0c shade, 0x0d shade+Z, 0x0e texshade, 0x0f texshade+Z. The
 * packing is the exact inverse of angrylion's edgewalker_for_prims decode.
 *
 * Integer-only: the RSP/RDP never use floating point. Every coordinate,
 * attribute and slope is an integer fixed-point quantity, computed with
 * 64-bit accumulators and integer division exactly as the RSP triangle setup
 * does. (The previous float implementation was a GLideN64-ism and produced
 * non-bit-exact slopes -- especially Z.)
 *
 * Language: ISO C89 (ANSI C), MSVC-compatible. No C99/C++ constructs:
 * all declarations at the top of each block; no for-loop initial
 * declarations; no // comments. Build check:
 *   gcc -std=c89 -pedantic -Wall -Wdeclaration-after-statement -Werror
 */

#include <string.h>

#include "rdp_emit.h"

/* Signed 64/64 division returning a 32-bit s15.16 result. The plane attribute
 * slopes are (delta_attr_cross) / (signed area); both are s15.16-derived
 * integers. The attribute deltas are s15.16 and the area is s15.16*s15.16>>16
 * scaled below, so the quotient lands back in s15.16. Guards divide-by-zero. */
static int32_t idiv_fix(int64_t num, int64_t den)
{
    if (den == 0)
        return 0;
    /* num is already pre-shifted by the caller (<<16) so the quotient is s15.16 */
    return (int32_t)(num / den);
}

static void pack_attr(int32_t *ew, int base, int hi_half, int32_t val)
{
    uint16_t hi = (uint16_t)((val >> 16) & 0xffff);
    uint16_t lo = (uint16_t)(val & 0xffff);
    if (hi_half)
    {
        ew[base]     = (int32_t)(((uint32_t)ew[base]     & 0x0000ffffu) | ((uint32_t)hi << 16));
        ew[base + 4] = (int32_t)(((uint32_t)ew[base + 4] & 0x0000ffffu) | ((uint32_t)lo << 16));
    }
    else
    {
        ew[base]     = (int32_t)(((uint32_t)ew[base]     & 0xffff0000u) | (uint32_t)hi);
        ew[base + 4] = (int32_t)(((uint32_t)ew[base + 4] & 0xffff0000u) | (uint32_t)lo);
    }
}

static void pack32(int32_t *ew, int base, int part, int32_t v)
{
    if (part == 0)
    {
        ew[base]     = (int32_t)(((uint32_t)ew[base]     & 0x0000ffffu) | ((uint32_t)v & 0xffff0000u));
        ew[base + 4] = (int32_t)(((uint32_t)ew[base + 4] & 0x0000ffffu) | (((uint32_t)v << 16) & 0xffff0000u));
    }
    else
    {
        ew[base]     = (int32_t)(((uint32_t)ew[base]     & 0xffff0000u) | (((uint32_t)v >> 16) & 0xffffu));
        ew[base + 4] = (int32_t)(((uint32_t)ew[base + 4] & 0xffff0000u) | ((uint32_t)v & 0xffffu));
    }
}

/* x/y are s15.16 screen coordinates. The RDP edge X fields are s15.16 too; the
 * Y fields (YH/YM/YL) are 11.2 fixed (sub-pixel /4), so the s15.16 y is shifted
 * down by 14 to land in .2 fixed. The dxXdy edge slopes are s15.16. */

/* Edge-start context shared from emit_edges to the attribute emitters: the
 * top-scanline correction floor(yh) - yh (s15.16, <= 0). The RSP anchors
 * the major/middle edge x and the attribute bases at the INTEGER scanline
 * containing the top vertex, walking them back from the vertex along the
 * edge; the low edge starts at the mid vertex itself. Solved from the cxd4
 * LLE oracle: for every triangle with distinct h/m slopes,
 * (xm - xh) / (dxmdy - dxhdy) lands exactly on floor(yh) - yh, and the
 * low-edge start solves to ym itself. Single-threaded DL walk. */
static int32_t s_dyfix = 0;

/* shared edge + screen setup; fills ew[0..7] and returns sort order + geometry
 * (vh/vm/vl pointers, the s15.16 inv-area in *inv_out scaled, dxhdy in
 * *dxhdy_out) for the attribute solves. */
static void emit_edges(int32_t *ew, const EmitVertex *va, const EmitVertex *vb,
                       const EmitVertex *vc, int tile, int max_level,
                       const EmitVertex **pvh, const EmitVertex **pvm,
                       const EmitVertex **pvl,
                       int64_t *area_out, int32_t *dxhdy_out)
{
    const EmitVertex *v[3];
    const EmitVertex *vh, *vm, *vl, *tmp;
    int i, j, flip;
    int64_t ex1, ey1, ex2, ey2, cross, area;
    int32_t dyh, dym, dyl, dxhdy, dxmdy, dxldy;

    v[0] = va; v[1] = vb; v[2] = vc;
    for (i = 0; i < 2; i++)
        for (j = i + 1; j < 3; j++)
            if (v[j]->y < v[i]->y) { tmp = v[i]; v[i] = v[j]; v[j] = tmp; }
    vh = v[0]; vm = v[1]; vl = v[2];

    /* winding from the signed area of the screen triangle (s15.16 coords) */
    ex1 = (int64_t)vm->x - vh->x; ey1 = (int64_t)vm->y - vh->y;
    ex2 = (int64_t)vl->x - vh->x; ey2 = (int64_t)vl->y - vh->y;
    cross = ex1 * ey2 - ex2 * ey1;          /* s15.16 * s15.16 -> .32 */
    flip = (cross > 0) ? 1 : 0;

    /* edge slopes dx/dy in s15.16: ((vl.x-vh.x)<<16)/(vl.y-vh.y). The
     * vertex coordinates arrive quantized to the RSP's S13.2 storage (see
     * clip_to_emit), so these deltas are already consistent with the .2
     * YH/YM/YL fields the rasterizer walks from. */
    dyh = vl->y - vh->y; dym = vm->y - vh->y; dyl = vl->y - vm->y;
    dxhdy = dyh ? (int32_t)((((int64_t)(vl->x - vh->x)) << 16) / dyh) : 0;
    dxmdy = dym ? (int32_t)((((int64_t)(vm->x - vh->x)) << 16) / dym) : 0;
    dxldy = dyl ? (int32_t)((((int64_t)(vl->x - vm->x)) << 16) / dyl) : 0;

    /* The RSP anchors the h/m edge starts and the attribute bases at the
     * integer scanline floor(yh), walking back from the top vertex along
     * each edge slope (the low edge starts at the mid vertex). With the
     * slopes consistent with the quantized vertices, the walked edges then
     * pass exactly through the true vertices; the previous vertex-anchored
     * starts overshot silhouettes by up to half a pixel on short edges
     * (hair leaking past the hat/face seam: the in-game streak artifact).
     * Oracle-verified: the emitted edge words now match cxd4's bit-for-bit
     * on the dissected triangles. */
    s_dyfix = (int32_t)(((uint32_t)vh->y & ~0xffffu)) - vh->y;

    memset(ew, 0, 24 * sizeof(int32_t));

    /* YH/YM/YL: s15.16 y -> .2 fixed = y >> 14, masked to 14 bits */
    ew[0] = (int32_t)(((uint32_t)(flip ? 1 : 0) << 23)
          | ((uint32_t)(max_level & 7) << 19)
          | ((uint32_t)(tile & 7) << 16)
          | (((uint32_t)(vl->y >> 14)) & 0x3fffu));
    ew[1] = (int32_t)((((uint32_t)(vm->y >> 14) & 0x3fffu) << 16)
          |  ((uint32_t)(vh->y >> 14) & 0x3fffu));

    ew[2] = vm->x;   /* xL: starts at the mid vertex (s15.16 screen x) */
    ew[3] = dxldy;
    ew[4] = vh->x + (int32_t)(((int64_t)dxhdy * s_dyfix) >> 16);  /* xH */
    ew[5] = dxhdy;
    ew[6] = vh->x + (int32_t)(((int64_t)dxmdy * s_dyfix) >> 16);  /* xM */
    ew[7] = dxmdy;

    /* signed area (s15.16*s15.16 >> 16 -> s15.16) used as plane divisor */
    area = ((int64_t)(vm->x - vh->x) * (int64_t)(vl->y - vh->y)
          - (int64_t)(vl->x - vh->x) * (int64_t)(vm->y - vh->y)) >> 16;

    *pvh = vh; *pvm = vm; *pvl = vl;
    *area_out = area;
    *dxhdy_out = dxhdy;
}

/* Solve one attribute plane: given the three sorted-vertex attribute values
 * a0(vh),a1(vm),a2(vl) (s15.16) and the screen geometry, return dx/dy/de.
 * dadx = ((a1-a0)*(Y2-Y0) - (a2-a0)*(Y1-Y0)) / area  (the (<<16)/area gives
 * s15.16); de = dy + (dxhdy * dx >> 16). */
static void solve_plane(int32_t a0, int32_t a1, int32_t a2,
                        int32_t X0, int32_t Y0, int32_t X1, int32_t Y1,
                        int32_t X2, int32_t Y2, int64_t area, int32_t dxhdy,
                        int32_t *dx, int32_t *dy, int32_t *de)
{
    int64_t nx = ((int64_t)(a1 - a0) * (int64_t)((Y2 - Y0) >> 8)
                - (int64_t)(a2 - a0) * (int64_t)((Y1 - Y0) >> 8));
    int64_t ny = ((int64_t)(a2 - a0) * (int64_t)((X1 - X0) >> 8)
                - (int64_t)(a1 - a0) * (int64_t)((X2 - X0) >> 8));
    /* nx,ny are attr(s15.16) * coord(s7.16>>8 = s15.8) = s.24; divide by
     * area(s15.16) then renormalise: result*256 keeps s15.16. */
    int32_t ddx = idiv_fix(nx << 8, area);
    int32_t ddy = idiv_fix(ny << 8, area);
    *dx = ddx;
    *dy = ddy;
    *de = ddy + (int32_t)(((int64_t)dxhdy * ddx) >> 16);
}

int emit_shaded_triangle(int32_t *ew, const EmitVertex *va,
                         const EmitVertex *vb, const EmitVertex *vc,
                         int tile, int max_level)
{
    const EmitVertex *vh, *vm, *vl;
    int64_t area;
    int32_t dxhdy;
    int32_t X0, Y0, X1, Y1, X2, Y2;
    int chan;
    int32_t base_c[4], dx_c[4], dy_c[4], de_c[4];

    emit_edges(ew, va, vb, vc, tile, max_level, &vh, &vm, &vl, &area, &dxhdy);

    X0 = vh->x; Y0 = vh->y; X1 = vm->x; Y1 = vm->y; X2 = vl->x; Y2 = vl->y;

    for (chan = 0; chan < 4; chan++)
    {
        int32_t c0, c1, c2, dcdx, dcdy, dcde;
        switch (chan)
        {
        case 0:  c0 = vh->r; c1 = vm->r; c2 = vl->r; break;
        case 1:  c0 = vh->g; c1 = vm->g; c2 = vl->g; break;
        case 2:  c0 = vh->b; c1 = vm->b; c2 = vl->b; break;
        default: c0 = vh->a; c1 = vm->a; c2 = vl->a; break;
        }
        solve_plane(c0, c1, c2, X0, Y0, X1, Y1, X2, Y2, area, dxhdy,
                    &dcdx, &dcdy, &dcde);
        /* The RSP's triangle write initialises every vertex colour
         * fraction to 0x8000 (vnxor vtx_attrs_*_f, vZero, 0x8000): the
         * shade attribute carries the 8-bit colour plus half an LSB.
         * Adding the half to the base is exact -- all three vertices
         * shift together, so the gradients are unchanged. */
        base_c[chan] = c0 + 0x8000 + (int32_t)(((int64_t)dcde * s_dyfix) >> 16);
        dx_c[chan]   = dcdx;
        dy_c[chan]   = dcdy;
        de_c[chan]   = dcde;
    }

    pack_attr(ew, 8, 1, base_c[0]);
    pack_attr(ew, 8, 0, base_c[1]);
    pack_attr(ew, 9, 1, base_c[2]);
    pack_attr(ew, 9, 0, base_c[3]);
    pack_attr(ew, 10, 1, dx_c[0]);
    pack_attr(ew, 10, 0, dx_c[1]);
    pack_attr(ew, 11, 1, dx_c[2]);
    pack_attr(ew, 11, 0, dx_c[3]);
    pack_attr(ew, 16, 1, de_c[0]);
    pack_attr(ew, 16, 0, de_c[1]);
    pack_attr(ew, 17, 1, de_c[2]);
    pack_attr(ew, 17, 0, de_c[3]);
    pack_attr(ew, 18, 1, dy_c[0]);
    pack_attr(ew, 18, 0, dy_c[1]);
    pack_attr(ew, 19, 1, dy_c[2]);
    pack_attr(ew, 19, 0, dy_c[3]);

    return 24;
}

/* Z block: z (s15.16 screen depth) and dz plane, packed as full 32-bit words at
 * ewdata[base..base+3]. The depth coefficient domain: the rasterizer walks z
 * linearly and per-pixel does sz=(z>>10); the 18-bit stored z = (z_int<<6). So
 * the s15.16 screen z (e->z) feeds the plane directly -- no extra scale. */
static void emit_z_block(int32_t *ew, int base,
                         const EmitVertex *va, const EmitVertex *vb,
                         const EmitVertex *vc)
{
    const EmitVertex *vh = va, *vm = vb, *vl = vc, *tmp;
    int64_t area;
    int32_t X0, Y0, X1, Y1, X2, Y2;
    int32_t dyh2, dxhdy2, zdx, zdy, zde;
    int32_t z0, z1, z2, zb;

    if (vm->y < vh->y) { tmp = vh; vh = vm; vm = tmp; }
    if (vl->y < vh->y) { tmp = vh; vh = vl; vl = tmp; }
    if (vl->y < vm->y) { tmp = vm; vm = vl; vl = tmp; }

    X0 = vh->x; Y0 = vh->y; X1 = vm->x; Y1 = vm->y; X2 = vl->x; Y2 = vl->y;
    area = ((int64_t)(X1 - X0) * (int64_t)(Y2 - Y0)
          - (int64_t)(X2 - X0) * (int64_t)(Y1 - Y0)) >> 16;
    dyh2 = vl->y - vh->y;
    dxhdy2 = dyh2 ? (int32_t)((((int64_t)(vl->x - vh->x)) << 16) / dyh2) : 0;

    /* The depth coefficient domain is the screen z (e->z, in s15.16) scaled to
     * the RDP sub-precision z: coeff_z = screen_z * 32 (G_MAXZ 10-bit screen z
     * -> 15-bit z range). Verified against the cxd4 LLE oracle (z base 31732 ==
     * screen 991.5 * 32). screen_z MUST be clamped to [0, G_MAXZ=1023] first:
     * the N64 z-buffer range is 0..0x3ffff and the hardware clamps z to it;
     * without the clamp, vertices at/behind the far plane (screen_z >= ~1023,
     * very common) make screen_z*32 overflow the int32 coefficient and wrap to a
     * large negative value, randomising the per-vertex depth (the scrambled-N
     * bug). G_MAXZ in s15.16 is 1023<<16; clamp there, then * 32 fits. */
    /* Solve the depth gradients on the true vertex plane and anchor the
     * base at the top vertex, clamped to the z-buffer range. Pre-clamping
     * every vertex to [0, G_MAXZ] before the solve (the previous behaviour)
     * bends the plane whenever a guard-band clip fragment carries a vertex
     * past the range, tilting the depth gradient across the whole visible
     * part; the rasterizer's per-pixel clamp already covers any walked
     * value outside the buffer range. */
    {
        int32_t zcb = vh->z;
        int32_t zmax = 1023 << 16;
        int32_t zlim = (int32_t)1 << 26;
        z0 = vh->z; z1 = vm->z; z2 = vl->z;
        if (zcb < 0) zcb = 0; if (zcb > zmax) zcb = zmax;
        zb = zcb << 5;

        if (z0 > -zlim && z0 < zlim && z1 > -zlim && z1 < zlim &&
            z2 > -zlim && z2 < zlim)
        {
            /* In-range vertices: solve in the x32 coefficient domain
             * directly, keeping the full sub-coefficient gradient
             * precision (bit-for-bit the pre-existing path when z is
             * within the buffer range). */
            solve_plane(z0 << 5, z1 << 5, z2 << 5, X0, Y0, X1, Y1, X2, Y2,
                        area, dxhdy2, &zdx, &zdy, &zde);
        }
        else
        {
            /* A vertex lies far outside the z range (z << 5 would wrap):
             * solve on the raw s15.16 z and scale the gradients
             * afterwards, saturating. */
            int64_t g;
            solve_plane(z0, z1, z2, X0, Y0, X1, Y1, X2, Y2, area, dxhdy2,
                        &zdx, &zdy, &zde);
            g = (int64_t)zdx * 32;
            if (g >  0x7fffffffl) g =  0x7fffffffl;
            if (g < -0x7fffffffl) g = -0x7fffffffl;
            zdx = (int32_t)g;
            g = (int64_t)zdy * 32;
            if (g >  0x7fffffffl) g =  0x7fffffffl;
            if (g < -0x7fffffffl) g = -0x7fffffffl;
            zdy = (int32_t)g;
            g = (int64_t)zde * 32;
            if (g >  0x7fffffffl) g =  0x7fffffffl;
            if (g < -0x7fffffffl) g = -0x7fffffffl;
            zde = (int32_t)g;
        }
    }

    zb += (int32_t)(((int64_t)zde * s_dyfix) >> 16);
    ew[base + 0] = zb;      /* z at the floor(yh) anchor, s15.16 */
    ew[base + 1] = zdx;
    ew[base + 2] = zde;
    ew[base + 3] = zdy;
}

int emit_shaded_z_triangle(int32_t *ew, const EmitVertex *va,
                           const EmitVertex *vb, const EmitVertex *vc,
                           int tile, int max_level)
{
    emit_shaded_triangle(ew, va, vb, vc, tile, max_level);
    emit_z_block(ew, 24, va, vb, vc);
    return 28;
}

/* Per-triangle S/T branch bias, in raw S10.5 units. The wrap fold in the
 * frontend recenters a triangle's texture coordinates so they fit the
 * s15.16 vertex storage; the amount subtracted is restored here, in the
 * 64-bit product domain, so the emitted coefficients carry the hardware's
 * absolute coordinate branch (clamp and mirror phases depend on it). */
static int32_t st_bias_s, st_bias_t;

void emit_set_st_bias(int32_t bias_s, int32_t bias_t)
{
    st_bias_s = bias_s;
    st_bias_t = bias_t;
}

int emit_texshade_triangle(int32_t *ew,
                           const EmitVertex *va, const EmitVertex *vb,
                           const EmitVertex *vc, int tex_w, int tex_h,
                           int tile, int max_level)
{
    const EmitVertex *vh, *vm, *vl;
    int64_t area;
    int32_t dxhdy;
    int32_t X0, Y0, X1, Y1, X2, Y2;
    int32_t s0, t0, w0;
    int32_t sdx, sdy, sde, tdx, tdy, tde, wdx, wdy, wde;
    int32_t sv[3], tv[3], wv[3];
    int i;

    emit_shaded_triangle(ew, va, vb, vc, tile, max_level);

    /* sort vh/vm/vl by y, same as emit_edges */
    {
        const EmitVertex *v[3], *tmp;
        int a, b;
        v[0] = va; v[1] = vb; v[2] = vc;
        for (a = 0; a < 2; a++)
            for (b = a + 1; b < 3; b++)
                if (v[b]->y < v[a]->y) { tmp = v[a]; v[a] = v[b]; v[b] = tmp; }
        vh = v[0]; vm = v[1]; vl = v[2];
    }

    X0 = vh->x; Y0 = vh->y; X1 = vm->x; Y1 = vm->y; X2 = vl->x; Y2 = vl->y;
    area = ((int64_t)(X1 - X0) * (int64_t)(Y2 - Y0)
          - (int64_t)(X2 - X0) * (int64_t)(Y1 - Y0)) >> 16;
    dxhdy = (vl->y - vh->y)
          ? (int32_t)((((int64_t)(vl->x - vh->x)) << 16) / (vl->y - vh->y)) : 0;
    (void)tex_w; (void)tex_h;

    /* The texture S/T coefficient is texel(raw S10.5) * W_coeff / 2^16, where
     * W_coeff is the s1.30 (1/w)*perspNorm inverse-w. The per-pixel tcdiv
     * recovers texel = (S>>16) / (W>>16). The stored vt->s is the raw S10.5
     * texel in s15.16 (raw << 16), so the product needs >> 32 total. Verified
     * against cxd4: S/W == texel_s10.5 / 65536 exactly. */
    sv[0] = (int32_t)(((((int64_t)st_bias_s << 16) + vh->s) * vh->w) >> 32);
    sv[1] = (int32_t)(((((int64_t)st_bias_s << 16) + vm->s) * vm->w) >> 32);
    sv[2] = (int32_t)(((((int64_t)st_bias_s << 16) + vl->s) * vl->w) >> 32);
    tv[0] = (int32_t)(((((int64_t)st_bias_t << 16) + vh->t) * vh->w) >> 32);
    tv[1] = (int32_t)(((((int64_t)st_bias_t << 16) + vm->t) * vm->w) >> 32);
    tv[2] = (int32_t)(((((int64_t)st_bias_t << 16) + vl->t) * vl->w) >> 32);
    wv[0] = vh->w; wv[1] = vm->w; wv[2] = vl->w;

    solve_plane(sv[0], sv[1], sv[2], X0, Y0, X1, Y1, X2, Y2, area, dxhdy,
                &sdx, &sdy, &sde);
    solve_plane(tv[0], tv[1], tv[2], X0, Y0, X1, Y1, X2, Y2, area, dxhdy,
                &tdx, &tdy, &tde);
    solve_plane(wv[0], wv[1], wv[2], X0, Y0, X1, Y1, X2, Y2, area, dxhdy,
                &wdx, &wdy, &wde);
    s0 = sv[0] + (int32_t)(((int64_t)sde * s_dyfix) >> 16);
    t0 = tv[0] + (int32_t)(((int64_t)tde * s_dyfix) >> 16);
    w0 = wv[0] + (int32_t)(((int64_t)wde * s_dyfix) >> 16);

    for (i = 24; i < 40; i++) ew[i] = 0;

    pack32(ew, 24, 0, s0);  pack32(ew, 24, 1, t0);
    pack32(ew, 25, 0, w0);
    pack32(ew, 26, 0, sdx); pack32(ew, 26, 1, tdx);
    pack32(ew, 27, 0, wdx);
    pack32(ew, 32, 0, sde); pack32(ew, 32, 1, tde);
    pack32(ew, 33, 0, wde);
    pack32(ew, 34, 0, sdy); pack32(ew, 34, 1, tdy);
    pack32(ew, 35, 0, wdy);

    return 40;
}

int emit_texshade_z_triangle(int32_t *ew,
                             const EmitVertex *va, const EmitVertex *vb,
                             const EmitVertex *vc, int tex_w, int tex_h,
                             int tile, int max_level)
{
    emit_texshade_triangle(ew, va, vb, vc, tex_w, tex_h, tile, max_level);
    emit_z_block(ew, 40, va, vb, vc);
    return 44;
}
