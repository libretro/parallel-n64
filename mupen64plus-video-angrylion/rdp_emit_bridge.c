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
    /* Perspective divide using the RSP's exact fixed-point reciprocal of w
     * (the div_ROM-table VRCP/VRCPL). The clip coords are s15.16; rsp_recip_div
     * returns (1/w) in the RSP's scaled reciprocal format (~2^15 magnitude, the
     * extra factor-of-2 the microcode applies). The ndc product is
     * (clip_fx * recip) >> 15, yielding a s15.16 ndc in [-1,+1] range. The
     * viewport map is then (ndc * vscale >> 16) + vtrans, all s15.16 -- exactly
     * the RSP's integer screen-coordinate generation, with NO float round-trip
     * (the float round-trip was the GLideN64-ism that broke the Z slope). */
    int32_t recip = rsp_recip_div(v->cw);
    int32_t ndcx  = (int32_t)(((int64_t)v->cx * recip) >> 15);
    int32_t ndcy  = (int32_t)(((int64_t)v->cy * recip) >> 15);
    int32_t ndcz  = (int32_t)(((int64_t)v->cz * recip) >> 15);

    e->x = (int32_t)(((int64_t)ndcx * vp->vscale_x) >> 16) + vp->vtrans_x;
    e->y = (int32_t)(((int64_t)ndcy * vp->vscale_y) >> 16) + vp->vtrans_y;
    e->z = (int32_t)(((int64_t)ndcz * vp->vscale_z) >> 16) + vp->vtrans_z;

    /* Inverse-w coefficient, perspNorm-scaled. recip is (1/w) * 2^15; the RSP
     * multiplies by the gSPPerspNormalize scale (vVpMisc[4]) so the stored
     * inverse-w lands in the upper range of the 15-bit tcdiv reciprocal LUT.
     * The <<4 brings it to the edgewalker's expected magnitude (matching the
     * prior path's ((recip*pn)<<4), now kept as a plain integer). */
    {
        unsigned int pn = vp->persp_norm ? vp->persp_norm : 0xffffu;
        e->w = (int32_t)(((int64_t)recip * (int64_t)(int)pn) << 4);
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
