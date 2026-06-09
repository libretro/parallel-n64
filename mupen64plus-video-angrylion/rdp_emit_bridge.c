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
                         const BridgeViewport *vp)
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

    /* Inverse-w coefficient: the RDP coefficient is (1/w) * perspNorm in s1.30
     * (perspNorm = (1<<17)/(near+far), gSPPerspNormalize), so for a vertex at
     * the normalisation depth the integer part (w >> 16) lands in the tcdiv
     * reciprocal LUT domain. Verified against the cxd4 LLE stream: cxd4 W ~
     * 0x40000000 * pn/w; the previous (VRCP_estimate * pn) << 4 was 2^14 too
     * small (w >> 16 == 1), which sent every per-pixel S/W,T/W texture divide
     * out of range and flattened all textures. Computed at full precision
     * ((pn << 41) / cw, then << 8) -- the RSP's double-precision VRCPL. */
    {
        unsigned int pn = vp->persp_norm ? vp->persp_norm : 0xffffu;
        if (v->cw > 0)
            e->w = (int32_t)((((int64_t)(int)pn << 41) / v->cw) << 8);
        else
            e->w = 0;
    }

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

    clip_to_emit(&a, v0, vp);
    clip_to_emit(&b, v1, vp);
    clip_to_emit(&c, v2, vp);

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
