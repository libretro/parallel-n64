/* rdp_emit_bridge.c -- self-contained C89 bridge from a geometry frontend
 * to the RDP command encoder (rdp_emit.c). MSVC-compatible ISO C89.
 *
 * bridge_add_triangle() takes three clip-space vertices, applies the N64 SP
 * viewport map (vscale/vtrans), preserves the per-vertex perspective W
 * (rhw = 1/w) for perspective-correct texturing, and calls the encoder to
 * produce the RDP triangle command angrylion rasterizes.
 *
 * Inert until a frontend calls it. Build check:
 *   gcc -std=c89 -pedantic -Wall -Wdeclaration-after-statement -Werror
 */

#include "rdp_emit.h"
#include "rdp_emit_bridge.h"
#include "rdp_emit_recip.h"

static void clip_to_emit(EmitVertex *e, const float *v, const BridgeViewport *vp)
{
    /* Perspective divide using the RSP's exact fixed-point reciprocal of w
     * (the div_ROM-table VRCP/VRCPL) rather than a float 1.0/w, so the screen
     * coordinates match the LLE path. The clip coordinates are s15.16; w is
     * converted back to s15.16, run through rsp_recip_div (whose result is
     * (1/w) scaled by 1/2, the RSP's reciprocal format), and the ndc product
     * is (clip * DivOut) >> 15 -- the >>16 to renormalise the s15.16*s15.16
     * product combined with the *2 the microcode applies to undo that 1/2. */
    int32_t cw_fx = (int32_t)(v[3] * 65536.0f);
    int32_t cx_fx = (int32_t)(v[0] * 65536.0f);
    int32_t cy_fx = (int32_t)(v[1] * 65536.0f);
    int32_t cz_fx = (int32_t)(v[2] * 65536.0f);
    int32_t recip = rsp_recip_div(cw_fx);
    float ndcx = (float)(((int64_t)cx_fx * recip) >> 15) / 65536.0f;
    float ndcy = (float)(((int64_t)cy_fx * recip) >> 15) / 65536.0f;
    float ndcz = (float)(((int64_t)cz_fx * recip) >> 15) / 65536.0f;
    e->x = ndcx * vp->vscale_x + vp->vtrans_x;
    e->y = ndcy * vp->vscale_y + vp->vtrans_y;
    e->z = ndcz * vp->vscale_z + vp->vtrans_z;
    /* Carry the RDP inverse-w coefficient directly: recip is (1/w) * 2^15
     * (the div_ROM reciprocal), so recip * 2 is (1/w) * 2^16, the coefficient
     * the edgewalker reads as w >> 16. No lossy round-trip through a tiny
     * float -- store the integer coefficient as the vertex's w. */
    e->w = (float)((int32_t)recip * 2);
    e->r = v[4]; e->g = v[5]; e->b = v[6]; e->a = v[7];
    e->s = v[8]; e->t = v[9];
}

int bridge_add_triangle(int32_t *cmd,
                        const float *v0, const float *v1, const float *v2,
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
