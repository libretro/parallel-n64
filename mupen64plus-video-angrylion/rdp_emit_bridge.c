/* rdp_emit_bridge.c -- self-contained C89 bridge from a geometry frontend
 * to the RDP command encoder (rdp_emit.c). MSVC-compatible ISO C89.
 *
 * bridge_add_triangle() takes three clip-space vertices (exact s15.16), applies
 * the N64 SP viewport map (vscale/vtrans, all s15.16), performs the perspective
 * divide with the RSP's exact fixed-point reciprocal of w, and preserves the
 * per-vertex inverse-w (rhw) coefficient. Integer-only: the RSP/RDP never use
 * floating point, so neither does this bridge.
 *
 * Build check:
 *   gcc -std=c89 -pedantic -Wall -Wdeclaration-after-statement -Werror
 */

#include "rdp_emit.h"
#include "rdp_emit_bridge.h"
#include "rdp_emit_recip.h"

static void clip_to_emit(EmitVertex *e, const BridgeVertex *v,
                         const BridgeViewport *vp, int64_t *w_raw)
{
    /* Perspective divide. The RSP uses the double-precision reciprocal (VRCPL,
     * refined from the VRCP div_ROM estimate) for the screen-coordinate divide,
     * which is full 32-bit precision. The single VRCP estimate (rsp_recip_div)
     * has only ~10-bit mantissa -- fine for X/Y (sub-pixel) but far too coarse
     * for the Z plane, where near-coplanar vertices differ only in the low bits
     * of z and the coarse reciprocal turned those differences into noise (the
     * scrambled-depth bug). Compute the NDC with the exact full-precision divide
     * ndc = (clip << 16) / cw (s15.16), matching VRCPL. cw>0 is guaranteed by
     * the caller's clip/cull (degenerate and w<=0 triangles are dropped). */
    int32_t ndcx, ndcy, ndcz;
    if (v->cw != 0)
    {
        ndcx = (int32_t)(((int64_t)v->cx << 16) / v->cw);
        ndcy = (int32_t)(((int64_t)v->cy << 16) / v->cw);
        ndcz = (int32_t)(((int64_t)v->cz << 16) / v->cw);
    }
    else
    {
        ndcx = ndcy = ndcz = 0;
    }

    e->x = (int32_t)(((int64_t)ndcx * vp->vscale_x) >> 16) + vp->vtrans_x;
    /* NDC +y points up (guPerspective), screen y grows downward and the Vp
     * vscale[1] is stored positive (View_ViewportToVp: height*2), so the RSP
     * screen map negates the y term: y = vtrans - ndc_y * vscale. Verified
     * against the cxd4 stream (single-pass triangle y sets match exactly). */
    e->y = vp->vtrans_y - (int32_t)(((int64_t)ndcy * vp->vscale_y) >> 16);
    e->z = (int32_t)(((int64_t)ndcz * vp->vscale_z) >> 16) + vp->vtrans_z;

    /* Inverse-w: hand the caller the full-precision (1/w) * perspNorm as a
     * 64-bit value, (pn << 41) / cw. The absolute scale of the RDP W
     * coefficient is free -- the rasterizer's tcdiv normalises the leading
     * bit of w and only the S:T:W ratios matter -- but its precision is not:
     * the RSP stores the reciprocal mantissa-normalised, so the emitted W
     * always lands near 2^30 regardless of the actual depth. Verified against
     * the cxd4 LLE stream: a pause-menu wall at w ~ 80 under perspNorm 10 and
     * an ortho HUD quad at w == 1 under perspNorm 0xFFFF both carry W ~
     * 0x40000000. A fixed post-shift (the previous << 8) is only correct for
     * one w * pn regime and overflowed int32 for the ortho HUD (negative
     * garbage W made the per-pixel texture divide collapse, e.g. the A-button
     * oval vanished). bridge_add_triangle normalises the triangle's three raw
     * values with one common shift instead. */
    {
        unsigned int pn = vp->persp_norm ? vp->persp_norm : 0xffffu;
        if (v->cw > 0)
            *w_raw = ((int64_t)(int)pn << 41) / v->cw;
        else
            *w_raw = 0;
    }
    e->w = 0; /* set by the caller after triangle-wide normalisation */

    e->r = v->r; e->g = v->g; e->b = v->b; e->a = v->a;
    e->s = v->s; e->t = v->t;
}

int bridge_add_triangle(int32_t *cmd,
                        const BridgeVertex *v0, const BridgeVertex *v1,
                        const BridgeVertex *v2,
                        const BridgeViewport *vp,
                        int textured, int z_buffered,
                        int tile, int max_level, int tex_w, int tex_h)
{
    EmitVertex a, b, c;
    int n;
    int id;
    int64_t wa, wb, wc, wmax;

    clip_to_emit(&a, v0, vp, &wa);
    clip_to_emit(&b, v1, vp, &wb);
    clip_to_emit(&c, v2, vp, &wc);

    /* Normalise the inverse-w trio with one common power-of-two shift so the
     * largest value lands in [2^30, 2^31), matching the RSP's mantissa-
     * normalised reciprocal. One shared shift keeps the three vertices --
     * and therefore the interpolated plane and the S/T coefficients derived
     * from them -- in a consistent scale. */
    wmax = wa;
    if (wb > wmax) wmax = wb;
    if (wc > wmax) wmax = wc;
    if (wmax > 0)
    {
        while (wmax >= ((int64_t)1 << 31))
        {
            wmax >>= 1; wa >>= 1; wb >>= 1; wc >>= 1;
        }
        while (wmax < ((int64_t)1 << 30))
        {
            wmax <<= 1; wa <<= 1; wb <<= 1; wc <<= 1;
        }
    }
    a.w = (int32_t)wa;
    b.w = (int32_t)wb;
    c.w = (int32_t)wc;

    if (textured && z_buffered)
        n = emit_texshade_z_triangle(cmd, &a, &b, &c, tex_w, tex_h, tile, max_level);
    else if (textured)
        n = emit_texshade_triangle(cmd, &a, &b, &c, tex_w, tex_h, tile, max_level);
    else if (z_buffered)
        n = emit_shaded_z_triangle(cmd, &a, &b, &c, tile, max_level);
    else
        n = emit_shaded_triangle(cmd, &a, &b, &c, tile, max_level);

    id = (textured && z_buffered) ? 0x0f
       : (textured ? 0x0e : (z_buffered ? 0x0d : 0x0c));
    cmd[0] = (int32_t)(((uint32_t)cmd[0] & 0x00ffffffu) | ((uint32_t)id << 24));
    return n;
}
