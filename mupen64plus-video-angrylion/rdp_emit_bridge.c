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

/* Clip-space near-plane threshold. A vertex with w below this sits on or
 * behind the camera plane; an edge from an in-front vertex (w >= W_NEAR) to
 * such a vertex is split at the plane so the in-front portion projects to a
 * bounded screen position instead of shooting off-screen through the divide. */
#define W_NEAR 0.01f

/* Linearly interpolate the 10 clip-space attributes (x,y,z,w,r,g,b,a,s,t)
 * between va and vb at parameter t into out. */
static void lerp_vtx10(float *out, const float *va, const float *vb, float t)
{
    int i;
    for (i = 0; i < 10; i++)
        out[i] = va[i] + (vb[i] - va[i]) * t;
}

/* Sutherland-Hodgman clip of the clip-space triangle (v0,v1,v2) against the
 * half-space w >= W_NEAR. Writes the resulting polygon (0, 3, or 4 vertices,
 * each 10 floats) into poly and returns the count. An all-inside triangle
 * returns its three vertices unchanged. */
static int clip_near_wspace(const float *v0, const float *v1, const float *v2,
                            float *poly)
{
    const float *in[3];
    int nout, i, k;
    in[0] = v0; in[1] = v1; in[2] = v2;
    nout = 0;
    for (i = 0; i < 3; i++)
    {
        const float *cur = in[i];
        const float *nxt = in[(i + 1) % 3];
        int cur_in = (cur[3] >= W_NEAR);
        int nxt_in = (nxt[3] >= W_NEAR);
        if (cur_in)
        {
            for (k = 0; k < 10; k++) poly[nout * 10 + k] = cur[k];
            nout++;
        }
        if (cur_in != nxt_in)
        {
            float t = (W_NEAR - cur[3]) / (nxt[3] - cur[3]);
            lerp_vtx10(&poly[nout * 10], cur, nxt, t);
            nout++;
        }
    }
    return nout;
}

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

/* Near-plane handling for the F3DZEX2.NoN pipeline. Three cases:
 *   - all three vertices in front (w >= W_NEAR): emit unchanged (fast path,
 *     bit-for-bit identical to bridge_add_triangle).
 *   - the triangle straddles the camera plane (some w >= W_NEAR, some below):
 *     clip the edges at the plane so the in-front portion projects to a
 *     bounded screen position, then fan-triangulate and emit. Without this the
 *     near-zero-w vertices project to screen coordinates thousands of pixels
 *     off-screen.
 *   - all three behind the plane (all w < W_NEAR): emit unchanged. NoN does not
 *     reject these; the rasterizer scissors them.
 * Writes up to two RDP triangle commands (each up to 44 words) into cmd and
 * returns the total word count. */
int bridge_add_triangle_clipped(int32_t *cmd,
                                const float *v0, const float *v1, const float *v2,
                                const BridgeViewport *vp,
                                int textured, int z_buffered,
                                int tile, int max_level, int tex_w, int tex_h)
{
    int in0 = (v0[3] >= W_NEAR);
    int in1 = (v1[3] >= W_NEAR);
    int in2 = (v2[3] >= W_NEAR);
    int nin = in0 + in1 + in2;

    if (nin == 3 || nin == 0)
        return bridge_add_triangle(cmd, v0, v1, v2, vp, textured, z_buffered,
                                   tile, max_level, tex_w, tex_h);

    {
        float poly[4 * 10];
        int np = clip_near_wspace(v0, v1, v2, poly);
        int total = 0, t;
        if (np < 3)
            return 0;
        for (t = 1; t + 1 < np; t++)
        {
            int n = bridge_add_triangle(cmd + total, &poly[0], &poly[t * 10],
                                        &poly[(t + 1) * 10], vp, textured,
                                        z_buffered, tile, max_level, tex_w, tex_h);
            total += n;
        }
        return total;
    }
}
