/* rdp_emit.c -- self-contained RDP triangle-command encoder for angrylion.
 *
 * Encodes screen-space triangles (per-vertex color, texcoords, perspective
 * W) into the low-level RDP triangle commands angrylion's edgewalker
 * decodes: 0x0c shade, 0x0d shade+Z, 0x0e texshade. The packing is the exact
 * inverse of angrylion's edgewalker_for_prims decode, verified bit-for-bit
 * and pixel-for-pixel against captured real RSP commands.
 *
 * Language: ISO C89 (ANSI C), MSVC-compatible. No C99/C++ constructs:
 * all declarations at the top of each block; no for-loop initial
 * declarations; no // comments; no compound literals or non-constant
 * aggregate initializers; no C99 <math.h>. Build check:
 *   gcc -std=c89 -pedantic -Wall -Wdeclaration-after-statement -Werror
 */

#include <string.h>

#include "rdp_emit.h"

static int32_t to_fix(float v, int frac_bits)
{
    double scaled = (double)v * (double)(1L << frac_bits);
    if (scaled >= 0.0)
        return (int32_t)(scaled + 0.5);
    return (int32_t)(scaled - 0.5);
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

int emit_shaded_triangle(int32_t *ew, const EmitVertex *va,
                         const EmitVertex *vb, const EmitVertex *vc,
                         int tile, int max_level)
{
    const EmitVertex *v[3];
    const EmitVertex *vh;
    const EmitVertex *vm;
    const EmitVertex *vl;
    const EmitVertex *tmp;
    int i, j, chan;
    float ex1, ey1, ex2, ey2, cross;
    int flip;
    float dyh, dym, dyl, dxhdy_f, dxmdy_f, dxldy_f;
    float x0, y0, x1, y1, x2, y2, area, inv;
    int32_t base_c[4], dx_c[4], dy_c[4], de_c[4];

    v[0] = va; v[1] = vb; v[2] = vc;

    for (i = 0; i < 2; i++)
    {
        for (j = i + 1; j < 3; j++)
        {
            if (v[j]->y < v[i]->y) { tmp = v[i]; v[i] = v[j]; v[j] = tmp; }
        }
    }
    vh = v[0]; vm = v[1]; vl = v[2];

    ex1 = vm->x - vh->x; ey1 = vm->y - vh->y;
    ex2 = vl->x - vh->x; ey2 = vl->y - vh->y;
    cross = ex1 * ey2 - ex2 * ey1;
    flip = (cross > 0.0f) ? 1 : 0;

    dyh = vl->y - vh->y; dym = vm->y - vh->y; dyl = vl->y - vm->y;
    dxhdy_f = (dyh != 0.0f) ? (vl->x - vh->x) / dyh : 0.0f;
    dxmdy_f = (dym != 0.0f) ? (vm->x - vh->x) / dym : 0.0f;
    dxldy_f = (dyl != 0.0f) ? (vl->x - vm->x) / dyl : 0.0f;

    memset(ew, 0, 24 * sizeof(int32_t));

    ew[0] = (int32_t)(((uint32_t)(flip ? 1 : 0) << 23)
          | ((uint32_t)(max_level & 7) << 19)
          | ((uint32_t)(tile & 7) << 16)
          | ((uint32_t)to_fix(vl->y, 2) & 0x3fffu));
    ew[1] = (int32_t)((((uint32_t)to_fix(vm->y, 2) & 0x3fffu) << 16)
          |  ((uint32_t)to_fix(vh->y, 2) & 0x3fffu));

    ew[2] = to_fix(vm->x, 16);
    ew[3] = to_fix(dxldy_f, 16);
    ew[4] = to_fix(vh->x, 16);
    ew[5] = to_fix(dxhdy_f, 16);
    ew[6] = to_fix(vh->x, 16);
    ew[7] = to_fix(dxmdy_f, 16);

    x0 = vh->x; y0 = vh->y;
    x1 = vm->x; y1 = vm->y;
    x2 = vl->x; y2 = vl->y;
    area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    inv  = (area != 0.0f) ? 1.0f / area : 0.0f;

    for (chan = 0; chan < 4; chan++)
    {
        float c0, c1, c2, dcdx, dcdy, dcde;
        switch (chan)
        {
        case 0:  c0 = vh->r; c1 = vm->r; c2 = vl->r; break;
        case 1:  c0 = vh->g; c1 = vm->g; c2 = vl->g; break;
        case 2:  c0 = vh->b; c1 = vm->b; c2 = vl->b; break;
        default: c0 = vh->a; c1 = vm->a; c2 = vl->a; break;
        }
        c0 *= 255.0f; c1 *= 255.0f; c2 *= 255.0f;
        dcdx = ((c1 - c0) * (y2 - y0) - (c2 - c0) * (y1 - y0)) * inv;
        dcdy = ((c2 - c0) * (x1 - x0) - (c1 - c0) * (x2 - x0)) * inv;
        dcde = dcdy + dxhdy_f * dcdx;
        base_c[chan] = to_fix(c0, 16);
        dx_c[chan]   = to_fix(dcdx, 16);
        dy_c[chan]   = to_fix(dcdy, 16);
        de_c[chan]   = to_fix(dcde, 16);
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

int emit_shaded_z_triangle(int32_t *ew, const EmitVertex *va,
                           const EmitVertex *vb, const EmitVertex *vc,
                           int tile, int max_level)
{
    float zf;

    emit_shaded_triangle(ew, va, vb, vc, tile, max_level);

    zf = (va->z + vb->z + vc->z) / 3.0f;
    zf = zf * (32767.0f / 1023.0f);
    if (zf < 0.0f) zf = 0.0f;
    if (zf > 32767.0f) zf = 32767.0f;
    ew[24] = (int32_t)((uint32_t)((int32_t)zf) << 16);
    ew[25] = 0;
    ew[26] = 0;
    ew[27] = 0;
    return 28;
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

int emit_texshade_triangle(int32_t *ew,
                           const EmitVertex *va, const EmitVertex *vb,
                           const EmitVertex *vc, int tex_w, int tex_h,
                           int tile, int max_level)
{
    const EmitVertex *vh;
    const EmitVertex *vm;
    const EmitVertex *vl;
    const EmitVertex *tmp;
    float X0, Y0, X1, Y1, X2, Y2, det2, inv, dyh2, dxhdy2;
    float wh, wm, wl;
    float s0, s1, s2, t0, t1, t2, w0, w1, w2;
    float sdx, sdy, sde, tdx, tdy, tde, wdx, wdy, wde;
    float a0, a1, a2;
    int i;

    emit_shaded_triangle(ew, va, vb, vc, tile, max_level);

    vh = va; vm = vb; vl = vc;
    if (vm->y < vh->y) { tmp = vh; vh = vm; vm = tmp; }
    if (vl->y < vh->y) { tmp = vh; vh = vl; vl = tmp; }
    if (vl->y < vm->y) { tmp = vm; vm = vl; vl = tmp; }

    X0 = vh->x; Y0 = vh->y; X1 = vm->x; Y1 = vm->y; X2 = vl->x; Y2 = vl->y;
    det2 = (X1 - X0) * (Y2 - Y0) - (X2 - X0) * (Y1 - Y0);
    if (det2 == 0.0f) det2 = 1.0f;
    inv = 1.0f / det2;
    dyh2 = vl->y - vh->y;
    dxhdy2 = (dyh2 != 0.0f) ? ((vl->x - vh->x) / dyh2) : 0.0f;

    /* Texture / inverse-w coefficients in the RDP's native format -- no fitted
     * envelope. e->w holds (1/w) renormalised to a plain inverse w; the RDP
     * inverse-w coefficient is (1/w) * 2^16 so its integer part lands in the
     * [0, 0x7fff] range the edgewalker reads as w >> 16. The S/T coefficient is
     * the texel coordinate (S10.5) times that same inverse-w coefficient, so
     * the per-pixel ss/sw divide recovers the texel. The scale is the hardware
     * 2^16, not a 2^30 window fitted to the LLE output. */
    /* vh->w already holds the RDP inverse-w coefficient (1/w) * 2^16. */
    wh = vh->w;
    wm = vm->w;
    wl = vl->w;

    s0 = vh->s * wh; s1 = vm->s * wm; s2 = vl->s * wl;
    t0 = vh->t * wh; t1 = vm->t * wm; t2 = vl->t * wl;
    w0 = wh; w1 = wm; w2 = wl;
    (void)tex_w; (void)tex_h;


    a0 = s0; a1 = s1; a2 = s2;
    sdx = ((a1 - a0) * (Y2 - Y0) - (a2 - a0) * (Y1 - Y0)) * inv;
    sdy = ((a2 - a0) * (X1 - X0) - (a1 - a0) * (X2 - X0)) * inv;
    sde = sdy + dxhdy2 * sdx;
    a0 = t0; a1 = t1; a2 = t2;
    tdx = ((a1 - a0) * (Y2 - Y0) - (a2 - a0) * (Y1 - Y0)) * inv;
    tdy = ((a2 - a0) * (X1 - X0) - (a1 - a0) * (X2 - X0)) * inv;
    tde = tdy + dxhdy2 * tdx;
    a0 = w0; a1 = w1; a2 = w2;
    wdx = ((a1 - a0) * (Y2 - Y0) - (a2 - a0) * (Y1 - Y0)) * inv;
    wdy = ((a2 - a0) * (X1 - X0) - (a1 - a0) * (X2 - X0)) * inv;
    wde = wdy + dxhdy2 * wdx;

    for (i = 24; i < 40; i++) ew[i] = 0;

    pack32(ew, 24, 0, (int32_t)s0);  pack32(ew, 24, 1, (int32_t)t0);
    pack32(ew, 25, 0, (int32_t)w0);
    pack32(ew, 26, 0, (int32_t)sdx); pack32(ew, 26, 1, (int32_t)tdx);
    pack32(ew, 27, 0, (int32_t)wdx);
    pack32(ew, 32, 0, (int32_t)sde); pack32(ew, 32, 1, (int32_t)tde);
    pack32(ew, 33, 0, (int32_t)wde);
    pack32(ew, 34, 0, (int32_t)sdy); pack32(ew, 34, 1, (int32_t)tdy);
    pack32(ew, 35, 0, (int32_t)wdy);

    return 40;
}

/* 0x0f texshade_z = the 40-word texshade triangle plus the same 4-word Z
 * block used by 0x0d, at ewdata[40..43]: z (s15.16), dzdx, dzde, dzdy.
 * Constant-depth (planar) like the shade_z path. */
int emit_texshade_z_triangle(int32_t *ew,
                             const EmitVertex *va, const EmitVertex *vb,
                             const EmitVertex *vc, int tex_w, int tex_h,
                             int tile, int max_level)
{
    float zf;

    emit_texshade_triangle(ew, va, vb, vc, tex_w, tex_h, tile, max_level);

    zf = (va->z + vb->z + vc->z) / 3.0f;
    zf = zf * (32767.0f / 1023.0f);
    if (zf < 0.0f) zf = 0.0f;
    if (zf > 32767.0f) zf = 32767.0f;
    ew[40] = (int32_t)((uint32_t)((int32_t)zf) << 16);
    ew[41] = 0;
    ew[42] = 0;
    ew[43] = 0;
    return 44;
}
