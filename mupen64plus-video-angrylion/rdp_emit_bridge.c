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
#include "rdp_emit_rsp.h"

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
        /* Saturate the s15.16 NDC instead of letting the int32 cast wrap: a
         * vertex barely in front of the eye (tiny cw) projects to coordinates
         * far beyond the s15.16 range, and wrapped values would masquerade as
         * sane screen positions. */
        int64_t nx64 = ((int64_t)v->cx << 16) / v->cw;
        int64_t ny64 = ((int64_t)v->cy << 16) / v->cw;
        int64_t nz64 = ((int64_t)v->cz << 16) / v->cw;
        int64_t lim = (int64_t)1 << 30;
        if (nx64 >  lim) nx64 =  lim;
        if (nx64 < -lim) nx64 = -lim;
        if (ny64 >  lim) ny64 =  lim;
        if (ny64 < -lim) ny64 = -lim;
        if (nz64 >  lim) nz64 =  lim;
        if (nz64 < -lim) nz64 = -lim;
        ndcx = (int32_t)nx64;
        ndcy = (int32_t)ny64;
        ndcz = (int32_t)nz64;
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

    /* The RSP stores vertex screen coordinates as S13.2 -- quarter-pixel
     * precision -- and ALL of its edge and attribute setup happens on the
     * quantized values. Keeping the full s15.16 here made the emitted edge
     * slopes inconsistent with the .2-quantized YH/YM/YL fields the
     * rasterizer walks from (up to ~6% on short edges), so silhouette
     * edges overshot their true vertices by up to half a pixel: on OoT
     * actors a closer surface leaked single-pixel runs past its boundary
     * (hair past the hat/face seam -- the in-game "streak" artifact).
     * Truncate to the .25 grid before any setup; the oracle's coefficient
     * sets (xh == xm at the shared top vertex, dxm == (xl-xh)/(ym-yh)
     * exactly over the quantized deltas) confirm the RSP convention. */
    e->x = (int32_t)((uint32_t)e->x & ~0x3fffu);
    e->y = (int32_t)((uint32_t)e->y & ~0x3fffu);
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

    /* RSP-exact vertex transform: when in-domain it supersedes the screen
     * position computed above with the microcode's own MAC-chain results,
     * making the downstream triangle write bit-identical to the LLE RSP. */
    e->rsp_ok = 0;
    e->rsp_invw = 0;
    {
        int32_t sx, sy, sz, iw;
        if (rsp_vtx_screen(v->cx, v->cy, v->cz, v->cw,
                           (int32_t)(vp->persp_norm ? vp->persp_norm : 0xffffu),
                           vp->vscale_x >> 14, vp->vscale_y >> 14,
                           vp->vscale_z >> 16,
                           vp->vtrans_x >> 14, vp->vtrans_y >> 14,
                           vp->vtrans_z >> 16,
                           &sx, &sy, &sz, &iw))
        {
            e->x = sx << 14;
            e->y = sy << 14;
            e->z = sz;
            e->rsp_ok = 1;
            e->rsp_invw = iw;
        }
    }
}

/* RSP-exact path: convert the bridge vertices into the microcode's
 * per-vertex domain and run the transcribed triangle write. Returns the
 * word count (0 for a degenerate cull) or -1 when the inputs fall outside
 * the RSP's representable domain and the caller must use the legacy
 * emitters. */
static int try_rsp_tri_write(int32_t *cmd,
                             const EmitVertex *ea, const EmitVertex *eb,
                             const EmitVertex *ec,
                             const BridgeVertex *v0, const BridgeVertex *v1,
                             const BridgeVertex *v2,
                             int textured, int z_buffered, int smooth,
                             int tile, int level)
{
    RspTriVtx r[3];
    const EmitVertex *ev[3];
    const BridgeVertex *bv[3];
    int32_t bs, bt;
    int i;

    ev[0] = ea; ev[1] = eb; ev[2] = ec;
    bv[0] = v0; bv[1] = v1; bv[2] = v2;
    emit_get_st_bias(&bs, &bt);

    for (i = 0; i < 3; i++)
    {
        int32_t x102 = ev[i]->x >> 14;
        int32_t y102 = ev[i]->y >> 14;
        int32_t tcs = ((ev[i]->s >> 16) + bs) >> 1;
        int32_t tct = ((ev[i]->t >> 16) + bt) >> 1;
        if (!ev[i]->rsp_ok)
            return -1;
        if (x102 < -32768 || x102 > 32767 ||
            y102 < -32768 || y102 > 32767)
            return -1;
        if (textured &&
            (tcs < -32768 || tcs > 32767 || tct < -32768 || tct > 32767))
            return -1;
        r[i].x = (int16_t)x102;
        r[i].y = (int16_t)y102;
        r[i].z = ev[i]->z;
        r[i].r = (ev[i]->r >> 16) & 0xff;
        r[i].g = (ev[i]->g >> 16) & 0xff;
        r[i].b = (ev[i]->b >> 16) & 0xff;
        r[i].a = (ev[i]->a >> 16) & 0xff;
        r[i].s = tcs;
        r[i].t = tct;
        r[i].invw = ev[i]->rsp_invw;
    }
    return rsp_tri_write(cmd, &r[0], &r[1], &r[2],
                         textured, z_buffered, smooth, tile, level);
}

int bridge_add_triangle(int32_t *cmd,
                        const BridgeVertex *v0, const BridgeVertex *v1,
                        const BridgeVertex *v2,
                        const BridgeViewport *vp,
                        int textured, int z_buffered, int smooth,
                        int tile, int max_level, int tex_w, int tex_h)
{
    EmitVertex a, b, c;
    int n;
    int id;
    int64_t wa, wb, wc, wmax;

    clip_to_emit(&a, v0, vp, &wa);
    clip_to_emit(&b, v1, vp, &wb);
    clip_to_emit(&c, v2, vp, &wc);

    n = try_rsp_tri_write(cmd, &a, &b, &c, v0, v1, v2,
                          textured, z_buffered, smooth, tile, max_level);
    if (n >= 0)
        return n;

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
