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

static void clip_to_emit(EmitVertex *e, const float *v, const BridgeViewport *vp)
{
    float cw = v[3];
    float rhw = (cw != 0.0f) ? (1.0f / cw) : 1.0f;
    float ndcx = v[0] * rhw;
    float ndcy = v[1] * rhw;
    float ndcz = v[2] * rhw;
    e->x = ndcx * vp->vscale_x + vp->vtrans_x;
    e->y = ndcy * vp->vscale_y + vp->vtrans_y;
    e->z = ndcz * vp->vscale_z + vp->vtrans_z;
    e->w = rhw;
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
